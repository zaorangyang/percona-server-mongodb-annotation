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

#include <boost/scoped_ptr.hpp>

#include "mongo/db/storage/kv/dictionary/kv_engine_impl.h"

#include <ftcxx/db_env.hpp>

namespace mongo {

    class TokuFTDictionaryOptions;

    class TokuFTEngine : public KVEngineImpl {
        MONGO_DISALLOW_COPYING(TokuFTEngine);
    public:
        // Opens or creates a storage engine environment at the given path
        TokuFTEngine(const std::string &path);
        virtual ~TokuFTEngine();

        virtual RecoveryUnit* newRecoveryUnit();

        virtual Status createKVDictionary(OperationContext* opCtx,
                                          StringData ident,
                                          const KVDictionary::Encoding &enc,
                                          const BSONObj& options);

        virtual KVDictionary* getKVDictionary(OperationContext* opCtx,
                                              StringData ident,
                                              const KVDictionary::Encoding &enc,
                                              const BSONObj& options,
                                              bool mayCreate = false);

        virtual Status dropKVDictionary(OperationContext* opCtx,
                                        StringData ident);

        virtual int64_t getIdentSize(OperationContext* opCtx,
                                     StringData ident) {
            return 1;
        }

        virtual Status repairIdent(OperationContext* opCtx,
                                   StringData ident) {
            return Status::OK();
        }

        virtual int flushAllFiles( bool sync );

        virtual bool isDurable() const { return true; }

        /**
         * TokuFT supports row-level ("document-level") locking.
         */
        virtual bool supportsDocLocking() const { return true; }

        virtual bool supportsDirectoryPerDB() const { return false; }

        // ------------------------------------------------------------------ //

        bool persistDictionaryStats() const { return true; }

        KVDictionary* getMetadataDictionary() {
            return _metadataDict.get();
        }

        void cleanShutdownImpl();

        bool hasIdent(OperationContext* opCtx, StringData ident) const;

        std::vector<std::string> getAllIdents(OperationContext *opCtx) const;

        ftcxx::DBEnv& env() { return _env; }

        KVDictionary* internalMetadataDict() const {
            return _internalMetadataDict.get();
        }

    private:
        static TokuFTDictionaryOptions _createOptions(const BSONObj& options, bool isRecordStore);

        void _checkAndUpgradeDiskFormatVersion();

        ftcxx::DBEnv _env;
        boost::scoped_ptr<KVDictionary> _metadataDict;
        boost::scoped_ptr<KVDictionary> _internalMetadataDict;
    };

} // namespace mongo
