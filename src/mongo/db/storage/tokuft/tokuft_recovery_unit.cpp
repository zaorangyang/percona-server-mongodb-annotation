/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved.

    Percona Server for MongoDB is free software: you can redistribute
    it and/or modify it under the terms of the GNU Affero General
    Public License, version 3, as published by the Free Software
    Foundation.

    Percona Server for MongoDB is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied
    warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public
    License along with Percona Server for MongoDB.  If not, see
    <http://www.gnu.org/licenses/>.
======= */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/db/concurrency/locker_noop.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/member_state.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/repl/replication_coordinator_global.h"
#include "mongo/db/storage/tokuft/tokuft_recovery_unit.h"
#include "mongo/db/storage/tokuft/tokuft_global_options.h"
#include "mongo/util/log.h"

#include <ftcxx/db_env.hpp>
#include <ftcxx/db_txn.hpp>

namespace mongo {

    TokuFTRecoveryUnit::TokuFTRecoveryUnit(const ftcxx::DBEnv &env) :
        // We use depth to track transaction nesting
        _env(env), _txn(), _depth(0), _rollbackWritesDisabled(false), _knowsAboutReplicationState(false) {
    }

    TokuFTRecoveryUnit::~TokuFTRecoveryUnit() {
        invariant(_depth == 0);
        invariant(_changes.size() == 0);
    }

    void TokuFTRecoveryUnit::beginUnitOfWork(OperationContext *opCtx) {
        _depth++;
        // Make sure we create a txn here so that we have a snapshot for getSnapshotId() later.
        txn(opCtx);
    }

    int TokuFTRecoveryUnit::_commitFlags() {
        if (tokuftGlobalOptions.engineOptions.journalCommitInterval == 0) {
            return 0;
        } else {
            return DB_TXN_NOSYNC;
        }
    }

    void TokuFTRecoveryUnit::commitUnitOfWork() {
        invariant(_depth > 0);

        if (_depth > 1) {
            return;
        }

        for (Changes::iterator it = _changes.begin(), end = _changes.end();
             it != end; ++it) {
            (*it)->commit();
        }
        _changes.clear();

        if (hasSnapshot()) {
            _txn.commit(_commitFlags());
        }
        _txn = ftcxx::DBTxn();
    }

    void TokuFTRecoveryUnit::commitAndRestart() {
        invariant(_depth == 0);
        invariant(_changes.size() == 0);

        if (hasSnapshot()) {
            _txn.commit(_commitFlags());
        }
        _txn = ftcxx::DBTxn();
    }

    void TokuFTRecoveryUnit::endUnitOfWork() {
        invariant(_depth > 0);

        if (--_depth > 0) {
            return;
        }

        for (Changes::reverse_iterator it = _changes.rbegin(), end = _changes.rend();
             it != end; ++it) {
            (*it)->rollback();
        }
        _changes.clear();

        if (_rollbackWritesDisabled && hasSnapshot()) {
            // Probably cheaper to commit than to send abort messages,
            // especially if they're all inserts.
            _txn.commit(DB_TXN_NOSYNC);
        }
        _rollbackWritesDisabled = false;
        _txn = ftcxx::DBTxn();
    }

    bool TokuFTRecoveryUnit::awaitCommit() {
        invariant(_env.env() != NULL);

        // The underlying transaction needs to have been committed to
        // the ydb environment, otherwise it's still provisional and
        // cannot be guaranteed durable even after a sync to the log.
        invariant(!hasSnapshot());

        // Once the log is synced, the transaction is fully durable.
        const int r = _env.env()->log_flush(_env.env(), NULL);
        return r == 0;
    }

    void TokuFTRecoveryUnit::registerChange(RecoveryUnit::Change *change) {
        _changes.push_back(ChangePtr(change));
    }

    //
    // The remaining methods probably belong on DurRecoveryUnit rather than on the interface.
    //
    
    void *TokuFTRecoveryUnit::writingPtr(void *data, size_t len) {
        log() << "tokuft-engine: writingPtr does nothing" << std::endl;
        return data;
    }

    bool TokuFTRecoveryUnit::hasSnapshot() const {
        return _txn.txn() != NULL;
    }

    SnapshotId TokuFTRecoveryUnit::getSnapshotId() const {
        if (!hasSnapshot()) {
            return SnapshotId();
        }
        return SnapshotId(_txn.id());
    }

    bool TokuFTRecoveryUnit::_opCtxIsWriting(OperationContext *opCtx) {
        const Locker *state = opCtx->lockState();
        invariant(state != NULL);
        const LockMode mode =
                (dynamic_cast<const LockerNoop *>(state) != NULL
                 // Only for c++ tests, assume tests can do whatever without proper locks.
                 ? MODE_X
                 // We don't have the ns so just check the global resource, generally it should have
                 // the IX or IS lock.
                 : state->getLockMode(ResourceId(RESOURCE_GLOBAL, 1ULL)));
        return mode == MODE_IX || mode == MODE_X;
    }

    const ftcxx::DBTxn &TokuFTRecoveryUnit::txn(OperationContext *opCtx) {
        if (_txn.is_read_only() && _opCtxIsWriting(opCtx)) {
            _txn = ftcxx::DBTxn();
        }
        if (!hasSnapshot()) {
            // No txn exists yet, create one on-demand.
            // If locked for write, get a serializable txn, otherwise get a read-only one.
            int flags;
            if (_opCtxIsWriting(opCtx)) {
                flags = DB_SERIALIZABLE;
            } else {
                flags = DB_TXN_SNAPSHOT | DB_TXN_READ_ONLY;
            }
            _txn = ftcxx::DBTxn(_env, flags);
        }
        return _txn;
    }

    bool TokuFTRecoveryUnit::isReplicaSetSecondary() {
        if (!_knowsAboutReplicationState) {
            repl::ReplicationCoordinator *coord = repl::getGlobalReplicationCoordinator();
            _isReplicaSetSecondary = (coord != NULL &&
                                      coord->getReplicationMode() == repl::ReplicationCoordinator::modeReplSet &&
                                      coord->getMemberState().secondary());
            _knowsAboutReplicationState = true;
        }
        return _isReplicaSetSecondary;
    }

}  // namespace mongo
