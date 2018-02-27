/**
 *    Copyright (C) 2015 MongoDB Inc.
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

#include "mongo/db/auth/action_set.h"
#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/auth/privilege.h"
#include "mongo/db/catalog_raii.h"
#include "mongo/db/commands.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/s/collection_sharding_state.h"
#include "mongo/db/s/migration_chunk_cloner_source_legacy.h"
#include "mongo/db/s/migration_source_manager.h"
#include "mongo/db/s/sharding_state.h"
#include "mongo/db/write_concern.h"

/**
 * This file contains commands, which are specific to the legacy chunk cloner source.
 */
namespace mongo {
namespace {

/**
 * Shortcut class to perform the appropriate checks and acquire the cloner associated with the
 * currently active migration. Uses the currently registered migration for this shard and ensures
 * the session ids match.
 */
class AutoGetActiveCloner {
    MONGO_DISALLOW_COPYING(AutoGetActiveCloner);

public:
    AutoGetActiveCloner(OperationContext* opCtx, const MigrationSessionId& migrationSessionId) {
        ShardingState* const gss = ShardingState::get(opCtx);

        const auto nss = gss->getActiveDonateChunkNss();
        uassert(ErrorCodes::NotYetInitialized, "No active migrations were found", nss);

        // Once the collection is locked, the migration status cannot change
        _autoColl.emplace(opCtx, *nss, MODE_IS);

        uassert(ErrorCodes::NamespaceNotFound,
                str::stream() << "Collection " << nss->ns() << " does not exist",
                _autoColl->getCollection());

        auto css = CollectionShardingState::get(opCtx, *nss);
        uassert(ErrorCodes::IllegalOperation,
                str::stream() << "No active migrations were found for collection " << nss->ns(),
                css->getMigrationSourceManager());

        // It is now safe to access the cloner
        _chunkCloner = dynamic_cast<MigrationChunkClonerSourceLegacy*>(
            css->getMigrationSourceManager()->getCloner());
        invariant(_chunkCloner);

        // Ensure the session ids are correct
        uassert(ErrorCodes::IllegalOperation,
                str::stream() << "Requested migration session id " << migrationSessionId.toString()
                              << " does not match active session id "
                              << _chunkCloner->getSessionId().toString(),
                migrationSessionId.matches(_chunkCloner->getSessionId()));
    }

    Database* getDb() const {
        invariant(_autoColl);
        return _autoColl->getDb();
    }

    Collection* getColl() const {
        invariant(_autoColl);
        return _autoColl->getCollection();
    }

    MigrationChunkClonerSourceLegacy* getCloner() const {
        invariant(_chunkCloner);
        return _chunkCloner;
    }

private:
    // Scoped database + collection lock
    boost::optional<AutoGetCollection> _autoColl;

    // Contains the active cloner for the namespace
    MigrationChunkClonerSourceLegacy* _chunkCloner;
};

class InitialCloneCommand : public BasicCommand {
public:
    InitialCloneCommand() : BasicCommand("_migrateClone") {}

    std::string help() const override {
        return "internal";
    }

    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }

    virtual bool adminOnly() const {
        return true;
    }

    virtual void addRequiredPrivileges(const std::string& dbname,
                                       const BSONObj& cmdObj,
                                       std::vector<Privilege>* out) const {
        ActionSet actions;
        actions.addAction(ActionType::internal);
        out->push_back(Privilege(ResourcePattern::forClusterResource(), actions));
    }

    bool run(OperationContext* opCtx,
             const std::string&,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) {
        const MigrationSessionId migrationSessionId(
            uassertStatusOK(MigrationSessionId::extractFromBSON(cmdObj)));

        boost::optional<BSONArrayBuilder> arrBuilder;

        // Try to maximize on the size of the buffer, which we are returning in order to have less
        // round-trips
        int arrSizeAtPrevIteration = -1;

        while (!arrBuilder || arrBuilder->arrSize() > arrSizeAtPrevIteration) {
            AutoGetActiveCloner autoCloner(opCtx, migrationSessionId);

            if (!arrBuilder) {
                arrBuilder.emplace(autoCloner.getCloner()->getCloneBatchBufferAllocationSize());
            }

            arrSizeAtPrevIteration = arrBuilder->arrSize();

            uassertStatusOK(autoCloner.getCloner()->nextCloneBatch(
                opCtx, autoCloner.getColl(), arrBuilder.get_ptr()));
        }

        invariant(arrBuilder);
        result.appendArray("objects", arrBuilder->arr());

        return true;
    }

} initialCloneCommand;

class TransferModsCommand : public BasicCommand {
public:
    TransferModsCommand() : BasicCommand("_transferMods") {}

    std::string help() const override {
        return "internal";
    }

    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }

    virtual bool adminOnly() const {
        return true;
    }

    virtual void addRequiredPrivileges(const std::string& dbname,
                                       const BSONObj& cmdObj,
                                       std::vector<Privilege>* out) const {
        ActionSet actions;
        actions.addAction(ActionType::internal);
        out->push_back(Privilege(ResourcePattern::forClusterResource(), actions));
    }

    bool run(OperationContext* opCtx,
             const std::string&,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) {
        const MigrationSessionId migrationSessionId(
            uassertStatusOK(MigrationSessionId::extractFromBSON(cmdObj)));

        AutoGetActiveCloner autoCloner(opCtx, migrationSessionId);

        uassertStatusOK(autoCloner.getCloner()->nextModsBatch(opCtx, autoCloner.getDb(), &result));
        return true;
    }

} transferModsCommand;

/**
 * Command for extracting the oplog entries that needs to be migrated for the given migration
 * session id.
 * Note: this command is not stateless. Calling this command has a side-effect of gradually
 * depleting the buffer that contains the oplog entries to be transfered.
 */
class MigrateSessionCommand : public BasicCommand {
public:
    MigrateSessionCommand() : BasicCommand("_getNextSessionMods") {}

    std::string help() const override {
        return "internal";
    }

    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }

    virtual bool adminOnly() const {
        return true;
    }

    virtual void addRequiredPrivileges(const std::string& dbname,
                                       const BSONObj& cmdObj,
                                       std::vector<Privilege>* out) const {
        ActionSet actions;
        actions.addAction(ActionType::internal);
        out->push_back(Privilege(ResourcePattern::forClusterResource(), actions));
    }

    bool run(OperationContext* opCtx,
             const std::string&,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) {
        const MigrationSessionId migrationSessionId(
            uassertStatusOK(MigrationSessionId::extractFromBSON(cmdObj)));

        BSONArrayBuilder arrBuilder;

        repl::OpTime opTime;

        writeConflictRetry(opCtx,
                           "Fetching session related oplogs for migration",
                           NamespaceString::kRsOplogNamespace.ns(),
                           [&]() {
                               AutoGetActiveCloner autoCloner(opCtx, migrationSessionId);
                               opTime = autoCloner.getCloner()->nextSessionMigrationBatch(
                                   opCtx, &arrBuilder);
                           });

        WriteConcernResult wcResult;
        WriteConcernOptions majorityWC(
            WriteConcernOptions::kMajority, WriteConcernOptions::SyncMode::UNSET, 0);
        uassertStatusOK(waitForWriteConcern(opCtx, opTime, majorityWC, &wcResult));

        result.appendArray("oplog", arrBuilder.arr());
        return true;
    }

} migrateSessionCommand;

}  // namespace
}  // namespace mongo
