// kv_heap_engine.h

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

#include <boost/thread/mutex.hpp>

#include "mongo/db/storage/kv/dictionary/kv_engine_impl.h"
#include "mongo/util/string_map.h"

namespace mongo {

    class KVHeapEngine : public KVEngineImpl {
        typedef StringMap<KVDictionary *> HeapsMap;
        mutable boost::mutex _mapMutex;
        HeapsMap _map;

    public:
        virtual ~KVHeapEngine() { }

        RecoveryUnit* newRecoveryUnit();

        Status createKVDictionary(OperationContext* opCtx,
                                  StringData ident,
                                  const KVDictionary::Encoding &enc,
                                  const BSONObj& options);

        KVDictionary* getKVDictionary(OperationContext* opCtx,
                                      StringData ident,
                                      const KVDictionary::Encoding &enc,
                                      const BSONObj& options,
                                      bool mayCreate = false);

        Status dropKVDictionary(OperationContext* opCtx,
                                StringData ident);

        int64_t getIdentSize(OperationContext* opCtx,
                             StringData ident) {
            return 1;
        }

        Status repairIdent(OperationContext* opCtx,
                           StringData ident) {
            return Status::OK();
        }

        int flushAllFiles( bool sync ) { return 0; }

        bool isDurable() const { return false; }

        // THe KVDictionaryHeap does not support fine-grained locking.
        bool supportsDocLocking() const { return false; }

        // why not
        bool supportsDirectoryPerDB() const { return true; }

        bool hasIdent(OperationContext* opCtx, StringData ident) const;

        std::vector<std::string> getAllIdents( OperationContext* opCtx ) const;

        void cleanShutdownImpl() {}

    };

}
