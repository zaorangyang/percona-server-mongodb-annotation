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
#include "mongo/db/catalog/uuid_catalog.h"

#include <algorithm>
#include <boost/optional/optional_io.hpp>

#include "mongo/db/catalog/collection_catalog_entry_mock.h"
#include "mongo/db/catalog/collection_mock.h"
#include "mongo/db/concurrency/lock_manager_defs.h"
#include "mongo/db/operation_context_noop.h"
#include "mongo/unittest/death_test.h"
#include "mongo/unittest/unittest.h"

using namespace mongo;

/**
 * A test fixture that creates a UUID Catalog and Collection* pointer to store in it.
 */
class UUIDCatalogTest : public unittest::Test {
public:
    UUIDCatalogTest()
        : nss("testdb", "testcol"),
          col(nullptr),
          colUUID(CollectionUUID::gen()),
          nextUUID(CollectionUUID::gen()),
          prevUUID(CollectionUUID::gen()) {
        if (prevUUID > colUUID)
            std::swap(prevUUID, colUUID);
        if (colUUID > nextUUID)
            std::swap(colUUID, nextUUID);
        if (prevUUID > colUUID)
            std::swap(prevUUID, colUUID);
        ASSERT_GT(colUUID, prevUUID);
        ASSERT_GT(nextUUID, colUUID);

        auto collection = std::make_unique<CollectionMock>(nss);
        auto catalogEntry = std::make_unique<CollectionCatalogEntryMock>(nss.ns());
        col = collection.get();
        // Register dummy collection in catalog.
        catalog.registerCatalogEntry(colUUID, std::move(catalogEntry));
        catalog.onCreateCollection(&opCtx, std::move(collection), colUUID);
    }

protected:
    UUIDCatalog catalog;
    OperationContextNoop opCtx;
    NamespaceString nss;
    CollectionMock* col;
    CollectionUUID colUUID;
    CollectionUUID nextUUID;
    CollectionUUID prevUUID;
};

class UUIDCatalogIterationTest : public unittest::Test {
public:
    void setUp() {
        for (int counter = 0; counter < 5; ++counter) {
            NamespaceString fooNss("foo", "coll" + std::to_string(counter));
            NamespaceString barNss("bar", "coll" + std::to_string(counter));

            auto fooUuid = CollectionUUID::gen();
            auto fooColl = std::make_unique<CollectionMock>(fooNss);
            auto fooCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(fooNss.ns());

            auto barUuid = CollectionUUID::gen();
            auto barColl = std::make_unique<CollectionMock>(barNss);
            auto barCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(barNss.ns());

            dbMap["foo"].insert(std::make_pair(fooUuid, fooColl.get()));
            dbMap["bar"].insert(std::make_pair(barUuid, barColl.get()));
            catalog.registerCatalogEntry(fooUuid, std::move(fooCatalogEntry));
            catalog.registerCatalogEntry(barUuid, std::move(barCatalogEntry));
            catalog.onCreateCollection(&opCtx, std::move(fooColl), fooUuid);
            catalog.onCreateCollection(&opCtx, std::move(barColl), barUuid);
        }
    }

    void tearDown() {
        for (auto& it : dbMap) {
            for (auto& kv : it.second) {
                catalog.onDropCollection(&opCtx, kv.first);
            }
        }
    }

    std::map<CollectionUUID, CollectionMock*>::iterator collsIterator(std::string dbName) {
        auto it = dbMap.find(dbName);
        ASSERT(it != dbMap.end());
        return it->second.begin();
    }

    std::map<CollectionUUID, CollectionMock*>::iterator collsIteratorEnd(std::string dbName) {
        auto it = dbMap.find(dbName);
        ASSERT(it != dbMap.end());
        return it->second.end();
    }

    void checkCollections(std::string dbName) {
        unsigned long counter = 0;

        for (auto[orderedIt, catalogIt] = std::tuple{collsIterator(dbName), catalog.begin(dbName)};
             catalogIt != catalog.end() && orderedIt != collsIteratorEnd(dbName);
             ++catalogIt, ++orderedIt) {

            auto catalogColl = *catalogIt;
            ASSERT(catalogColl != nullptr);
            auto orderedColl = orderedIt->second;
            ASSERT_EQ(catalogColl->ns(), orderedColl->ns());
            ++counter;
        }

        ASSERT_EQUALS(counter, dbMap[dbName].size());
    }

    void dropColl(const std::string dbName, CollectionUUID uuid) {
        dbMap[dbName].erase(uuid);
    }

protected:
    UUIDCatalog catalog;
    OperationContextNoop opCtx;
    std::map<std::string, std::map<CollectionUUID, CollectionMock*>> dbMap;
};

class UUIDCatalogResourceMapTest : public unittest::Test {
public:
    void setUp() {
        // The first and second collection namespaces map to the same ResourceId.
        firstCollection = "1661880728";
        secondCollection = "1626936312";

        firstResourceId = ResourceId(RESOURCE_COLLECTION, firstCollection);
        secondResourceId = ResourceId(RESOURCE_COLLECTION, secondCollection);
        ASSERT_EQ(firstResourceId, secondResourceId);

        thirdCollection = "2930102946";
        thirdResourceId = ResourceId(RESOURCE_COLLECTION, thirdCollection);
        ASSERT_NE(firstResourceId, thirdResourceId);
    }

protected:
    std::string firstCollection;
    ResourceId firstResourceId;

    std::string secondCollection;
    ResourceId secondResourceId;

    std::string thirdCollection;
    ResourceId thirdResourceId;

    UUIDCatalog catalog;
};

TEST_F(UUIDCatalogResourceMapTest, EmptyTest) {
    boost::optional<std::string> resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(boost::none, resource);

    catalog.removeResource(secondResourceId, secondCollection);
    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(boost::none, resource);
}

TEST_F(UUIDCatalogResourceMapTest, InsertTest) {
    catalog.addResource(firstResourceId, firstCollection);
    boost::optional<std::string> resource = catalog.lookupResourceName(thirdResourceId);
    ASSERT_EQ(boost::none, resource);

    catalog.addResource(thirdResourceId, thirdCollection);

    resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(firstCollection, *resource);

    resource = catalog.lookupResourceName(thirdResourceId);
    ASSERT_EQ(thirdCollection, resource);
}

TEST_F(UUIDCatalogResourceMapTest, RemoveTest) {
    catalog.addResource(firstResourceId, firstCollection);
    catalog.addResource(thirdResourceId, thirdCollection);

    // This fails to remove the resource because of an invalid namespace.
    catalog.removeResource(firstResourceId, "BadNamespace");
    boost::optional<std::string> resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(firstCollection, *resource);

    catalog.removeResource(firstResourceId, firstCollection);
    catalog.removeResource(firstResourceId, firstCollection);
    catalog.removeResource(thirdResourceId, thirdCollection);

    resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(boost::none, resource);

    resource = catalog.lookupResourceName(thirdResourceId);
    ASSERT_EQ(boost::none, resource);
}

TEST_F(UUIDCatalogResourceMapTest, CollisionTest) {
    // firstCollection and secondCollection map to the same ResourceId.
    catalog.addResource(firstResourceId, firstCollection);
    catalog.addResource(secondResourceId, secondCollection);

    // Looking up the namespace on a ResourceId while it has a collision should
    // return the empty string.
    boost::optional<std::string> resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(boost::none, resource);

    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(boost::none, resource);

    // We remove a namespace, resolving the collision.
    catalog.removeResource(firstResourceId, firstCollection);
    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(secondCollection, *resource);

    // Adding the same namespace twice does not create a collision.
    catalog.addResource(secondResourceId, secondCollection);
    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(secondCollection, *resource);

    // The map should function normally for entries without collisions.
    catalog.addResource(firstResourceId, firstCollection);
    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(boost::none, resource);

    catalog.addResource(thirdResourceId, thirdCollection);
    resource = catalog.lookupResourceName(thirdResourceId);
    ASSERT_EQ(thirdCollection, *resource);

    catalog.removeResource(thirdResourceId, thirdCollection);
    resource = catalog.lookupResourceName(thirdResourceId);
    ASSERT_EQ(boost::none, resource);

    catalog.removeResource(firstResourceId, firstCollection);
    catalog.removeResource(secondResourceId, secondCollection);

    resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(boost::none, resource);

    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(boost::none, resource);
}

class UUIDCatalogResourceTest : public unittest::Test {
public:
    void setUp() {
        for (int i = 0; i < 5; i++) {
            NamespaceString nss("resourceDb", "coll" + std::to_string(i));
            auto coll = std::make_unique<CollectionMock>(nss);
            auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(nss.ns());
            auto uuid = coll->uuid();

            catalog.registerCatalogEntry(uuid.get(), std::move(newCatalogEntry));
            catalog.onCreateCollection(&opCtx, std::move(coll), uuid.get());
        }

        int numEntries = 0;
        for (auto it = catalog.begin("resourceDb"); it != catalog.end(); it++) {
            auto coll = *it;
            std::string collName = coll->ns().ns();
            ResourceId rid(RESOURCE_COLLECTION, collName);

            ASSERT_NE(catalog.lookupResourceName(rid), boost::none);
            numEntries++;
        }
        ASSERT_EQ(5, numEntries);
    }

    void tearDown() {
        for (auto it = catalog.begin("resourceDb"); it != catalog.end(); ++it) {
            auto coll = *it;
            auto uuid = coll->uuid().get();
            if (!coll) {
                break;
            }

            catalog.deregisterCollectionObject(uuid);
            catalog.deregisterCatalogEntry(uuid);
        }

        int numEntries = 0;
        for (auto it = catalog.begin("resourceDb"); it != catalog.end(); it++) {
            numEntries++;
        }
        ASSERT_EQ(0, numEntries);
    }

protected:
    OperationContextNoop opCtx;
    UUIDCatalog catalog;
};

namespace {

TEST_F(UUIDCatalogResourceTest, RemoveAllResources) {
    catalog.deregisterAllCatalogEntriesAndCollectionObjects();

    const std::string dbName = "resourceDb";
    auto rid = ResourceId(RESOURCE_DATABASE, dbName);
    ASSERT_EQ(boost::none, catalog.lookupResourceName(rid));

    for (int i = 0; i < 5; i++) {
        NamespaceString nss("resourceDb", "coll" + std::to_string(i));
        rid = ResourceId(RESOURCE_COLLECTION, nss.ns());
        ASSERT_EQ(boost::none, catalog.lookupResourceName((rid)));
    }
}

TEST_F(UUIDCatalogResourceTest, LookupDatabaseResource) {
    const std::string dbName = "resourceDb";
    auto rid = ResourceId(RESOURCE_DATABASE, dbName);
    boost::optional<std::string> ridStr = catalog.lookupResourceName(rid);

    ASSERT(ridStr);
    ASSERT(ridStr->find(dbName) != std::string::npos);
}

TEST_F(UUIDCatalogResourceTest, LookupMissingDatabaseResource) {
    const std::string dbName = "missingDb";
    auto rid = ResourceId(RESOURCE_DATABASE, dbName);
    ASSERT(!catalog.lookupResourceName(rid));
}

TEST_F(UUIDCatalogResourceTest, LookupCollectionResource) {
    const std::string collNs = "resourceDb.coll1";
    auto rid = ResourceId(RESOURCE_COLLECTION, collNs);
    boost::optional<std::string> ridStr = catalog.lookupResourceName(rid);

    ASSERT(ridStr);
    ASSERT(ridStr->find(collNs) != std::string::npos);
}

TEST_F(UUIDCatalogResourceTest, LookupMissingCollectionResource) {
    const std::string dbName = "resourceDb.coll5";
    auto rid = ResourceId(RESOURCE_COLLECTION, dbName);
    ASSERT(!catalog.lookupResourceName(rid));
}

TEST_F(UUIDCatalogResourceTest, RemoveCollection) {
    const std::string collNs = "resourceDb.coll1";
    auto coll = catalog.lookupCollectionByNamespace(NamespaceString(collNs));
    auto uniqueColl = catalog.deregisterCollectionObject(coll->uuid().get());
    catalog.deregisterCatalogEntry(uniqueColl->uuid().get());
    auto rid = ResourceId(RESOURCE_COLLECTION, collNs);
    ASSERT(!catalog.lookupResourceName(rid));
}

// Create an iterator over the UUIDCatalog and assert that all collections are present.
// Iteration ends when the end of the catalog is reached.
TEST_F(UUIDCatalogIterationTest, EndAtEndOfCatalog) {
    checkCollections("foo");
}

// Create an iterator over the UUIDCatalog and test that all collections are present. Iteration ends
// when the end of a database-specific section of the catalog is reached.
TEST_F(UUIDCatalogIterationTest, EndAtEndOfSection) {
    checkCollections("bar");
}

// Delete an entry in the catalog while iterating.
TEST_F(UUIDCatalogIterationTest, InvalidateEntry) {
    auto it = catalog.begin("bar");

    // Invalidate bar.coll1.
    for (auto collsIt = collsIterator("bar"); collsIt != collsIteratorEnd("bar"); ++collsIt) {
        if (collsIt->second->ns().ns() == "bar.coll1") {
            catalog.onDropCollection(&opCtx, collsIt->first);
            dropColl("bar", collsIt->first);
            break;
        }
    }

    // Ensure bar.coll1 is not returned by the iterator.
    for (; it != catalog.end(); ++it) {
        auto coll = *it;
        ASSERT(coll && coll->ns().ns() != "bar.coll1");
    }
}

// Delete the entry pointed to by the iterator and dereference the iterator.
TEST_F(UUIDCatalogIterationTest, InvalidateAndDereference) {
    auto it = catalog.begin("bar");
    auto collsIt = collsIterator("bar");
    auto uuid = collsIt->first;
    catalog.onDropCollection(&opCtx, uuid);
    ++collsIt;

    ASSERT(it != catalog.end());
    auto catalogColl = *it;
    ASSERT(catalogColl != nullptr);
    ASSERT_EQUALS(catalogColl->ns(), collsIt->second->ns());

    dropColl("bar", uuid);
}

// Delete the last entry for a database while pointing to it and dereference the iterator.
TEST_F(UUIDCatalogIterationTest, InvalidateLastEntryAndDereference) {
    auto it = catalog.begin("bar");
    NamespaceString lastNs;
    boost::optional<CollectionUUID> uuid;
    for (auto collsIt = collsIterator("bar"); collsIt != collsIteratorEnd("bar"); ++collsIt) {
        lastNs = collsIt->second->ns();
        uuid = collsIt->first;
    }

    // Increment until it points to the last collection.
    for (; it != catalog.end(); ++it) {
        auto coll = *it;
        ASSERT(coll != nullptr);
        if (coll->ns() == lastNs) {
            break;
        }
    }

    catalog.onDropCollection(&opCtx, *uuid);
    dropColl("bar", *uuid);
    ASSERT(*it == nullptr);
}

// Delete the last entry in the map while pointing to it and dereference the iterator.
TEST_F(UUIDCatalogIterationTest, InvalidateLastEntryInMapAndDereference) {
    auto it = catalog.begin("foo");
    NamespaceString lastNs;
    boost::optional<CollectionUUID> uuid;
    for (auto collsIt = collsIterator("foo"); collsIt != collsIteratorEnd("foo"); ++collsIt) {
        lastNs = collsIt->second->ns();
        uuid = collsIt->first;
    }

    // Increment until it points to the last collection.
    for (; it != catalog.end(); ++it) {
        auto coll = *it;
        ASSERT(coll != nullptr);
        if (coll->ns() == lastNs) {
            break;
        }
    }

    catalog.onDropCollection(&opCtx, *uuid);
    dropColl("foo", *uuid);
    ASSERT(*it == nullptr);
}

TEST_F(UUIDCatalogTest, OnCreateCollection) {
    ASSERT(catalog.lookupCollectionByUUID(colUUID) == col);
}

TEST_F(UUIDCatalogTest, LookupCollectionByUUID) {
    // Ensure the string value of the NamespaceString of the obtained Collection is equal to
    // nss.ns().
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(colUUID)->ns().ns(), nss.ns());
    // Ensure lookups of unknown UUIDs result in null pointers.
    ASSERT(catalog.lookupCollectionByUUID(CollectionUUID::gen()) == nullptr);
}

TEST_F(UUIDCatalogTest, LookupNSSByUUID) {
    // Ensure the string value of the obtained NamespaceString is equal to nss.ns().
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID).ns(), nss.ns());
    // Ensure namespace lookups of unknown UUIDs result in empty NamespaceStrings.
    ASSERT_EQUALS(catalog.lookupNSSByUUID(CollectionUUID::gen()).ns(), NamespaceString().ns());
}

TEST_F(UUIDCatalogTest, InsertAfterLookup) {
    auto newUUID = CollectionUUID::gen();
    NamespaceString newNss(nss.db(), "newcol");
    auto newCollUnique = std::make_unique<CollectionMock>(newNss);
    auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(newNss.ns());
    auto newCol = newCollUnique.get();

    // Ensure that looking up non-existing UUIDs doesn't affect later registration of those UUIDs.
    ASSERT(catalog.lookupCollectionByUUID(newUUID) == nullptr);
    ASSERT(catalog.lookupNSSByUUID(newUUID) == NamespaceString());
    catalog.registerCatalogEntry(newUUID, std::move(newCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(newCollUnique), newUUID);
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(newUUID), newCol);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID), nss);
}

TEST_F(UUIDCatalogTest, OnDropCollection) {
    catalog.onDropCollection(&opCtx, colUUID);
    // Ensure the lookup returns a null pointer upon removing the colUUID entry.
    ASSERT(catalog.lookupCollectionByUUID(colUUID) == nullptr);
}

TEST_F(UUIDCatalogTest, RenameCollection) {
    auto uuid = CollectionUUID::gen();
    NamespaceString oldNss(nss.db(), "oldcol");
    auto collUnique = std::make_unique<CollectionMock>(oldNss);
    auto catalogEntry = std::make_unique<CollectionCatalogEntryMock>(oldNss.ns());
    auto collection = collUnique.get();
    catalog.registerCatalogEntry(uuid, std::move(catalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(collUnique), uuid);
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(uuid), collection);

    NamespaceString newNss(nss.db(), "newcol");
    catalog.setCollectionNamespace(&opCtx, collection, oldNss, newNss);
    ASSERT_EQ(collection->ns(), newNss);
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(uuid), collection);
}

TEST_F(UUIDCatalogTest, NonExistingNextCol) {
    ASSERT_FALSE(catalog.next(nss.db(), colUUID));
    ASSERT_FALSE(catalog.next(nss.db(), nextUUID));

    NamespaceString newNss("anotherdb", "newcol");
    auto newColl = std::make_unique<CollectionMock>(newNss);
    auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(newNss.ns());
    catalog.registerCatalogEntry(nextUUID, std::move(newCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(newColl), nextUUID);
    ASSERT_FALSE(catalog.next(nss.db(), colUUID));

    NamespaceString prevNss(nss.db(), "prevcol");
    auto prevColl = std::make_unique<CollectionMock>(prevNss);
    auto prevCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(prevNss.ns());
    catalog.registerCatalogEntry(prevUUID, std::move(prevCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(prevColl), prevUUID);
    ASSERT_FALSE(catalog.next(nss.db(), colUUID));
}

TEST_F(UUIDCatalogTest, ExistingNextCol) {
    NamespaceString nextNss(nss.db(), "next");
    auto newColl = std::make_unique<CollectionMock>(nextNss);
    auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(nextNss.ns());
    catalog.registerCatalogEntry(nextUUID, std::move(newCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(newColl), nextUUID);
    auto next = catalog.next(nss.db(), colUUID);
    ASSERT_TRUE(next);
    ASSERT_EQUALS(*next, nextUUID);
}

TEST_F(UUIDCatalogTest, NonExistingPrevCol) {
    ASSERT_FALSE(catalog.prev(nss.db(), colUUID));
    ASSERT_FALSE(catalog.prev(nss.db(), prevUUID));

    NamespaceString newNss("anotherdb", "newcol");
    auto newColl = std::make_unique<CollectionMock>(newNss);
    auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(newNss.ns());
    catalog.registerCatalogEntry(nextUUID, std::move(newCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(newColl), nextUUID);
    ASSERT_FALSE(catalog.prev(nss.db(), colUUID));

    catalog.onDropCollection(&opCtx, nextUUID);
    catalog.deregisterCatalogEntry(nextUUID);
    NamespaceString nextNss(nss.db(), "nextcol");
    auto nextColl = std::make_unique<CollectionMock>(nextNss);
    auto nextCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(nextNss.ns());
    catalog.registerCatalogEntry(nextUUID, std::move(nextCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(nextColl), nextUUID);
    ASSERT_FALSE(catalog.prev(nss.db(), colUUID));
}

TEST_F(UUIDCatalogTest, ExistingPrevCol) {
    NamespaceString prevNss(nss.db(), "prevcol");
    auto prevColl = std::make_unique<CollectionMock>(prevNss);
    auto prevCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(prevNss.ns());
    catalog.registerCatalogEntry(prevUUID, std::move(prevCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(prevColl), prevUUID);
    auto prev = catalog.prev(nss.db(), colUUID);
    ASSERT_TRUE(prev);
    ASSERT_EQUALS(*prev, prevUUID);
}

TEST_F(UUIDCatalogTest, NextPrevColOnEmptyCatalog) {
    catalog.onDropCollection(&opCtx, colUUID);
    ASSERT_FALSE(catalog.next(nss.db(), colUUID));
    ASSERT_FALSE(catalog.next(nss.db(), prevUUID));
    ASSERT_FALSE(catalog.prev(nss.db(), colUUID));
    ASSERT_FALSE(catalog.prev(nss.db(), nextUUID));
}

TEST_F(UUIDCatalogTest, InvalidateOrdering) {
    NamespaceString prevNss(nss.db(), "prevcol");
    auto prevColl = std::make_unique<CollectionMock>(prevNss);
    auto prevCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(prevNss.ns());
    catalog.registerCatalogEntry(prevUUID, std::move(prevCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(prevColl), prevUUID);

    NamespaceString nextNss(nss.db(), "nextcol");
    auto nextColl = std::make_unique<CollectionMock>(nextNss);
    auto nextCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(nextNss.ns());
    catalog.registerCatalogEntry(nextUUID, std::move(nextCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(nextColl), nextUUID);

    catalog.onDropCollection(&opCtx, colUUID);

    auto nextPrev = catalog.prev(nss.db(), nextUUID);
    ASSERT(nextPrev);
    ASSERT_EQUALS(*nextPrev, prevUUID);

    auto prevNext = catalog.next(nss.db(), prevUUID);
    ASSERT(prevNext);
    ASSERT_EQUALS(*prevNext, nextUUID);
}

TEST_F(UUIDCatalogTest, LookupNSSByUUIDForClosedCatalogReturnsOldNSSIfDropped) {
    catalog.onCloseCatalog(&opCtx);
    catalog.onDropCollection(&opCtx, colUUID);
    catalog.deregisterCatalogEntry(colUUID);
    ASSERT(catalog.lookupCollectionByUUID(colUUID) == nullptr);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID), nss);
    catalog.onOpenCatalog(&opCtx);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID), NamespaceString());
}

TEST_F(UUIDCatalogTest, LookupNSSByUUIDForClosedCatalogReturnsNewlyCreatedNSS) {
    auto newUUID = CollectionUUID::gen();
    NamespaceString newNss(nss.db(), "newcol");
    auto newCollUnique = std::make_unique<CollectionMock>(newNss);
    auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(newNss.ns());
    auto newCol = newCollUnique.get();

    // Ensure that looking up non-existing UUIDs doesn't affect later registration of those UUIDs.
    catalog.onCloseCatalog(&opCtx);
    ASSERT(catalog.lookupCollectionByUUID(newUUID) == nullptr);
    ASSERT(catalog.lookupNSSByUUID(newUUID) == NamespaceString());
    catalog.registerCatalogEntry(newUUID, std::move(newCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(newCollUnique), newUUID);
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(newUUID), newCol);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID), nss);

    // Ensure that collection still exists after opening the catalog again.
    catalog.onOpenCatalog(&opCtx);
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(newUUID), newCol);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID), nss);
}

TEST_F(UUIDCatalogTest, LookupNSSByUUIDForClosedCatalogReturnsFreshestNSS) {
    NamespaceString newNss(nss.db(), "newcol");
    auto newCollUnique = std::make_unique<CollectionMock>(newNss);
    auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(newNss.ns());
    auto newCol = newCollUnique.get();

    catalog.onCloseCatalog(&opCtx);
    catalog.onDropCollection(&opCtx, colUUID);
    catalog.deregisterCatalogEntry(colUUID);
    ASSERT(catalog.lookupCollectionByUUID(colUUID) == nullptr);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID), nss);
    catalog.registerCatalogEntry(colUUID, std::move(newCatalogEntry));
    catalog.onCreateCollection(&opCtx, std::move(newCollUnique), colUUID);
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(colUUID), newCol);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID), newNss);

    // Ensure that collection still exists after opening the catalog again.
    catalog.onOpenCatalog(&opCtx);
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(colUUID), newCol);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID), newNss);
}

DEATH_TEST_F(UUIDCatalogResourceTest, AddInvalidResourceType, "invariant") {
    auto rid = ResourceId(RESOURCE_GLOBAL, 0);
    catalog.addResource(rid, "");
}

}  // namespace
