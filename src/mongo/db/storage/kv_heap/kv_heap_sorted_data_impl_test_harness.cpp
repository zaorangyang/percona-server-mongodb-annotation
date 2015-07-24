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

#include "mongo/db/storage/index_entry_comparison.h"
#include "mongo/db/storage/kv/dictionary/kv_sorted_data_impl.h"
#include "mongo/db/storage/kv_heap/kv_heap_dictionary.h"
#include "mongo/db/storage/kv_heap/kv_heap_recovery_unit.h"
#include "mongo/db/storage/sorted_data_interface_test_harness.h"

namespace mongo {

    class KVSortedDataImplHarness : public HarnessHelper {
    public:
        virtual SortedDataInterface* newSortedDataInterface(bool unqiue) {
            std::auto_ptr<OperationContext> opCtx(newOperationContext());
            IndexEntryComparison iec(Ordering::make(BSONObj()));
            std::auto_ptr<KVDictionary> db(new KVHeapDictionary(KVDictionary::Encoding::forIndex(Ordering::make(BSONObj()))));
            return new KVSortedDataImpl(db.release(), opCtx.get(), NULL);
        }

        virtual RecoveryUnit* newRecoveryUnit() {
            return new KVHeapRecoveryUnit();
        }
    };

    HarnessHelper* newHarnessHelper() {
        return new KVSortedDataImplHarness();
    }
}
