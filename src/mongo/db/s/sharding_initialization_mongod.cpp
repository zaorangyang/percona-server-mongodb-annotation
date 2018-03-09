/**
 * Copyright (C) 2016 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/db/s/sharding_initialization_mongod.h"

#include "mongo/platform/basic.h"

#include "mongo/base/status.h"
#include "mongo/client/connection_string.h"
#include "mongo/client/global_conn_pool.h"
#include "mongo/client/remote_command_targeter.h"
#include "mongo/client/remote_command_targeter_factory_impl.h"
#include "mongo/db/logical_time_metadata_hook.h"
#include "mongo/db/logical_time_validator.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/s/read_only_catalog_cache_loader.h"
#include "mongo/db/s/shard_server_catalog_cache_loader.h"
#include "mongo/db/s/sharding_egress_metadata_hook_for_mongod.h"
#include "mongo/db/server_options.h"
#include "mongo/executor/task_executor.h"
#include "mongo/rpc/metadata/egress_metadata_hook_list.h"
#include "mongo/s/catalog_cache.h"
#include "mongo/s/catalog_cache_loader.h"
#include "mongo/s/client/shard_connection.h"
#include "mongo/s/client/shard_factory.h"
#include "mongo/s/client/shard_local.h"
#include "mongo/s/client/shard_remote.h"
#include "mongo/s/client/sharding_connection_hook.h"
#include "mongo/s/config_server_catalog_cache_loader.h"
#include "mongo/s/sharding_initialization.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {

auto makeEgressHooksList(ServiceContext* service) {
    auto unshardedHookList = stdx::make_unique<rpc::EgressMetadataHookList>();
    unshardedHookList->addHook(stdx::make_unique<rpc::LogicalTimeMetadataHook>(service));
    unshardedHookList->addHook(
        stdx::make_unique<rpc::ShardingEgressMetadataHookForMongod>(service));

    return unshardedHookList;
};

}  // namespace

Status initializeGlobalShardingStateForMongod(OperationContext* opCtx,
                                              const ConnectionString& configCS,
                                              StringData distLockProcessId) {
    auto targeterFactory = stdx::make_unique<RemoteCommandTargeterFactoryImpl>();
    auto targeterFactoryPtr = targeterFactory.get();

    ShardFactory::BuilderCallable setBuilder =
        [targeterFactoryPtr](const ShardId& shardId, const ConnectionString& connStr) {
            return stdx::make_unique<ShardRemote>(
                shardId, connStr, targeterFactoryPtr->create(connStr));
        };

    ShardFactory::BuilderCallable masterBuilder =
        [targeterFactoryPtr](const ShardId& shardId, const ConnectionString& connStr) {
            return stdx::make_unique<ShardRemote>(
                shardId, connStr, targeterFactoryPtr->create(connStr));
        };

    ShardFactory::BuilderCallable localBuilder = [](const ShardId& shardId,
                                                    const ConnectionString& connStr) {
        return stdx::make_unique<ShardLocal>(shardId);
    };

    ShardFactory::BuildersMap buildersMap{
        {ConnectionString::SET, std::move(setBuilder)},
        {ConnectionString::MASTER, std::move(masterBuilder)},
        {ConnectionString::LOCAL, std::move(localBuilder)},
    };

    auto shardFactory =
        stdx::make_unique<ShardFactory>(std::move(buildersMap), std::move(targeterFactory));

    auto service = opCtx->getServiceContext();
    if (serverGlobalParams.clusterRole == ClusterRole::ShardServer) {
        if (storageGlobalParams.readOnly) {
            CatalogCacheLoader::set(service, stdx::make_unique<ReadOnlyCatalogCacheLoader>());
        } else {
            CatalogCacheLoader::set(service,
                                    stdx::make_unique<ShardServerCatalogCacheLoader>(
                                        stdx::make_unique<ConfigServerCatalogCacheLoader>()));
        }
    } else {
        CatalogCacheLoader::set(service, stdx::make_unique<ConfigServerCatalogCacheLoader>());
    }

    auto validator = LogicalTimeValidator::get(service);
    if (validator) {  // The keyManager may be existing if the node was a part of a standalone RS.
        validator->resetKeyManager();
    }

    globalConnPool.addHook(new ShardingConnectionHook(false, makeEgressHooksList(service)));
    shardConnectionPool.addHook(new ShardingConnectionHook(true, makeEgressHooksList(service)));

    return initializeGlobalShardingState(
        opCtx,
        configCS,
        distLockProcessId,
        std::move(shardFactory),
        stdx::make_unique<CatalogCache>(CatalogCacheLoader::get(opCtx)),
        [service] { return makeEgressHooksList(service); },
        // We only need one task executor here because sharding task executors aren't used for user
        // queries in mongod.
        1);
}

}  // namespace mongo
