// kv_size_storer.h

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
#include <boost/thread/thread.hpp>

#include "mongo/base/string_data.h"
#include "mongo/db/storage/record_store.h"
#include "mongo/util/string_map.h"

namespace mongo {

    class KVDictionary;
    class KVRecordStore;
    class RecoveryUnit;

    class KVSizeStorer {
    public:
        KVSizeStorer(KVDictionary *metadataDict, RecoveryUnit *ru);
        ~KVSizeStorer();

        void onCreate(RecordStore *rs, const StringData &ident,
                      long long nr, long long ds);
        void onDestroy(const StringData &ident,
                       long long nr, long long ds);

        void store(RecordStore *rs, const StringData& ident,
                   long long numRecords, long long dataSize);

        void load(const StringData& ident,
                  long long* numRecords, long long* dataSize) const;

        void loadFromDict(OperationContext *opCtx);
        void storeIntoDict(OperationContext *opCtx);

    private:
        void _checkMagic() const;

        struct Entry {
            Entry() : numRecords(0), dataSize(0), dirty(false), rs(NULL) {}
            long long numRecords;
            long long dataSize;
            bool dirty;
            RecordStore *rs;

            BSONObj serialize() const;
            Entry(const BSONObj &serialized);
        };

        void syncThread(RecoveryUnit *ru);

        int _magic;

        KVDictionary *_metadataDict;

        typedef std::map<std::string,Entry> Map;
        Map _entries;
        mutable boost::mutex _entriesMutex;

        volatile bool _syncRunning;
        volatile bool _syncTerminated;
        boost::mutex _syncMutex;
        boost::condition_variable _syncCond;
        boost::thread _syncThread;
    };

}
