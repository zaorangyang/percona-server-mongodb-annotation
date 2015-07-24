// kv_dictionary_test_harness.h

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

#include "mongo/db/operation_context_noop.h"
#include "mongo/db/storage/kv/dictionary/kv_dictionary.h"

namespace mongo {

    class KVDictionary;
    class RecoveryUnit;

    class HarnessHelper {
    public:
        virtual ~HarnessHelper() { }

        virtual KVDictionary* newKVDictionary() = 0;

        virtual RecoveryUnit* newRecoveryUnit() = 0;

        virtual OperationContext* newOperationContext() {
            return new OperationContextNoop( newRecoveryUnit() );
        }
    };

    HarnessHelper* newHarnessHelper();
}
