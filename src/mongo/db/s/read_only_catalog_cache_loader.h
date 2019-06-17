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

#include "mongo/s/config_server_catalog_cache_loader.h"

namespace mongo {

/**
 * Contains a ConfigServerCatalogCacheLoader for remote metadata loading. Inactive functions simply
 * return, rather than invariant, so this class can be plugged into the shard server for read-only
 * mode, where persistence should not be attempted.
 */
class ReadOnlyCatalogCacheLoader final : public CatalogCacheLoader {
public:
    ~ReadOnlyCatalogCacheLoader();

    void initializeReplicaSetRole(bool isPrimary) override {}
    void onStepDown() override {}
    void onStepUp() override {}
    void shutDown() override;
    void notifyOfCollectionVersionUpdate(const NamespaceString& nss) override {}
    void waitForCollectionFlush(OperationContext* opCtx, const NamespaceString& nss) override;
    void waitForDatabaseFlush(OperationContext* opCtx, StringData dbName) override;

    std::shared_ptr<Notification<void>> getChunksSince(
        const NamespaceString& nss,
        ChunkVersion version,
        GetChunksSinceCallbackFn callbackFn) override;

    void getDatabase(
        StringData dbName,
        stdx::function<void(OperationContext*, StatusWith<DatabaseType>)> callbackFn) override;

private:
    ConfigServerCatalogCacheLoader _configServerLoader;
};

}  // namespace mongo
