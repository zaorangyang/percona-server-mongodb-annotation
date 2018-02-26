/**
 *    Copyright (C) 2018 MongoDB Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kCommand

#include "mongo/platform/basic.h"

#include "mongo/db/commands.h"
#include "mongo/s/commands/cluster_commands_helpers.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {

class DropIndexesCmd : public ErrmsgCommandDeprecated {
public:
    DropIndexesCmd() : ErrmsgCommandDeprecated("dropIndexes", "deleteIndexes") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }

    bool adminOnly() const override {
        return false;
    }

    void addRequiredPrivileges(const std::string& dbname,
                               const BSONObj& cmdObj,
                               std::vector<Privilege>* out) const override {
        ActionSet actions;
        actions.addAction(ActionType::dropIndex);
        out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }

    bool errmsgRun(OperationContext* opCtx,
                   const std::string& dbName,
                   const BSONObj& cmdObj,
                   std::string& errmsg,
                   BSONObjBuilder& output) override {
        const NamespaceString nss(CommandHelpers::parseNsCollectionRequired(dbName, cmdObj));
        LOG(1) << "dropIndexes: " << nss << " cmd:" << redact(cmdObj);

        // If the collection is sharded, we target all shards rather than just shards that own
        // chunks for the collection, because some shard may have previously owned chunks but no
        // longer does (and so, may have the index). However, we ignore NamespaceNotFound errors
        // from individual shards, because some shards may have never owned chunks for the
        // collection. We additionally ignore IndexNotFound errors, because the index may not have
        // been built on a shard if the earlier createIndexes command coincided with the shard
        // receiving its first chunk for the collection (see SERVER-31715).
        auto shardResponses = scatterGatherOnlyVersionIfUnsharded(
            opCtx,
            dbName,
            nss,
            CommandHelpers::filterCommandRequestForPassthrough(cmdObj),
            ReadPreferenceSetting::get(opCtx),
            Shard::RetryPolicy::kNotIdempotent);
        return appendRawResponses(opCtx,
                                  &errmsg,
                                  &output,
                                  std::move(shardResponses),
                                  {ErrorCodes::NamespaceNotFound, ErrorCodes::IndexNotFound});
    }

} dropIndexesCmd;

}  // namespace
}  // namespace mongo
