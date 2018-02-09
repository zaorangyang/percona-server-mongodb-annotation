/**
 *    Copyright (C) 2017 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include <cstring>

#include "mongo/db/storage/wiredtiger/wiredtiger_kv_engine.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_oplog_manager.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/concurrency/idle_thread_block.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {
// This is the minimum valid timestamp; it can be used for reads that need to see all untimestamped
// data but no timestamped data.  We cannot use 0 here because 0 means see all timestamped data.
const uint64_t kMinimumTimestamp = 1;
}  // namespace

MONGO_FP_DECLARE(WTPausePrimaryOplogDurabilityLoop);

void WiredTigerOplogManager::start(OperationContext* opCtx,
                                   const std::string& uri,
                                   WiredTigerRecordStore* oplogRecordStore) {
    invariant(!_isRunning);
    // Prime the oplog read timestamp.
    auto sessionCache = WiredTigerRecoveryUnit::get(opCtx)->getSessionCache();
    setOplogReadTimestamp(Timestamp(_fetchAllCommittedValue(sessionCache->conn())));

    std::unique_ptr<SeekableRecordCursor> reverseOplogCursor =
        oplogRecordStore->getCursor(opCtx, false /* false = reverse cursor */);
    auto lastRecord = reverseOplogCursor->next();
    _oplogMaxAtStartup = lastRecord ? lastRecord->id : RecordId();

    auto replCoord = repl::ReplicationCoordinator::get(getGlobalServiceContext());
    bool isMasterSlave = false;
    if (replCoord) {
        isMasterSlave =
            replCoord->getReplicationMode() == repl::ReplicationCoordinator::modeMasterSlave;
    }

    // Need to obtain the mutex before starting the thread, as otherwise it may race ahead
    // see _shuttingDown as true and quit prematurely.
    stdx::lock_guard<stdx::mutex> lk(_oplogVisibilityStateMutex);
    _oplogJournalThread = stdx::thread(&WiredTigerOplogManager::_oplogJournalThreadLoop,
                                       this,
                                       sessionCache,
                                       oplogRecordStore,
                                       isMasterSlave);

    _isRunning = true;
    _shuttingDown = false;
}

void WiredTigerOplogManager::halt() {
    {
        stdx::lock_guard<stdx::mutex> lk(_oplogVisibilityStateMutex);
        invariant(_isRunning);
        _shuttingDown = true;
        _isRunning = false;
    }

    if (_oplogJournalThread.joinable()) {
        _opsWaitingForJournalCV.notify_one();
        _oplogJournalThread.join();
    }
}

void WiredTigerOplogManager::waitForAllEarlierOplogWritesToBeVisible(
    const WiredTigerRecordStore* oplogRecordStore, OperationContext* opCtx) const {
    invariant(opCtx->lockState()->isNoop() || !opCtx->lockState()->inAWriteUnitOfWork());

    // In order to reliably detect rollback situations, we need to fetch the latestVisibleTimestamp
    // prior to querying the end of the oplog.
    auto currentLatestVisibleTimestamp = getOplogReadTimestamp();

    // Procedure: issue a read on a reverse cursor (which is not subject to the oplog visibility
    // rules), see what is last, and wait for that to become visible.
    std::unique_ptr<SeekableRecordCursor> cursor =
        oplogRecordStore->getCursor(opCtx, false /* false = reverse cursor */);
    auto lastRecord = cursor->next();
    if (!lastRecord) {
        LOG(2) << "Trying to query an empty oplog";
        opCtx->recoveryUnit()->abandonSnapshot();
        return;
    }
    const auto waitingFor = lastRecord->id;
    // Close transaction before we wait.
    opCtx->recoveryUnit()->abandonSnapshot();

    stdx::unique_lock<stdx::mutex> lk(_oplogVisibilityStateMutex);
    opCtx->waitForConditionOrInterrupt(_opsBecameVisibleCV, lk, [&] {
        auto newLatestVisibleTimestamp = getOplogReadTimestamp();
        if (newLatestVisibleTimestamp < currentLatestVisibleTimestamp) {
            LOG(1) << "oplog latest visible timestamp went backwards";
            // If the visibility went backwards, this means a rollback occurred.
            // Thus, we are finished waiting.
            return true;
        }
        currentLatestVisibleTimestamp = newLatestVisibleTimestamp;

        // currentLatestVisibleTimestamp might be Timestamp "1" if there are no oplog documents
        // inserted since the last mongod restart.  In this case, we need to simulate what timestamp
        // the last oplog document had when it was written, which is the _oplogMaxAtStartup value.
        RecordId latestVisible =
            std::max(RecordId(currentLatestVisibleTimestamp), _oplogMaxAtStartup);
        if (latestVisible < waitingFor) {
            LOG(2) << "Operation is waiting for " << waitingFor << "; latestVisible is "
                   << currentLatestVisibleTimestamp << " oplogMaxAtStartup is "
                   << _oplogMaxAtStartup;
        }
        return latestVisible >= waitingFor;
    });
}

void WiredTigerOplogManager::triggerJournalFlush() {
    stdx::lock_guard<stdx::mutex> lk(_oplogVisibilityStateMutex);
    if (!_opsWaitingForJournal) {
        _opsWaitingForJournal = true;
        _opsWaitingForJournalCV.notify_one();
    }
}

void WiredTigerOplogManager::_oplogJournalThreadLoop(WiredTigerSessionCache* sessionCache,
                                                     WiredTigerRecordStore* oplogRecordStore,
                                                     bool isMasterSlave) noexcept {
    Client::initThread("WTOplogJournalThread");

    // This thread updates the oplog read timestamp, the timestamp used to read from the oplog with
    // forward cursors.  The timestamp is used to hide oplog entries that might be committed but
    // have uncommitted entries ahead of them.
    while (true) {
        stdx::unique_lock<stdx::mutex> lk(_oplogVisibilityStateMutex);
        {
            MONGO_IDLE_THREAD_BLOCK;
            _opsWaitingForJournalCV.wait(lk,
                                         [&] { return _shuttingDown || _opsWaitingForJournal; });

            // If we're not shutting down and nobody is actively waiting for the oplog to become
            // durable, delay journaling a bit to reduce the sync rate.
            auto journalDelay = Milliseconds(storageGlobalParams.journalCommitIntervalMs.load());
            if (journalDelay == Milliseconds(0)) {
                journalDelay = Milliseconds(WiredTigerKVEngine::kDefaultJournalDelayMillis);
            }
            auto now = Date_t::now();
            auto deadline = now + journalDelay;
            auto shouldSyncOpsWaitingForJournal = [&] {
                return _shuttingDown || oplogRecordStore->haveCappedWaiters();
            };

            // Eventually it would be more optimal to merge this with the normal journal flushing
            // and block for oplog tailers to show up. For now this loop will poll once a
            // millisecond up to the journalDelay to see if we have any waiters yet. This reduces
            // sync-related I/O on the primary when secondaries are lagged, but will avoid
            // significant delays in confirming majority writes on replica sets with infrequent
            // writes.
            while (now < deadline &&
                   !_opsWaitingForJournalCV.wait_until(
                       lk, now.toSystemTimePoint(), shouldSyncOpsWaitingForJournal)) {
                now += Milliseconds(1);
            }
        }

        while (!_shuttingDown && MONGO_FAIL_POINT(WTPausePrimaryOplogDurabilityLoop)) {
            lk.unlock();
            sleepmillis(10);
            lk.lock();
        }

        if (_shuttingDown) {
            log() << "oplog journal thread loop shutting down";
            return;
        }
        invariant(_opsWaitingForJournal);
        _opsWaitingForJournal = false;
        lk.unlock();

        const uint64_t newTimestamp = _fetchAllCommittedValue(sessionCache->conn());

        // The newTimestamp may actually go backward during secondary batch application,
        // where we commit data file changes separately from oplog changes, so ignore
        // a non-incrementing timestamp.
        if (newTimestamp <= _oplogReadTimestamp.load()) {
            LOG(2) << "no new oplog entries were made visible: " << newTimestamp;
            continue;
        }

        // In order to avoid oplog holes after an unclean shutdown, we must ensure this proposed
        // oplog read timestamp's documents are durable before publishing that timestamp.
        sessionCache->waitUntilDurable(/*forceCheckpoint=*/false, false);

        lk.lock();
        // Publish the new timestamp value.
        _setOplogReadTimestamp(lk, newTimestamp);
        lk.unlock();

        // Wake up any await_data cursors and tell them more data might be visible now.
        oplogRecordStore->notifyCappedWaitersIfNeeded();

        // For master/slave masters, set oldest timestamp here so that we clean up old timestamp
        // data.  SERVER-31802
        if (isMasterSlave) {
            sessionCache->getKVEngine()->setStableTimestamp(Timestamp(newTimestamp));
        }
    }
}

std::uint64_t WiredTigerOplogManager::getOplogReadTimestamp() const {
    return _oplogReadTimestamp.load();
}

void WiredTigerOplogManager::setOplogReadTimestamp(Timestamp ts) {
    stdx::lock_guard<stdx::mutex> lk(_oplogVisibilityStateMutex);
    _setOplogReadTimestamp(lk, ts.asULL());
}

void WiredTigerOplogManager::_setOplogReadTimestamp(WithLock, uint64_t newTimestamp) {
    _oplogReadTimestamp.store(newTimestamp);
    _opsBecameVisibleCV.notify_all();
    LOG(2) << "setting new oplogReadTimestamp: " << newTimestamp;
}

uint64_t WiredTigerOplogManager::_fetchAllCommittedValue(WT_CONNECTION* conn) {
    // Fetch the latest all_committed value from the storage engine.  This value will be a
    // timestamp that has no holes (uncommitted transactions with lower timestamps) behind it.
    char buf[(2 * 8 /*bytes in hex*/) + 1 /*nul terminator*/];
    auto wtstatus = conn->query_timestamp(conn, buf, "get=all_committed");
    if (wtstatus == WT_NOTFOUND) {
        // Treat this as lowest possible timestamp; we need to see all preexisting data but no new
        // (timestamped) data.
        return kMinimumTimestamp;
    } else {
        invariantWTOK(wtstatus);
    }

    uint64_t tmp;
    fassertStatusOK(38002, parseNumberFromStringWithBase(buf, 16, &tmp));
    return tmp;
}

}  // namespace mongo
