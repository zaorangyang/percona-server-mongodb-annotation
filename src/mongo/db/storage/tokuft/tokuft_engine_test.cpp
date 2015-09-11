// tokuft_engine_test.cpp

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

#include <boost/filesystem/operations.hpp>
#include <boost/scoped_ptr.hpp>

#include "mongo/db/operation_context_noop.h"
#include "mongo/db/storage/kv/kv_engine.h"
#include "mongo/db/storage/kv/kv_engine_test_harness.h"
#include "mongo/db/storage/tokuft/tokuft_engine.h"
#include "mongo/unittest/temp_dir.h"

namespace mongo {

    class TokuFTEngineHarnessHelper : public KVHarnessHelper {
    public:
        TokuFTEngineHarnessHelper() : _dbpath("mongo-perconaft-engine-test") {
            boost::filesystem::remove_all(_dbpath.path());
            boost::filesystem::create_directory(_dbpath.path());
            _engine.reset(new TokuFTEngine(_dbpath.path()));
        }

        virtual ~TokuFTEngineHarnessHelper() {
            _doCleanShutdown();
        }

        virtual KVEngine* getEngine() { return _engine.get(); }

        virtual KVEngine* restartEngine() {
            _doCleanShutdown();
            _engine.reset(new TokuFTEngine(_dbpath.path()));
            return _engine.get();
        }

    private:
        void _doCleanShutdown() {
            if (_engine) {
                _engine->cleanShutdown();
                _engine.reset();
            }
        }

        unittest::TempDir _dbpath;

        boost::scoped_ptr<TokuFTEngine> _engine;
    };

    KVHarnessHelper* KVHarnessHelper::create() {
        return new TokuFTEngineHarnessHelper();
    }

}
