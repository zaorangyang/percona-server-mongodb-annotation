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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/base/init.h"
#include "mongo/db/service_context_d.h"
#include "mongo/db/service_context.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/db/storage/kv/kv_storage_engine.h"
#include "mongo/db/storage/storage_engine_metadata.h"
#include "mongo/db/storage/tokuft/tokuft_dictionary_options.h"
#include "mongo/db/storage/tokuft/tokuft_engine.h"
#include "mongo/db/storage/tokuft/tokuft_global_options.h"
#include "mongo/util/log.h"

namespace mongo {

    class TokuFTFactory : public StorageEngine::Factory {
    public:
        virtual ~TokuFTFactory() { }
        KVStorageEngine *createTokuFTEngine(const std::string &path, const KVStorageEngineOptions &options) const {
            TokuFTEngine *p = new TokuFTEngine(path);
            return new KVStorageEngine(p, options);
        }
        virtual StorageEngine *create(const StorageGlobalParams &params,
                                      const StorageEngineLockFile &lockFile) const {
            if (params.directoryperdb) {
                severe() << "PerconaFT: directoryPerDB not yet supported.  This option is incompatible with PerconaFT.";
                severe() << "PerconaFT: The following server crash is intentional.";
                fassertFailedNoTrace(28928);
            }
            if (tokuftGlobalOptions.engineOptions.directoryForIndexes) {
                severe() << "PerconaFT: directoryForIndexes not yet supported.  This option is incompatible with PerconaFT.";
                severe() << "PerconaFT: The following server crash is intentional.";
                fassertFailedNoTrace(28929);
            }

            KVStorageEngineOptions options;
            options.directoryPerDB = params.directoryperdb;
            options.directoryForIndexes = tokuftGlobalOptions.engineOptions.directoryForIndexes;
            options.forRepair = params.repair;
            // TODO: Possibly push the durability flag down to the FT Engine...
            //return new TokuFTStorageEngine(params.dbpath, params.dur, options);
            return createTokuFTEngine(params.dbpath, options);
        }
        virtual StringData getCanonicalName() const {
            return "PerconaFT";
        }
        virtual Status validateCollectionStorageOptions(const BSONObj& options) const {
            return TokuFTDictionaryOptions::validateOptions(options);
        }
        virtual Status validateIndexStorageOptions(const BSONObj& options) const {
            return TokuFTDictionaryOptions::validateOptions(options);
        }
        virtual Status validateMetadata(const StorageEngineMetadata& metadata,
                                        const StorageGlobalParams& params) const {
            Status status = metadata.validateStorageEngineOption(
                "directoryPerDB", params.directoryperdb);
            if (!status.isOK()) {
                return status;
            }

            status = metadata.validateStorageEngineOption(
                "directoryForIndexes", tokuftGlobalOptions.engineOptions.directoryForIndexes);
            if (!status.isOK()) {
                return status;
            }

            return Status::OK();
        }
        virtual BSONObj createMetadataOptions(const StorageGlobalParams& params) const {
            BSONObjBuilder builder;
            builder.appendBool("directoryPerDB", params.directoryperdb);
            builder.appendBool("directoryForIndexes",
                               tokuftGlobalOptions.engineOptions.directoryForIndexes);
            return builder.obj();
        }
    };

    MONGO_INITIALIZER_WITH_PREREQUISITES(TokuFTStorageEngineInit,
                                         ("SetGlobalEnvironment"))
                                         (InitializerContext *context) {
        getGlobalServiceContext()->registerStorageEngine("PerconaFT", new TokuFTFactory());
        return Status::OK();
    }

} // namespace mongo
