/**
 *    Copyright (C) 2017 MongoDB Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include <set>

#include "mongo/db/client.h"
#include "mongo/s/catalog/type_chunk.h"
#include "mongo/s/catalog/type_collection.h"
#include "mongo/s/catalog/type_shard.h"
#include "mongo/s/catalog_cache.h"
#include "mongo/s/chunk_manager_test_fixture.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace {

using ChunkManagerLoadTest = ChunkManagerTestFixture;

TEST_F(ChunkManagerLoadTest, IncrementalLoadAfterSplit) {
    const ShardKeyPattern shardKeyPattern(BSON("_id" << 1));

    auto initialRoutingInfo(makeChunkManager(shardKeyPattern, nullptr, true, {}));
    ASSERT_EQ(1, initialRoutingInfo->numChunks());

    auto future = launchAsync([&] {
        auto client = serviceContext()->makeClient("Test");
        auto opCtx = client->makeOperationContext();
        return CatalogCache::refreshCollectionRoutingInfo(opCtx.get(), kNss, initialRoutingInfo);
    });

    ChunkVersion version = initialRoutingInfo->getVersion();

    expectFindOnConfigSendBSONObjVector([&]() {
        CollectionType collType;
        collType.setNs(kNss);
        collType.setEpoch(version.epoch());
        collType.setKeyPattern(shardKeyPattern.toBSON());
        collType.setUnique(false);

        return std::vector<BSONObj>{collType.toBSON()};
    }());

    // Return set of chunks, which represent a split
    expectFindOnConfigSendBSONObjVector([&]() {
        version.incMajor();
        ChunkType chunk1(
            kNss, {shardKeyPattern.getKeyPattern().globalMin(), BSON("_id" << 0)}, version, {"0"});

        version.incMinor();
        ChunkType chunk2(
            kNss, {BSON("_id" << 0), shardKeyPattern.getKeyPattern().globalMax()}, version, {"0"});

        return std::vector<BSONObj>{chunk1.toBSON(), chunk2.toBSON()};
    }());

    auto newRoutingInfo(future.timed_get(kFutureTimeout));
    ASSERT_EQ(2, newRoutingInfo->numChunks());
    ASSERT_EQ(version, newRoutingInfo->getVersion());
    ASSERT_EQ(version, newRoutingInfo->getVersion({"0"}));
    ASSERT_EQ(ChunkVersion(0, 0, version.epoch()), newRoutingInfo->getVersion({"1"}));
}

TEST_F(ChunkManagerLoadTest, IncrementalLoadAfterMove) {
    const ShardKeyPattern shardKeyPattern(BSON("_id" << 1));

    auto initialRoutingInfo(makeChunkManager(shardKeyPattern, nullptr, true, {BSON("_id" << 0)}));
    ASSERT_EQ(2, initialRoutingInfo->numChunks());

    auto future = launchAsync([&] {
        auto client = serviceContext()->makeClient("Test");
        auto opCtx = client->makeOperationContext();
        return CatalogCache::refreshCollectionRoutingInfo(opCtx.get(), kNss, initialRoutingInfo);
    });

    ChunkVersion version = initialRoutingInfo->getVersion();

    expectFindOnConfigSendBSONObjVector([&]() {
        CollectionType collType;
        collType.setNs(kNss);
        collType.setEpoch(version.epoch());
        collType.setKeyPattern(shardKeyPattern.toBSON());
        collType.setUnique(false);

        return std::vector<BSONObj>{collType.toBSON()};
    }());

    ChunkVersion expectedDestShardVersion;

    // Return set of chunks, which represent a move
    expectFindOnConfigSendBSONObjVector([&]() {
        version.incMajor();
        expectedDestShardVersion = version;
        ChunkType chunk1(
            kNss, {shardKeyPattern.getKeyPattern().globalMin(), BSON("_id" << 0)}, version, {"1"});

        version.incMinor();
        ChunkType chunk2(
            kNss, {BSON("_id" << 0), shardKeyPattern.getKeyPattern().globalMax()}, version, {"0"});

        return std::vector<BSONObj>{chunk1.toBSON(), chunk2.toBSON()};
    }());

    auto newRoutingInfo(future.timed_get(kFutureTimeout));
    ASSERT_EQ(2, newRoutingInfo->numChunks());
    ASSERT_EQ(version, newRoutingInfo->getVersion());
    ASSERT_EQ(version, newRoutingInfo->getVersion({"0"}));
    ASSERT_EQ(expectedDestShardVersion, newRoutingInfo->getVersion({"1"}));
}

TEST_F(ChunkManagerLoadTest, IncrementalLoadAfterMoveLastChunk) {
    const ShardKeyPattern shardKeyPattern(BSON("_id" << 1));

    auto initialRoutingInfo(makeChunkManager(shardKeyPattern, nullptr, true, {}));
    ASSERT_EQ(1, initialRoutingInfo->numChunks());

    auto future = launchAsync([&] {
        auto client = serviceContext()->makeClient("Test");
        auto opCtx = client->makeOperationContext();
        return CatalogCache::refreshCollectionRoutingInfo(opCtx.get(), kNss, initialRoutingInfo);
    });

    ChunkVersion version = initialRoutingInfo->getVersion();

    expectFindOnConfigSendBSONObjVector([&]() {
        CollectionType collType;
        collType.setNs(kNss);
        collType.setEpoch(version.epoch());
        collType.setKeyPattern(shardKeyPattern.toBSON());
        collType.setUnique(false);

        return std::vector<BSONObj>{collType.toBSON()};
    }());

    // Return set of chunks, which represent a move
    expectFindOnConfigSendBSONObjVector([&]() {
        version.incMajor();
        ChunkType chunk1(kNss,
                         {shardKeyPattern.getKeyPattern().globalMin(),
                          shardKeyPattern.getKeyPattern().globalMax()},
                         version,
                         {"1"});

        return std::vector<BSONObj>{chunk1.toBSON()};
    }());

    expectFindOnConfigSendBSONObjVector([&]() {
        ShardType shard1;
        shard1.setName("0");
        shard1.setHost(str::stream() << "Host0:12345");

        ShardType shard2;
        shard2.setName("1");
        shard2.setHost(str::stream() << "Host1:12345");

        return std::vector<BSONObj>{shard1.toBSON(), shard2.toBSON()};
    }());

    auto newRoutingInfo(future.timed_get(kFutureTimeout));
    ASSERT_EQ(1, newRoutingInfo->numChunks());
    ASSERT_EQ(version, newRoutingInfo->getVersion());
    ASSERT_EQ(ChunkVersion(0, 0, version.epoch()), newRoutingInfo->getVersion({"0"}));
    ASSERT_EQ(version, newRoutingInfo->getVersion({"1"}));
}

}  // namespace
}  // namespace mongo
