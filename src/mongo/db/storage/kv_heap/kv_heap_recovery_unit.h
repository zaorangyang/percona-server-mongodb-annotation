// kv_heap_recovery_unit.h

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

#include <algorithm>
#include <string.h>
#include <vector>

#include "mongo/db/storage/kv/dictionary/kv_recovery_unit.h"
#include "mongo/db/storage/kv/slice.h"
#include "mongo/util/assert_util.h"

namespace mongo {

    namespace {

        int sliceCompare(const Slice &a, const Slice &b) {
            const size_t cmp_len = std::min(a.size(), b.size());
            const int c = memcmp(a.data(), b.data(), cmp_len);
            return c || (a.size() < b.size() ? -1 : 1);
        }

    }

    class KVHeapDictionary;

    class KVHeapChange : public RecoveryUnit::Change {
    protected:
        KVHeapDictionary *_dict;
        Slice _key;
        Slice _value;

    public:
        KVHeapChange(KVHeapDictionary *dict, const Slice &key, const Slice &value)
            : _dict(dict),
              _key(key.owned()),
              _value(value.owned())
        {}

        KVHeapChange(KVHeapDictionary *dict, const Slice &key)
            : _dict(dict),
              _key(key.owned()),
              _value()
        {}

        virtual ~KVHeapChange() {}

        virtual void commit() {}
    };


    class InsertOperation : public KVHeapChange {
        bool _wasDeleted;

    public:
        InsertOperation(KVHeapDictionary *dict, const Slice &key, const Slice &value)
            : KVHeapChange(dict, key, value),
              _wasDeleted(false)
        {}

        InsertOperation(KVHeapDictionary *dict, const Slice &key)
            : KVHeapChange(dict, key),
              _wasDeleted(true)
        {}

        virtual void rollback();
    };

    class DeleteOperation : public KVHeapChange {
    public:
        DeleteOperation(KVHeapDictionary *dict, const Slice &key, const Slice &value)
            : KVHeapChange(dict, key, value)
        {}

        virtual void rollback();
    };

    class KVHeapRecoveryUnit : public KVRecoveryUnit {
        MONGO_DISALLOW_COPYING(KVHeapRecoveryUnit);

        std::vector< std::shared_ptr<Change> > _ops;

    public:
        KVHeapRecoveryUnit() {}
        virtual ~KVHeapRecoveryUnit() {}

        virtual void beginUnitOfWork(OperationContext *opCtx) {}

        virtual void commitUnitOfWork();

        virtual void commitAndRestart();

        virtual void endUnitOfWork();

        virtual bool awaitCommit() {
            return true;
        }

        virtual void* writingPtr(void* data, size_t len) {
            // die
            return data;
        }

        virtual void syncDataAndTruncateJournal() {}

        virtual void registerChange(Change* change);

        virtual void setRollbackWritesDisabled() {}

        virtual KVRecoveryUnit *newRecoveryUnit() const {
            return new KVHeapRecoveryUnit();
        }

        virtual bool hasSnapshot() const {
            // not a doc-level locking engine
            invariant(false);
        }

        virtual SnapshotId getSnapshotId() const { return SnapshotId(); }

        static KVHeapRecoveryUnit* getKVHeapRecoveryUnit(OperationContext* opCtx);
    };

} // namespace mongo
