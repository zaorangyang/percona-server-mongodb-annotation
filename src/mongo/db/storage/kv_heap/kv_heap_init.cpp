// kv_heap_init.cpp

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

#include "mongo/base/error_codes.h"
#include "mongo/base/init.h"
#include "mongo/base/status.h"
#include "mongo/db/global_environment_experiment.h"
#include "mongo/db/storage/kv/kv_storage_engine.h"
#include "mongo/db/storage/kv_heap/kv_heap_engine.h"
#include "mongo/db/storage_options.h"

namespace mongo {

    class KVHeapStorageEngine : public KVStorageEngine {
        MONGO_DISALLOW_COPYING( KVHeapStorageEngine );
    public:
        KVHeapStorageEngine() :
            KVStorageEngine(new KVHeapEngine()) {
        }

        virtual ~KVHeapStorageEngine() { }
    };

    namespace {

        class KVHeapEngineFactory : public StorageEngine::Factory {
        public:
            virtual ~KVHeapEngineFactory() { }
            virtual StorageEngine* create(const StorageGlobalParams& params,
                                          const StorageEngineLockFile& lockFile) const {
                return new KVHeapStorageEngine();
            }
            virtual StringData getCanonicalName() const {
                return "kv_heap";
            }
            virtual Status validateCollectionStorageOptions(const BSONObj& options) const {
                if (!options.isEmpty()) {
                    return Status(ErrorCodes::BadValue, "no options allowed for kv_heap engine");
                }
                return Status::OK();
            }
            virtual Status validateIndexStorageOptions(const BSONObj& options) const {
                if (!options.isEmpty()) {
                    return Status(ErrorCodes::BadValue, "no options allowed for kv_heap engine");
                }
                return Status::OK();
            }
            virtual Status validateMetadata(const StorageEngineMetadata& metadata,
                                            const StorageGlobalParams& params) const {
                return Status::OK();
            }
            virtual BSONObj createMetadataOptions(const StorageGlobalParams& params) const {
                return BSONObj();
            }
        };

    } // namespace

    MONGO_INITIALIZER_WITH_PREREQUISITES(KVHeapEngineInit,
                                         ("SetGlobalEnvironment"))
                                         (InitializerContext* context) {
        getGlobalEnvironment()->registerStorageEngine("kv_heap", new KVHeapEngineFactory());
        return Status::OK();
    }

}  // namespace mongo
