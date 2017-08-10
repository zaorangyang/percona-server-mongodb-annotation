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

#include <string>
#include <vector>

#include "mongo/base/disallow_copying.h"
#include "mongo/stdx/memory.h"

namespace mongo {

class BSONObj;
class ChunkRange;
class ConnectionString;
class NamespaceString;
class OperationContext;
class ShardId;
class ShardType;
class ChunkType;
class ShardKeyPattern;
class Status;
template <typename T>
class StatusWith;

namespace executor {
struct ConnectionPoolStats;
}

/**
 * Abstracts writes of the sharding catalog metadata.
 *
 * All implementations of this interface should go directly to the persistent backing store
 * and should avoid doing any caching of their own. The caching is delegated to a parallel
 * read-only view of the catalog, which is maintained by a higher level code.
 *
 * TODO: Currently the code responsible for writing the sharding catalog metadata is split between
 * this class and ShardingCatalogClient.  Eventually all methods that write catalog data should be
 * moved out of ShardingCatalogClient and into ShardingCatalogManager, here.
 */
class ShardingCatalogManager {
    MONGO_DISALLOW_COPYING(ShardingCatalogManager);

public:
    static Seconds getAddShardTaskRetryInterval() {
        return Seconds{30};
    }

    virtual ~ShardingCatalogManager() = default;

    /**
     * Performs implementation-specific startup tasks. Must be run after the catalog manager
     * has been installed into the global 'grid' object. Implementations do not need to guarantee
     * thread safety so callers should employ proper synchronization when calling this method.
     */
    virtual Status startup() = 0;

    /**
     * Performs necessary cleanup when shutting down cleanly.
     */
    virtual void shutDown(OperationContext* opCtx) = 0;

    //
    // Shard Operations
    //

    /**
     *
     * Adds a new shard. It expects a standalone mongod process or replica set to be running
     * on the provided address.
     *
     * @param  shardProposedName is an optional string with the proposed name of the shard.
     *         If it is nullptr, a name will be automatically generated; if not nullptr, it cannot
     *         contain the empty string.
     * @param  shardConnectionString is the connection string of the shard being added.
     * @param  maxSize is the optional space quota in bytes. Zeros means there's
     *         no limitation to space usage.
     * @return either an !OK status or the name of the newly added shard.
     */
    virtual StatusWith<std::string> addShard(OperationContext* opCtx,
                                             const std::string* shardProposedName,
                                             const ConnectionString& shardConnectionString,
                                             const long long maxSize) = 0;
    /**
     * Returns a BSON representation of an update request that can be used to insert a
     * shardIdentity doc into the shard for the given shardType (or update the shard's existing
     * shardIdentity doc's configsvrConnString if the _id, shardName, and clusterId do not
     * conflict).
     */
    virtual BSONObj createShardIdentityUpsertForAddShard(OperationContext* opCtx,
                                                         const std::string& shardName) = 0;

    //
    // Zone Operations
    //

    /**
     * Adds the shard to the zone.
     * Returns ErrorCodes::ShardNotFound if the shard does not exist.
     */
    virtual Status addShardToZone(OperationContext* opCtx,
                                  const std::string& shardName,
                                  const std::string& zoneName) = 0;

    /**
     * Removes the shard from the zone.
     * Returns ErrorCodes::ShardNotFound if the shard does not exist.
     */
    virtual Status removeShardFromZone(OperationContext* opCtx,
                                       const std::string& shardName,
                                       const std::string& zoneName) = 0;

    /**
     * Assigns a range of a sharded collection to a particular shard zone. If range is a prefix of
     * the shard key, the range will be converted into a new range with full shard key filled
     * with MinKey values.
     */
    virtual Status assignKeyRangeToZone(OperationContext* opCtx,
                                        const NamespaceString& ns,
                                        const ChunkRange& range,
                                        const std::string& zoneName) = 0;

    /**
     * Removes a range from a zone.
     * Note: unlike assignKeyRangeToZone, the given range will never be converted to include the
     * full shard key.
     */
    virtual Status removeKeyRangeFromZone(OperationContext* opCtx,
                                          const NamespaceString& ns,
                                          const ChunkRange& range) = 0;

    //
    // Chunk Operations
    //

    /**
     * Updates metadata in config.chunks collection to show the given chunk as split
     * into smaller chunks at the specified split points.
     */
    virtual Status commitChunkSplit(OperationContext* opCtx,
                                    const NamespaceString& ns,
                                    const OID& requestEpoch,
                                    const ChunkRange& range,
                                    const std::vector<BSONObj>& splitPoints,
                                    const std::string& shardName) = 0;

    /**
     * Updates metadata in config.chunks collection so the chunks with given boundaries are seen
     * merged into a single larger chunk.
     */
    virtual Status commitChunkMerge(OperationContext* opCtx,
                                    const NamespaceString& ns,
                                    const OID& requestEpoch,
                                    const std::vector<BSONObj>& chunkBoundaries,
                                    const std::string& shardName) = 0;

    /**
     * Updates metadata in config.chunks collection to show the given chunk in its new shard.
     */
    virtual StatusWith<BSONObj> commitChunkMigration(OperationContext* opCtx,
                                                     const NamespaceString& nss,
                                                     const ChunkType& migratedChunk,
                                                     const boost::optional<ChunkType>& controlChunk,
                                                     const OID& collectionEpoch,
                                                     const ShardId& fromShard,
                                                     const ShardId& toShard) = 0;

    //
    // Collection Operations
    //

    /**
     * Shards a collection. Assumes that the database is enabled for sharding.
     *
     * @param ns: namespace of collection to shard
     * @param fieldsAndOrder: shardKey pattern
     * @param defaultCollation: the default collation for the collection, to be written to
     *     config.collections. If empty, the collection default collation is simple binary
     *     comparison. Note the the shard key collation will always be simple binary comparison,
     *     even if the collection default collation is non-simple.
     * @param unique: if true, ensure underlying index enforces a unique constraint.
     * @param initPoints: create chunks based on a set of specified split points.
     * @param initShardIds: If non-empty, specifies the set of shards to assign chunks between.
     *     Otherwise all chunks will be assigned to the primary shard for the database.
     */
    virtual void shardCollection(OperationContext* opCtx,
                                 const std::string& ns,
                                 const ShardKeyPattern& fieldsAndOrder,
                                 const BSONObj& defaultCollation,
                                 bool unique,
                                 const std::vector<BSONObj>& initPoints,
                                 const bool distributeInitialChunks) = 0;

    //
    // Cluster Identity Operations
    //

    /**
     * Initializes the collections that live in the config server.  Mostly this involves building
     * necessary indexes and populating the config.version document.
     */
    virtual Status initializeConfigDatabaseIfNeeded(OperationContext* opCtx) = 0;

    /**
     * Called if the config.version document is rolled back.  Indicates to the
     * ShardingCatalogManager that on the next transition to primary
     * initializeConfigDatabaseIfNeeded will need to re-run the work to initialize the config
     * database.
     */
    virtual void discardCachedConfigDatabaseInitializationState() = 0;

    //
    // Cluster Upgrade Operations
    //

    /**
     * Runs the setFeatureCompatibilityVersion command on all shards.
     */
    virtual Status setFeatureCompatibilityVersionOnShards(OperationContext* opCtx,
                                                          const std::string& version) = 0;

    //
    // For Diagnostics
    //

    /**
     * Append information about the connection pools owned by the CatalogManager.
     */
    virtual void appendConnectionStats(executor::ConnectionPoolStats* stats) = 0;

protected:
    ShardingCatalogManager() = default;
};

}  // namespace mongo
