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

#pragma once

#include <deque>

#include "mongo/db/storage/kv/dictionary/kv_recovery_unit.h"

#include <boost/shared_ptr.hpp>
#include <ftcxx/db_env.hpp>
#include <ftcxx/db_txn.hpp>

namespace mongo {

    class OperationContext;
    class TokuFTStorageEngine;

    class TokuFTRecoveryUnit : public KVRecoveryUnit {
        MONGO_DISALLOW_COPYING(TokuFTRecoveryUnit);
    public:
        TokuFTRecoveryUnit(const ftcxx::DBEnv &env);

        virtual ~TokuFTRecoveryUnit();

        void beginUnitOfWork(OperationContext *opCtx);

        void commitUnitOfWork();

        void endUnitOfWork();

        bool awaitCommit();

        void commitAndRestart();

        void registerChange(Change* change);

        //
        // The remaining methods probably belong on DurRecoveryUnit rather than on the interface.
        //

        void *writingPtr(void *data, size_t len);

        void setRollbackWritesDisabled() {
            _rollbackWritesDisabled = true;
        }

        KVRecoveryUnit* newRecoveryUnit() const {
            return new TokuFTRecoveryUnit(_env);
        }

        bool hasSnapshot() const;

        SnapshotId getSnapshotId() const;

    private:
        typedef boost::shared_ptr<Change> ChangePtr;
        typedef std::vector<ChangePtr> Changes;

        const ftcxx::DBEnv &_env;
        ftcxx::DBTxn _txn;

        int _depth;
        Changes _changes;
        bool _rollbackWritesDisabled;

        bool _knowsAboutReplicationState;
        bool _isReplicaSetSecondary;

        static bool _opCtxIsWriting(OperationContext *opCtx);

        static int _commitFlags();

    public:
        // -- TokuFT Specific

        DB_TXN *db_txn() const {
            return _txn.txn();
        }

        const ftcxx::DBTxn &txn(OperationContext *opCtx);

        /**
         * ReplicationCoordinator::getCurrentMemberState takes a lock, which is why we'd like to
         * cache this as long as we can.  The recovery unit is probably the longest lived object we
         * have that is (hopefully) guaranteed not to outlast a state transition.  This doesn't
         * quite belong here, but that's the rationale.
         */
        bool isReplicaSetSecondary();
    };

}  // namespace mongo
