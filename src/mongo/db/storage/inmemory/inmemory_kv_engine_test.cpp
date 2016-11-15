// inmemory_kv_engine_test.cpp

/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2016, Percona and/or its affiliates. All rights reserved.

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

#include "mongo/db/storage/wiredtiger/wiredtiger_kv_engine.h"
#include "mongo/stdx/memory.h"
#include "mongo/unittest/temp_dir.h"
#include "mongo/util/clock_source_mock.h"

namespace mongo {

namespace {
    const std::string kInMemoryEngineName = "inMemory";
}

class InMemoryKVHarnessHelper : public KVHarnessHelper {
public:
    InMemoryKVHarnessHelper() : _dbpath("inmem-kv-harness") {
        const bool readOnly = false;
        _engine.reset(new WiredTigerKVEngine(
            kInMemoryEngineName, _dbpath.path(), _cs.get(),
            "in_memory=true,"
            "log=(enabled=false),"
            "file_manager=(close_idle_time=0),"
            "checkpoint=(wait=0,log_size=0)",
            100, false, true, false, readOnly));
    }

    virtual ~InMemoryKVHarnessHelper() {
        _engine.reset(NULL);
    }

    virtual KVEngine* restartEngine() {
        // Don't reset the engine since it doesn't persist anything
        // and all the data will be lost.
        return _engine.get();
    }

    virtual KVEngine* getEngine() {
        return _engine.get();
    }

private:
    const std::unique_ptr<ClockSource> _cs = stdx::make_unique<ClockSourceMock>();
    unittest::TempDir _dbpath;
    std::unique_ptr<WiredTigerKVEngine> _engine;
};

KVHarnessHelper* KVHarnessHelper::create() {
    return new InMemoryKVHarnessHelper();
}
}
