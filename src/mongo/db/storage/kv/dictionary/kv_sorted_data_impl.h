// kv_sorted_data_impl.h

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

#include "mongo/bson/ordering.h"
#include "mongo/db/storage/kv/slice.h"
#include "mongo/db/storage/sorted_data_interface.h"

namespace mongo {

    class KVDictionary;
    class IndexDescriptor;
    class OperationContext;
    class KVSortedDataImpl;

    /**
     * Generic implementation of the SortedDataInterface using a KVDictionary.
     */
    class KVSortedDataImpl : public SortedDataInterface {
        MONGO_DISALLOW_COPYING( KVSortedDataImpl );
    public:
        KVSortedDataImpl( KVDictionary* db, OperationContext* opCtx, const IndexDescriptor *desc );

        virtual SortedDataBuilderInterface* getBulkBuilder(OperationContext* txn, bool dupsAllowed);

        virtual Status insert(OperationContext* txn,
                              const BSONObj& key,
                              const RecordId& loc,
                              bool dupsAllowed);

        virtual void unindex(OperationContext* txn, const BSONObj& key, const RecordId& loc, bool dupsAllowed);

        virtual Status dupKeyCheck(OperationContext* txn, const BSONObj& key, const RecordId& loc);

        virtual void fullValidate(OperationContext* txn, bool full, long long* numKeysOut,
                                  BSONObjBuilder* output) const;

        virtual bool isEmpty(OperationContext* txn);

        virtual long long numEntries(OperationContext* txn) const;

        virtual Cursor* newCursor(OperationContext* txn, int direction = 1) const;

        virtual std::unique_ptr<Cursor> newCursor(OperationContext* txn, bool isForward = true) const;

        virtual Status initAsEmpty(OperationContext* txn);

        virtual long long getSpaceUsedBytes( OperationContext* txn ) const;

        virtual bool appendCustomStats(OperationContext* txn, BSONObjBuilder* output, double scale) const;

        // Will be used for diagnostic printing by the TokuFT KVDictionary implementation.
        static BSONObj extractKey(const Slice &key, const Ordering &ordering, const KeyString::TypeBits &typeBits);
        static BSONObj extractKey(const Slice &key, const Slice &val, const Ordering &ordering);
        static RecordId extractRecordId(const Slice &s);

        void DuplicatesAreAllowed() { _dupsAllowed = true; }
        void DuplicatesAreNotAllowed() { _dupsAllowed = false; }
    private:
        // The KVDictionary interface used to store index keys, which map to empty values.
        boost::scoped_ptr<KVDictionary> _db;
        const Ordering _ordering;
        bool _dupsAllowed;
    };

    /**
     * Dummy implementation for now.  We'd need a KVDictionaryBuilder to
     * do better, let's worry about that later.
     */
    class KVSortedDataBuilderImpl : public SortedDataBuilderInterface {
        KVSortedDataImpl *_impl;
        OperationContext *_txn;
        bool _dupsAllowed;

    public:
        KVSortedDataBuilderImpl(KVSortedDataImpl *impl, OperationContext *txn, bool dupsAllowed)
            : _impl(impl),
              _txn(txn),
              _dupsAllowed(dupsAllowed)
        {
            if (_dupsAllowed) {
                _impl->DuplicatesAreAllowed();
            } else {
                _impl->DuplicatesAreNotAllowed();
            }
        }

        virtual Status addKey(const BSONObj& key, const RecordId& loc);
        virtual void commit(bool mayInterrupt) {
            // NOTE: As of v3.2, This arbitrary start/commit is
            // required to pass unit tests (and transitively support
            // the most recent storage engine API specifications).
            // Code paths that depend on this builder interface now
            // manage their own outer level transactions, irrespective
            // of this builder's state, so no transaction needs to be
            // maintained across this builder's lifetime.
            WriteUnitOfWork _wuow(_txn);
            _wuow.commit();
        }
    };
} // namespace mongo
