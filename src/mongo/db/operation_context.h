/**
 *    Copyright (C) 2014 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
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

#pragma once

#include "mongo/base/disallow_copying.h"
#include "mongo/base/status.h"
#include "mongo/db/client.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/storage/recovery_unit.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/db/write_concern_options.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/util/decorable.h"
#include "mongo/util/time_support.h"
#include "mongo/util/timer.h"

namespace mongo {

class Client;
class CurOp;
class Locker;
class ProgressMeter;
class ServiceContext;
class StringData;
class WriteUnitOfWork;

/**
 * This class encompasses the state required by an operation and lives from the time a network
 * operation is dispatched until its execution is finished. Note that each "getmore" on a cursor
 * is a separate operation. On construction, an OperationContext associates itself with the
 * current client, and only on destruction it deassociates itself. At any time a client can be
 * associated with at most one OperationContext. Each OperationContext has a RecoveryUnit
 * associated with it, though the lifetime is not necesarily the same, see releaseRecoveryUnit
 * and setRecoveryUnit. The operation context also keeps track of some transaction state
 * (RecoveryUnitState) to reduce complexity and duplication in the storage-engine specific
 * RecoveryUnit and to allow better invariant checking.
 */
class OperationContext : public Decorable<OperationContext> {
    MONGO_DISALLOW_COPYING(OperationContext);

public:
    /**
     * The RecoveryUnitState is used by WriteUnitOfWork to ensure valid state transitions.
     */
    enum RecoveryUnitState {
        kNotInUnitOfWork,   // not in a unit of work, no writes allowed
        kActiveUnitOfWork,  // in a unit of work that still may either commit or abort
        kFailedUnitOfWork   // in a unit of work that has failed and must be aborted
    };

    virtual ~OperationContext() = default;

    /**
     * Interface for durability.  Caller DOES NOT own pointer.
     */
    virtual RecoveryUnit* recoveryUnit() const = 0;

    /**
     * Returns the RecoveryUnit (same return value as recoveryUnit()) but the caller takes
     * ownership of the returned RecoveryUnit, and the OperationContext instance relinquishes
     * ownership.  Sets the RecoveryUnit to NULL.
     *
     * Used to transfer ownership of storage engine state from OperationContext
     * to ClientCursor for getMore-able queries.
     *
     * Note that we don't allow the top-level locks to be stored across getMore.
     * We rely on active cursors being killed when collections or databases are dropped,
     * or when collection metadata changes.
     */
    virtual RecoveryUnit* releaseRecoveryUnit() = 0;

    /**
     * Associates the OperatingContext with a different RecoveryUnit for getMore or
     * subtransactions, see RecoveryUnitSwap. The new state is passed and the old state is
     * returned separately even though the state logically belongs to the RecoveryUnit,
     * as it is managed by the OperationContext.
     */
    virtual RecoveryUnitState setRecoveryUnit(RecoveryUnit* unit, RecoveryUnitState state) = 0;

    /**
     * Interface for locking.  Caller DOES NOT own pointer.
     */
    Locker* lockState() const {
        return _locker;
    }

    // --- operation level info? ---

    /**
     * Raises a UserAssertion if this operation is in a killed state.
     */
    void checkForInterrupt();

    /**
     * Returns Status::OK() unless this operation is in a killed state.
     */
    Status checkForInterruptNoAssert();

    /**
     * Delegates to CurOp, but is included here to break dependencies.
     * Caller does not own the pointer.
     *
     * Caller must have locked the "Client" associated with this context.
     */
    virtual ProgressMeter* setMessage_inlock(const char* msg,
                                             const std::string& name = "Progress",
                                             unsigned long long progressMeterTotal = 0,
                                             int secondsBetween = 3) = 0;

    /**
     * Delegates to CurOp, but is included here to break dependencies.
     *
     * TODO: We return a string because of hopefully transient CurOp thread-unsafe insanity.
     */
    virtual std::string getNS() const = 0;

    /**
     * Returns the service context under which this operation context runs.
     */
    ServiceContext* getServiceContext() const {
        return _client->getServiceContext();
    }

    /**
     * Returns the client under which this context runs.
     */
    Client* getClient() const {
        return _client;
    }

    /**
     * Returns the operation ID associated with this operation.
     */
    unsigned int getOpID() const {
        return _opId;
    }

    /**
     * @return true if this instance is primary for this namespace
     */
    virtual bool isPrimaryFor(StringData ns) = 0;

    /**
     * Returns WriteConcernOptions of the current operation
     */
    const WriteConcernOptions& getWriteConcern() const {
        return _writeConcern;
    }

    void setWriteConcern(const WriteConcernOptions& writeConcern) {
        _writeConcern = writeConcern;
    }

    /**
     * Set whether or not operations should generate oplog entries.
     */
    void setReplicatedWrites(bool writesAreReplicated = true) {
        _writesAreReplicated = writesAreReplicated;
    }

    /**
     * Returns true if operations should generate oplog entries.
     */
    bool writesAreReplicated() const {
        return _writesAreReplicated;
    }

    /**
     * Marks this operation as killed so that subsequent calls to checkForInterrupt and
     * checkForInterruptNoAssert by the thread executing the operation will start returning the
     * specified error code.
     *
     * If multiple threads kill the same operation with different codes, only the first code will
     * be preserved.
     *
     * May be called by any thread that has locked the Client owning this operation context.
     */
    void markKilled(ErrorCodes::Error killCode = ErrorCodes::Interrupted);

    /**
     * Returns the code passed to markKilled if this operation context has been killed previously
     * or ErrorCodes::OK otherwise.
     *
     * May be called by any thread that has locked the Client owning this operation context, or
     * without lock by the thread executing on behalf of this operation context.
     */
    ErrorCodes::Error getKillStatus() const {
        return _killCode.loadRelaxed();
    }

    /**
     * Shortcut method, which checks whether getKillStatus returns a non-OK value. Has the same
     * concurrency rules as getKillStatus.
     */
    bool isKillPending() const {
        return getKillStatus() != ErrorCodes::OK;
    }

    /**
     * Returns the amount of time since the operation was constructed. Uses the system's most
     * precise tick source, and may not be cheap to call in a tight loop.
     */
    Microseconds getElapsedTime() const {
        return _elapsedTime.elapsed();
    }

    /**
     * Sets the deadline for this operation to the given point in time.
     *
     * To remove a deadline, pass in Date_t::max().
     */
    void setDeadlineByDate(Date_t when);

    /**
     * Sets the deadline for this operation to the maxTime plus the current time reported
     * by the ServiceContext's fast clock source.
     */
    void setDeadlineAfterNowBy(Microseconds maxTime);
    template <typename D>
    void setDeadlineAfterNowBy(D maxTime) {
        if (maxTime <= D::zero()) {
            maxTime = D::zero();
        }
        if (maxTime <= Microseconds::max()) {
            setDeadlineAfterNowBy(duration_cast<Microseconds>(maxTime));
        } else {
            setDeadlineByDate(Date_t::max());
        }
    }

    /**
     * Returns true if this operation has a deadline.
     */
    bool hasDeadline() const {
        return getDeadline() < Date_t::max();
    }

    /**
     * Returns the deadline for this operation, or Date_t::max() if there is no deadline.
     */
    Date_t getDeadline() const {
        return _deadline;
    }

    //
    // Legacy "max time" methods for controlling operation deadlines.
    //

    /**
     * Returns the number of microseconds remaining for this operation's time limit, or the
     * special value Microseconds::max() if the operation has no time limit.
     */
    Microseconds getRemainingMaxTimeMicros() const;

protected:
    OperationContext(Client* client, unsigned int opId, Locker* locker);

    RecoveryUnitState _ruState = kNotInUnitOfWork;

private:
    /**
     * Returns true if this operation has a deadline and it has passed according to the fast clock
     * on ServiceContext.
     */
    bool hasDeadlineExpired() const;

    /**
     * Sets the deadline and maxTime as described. It is up to the caller to ensure that
     * these correctly correspond.
     */
    void setDeadlineAndMaxTime(Date_t when, Microseconds maxTime);

    friend class WriteUnitOfWork;
    Client* const _client;
    const unsigned int _opId;

    // Not owned.
    Locker* const _locker;

    // Follows the values of ErrorCodes::Error. The default value is 0 (OK), which means the
    // operation is not killed. If killed, it will contain a specific code. This value changes only
    // once from OK to some kill code.
    AtomicWord<ErrorCodes::Error> _killCode{ErrorCodes::OK};

    WriteConcernOptions _writeConcern;

    Date_t _deadline =
        Date_t::max();  // The timepoint at which this operation exceeds its time limit.

    // Max operation time requested by the user or by the cursor in the case of a getMore with no
    // user-specified maxTime. This is tracked with microsecond granularity for the purpose of
    // assigning unused execution time back to a cursor at the end of an operation, only. The
    // _deadline and the service context's fast clock are the only values consulted for determining
    // if the operation's timelimit has been exceeded.
    Microseconds _maxTime = Microseconds::max();

    // Timer counting the elapsed time since the construction of this OperationContext.
    Timer _elapsedTime;

    bool _writesAreReplicated = true;
};

class WriteUnitOfWork {
    MONGO_DISALLOW_COPYING(WriteUnitOfWork);

public:
    WriteUnitOfWork(OperationContext* txn)
        : _txn(txn),
          _committed(false),
          _toplevel(txn->_ruState == OperationContext::kNotInUnitOfWork) {
        uassert(ErrorCodes::IllegalOperation,
                "Cannot execute a write operation in read-only mode",
                !storageGlobalParams.readOnly);
        _txn->lockState()->beginWriteUnitOfWork();
        if (_toplevel) {
            _txn->recoveryUnit()->beginUnitOfWork(_txn);
            _txn->_ruState = OperationContext::kActiveUnitOfWork;
        }
    }

    ~WriteUnitOfWork() {
        dassert(!storageGlobalParams.readOnly);
        if (!_committed) {
            invariant(_txn->_ruState != OperationContext::kNotInUnitOfWork);
            if (_toplevel) {
                _txn->recoveryUnit()->abortUnitOfWork();
                _txn->_ruState = OperationContext::kNotInUnitOfWork;
            } else {
                _txn->_ruState = OperationContext::kFailedUnitOfWork;
            }
            _txn->lockState()->endWriteUnitOfWork();
        }
    }

    void commit() {
        invariant(!_committed);
        invariant(_txn->_ruState == OperationContext::kActiveUnitOfWork);
        if (_toplevel) {
            _txn->recoveryUnit()->commitUnitOfWork();
            _txn->_ruState = OperationContext::kNotInUnitOfWork;
        }
        _txn->lockState()->endWriteUnitOfWork();
        _committed = true;
    }

private:
    OperationContext* const _txn;

    bool _committed;
    bool _toplevel;
};


/**
 * RAII-style class to mark the scope of a transaction. ScopedTransactions may be nested.
 * An outermost ScopedTransaction calls abandonSnapshot() on destruction, so that the storage
 * engine can release resources, such as snapshots or locks, that it may have acquired during
 * the transaction. Note that any writes are committed in nested WriteUnitOfWork scopes,
 * so write conflicts cannot happen on completing a ScopedTransaction.
 *
 * TODO: The ScopedTransaction should hold the global lock
 */
class ScopedTransaction {
    MONGO_DISALLOW_COPYING(ScopedTransaction);

public:
    /**
     * The mode for the transaction indicates whether the transaction will write (MODE_IX) or
     * only read (MODE_IS), or needs to run without other writers (MODE_S) or any other
     * operations (MODE_X) on the server.
     */
    ScopedTransaction(OperationContext* txn, LockMode mode) : _txn(txn) {}

    ~ScopedTransaction() {
        if (!_txn->lockState()->isLocked()) {
            _txn->recoveryUnit()->abandonSnapshot();
        }
    }

private:
    OperationContext* _txn;
};

}  // namespace mongo
