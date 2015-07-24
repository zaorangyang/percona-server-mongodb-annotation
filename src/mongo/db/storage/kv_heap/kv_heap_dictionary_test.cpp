// kv_dictionary_heap_test.cpp

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

#include "mongo/db/storage/kv/dictionary/kv_dictionary_test_harness.h"
#include "mongo/db/storage/kv_heap/kv_heap_dictionary.h"
#include "mongo/db/storage/kv_heap/kv_heap_recovery_unit.h"
#include "mongo/db/storage/recovery_unit_noop.h"

namespace mongo {

    class KVHeapDictionaryHarnessHelper : public HarnessHelper {
    public:
        virtual ~KVHeapDictionaryHarnessHelper() { }

        virtual KVDictionary* newKVDictionary() {
            return new KVHeapDictionary();
        }

        virtual RecoveryUnit* newRecoveryUnit() {
            return new KVHeapRecoveryUnit();
        }

        virtual OperationContext* newOperationContext() {
            return new OperationContextNoop( newRecoveryUnit() );
        }
    };

    HarnessHelper* newHarnessHelper() {
        return new KVHeapDictionaryHarnessHelper();
    }
}
