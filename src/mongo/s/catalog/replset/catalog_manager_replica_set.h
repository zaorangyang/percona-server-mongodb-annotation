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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/client/connection_string.h"
#include "mongo/db/repl/optime.h"
#include "mongo/s/catalog/catalog_manager.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/stdx/mutex.h"

namespace mongo {

class NamespaceString;
struct ReadPreferenceSetting;
class VersionType;

/**
 * Implements the catalog manager for talking to replica set config servers.
 */
class CatalogManagerReplicaSet final : public CatalogManager {
public:
    CatalogManagerReplicaSet(std::unique_ptr<DistLockManager> distLockManager,
                             std::unique_ptr<executor::TaskExecutor> addShardExecutor);
    virtual ~CatalogManagerReplicaSet();

    /**
     * Safe to call multiple times as long as the calls are externally synchronized to be
     * non-overlapping.
     */
    Status startup(OperationContext* txn) override;

    void shutDown(OperationContext* txn) override;

    Status enableSharding(OperationContext* txn, const std::string& dbName) override;

    StatusWith<std::string> addShard(OperationContext* txn,
                                     const std::string* shardProposedName,
                                     const ConnectionString& shardConnectionString,
                                     const long long maxSize) override;

    Status updateDatabase(OperationContext* txn,
                          const std::string& dbName,
                          const DatabaseType& db) override;

    Status updateCollection(OperationContext* txn,
                            const std::string& collNs,
                            const CollectionType& coll) override;

    Status createDatabase(OperationContext* txn, const std::string& dbName) override;

    Status logAction(OperationContext* txn,
                     const std::string& what,
                     const std::string& ns,
                     const BSONObj& detail) override;

    Status logChange(OperationContext* txn,
                     const std::string& what,
                     const std::string& ns,
                     const BSONObj& detail) override;

    StatusWith<DistLockManager::ScopedDistLock> distLock(
        OperationContext* txn,
        StringData name,
        StringData whyMessage,
        stdx::chrono::milliseconds waitFor) override;

    Status shardCollection(OperationContext* txn,
                           const std::string& ns,
                           const ShardKeyPattern& fieldsAndOrder,
                           bool unique,
                           const std::vector<BSONObj>& initPoints,
                           const std::set<ShardId>& initShardsIds) override;

    StatusWith<ShardDrainingStatus> removeShard(OperationContext* txn,
                                                const std::string& name) override;

    StatusWith<repl::OpTimeWith<DatabaseType>> getDatabase(OperationContext* txn,
                                                           const std::string& dbName) override;

    StatusWith<repl::OpTimeWith<CollectionType>> getCollection(OperationContext* txn,
                                                               const std::string& collNs) override;

    Status getCollections(OperationContext* txn,
                          const std::string* dbName,
                          std::vector<CollectionType>* collections,
                          repl::OpTime* optime) override;

    Status dropCollection(OperationContext* txn, const NamespaceString& ns) override;

    Status getDatabasesForShard(OperationContext* txn,
                                const std::string& shardName,
                                std::vector<std::string>* dbs) override;

    Status getChunks(OperationContext* txn,
                     const BSONObj& query,
                     const BSONObj& sort,
                     boost::optional<int> limit,
                     std::vector<ChunkType>* chunks,
                     repl::OpTime* opTime) override;

    Status getTagsForCollection(OperationContext* txn,
                                const std::string& collectionNs,
                                std::vector<TagsType>* tags) override;

    StatusWith<std::string> getTagForChunk(OperationContext* txn,
                                           const std::string& collectionNs,
                                           const ChunkType& chunk) override;

    StatusWith<repl::OpTimeWith<std::vector<ShardType>>> getAllShards(
        OperationContext* txn) override;

    bool runUserManagementWriteCommand(OperationContext* txn,
                                       const std::string& commandName,
                                       const std::string& dbname,
                                       const BSONObj& cmdObj,
                                       BSONObjBuilder* result) override;

    bool runUserManagementReadCommand(OperationContext* txn,
                                      const std::string& dbname,
                                      const BSONObj& cmdObj,
                                      BSONObjBuilder* result) override;

    Status applyChunkOpsDeprecated(OperationContext* txn,
                                   const BSONArray& updateOps,
                                   const BSONArray& preCondition,
                                   const std::string& nss,
                                   const ChunkVersion& lastChunkVersion) override;

    StatusWith<SettingsType> getGlobalSettings(OperationContext* txn,
                                               const std::string& key) override;

    void writeConfigServerDirect(OperationContext* txn,
                                 const BatchedCommandRequest& request,
                                 BatchedCommandResponse* response) override;

    Status insertConfigDocument(OperationContext* txn,
                                const std::string& ns,
                                const BSONObj& doc) override;

    StatusWith<bool> updateConfigDocument(OperationContext* txn,
                                          const std::string& ns,
                                          const BSONObj& query,
                                          const BSONObj& update,
                                          bool upsert) override;

    Status removeConfigDocuments(OperationContext* txn,
                                 const std::string& ns,
                                 const BSONObj& query) override;

    DistLockManager* getDistLockManager() override;

    Status initConfigVersion(OperationContext* txn) override;

    Status appendInfoForConfigServerDatabases(OperationContext* txn,
                                              BSONArrayBuilder* builder) override;

    void appendConnectionStats(executor::ConnectionPoolStats* stats) override;

    /**
     * Runs a read command against the config server with majority read concern.
     */
    bool runReadCommandForTest(OperationContext* txn,
                               const std::string& dbname,
                               const BSONObj& cmdObj,
                               BSONObjBuilder* result);

private:
    /**
     * Selects an optimal shard on which to place a newly created database from the set of
     * available shards. Will return ShardNotFound if shard could not be found.
     */
    static StatusWith<ShardId> _selectShardForNewDatabase(OperationContext* txn,
                                                          ShardRegistry* shardRegistry);

    /**
     * Checks that the given database name doesn't already exist in the config.databases
     * collection, including under different casing. Optional db can be passed and will
     * be set with the database details if the given dbName exists.
     *
     * Returns OK status if the db does not exist.
     * Some known errors include:
     *  NamespaceExists if it exists with the same casing
     *  DatabaseDifferCase if it exists under different casing.
     */
    Status _checkDbDoesNotExist(OperationContext* txn, const std::string& dbName, DatabaseType* db);

    /**
     * Generates a unique name to be given to a newly added shard.
     */
    StatusWith<std::string> _generateNewShardName(OperationContext* txn);

    /**
     * Validates that the specified connection string can serve as a shard server. In particular,
     * this function checks that the shard can be contacted, that it is not already member of
     * another sharded cluster and etc.
     *
     * @param shardRegistry Shard registry to use for getting a targeter to the shard-to-be.
     * @param connectionString Connection string to be attempted as a shard host.
     * @param shardProposedName Optional proposed name for the shard. Can be omitted in which case
     *      a unique name for the shard will be generated from the shard's connection string. If it
     *      is not omitted, the value cannot be the empty string.
     *
     * On success returns a partially initialized ShardType object corresponding to the requested
     * shard. It will have the hostName field set and optionally the name, if the name could be
     * generated from either the proposed name or the connection string set name. The returned
     * shard's name should be checked and if empty, one should be generated using some uniform
     * algorithm.
     */
    StatusWith<ShardType> _validateHostAsShard(OperationContext* txn,
                                               ShardRegistry* shardRegistry,
                                               const ConnectionString& connectionString,
                                               const std::string* shardProposedName);

    /**
     * Runs the listDatabases command on the specified host and returns the names of all databases
     * it returns excluding those named local and admin, since they serve administrative purpose.
     */
    StatusWith<std::vector<std::string>> _getDBNamesListFromShard(
        OperationContext* txn,
        ShardRegistry* shardRegistry,
        const ConnectionString& connectionString);

    /**
     * Creates the specified collection name in the config database.
     */
    Status _createCappedConfigCollection(OperationContext* txn,
                                         StringData collName,
                                         int cappedSize);

    /**
     * Executes the specified batch write command on the current config server's primary and retries
     * on the specified set of errors using the default retry policy.
     */
    void _runBatchWriteCommand(OperationContext* txn,
                               const BatchedCommandRequest& request,
                               BatchedCommandResponse* response,
                               const ShardRegistry::ErrorCodesSet& errorsToCheck);

    /**
     * Helper method for running a count command against the config server with appropriate
     * error handling.
     */
    StatusWith<long long> _runCountCommandOnConfig(OperationContext* txn,
                                                   const NamespaceString& ns,
                                                   BSONObj query);

    /**
     * Runs a command against a "shard" that is not yet in the cluster and thus not present in the
     * ShardRegistry.
     */
    StatusWith<BSONObj> _runCommandForAddShard(OperationContext* txn,
                                               RemoteCommandTargeter* targeter,
                                               const std::string& dbName,
                                               const BSONObj& cmdObj);

    StatusWith<repl::OpTimeWith<std::vector<BSONObj>>> _exhaustiveFindOnConfig(
        OperationContext* txn,
        const ReadPreferenceSetting& readPref,
        const NamespaceString& nss,
        const BSONObj& query,
        const BSONObj& sort,
        boost::optional<long long> limit);

    /**
     * Appends a read committed read concern to the request object.
     */
    void _appendReadConcern(BSONObjBuilder* builder);

    /**
     * Returns the current cluster schema/protocol version.
     */
    StatusWith<VersionType> _getConfigVersion(OperationContext* txn);

    /**
     * Queries the config servers for the database metadata for the given database, using the
     * given read preference.  Returns NamespaceNotFound if no database metadata is found.
     */
    StatusWith<repl::OpTimeWith<DatabaseType>> _fetchDatabaseMetadata(
        OperationContext* txn, const std::string& dbName, const ReadPreferenceSetting& readPref);

    /**
     * Best effort method, which logs diagnostic events on the config server. If the config server
     * write fails for any reason a warning will be written to the local service log and the method
     * will return a failed status.
     *
     * @param txn Operation context in which the call is running
     * @param logCollName Which config collection to write to (excluding the database name)
     * @param what E.g. "split", "migrate" (not interpreted)
     * @param operationNS To which collection the metadata change is being applied (not interpreted)
     * @param detail Additional info about the metadata change (not interpreted)
     */
    Status _log(OperationContext* txn,
                const StringData& logCollName,
                const std::string& what,
                const std::string& operationNS,
                const BSONObj& detail);

    //
    // All member variables are labeled with one of the following codes indicating the
    // synchronization rules for accessing them.
    //
    // (M) Must hold _mutex for access.
    // (R) Read only, can only be written during initialization.
    // (S) Self-synchronizing; access in any way from any context.
    //

    stdx::mutex _mutex;

    // Distributed lock manager singleton.
    std::unique_ptr<DistLockManager> _distLockManager;  // (R)

    // Executor specifically used for sending commands to servers that are in the process of being
    // added as shards.  Does not have any connection hook set on it, thus it can be used to talk
    // to servers that are not yet in the ShardRegistry.
    std::unique_ptr<executor::TaskExecutor> _executorForAddShard;  // (R)

    // True if shutDown() has been called. False, otherwise.
    bool _inShutdown = false;  // (M)

    // True if startup() has been called.
    bool _started = false;  // (M)

    // Last known highest opTime from the config server.
    repl::OpTime _configOpTime;  // (M)

    // Whether the logAction call should attempt to create the actionlog collection
    AtomicInt32 _actionLogCollectionCreated{0};  // (S)

    // Whether the logChange call should attempt to create the changelog collection
    AtomicInt32 _changeLogCollectionCreated{0};  // (S)
};

}  // namespace mongo
