// rocks_database_catalog_entry.h

/**
*    Copyright (C) 2014 MongoDB Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*    As a special exception, the copyright holders give permission to link the
*    code of portions of this program with the OpenSSL library under certain
*    conditions as described in each individual source file and distribute
*    linked combinations including the program with the OpenSSL library. You
*    must comply with the GNU Affero General Public License in all respects for
*    all of the code used other than as permitted herein. If you modify file(s)
*    with this exception, you may extend this exception to your version of the
*    file(s), but you are not obligated to do so. If you do not wish to do so,
*    delete this exception statement from your version. If you delete this
*    exception statement from all source files in the program, then also delete
*    it in the license file.
*/

#pragma once

#include "mongo/db/catalog/database_catalog_entry.h"

namespace mongo {

    class RocksEngine;

    /**
     * this class acts as a thin layer over RocksEngine,
     * and does and stores nothing.
     */
    class RocksDatabaseCatalogEntry : public DatabaseCatalogEntry {
    public:
        RocksDatabaseCatalogEntry( RocksEngine* engine, const StringData& dbname );

        virtual bool exists() const;
        virtual bool isEmpty() const;

        virtual void appendExtraStats( OperationContext* opCtx,
                                       BSONObjBuilder* out,
                                       double scale ) const;

        // these are hacks :(
        virtual bool isOlderThan24( OperationContext* opCtx ) const { return false; }
        virtual void markIndexSafe24AndUp( OperationContext* opCtx ) {}

        /**
         * @return true if current files on disk are compatibile with the current version.
         *              if we return false, then an upgrade will be required
         */
        virtual bool currentFilesCompatible( OperationContext* opCtx ) const;

        // ----

        virtual void getCollectionNamespaces( std::list<std::string>* out ) const;

        // The DatabaseCatalogEntry owns this, do not delete
        virtual CollectionCatalogEntry* getCollectionCatalogEntry( OperationContext* txn,
                                                                   const StringData& ns ) const;

        // The DatabaseCatalogEntry owns this, do not delete
        virtual RecordStore* getRecordStore( OperationContext* txn,
                                             const StringData& ns );

        // Ownership passes to caller
        virtual IndexAccessMethod* getIndex( OperationContext* txn,
                                             const CollectionCatalogEntry* collection,
                                             IndexCatalogEntry* index );

        virtual Status createCollection( OperationContext* txn,
                                         const StringData& ns,
                                         const CollectionOptions& options,
                                         bool allocateDefaultSpace );

        virtual Status renameCollection( OperationContext* txn,
                                         const StringData& fromNS,
                                         const StringData& toNS,
                                         bool stayTemp );

        virtual Status dropCollection( OperationContext* opCtx,
                                       const StringData& ns );

    private:
        RocksEngine* _engine;
    };
}
