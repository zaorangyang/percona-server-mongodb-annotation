/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#define LOGV2_FOR_RECOVERY(ID, DLEVEL, MESSAGE, ...) \
    LOGV2_DEBUG_OPTIONS(ID, DLEVEL, {logv2::LogComponent::kStorageRecovery}, MESSAGE, ##__VA_ARGS__)
#define LOGV2_FOR_ROLLBACK(ID, DLEVEL, MESSAGE, ...) \
    LOGV2_DEBUG_OPTIONS(                             \
        ID, DLEVEL, {logv2::LogComponent::kReplicationRollback}, MESSAGE, ##__VA_ARGS__)

#include "mongo/platform/basic.h"

#ifdef _WIN32
#define NVALGRIND
#endif

#include <fmt/format.h>
#include <iomanip>
#include <memory>

#include "mongo/db/storage/wiredtiger/wiredtiger_kv_engine.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/error_code.hpp>
#include <fmt/format.h>
#include <valgrind/valgrind.h>

#include "mongo/base/error_codes.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/bson/dotted_path_support.h"
#include "mongo/db/catalog/collection.h"
#include "mongo/db/client.h"
#include "mongo/db/commands/server_status_metric.h"
#include "mongo/db/concurrency/locker.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/global_settings.h"
#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/repl/repl_settings.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/server_options.h"
#include "mongo/db/server_recovery.h"
#include "mongo/db/service_context.h"
#include "mongo/db/snapshot_window_options.h"
#include "mongo/db/storage/journal_listener.h"
#include "mongo/db/storage/storage_file_util.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/db/storage/storage_parameters_gen.h"
#include "mongo/db/storage/storage_repair_observer.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_cursor.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_customization_hooks.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_extensions.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_global_options.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_index.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_parameters_gen.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_record_store.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_recovery_unit.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_session_cache.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_size_storer.h"
#include "mongo/logv2/log.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/util/background.h"
#include "mongo/util/concurrency/idle_thread_block.h"
#include "mongo/util/concurrency/ticketholder.h"
#include "mongo/util/debug_util.h"
#include "mongo/util/exit.h"
#include "mongo/util/processinfo.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/time_support.h"

#if !defined(__has_feature)
#define __has_feature(x) 0
#endif

#if __has_feature(address_sanitizer)
const bool kAddressSanitizerEnabled = true;
#else
const bool kAddressSanitizerEnabled = false;
#endif

using namespace fmt::literals;

namespace mongo {

namespace {

MONGO_FAIL_POINT_DEFINE(WTPreserveSnapshotHistoryIndefinitely);
MONGO_FAIL_POINT_DEFINE(WTSetOldestTSToStableTS);

MONGO_FAIL_POINT_DEFINE(pauseCheckpointThread);
MONGO_FAIL_POINT_DEFINE(pauseJournalFlusherThread);

}  // namespace

bool WiredTigerFileVersion::shouldDowngrade(bool readOnly,
                                            bool repairMode,
                                            bool hasRecoveryTimestamp) {
    if (readOnly) {
        // A read-only state must not have upgraded. Nor could it downgrade.
        return false;
    }

    const auto replCoord = repl::ReplicationCoordinator::get(getGlobalServiceContext());
    const auto memberState = replCoord->getMemberState();
    if (memberState.arbiter()) {
        // SERVER-35361: Arbiters will no longer downgrade their data files. To downgrade
        // binaries, the user must delete the dbpath. It's not particularly expensive for a
        // replica set to re-initialize an arbiter that comes online.
        return false;
    }

    if (!serverGlobalParams.featureCompatibility.isVersionInitialized()) {
        // If the FCV document hasn't been read, trust the WT compatibility. MongoD will
        // downgrade to the same compatibility it discovered on startup.
        return _startupVersion == StartupVersion::IS_44_FCV_42 ||
            _startupVersion == StartupVersion::IS_42;
    }

    if (serverGlobalParams.featureCompatibility.getVersion() !=
        ServerGlobalParams::FeatureCompatibility::Version::kFullyDowngradedTo42) {
        // Only consider downgrading when FCV is set to 4.2
        return false;
    }

    if (getGlobalReplSettings().usingReplSets()) {
        // If this process is run with `--replSet`, it must have run any startup replication
        // recovery and downgrading at this point is safe.
        return true;
    }

    if (hasRecoveryTimestamp) {
        // If we're not running with `--replSet`, don't allow downgrades if the node needed to run
        // replication recovery. Having a recovery timestamp implies recovery must be run, but it
        // was not.
        return false;
    }

    // If there is no `recoveryTimestamp`, then the data should be consistent with the top of
    // oplog and downgrading can proceed. This is expected for standalone datasets that use FCV.
    return true;
}

std::string WiredTigerFileVersion::getDowngradeString() {
    if (!serverGlobalParams.featureCompatibility.isVersionInitialized()) {
        invariant(_startupVersion != StartupVersion::IS_44_FCV_44);

        switch (_startupVersion) {
            case StartupVersion::IS_44_FCV_42:
                return "compatibility=(release=3.3)";
            case StartupVersion::IS_42:
                return "compatibility=(release=3.3)";
            default:
                MONGO_UNREACHABLE;
        }
    }
    return "compatibility=(release=3.3)";
}

using std::set;
using std::string;

namespace dps = ::mongo::dotted_path_support;

class WiredTigerKVEngine::WiredTigerSessionSweeper : public BackgroundJob {
public:
    explicit WiredTigerSessionSweeper(WiredTigerSessionCache* sessionCache)
        : BackgroundJob(false /* deleteSelf */), _sessionCache(sessionCache) {}

    virtual string name() const {
        return "WTIdleSessionSweeper";
    }

    virtual void run() {
        ThreadClient tc(name(), getGlobalServiceContext());
        LOGV2_DEBUG(22303, 1, "starting {name} thread", "name"_attr = name());

        while (!_shuttingDown.load()) {
            {
                stdx::unique_lock<Latch> lock(_mutex);
                MONGO_IDLE_THREAD_BLOCK;
                // Check every 10 seconds or sooner in the debug builds
                _condvar.wait_for(lock, stdx::chrono::seconds(kDebugBuild ? 1 : 10));
            }

            _sessionCache->closeExpiredIdleSessions(gWiredTigerSessionCloseIdleTimeSecs.load() *
                                                    1000);
        }
        LOGV2_DEBUG(22304, 1, "stopping {name} thread", "name"_attr = name());
    }

    void shutdown() {
        _shuttingDown.store(true);
        {
            stdx::unique_lock<Latch> lock(_mutex);
            // Wake up the session sweeper thread early, we do not want the shutdown
            // to wait for us too long.
            _condvar.notify_one();
        }
        wait();
    }

private:
    WiredTigerSessionCache* _sessionCache;
    AtomicWord<bool> _shuttingDown{false};

    Mutex _mutex = MONGO_MAKE_LATCH("WiredTigerSessionSweeper::_mutex");  // protects _condvar
    // The session sweeper thread idles on this condition variable for a particular time duration
    // between cleaning up expired sessions. It can be triggered early to expediate shutdown.
    stdx::condition_variable _condvar;
};

class WiredTigerKVEngine::WiredTigerJournalFlusher : public BackgroundJob {
public:
    explicit WiredTigerJournalFlusher(WiredTigerSessionCache* sessionCache)
        : BackgroundJob(false /* deleteSelf */), _sessionCache(sessionCache) {}

    virtual string name() const {
        return "WTJournalFlusher";
    }

    virtual void run() {
        ThreadClient tc(name(), getGlobalServiceContext());
        LOGV2_DEBUG(22305, 1, "starting {name} thread", "name"_attr = name());

        // Initialize the thread's opCtx.
        _uniqueCtx.emplace(tc->makeOperationContext());

        // Updates to a non-replicated collection, oplogTruncateAfterPoint, are made by this thread.
        // Non-replicated writes will not contribute to replication lag and can be safely excluded
        // from Flow Control.
        _uniqueCtx->get()->setShouldParticipateInFlowControl(false);
        while (true) {

            pauseJournalFlusherThread.pauseWhileSet(_uniqueCtx->get());

            try {
                ON_BLOCK_EXIT([&] {
                    // We do not want to miss an interrupt for the next round. Therefore, the opCtx
                    // will be reset after a flushing round finishes.
                    //
                    // It is fine if the opCtx is signaled between finishing and resetting because
                    // state changes will be seen before the next round. We want to catch any
                    // interrupt signals that occur after state is checked at the start of a round:
                    // the time during or before the next flush.
                    stdx::lock_guard<Latch> lk(_opCtxMutex);
                    _uniqueCtx.reset();
                    _uniqueCtx.emplace(tc->makeOperationContext());
                    _uniqueCtx->get()->setShouldParticipateInFlowControl(false);
                });

                _sessionCache->waitUntilDurable(
                    _uniqueCtx->get(),
                    WiredTigerSessionCache::Fsync::kJournal,
                    WiredTigerSessionCache::UseJournalListener::kUpdate);

                // Signal the waiters that a round completed.
                _currentSharedPromise->emplaceValue();
            } catch (const AssertionException& e) {
                invariant(ErrorCodes::isShutdownError(e.code()) ||
                              e.code() == ErrorCodes::InterruptedDueToReplStateChange,
                          e.toString());

                // Signal the waiters that the fsync was interrupted.
                _currentSharedPromise->setError(e.toStatus());
            }

            // Wait until either journalCommitIntervalMs passes or an immediate journal flush is
            // requested (or shutdown).

            auto deadline =
                Date_t::now() + Milliseconds(storageGlobalParams.journalCommitIntervalMs.load());

            stdx::unique_lock<Latch> lk(_stateMutex);

            MONGO_IDLE_THREAD_BLOCK;
            _flushJournalNowCV.wait_until(lk, deadline.toSystemTimePoint(), [&] {
                return _flushJournalNow || _shuttingDown;
            });

            _flushJournalNow = false;

            if (_shuttingDown) {
                LOGV2_DEBUG(22306, 1, "stopping {name} thread", "name"_attr = name());
                _nextSharedPromise->setError(
                    Status(ErrorCodes::ShutdownInProgress, "The storage catalog is being closed."));
                stdx::lock_guard<Latch> lk(_opCtxMutex);
                _uniqueCtx.reset();
                return;
            }

            // Take the next promise as current and reset the next promise.
            _currentSharedPromise =
                std::exchange(_nextSharedPromise, std::make_unique<SharedPromise<void>>());
        }
    }

    /**
     * Signals the thread to quit and then waits until it does.
     */
    void shutdown() {
        {
            stdx::lock_guard<Latch> lk(_stateMutex);
            _shuttingDown = true;
            _flushJournalNowCV.notify_one();
        }
        wait();
    }

    /**
     * Signals an immediate journal flush and leaves.
     */
    void triggerJournalFlush() {
        stdx::lock_guard<Latch> lk(_stateMutex);
        if (!_flushJournalNow) {
            _flushJournalNow = true;
            _flushJournalNowCV.notify_one();
        }
    }

    /**
     * Signals an immediate journal flush and waits for it to complete before returning.
     *
     * Will throw ShutdownInProgress if the flusher thread is being stopped.
     * Will throw InterruptedDueToReplStateChange if a flusher round is interrupted by stepdown.
     */
    void waitForJournalFlush() {
        auto myFuture = [&]() {
            stdx::unique_lock<Latch> lk(_stateMutex);
            if (!_flushJournalNow) {
                _flushJournalNow = true;
                _flushJournalNowCV.notify_one();
            }
            return _nextSharedPromise->getFuture();
        }();
        // Throws on error if the catalog is closed or the flusher round is interrupted by stepdown.
        myFuture.get();
    }

    /**
     * Interrupts the journal flusher thread via its operation context with an
     * InterruptedDueToReplStateChange error.
     */
    void interruptJournalFlusherForReplStateChange() {
        stdx::lock_guard<Latch> lk(_opCtxMutex);
        if (_uniqueCtx) {
            stdx::lock_guard<Client> lk(*_uniqueCtx->get()->getClient());
            _uniqueCtx->get()->markKilled(ErrorCodes::InterruptedDueToReplStateChange);
        }
    }

private:
    WiredTigerSessionCache* _sessionCache;

    // Serializes setting/resetting _uniqueCtx and marking _uniqueCtx killed.
    mutable Mutex _opCtxMutex = MONGO_MAKE_LATCH("WiredTigerJournalFlusherOpCtxMutex");

    // Saves a reference to the flusher thread's operation context so it can be interrupted if the
    // flusher is active.
    boost::optional<ServiceContext::UniqueOperationContext> _uniqueCtx;

    // Protects the state below.
    mutable Mutex _stateMutex = MONGO_MAKE_LATCH("WiredTigerJournalFlusherStateMutex");

    // Signaled to wake up the thread, if the thread is waiting. The thread will check whether
    // _flushJournalNow or _shuttingDown is set and flush or stop accordingly.
    mutable stdx::condition_variable _flushJournalNowCV;

    bool _flushJournalNow = false;
    bool _shuttingDown = false;

    // New callers get a future from nextSharedPromise. The JournalFlusher thread will swap that to
    // currentSharedPromise at the start of every round of flushing, and reset nextSharedPromise
    // with a new shared promise.
    std::unique_ptr<SharedPromise<void>> _currentSharedPromise =
        std::make_unique<SharedPromise<void>>();
    std::unique_ptr<SharedPromise<void>> _nextSharedPromise =
        std::make_unique<SharedPromise<void>>();
};

namespace {

/**
 * RAII class that holds an exclusive lock on the checkpoint resource mutex.
 *
 * Instances are created via getCheckpointLock(), which passes in the checkpoint resource mutex.
 */
class CheckpointLockImpl : public StorageEngine::CheckpointLock {
    CheckpointLockImpl(const CheckpointLockImpl&) = delete;
    CheckpointLockImpl& operator=(const CheckpointLockImpl&) = delete;
    CheckpointLockImpl(CheckpointLockImpl&& other) = delete;

public:
    CheckpointLockImpl() = delete;
    CheckpointLockImpl(OperationContext* opCtx, Lock::ResourceMutex mutex)
        : _lk(opCtx->lockState(), mutex) {
        invariant(_lk.isLocked());
    }

    ~CheckpointLockImpl() = default;

private:
    Lock::ExclusiveLock _lk;
};

}  // namespace

std::string toString(const StorageEngine::OldestActiveTransactionTimestampResult& r) {
    if (r.isOK()) {
        if (r.getValue()) {
            // Timestamp.
            return r.getValue().value().toString();
        } else {
            // boost::none.
            return "null";
        }
    } else {
        return r.getStatus().toString();
    }
}

class WiredTigerKVEngine::WiredTigerCheckpointThread : public BackgroundJob {
public:
    explicit WiredTigerCheckpointThread(WiredTigerKVEngine* wiredTigerKVEngine,
                                        WiredTigerSessionCache* sessionCache)
        : BackgroundJob(false /* deleteSelf */),
          _wiredTigerKVEngine(wiredTigerKVEngine),
          _sessionCache(sessionCache) {}

    virtual string name() const {
        return "WTCheckpointThread";
    }

    virtual void run() {
        ThreadClient tc(name(), getGlobalServiceContext());
        LOGV2_DEBUG(22307, 1, "starting {name} thread", "name"_attr = name());

        while (!_shuttingDown.load()) {
            auto opCtx = tc->makeOperationContext();

            {
                stdx::unique_lock<Latch> lock(_mutex);
                MONGO_IDLE_THREAD_BLOCK;
                _condvar.wait_for(lock,
                                  stdx::chrono::seconds(static_cast<std::int64_t>(
                                      wiredTigerGlobalOptions.checkpointDelaySecs)));
            }

            pauseCheckpointThread.pauseWhileSet();

            // Might have been awakened by another thread shutting us down.
            if (_shuttingDown.load()) {
                break;
            }

            const Date_t startTime = Date_t::now();

            const Timestamp stableTimestamp = _wiredTigerKVEngine->getStableTimestamp();
            const Timestamp initialDataTimestamp = _wiredTigerKVEngine->getInitialDataTimestamp();

            // The amount of oplog to keep is primarily dictated by a user setting. However, in
            // unexpected cases, durable, recover to a timestamp storage engines may need to play
            // forward from an oplog entry that would otherwise be truncated by the user
            // setting. Furthermore, the entries in prepared or large transactions can refer to
            // previous entries in the same transaction.
            //
            // Live (replication) rollback will replay oplogs from exactly the stable timestamp.
            // With prepared or large transactions, it may require some additional entries prior to
            // the stable timestamp. These requirements are summarized in getOplogNeededForRollback.
            // Truncating the oplog at this point is sufficient for in-memory configurations, but
            // could cause an unrecoverable scenario if the node crashed and has to play from the
            // last stable checkpoint.
            //
            // By recording the oplog needed for rollback "now", then taking a stable checkpoint,
            // we can safely assume that the oplog needed for crash recovery has caught up to the
            // recorded value. After the checkpoint, this value will be published such that actors
            // which truncate the oplog can read an updated value.
            try {
                // Three cases:
                //
                // First, initialDataTimestamp is Timestamp(0, 1) -> Take full checkpoint. This is
                // when there is no consistent view of the data (i.e: during initial sync).
                //
                // Second, stableTimestamp < initialDataTimestamp: Skip checkpoints. The data on
                // disk is prone to being rolled back. Hold off on checkpoints.  Hope that the
                // stable timestamp surpasses the data on disk, allowing storage to persist newer
                // copies to disk.
                //
                // Third, stableTimestamp >= initialDataTimestamp: Take stable checkpoint. Steady
                // state case.
                if (initialDataTimestamp.asULL() <= 1) {
                    UniqueWiredTigerSession session = _sessionCache->getSession();
                    WT_SESSION* s = session->getSession();
                    auto checkpointLock = _wiredTigerKVEngine->getCheckpointLock(opCtx.get());
                    _wiredTigerKVEngine->clearIndividuallyCheckpointedIndexesList();
                    invariantWTOK(s->checkpoint(s, "use_timestamp=false"));
                } else if (stableTimestamp < initialDataTimestamp) {
                    LOGV2_FOR_RECOVERY(
                        23985,
                        2,
                        "Stable timestamp is behind the initial data timestamp, skipping "
                        "a checkpoint. StableTimestamp: {stableTimestamp} InitialDataTimestamp: "
                        "{initialDataTimestamp}",
                        "stableTimestamp"_attr = stableTimestamp.toString(),
                        "initialDataTimestamp"_attr = initialDataTimestamp.toString());
                } else {
                    auto oplogNeededForRollback = _wiredTigerKVEngine->getOplogNeededForRollback();

                    LOGV2_FOR_RECOVERY(
                        23986,
                        2,
                        "Performing stable checkpoint. StableTimestamp: {stableTimestamp}, "
                        "OplogNeededForRollback: {oplogNeededForRollback}",
                        "stableTimestamp"_attr = stableTimestamp,
                        "oplogNeededForRollback"_attr = toString(oplogNeededForRollback));

                    UniqueWiredTigerSession session = _sessionCache->getSession();
                    WT_SESSION* s = session->getSession();
                    {
                        auto checkpointLock = _wiredTigerKVEngine->getCheckpointLock(opCtx.get());
                        _wiredTigerKVEngine->clearIndividuallyCheckpointedIndexesList();
                        invariantWTOK(s->checkpoint(s, "use_timestamp=true"));
                    }

                    if (oplogNeededForRollback.isOK()) {
                        // Now that the checkpoint is durable, publish the oplog needed to recover
                        // from it.
                        stdx::lock_guard<Latch> lk(_oplogNeededForCrashRecoveryMutex);
                        _oplogNeededForCrashRecovery.store(
                            oplogNeededForRollback.getValue().asULL());
                    }
                }

                const auto secondsElapsed = durationCount<Seconds>(Date_t::now() - startTime);
                if (secondsElapsed >= 30) {
                    LOGV2_DEBUG(22308,
                                1,
                                "Checkpoint took {secondsElapsed} seconds to complete.",
                                "secondsElapsed"_attr = secondsElapsed);
                }
            } catch (const WriteConflictException&) {
                // Temporary: remove this after WT-3483
                LOGV2_WARNING(22346, "Checkpoint encountered a write conflict exception.");
            } catch (const AssertionException& exc) {
                invariant(ErrorCodes::isShutdownError(exc.code()), exc.what());
            }
        }
        LOGV2_DEBUG(22309, 1, "stopping {name} thread", "name"_attr = name());
    }

    /**
     * Returns true if we have already triggered taking the first checkpoint.
     */
    bool hasTriggeredFirstStableCheckpoint() {
        return _hasTriggeredFirstStableCheckpoint;
    }

    /**
     * Triggers taking the first stable checkpoint, which is when the stable timestamp advances past
     * the initial data timestamp.
     *
     * The checkpoint thread runs automatically every wiredTigerGlobalOptions.checkpointDelaySecs
     * seconds. This function avoids potentially waiting that full duration for a stable checkpoint,
     * initiating one immediately.
     *
     * Do not call this function if hasTriggeredFirstStableCheckpoint() returns true.
     */
    void triggerFirstStableCheckpoint(Timestamp prevStable,
                                      Timestamp initialData,
                                      Timestamp currStable) {
        invariant(!_hasTriggeredFirstStableCheckpoint);
        if (prevStable < initialData && currStable >= initialData) {
            _hasTriggeredFirstStableCheckpoint = true;
            LOGV2(22310,
                  "Triggering the first stable checkpoint. Initial Data: {initialData} PrevStable: "
                  "{prevStable} CurrStable: {currStable}",
                  "initialData"_attr = initialData,
                  "prevStable"_attr = prevStable,
                  "currStable"_attr = currStable);
            stdx::unique_lock<Latch> lock(_mutex);
            _condvar.notify_one();
        }
    }

    std::uint64_t getOplogNeededForCrashRecovery() const {
        return _oplogNeededForCrashRecovery.load();
    }

    /*
     * Atomically assign _oplogNeededForCrashRecovery to a variable.
     * _oplogNeededForCrashRecovery will not change during assignment.
     */
    void assignOplogNeededForCrashRecoveryTo(boost::optional<Timestamp>* timestamp) {
        stdx::lock_guard<Latch> lk(_oplogNeededForCrashRecoveryMutex);
        *timestamp = Timestamp(_oplogNeededForCrashRecovery.load());
    }

    void shutdown() {
        _shuttingDown.store(true);
        {
            stdx::unique_lock<Latch> lock(_mutex);
            // Wake up the checkpoint thread early, to take a final checkpoint before shutting
            // down, if one has not coincidentally just been taken.
            _condvar.notify_one();
        }
        wait();
    }

private:
    WiredTigerKVEngine* _wiredTigerKVEngine;
    WiredTigerSessionCache* _sessionCache;

    Mutex _mutex = MONGO_MAKE_LATCH("WiredTigerCheckpointThread::_mutex");
    ;  // protects _condvar
    // The checkpoint thread idles on this condition variable for a particular time duration between
    // taking checkpoints. It can be triggered early to expediate immediate checkpointing.
    stdx::condition_variable _condvar;

    AtomicWord<bool> _shuttingDown{false};

    bool _hasTriggeredFirstStableCheckpoint = false;

    Mutex _oplogNeededForCrashRecoveryMutex =
        MONGO_MAKE_LATCH("WiredTigerCheckpointThread::_oplogNeededForCrashRecoveryMutex");
    AtomicWord<std::uint64_t> _oplogNeededForCrashRecovery;
};

namespace {
TicketHolder openWriteTransaction(128);
TicketHolder openReadTransaction(128);
}  // namespace

OpenWriteTransactionParam::OpenWriteTransactionParam(StringData name, ServerParameterType spt)
    : ServerParameter(name, spt), _data(&openWriteTransaction) {}

void OpenWriteTransactionParam::append(OperationContext* opCtx,
                                       BSONObjBuilder& b,
                                       const std::string& name) {
    b.append(name, _data->outof());
}

Status OpenWriteTransactionParam::setFromString(const std::string& str) {
    int num = 0;
    Status status = NumberParser{}(str, &num);
    if (!status.isOK()) {
        return status;
    }
    if (num <= 0) {
        return {ErrorCodes::BadValue, str::stream() << name() << " has to be > 0"};
    }
    return _data->resize(num);
}

OpenReadTransactionParam::OpenReadTransactionParam(StringData name, ServerParameterType spt)
    : ServerParameter(name, spt), _data(&openReadTransaction) {}

void OpenReadTransactionParam::append(OperationContext* opCtx,
                                      BSONObjBuilder& b,
                                      const std::string& name) {
    b.append(name, _data->outof());
}

Status OpenReadTransactionParam::setFromString(const std::string& str) {
    int num = 0;
    Status status = NumberParser{}(str, &num);
    if (!status.isOK()) {
        return status;
    }
    if (num <= 0) {
        return {ErrorCodes::BadValue, str::stream() << name() << " has to be > 0"};
    }
    return _data->resize(num);
}

namespace {

StatusWith<StorageEngine::BackupInformation> getBackupInformationFromBackupCursor(
    WT_SESSION* session,
    WT_CURSOR* cursor,
    bool incrementalBackup,
    bool fullBackup,
    std::string dbPath,
    const char* statusPrefix) {
    int wtRet;
    StorageEngine::BackupInformation backupInformation;
    const char* filename;
    const auto directoryPath = boost::filesystem::path(dbPath);
    const auto wiredTigerLogFilePrefix = "WiredTigerLog";
    while ((wtRet = cursor->next(cursor)) == 0) {
        invariantWTOK(cursor->get_key(cursor, &filename));

        std::string name(filename);

        boost::filesystem::path filePath = directoryPath;
        if (name.find(wiredTigerLogFilePrefix) == 0) {
            // TODO SERVER-13455:replace `journal/` with the configurable journal path.
            filePath /= boost::filesystem::path("journal");
        }
        filePath /= name;

        boost::system::error_code errorCode;
        const std::uint64_t fileSize = boost::filesystem::file_size(filePath, errorCode);
        uassert(31403,
                "Failed to get a file's size. Filename: {} Error: {}"_format(filePath.string(),
                                                                             errorCode.message()),
                !errorCode);

        StorageEngine::BackupFile backupFile(fileSize);
        backupInformation.insert({filePath.string(), backupFile});

        // For the first full incremental backup, include the offset and length.
        if (incrementalBackup && fullBackup) {
            backupInformation.at(filePath.string()).blocksToCopy.push_back({0, fileSize});
        }

        // Full backups cannot open an incremental cursor, even if they are the first full backup
        // for incremental.
        if (!incrementalBackup || fullBackup) {
            continue;
        }

        // For each file listed, open a duplicate backup cursor and get the blocks to copy.
        std::stringstream ss;
        ss << "incremental=(file=" << filename << ")";
        const std::string config = ss.str();
        WT_CURSOR* dupCursor;
        wtRet = session->open_cursor(session, nullptr, cursor, config.c_str(), &dupCursor);
        if (wtRet != 0) {
            return wtRCToStatus(wtRet);
        }

        while ((wtRet = dupCursor->next(dupCursor)) == 0) {
            uint64_t offset, size, type;
            invariantWTOK(dupCursor->get_key(dupCursor, &offset, &size, &type));
            LOGV2_DEBUG(22311,
                        2,
                        "Block to copy for incremental backup: filename: {filePath_string}, "
                        "offset: {offset}, size: {size}, type: {type}",
                        "filePath_string"_attr = filePath.string(),
                        "offset"_attr = offset,
                        "size"_attr = size,
                        "type"_attr = type);
            backupInformation.at(filePath.string()).blocksToCopy.push_back({offset, size});
        }

        if (wtRet != WT_NOTFOUND) {
            return wtRCToStatus(wtRet);
        }

        wtRet = dupCursor->close(dupCursor);
        if (wtRet != 0) {
            return wtRCToStatus(wtRet);
        }
    }

    if (wtRet != WT_NOTFOUND) {
        return wtRCToStatus(wtRet, statusPrefix);
    }
    return backupInformation;
}

}  // namespace

StringData WiredTigerKVEngine::kTableUriPrefix = "table:"_sd;

WiredTigerKVEngine::WiredTigerKVEngine(const std::string& canonicalName,
                                       const std::string& path,
                                       ClockSource* cs,
                                       const std::string& extraOpenOptions,
                                       size_t cacheSizeMB,
                                       size_t maxCacheOverflowFileSizeMB,
                                       bool durable,
                                       bool ephemeral,
                                       bool repair,
                                       bool readOnly)
    : _clockSource(cs),
      _oplogManager(std::make_unique<WiredTigerOplogManager>()),
      _canonicalName(canonicalName),
      _path(path),
      _sizeStorerSyncTracker(cs, 100000, Seconds(60)),
      _durable(durable),
      _ephemeral(ephemeral),
      _inRepairMode(repair),
      _readOnly(readOnly),
      _keepDataHistory(serverGlobalParams.enableMajorityReadConcern) {
    boost::filesystem::path journalPath = path;
    journalPath /= "journal";
    if (_durable) {
        if (!boost::filesystem::exists(journalPath)) {
            try {
                boost::filesystem::create_directory(journalPath);
            } catch (std::exception& e) {
                LOGV2(22312,
                      "error creating journal dir {journalPath_string} {e_what}",
                      "journalPath_string"_attr = journalPath.string(),
                      "e_what"_attr = e.what());
                throw;
            }
        }
    }

    _previousCheckedDropsQueued = _clockSource->now();

    std::stringstream ss;
    ss << "create,";
    ss << "cache_size=" << cacheSizeMB << "M,";
    ss << "cache_overflow=(file_max=" << maxCacheOverflowFileSizeMB << "M),";
    ss << "session_max=33000,";
    ss << "eviction=(threads_min=4,threads_max=4),";
    ss << "config_base=false,";
    ss << "statistics=(fast),";

    if (!WiredTigerSessionCache::isEngineCachingCursors()) {
        ss << "cache_cursors=false,";
    }

    // The setting may have a later setting override it if not using the journal.  We make it
    // unconditional here because even nojournal may need this setting if it is a transition
    // from using the journal.
    ss << "log=(enabled=true,archive=" << (_readOnly ? "false" : "true")
       << ",path=journal,compressor=";
    ss << wiredTigerGlobalOptions.journalCompressor << "),";
    ss << "file_manager=(close_idle_time=" << gWiredTigerFileHandleCloseIdleTime
       << ",close_scan_interval=" << gWiredTigerFileHandleCloseScanInterval
       << ",close_handle_minimum=" << gWiredTigerFileHandleCloseMinimum << "),";
    ss << "statistics_log=(wait=" << wiredTigerGlobalOptions.statisticsLogDelaySecs << "),";

    if (shouldLog(::mongo::logv2::LogComponent::kStorageRecovery, logv2::LogSeverity::Debug(3))) {
        ss << "verbose=[recovery_progress,checkpoint_progress,compact_progress,recovery],";
    } else {
        ss << "verbose=[recovery_progress,checkpoint_progress,compact_progress],";
    }

    if (kDebugBuild) {
        // Enable debug write-ahead logging for all tables under debug build.
        ss << "debug_mode=(table_logging=true,";
        // For select debug builds, support enabling WiredTiger eviction debug mode. This uses
        // more aggressive eviction tactics, but may have a negative performance impact.
        if (gWiredTigerEvictionDebugMode) {
            ss << "eviction=true,";
        }
        ss << "),";
    }
    if (kAddressSanitizerEnabled) {
        // For applications using WT, advancing a cursor invalidates the data/memory that cursor was
        // pointing to. WT performs the optimization of managing its own memory. The unit of memory
        // allocation is a page. Walking a cursor from one key/value to the next often lands on the
        // same page, which has the effect of keeping the address of the prior key/value valid. For
        // a bug to occur, the cursor must move across pages, and the prior page must be
        // evicted. While rare, this can happen, resulting in reading random memory.
        //
        // The cursor copy debug mode will instead cause WT to malloc/free memory for each key/value
        // a cursor is positioned on. Thus, enabling when using with address sanitizer will catch
        // many cases of dereferencing invalid cursor positions. Note, there is a known caveat: a
        // free/malloc for roughly the same allocation size can often return the same memory
        // address. This is a scenario where the address sanitizer is not able to detect a
        // use-after-free error.
        ss << "debug_mode=(cursor_copy=true),";
    }

    ss << WiredTigerCustomizationHooks::get(getGlobalServiceContext())
              ->getTableCreateConfig("system");
    ss << WiredTigerExtensions::get(getGlobalServiceContext())->getOpenExtensionsConfig();
    ss << extraOpenOptions;

    if (!_durable) {
        // If we started without the journal, but previously used the journal then open with the
        // WT log enabled to perform any unclean shutdown recovery and then close and reopen in
        // the normal path without the journal.
        if (boost::filesystem::exists(journalPath)) {
            string config = ss.str();
            LOGV2(22313,
                  "Detected WT journal files. Running recovery from last checkpoint. journal to "
                  "nojournal transition config",
                  "config"_attr = config);
            int ret = wiredtiger_open(
                path.c_str(), _eventHandler.getWtEventHandler(), config.c_str(), &_conn);
            if (ret == EINVAL) {
                fassertFailedNoTrace(28717);
            } else if (ret != 0) {
                Status s(wtRCToStatus(ret));
                msgasserted(28718, s.reason());
            }
            invariantWTOK(_conn->close(_conn, nullptr));
            // After successful recovery, remove the journal directory.
            try {
                boost::filesystem::remove_all(journalPath);
            } catch (std::exception& e) {
                LOGV2_ERROR(22355,
                            "error removing journal dir {journalPath_string} {e_what}",
                            "journalPath_string"_attr = journalPath.string(),
                            "e_what"_attr = e.what());
                throw;
            }
        }
        // This setting overrides the earlier setting because it is later in the config string.
        ss << ",log=(enabled=false),";
    }

    string config = ss.str();
    LOGV2(22315, "wiredtiger_open config", "config"_attr = config);
    _openWiredTiger(path, config);
    _eventHandler.setStartupSuccessful();
    _wtOpenConfig = config;

    {
        char buf[(2 * 8 /*bytes in hex*/) + 1 /*nul terminator*/];
        invariantWTOK(_conn->query_timestamp(_conn, buf, "get=recovery"));

        std::uint64_t tmp;
        fassert(50758, NumberParser().base(16)(buf, &tmp));
        _recoveryTimestamp = Timestamp(tmp);
        LOGV2_FOR_RECOVERY(23987,
                           0,
                           "WiredTiger recoveryTimestamp",
                           "recoveryTimestamp"_attr = _recoveryTimestamp);
    }

    _sessionCache.reset(new WiredTigerSessionCache(this));

    _sessionSweeper = std::make_unique<WiredTigerSessionSweeper>(_sessionCache.get());
    _sessionSweeper->go();

    // Until the Replication layer installs a real callback, prevent truncating the oplog.
    setOldestActiveTransactionTimestampCallback(
        [](Timestamp) { return StatusWith(boost::make_optional(Timestamp::min())); });

    if (!_readOnly && !_ephemeral) {
        if (!_recoveryTimestamp.isNull()) {
            setInitialDataTimestamp(_recoveryTimestamp);
            setOldestTimestamp(_recoveryTimestamp, false);
            setStableTimestamp(_recoveryTimestamp, false);
        }
    }

    if (_ephemeral && !getTestCommandsEnabled()) {
        // We do not maintain any snapshot history for the ephemeral storage engine in production
        // because replication and sharded transactions do not currently run on the inMemory engine.
        // It is live in testing, however.
        snapshotWindowParams.targetSnapshotHistoryWindowInSeconds.store(0);
    }

    _sizeStorerUri = _uri("sizeStorer");
    WiredTigerSession session(_conn);
    if (!_readOnly && repair && _hasUri(session.getSession(), _sizeStorerUri)) {
        LOGV2(22316, "Repairing size cache");

        auto status = _salvageIfNeeded(_sizeStorerUri.c_str());
        if (status.code() != ErrorCodes::DataModifiedByRepair)
            fassertNoTrace(28577, status);
    }

    _sizeStorer = std::make_unique<WiredTigerSizeStorer>(_conn, _sizeStorerUri, _readOnly);

    Locker::setGlobalThrottling(&openReadTransaction, &openWriteTransaction);

    _runTimeConfigParam.reset(new WiredTigerEngineRuntimeConfigParameter(
        "wiredTigerEngineRuntimeConfig", ServerParameterType::kRuntimeOnly));
    _runTimeConfigParam->_data.second = this;
    _maxCacheOverflowParam.reset(new WiredTigerMaxCacheOverflowSizeGBParameter(
        "wiredTigerMaxCacheOverflowSizeGB", ServerParameterType::kRuntimeOnly));
    _maxCacheOverflowParam->_data = {maxCacheOverflowFileSizeMB / 1024, this};
}

WiredTigerKVEngine::~WiredTigerKVEngine() {
    // Remove server parameters that we added in the constructor, to enable unit tests to reload the
    // storage engine again in this same process.
    ServerParameterSet::getGlobal()->remove("wiredTigerEngineRuntimeConfig");
    ServerParameterSet::getGlobal()->remove("wiredTigerMaxCacheOverflowSizeGB");

    cleanShutdown();

    _sessionCache.reset(nullptr);
}

void WiredTigerKVEngine::startAsyncThreads() {
    if (!_ephemeral) {
        if (_durable) {
            _journalFlusher = std::make_unique<WiredTigerJournalFlusher>(_sessionCache.get());
            _journalFlusher->go();
        }
        if (!_readOnly) {
            _checkpointThread =
                std::make_unique<WiredTigerCheckpointThread>(this, _sessionCache.get());
            _checkpointThread->go();
        }
    }
}

void WiredTigerKVEngine::appendGlobalStats(BSONObjBuilder& b) {
    BSONObjBuilder bb(b.subobjStart("concurrentTransactions"));
    {
        BSONObjBuilder bbb(bb.subobjStart("write"));
        bbb.append("out", openWriteTransaction.used());
        bbb.append("available", openWriteTransaction.available());
        bbb.append("totalTickets", openWriteTransaction.outof());
        bbb.done();
    }
    {
        BSONObjBuilder bbb(bb.subobjStart("read"));
        bbb.append("out", openReadTransaction.used());
        bbb.append("available", openReadTransaction.available());
        bbb.append("totalTickets", openReadTransaction.outof());
        bbb.done();
    }
    bb.done();
}

void WiredTigerKVEngine::_openWiredTiger(const std::string& path, const std::string& wtOpenConfig) {
    // MongoDB 4.4 will always run in compatibility version 10.0.
    std::string configStr = wtOpenConfig + ",compatibility=(require_min=\"10.0.0\")";
    auto wtEventHandler = _eventHandler.getWtEventHandler();

    int ret = wiredtiger_open(path.c_str(), wtEventHandler, configStr.c_str(), &_conn);
    if (!ret) {
        _fileVersion = {WiredTigerFileVersion::StartupVersion::IS_44_FCV_44};
        return;
    }

    if (_eventHandler.isWtIncompatible()) {
        // WT 4.4+ will refuse to startup on datafiles left behind by 4.0 and earlier. This behavior
        // is enforced outside of `require_min`. This condition is detected via a specific error
        // message from WiredTiger.
        if (_inRepairMode) {
            // In case this process was started with `--repair`, remove the "repair incomplete"
            // file.
            StorageRepairObserver::get(getGlobalServiceContext())->onRepairDone(nullptr);
        }
        LOGV2_FATAL(46712005,
                    "This version of MongoDB is too recent to start up on the existing data files. "
                    "Try MongoDB 4.2 or earlier.");
        fassertFailedNoTrace(46712006);
    }

    // MongoDB 4.4 doing clean shutdown in FCV 4.2 will use compatibility version 3.3.
    configStr = wtOpenConfig + ",compatibility=(require_min=\"3.3.0\")";
    ret = wiredtiger_open(path.c_str(), wtEventHandler, configStr.c_str(), &_conn);
    if (!ret) {
        _fileVersion = {WiredTigerFileVersion::StartupVersion::IS_44_FCV_42};
        return;
    }

    // MongoDB 4.2 uses compatibility version 3.2.
    configStr = wtOpenConfig + ",compatibility=(require_min=\"3.2.0\")";
    ret = wiredtiger_open(path.c_str(), wtEventHandler, configStr.c_str(), &_conn);
    if (!ret) {
        _fileVersion = {WiredTigerFileVersion::StartupVersion::IS_42};
        return;
    }

    LOGV2_WARNING(22347,
                  "Failed to start up WiredTiger under any compatibility version. This may be due "
                  "to an unsupported upgrade or downgrade.");
    if (ret == EINVAL) {
        fassertFailedNoTrace(28561);
    }

    if (ret == WT_TRY_SALVAGE) {
        LOGV2_WARNING(22348, "WiredTiger metadata corruption detected");
        if (!_inRepairMode) {
            LOGV2_FATAL_NOTRACE(50944, kWTRepairMsg);
        }
    }

    if (!_inRepairMode) {
        LOGV2_FATAL_NOTRACE(28595, "Terminating.", "Reason"_attr = wtRCToStatus(ret).reason());
    }

    // Always attempt to salvage metadata regardless of error code when in repair mode.
    LOGV2_WARNING(22349, "Attempting to salvage WiredTiger metadata");
    configStr = wtOpenConfig + ",salvage=true";
    ret = wiredtiger_open(path.c_str(), wtEventHandler, configStr.c_str(), &_conn);
    if (!ret) {
        StorageRepairObserver::get(getGlobalServiceContext())
            ->invalidatingModification("WiredTiger metadata salvaged");
        return;
    }

    LOGV2_FATAL_NOTRACE(50947,
                        "Failed to salvage WiredTiger metadata.",
                        "Details"_attr = wtRCToStatus(ret).reason());
}

void WiredTigerKVEngine::cleanShutdown() {
    LOGV2(22317, "WiredTigerKVEngine shutting down");
    if (!_readOnly)
        syncSizeInfo(true);
    if (!_conn) {
        return;
    }

    // these must be the last things we do before _conn->close();
    if (_sessionSweeper) {
        LOGV2(22318, "Shutting down session sweeper thread");
        _sessionSweeper->shutdown();
        LOGV2(22319, "Finished shutting down session sweeper thread");
    }
    if (_journalFlusher) {
        LOGV2(22320, "Shutting down journal flusher thread");
        _journalFlusher->shutdown();
        LOGV2(22321, "Finished shutting down journal flusher thread");
    }
    if (_checkpointThread) {
        LOGV2(22322, "Shutting down checkpoint thread");
        _checkpointThread->shutdown();
        LOGV2(22323, "Finished shutting down checkpoint thread");
    }
    LOGV2_FOR_RECOVERY(23988,
                       2,
                       "Shutdown timestamps. StableTimestamp: {stableTimestamp_load} Initial data "
                       "timestamp: {initialDataTimestamp_load}",
                       "stableTimestamp_load"_attr = _stableTimestamp.load(),
                       "initialDataTimestamp_load"_attr = _initialDataTimestamp.load());

    _sizeStorer.reset();
    _sessionCache->shuttingDown();

    // We want WiredTiger to leak memory for faster shutdown except when we are running tools to
    // look for memory leaks.
    bool leak_memory = !kAddressSanitizerEnabled;
    std::string closeConfig = "";

    if (RUNNING_ON_VALGRIND) {
        leak_memory = false;
    }

    if (leak_memory) {
        closeConfig = "leak_memory=true,";
    }

    std::uint64_t stableTimestamp = _stableTimestamp.load();
    if (gTakeUnstableCheckpointOnShutdown) {
        closeConfig += "use_timestamp=false,";
    } else if (stableTimestamp > 0 && stableTimestamp < _initialDataTimestamp.load()) {
        // After a rollback via refetch, WT update chains for _id index keys can be logically
        // corrupt for read timestamps earlier than the `_initialDataTimestamp`. Because the stable
        // timestamp is really a read timestamp, we must avoid taking a stable checkpoint.
        //
        // If a stable timestamp is not set, there's no risk of reading corrupt history.
        LOGV2(22326,
              "Skipping checkpoint during clean shutdown because stableTimestamp < "
              "initialDataTimestamp.",
              "stableTimestamp"_attr = stableTimestamp,
              "initialDataTimestamp"_attr = _initialDataTimestamp.load());
        quickExit(EXIT_SUCCESS);
    }

    if (_fileVersion.shouldDowngrade(_readOnly, _inRepairMode, !_recoveryTimestamp.isNull())) {
        LOGV2(22324, "Downgrading WiredTiger datafiles.");
        invariantWTOK(_conn->close(_conn, closeConfig.c_str()));

        invariantWTOK(wiredtiger_open(
            _path.c_str(), _eventHandler.getWtEventHandler(), _wtOpenConfig.c_str(), &_conn));
        LOGV2_DEBUG(22325,
                    1,
                    "Downgrade compatibility configuration: {fileVersion_getDowngradeString}",
                    "fileVersion_getDowngradeString"_attr = _fileVersion.getDowngradeString());
        invariantWTOK(_conn->reconfigure(_conn, _fileVersion.getDowngradeString().c_str()));
    }

    invariantWTOK(_conn->close(_conn, closeConfig.c_str()));
    _conn = nullptr;
}

Status WiredTigerKVEngine::okToRename(OperationContext* opCtx,
                                      StringData fromNS,
                                      StringData toNS,
                                      StringData ident,
                                      const RecordStore* originalRecordStore) const {
    syncSizeInfo(false);

    return Status::OK();
}

int64_t WiredTigerKVEngine::getIdentSize(OperationContext* opCtx, StringData ident) {
    WiredTigerSession* session = WiredTigerRecoveryUnit::get(opCtx)->getSession();
    return WiredTigerUtil::getIdentSize(session->getSession(), _uri(ident));
}

Status WiredTigerKVEngine::repairIdent(OperationContext* opCtx, StringData ident) {
    WiredTigerSession* session = WiredTigerRecoveryUnit::get(opCtx)->getSession();
    string uri = _uri(ident);
    session->closeAllCursors(uri);
    _sessionCache->closeAllCursors(uri);
    if (isEphemeral()) {
        return Status::OK();
    }
    _ensureIdentPath(ident);
    return _salvageIfNeeded(uri.c_str());
}

Status WiredTigerKVEngine::_salvageIfNeeded(const char* uri) {
    // Using a side session to avoid transactional issues
    WiredTigerSession sessionWrapper(_conn);
    WT_SESSION* session = sessionWrapper.getSession();

    int rc = (session->verify)(session, uri, nullptr);
    if (rc == 0) {
        LOGV2(22327, "Verify succeeded on uri {uri}. Not salvaging.", "uri"_attr = uri);
        return Status::OK();
    }

    if (rc == EBUSY) {
        // SERVER-16457: verify and salvage are occasionally failing with EBUSY. For now we
        // lie and return OK to avoid breaking tests. This block should go away when that ticket
        // is resolved.
        LOGV2_ERROR(22356,
                    "Verify on {uri} failed with EBUSY. This means the collection was being "
                    "accessed. No repair is necessary unless other "
                    "errors are reported.",
                    "uri"_attr = uri);
        return Status::OK();
    }

    if (rc == ENOENT) {
        LOGV2_WARNING(
            22350,
            "Data file is missing for {uri}. Attempting to drop and re-create the collection.",
            "uri"_attr = uri);

        return _rebuildIdent(session, uri);
    }

    LOGV2(22328, "Verify failed on uri {uri}. Running a salvage operation.", "uri"_attr = uri);
    auto status = wtRCToStatus(session->salvage(session, uri, nullptr), "Salvage failed:");
    if (status.isOK()) {
        return {ErrorCodes::DataModifiedByRepair, str::stream() << "Salvaged data for " << uri};
    }

    LOGV2_WARNING(22351,
                  "Salvage failed for uri {uri}: {status_reason}. The file will be moved out of "
                  "the way and a new ident will be created.",
                  "uri"_attr = uri,
                  "status_reason"_attr = status.reason());

    //  If the data is unsalvageable, we should completely rebuild the ident.
    return _rebuildIdent(session, uri);
}

Status WiredTigerKVEngine::_rebuildIdent(WT_SESSION* session, const char* uri) {
    invariant(_inRepairMode);

    invariant(std::string(uri).find(kTableUriPrefix.rawData()) == 0);

    const std::string identName(uri + kTableUriPrefix.size());
    auto filePath = getDataFilePathForIdent(identName);
    if (filePath) {
        const boost::filesystem::path corruptFile(filePath->string() + ".corrupt");
        LOGV2_WARNING(22352,
                      "Moving data file {filePath_string} to backup as {corruptFile_string}",
                      "filePath_string"_attr = filePath->string(),
                      "corruptFile_string"_attr = corruptFile.string());

        auto status = fsyncRename(filePath.get(), corruptFile);
        if (!status.isOK()) {
            return status;
        }
    }

    LOGV2_WARNING(22353, "Rebuilding ident {identName}", "identName"_attr = identName);

    // This is safe to call after moving the file because it only reads from the metadata, and not
    // the data file itself.
    auto swMetadata = WiredTigerUtil::getMetadataCreate(session, uri);
    if (!swMetadata.isOK()) {
        LOGV2_ERROR(22357, "Failed to get metadata for {uri}", "uri"_attr = uri);
        return swMetadata.getStatus();
    }

    int rc = session->drop(session, uri, nullptr);
    if (rc != 0) {
        LOGV2_ERROR(22358, "Failed to drop {uri}", "uri"_attr = uri);
        return wtRCToStatus(rc);
    }

    rc = session->create(session, uri, swMetadata.getValue().c_str());
    if (rc != 0) {
        LOGV2_ERROR(22359,
                    "Failed to create {uri} with config: {swMetadata_getValue}",
                    "uri"_attr = uri,
                    "swMetadata_getValue"_attr = swMetadata.getValue());
        return wtRCToStatus(rc);
    }
    LOGV2(22329, "Successfully re-created {uri}.", "uri"_attr = uri);
    return {ErrorCodes::DataModifiedByRepair,
            str::stream() << "Re-created empty data file for " << uri};
}

void WiredTigerKVEngine::flushAllFiles(OperationContext* opCtx, bool callerHoldsReadLock) {
    LOGV2_DEBUG(22330, 1, "WiredTigerKVEngine::flushAllFiles");
    if (_ephemeral) {
        return;
    }
    syncSizeInfo(false);

    // If there's no journal, we must checkpoint all of the data.
    WiredTigerSessionCache::Fsync fsyncType = _durable
        ? WiredTigerSessionCache::Fsync::kCheckpointStableTimestamp
        : WiredTigerSessionCache::Fsync::kCheckpointAll;

    // We will skip updating the journal listener if the caller holds read locks.
    // The JournalListener may do writes, and taking write locks would conflict with the read locks.
    WiredTigerSessionCache::UseJournalListener useListener = callerHoldsReadLock
        ? WiredTigerSessionCache::UseJournalListener::kSkip
        : WiredTigerSessionCache::UseJournalListener::kUpdate;

    _sessionCache->waitUntilDurable(opCtx, fsyncType, useListener);
}

Status WiredTigerKVEngine::beginBackup(OperationContext* opCtx) {
    invariant(!_backupSession);

    // The inMemory Storage Engine cannot create a backup cursor.
    if (_ephemeral) {
        return Status::OK();
    }

    // Persist the sizeStorer information to disk before opening the backup cursor.
    syncSizeInfo(true);

    // This cursor will be freed by the backupSession being closed as the session is uncached
    auto session = std::make_unique<WiredTigerSession>(_conn);
    WT_CURSOR* c = nullptr;
    WT_SESSION* s = session->getSession();
    int ret = WT_OP_CHECK(s->open_cursor(s, "backup:", nullptr, nullptr, &c));
    if (ret != 0) {
        return wtRCToStatus(ret);
    }
    _backupSession = std::move(session);
    return Status::OK();
}

void WiredTigerKVEngine::endBackup(OperationContext* opCtx) {
    if (_sessionCache->isShuttingDown()) {
        // There could be a race with clean shutdown which unconditionally closes all the sessions.
        _backupSession->_session = nullptr;  // Prevent calling _session->close() in destructor.
    }
    _backupSession.reset();
}

Status WiredTigerKVEngine::disableIncrementalBackup(OperationContext* opCtx) {
    // Opening an incremental backup cursor with the "force_stop=true" configuration option then
    // closing the cursor will set a flag in WiredTiger that causes it to release all incremental
    // information and resources.
    // Opening a subsequent incremental backup cursor will reset the flag in WiredTiger and
    // reinstate incremental backup history.
    uassert(31401, "Cannot open backup cursor with in-memory storage engine.", !isEphemeral());

    auto sessionRaii = std::make_unique<WiredTigerSession>(_conn);
    WT_CURSOR* cursor = nullptr;
    WT_SESSION* session = sessionRaii->getSession();
    int wtRet =
        session->open_cursor(session, "backup:", nullptr, "incremental=(force_stop=true)", &cursor);
    if (wtRet != 0) {
        LOGV2_ERROR(22360, "Could not open a backup cursor to disable incremental backups");
        return wtRCToStatus(wtRet);
    }

    return Status::OK();
}

StatusWith<StorageEngine::BackupInformation> WiredTigerKVEngine::beginNonBlockingBackup(
    OperationContext* opCtx, const StorageEngine::BackupOptions& options) {
    uassert(51034, "Cannot open backup cursor with in-memory mode.", !isEphemeral());

    std::stringstream ss;
    if (options.incrementalBackup) {
        invariant(options.thisBackupName);
        ss << "incremental=(enabled=true,force_stop=false,";
        ss << "granularity=" << options.blockSizeMB << "MB,";
        ss << "this_id=" << std::quoted(str::escape(*options.thisBackupName)) << ",";

        if (options.srcBackupName) {
            ss << "src_id=" << std::quoted(str::escape(*options.srcBackupName)) << ",";
        }

        ss << ")";
    }

    // Oplog truncation thread won't remove oplog since the checkpoint pinned by the backup cursor.
    stdx::lock_guard<Latch> lock(_oplogPinnedByBackupMutex);
    _checkpointThread->assignOplogNeededForCrashRecoveryTo(&_oplogPinnedByBackup);
    auto pinOplogGuard = makeGuard([&] { _oplogPinnedByBackup = boost::none; });

    // Persist the sizeStorer information to disk before opening the backup cursor. We aren't
    // guaranteed to have the most up-to-date size information after the backup as writes can still
    // occur during a nonblocking backup.
    syncSizeInfo(true);

    // This cursor will be freed by the backupSession being closed as the session is uncached
    auto sessionRaii = std::make_unique<WiredTigerSession>(_conn);
    WT_CURSOR* cursor = nullptr;
    WT_SESSION* session = sessionRaii->getSession();
    const std::string config = ss.str();
    int wtRet = session->open_cursor(session, "backup:", nullptr, config.c_str(), &cursor);
    if (wtRet != 0) {
        return wtRCToStatus(wtRet);
    }

    const bool fullBackup = !options.srcBackupName;
    auto swBackupInfo = getBackupInformationFromBackupCursor(session,
                                                             cursor,
                                                             options.incrementalBackup,
                                                             fullBackup,
                                                             _path,
                                                             "Error opening backup cursor.");

    if (!swBackupInfo.isOK()) {
        return swBackupInfo;
    }

    pinOplogGuard.dismiss();
    _backupSession = std::move(sessionRaii);
    _backupCursor = cursor;

    return swBackupInfo;
}

void WiredTigerKVEngine::endNonBlockingBackup(OperationContext* opCtx) {
    _backupSession.reset();
    // Oplog truncation thread can now remove the pinned oplog.
    stdx::lock_guard<Latch> lock(_oplogPinnedByBackupMutex);
    _oplogPinnedByBackup = boost::none;
    _backupCursor = nullptr;
}

StatusWith<std::vector<std::string>> WiredTigerKVEngine::extendBackupCursor(
    OperationContext* opCtx) {
    uassert(51033, "Cannot extend backup cursor with in-memory mode.", !isEphemeral());
    invariant(_backupCursor);

    // The "target=(\"log:\")" configuration string for the cursor will ensure that we only see the
    // log files when iterating on the cursor.
    WT_CURSOR* cursor = nullptr;
    WT_SESSION* session = _backupSession->getSession();
    int wtRet = session->open_cursor(session, nullptr, _backupCursor, "target=(\"log:\")", &cursor);
    if (wtRet != 0) {
        return wtRCToStatus(wtRet);
    }

    StatusWith<StorageEngine::BackupInformation> swBackupInfo =
        getBackupInformationFromBackupCursor(session,
                                             cursor,
                                             /*incrementalBackup=*/false,
                                             /*fullBackup=*/true,
                                             _path,
                                             "Error extending backup cursor.");

    wtRet = cursor->close(cursor);
    if (wtRet != 0) {
        return wtRCToStatus(wtRet);
    }

    if (!swBackupInfo.isOK()) {
        return swBackupInfo.getStatus();
    }

    // Once all the backup cursors have been opened on a sharded cluster, we need to ensure that the
    // data being copied from each shard is at the same point-in-time across the entire cluster to
    // have a consistent view of the data. For shards that opened their backup cursor before the
    // established point-in-time for backup, they will need to create a full copy of the additional
    // journal files returned by this method to ensure a consistent backup of the data is taken.
    std::vector<std::string> filenames;
    for (const auto& entry : swBackupInfo.getValue()) {
        filenames.push_back(entry.first);
    }

    return {filenames};
}

void WiredTigerKVEngine::syncSizeInfo(bool sync) const {
    if (!_sizeStorer)
        return;

    try {
        _sizeStorer->flush(sync);
    } catch (const WriteConflictException&) {
        // ignore, we'll try again later.
    }
}

void WiredTigerKVEngine::setOldestActiveTransactionTimestampCallback(
    StorageEngine::OldestActiveTransactionTimestampCallback callback) {
    stdx::lock_guard<Latch> lk(_oldestActiveTransactionTimestampCallbackMutex);
    _oldestActiveTransactionTimestampCallback = std::move(callback);
};

RecoveryUnit* WiredTigerKVEngine::newRecoveryUnit() {
    return new WiredTigerRecoveryUnit(_sessionCache.get());
}

void WiredTigerKVEngine::setRecordStoreExtraOptions(const std::string& options) {
    _rsOptions = options;
}

void WiredTigerKVEngine::setSortedDataInterfaceExtraOptions(const std::string& options) {
    _indexOptions = options;
}

Status WiredTigerKVEngine::createGroupedRecordStore(OperationContext* opCtx,
                                                    StringData ns,
                                                    StringData ident,
                                                    const CollectionOptions& options,
                                                    KVPrefix prefix) {
    _ensureIdentPath(ident);
    WiredTigerSession session(_conn);

    const bool prefixed = prefix.isPrefixed();
    StatusWith<std::string> result = WiredTigerRecordStore::generateCreateString(
        _canonicalName, ns, options, _rsOptions, prefixed);
    if (!result.isOK()) {
        return result.getStatus();
    }
    std::string config = result.getValue();

    string uri = _uri(ident);
    WT_SESSION* s = session.getSession();
    LOGV2_DEBUG(22331,
                2,
                "WiredTigerKVEngine::createRecordStore ns: {ns} uri: {uri} config: {config}",
                "ns"_attr = ns,
                "uri"_attr = uri,
                "config"_attr = config);
    return wtRCToStatus(s->create(s, uri.c_str(), config.c_str()));
}

Status WiredTigerKVEngine::recoverOrphanedIdent(OperationContext* opCtx,
                                                const NamespaceString& nss,
                                                StringData ident,
                                                const CollectionOptions& options) {
#ifdef _WIN32
    return {ErrorCodes::CommandNotSupported, "Orphan file recovery is not supported on Windows"};
#else
    invariant(_inRepairMode);

    // Moves the data file to a temporary name so that a new RecordStore can be created with the
    // same ident name. We will delete the new empty collection and rename the data file back so it
    // can be salvaged.

    boost::optional<boost::filesystem::path> identFilePath = getDataFilePathForIdent(ident);
    if (!identFilePath) {
        return {ErrorCodes::UnknownError, "Data file for ident " + ident + " not found"};
    }

    boost::system::error_code ec;
    invariant(boost::filesystem::exists(*identFilePath, ec));

    boost::filesystem::path tmpFile{*identFilePath};
    tmpFile += ".tmp";

    LOGV2(22332,
          "{Renaming_data_file_identFilePath_string_to_temporary_file_tmpFile_string}",
          "Renaming_data_file_identFilePath_string_to_temporary_file_tmpFile_string"_attr =
              "Renaming data file " + identFilePath->string() + " to temporary file " +
              tmpFile.string());
    auto status = fsyncRename(identFilePath.get(), tmpFile);
    if (!status.isOK()) {
        return status;
    }

    LOGV2(22333,
          "Creating new RecordStore for collection {nss} with UUID: {options_uuid}",
          "nss"_attr = nss,
          "options_uuid"_attr = options.uuid);

    status = createGroupedRecordStore(opCtx, nss.ns(), ident, options, KVPrefix::kNotPrefixed);
    if (!status.isOK()) {
        return status;
    }

    LOGV2(22334,
          "{Moving_orphaned_data_file_back_as_identFilePath_string}",
          "Moving_orphaned_data_file_back_as_identFilePath_string"_attr =
              "Moving orphaned data file back as " + identFilePath->string());

    boost::filesystem::remove(*identFilePath, ec);
    if (ec) {
        return {ErrorCodes::UnknownError, "Error deleting empty data file: " + ec.message()};
    }
    status = fsyncParentDirectory(*identFilePath);
    if (!status.isOK()) {
        return status;
    }

    status = fsyncRename(tmpFile, identFilePath.get());
    if (!status.isOK()) {
        return status;
    }

    LOGV2(22335,
          "{Salvaging_ident_ident}",
          "Salvaging_ident_ident"_attr = "Salvaging ident " + ident);

    WiredTigerSession sessionWrapper(_conn);
    WT_SESSION* session = sessionWrapper.getSession();
    status =
        wtRCToStatus(session->salvage(session, _uri(ident).c_str(), nullptr), "Salvage failed: ");
    if (status.isOK()) {
        return {ErrorCodes::DataModifiedByRepair,
                str::stream() << "Salvaged data for ident " << ident};
    }
    LOGV2_WARNING(22354,
                  "Could not salvage data. Rebuilding ident: {status_reason}",
                  "status_reason"_attr = status.reason());

    //  If the data is unsalvageable, we should completely rebuild the ident.
    return _rebuildIdent(session, _uri(ident).c_str());
#endif
}

std::unique_ptr<RecordStore> WiredTigerKVEngine::getGroupedRecordStore(
    OperationContext* opCtx,
    StringData ns,
    StringData ident,
    const CollectionOptions& options,
    KVPrefix prefix) {

    WiredTigerRecordStore::Params params;
    params.ns = ns;
    params.ident = ident.toString();
    params.engineName = _canonicalName;
    params.isCapped = options.capped;
    params.isEphemeral = _ephemeral;
    params.cappedCallback = nullptr;
    params.sizeStorer = _sizeStorer.get();
    params.isReadOnly = _readOnly;
    params.tracksSizeAdjustments = true;

    params.cappedMaxSize = -1;
    if (options.capped) {
        if (options.cappedSize) {
            params.cappedMaxSize = options.cappedSize;
        } else {
            params.cappedMaxSize = kDefaultCappedSizeBytes;
        }
    }
    params.cappedMaxDocs = -1;
    if (options.capped && options.cappedMaxDocs)
        params.cappedMaxDocs = options.cappedMaxDocs;

    std::unique_ptr<WiredTigerRecordStore> ret;
    if (prefix == KVPrefix::kNotPrefixed) {
        ret = std::make_unique<StandardWiredTigerRecordStore>(this, opCtx, params);
    } else {
        ret = std::make_unique<PrefixedWiredTigerRecordStore>(this, opCtx, params, prefix);
    }
    ret->postConstructorInit(opCtx);

    // Sizes should always be checked when creating a collection during rollback or replication
    // recovery. This is in case the size storer information is no longer accurate. This may be
    // necessary if capped deletes are rolled-back, if rollback occurs across a collection rename,
    // or when collection creation is not part of a stable checkpoint.
    const auto replCoord = repl::ReplicationCoordinator::get(getGlobalServiceContext());
    const bool inRollback = replCoord && replCoord->getMemberState().rollback();
    if (inRollback || inReplicationRecovery(getGlobalServiceContext())) {
        ret->checkSize(opCtx);
    }

    return std::move(ret);
}

string WiredTigerKVEngine::_uri(StringData ident) const {
    invariant(ident.find(kTableUriPrefix) == string::npos);
    return kTableUriPrefix + ident.toString();
}

Status WiredTigerKVEngine::createGroupedSortedDataInterface(OperationContext* opCtx,
                                                            const CollectionOptions& collOptions,
                                                            StringData ident,
                                                            const IndexDescriptor* desc,
                                                            KVPrefix prefix) {
    _ensureIdentPath(ident);

    std::string collIndexOptions;
    const Collection* collection = desc->getCollection();

    // Treat 'collIndexOptions' as an empty string when the collection member of 'desc' is NULL in
    // order to allow for unit testing WiredTigerKVEngine::createSortedDataInterface().
    if (collection) {
        if (!collOptions.indexOptionDefaults["storageEngine"].eoo()) {
            BSONObj storageEngineOptions = collOptions.indexOptionDefaults["storageEngine"].Obj();
            collIndexOptions =
                dps::extractElementAtPath(storageEngineOptions, _canonicalName + ".configString")
                    .valuestrsafe();
        }
    }

    StatusWith<std::string> result = WiredTigerIndex::generateCreateString(
        _canonicalName, _indexOptions, collIndexOptions, *desc, prefix.isPrefixed());
    if (!result.isOK()) {
        return result.getStatus();
    }

    std::string config = result.getValue();

    LOGV2_DEBUG(22336,
                2,
                "WiredTigerKVEngine::createSortedDataInterface ns: {collection_ns} ident: {ident} "
                "config: {config}",
                "collection_ns"_attr = collection->ns(),
                "ident"_attr = ident,
                "config"_attr = config);
    return wtRCToStatus(WiredTigerIndex::Create(opCtx, _uri(ident), config));
}

std::unique_ptr<SortedDataInterface> WiredTigerKVEngine::getGroupedSortedDataInterface(
    OperationContext* opCtx, StringData ident, const IndexDescriptor* desc, KVPrefix prefix) {
    if (desc->unique()) {
        return std::make_unique<WiredTigerIndexUnique>(opCtx, _uri(ident), desc, prefix, _readOnly);
    }

    return std::make_unique<WiredTigerIndexStandard>(opCtx, _uri(ident), desc, prefix, _readOnly);
}

std::unique_ptr<RecordStore> WiredTigerKVEngine::makeTemporaryRecordStore(OperationContext* opCtx,
                                                                          StringData ident) {
    invariant(!_readOnly);

    _ensureIdentPath(ident);
    WiredTigerSession wtSession(_conn);

    CollectionOptions noOptions;
    StatusWith<std::string> swConfig = WiredTigerRecordStore::generateCreateString(
        _canonicalName, "" /* internal table */, noOptions, _rsOptions, false /* prefixed */);
    uassertStatusOK(swConfig.getStatus());

    std::string config = swConfig.getValue();

    std::string uri = _uri(ident);
    WT_SESSION* session = wtSession.getSession();
    LOGV2_DEBUG(22337,
                2,
                "WiredTigerKVEngine::createTemporaryRecordStore uri: {uri} config: {config}",
                "uri"_attr = uri,
                "config"_attr = config);
    uassertStatusOK(wtRCToStatus(session->create(session, uri.c_str(), config.c_str())));

    WiredTigerRecordStore::Params params;
    params.ns = "";
    params.ident = ident.toString();
    params.engineName = _canonicalName;
    params.isCapped = false;
    params.isEphemeral = _ephemeral;
    params.cappedCallback = nullptr;
    // Temporary collections do not need to persist size information to the size storer.
    params.sizeStorer = nullptr;
    // Temporary collections do not need to reconcile collection size/counts.
    params.tracksSizeAdjustments = false;
    params.isReadOnly = false;

    params.cappedMaxSize = -1;
    params.cappedMaxDocs = -1;

    std::unique_ptr<WiredTigerRecordStore> rs;
    rs = std::make_unique<StandardWiredTigerRecordStore>(this, opCtx, params);
    rs->postConstructorInit(opCtx);

    return std::move(rs);
}

Status WiredTigerKVEngine::dropIdent(OperationContext* opCtx, RecoveryUnit* ru, StringData ident) {
    string uri = _uri(ident);

    WiredTigerRecoveryUnit* wtRu = checked_cast<WiredTigerRecoveryUnit*>(ru);
    wtRu->getSessionNoTxn()->closeAllCursors(uri);
    _sessionCache->closeAllCursors(uri);

    WiredTigerSession session(_conn);

    int ret = session.getSession()->drop(
        session.getSession(), uri.c_str(), "force,checkpoint_wait=false");
    LOGV2_DEBUG(22338, 1, "WT drop of {uri} res {ret}", "uri"_attr = uri, "ret"_attr = ret);

    if (ret == 0) {
        // yay, it worked
        return Status::OK();
    }

    if (ret == EBUSY) {
        // this is expected, queue it up
        {
            stdx::lock_guard<Latch> lk(_identToDropMutex);
            _identToDrop.push_front(uri);
        }
        _sessionCache->closeCursorsForQueuedDrops();
        return Status::OK();
    }

    if (ret == ENOENT) {
        return Status::OK();
    }

    invariantWTOK(ret);
    return Status::OK();
}

std::list<WiredTigerCachedCursor> WiredTigerKVEngine::filterCursorsWithQueuedDrops(
    std::list<WiredTigerCachedCursor>* cache) {
    std::list<WiredTigerCachedCursor> toDrop;

    stdx::lock_guard<Latch> lk(_identToDropMutex);
    if (_identToDrop.empty())
        return toDrop;

    for (auto i = cache->begin(); i != cache->end();) {
        if (!i->_cursor ||
            std::find(_identToDrop.begin(), _identToDrop.end(), std::string(i->_cursor->uri)) ==
                _identToDrop.end()) {
            ++i;
            continue;
        }
        toDrop.push_back(*i);
        i = cache->erase(i);
    }

    return toDrop;
}

bool WiredTigerKVEngine::haveDropsQueued() const {
    Date_t now = _clockSource->now();
    Milliseconds delta = now - _previousCheckedDropsQueued;

    if (!_readOnly && _sizeStorerSyncTracker.intervalHasElapsed()) {
        _sizeStorerSyncTracker.resetLastTime();
        syncSizeInfo(false);
    }

    // We only want to check the queue max once per second or we'll thrash
    if (delta < Milliseconds(1000))
        return false;

    _previousCheckedDropsQueued = now;

    // Don't wait for the mutex: if we can't get it, report that no drops are queued.
    stdx::unique_lock<Latch> lk(_identToDropMutex, stdx::defer_lock);
    return lk.try_lock() && !_identToDrop.empty();
}

void WiredTigerKVEngine::dropSomeQueuedIdents() {
    int numInQueue;

    WiredTigerSession session(_conn);

    {
        stdx::lock_guard<Latch> lk(_identToDropMutex);
        numInQueue = _identToDrop.size();
    }

    int numToDelete = 10;
    int tenPercentQueue = numInQueue * 0.1;
    if (tenPercentQueue > 10)
        numToDelete = tenPercentQueue;

    LOGV2_DEBUG(22339,
                1,
                "WT Queue is: {numInQueue} attempting to drop: {numToDelete} tables",
                "numInQueue"_attr = numInQueue,
                "numToDelete"_attr = numToDelete);
    for (int i = 0; i < numToDelete; i++) {
        string uri;
        {
            stdx::lock_guard<Latch> lk(_identToDropMutex);
            if (_identToDrop.empty())
                break;
            uri = _identToDrop.front();
            _identToDrop.pop_front();
        }
        int ret = session.getSession()->drop(
            session.getSession(), uri.c_str(), "force,checkpoint_wait=false");
        LOGV2_DEBUG(
            22340, 1, "WT queued drop of  {uri} res {ret}", "uri"_attr = uri, "ret"_attr = ret);

        if (ret == EBUSY) {
            stdx::lock_guard<Latch> lk(_identToDropMutex);
            _identToDrop.push_back(uri);
        } else {
            invariantWTOK(ret);
        }
    }
}

bool WiredTigerKVEngine::supportsDocLocking() const {
    return true;
}

bool WiredTigerKVEngine::supportsDirectoryPerDB() const {
    return true;
}

bool WiredTigerKVEngine::hasIdent(OperationContext* opCtx, StringData ident) const {
    return _hasUri(WiredTigerRecoveryUnit::get(opCtx)->getSession()->getSession(), _uri(ident));
}

bool WiredTigerKVEngine::_hasUri(WT_SESSION* session, const std::string& uri) const {
    // can't use WiredTigerCursor since this is called from constructor.
    WT_CURSOR* c = nullptr;
    // No need for a metadata:create cursor, since it gathers extra information and is slower.
    int ret = session->open_cursor(session, "metadata:", nullptr, nullptr, &c);
    if (ret == ENOENT)
        return false;
    invariantWTOK(ret);
    ON_BLOCK_EXIT([&] { c->close(c); });

    c->set_key(c, uri.c_str());
    return c->search(c) == 0;
}

std::vector<std::string> WiredTigerKVEngine::getAllIdents(OperationContext* opCtx) const {
    std::vector<std::string> all;
    int ret;
    // No need for a metadata:create cursor, since it gathers extra information and is slower.
    WiredTigerCursor cursor("metadata:", WiredTigerSession::kMetadataTableId, false, opCtx);
    WT_CURSOR* c = cursor.get();
    if (!c)
        return all;

    while ((ret = c->next(c)) == 0) {
        const char* raw;
        c->get_key(c, &raw);
        StringData key(raw);
        size_t idx = key.find(':');
        if (idx == string::npos)
            continue;
        StringData type = key.substr(0, idx);
        if (type != "table")
            continue;

        StringData ident = key.substr(idx + 1);
        if (ident == "sizeStorer")
            continue;

        all.push_back(ident.toString());
    }

    fassert(50663, ret == WT_NOTFOUND);

    return all;
}

boost::optional<boost::filesystem::path> WiredTigerKVEngine::getDataFilePathForIdent(
    StringData ident) const {
    boost::filesystem::path identPath = _path;
    identPath /= ident.toString() + ".wt";

    boost::system::error_code ec;
    if (!boost::filesystem::exists(identPath, ec)) {
        return boost::none;
    }
    return identPath;
}

int WiredTigerKVEngine::reconfigure(const char* str) {
    return _conn->reconfigure(_conn, str);
}

void WiredTigerKVEngine::_ensureIdentPath(StringData ident) {
    size_t start = 0;
    size_t idx;
    while ((idx = ident.find('/', start)) != string::npos) {
        StringData dir = ident.substr(0, idx);

        boost::filesystem::path subdir = _path;
        subdir /= dir.toString();
        if (!boost::filesystem::exists(subdir)) {
            LOGV2_DEBUG(22341, 1, "creating subdirectory: {dir}", "dir"_attr = dir);
            try {
                boost::filesystem::create_directory(subdir);
            } catch (const std::exception& e) {
                LOGV2_ERROR(22361,
                            "error creating path {subdir_string} {e_what}",
                            "subdir_string"_attr = subdir.string(),
                            "e_what"_attr = e.what());
                throw;
            }
        }

        start = idx + 1;
    }
}

void WiredTigerKVEngine::setJournalListener(JournalListener* jl) {
    return _sessionCache->setJournalListener(jl);
}

void WiredTigerKVEngine::setStableTimestamp(Timestamp stableTimestamp, bool force) {
    if (stableTimestamp.isNull()) {
        return;
    }

    // Do not set the stable timestamp backward, unless 'force' is set.
    Timestamp prevStable(_stableTimestamp.load());
    if ((stableTimestamp < prevStable) && !force) {
        return;
    }

    // Communicate to WiredTiger what the "stable timestamp" is. Timestamp-aware checkpoints will
    // only persist to disk transactions committed with a timestamp earlier than the "stable
    // timestamp".
    //
    // After passing the "stable timestamp" to WiredTiger, communicate it to the
    // `CheckpointThread`. It's not obvious a stale stable timestamp in the `CheckpointThread` is
    // safe. Consider the following arguments:
    //
    // Setting the "stable timestamp" is only meaningful when the "initial data timestamp" is real
    // (i.e: not `kAllowUnstableCheckpointsSentinel`). In this normal case, the `stableTimestamp`
    // input must be greater than the current value. The only effect this can have in the
    // `CheckpointThread` is to transition it from a state of not taking any checkpoints, to
    // taking "stable checkpoints". In the transitioning case, it's imperative for the "stable
    // timestamp" to have first been communicated to WiredTiger.
    using namespace fmt::literals;
    std::string stableTSConfigString;
    auto ts = stableTimestamp.asULL();
    if (force) {
        stableTSConfigString =
            "force=true,oldest_timestamp={0:x},commit_timestamp={0:x},stable_timestamp={0:x}"_format(
                ts);
        stdx::lock_guard<Latch> lk(_highestDurableTimestampMutex);
        _highestSeenDurableTimestamp = ts;
    } else {
        stableTSConfigString = "stable_timestamp={:x}"_format(ts);
    }
    invariantWTOK(_conn->set_timestamp(_conn, stableTSConfigString.c_str()));

    // After publishing a stable timestamp to WT, we can record the updated stable timestamp value
    // for the necessary oplog to keep.
    _stableTimestamp.store(stableTimestamp.asULL());
    if (_checkpointThread && !_checkpointThread->hasTriggeredFirstStableCheckpoint()) {
        _checkpointThread->triggerFirstStableCheckpoint(
            prevStable, Timestamp(_initialDataTimestamp.load()), stableTimestamp);
    }

    // If 'force' is set, then we have already set the oldest timestamp equal to the stable
    // timestamp, so there is nothing left to do.
    if (force) {
        return;
    }

    // Forward the oldest timestamp so that WiredTiger can clean up earlier timestamp data.
    setOldestTimestampFromStable();
}

void WiredTigerKVEngine::setOldestTimestampFromStable() {
    Timestamp stableTimestamp(_stableTimestamp.load());

    // Set the oldest timestamp to the stable timestamp to ensure that there is no lag window
    // between the two.
    if (MONGO_unlikely(WTSetOldestTSToStableTS.shouldFail())) {
        setOldestTimestamp(stableTimestamp, false);
        return;
    }

    // Calculate what the oldest_timestamp should be from the stable_timestamp. The oldest
    // timestamp should lag behind stable by 'targetSnapshotHistoryWindowInSeconds' to create a
    // window of available snapshots. If the lag window is not yet large enough, we will not
    // update/forward the oldest_timestamp yet and instead return early.
    Timestamp newOldestTimestamp = _calculateHistoryLagFromStableTimestamp(stableTimestamp);
    if (newOldestTimestamp.isNull()) {
        return;
    }

    setOldestTimestamp(newOldestTimestamp, false);
}

void WiredTigerKVEngine::setOldestTimestamp(Timestamp newOldestTimestamp, bool force) {
    if (MONGO_unlikely(WTPreserveSnapshotHistoryIndefinitely.shouldFail())) {
        return;
    }

    if (force) {
        auto oldestTSConfigString =
            "force=true,oldest_timestamp={0:x},commit_timestamp={0:x}"_format(
                newOldestTimestamp.asULL());
        invariantWTOK(_conn->set_timestamp(_conn, oldestTSConfigString.c_str()));
        _oldestTimestamp.store(newOldestTimestamp.asULL());
        stdx::lock_guard<Latch> lk(_highestDurableTimestampMutex);
        _highestSeenDurableTimestamp = newOldestTimestamp.asULL();
        LOGV2_DEBUG(22342,
                    2,
                    "oldest_timestamp and commit_timestamp force set to {newOldestTimestamp}",
                    "newOldestTimestamp"_attr = newOldestTimestamp);
    } else {
        auto oldestTSConfigString = "oldest_timestamp={:x}"_format(newOldestTimestamp.asULL());
        invariantWTOK(_conn->set_timestamp(_conn, oldestTSConfigString.c_str()));
        // set_timestamp above ignores backwards in time if 'force' is not set.
        if (_oldestTimestamp.load() < newOldestTimestamp.asULL())
            _oldestTimestamp.store(newOldestTimestamp.asULL());
        LOGV2_DEBUG(22343,
                    2,
                    "oldest_timestamp set to {newOldestTimestamp}",
                    "newOldestTimestamp"_attr = newOldestTimestamp);
    }
}

Timestamp WiredTigerKVEngine::_calculateHistoryLagFromStableTimestamp(Timestamp stableTimestamp) {
    // The oldest_timestamp should lag behind the stable_timestamp by
    // 'targetSnapshotHistoryWindowInSeconds' seconds.

    if (_ephemeral && !getTestCommandsEnabled()) {
        // No history should be maintained for the inMemory engine because it is not used yet.
        invariant(snapshotWindowParams.targetSnapshotHistoryWindowInSeconds.load() == 0);
    }

    if (stableTimestamp.getSecs() <
        static_cast<unsigned>(snapshotWindowParams.targetSnapshotHistoryWindowInSeconds.load())) {
        // The history window is larger than the timestamp history thus far. We must wait for
        // the history to reach the window size before moving oldest_timestamp forward.
        return Timestamp();
    }

    Timestamp calculatedOldestTimestamp(
        stableTimestamp.getSecs() -
            snapshotWindowParams.targetSnapshotHistoryWindowInSeconds.load(),
        stableTimestamp.getInc());

    if (calculatedOldestTimestamp.asULL() <= _oldestTimestamp.load()) {
        // The stable_timestamp is not far enough ahead of the oldest_timestamp for the
        // oldest_timestamp to be moved forward: the window is still too small.
        return Timestamp();
    }

    return calculatedOldestTimestamp;
}

void WiredTigerKVEngine::setInitialDataTimestamp(Timestamp initialDataTimestamp) {
    LOGV2_DEBUG(22344,
                2,
                "Setting initial data timestamp. Value: {initialDataTimestamp}",
                "initialDataTimestamp"_attr = initialDataTimestamp);
    _initialDataTimestamp.store(initialDataTimestamp.asULL());
}

bool WiredTigerKVEngine::supportsRecoverToStableTimestamp() const {
    if (!_keepDataHistory) {
        return false;
    }
    return true;
}

bool WiredTigerKVEngine::supportsRecoveryTimestamp() const {
    return true;
}

bool WiredTigerKVEngine::_canRecoverToStableTimestamp() const {
    static const std::uint64_t allowUnstableCheckpointsSentinel =
        static_cast<std::uint64_t>(Timestamp::kAllowUnstableCheckpointsSentinel.asULL());
    const std::uint64_t initialDataTimestamp = _initialDataTimestamp.load();
    // Illegal to be called when the dataset is incomplete.
    invariant(initialDataTimestamp > allowUnstableCheckpointsSentinel);
    return _stableTimestamp.load() >= initialDataTimestamp;
}

StatusWith<Timestamp> WiredTigerKVEngine::recoverToStableTimestamp(OperationContext* opCtx) {
    if (!supportsRecoverToStableTimestamp()) {
        LOGV2_FATAL(50665, "WiredTiger is configured to not support recover to a stable timestamp");
    }

    if (!_canRecoverToStableTimestamp()) {
        Timestamp stableTS(_stableTimestamp.load());
        Timestamp initialDataTS(_initialDataTimestamp.load());
        return Status(ErrorCodes::UnrecoverableRollbackError,
                      str::stream()
                          << "No stable timestamp available to recover to. Initial data timestamp: "
                          << initialDataTS.toString()
                          << ", Stable timestamp: " << stableTS.toString());
    }

    LOGV2_FOR_ROLLBACK(
        23989, 2, "WiredTiger::RecoverToStableTimestamp syncing size storer to disk.");
    syncSizeInfo(true);

    if (!_ephemeral) {
        LOGV2_FOR_ROLLBACK(
            23990,
            2,
            "WiredTiger::RecoverToStableTimestamp shutting down journal and checkpoint threads.");
        // Shutdown WiredTigerKVEngine owned accesses into the storage engine.
        if (_durable) {
            _journalFlusher->shutdown();
        }
        _checkpointThread->shutdown();
    }

    const Timestamp stableTimestamp(_stableTimestamp.load());
    const Timestamp initialDataTimestamp(_initialDataTimestamp.load());

    LOGV2_FOR_ROLLBACK(23991,
                       0,
                       "Rolling back to the stable timestamp. StableTimestamp: {stableTimestamp} "
                       "Initial Data Timestamp: {initialDataTimestamp}",
                       "stableTimestamp"_attr = stableTimestamp,
                       "initialDataTimestamp"_attr = initialDataTimestamp);
    int ret = _conn->rollback_to_stable(_conn, nullptr);
    if (ret) {
        return {ErrorCodes::UnrecoverableRollbackError,
                str::stream() << "Error rolling back to stable. Err: " << wiredtiger_strerror(ret)};
    }

    if (!_ephemeral) {
        if (_durable) {
            _journalFlusher = std::make_unique<WiredTigerJournalFlusher>(_sessionCache.get());
            _journalFlusher->go();
        }
        _checkpointThread = std::make_unique<WiredTigerCheckpointThread>(this, _sessionCache.get());
        _checkpointThread->go();
    }

    _sizeStorer = std::make_unique<WiredTigerSizeStorer>(_conn, _sizeStorerUri, _readOnly);

    return {stableTimestamp};
}

Timestamp WiredTigerKVEngine::getAllDurableTimestamp() const {
    auto ret = _oplogManager->fetchAllDurableValue(_conn);

    stdx::lock_guard<Latch> lk(_highestDurableTimestampMutex);
    if (ret < _highestSeenDurableTimestamp) {
        ret = _highestSeenDurableTimestamp;
    } else {
        _highestSeenDurableTimestamp = ret;
    }
    return Timestamp(ret);
}

Timestamp WiredTigerKVEngine::getOldestOpenReadTimestamp() const {
    // Return the minimum read timestamp of all open transactions.
    char buf[(2 * 8 /*bytes in hex*/) + 1 /*null terminator*/];
    auto wtstatus = _conn->query_timestamp(_conn, buf, "get=oldest_reader");
    if (wtstatus == WT_NOTFOUND) {
        return Timestamp();
    } else {
        invariantWTOK(wtstatus);
    }

    uint64_t tmp;
    fassert(38802, NumberParser().base(16)(buf, &tmp));
    return Timestamp(tmp);
}

boost::optional<Timestamp> WiredTigerKVEngine::getRecoveryTimestamp() const {
    if (!supportsRecoveryTimestamp()) {
        LOGV2_FATAL(50745,
                    "WiredTiger is configured to not support providing a recovery timestamp");
    }

    if (_recoveryTimestamp.isNull()) {
        return boost::none;
    }

    return _recoveryTimestamp;
}

boost::optional<Timestamp> WiredTigerKVEngine::getLastStableRecoveryTimestamp() const {
    if (_ephemeral) {
        Timestamp stable(_stableTimestamp.load());
        Timestamp initialData(_initialDataTimestamp.load());
        if (stable.isNull() || stable < initialData) {
            return boost::none;
        }
        return stable;
    }

    const auto ret = _getCheckpointTimestamp();
    if (ret) {
        return Timestamp(ret);
    }

    if (!_recoveryTimestamp.isNull()) {
        return _recoveryTimestamp;
    }

    return boost::none;
}

StatusWith<Timestamp> WiredTigerKVEngine::getOplogNeededForRollback() const {
    // Get the current stable timestamp and use it throughout this function, ignoring updates from
    // another thread.
    auto stableTimestamp = _stableTimestamp.load();

    // Only one thread can set or execute this callback.
    stdx::lock_guard<Latch> lk(_oldestActiveTransactionTimestampCallbackMutex);
    boost::optional<Timestamp> oldestActiveTransactionTimestamp;
    if (_oldestActiveTransactionTimestampCallback) {
        auto status = _oldestActiveTransactionTimestampCallback(Timestamp(stableTimestamp));
        if (status.isOK()) {
            oldestActiveTransactionTimestamp.swap(status.getValue());
        } else {
            LOGV2_DEBUG(22345,
                        1,
                        "getting oldest active transaction timestamp: {status_getStatus}",
                        "status_getStatus"_attr = status.getStatus());
            return status.getStatus();
        }
    }

    if (oldestActiveTransactionTimestamp) {
        return std::min(oldestActiveTransactionTimestamp.value(), Timestamp(stableTimestamp));
    } else {
        return Timestamp(stableTimestamp);
    }
}

boost::optional<Timestamp> WiredTigerKVEngine::getOplogNeededForCrashRecovery() const {
    if (_ephemeral) {
        return boost::none;
    }

    if (_readOnly) {
        return boost::none;
    }

    return Timestamp(_checkpointThread->getOplogNeededForCrashRecovery());
}

Timestamp WiredTigerKVEngine::getPinnedOplog() const {
    {
        stdx::lock_guard<Latch> lock(_oplogPinnedByBackupMutex);
        if (!storageGlobalParams.allowOplogTruncation) {
            // If oplog truncation is not allowed, then return the min timestamp so that no history
            // is
            // ever allowed to be deleted.
            return Timestamp::min();
        }
        if (_oplogPinnedByBackup) {
            // All the oplog since `_oplogPinnedByBackup` should remain intact during the backup.
            return _oplogPinnedByBackup.get();
        }
    }

    auto oplogNeededForCrashRecovery = getOplogNeededForCrashRecovery();
    if (!_keepDataHistory) {
        // We use rollbackViaRefetch, so we only need to pin oplog for crash recovery.
        return oplogNeededForCrashRecovery.value_or(Timestamp::max());
    }

    if (oplogNeededForCrashRecovery) {
        return oplogNeededForCrashRecovery.value();
    }

    auto status = getOplogNeededForRollback();
    if (status.isOK()) {
        return status.getValue();
    }

    // If getOplogNeededForRollback fails, don't truncate any oplog right now.
    return Timestamp::min();
}

std::unique_ptr<StorageEngine::CheckpointLock> WiredTigerKVEngine::getCheckpointLock(
    OperationContext* opCtx) {
    return std::make_unique<CheckpointLockImpl>(opCtx, _checkpointMutex);
}

bool WiredTigerKVEngine::isInIndividuallyCheckpointedIndexesList(const std::string& ident) const {
    for (auto it = _checkpointedIndexes.begin(); it != _checkpointedIndexes.end(); ++it) {
        if (*it == ident) {
            return true;
        }
    }
    return false;
}

bool WiredTigerKVEngine::supportsReadConcernSnapshot() const {
    return true;
}

bool WiredTigerKVEngine::supportsReadConcernMajority() const {
    return _keepDataHistory;
}

bool WiredTigerKVEngine::supportsOplogStones() const {
    return true;
}

void WiredTigerKVEngine::startOplogManager(OperationContext* opCtx,
                                           const std::string& uri,
                                           WiredTigerRecordStore* oplogRecordStore) {
    stdx::lock_guard<Latch> lock(_oplogManagerMutex);
    if (_oplogManagerCount == 0)
        _oplogManager->start(opCtx, uri, oplogRecordStore);
    _oplogManagerCount++;
}

void WiredTigerKVEngine::haltOplogManager() {
    stdx::unique_lock<Latch> lock(_oplogManagerMutex);
    invariant(_oplogManagerCount > 0);
    _oplogManagerCount--;
    if (_oplogManagerCount == 0) {
        _oplogManager->halt();
    }
}

void WiredTigerKVEngine::triggerJournalFlush() const {
    if (_journalFlusher) {
        _journalFlusher->triggerJournalFlush();
    }
}

void WiredTigerKVEngine::waitForJournalFlush(OperationContext* opCtx) const {
    if (_journalFlusher) {
        _journalFlusher->waitForJournalFlush();
    } else {
        opCtx->recoveryUnit()->waitUntilDurable(opCtx);
    }
}


void WiredTigerKVEngine::interruptJournalFlusherForReplStateChange() const {
    if (_journalFlusher) {
        _journalFlusher->interruptJournalFlusherForReplStateChange();
    }
}

bool WiredTigerKVEngine::isCacheUnderPressure(OperationContext* opCtx) const {
    WiredTigerSession* session = WiredTigerRecoveryUnit::get(opCtx)->getSessionNoTxn();
    invariant(session);

    int64_t score = uassertStatusOK(WiredTigerUtil::getStatisticsValue(
        session->getSession(), "statistics:", "", WT_STAT_CONN_CACHE_LOOKASIDE_SCORE));

    return (score >= snapshotWindowParams.cachePressureThreshold.load());
}

Timestamp WiredTigerKVEngine::getStableTimestamp() const {
    return Timestamp(_stableTimestamp.load());
}

Timestamp WiredTigerKVEngine::getOldestTimestamp() const {
    return Timestamp(_oldestTimestamp.load());
}

Timestamp WiredTigerKVEngine::getCheckpointTimestamp() const {
    return Timestamp(_getCheckpointTimestamp());
}

Timestamp WiredTigerKVEngine::getInitialDataTimestamp() const {
    return Timestamp(_initialDataTimestamp.load());
}

std::uint64_t WiredTigerKVEngine::_getCheckpointTimestamp() const {
    char buf[(2 * 8 /*bytes in hex*/) + 1 /*nul terminator*/];
    invariantWTOK(_conn->query_timestamp(_conn, buf, "get=last_checkpoint"));

    std::uint64_t tmp;
    fassert(50963, NumberParser().base(16)(buf, &tmp));
    return tmp;
}

}  // namespace mongo
