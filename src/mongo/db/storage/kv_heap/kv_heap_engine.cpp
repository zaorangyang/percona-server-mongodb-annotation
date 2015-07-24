// kv_heap_engine.cpp

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

#include "mongo/db/storage/kv_heap/kv_heap_dictionary.h"
#include "mongo/db/storage/kv_heap/kv_heap_engine.h"
#include "mongo/db/storage/kv_heap/kv_heap_recovery_unit.h"

namespace mongo {

    RecoveryUnit* KVHeapEngine::newRecoveryUnit() {
        return new KVHeapRecoveryUnit();
    }

    Status KVHeapEngine::createKVDictionary(OperationContext* opCtx,
                                            const StringData& ident,
                                            const KVDictionary::Encoding &enc,
                                            const BSONObj& options) {
        return Status::OK();
    }

    KVDictionary* KVHeapEngine::getKVDictionary(OperationContext* opCtx,
                                                const StringData& ident,
                                                const KVDictionary::Encoding &enc,
                                                const BSONObj& options,
                                                bool mayCreate) {
        boost::mutex::scoped_lock lk(_mapMutex);
        HeapsMap::const_iterator it = _map.find(ident);
        if (it != _map.end()) {
            return it->second;
        }
        std::auto_ptr<KVDictionary> ptr(new KVHeapDictionary(enc));
        _map[ident] = ptr.get();
        return ptr.release();
    }

    Status KVHeapEngine::dropKVDictionary( OperationContext* opCtx,
                                            const StringData& ident ) {
        boost::mutex::scoped_lock lk(_mapMutex);
        _map.erase(ident);
        return Status::OK();
    }

    bool KVHeapEngine::hasIdent(OperationContext* opCtx, const StringData& ident) const {
        boost::mutex::scoped_lock lk(_mapMutex);
        return _map.find(ident) != _map.end();
    }

    std::vector<std::string> KVHeapEngine::getAllIdents( OperationContext* opCtx ) const {
        std::vector<std::string> idents;
        boost::mutex::scoped_lock lk(_mapMutex);
        for (HeapsMap::const_iterator it = _map.begin(); it != _map.end(); ++it) {
            idents.push_back(it->first);
        }
        return idents;
    }

}
