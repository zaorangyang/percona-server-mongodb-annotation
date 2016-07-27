// inmemory_init.cpp

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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include "mongo/base/init.h"
#include "mongo/db/catalog/collection_options.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/service_context.h"
#include "mongo/db/service_context_d.h"
#include "mongo/db/storage/inmemory/inmemory_global_options.h"
#include "mongo/db/storage/kv/kv_storage_engine.h"
#include "mongo/db/storage/storage_engine_lock_file.h"
#include "mongo/db/storage/storage_engine_metadata.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_global_options.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_index.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_kv_engine.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_record_store.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_server_status.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_util.h"
#include "mongo/util/log.h"

namespace mongo {

namespace {
const std::string kInMemoryEngineName = "inMemory";

class InMemoryFactory : public StorageEngine::Factory {
public:
    virtual ~InMemoryFactory() {}
    virtual StorageEngine* create(const StorageGlobalParams& params,
                                  const StorageEngineLockFile&) const {
        syncInMemoryAndWiredTigerOptions();

        size_t cacheMB = static_cast<size_t>(inMemoryGlobalOptions.cacheSizeGB * 1024);
        if (cacheMB == 0) {
            // Since the user didn't provide a cache size, choose a reasonable default value.
            // We want to reserve 1GB for the system and binaries, but it's not bad to
            // leave a fair amount left over for pagecache since that's compressed storage.
            ProcessInfo pi;
            double memSizeMB = pi.getMemSizeMB();
            cacheMB = static_cast<size_t>(std::max((memSizeMB - 1024) * 0.6, 256.0));
        }
        const bool durable = false;
        const bool ephemeral = true;
        WiredTigerKVEngine* kv = new WiredTigerKVEngine(getCanonicalName().toString(),
                                                        params.dbpath,
                                                        wiredTigerGlobalOptions.engineConfig,
                                                        cacheMB,
                                                        durable,
                                                        ephemeral,
                                                        params.repair,
                                                        true);
        kv->setRecordStoreExtraOptions(wiredTigerGlobalOptions.collectionConfig);
        kv->setSortedDataInterfaceExtraOptions(wiredTigerGlobalOptions.indexConfig);
        // Intentionally leaked.
        new WiredTigerServerStatusSection(kv);

        KVStorageEngineOptions options;
        options.directoryPerDB = params.directoryperdb;
        options.directoryForIndexes = wiredTigerGlobalOptions.directoryForIndexes;
        options.forRepair = params.repair;
        return new KVStorageEngine(kv, options);
    }

    virtual StringData getCanonicalName() const {
        return kInMemoryEngineName;
    }

    virtual Status validateCollectionStorageOptions(const BSONObj& options) const {
        return WiredTigerRecordStore::parseOptionsField(options).getStatus();
    }

    virtual Status validateIndexStorageOptions(const BSONObj& options) const {
        return WiredTigerIndex::parseIndexOptions(options).getStatus();
    }

    virtual Status validateMetadata(const StorageEngineMetadata& metadata,
                                    const StorageGlobalParams& params) const {
        Status status =
            metadata.validateStorageEngineOption("directoryPerDB", params.directoryperdb);
        if (!status.isOK()) {
            return status;
        }

        status = metadata.validateStorageEngineOption("directoryForIndexes",
                                                      wiredTigerGlobalOptions.directoryForIndexes);
        if (!status.isOK()) {
            return status;
        }

        return Status::OK();
    }

    virtual BSONObj createMetadataOptions(const StorageGlobalParams& params) const {
        BSONObjBuilder builder;
        builder.appendBool("directoryPerDB", params.directoryperdb);
        builder.appendBool("directoryForIndexes", wiredTigerGlobalOptions.directoryForIndexes);
        return builder.obj();
    }

private:
    static void syncInMemoryAndWiredTigerOptions() {
        // Re-create WiredTiger options to fill it with default values
        wiredTigerGlobalOptions = WiredTigerGlobalOptions();

        wiredTigerGlobalOptions.statisticsLogDelaySecs =
            inMemoryGlobalOptions.statisticsLogDelaySecs;
        // Set InMemory configuration as part of engineConfig string
        wiredTigerGlobalOptions.engineConfig =
            "in_memory=true,"
            "log=(enabled=false),"
            "file_manager=(close_idle_time=0),"
            "checkpoint=(wait=0,log_size=0),";
        // Don't change the order as user-defined config string should go
        // AFTER InMemory config to override it if needed
        wiredTigerGlobalOptions.engineConfig += inMemoryGlobalOptions.engineConfig;
        // Don't change the order as user-defined collection & index config strings should go
        // BEFORE InMemory configs to disable cache_resident option later
        wiredTigerGlobalOptions.collectionConfig = inMemoryGlobalOptions.collectionConfig;
        wiredTigerGlobalOptions.collectionConfig += ",cache_resident=false";
        wiredTigerGlobalOptions.indexConfig = inMemoryGlobalOptions.indexConfig;
        wiredTigerGlobalOptions.indexConfig += ",cache_resident=false";
    }
};
}  // namespace

MONGO_INITIALIZER_WITH_PREREQUISITES(InMemoryEngineInit, ("SetGlobalEnvironment"))
(InitializerContext* context) {
    getGlobalServiceContext()->registerStorageEngine(kInMemoryEngineName, new InMemoryFactory());

    return Status::OK();
}
}
