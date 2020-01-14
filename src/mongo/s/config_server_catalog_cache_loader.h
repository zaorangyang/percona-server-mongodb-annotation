/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/s/catalog_cache_loader.h"
#include "mongo/util/concurrency/thread_pool.h"

namespace mongo {

class ConfigServerCatalogCacheLoader final : public CatalogCacheLoader {
public:
    ConfigServerCatalogCacheLoader();
    ~ConfigServerCatalogCacheLoader();

    /**
     * These functions should never be called. They trigger invariants if called.
     */
    void initializeReplicaSetRole(bool isPrimary) override;
    void onStepDown() override;
    void onStepUp() override;
    void shutDown() override;
    void notifyOfCollectionVersionUpdate(const NamespaceString& nss) override;
    void waitForCollectionFlush(OperationContext* opCtx, const NamespaceString& nss) override;
    void waitForDatabaseFlush(OperationContext* opCtx, StringData dbName) override;

    std::shared_ptr<Notification<void>> getChunksSince(
        const NamespaceString& nss,
        ChunkVersion version,
        GetChunksSinceCallbackFn callbackFn) override;

    void getDatabase(
        StringData dbName,
        std::function<void(OperationContext*, StatusWith<DatabaseType>)> callbackFn) override;

private:
    // Thread pool to be used to perform metadata load
    ThreadPool _threadPool;

    // Protects the class state below
    Mutex _mutex = MONGO_MAKE_LATCH("ConfigServerCatalogCacheLoader::_mutex");

    // True if shutDown was called.
    bool _inShutdown{false};
};

}  // namespace mongo
