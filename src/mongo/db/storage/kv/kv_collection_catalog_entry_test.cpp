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

#include "mongo/platform/basic.h"

#include <boost/optional/optional_io.hpp>
#include <iostream>
#include <string>

#include "mongo/db/catalog/collection_catalog.h"
#include "mongo/db/catalog/collection_catalog_entry.h"
#include "mongo/db/catalog/collection_mock.h"
#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/index/multikey_paths.h"
#include "mongo/db/index_names.h"
#include "mongo/db/operation_context_noop.h"
#include "mongo/db/service_context_test_fixture.h"
#include "mongo/db/storage/devnull/devnull_kv_engine.h"
#include "mongo/db/storage/kv/kv_engine.h"
#include "mongo/db/storage/storage_engine_impl.h"
#include "mongo/unittest/death_test.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/str.h"

namespace mongo {
namespace {

static std::string kSideWritesTableIdent("sideWrites");
static std::string kConstraintViolationsTableIdent("constraintViolations");

// Update version as breaking changes are introduced into the index build procedure.
static const long kExpectedVersion = 1;

class KVCollectionCatalogEntryTest : public ServiceContextTest {
public:
    KVCollectionCatalogEntryTest()
        : _nss("unittests.kv_collection_catalog_entry"),
          _storageEngine(new DevNullKVEngine(), StorageEngineOptions()) {
        _storageEngine.finishInit();
    }

    ~KVCollectionCatalogEntryTest() {
        _storageEngine.cleanShutdown();
    }

    std::unique_ptr<OperationContext> newOperationContext() {
        auto opCtx = stdx::make_unique<OperationContextNoop>(&cc(), 0);
        opCtx->setRecoveryUnit(std::unique_ptr<RecoveryUnit>(_storageEngine.newRecoveryUnit()),
                               WriteUnitOfWork::RecoveryUnitState::kNotInUnitOfWork);
        return opCtx;
    }

    void setUp() final {
        auto opCtx = newOperationContext();

        {
            WriteUnitOfWork wuow(opCtx.get());
            const bool allocateDefaultSpace = true;
            CollectionOptions options;
            options.uuid = UUID::gen();
            auto statusWithCatalogEntry = _storageEngine.getCatalog()->createCollection(
                opCtx.get(), _nss, options, allocateDefaultSpace);
            ASSERT_OK(statusWithCatalogEntry.getStatus());
            auto collection = std::make_unique<CollectionMock>(_nss);
            CollectionCatalog::get(opCtx.get())
                .registerCollection(options.uuid.get(),
                                    std::move(statusWithCatalogEntry.getValue()),
                                    std::move(collection));
            wuow.commit();
        }
    }

    NamespaceString ns() {
        return _nss;
    }

    DurableCatalog* getCatalog() {
        return _storageEngine.getCatalog();
    }

    std::string createIndex(BSONObj keyPattern,
                            std::string indexType = IndexNames::BTREE,
                            IndexBuildProtocol protocol = IndexBuildProtocol::kSinglePhase) {
        auto opCtx = newOperationContext();
        std::string indexName = "idx" + std::to_string(numIndexesCreated);

        auto collection = std::make_unique<CollectionMock>(_nss);
        IndexDescriptor desc(
            collection.get(),
            indexType,
            BSON("v" << 1 << "key" << keyPattern << "name" << indexName << "ns" << _nss.ns()));

        {
            WriteUnitOfWork wuow(opCtx.get());
            const bool isSecondaryBackgroundIndexBuild = false;
            ASSERT_OK(_storageEngine.getCatalog()->prepareForIndexBuild(
                opCtx.get(), _nss, &desc, protocol, isSecondaryBackgroundIndexBuild));
            wuow.commit();
        }

        ++numIndexesCreated;
        return indexName;
    }

    void assertMultikeyPathsAreEqual(const MultikeyPaths& actual, const MultikeyPaths& expected) {
        bool match = (expected == actual);
        if (!match) {
            FAIL(str::stream() << "Expected: " << dumpMultikeyPaths(expected) << ", "
                               << "Actual: "
                               << dumpMultikeyPaths(actual));
        }
        ASSERT(match);
    }

private:
    std::string dumpMultikeyPaths(const MultikeyPaths& multikeyPaths) {
        std::stringstream ss;

        ss << "[ ";
        for (const auto multikeyComponents : multikeyPaths) {
            ss << "[ ";
            for (const auto multikeyComponent : multikeyComponents) {
                ss << multikeyComponent << " ";
            }
            ss << "] ";
        }
        ss << "]";

        return ss.str();
    }

    const NamespaceString _nss;
    StorageEngineImpl _storageEngine;
    size_t numIndexesCreated = 0;
};

TEST_F(KVCollectionCatalogEntryTest, MultikeyPathsForBtreeIndexInitializedToVectorOfEmptySets) {
    std::string indexName = createIndex(BSON("a" << 1 << "b" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    {
        MultikeyPaths multikeyPaths;
        ASSERT(!catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        assertMultikeyPathsAreEqual(multikeyPaths, {std::set<size_t>{}, std::set<size_t>{}});
    }
}

TEST_F(KVCollectionCatalogEntryTest, CanSetIndividualPathComponentOfBtreeIndexAsMultikey) {
    std::string indexName = createIndex(BSON("a" << 1 << "b" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    ASSERT(catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, {std::set<size_t>{}, {0U}}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        assertMultikeyPathsAreEqual(multikeyPaths, {std::set<size_t>{}, {0U}});
    }
}

TEST_F(KVCollectionCatalogEntryTest, MultikeyPathsAccumulateOnDifferentFields) {
    std::string indexName = createIndex(BSON("a" << 1 << "b" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    ASSERT(catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, {std::set<size_t>{}, {0U}}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        assertMultikeyPathsAreEqual(multikeyPaths, {std::set<size_t>{}, {0U}});
    }

    ASSERT(catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, {{0U}, std::set<size_t>{}}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        assertMultikeyPathsAreEqual(multikeyPaths, {{0U}, {0U}});
    }
}

TEST_F(KVCollectionCatalogEntryTest, MultikeyPathsAccumulateOnDifferentComponentsOfTheSameField) {
    std::string indexName = createIndex(BSON("a.b" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    ASSERT(catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, {{0U}}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        assertMultikeyPathsAreEqual(multikeyPaths, {{0U}});
    }

    ASSERT(catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, {{1U}}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        assertMultikeyPathsAreEqual(multikeyPaths, {{0U, 1U}});
    }
}

TEST_F(KVCollectionCatalogEntryTest, NoOpWhenSpecifiedPathComponentsAlreadySetAsMultikey) {
    std::string indexName = createIndex(BSON("a" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    ASSERT(catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, {{0U}}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        assertMultikeyPathsAreEqual(multikeyPaths, {{0U}});
    }

    ASSERT(!catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, {{0U}}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        assertMultikeyPathsAreEqual(multikeyPaths, {{0U}});
    }
}

TEST_F(KVCollectionCatalogEntryTest, CanSetMultipleFieldsAndComponentsAsMultikey) {
    std::string indexName = createIndex(BSON("a.b.c" << 1 << "a.b.d" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    ASSERT(catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, {{0U, 1U}, {0U, 1U}}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        assertMultikeyPathsAreEqual(multikeyPaths, {{0U, 1U}, {0U, 1U}});
    }
}

DEATH_TEST_F(KVCollectionCatalogEntryTest,
             CannotOmitPathLevelMultikeyInfoWithBtreeIndex,
             "Invariant failure !multikeyPaths.empty()") {
    std::string indexName = createIndex(BSON("a" << 1 << "b" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, MultikeyPaths{});
}

DEATH_TEST_F(KVCollectionCatalogEntryTest,
             AtLeastOnePathComponentMustCauseIndexToBeMultikey,
             "Invariant failure somePathIsMultikey") {
    std::string indexName = createIndex(BSON("a" << 1 << "b" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    catalog->setIndexIsMultikey(
        opCtx.get(), ns(), indexName, {std::set<size_t>{}, std::set<size_t>{}});
}

TEST_F(KVCollectionCatalogEntryTest, PathLevelMultikeyTrackingIsSupportedBy2dsphereIndexes) {
    std::string indexType = IndexNames::GEO_2DSPHERE;
    std::string indexName = createIndex(BSON("a" << indexType << "b" << 1), indexType);
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    {
        MultikeyPaths multikeyPaths;
        ASSERT(!catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        assertMultikeyPathsAreEqual(multikeyPaths, {std::set<size_t>{}, std::set<size_t>{}});
    }
}

TEST_F(KVCollectionCatalogEntryTest, PathLevelMultikeyTrackingIsNotSupportedByAllIndexTypes) {
    std::string indexTypes[] = {
        IndexNames::GEO_2D, IndexNames::GEO_HAYSTACK, IndexNames::TEXT, IndexNames::HASHED};

    for (auto&& indexType : indexTypes) {
        std::string indexName = createIndex(BSON("a" << indexType << "b" << 1), indexType);
        auto opCtx = newOperationContext();
        DurableCatalog* catalog = getCatalog();
        {
            MultikeyPaths multikeyPaths;
            ASSERT(!catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
            ASSERT(multikeyPaths.empty());
        }
    }
}

TEST_F(KVCollectionCatalogEntryTest, CanSetEntireTextIndexAsMultikey) {
    std::string indexType = IndexNames::TEXT;
    std::string indexName = createIndex(BSON("a" << indexType << "b" << 1), indexType);
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    ASSERT(catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, MultikeyPaths{}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        ASSERT(multikeyPaths.empty());
    }
}

TEST_F(KVCollectionCatalogEntryTest, NoOpWhenEntireIndexAlreadySetAsMultikey) {
    std::string indexType = IndexNames::TEXT;
    std::string indexName = createIndex(BSON("a" << indexType << "b" << 1), indexType);
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    ASSERT(catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, MultikeyPaths{}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        ASSERT(multikeyPaths.empty());
    }

    ASSERT(!catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, MultikeyPaths{}));

    {
        MultikeyPaths multikeyPaths;
        ASSERT(catalog->isIndexMultikey(opCtx.get(), ns(), indexName, &multikeyPaths));
        ASSERT(multikeyPaths.empty());
    }
}

TEST_F(KVCollectionCatalogEntryTest, SinglePhaseIndexBuild) {
    std::string indexName = createIndex(BSON("a" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();

    ASSERT_EQ(kExpectedVersion, catalog->getIndexBuildVersion(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexReady(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isTwoPhaseIndexBuild(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexBuildScanning(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexBuildDraining(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->getSideWritesIdent(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->getConstraintViolationsIdent(opCtx.get(), ns(), indexName));

    catalog->indexBuildSuccess(opCtx.get(), ns(), indexName);

    ASSERT_EQ(kExpectedVersion, catalog->getIndexBuildVersion(opCtx.get(), ns(), indexName));
    ASSERT_TRUE(catalog->isIndexReady(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isTwoPhaseIndexBuild(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexBuildScanning(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexBuildDraining(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->getSideWritesIdent(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->getConstraintViolationsIdent(opCtx.get(), ns(), indexName));
}

TEST_F(KVCollectionCatalogEntryTest, TwoPhaseIndexBuild) {
    std::string indexName =
        createIndex(BSON("a" << 1), IndexNames::BTREE, IndexBuildProtocol::kTwoPhase);
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();

    ASSERT_EQ(kExpectedVersion, catalog->getIndexBuildVersion(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexReady(opCtx.get(), ns(), indexName));
    ASSERT_TRUE(catalog->isTwoPhaseIndexBuild(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexBuildScanning(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexBuildDraining(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->getSideWritesIdent(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->getConstraintViolationsIdent(opCtx.get(), ns(), indexName));

    catalog->setIndexBuildScanning(
        opCtx.get(), ns(), indexName, kSideWritesTableIdent, kConstraintViolationsTableIdent);

    ASSERT_EQ(kExpectedVersion, catalog->getIndexBuildVersion(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexReady(opCtx.get(), ns(), indexName));
    ASSERT_TRUE(catalog->isTwoPhaseIndexBuild(opCtx.get(), ns(), indexName));
    ASSERT_TRUE(catalog->isIndexBuildScanning(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexBuildDraining(opCtx.get(), ns(), indexName));
    ASSERT_EQ(kSideWritesTableIdent, catalog->getSideWritesIdent(opCtx.get(), ns(), indexName));
    ASSERT_EQ(kConstraintViolationsTableIdent,
              catalog->getConstraintViolationsIdent(opCtx.get(), ns(), indexName));

    catalog->setIndexBuildDraining(opCtx.get(), ns(), indexName);

    ASSERT_EQ(kExpectedVersion, catalog->getIndexBuildVersion(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexReady(opCtx.get(), ns(), indexName));
    ASSERT_TRUE(catalog->isTwoPhaseIndexBuild(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexBuildScanning(opCtx.get(), ns(), indexName));
    ASSERT_TRUE(catalog->isIndexBuildDraining(opCtx.get(), ns(), indexName));
    ASSERT_EQ(kSideWritesTableIdent, catalog->getSideWritesIdent(opCtx.get(), ns(), indexName));
    ASSERT_EQ(kConstraintViolationsTableIdent,
              catalog->getConstraintViolationsIdent(opCtx.get(), ns(), indexName));

    catalog->indexBuildSuccess(opCtx.get(), ns(), indexName);

    ASSERT_EQ(kExpectedVersion, catalog->getIndexBuildVersion(opCtx.get(), ns(), indexName));
    ASSERT(catalog->isIndexReady(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexBuildScanning(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isIndexBuildDraining(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->isTwoPhaseIndexBuild(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->getSideWritesIdent(opCtx.get(), ns(), indexName));
    ASSERT_FALSE(catalog->getConstraintViolationsIdent(opCtx.get(), ns(), indexName));
}

DEATH_TEST_F(KVCollectionCatalogEntryTest,
             SinglePhaseIllegalScanPhase,
             "Invariant failure md.indexes[offset].runTwoPhaseBuild") {
    std::string indexName = createIndex(BSON("a" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();

    catalog->setIndexBuildScanning(
        opCtx.get(), ns(), indexName, kSideWritesTableIdent, kConstraintViolationsTableIdent);
}

DEATH_TEST_F(KVCollectionCatalogEntryTest,
             SinglePhaseIllegalDrainPhase,
             "Invariant failure md.indexes[offset].runTwoPhaseBuild") {
    std::string indexName = createIndex(BSON("a" << 1));
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    catalog->setIndexBuildDraining(opCtx.get(), ns(), indexName);
}

DEATH_TEST_F(KVCollectionCatalogEntryTest,
             CannotSetIndividualPathComponentsOfTextIndexAsMultikey,
             "Invariant failure multikeyPaths.empty()") {
    std::string indexType = IndexNames::TEXT;
    std::string indexName = createIndex(BSON("a" << indexType << "b" << 1), indexType);
    auto opCtx = newOperationContext();
    DurableCatalog* catalog = getCatalog();
    catalog->setIndexIsMultikey(opCtx.get(), ns(), indexName, {{0U}, {0U}});
}

}  // namespace
}  // namespace mongo
