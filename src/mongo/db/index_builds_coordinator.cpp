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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include "mongo/db/index_builds_coordinator.h"

#include "mongo/db/catalog/database_catalog_entry.h"
#include "mongo/db/catalog/database_holder.h"
#include "mongo/db/catalog/uuid_catalog.h"
#include "mongo/db/catalog_raii.h"
#include "mongo/db/concurrency/locker.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/op_observer.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/s/collection_sharding_state.h"
#include "mongo/db/s/database_sharding_state.h"
#include "mongo/db/service_context.h"
#include "mongo/s/shard_key_pattern.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

MONGO_FAIL_POINT_DEFINE(hangAfterIndexBuildFirstDrain);
MONGO_FAIL_POINT_DEFINE(hangAfterIndexBuildSecondDrain);
MONGO_FAIL_POINT_DEFINE(hangAfterIndexBuildDumpsInsertsFromBulk);

namespace {

constexpr auto kUniqueFieldName = "unique"_sd;
constexpr auto kKeyFieldName = "key"_sd;

/**
 * Returns the collection UUID for the given 'nss', or a NamespaceNotFound error.
 *
 * Momentarily takes the collection IS lock for 'nss' to access the collection UUID.
 */
StatusWith<UUID> getCollectionUUID(OperationContext* opCtx, const NamespaceString& nss) {
    try {
        AutoGetCollection autoColl(opCtx, nss, MODE_IS);
        return autoColl.getCollection()->uuid().get();
    } catch (const DBException& ex) {
        invariant(ex.toStatus().code() == ErrorCodes::NamespaceNotFound);
        return ex.toStatus();
    }
}

/**
 * Checks if unique index specification is compatible with sharding configuration.
 */
void checkShardKeyRestrictions(OperationContext* opCtx,
                               const NamespaceString& nss,
                               const BSONObj& newIdxKey) {
    invariant(opCtx->lockState()->isCollectionLockedForMode(nss.ns(), MODE_X));

    const auto metadata = CollectionShardingState::get(opCtx, nss)->getCurrentMetadata();
    if (!metadata->isSharded())
        return;

    const ShardKeyPattern shardKeyPattern(metadata->getKeyPattern());
    uassert(ErrorCodes::CannotCreateIndex,
            str::stream() << "cannot create unique index over " << newIdxKey
                          << " with shard key pattern "
                          << shardKeyPattern.toBSON(),
            shardKeyPattern.isUniqueIndexCompatible(newIdxKey));
}

/**
 * Aborts the index build identified by the provided 'replIndexBuildState'.
 *
 * Sets a signal on the coordinator's repl index build state if the builder does not yet exist in
 * the manager.
 */
void abortIndexBuild(WithLock lk,
                     IndexBuildsManager* indexBuildsManager,
                     std::shared_ptr<ReplIndexBuildState> replIndexBuildState,
                     const std::string& reason) {
    bool res = indexBuildsManager->abortIndexBuild(replIndexBuildState->buildUUID, reason);
    if (res) {
        return;
    }
    // The index builder was not found in the manager, so it only exists in the coordinator. In this
    // case, set the abort signal on the coordinator index build state.
    replIndexBuildState->aborted = true;
    replIndexBuildState->abortReason = reason;
}

/**
 * Logs the index build failure error in a standard format.
 */
void logFailure(Status status,
                const NamespaceString& nss,
                std::shared_ptr<ReplIndexBuildState> replState) {
    log() << "Index build failed: " << replState->buildUUID << ": " << nss << " ( "
          << replState->collectionUUID << " ): " << status;
}

}  // namespace

const auto getIndexBuildsCoord =
    ServiceContext::declareDecoration<std::unique_ptr<IndexBuildsCoordinator>>();

void IndexBuildsCoordinator::set(ServiceContext* serviceContext,
                                 std::unique_ptr<IndexBuildsCoordinator> ibc) {
    auto& indexBuildsCoordinator = getIndexBuildsCoord(serviceContext);
    invariant(!indexBuildsCoordinator);

    indexBuildsCoordinator = std::move(ibc);
}

IndexBuildsCoordinator* IndexBuildsCoordinator::get(ServiceContext* serviceContext) {
    auto& indexBuildsCoordinator = getIndexBuildsCoord(serviceContext);
    invariant(indexBuildsCoordinator);

    return indexBuildsCoordinator.get();
}

IndexBuildsCoordinator* IndexBuildsCoordinator::get(OperationContext* operationContext) {
    return get(operationContext->getServiceContext());
}

IndexBuildsCoordinator::~IndexBuildsCoordinator() {
    invariant(_databaseIndexBuilds.empty());
    invariant(_disallowedDbs.empty());
    invariant(_disallowedCollections.empty());
    invariant(_collectionIndexBuilds.empty());
}

StatusWith<std::pair<long long, long long>> IndexBuildsCoordinator::startIndexRebuildForRecovery(
    OperationContext* opCtx,
    DatabaseCatalogEntry* dbce,
    CollectionCatalogEntry* cce,
    const std::vector<BSONObj>& specs,
    const UUID& buildUUID) {
    // Index builds in recovery mode have the global write lock.
    invariant(opCtx->lockState()->isW());

    std::vector<std::string> indexNames;
    for (auto& spec : specs) {
        std::string name = spec.getStringField(IndexDescriptor::kIndexNameFieldName);
        if (name.empty()) {
            return Status(
                ErrorCodes::CannotCreateIndex,
                str::stream() << "Cannot create an index for a spec '" << spec
                              << "' without a non-empty string value for the 'name' field");
        }
        indexNames.push_back(name);
    }

    const auto& ns = cce->ns().ns();
    auto rs = dbce->getRecordStore(ns);

    ReplIndexBuildState::IndexCatalogStats indexCatalogStats;

    std::unique_ptr<Collection> collection;
    std::unique_ptr<MultiIndexBlock> indexer;
    {
        // These steps are combined into a single WUOW to ensure there are no commits without
        // the indexes.
        // 1) Drop all indexes.
        // 2) Open the Collection
        // 3) Start the index build process.

        WriteUnitOfWork wuow(opCtx);

        {  // 1
            for (size_t i = 0; i < indexNames.size(); i++) {
                Status s = cce->removeIndex(opCtx, indexNames[i]);
                if (!s.isOK()) {
                    return s;
                }
            }
        }

        // Indexes must be dropped before we open the Collection otherwise we could attempt to
        // open a bad index and fail.
        const auto uuid = cce->getCollectionOptions(opCtx).uuid;
        auto databaseHolder = DatabaseHolder::get(opCtx);
        collection = databaseHolder->makeCollection(opCtx, ns, uuid, cce, rs, dbce);

        // Register the index build. During recovery, collections may not have UUIDs present yet to
        // due upgrading. We don't require collection UUIDs during recovery except to create a
        // ReplIndexBuildState object.
        auto collectionUUID = UUID::gen();
        auto nss = collection->ns();
        auto dbName = nss.db().toString();

        // We run the index build using the single phase protocol as we already hold the global
        // write lock.
        auto replIndexBuildState = std::make_shared<ReplIndexBuildState>(
            buildUUID, collectionUUID, dbName, specs, IndexBuildProtocol::kSinglePhase);

        Status status = [&]() {
            stdx::unique_lock<stdx::mutex> lk(_mutex);
            return _registerIndexBuild(lk, replIndexBuildState);
        }();
        if (!status.isOK()) {
            return status;
        }

        // Setup the index build.
        indexCatalogStats.numIndexesBefore =
            _getNumIndexesTotal(opCtx, collection.get()) + indexNames.size();

        status = _indexBuildsManager.setUpIndexBuild(opCtx,
                                                     collection.get(),
                                                     specs,
                                                     buildUUID,
                                                     MultiIndexBlock::kNoopOnInitFn,
                                                     /*forRecovery=*/true);
        if (!status.isOK()) {
            // An index build failure during recovery is fatal.
            logFailure(status, nss, replIndexBuildState);
            fassertNoTrace(51086, status);
        }

        wuow.commit();
    }

    return _runIndexRebuildForRecovery(opCtx, collection.get(), indexCatalogStats, buildUUID);
}

Future<void> IndexBuildsCoordinator::joinIndexBuilds(const NamespaceString& nss,
                                                     const std::vector<BSONObj>& indexSpecs) {
    // TODO: implement. This code is just to make it compile.
    auto pf = makePromiseFuture<void>();
    auto promise = std::move(pf.promise);
    return std::move(pf.future);
}

void IndexBuildsCoordinator::interruptAllIndexBuilds(const std::string& reason) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    // Signal all the index builds to stop.
    for (auto& buildStateIt : _allIndexBuilds) {
        _indexBuildsManager.interruptIndexBuild(buildStateIt.second->buildUUID, reason);
    }

    // Wait for all the index builds to stop.
    for (auto& dbIt : _databaseIndexBuilds) {
        dbIt.second->waitUntilNoIndexBuildsRemain(lk);
    }
}

void IndexBuildsCoordinator::abortCollectionIndexBuilds(const UUID& collectionUUID,
                                                        const std::string& reason) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    // Ensure the caller correctly stopped any new index builds on the collection.
    auto it = _disallowedCollections.find(collectionUUID);
    invariant(it != _disallowedCollections.end());

    auto collIndexBuildsIt = _collectionIndexBuilds.find(collectionUUID);
    if (collIndexBuildsIt == _collectionIndexBuilds.end()) {
        return;
    }

    collIndexBuildsIt->second->runOperationOnAllBuilds(
        lk, &_indexBuildsManager, abortIndexBuild, reason);
    collIndexBuildsIt->second->waitUntilNoIndexBuildsRemain(lk);
}

void IndexBuildsCoordinator::abortDatabaseIndexBuilds(StringData db, const std::string& reason) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    // Ensure the caller correctly stopped any new index builds on the database.
    auto it = _disallowedDbs.find(db);
    invariant(it != _disallowedDbs.end());

    auto dbIndexBuilds = _databaseIndexBuilds[db];
    if (!dbIndexBuilds) {
        return;
    }

    dbIndexBuilds->runOperationOnAllBuilds(lk, &_indexBuildsManager, abortIndexBuild, reason);
    dbIndexBuilds->waitUntilNoIndexBuildsRemain(lk);
}

Future<void> IndexBuildsCoordinator::abortIndexBuildByName(
    const NamespaceString& nss,
    const std::vector<std::string>& indexNames,
    const std::string& reason) {
    // TODO: not yet implemented. Some code to make it compile.
    auto pf = makePromiseFuture<void>();
    auto promise = std::move(pf.promise);
    return std::move(pf.future);
}

Future<void> IndexBuildsCoordinator::abortIndexBuildByBuildUUID(const UUID& buildUUID,
                                                                const std::string& reason) {
    // TODO: not yet implemented. Some code to make it compile.
    auto pf = makePromiseFuture<void>();
    auto promise = std::move(pf.promise);
    return std::move(pf.future);
}

void IndexBuildsCoordinator::recoverIndexBuilds() {
    // TODO: not yet implemented.
}

int IndexBuildsCoordinator::numInProgForDb(StringData db) const {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto dbIndexBuildsIt = _databaseIndexBuilds.find(db);
    if (dbIndexBuildsIt == _databaseIndexBuilds.end()) {
        return 0;
    }
    return dbIndexBuildsIt->second->getNumberOfIndexBuilds(lk);
}

void IndexBuildsCoordinator::dump(std::ostream& ss) const {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    if (_collectionIndexBuilds.size()) {
        ss << "\n<b>Background Jobs in Progress</b>\n";
        // TODO: We should improve this to print index names per collection, not just collection
        // names.
        for (auto it = _collectionIndexBuilds.begin(); it != _collectionIndexBuilds.end(); ++it) {
            ss << "  " << it->first << '\n';
        }
    }

    for (auto it = _databaseIndexBuilds.begin(); it != _databaseIndexBuilds.end(); ++it) {
        ss << "database " << it->first << ": " << it->second->getNumberOfIndexBuilds(lk) << '\n';
    }
}

bool IndexBuildsCoordinator::inProgForCollection(const UUID& collectionUUID) const {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    return _collectionIndexBuilds.find(collectionUUID) != _collectionIndexBuilds.end();
}

bool IndexBuildsCoordinator::inProgForDb(StringData db) const {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    return _databaseIndexBuilds.find(db) != _databaseIndexBuilds.end();
}

void IndexBuildsCoordinator::assertNoIndexBuildInProgForCollection(
    const UUID& collectionUUID) const {
    uassert(ErrorCodes::BackgroundOperationInProgressForNamespace,
            mongoutils::str::stream()
                << "cannot perform operation: an index build is currently running",
            !inProgForCollection(collectionUUID));
}

void IndexBuildsCoordinator::assertNoBgOpInProgForDb(StringData db) const {
    uassert(ErrorCodes::BackgroundOperationInProgressForDatabase,
            mongoutils::str::stream()
                << "cannot perform operation: an index build is currently running for "
                   "database "
                << db,
            !inProgForDb(db));
}

void IndexBuildsCoordinator::awaitNoBgOpInProgForNs(OperationContext* opCtx, StringData ns) const {
    auto statusWithCollectionUUID = getCollectionUUID(opCtx, NamespaceString(ns));
    if (!statusWithCollectionUUID.isOK()) {
        // The collection does not exist, so there are no index builds on it.
        invariant(statusWithCollectionUUID.getStatus().code() == ErrorCodes::NamespaceNotFound);
        return;
    }

    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto collIndexBuildsIt = _collectionIndexBuilds.find(statusWithCollectionUUID.getValue());
    if (collIndexBuildsIt == _collectionIndexBuilds.end()) {
        return;
    }

    collIndexBuildsIt->second->waitUntilNoIndexBuildsRemain(lk);
}

void IndexBuildsCoordinator::awaitNoBgOpInProgForDb(StringData db) const {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto dbIndexBuildsIt = _databaseIndexBuilds.find(db);
    if (dbIndexBuildsIt != _databaseIndexBuilds.end()) {
        return;
    }

    dbIndexBuildsIt->second->waitUntilNoIndexBuildsRemain(lk);
}

void IndexBuildsCoordinator::onReplicaSetReconfig() {
    // TODO: not yet implemented.
}

void IndexBuildsCoordinator::sleepIndexBuilds_forTestOnly(bool sleep) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    _sleepForTest = sleep;
}

void IndexBuildsCoordinator::verifyNoIndexBuilds_forTestOnly() {
    invariant(_databaseIndexBuilds.empty());
    invariant(_disallowedDbs.empty());
    invariant(_disallowedCollections.empty());
    invariant(_collectionIndexBuilds.empty());
}

Status IndexBuildsCoordinator::_registerIndexBuild(
    WithLock lk, std::shared_ptr<ReplIndexBuildState> replIndexBuildState) {

    auto itns = _disallowedCollections.find(replIndexBuildState->collectionUUID);
    auto itdb = _disallowedDbs.find(replIndexBuildState->dbName);
    if (itns != _disallowedCollections.end() || itdb != _disallowedDbs.end()) {
        return Status(ErrorCodes::CannotCreateIndex,
                      str::stream() << "Collection ( " << replIndexBuildState->collectionUUID
                                    << " ) is in the process of being dropped. New index builds "
                                       "are not currently allowed.");
    }

    // Check whether any indexes are already being built with the same index name(s). (Duplicate
    // specs will be discovered by the index builder.)
    auto collIndexBuildsIt = _collectionIndexBuilds.find(replIndexBuildState->collectionUUID);
    if (collIndexBuildsIt != _collectionIndexBuilds.end()) {
        for (const auto& name : replIndexBuildState->indexNames) {
            if (collIndexBuildsIt->second->hasIndexBuildState(lk, name)) {
                return Status(ErrorCodes::IndexKeySpecsConflict,
                              str::stream() << "There's already an index with name '" << name
                                            << "' being built on the collection: "
                                            << " ( "
                                            << replIndexBuildState->collectionUUID
                                            << " )");
            }
        }
    }

    // Register the index build.

    auto dbIndexBuilds = _databaseIndexBuilds[replIndexBuildState->dbName];
    if (!dbIndexBuilds) {
        _databaseIndexBuilds[replIndexBuildState->dbName] =
            std::make_shared<DatabaseIndexBuildsTracker>();
        dbIndexBuilds = _databaseIndexBuilds[replIndexBuildState->dbName];
    }
    dbIndexBuilds->addIndexBuild(lk, replIndexBuildState);

    auto collIndexBuildsItAndRes = _collectionIndexBuilds.insert(
        {replIndexBuildState->collectionUUID, std::make_shared<CollectionIndexBuildsTracker>()});
    collIndexBuildsItAndRes.first->second->addIndexBuild(lk, replIndexBuildState);

    invariant(_allIndexBuilds.emplace(replIndexBuildState->buildUUID, replIndexBuildState).second);

    return Status::OK();
}

void IndexBuildsCoordinator::_unregisterIndexBuild(
    WithLock lk, std::shared_ptr<ReplIndexBuildState> replIndexBuildState) {
    auto dbIndexBuilds = _databaseIndexBuilds[replIndexBuildState->dbName];
    invariant(dbIndexBuilds);
    dbIndexBuilds->removeIndexBuild(lk, replIndexBuildState->buildUUID);
    if (dbIndexBuilds->getNumberOfIndexBuilds(lk) == 0) {
        _databaseIndexBuilds.erase(replIndexBuildState->dbName);
    }

    auto collIndexBuildsIt = _collectionIndexBuilds.find(replIndexBuildState->collectionUUID);
    invariant(collIndexBuildsIt != _collectionIndexBuilds.end());
    collIndexBuildsIt->second->removeIndexBuild(lk, replIndexBuildState);
    if (collIndexBuildsIt->second->getNumberOfIndexBuilds(lk) == 0) {
        _collectionIndexBuilds.erase(collIndexBuildsIt);
    }

    invariant(_allIndexBuilds.erase(replIndexBuildState->buildUUID));
}

StatusWith<boost::optional<SharedSemiFuture<ReplIndexBuildState::IndexCatalogStats>>>
IndexBuildsCoordinator::_registerAndSetUpIndexBuild(OperationContext* opCtx,
                                                    CollectionUUID collectionUUID,
                                                    const std::vector<BSONObj>& specs,
                                                    const UUID& buildUUID,
                                                    IndexBuildProtocol protocol) {
    auto nss = UUIDCatalog::get(opCtx).lookupNSSByUUID(collectionUUID);
    if (nss.isEmpty()) {
        return Status(ErrorCodes::NamespaceNotFound,
                      str::stream() << "Cannot create index on collection '" << collectionUUID
                                    << "' because the collection no longer exists.");
    }
    auto dbName = nss.db().toString();

    AutoGetDb autoDb(opCtx, dbName, MODE_X);
    if (!autoDb.getDb()) {
        return Status(ErrorCodes::NamespaceNotFound,
                      str::stream() << "Failed to create index(es) on collection '" << nss
                                    << "' because the collection no longer exists");
    }

    auto collection = autoDb.getDb()->getCollection(opCtx, nss);
    if (!collection) {
        // The collection does not exist. We will not build an index.
        return Status(ErrorCodes::NamespaceNotFound,
                      str::stream() << "Failed to create index(es) on collection '" << nss
                                    << "' because the collection no longer exists");
    }

    // Lock from when we ascertain what indexes to build through to when the build is registered
    // on the Coordinator and persistedly set up in the catalog. This serializes setting up an
    // index build so that no attempts are made to register the same build twice.
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    std::vector<BSONObj> filteredSpecs;
    try {
        filteredSpecs = _addDefaultsAndFilterExistingIndexes(opCtx, collection, nss, specs);
    } catch (const DBException& ex) {
        return ex.toStatus();
    }

    if (filteredSpecs.size() == 0) {
        // The requested index (specs) are already built or are being built. Return success
        // early (this is v4.0 behavior compatible).
        ReplIndexBuildState::IndexCatalogStats indexCatalogStats;
        int numIndexes = _getNumIndexesTotal(opCtx, collection);
        indexCatalogStats.numIndexesBefore = numIndexes;
        indexCatalogStats.numIndexesAfter = numIndexes;
        return SharedSemiFuture(indexCatalogStats);
    }

    auto replIndexBuildState = std::make_shared<ReplIndexBuildState>(
        buildUUID, collectionUUID, dbName, filteredSpecs, protocol);
    replIndexBuildState->stats.numIndexesBefore = _getNumIndexesTotal(opCtx, collection);

    Status status = _registerIndexBuild(lk, replIndexBuildState);
    if (!status.isOK()) {
        return status;
    }

    MultiIndexBlock::OnInitFn onInitFn;
    // Two-phase index builds write a different oplog entry than the default behavior which
    // writes a no-op just to generate an optime.
    if (IndexBuildProtocol::kTwoPhase == replIndexBuildState->protocol) {
        onInitFn = [&] {
            opCtx->getServiceContext()->getOpObserver()->onStartIndexBuild(
                opCtx,
                nss,
                replIndexBuildState->collectionUUID,
                replIndexBuildState->buildUUID,
                filteredSpecs,
                false /* fromMigrate */);
        };
    } else {
        onInitFn = MultiIndexBlock::makeTimestampedIndexOnInitFn(opCtx, collection);
    }

    status = _indexBuildsManager.setUpIndexBuild(opCtx,
                                                 collection,
                                                 filteredSpecs,
                                                 replIndexBuildState->buildUUID,
                                                 onInitFn,
                                                 /*forRecovery=*/false);
    if (!status.isOK()) {
        // Unregister the index build before setting the promise, so callers do not see the build
        // again.
        _unregisterIndexBuild(lk, replIndexBuildState);

        // Set the promise in case another thread already joined the index build.
        replIndexBuildState->sharedPromise.setError(status);

        return status;
    }

    return boost::none;
}

void IndexBuildsCoordinator::_runIndexBuild(OperationContext* opCtx,
                                            const UUID& buildUUID) noexcept {
    {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        while (_sleepForTest) {
            lk.unlock();
            sleepmillis(100);
            lk.lock();
        }
    }

    auto replState = [&] {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        auto it = _allIndexBuilds.find(buildUUID);
        invariant(it != _allIndexBuilds.end());
        return it->second;
    }();

    // 'status' should always be set to something else before this function exits.
    Status status{ErrorCodes::InternalError,
                  "Uninitialized status value in IndexBuildsCoordinator"};

    ON_BLOCK_EXIT([&] {
        // Ensure the index build is unregistered from the Coordinator and the Promise is set with
        // the build's result so that callers are notified of the outcome.

        invariant(status.code() != ErrorCodes::InternalError, status.toString());

        stdx::unique_lock<stdx::mutex> lk(_mutex);

        _unregisterIndexBuild(lk, replState);

        if (status.isOK()) {
            replState->sharedPromise.emplaceValue(replState->stats);
        } else {
            replState->sharedPromise.setError(status);
        }
    });

    NamespaceString nss = UUIDCatalog::get(opCtx).lookupNSSByUUID(replState->collectionUUID);

    invariant(!nss.isEmpty(),
              str::stream() << "Collection '" << replState->collectionUUID
                            << "' should exist because an index build is in progress.");

    // Do not use AutoGetOrCreateDb because we may relock the database in mode IX.
    Lock::DBLock dbLock(opCtx, nss.db(), MODE_X);

    // Allow the strong lock acquisition above to be interrupted, but from this point forward do
    // not allow locks or re-locks to be interrupted.
    UninterruptibleLockGuard noInterrupt(opCtx->lockState());

    auto collection = UUIDCatalog::get(opCtx).lookupCollectionByUUID(replState->collectionUUID);
    invariant(collection,
              str::stream() << "Collection " << nss
                            << " should exist because an index build is in progress.");

    try {
        _buildIndex(opCtx, collection, nss, replState, &dbLock);
        replState->stats.numIndexesAfter = _getNumIndexesTotal(opCtx, collection);
    } catch (const DBException& ex) {
        status = ex.toStatus();
        logFailure(status, nss, replState);
        return;
    }

    invariant(opCtx->lockState()->isDbLockedForMode(replState->dbName, MODE_X));
    _indexBuildsManager.tearDownIndexBuild(opCtx, collection, replState->buildUUID);

    log() << "Index build completed successfully: " << replState->buildUUID << ": " << nss << " ( "
          << replState->collectionUUID << " ). Index specs built: " << replState->indexSpecs.size()
          << ". Indexes in catalog before build: " << replState->stats.numIndexesBefore
          << ". Indexes in catalog after build: " << replState->stats.numIndexesAfter;

    status = Status::OK();

    return;
}

void IndexBuildsCoordinator::_buildIndex(OperationContext* opCtx,
                                         Collection* collection,
                                         const NamespaceString& nss,
                                         std::shared_ptr<ReplIndexBuildState> replState,
                                         Lock::DBLock* dbLock) {
    invariant(opCtx->lockState()->isDbLockedForMode(replState->dbName, MODE_X));

    // If we're a background index, replace exclusive db lock with an intent lock, so that
    // other readers and writers can proceed during this phase.
    if (_indexBuildsManager.isBackgroundBuilding(replState->buildUUID)) {
        opCtx->recoveryUnit()->abandonSnapshot();
        dbLock->relockWithMode(MODE_IX);
    }

    auto relockOnErrorGuard = makeGuard([&] {
        // Must have exclusive DB lock before we clean up the index build via the
        // destructor of 'indexer'.
        if (_indexBuildsManager.isBackgroundBuilding(replState->buildUUID)) {
            opCtx->recoveryUnit()->abandonSnapshot();
            dbLock->relockWithMode(MODE_X);
        }
    });

    // Collection scan and insert into index, followed by a drain of writes received in the
    // background.
    {
        Lock::CollectionLock colLock(opCtx->lockState(), nss.ns(), MODE_IX);
        uassertStatusOK(
            _indexBuildsManager.startBuildingIndex(opCtx, collection, replState->buildUUID));
    }

    if (MONGO_FAIL_POINT(hangAfterIndexBuildDumpsInsertsFromBulk)) {
        log() << "Hanging after dumping inserts from bulk builder";
        MONGO_FAIL_POINT_PAUSE_WHILE_SET(hangAfterIndexBuildDumpsInsertsFromBulk);
    }

    // Perform the first drain while holding an intent lock.
    {
        opCtx->recoveryUnit()->abandonSnapshot();
        Lock::CollectionLock colLock(opCtx->lockState(), nss.ns(), MODE_IS);

        uassertStatusOK(_indexBuildsManager.drainBackgroundWrites(opCtx, replState->buildUUID));
    }

    if (MONGO_FAIL_POINT(hangAfterIndexBuildFirstDrain)) {
        log() << "Hanging after index build first drain";
        MONGO_FAIL_POINT_PAUSE_WHILE_SET(hangAfterIndexBuildFirstDrain);
    }

    // Perform the second drain while stopping writes on the collection.
    {
        opCtx->recoveryUnit()->abandonSnapshot();
        Lock::CollectionLock colLock(opCtx->lockState(), nss.ns(), MODE_S);

        uassertStatusOK(_indexBuildsManager.drainBackgroundWrites(opCtx, replState->buildUUID));
    }

    if (MONGO_FAIL_POINT(hangAfterIndexBuildSecondDrain)) {
        log() << "Hanging after index build second drain";
        MONGO_FAIL_POINT_PAUSE_WHILE_SET(hangAfterIndexBuildSecondDrain);
    }

    relockOnErrorGuard.dismiss();

    // Need to return db lock back to exclusive, to complete the index build.
    if (_indexBuildsManager.isBackgroundBuilding(replState->buildUUID)) {
        opCtx->recoveryUnit()->abandonSnapshot();
        dbLock->relockWithMode(MODE_X);

        auto db = DatabaseHolder::get(opCtx)->getDb(opCtx, nss.db());
        if (db) {
            auto& dss = DatabaseShardingState::get(db);
            auto dssLock = DatabaseShardingState::DSSLock::lock(opCtx, &dss);
            dss.checkDbVersion(opCtx, dssLock);
        }

        invariant(db,
                  str::stream() << "Database not found after relocking. Index build: "
                                << replState->buildUUID
                                << ": "
                                << nss.ns()
                                << " ("
                                << replState->collectionUUID
                                << ")");
        invariant(db->getCollection(opCtx, nss),
                  str::stream() << "Collection not found after relocking. Index build: "
                                << replState->buildUUID
                                << ": "
                                << nss.ns()
                                << " ("
                                << replState->collectionUUID
                                << ")");
    }

    // Perform the third and final drain after releasing a shared lock and reacquiring an
    // exclusive lock on the database.
    uassertStatusOK(_indexBuildsManager.drainBackgroundWrites(opCtx, replState->buildUUID));

    // Index constraint checking phase.
    uassertStatusOK(
        _indexBuildsManager.checkIndexConstraintViolations(opCtx, replState->buildUUID));

    auto collectionUUID = replState->collectionUUID;
    auto onCommitFn = MultiIndexBlock::kNoopOnCommitFn;
    auto onCreateEachFn = MultiIndexBlock::kNoopOnCreateEachFn;
    if (IndexBuildProtocol::kTwoPhase == replState->protocol) {
        // Two-phase index builds write one oplog entry for all indexes that are completed.
        onCommitFn = [&] {
            opCtx->getServiceContext()->getOpObserver()->onCommitIndexBuild(
                opCtx,
                nss,
                collectionUUID,
                replState->buildUUID,
                replState->indexSpecs,
                false /* fromMigrate */);
        };
    } else {
        // Single-phase index builds write an oplog entry per index being built.
        onCreateEachFn = [opCtx, &nss, &collectionUUID](const BSONObj& spec) {
            opCtx->getServiceContext()->getOpObserver()->onCreateIndex(
                opCtx, nss, collectionUUID, spec, false);
        };
    }

    // Commit index build.
    uassertStatusOK(_indexBuildsManager.commitIndexBuild(
        opCtx, collection, nss, replState->buildUUID, onCreateEachFn, onCommitFn));

    return;
}

StatusWith<std::pair<long long, long long>> IndexBuildsCoordinator::_runIndexRebuildForRecovery(
    OperationContext* opCtx,
    Collection* collection,
    ReplIndexBuildState::IndexCatalogStats& indexCatalogStats,
    const UUID& buildUUID) noexcept {
    // Index builds in recovery mode have the global write lock.
    invariant(opCtx->lockState()->isW());

    auto replState = [&] {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        auto it = _allIndexBuilds.find(buildUUID);
        invariant(it != _allIndexBuilds.end());
        return it->second;
    }();

    // We rely on 'collection' for any collection information because no databases are open during
    // recovery.
    NamespaceString nss = collection->ns();
    invariant(!nss.isEmpty());

    auto status = Status::OK();

    long long numRecords = 0;
    long long dataSize = 0;

    try {
        log() << "Index builds manager starting: " << buildUUID << ": " << nss;

        std::tie(numRecords, dataSize) = uassertStatusOK(
            _indexBuildsManager.startBuildingIndexForRecovery(opCtx, collection->ns(), buildUUID));

        // Commit the index build.
        uassertStatusOK(_indexBuildsManager.commitIndexBuild(opCtx,
                                                             collection,
                                                             nss,
                                                             buildUUID,
                                                             MultiIndexBlock::kNoopOnCreateEachFn,
                                                             MultiIndexBlock::kNoopOnCommitFn));

        indexCatalogStats.numIndexesAfter = _getNumIndexesTotal(opCtx, collection);

        log() << "Index builds manager completed successfully: " << buildUUID << ": " << nss
              << ". Index specs requested: " << replState->indexSpecs.size()
              << ". Indexes in catalog before build: " << indexCatalogStats.numIndexesBefore
              << ". Indexes in catalog after build: " << indexCatalogStats.numIndexesAfter;
    } catch (const DBException& ex) {
        status = ex.toStatus();
        invariant(status != ErrorCodes::IndexAlreadyExists);
        log() << "Index builds manager failed: " << buildUUID << ": " << nss << ": " << status;
    }

    // Index build is registered in manager regardless of IndexBuildsManager::setUpIndexBuild()
    // result.
    if (status.isOK()) {
        // A successful index build means that all the requested indexes are now part of the
        // catalog.
        _indexBuildsManager.tearDownIndexBuild(opCtx, collection, buildUUID);
    } else {
        // An index build failure during recovery is fatal.
        logFailure(status, nss, replState);
        fassertNoTrace(51076, status);
    }

    // 'numIndexesBefore' was before we cleared any unfinished indexes, so it must be the same
    // as 'numIndexesAfter', since we're going to be building any unfinished indexes too.
    invariant(indexCatalogStats.numIndexesBefore == indexCatalogStats.numIndexesAfter);

    {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        _unregisterIndexBuild(lk, replState);
    }

    if (status.isOK()) {
        return std::make_pair(numRecords, dataSize);
    }
    return status;
}

void IndexBuildsCoordinator::_stopIndexBuildsOnDatabase(StringData dbName) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto it = _disallowedDbs.find(dbName);
    if (it != _disallowedDbs.end()) {
        ++(it->second);
        return;
    }
    _disallowedDbs[dbName] = 1;
}

void IndexBuildsCoordinator::_stopIndexBuildsOnCollection(const UUID& collectionUUID) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto it = _disallowedCollections.find(collectionUUID);
    if (it != _disallowedCollections.end()) {
        ++(it->second);
        return;
    }
    _disallowedCollections[collectionUUID] = 1;
}

void IndexBuildsCoordinator::_allowIndexBuildsOnDatabase(StringData dbName) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto it = _disallowedDbs.find(dbName);
    invariant(it != _disallowedDbs.end());
    invariant(it->second);
    if (--(it->second) == 0) {
        _disallowedDbs.erase(it);
    }
}

void IndexBuildsCoordinator::_allowIndexBuildsOnCollection(const UUID& collectionUUID) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto it = _disallowedCollections.find(collectionUUID);
    invariant(it != _disallowedCollections.end());
    invariant(it->second > 0);
    if (--(it->second) == 0) {
        _disallowedCollections.erase(it);
    }
}

ScopedStopNewDatabaseIndexBuilds::ScopedStopNewDatabaseIndexBuilds(
    IndexBuildsCoordinator* indexBuildsCoordinator, StringData dbName)
    : _indexBuildsCoordinatorPtr(indexBuildsCoordinator), _dbName(dbName.toString()) {
    _indexBuildsCoordinatorPtr->_stopIndexBuildsOnDatabase(_dbName);
}

ScopedStopNewDatabaseIndexBuilds::~ScopedStopNewDatabaseIndexBuilds() {
    _indexBuildsCoordinatorPtr->_allowIndexBuildsOnDatabase(_dbName);
}

ScopedStopNewCollectionIndexBuilds::ScopedStopNewCollectionIndexBuilds(
    IndexBuildsCoordinator* indexBuildsCoordinator, const UUID& collectionUUID)
    : _indexBuildsCoordinatorPtr(indexBuildsCoordinator), _collectionUUID(collectionUUID) {
    _indexBuildsCoordinatorPtr->_stopIndexBuildsOnCollection(_collectionUUID);
}

ScopedStopNewCollectionIndexBuilds::~ScopedStopNewCollectionIndexBuilds() {
    _indexBuildsCoordinatorPtr->_allowIndexBuildsOnCollection(_collectionUUID);
}

int IndexBuildsCoordinator::_getNumIndexesTotal(OperationContext* opCtx, Collection* collection) {
    invariant(collection);
    const auto& nss = collection->ns();
    invariant(opCtx->lockState()->isLocked(),
              str::stream() << "Unable to get index count because collection was not locked"
                            << nss);

    auto indexCatalog = collection->getIndexCatalog();
    invariant(indexCatalog, str::stream() << "Collection is missing index catalog: " << nss.ns());

    return indexCatalog->numIndexesTotal(opCtx);
}

std::vector<BSONObj> IndexBuildsCoordinator::_addDefaultsAndFilterExistingIndexes(
    OperationContext* opCtx,
    Collection* collection,
    const NamespaceString& nss,
    const std::vector<BSONObj>& indexSpecs) {
    invariant(opCtx->lockState()->isCollectionLockedForMode(nss.ns(), MODE_X));
    invariant(collection);

    auto specsWithCollationDefaults =
        uassertStatusOK(collection->addCollationDefaultsToIndexSpecsForCreate(opCtx, indexSpecs));

    auto indexCatalog = collection->getIndexCatalog();
    auto filteredSpecs = indexCatalog->removeExistingIndexes(
        opCtx, specsWithCollationDefaults, /*throwOnError=*/true);

    for (const BSONObj& spec : filteredSpecs) {
        if (spec[kUniqueFieldName].trueValue()) {
            checkShardKeyRestrictions(opCtx, nss, spec[kKeyFieldName].Obj());
        }
    }

    return filteredSpecs;
}

}  // namespace mongo
