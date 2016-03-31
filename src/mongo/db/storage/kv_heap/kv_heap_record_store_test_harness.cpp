// kv_heap_record_store_test_harness.cpp

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

#include "mongo/db/catalog/collection_options.h"
#include "mongo/db/storage/kv/dictionary/kv_record_store.h"
#include "mongo/db/storage/kv/dictionary/kv_record_store_capped.h"
#include "mongo/db/storage/kv_heap/kv_heap_dictionary.h"
#include "mongo/db/storage/kv_heap/kv_heap_recovery_unit.h"
#include "mongo/db/storage/record_store_test_harness.h"

namespace mongo {

    class KVRecordStoreHarnessHelper : public HarnessHelper {
    public:
        virtual std::unique_ptr<RecordStore> newNonCappedRecordStore() {
            std::unique_ptr<OperationContext> opCtx(newOperationContext());
            std::auto_ptr<KVDictionary> db(new KVHeapDictionary());
            const StringData ns("kvRecordStoreTestHarnessNamespace");
            KVRecordStore *p = new KVRecordStore(db.release(), opCtx.get(), ns, ns, CollectionOptions(), NULL);
            return std::unique_ptr<RecordStore>(p);
        }

        virtual std::unique_ptr<RecordStore> newCappedRecordStore(int64_t cappedSizeBytes = HarnessHelper::kDefaultCapedSizeBytes, 
                                                                  int64_t cappedMaxDocs = -1) {
            std::unique_ptr<OperationContext> opCtx(newOperationContext());
            std::auto_ptr<KVDictionary> db(new KVHeapDictionary());
            const StringData ns("kvRecordStoreTestHarnessNamespace");
            CollectionOptions options;
            options.cappedSize = cappedSizeBytes;
            options.cappedMaxDocs = cappedMaxDocs;
            KVRecordStoreCapped *p = new KVRecordStoreCapped(db.release(), opCtx.get(), ns, ns, options, NULL, false);
            return std::unique_ptr<RecordStore>(p);
        }

        virtual bool supportsDocLocking() {
            return false;
        }

        virtual RecoveryUnit* newRecoveryUnit() {
            return new KVHeapRecoveryUnit();
        }
    };

    std::unique_ptr<HarnessHelper> newHarnessHelper() {
        return std::unique_ptr<HarnessHelper> (new KVRecordStoreHarnessHelper());
    }

} // namespace mongo
