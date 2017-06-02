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

#include "mongo/platform/basic.h"

#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/commands.h"
#include "mongo/db/commands/feature_compatibility_version.h"
#include "mongo/db/commands/feature_compatibility_version_command_parser.h"
#include "mongo/s/catalog/sharding_catalog_manager.h"
#include "mongo/s/grid.h"

namespace mongo {

namespace {

/**
 * Internal sharding command run on config servers to set featureCompatibilityVersion on all shards.
 *
 * Format:
 * {
 *   _configsvrSetFeatureCompatibilityVersion: <string version>
 * }
 */
class ConfigSvrSetFeatureCompatibilityVersionCommand : public Command {
public:
    ConfigSvrSetFeatureCompatibilityVersionCommand()
        : Command("_configsvrSetFeatureCompatibilityVersion") {}

    void help(std::stringstream& help) const override {
        help << "Internal command, which is exported by the sharding config server. Do not call "
                "directly. Sets featureCompatibilityVersion on all shards. See "
                "http://dochub.mongodb.org/core/3.6-feature-compatibility.";
    }

    bool slaveOk() const override {
        return false;
    }

    bool adminOnly() const override {
        return true;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    Status checkAuthForCommand(Client* client,
                               const std::string& dbname,
                               const BSONObj& cmdObj) override {
        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forClusterResource(), ActionType::internal)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }
        return Status::OK();
    }

    bool run(OperationContext* opCtx,
             const std::string& unusedDbName,
             const BSONObj& cmdObj,
             std::string& errmsg,
             BSONObjBuilder& result) override {
        const auto version = uassertStatusOK(
            FeatureCompatibilityVersionCommandParser::extractVersionFromCommand(getName(), cmdObj));

        uassert(ErrorCodes::IllegalOperation,
                str::stream() << getName()
                              << " can only be run on config servers. See "
                                 "http://dochub.mongodb.org/core/3.6-feature-compatibility.",
                serverGlobalParams.clusterRole == ClusterRole::ConfigServer);

        // Forward to all shards.
        uassertStatusOK(Grid::get(opCtx)->catalogManager()->setFeatureCompatibilityVersionOnShards(
            opCtx, version));

        // On success, set featureCompatibilityVersion on self.
        FeatureCompatibilityVersion::set(opCtx, version);

        return true;
    }

} configsvrSetFeatureCompatibilityVersionCmd;

}  // namespace
}  // namespace mongo
