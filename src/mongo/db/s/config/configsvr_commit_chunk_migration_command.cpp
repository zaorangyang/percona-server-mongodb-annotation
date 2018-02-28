/**
 *    Copyright (C) 2016 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/base/status_with.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/commands.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/read_concern_args.h"
#include "mongo/db/s/chunk_move_write_concern_options.h"
#include "mongo/db/s/config/sharding_catalog_manager.h"
#include "mongo/db/s/sharding_state.h"
#include "mongo/rpc/get_status_from_command_result.h"
#include "mongo/s/catalog/type_chunk.h"
#include "mongo/s/catalog/type_locks.h"
#include "mongo/s/chunk_version.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/s/grid.h"
#include "mongo/s/request_types/commit_chunk_migration_request_type.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {

/**
 * This command takes the chunk being migrated ("migratedChunk") and generates a new version for it
 * that is written along with its new shard location ("toShard") to the chunks collection. It also
 * takes a control chunk ("controlChunk") and assigns it a new version as well so that the source
 * ("fromShard") shard's shardVersion will increases. If there is no control chunk, then the chunk
 * being migrated is the source shard's only remaining chunk.
 *
 * The new chunk version is generated by querying the highest chunk version of the collection, and
 * then incrementing that major value for both migrated and control chunks and setting the minor to
 * 0 for the migrated chunk and 1 for the control chunk. A global exclusive lock is held for the
 * duration of generating the new chunk version and writing to the chunks collection so that
 * yielding cannot occur. This assures that generated ChunkVersions are strictly monotonically
 * increasing -- a second process will not be able to query for max chunk version until the first
 * finishes writing the new highest chunk version it generated.
 *
 * Command Format:
 * {
 *   _configsvrCommitChunkMigration: <database>.<collection>,
 *   fromShard: "<from_shard_name>",
 *   toShard: "<to_shard_name>",
 *   migratedChunk: {min: <min_value>, max: <max_value>, etc. },
 *   controlChunk: {min: <min_value>, max: <max_value>, etc. },  (optional)
 *   fromShardCollectionVersion: { shardVersionField: <version> }, (for backward compatibility only)
 * }
 *
 * Returns:
 * {
 *   migratedChunkVersion: <ChunkVersion_BSON>,
 *   controlChunkVersion: <ChunkVersion_BSON>, (only present if a controlChunk is defined)
 * }
 *
 */
class ConfigSvrCommitChunkMigrationCommand : public BasicCommand {
public:
    ConfigSvrCommitChunkMigrationCommand() : BasicCommand("_configsvrCommitChunkMigration") {}

    std::string help() const override {
        return "should not be calling this directly";
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }

    bool adminOnly() const override {
        return true;
    }

    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }

    Status checkAuthForCommand(Client* client,
                               const std::string& dbname,
                               const BSONObj& cmdObj) const override {
        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forClusterResource(), ActionType::internal)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }
        return Status::OK();
    }

    std::string parseNs(const std::string& dbname, const BSONObj& cmdObj) const override {
        return CommandHelpers::parseNsFullyQualified(dbname, cmdObj);
    }

    bool run(OperationContext* opCtx,
             const std::string& dbName,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) override {

        const NamespaceString nss = NamespaceString(parseNs(dbName, cmdObj));

        auto commitRequest =
            uassertStatusOK(CommitChunkMigrationRequest::createFromCommand(nss, cmdObj));

        StatusWith<BSONObj> response = ShardingCatalogManager::get(opCtx)->commitChunkMigration(
            opCtx,
            nss,
            commitRequest.getMigratedChunk(),
            commitRequest.getControlChunk(),
            commitRequest.getCollectionEpoch(),
            commitRequest.getFromShard(),
            commitRequest.getToShard(),
            commitRequest.getValidAfter());
        if (!response.isOK()) {
            return CommandHelpers::appendCommandStatus(result, response.getStatus());
        }
        result.appendElements(response.getValue());
        return true;
    }

} configsvrCommitChunkMigrationCommand;

}  // namespace
}  // namespace mongo
