// kv_heap_engine_test.cpp

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

#include "mongo/db/storage/kv/kv_engine_test_harness.h"
#include "mongo/db/storage/kv_heap/kv_heap_engine.h"

namespace mongo {

    class KVHeapEngineHarnessHelper : public KVHarnessHelper {
    public:
        KVHeapEngineHarnessHelper() {
            _engine.reset(new KVHeapEngine());
        }
        virtual ~KVHeapEngineHarnessHelper() = default;

        virtual KVEngine* getEngine() { return _engine.get(); }

        virtual KVEngine* restartEngine() {
            // NULL means we do not support restart (since this is a transient, test-only engine)
            return NULL;
        }

    private:
        boost::scoped_ptr<KVHeapEngine> _engine;
    };

    KVHarnessHelper* KVHarnessHelper::create() {
        return new KVHeapEngineHarnessHelper();
    }
}
