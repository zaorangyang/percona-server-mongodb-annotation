// tokuft_engine_global_accessor.cpp

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

#include "mongo/base/checked_cast.h"
#include "mongo/db/global_environment_experiment.h"
#include "mongo/db/storage_options.h"
#include "mongo/db/storage/kv/kv_storage_engine.h"
#include "mongo/db/storage/tokuft/tokuft_engine.h"
#include "mongo/util/assert_util.h"

#include <ftcxx/db_env.hpp>

namespace mongo {

    bool globalStorageEngineIsTokuFT() {
        return storageGlobalParams.engine == "tokuft";
    }

    TokuFTEngine* tokuftGlobalEngine() {
        StorageEngine* storageEngine = getGlobalEnvironment()->getGlobalStorageEngine();
        massert(28616, "no storage engine available", storageEngine);
        KVStorageEngine* kvStorageEngine = checked_cast<KVStorageEngine*>(storageEngine);
        KVEngine* kvEngine = kvStorageEngine->getEngine();
        invariant(kvEngine);
        TokuFTEngine* tokuftEngine = checked_cast<TokuFTEngine*>(kvEngine);
        return tokuftEngine;
    }

    ftcxx::DBEnv& tokuftGlobalEnv() {
        return tokuftGlobalEngine()->env();
    }

} // namespace mongo
