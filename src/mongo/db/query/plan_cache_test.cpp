/**
 *    Copyright (C) 2014 MongoDB Inc.
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

/**
 * This file contains tests for mongo/db/query/plan_cache.h
 */

#include "mongo/db/query/plan_cache.h"

#include <algorithm>
#include <memory>
#include <ostream>

#include "mongo/db/jsobj.h"
#include "mongo/db/json.h"
#include "mongo/db/matcher/extensions_callback_noop.h"
#include "mongo/db/pipeline/expression_context_for_test.h"
#include "mongo/db/query/collation/collator_interface_mock.h"
#include "mongo/db/query/plan_ranker.h"
#include "mongo/db/query/query_knobs.h"
#include "mongo/db/query/query_planner.h"
#include "mongo/db/query/query_planner_test_lib.h"
#include "mongo/db/query/query_solution.h"
#include "mongo/db/query/query_test_service_context.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/transitional_tools_do_not_use/vector_spooling.h"

using namespace mongo;

using unittest::assertGet;

namespace {

using std::string;
using std::unique_ptr;
using std::vector;

static const NamespaceString nss("test.collection");

/**
 * Utility functions to create a CanonicalQuery
 */
unique_ptr<CanonicalQuery> canonicalize(const BSONObj& queryObj) {
    QueryTestServiceContext serviceContext;
    auto opCtx = serviceContext.makeOperationContext();

    auto qr = stdx::make_unique<QueryRequest>(nss);
    qr->setFilter(queryObj);
    const boost::intrusive_ptr<ExpressionContext> expCtx;
    auto statusWithCQ =
        CanonicalQuery::canonicalize(opCtx.get(),
                                     std::move(qr),
                                     expCtx,
                                     ExtensionsCallbackNoop(),
                                     MatchExpressionParser::kAllowAllSpecialFeatures);
    ASSERT_OK(statusWithCQ.getStatus());
    return std::move(statusWithCQ.getValue());
}

unique_ptr<CanonicalQuery> canonicalize(const char* queryStr) {
    BSONObj queryObj = fromjson(queryStr);
    return canonicalize(queryObj);
}

unique_ptr<CanonicalQuery> canonicalize(const char* queryStr,
                                        const char* sortStr,
                                        const char* projStr,
                                        const char* collationStr) {
    QueryTestServiceContext serviceContext;
    auto opCtx = serviceContext.makeOperationContext();

    auto qr = stdx::make_unique<QueryRequest>(nss);
    qr->setFilter(fromjson(queryStr));
    qr->setSort(fromjson(sortStr));
    qr->setProj(fromjson(projStr));
    qr->setCollation(fromjson(collationStr));
    const boost::intrusive_ptr<ExpressionContext> expCtx;
    auto statusWithCQ =
        CanonicalQuery::canonicalize(opCtx.get(),
                                     std::move(qr),
                                     expCtx,
                                     ExtensionsCallbackNoop(),
                                     MatchExpressionParser::kAllowAllSpecialFeatures);
    ASSERT_OK(statusWithCQ.getStatus());
    return std::move(statusWithCQ.getValue());
}

unique_ptr<CanonicalQuery> canonicalize(const char* queryStr,
                                        const char* sortStr,
                                        const char* projStr,
                                        long long skip,
                                        long long limit,
                                        const char* hintStr,
                                        const char* minStr,
                                        const char* maxStr) {
    QueryTestServiceContext serviceContext;
    auto opCtx = serviceContext.makeOperationContext();

    auto qr = stdx::make_unique<QueryRequest>(nss);
    qr->setFilter(fromjson(queryStr));
    qr->setSort(fromjson(sortStr));
    qr->setProj(fromjson(projStr));
    if (skip) {
        qr->setSkip(skip);
    }
    if (limit) {
        qr->setLimit(limit);
    }
    qr->setHint(fromjson(hintStr));
    qr->setMin(fromjson(minStr));
    qr->setMax(fromjson(maxStr));
    const boost::intrusive_ptr<ExpressionContext> expCtx;
    auto statusWithCQ =
        CanonicalQuery::canonicalize(opCtx.get(),
                                     std::move(qr),
                                     expCtx,
                                     ExtensionsCallbackNoop(),
                                     MatchExpressionParser::kAllowAllSpecialFeatures);
    ASSERT_OK(statusWithCQ.getStatus());
    return std::move(statusWithCQ.getValue());
}

unique_ptr<CanonicalQuery> canonicalize(const char* queryStr,
                                        const char* sortStr,
                                        const char* projStr,
                                        long long skip,
                                        long long limit,
                                        const char* hintStr,
                                        const char* minStr,
                                        const char* maxStr,
                                        bool explain) {
    QueryTestServiceContext serviceContext;
    auto opCtx = serviceContext.makeOperationContext();

    auto qr = stdx::make_unique<QueryRequest>(nss);
    qr->setFilter(fromjson(queryStr));
    qr->setSort(fromjson(sortStr));
    qr->setProj(fromjson(projStr));
    if (skip) {
        qr->setSkip(skip);
    }
    if (limit) {
        qr->setLimit(limit);
    }
    qr->setHint(fromjson(hintStr));
    qr->setMin(fromjson(minStr));
    qr->setMax(fromjson(maxStr));
    qr->setExplain(explain);
    const boost::intrusive_ptr<ExpressionContext> expCtx;
    auto statusWithCQ =
        CanonicalQuery::canonicalize(opCtx.get(),
                                     std::move(qr),
                                     expCtx,
                                     ExtensionsCallbackNoop(),
                                     MatchExpressionParser::kAllowAllSpecialFeatures);
    ASSERT_OK(statusWithCQ.getStatus());
    return std::move(statusWithCQ.getValue());
}

/**
 * Utility function to create MatchExpression
 */
unique_ptr<MatchExpression> parseMatchExpression(const BSONObj& obj) {
    boost::intrusive_ptr<ExpressionContextForTest> expCtx(new ExpressionContextForTest());
    StatusWithMatchExpression status =
        MatchExpressionParser::parse(obj,
                                     std::move(expCtx),
                                     ExtensionsCallbackNoop(),
                                     MatchExpressionParser::kAllowAllSpecialFeatures);
    if (!status.isOK()) {
        str::stream ss;
        ss << "failed to parse query: " << obj.toString()
           << ". Reason: " << status.getStatus().toString();
        FAIL(ss);
    }

    return std::move(status.getValue());
}

void assertEquivalent(const char* queryStr,
                      const MatchExpression* expected,
                      const MatchExpression* actual) {
    if (actual->equivalent(expected)) {
        return;
    }
    str::stream ss;
    ss << "Match expressions are not equivalent."
       << "\nOriginal query: " << queryStr << "\nExpected: " << expected->toString()
       << "\nActual: " << actual->toString();
    FAIL(ss);
}

//
// Tests for CachedSolution
//

/**
 * Generator for vector of QuerySolution shared pointers.
 */
struct GenerateQuerySolution {
    QuerySolution* operator()() const {
        unique_ptr<QuerySolution> qs(new QuerySolution());
        qs->cacheData.reset(new SolutionCacheData());
        qs->cacheData->solnType = SolutionCacheData::COLLSCAN_SOLN;
        qs->cacheData->tree.reset(new PlanCacheIndexTree());
        return qs.release();
    }
};

/**
 * Utility function to create a PlanRankingDecision
 */
std::unique_ptr<PlanRankingDecision> createDecision(size_t numPlans, size_t works = 0) {
    unique_ptr<PlanRankingDecision> why(new PlanRankingDecision());
    for (size_t i = 0; i < numPlans; ++i) {
        CommonStats common("COLLSCAN");
        auto stats = stdx::make_unique<PlanStageStats>(common, STAGE_COLLSCAN);
        stats->specific.reset(new CollectionScanStats());
        why->stats.push_back(std::move(stats));
        why->stats[i]->common.works = works;
        why->scores.push_back(0U);
        why->candidateOrder.push_back(i);
    }
    return why;
}

/**
 * Test functions for shouldCacheQuery
 * Use these functions to assert which categories
 * of canonicalized queries are suitable for inclusion
 * in the planner cache.
 */
void assertShouldCacheQuery(const CanonicalQuery& query) {
    if (PlanCache::shouldCacheQuery(query)) {
        return;
    }
    str::stream ss;
    ss << "Canonical query should be cacheable: " << query.toString();
    FAIL(ss);
}

void assertShouldNotCacheQuery(const CanonicalQuery& query) {
    if (!PlanCache::shouldCacheQuery(query)) {
        return;
    }
    str::stream ss;
    ss << "Canonical query should not be cacheable: " << query.toString();
    FAIL(ss);
}

void assertShouldNotCacheQuery(const BSONObj& query) {
    unique_ptr<CanonicalQuery> cq(canonicalize(query));
    assertShouldNotCacheQuery(*cq);
}

void assertShouldNotCacheQuery(const char* queryStr) {
    unique_ptr<CanonicalQuery> cq(canonicalize(queryStr));
    assertShouldNotCacheQuery(*cq);
}

std::unique_ptr<QuerySolution> getQuerySolutionForCaching() {
    std::unique_ptr<QuerySolution> qs = std::make_unique<QuerySolution>();
    qs->cacheData = stdx::make_unique<SolutionCacheData>();
    qs->cacheData->tree = stdx::make_unique<PlanCacheIndexTree>();
    return qs;
}

/**
 * Cacheable queries
 * These queries will be added to the cache with run-time statistics
 * and can be managed with the cache DB commands.
 */

TEST(PlanCacheTest, ShouldCacheQueryBasic) {
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    assertShouldCacheQuery(*cq);
}

TEST(PlanCacheTest, ShouldCacheQuerySort) {
    unique_ptr<CanonicalQuery> cq(canonicalize("{}", "{a: -1}", "{_id: 0, a: 1}", "{}"));
    assertShouldCacheQuery(*cq);
}

/*
 * Non-cacheable queries.
 * These queries will be sent through the planning process everytime.
 */

/**
 * Collection scan
 * This should normally be handled by the IDHack runner.
 */
TEST(PlanCacheTest, ShouldNotCacheQueryCollectionScan) {
    unique_ptr<CanonicalQuery> cq(canonicalize("{}"));
    assertShouldNotCacheQuery(*cq);
}

/**
 * Hint
 * A hinted query implies strong user preference for a particular index.
 * Therefore, not much point in caching.
 */
TEST(PlanCacheTest, ShouldNotCacheQueryWithHint) {
    unique_ptr<CanonicalQuery> cq(
        canonicalize("{a: 1}", "{}", "{}", 0, 0, "{a: 1, b: 1}", "{}", "{}"));
    assertShouldNotCacheQuery(*cq);
}

/**
 * Min queries are a specialized case of hinted queries
 */
TEST(PlanCacheTest, ShouldNotCacheQueryWithMin) {
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}", "{}", "{}", 0, 0, "{}", "{a: 100}", "{}"));
    assertShouldNotCacheQuery(*cq);
}

/**
 *  Max queries are non-cacheable for the same reasons as min queries.
 */
TEST(PlanCacheTest, ShouldNotCacheQueryWithMax) {
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}", "{}", "{}", 0, 0, "{}", "{}", "{a: 100}"));
    assertShouldNotCacheQuery(*cq);
}

/**
 * $geoWithin queries with legacy coordinates are cacheable as long as
 * the planner is able to come up with a cacheable solution.
 */
TEST(PlanCacheTest, ShouldCacheQueryWithGeoWithinLegacyCoordinates) {
    unique_ptr<CanonicalQuery> cq(
        canonicalize("{a: {$geoWithin: "
                     "{$box: [[-180, -90], [180, 90]]}}}"));
    assertShouldCacheQuery(*cq);
}

/**
 * $geoWithin queries with GeoJSON coordinates are supported by the index bounds builder.
 */
TEST(PlanCacheTest, ShouldCacheQueryWithGeoWithinJSONCoordinates) {
    unique_ptr<CanonicalQuery> cq(
        canonicalize("{a: {$geoWithin: "
                     "{$geometry: {type: 'Polygon', coordinates: "
                     "[[[0, 0], [0, 90], [90, 0], [0, 0]]]}}}}"));
    assertShouldCacheQuery(*cq);
}

/**
 * $geoWithin queries with both legacy and GeoJSON coordinates are cacheable.
 */
TEST(PlanCacheTest, ShouldCacheQueryWithGeoWithinLegacyAndJSONCoordinates) {
    unique_ptr<CanonicalQuery> cq(
        canonicalize("{$or: [{a: {$geoWithin: {$geometry: {type: 'Polygon', "
                     "coordinates: [[[0, 0], [0, 90], "
                     "[90, 0], [0, 0]]]}}}},"
                     "{a: {$geoWithin: {$box: [[-180, -90], [180, 90]]}}}]}"));
    assertShouldCacheQuery(*cq);
}

/**
 * $geoIntersects queries are always cacheable because they support GeoJSON coordinates only.
 */
TEST(PlanCacheTest, ShouldCacheQueryWithGeoIntersects) {
    unique_ptr<CanonicalQuery> cq(
        canonicalize("{a: {$geoIntersects: "
                     "{$geometry: {type: 'Point', coordinates: "
                     "[10.0, 10.0]}}}}"));
    assertShouldCacheQuery(*cq);
}

/**
 * $geoNear queries are cacheable because we are able to distinguish
 * between flat and spherical queries.
 */
TEST(PlanCacheTest, ShouldNotCacheQueryWithGeoNear) {
    unique_ptr<CanonicalQuery> cq(
        canonicalize("{a: {$geoNear: {$geometry: {type: 'Point',"
                     "coordinates: [0,0]}, $maxDistance:100}}}"));
    assertShouldCacheQuery(*cq);
}

/**
 * Explain queries are not-cacheable because of allPlans cannot
 * be accurately generated from stale cached stats in the plan cache for
 * non-winning plans.
 */
TEST(PlanCacheTest, ShouldNotCacheQueryExplain) {
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}",
                                               "{}",
                                               "{}",
                                               0,
                                               0,
                                               "{}",
                                               "{}",
                                               "{}",  // min, max
                                               true   // explain
                                               ));
    const QueryRequest& qr = cq->getQueryRequest();
    ASSERT_TRUE(qr.isExplain());
    assertShouldNotCacheQuery(*cq);
}

// Adding an empty vector of query solutions should fail.
TEST(PlanCacheTest, AddEmptySolutions) {
    PlanCache planCache;
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    std::vector<QuerySolution*> solns;
    unique_ptr<PlanRankingDecision> decision(createDecision(1U));
    QueryTestServiceContext serviceContext;
    ASSERT_NOT_OK(planCache.set(*cq, solns, std::move(decision), Date_t{}));
}

void addCacheEntryForShape(const CanonicalQuery& cq, PlanCache* planCache) {
    invariant(planCache);
    auto qs = getQuerySolutionForCaching();
    std::vector<QuerySolution*> solns = {qs.get()};

    ASSERT_OK(planCache->set(cq, solns, createDecision(1U), Date_t{}));
}

TEST(PlanCacheTest, InactiveEntriesDisabled) {
    // Set the global flag for disabling active entries.
    internalQueryCacheDisableInactiveEntries.store(true);
    ON_BLOCK_EXIT([] { internalQueryCacheDisableInactiveEntries.store(false); });

    PlanCache planCache;
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    auto qs = getQuerySolutionForCaching();
    std::vector<QuerySolution*> solns = {qs.get()};

    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
    QueryTestServiceContext serviceContext;
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U), Date_t{}));

    // After add, the planCache should have an _active_ entry.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentActive);

    // Call deactivate(). It should be a noop.
    planCache.deactivate(*cq);

    // The entry should still be active.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentActive);

    // remove() the entry.
    ASSERT_OK(planCache.remove(*cq));
    ASSERT_EQ(planCache.size(), 0U);
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
}


TEST(PlanCacheTest, PlanCacheLRUPolicyRemovesInactiveEntries) {
    // Use a tiny cache size.
    const size_t kCacheSize = 2;
    PlanCache planCache(kCacheSize);
    QueryTestServiceContext serviceContext;

    unique_ptr<CanonicalQuery> cqA(canonicalize("{a: 1}"));
    ASSERT_EQ(planCache.get(*cqA).state, PlanCache::CacheEntryState::kNotPresent);
    addCacheEntryForShape(*cqA.get(), &planCache);

    // After add, the planCache should have an inactive entry.
    ASSERT_EQ(planCache.get(*cqA).state, PlanCache::CacheEntryState::kPresentInactive);

    // Add a cache entry for another shape.
    unique_ptr<CanonicalQuery> cqB(canonicalize("{b: 1}"));
    ASSERT_EQ(planCache.get(*cqB).state, PlanCache::CacheEntryState::kNotPresent);
    addCacheEntryForShape(*cqB.get(), &planCache);
    ASSERT_EQ(planCache.get(*cqB).state, PlanCache::CacheEntryState::kPresentInactive);

    // Access the cached solution for the {a: 1} shape. Now the entry for {b: 1} will be the least
    // recently used.
    ASSERT_EQ(planCache.get(*cqA).state, PlanCache::CacheEntryState::kPresentInactive);

    // Insert another entry. Since the cache size is 2, we expect the {b: 1} entry to be ejected.
    unique_ptr<CanonicalQuery> cqC(canonicalize("{c: 1}"));
    ASSERT_EQ(planCache.get(*cqC).state, PlanCache::CacheEntryState::kNotPresent);
    addCacheEntryForShape(*cqC.get(), &planCache);

    // Check that {b: 1} is gone, but {a: 1} and {c: 1} both still have entries.
    ASSERT_EQ(planCache.get(*cqB).state, PlanCache::CacheEntryState::kNotPresent);
    ASSERT_EQ(planCache.get(*cqA).state, PlanCache::CacheEntryState::kPresentInactive);
    ASSERT_EQ(planCache.get(*cqC).state, PlanCache::CacheEntryState::kPresentInactive);
}

TEST(PlanCacheTest, PlanCacheRemoveDeletesInactiveEntries) {
    PlanCache planCache;
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    auto qs = getQuerySolutionForCaching();
    std::vector<QuerySolution*> solns = {qs.get()};

    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
    QueryTestServiceContext serviceContext;
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U), Date_t{}));

    // After add, the planCache should have an inactive entry.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentInactive);

    // remove() the entry.
    ASSERT_OK(planCache.remove(*cq));
    ASSERT_EQ(planCache.size(), 0U);
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
}

TEST(PlanCacheTest, PlanCacheFlushDeletesInactiveEntries) {
    PlanCache planCache;
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    auto qs = getQuerySolutionForCaching();
    std::vector<QuerySolution*> solns = {qs.get()};

    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
    QueryTestServiceContext serviceContext;
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U), Date_t{}));

    // After add, the planCache should have an inactive entry.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentInactive);

    // Clear the plan cache. The inactive entry should now be removed.
    planCache.clear();
    ASSERT_EQ(planCache.size(), 0U);
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
}

TEST(PlanCacheTest, AddActiveCacheEntry) {
    PlanCache planCache;
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    auto qs = getQuerySolutionForCaching();
    std::vector<QuerySolution*> solns = {qs.get()};

    // Check if key is in cache before and after set().
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
    QueryTestServiceContext serviceContext;
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 20), Date_t{}));

    // After add, the planCache should have an inactive entry.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentInactive);

    // Calling set() again, with a solution that had a lower works value should create an active
    // entry.
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 10), Date_t{}));
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentActive);
    ASSERT_EQUALS(planCache.size(), 1U);

    // Clear the plan cache. The active entry should now be removed.
    planCache.clear();
    ASSERT_EQ(planCache.size(), 0U);
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
}

TEST(PlanCacheTest, WorksValueIncreases) {
    PlanCache planCache;
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    auto qs = getQuerySolutionForCaching();
    std::vector<QuerySolution*> solns = {qs.get()};

    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
    QueryTestServiceContext serviceContext;
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 10), Date_t{}));

    // After add, the planCache should have an inactive entry.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentInactive);
    auto entry = assertGet(planCache.getEntry(*cq));
    ASSERT_EQ(entry->works, 10U);
    ASSERT_FALSE(entry->isActive);

    // Calling set() again, with a solution that had a higher works value. This should cause the
    // works on the original entry to be increased.
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 50), Date_t{}));

    // The entry should still be inactive. Its works should double though.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentInactive);
    entry = assertGet(planCache.getEntry(*cq));
    ASSERT_FALSE(entry->isActive);
    ASSERT_EQ(entry->works, 20U);

    // Calling set() again, with a solution that had a higher works value. This should cause the
    // works on the original entry to be increased.
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 30), Date_t{}));

    // The entry should still be inactive. Its works should have doubled again.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentInactive);
    entry = assertGet(planCache.getEntry(*cq));
    ASSERT_FALSE(entry->isActive);
    ASSERT_EQ(entry->works, 40U);

    // Calling set() again, with a solution that has a lower works value than what's currently in
    // the cache.
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 25), Date_t{}));

    // The solution just run should now be in an active cache entry, with a works
    // equal to the number of works the solution took.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentActive);
    entry = assertGet(planCache.getEntry(*cq));
    ASSERT_TRUE(entry->isActive);
    ASSERT_EQ(entry->decision->stats[0]->common.works, 25U);
    ASSERT_EQ(entry->works, 25U);

    ASSERT_EQUALS(planCache.size(), 1U);

    // Clear the plan cache. The active entry should now be removed.
    planCache.clear();
    ASSERT_EQ(planCache.size(), 0U);
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
}

TEST(PlanCacheTest, WorksValueIncreasesByAtLeastOne) {
    // Will use a very small growth coefficient.
    const double kWorksCoeff = 1.10;

    PlanCache planCache;
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    auto qs = getQuerySolutionForCaching();
    std::vector<QuerySolution*> solns = {qs.get()};

    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
    QueryTestServiceContext serviceContext;
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 3), Date_t{}));

    // After add, the planCache should have an inactive entry.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentInactive);
    auto entry = assertGet(planCache.getEntry(*cq));
    ASSERT_EQ(entry->works, 3U);
    ASSERT_FALSE(entry->isActive);

    // Calling set() again, with a solution that had a higher works value. This should cause the
    // works on the original entry to be increased. In this case, since nWorks is 3,
    // multiplying by the value 1.10 will give a value of 3 (static_cast<size_t>(1.1 * 3) == 3).
    // We check that the works value is increased 1 instead.
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 50), Date_t{}, kWorksCoeff));

    // The entry should still be inactive. Its works should increase by 1.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentInactive);
    entry = assertGet(planCache.getEntry(*cq));
    ASSERT_FALSE(entry->isActive);
    ASSERT_EQ(entry->works, 4U);

    // Clear the plan cache. The inactive entry should now be removed.
    planCache.clear();
    ASSERT_EQ(planCache.size(), 0U);
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
}

TEST(PlanCacheTest, SetIsNoopWhenNewEntryIsWorse) {
    PlanCache planCache;
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    auto qs = getQuerySolutionForCaching();
    std::vector<QuerySolution*> solns = {qs.get()};

    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
    QueryTestServiceContext serviceContext;
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 50), Date_t{}));

    // After add, the planCache should have an inactive entry.
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentInactive);
    auto entry = assertGet(planCache.getEntry(*cq));
    ASSERT_EQ(entry->works, 50U);
    ASSERT_FALSE(entry->isActive);

    // Call set() again, with a solution that has a lower works value. This will result in an
    // active entry being created.
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 20), Date_t{}));
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentActive);
    entry = assertGet(planCache.getEntry(*cq));
    ASSERT_TRUE(entry->isActive);
    ASSERT_EQ(entry->works, 20U);

    // Now call set() again, but with a solution that has a higher works value. This should be
    // a noop.
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 100), Date_t{}));
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentActive);
    entry = assertGet(planCache.getEntry(*cq));
    ASSERT_TRUE(entry->isActive);
    ASSERT_EQ(entry->works, 20U);
}

TEST(PlanCacheTest, SetOverwritesWhenNewEntryIsBetter) {
    PlanCache planCache;
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    auto qs = getQuerySolutionForCaching();
    std::vector<QuerySolution*> solns = {qs.get()};

    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
    QueryTestServiceContext serviceContext;
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 50), Date_t{}));

    // After add, the planCache should have an inactive entry.
    auto entry = assertGet(planCache.getEntry(*cq));
    ASSERT_EQ(entry->works, 50U);
    ASSERT_FALSE(entry->isActive);

    // Call set() again, with a solution that has a lower works value. This will result in an
    // active entry being created.
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 20), Date_t{}));
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentActive);
    entry = assertGet(planCache.getEntry(*cq));
    ASSERT_TRUE(entry->isActive);
    ASSERT_EQ(entry->works, 20U);

    // Now call set() again, with a solution that has a lower works value. The current active entry
    // should be overwritten.
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 10), Date_t{}));
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentActive);
    entry = assertGet(planCache.getEntry(*cq));
    ASSERT_TRUE(entry->isActive);
    ASSERT_EQ(entry->works, 10U);
}

TEST(PlanCacheTest, DeactivateCacheEntry) {
    PlanCache planCache;
    unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
    auto qs = getQuerySolutionForCaching();
    std::vector<QuerySolution*> solns = {qs.get()};

    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kNotPresent);
    QueryTestServiceContext serviceContext;
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 50), Date_t{}));

    // After add, the planCache should have an inactive entry.
    auto entry = assertGet(planCache.getEntry(*cq));
    ASSERT_EQ(entry->works, 50U);
    ASSERT_FALSE(entry->isActive);

    // Call set() again, with a solution that has a lower works value. This will result in an
    // active entry being created.
    ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 20), Date_t{}));
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentActive);
    entry = assertGet(planCache.getEntry(*cq));
    ASSERT_TRUE(entry->isActive);
    ASSERT_EQ(entry->works, 20U);

    planCache.deactivate(*cq);
    ASSERT_EQ(planCache.get(*cq).state, PlanCache::CacheEntryState::kPresentInactive);

    // Be sure the entry has the same works value.
    entry = assertGet(planCache.getEntry(*cq));
    ASSERT_FALSE(entry->isActive);
    ASSERT_EQ(entry->works, 20U);
}

TEST(PlanCacheTest, GetMatchingStatsMatchesAndSerializesCorrectly) {
    PlanCache planCache;

    // Create a cache entry with 5 works.
    {
        unique_ptr<CanonicalQuery> cq(canonicalize("{a: 1}"));
        auto qs = getQuerySolutionForCaching();
        std::vector<QuerySolution*> solns = {qs.get()};
        ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 5), Date_t{}));
    }

    // Create a second cache entry with 3 works.
    {
        unique_ptr<CanonicalQuery> cq(canonicalize("{b: 1}"));
        auto qs = getQuerySolutionForCaching();
        std::vector<QuerySolution*> solns = {qs.get()};
        ASSERT_OK(planCache.set(*cq, solns, createDecision(1U, 3), Date_t{}));
    }

    // Verify that the cache entries have been created.
    ASSERT_EQ(2U, planCache.size());

    // Define a serialization function which just serializes the number of works.
    const auto serializer = [](const PlanCacheEntry& entry) {
        return BSON("works" << static_cast<int>(entry.works));
    };

    // Define a matcher which matches if the number of works exceeds 4.
    const auto matcher = [](const BSONObj& serializedEntry) {
        BSONElement worksElt = serializedEntry["works"];
        return worksElt && worksElt.number() > 4;
    };

    // Verify the output of getMatchingStats().
    auto getStatsResult = planCache.getMatchingStats(serializer, matcher);
    ASSERT_EQ(1U, getStatsResult.size());
    ASSERT_BSONOBJ_EQ(BSON("works" << 5), getStatsResult[0]);
}

/**
 * Each test in the CachePlanSelectionTest suite goes through
 * the following flow:
 *
 * 1) Run QueryPlanner::plan on the query, with specified indices
 * available. This simulates the case in which we failed to plan from
 * the plan cache, and fell back on selecting a plan ourselves. The
 * enumerator will run, and cache data will be stashed into each solution
 * that it generates.
 *
 * 2) Use firstMatchingSolution to select one of the solutions generated
 * by QueryPlanner::plan. This simulates the multi plan runner picking
 * the "best solution".
 *
 * 3) The cache data stashed inside the "best solution" is used to
 * make a CachedSolution which looks exactly like the data structure that
 * would be returned from the cache. This simulates a plan cache hit.
 *
 * 4) Call QueryPlanner::planFromCache, passing it the CachedSolution.
 * This exercises the code which is able to map from a CachedSolution to
 * a full-blown QuerySolution. Finally, assert that the query solution
 * recovered from the cache is identical to the original "best solution".
 */
class CachePlanSelectionTest : public mongo::unittest::Test {
protected:
    void setUp() {
        params.options = QueryPlannerParams::INCLUDE_COLLSCAN;
        addIndex(BSON("_id" << 1), "_id_");
    }

    void addIndex(BSONObj keyPattern, const std::string& indexName, bool multikey = false) {
        // The first false means not multikey.
        // The second false means not sparse.
        // The NULL means no filter expression.
        params.indices.push_back(IndexEntry(keyPattern,
                                            multikey,
                                            false,
                                            false,
                                            IndexEntry::Identifier{indexName},
                                            NULL,
                                            BSONObj()));
    }

    void addIndex(BSONObj keyPattern, const std::string& indexName, bool multikey, bool sparse) {
        params.indices.push_back(IndexEntry(keyPattern,
                                            multikey,
                                            sparse,
                                            false,
                                            IndexEntry::Identifier{indexName},
                                            NULL,
                                            BSONObj()));
    }

    void addIndex(BSONObj keyPattern, const std::string& indexName, CollatorInterface* collator) {
        IndexEntry entry(
            keyPattern, false, false, false, IndexEntry::Identifier{indexName}, NULL, BSONObj());
        entry.collator = collator;
        params.indices.push_back(entry);
    }

    //
    // Execute planner.
    //

    void runQuery(BSONObj query) {
        runQuerySortProjSkipLimit(query, BSONObj(), BSONObj(), 0, 0);
    }

    void runQuerySortProj(const BSONObj& query, const BSONObj& sort, const BSONObj& proj) {
        runQuerySortProjSkipLimit(query, sort, proj, 0, 0);
    }

    void runQuerySkipLimit(const BSONObj& query, long long skip, long long limit) {
        runQuerySortProjSkipLimit(query, BSONObj(), BSONObj(), skip, limit);
    }

    void runQueryHint(const BSONObj& query, const BSONObj& hint) {
        runQuerySortProjSkipLimitHint(query, BSONObj(), BSONObj(), 0, 0, hint);
    }

    void runQuerySortProjSkipLimit(const BSONObj& query,
                                   const BSONObj& sort,
                                   const BSONObj& proj,
                                   long long skip,
                                   long long limit) {
        runQuerySortProjSkipLimitHint(query, sort, proj, skip, limit, BSONObj());
    }

    void runQuerySortHint(const BSONObj& query, const BSONObj& sort, const BSONObj& hint) {
        runQuerySortProjSkipLimitHint(query, sort, BSONObj(), 0, 0, hint);
    }

    void runQueryHintMinMax(const BSONObj& query,
                            const BSONObj& hint,
                            const BSONObj& minObj,
                            const BSONObj& maxObj) {
        runQueryFull(query, BSONObj(), BSONObj(), 0, 0, hint, minObj, maxObj);
    }

    void runQuerySortProjSkipLimitHint(const BSONObj& query,
                                       const BSONObj& sort,
                                       const BSONObj& proj,
                                       long long skip,
                                       long long limit,
                                       const BSONObj& hint) {
        runQueryFull(query, sort, proj, skip, limit, hint, BSONObj(), BSONObj());
    }

    void runQueryFull(const BSONObj& query,
                      const BSONObj& sort,
                      const BSONObj& proj,
                      long long skip,
                      long long limit,
                      const BSONObj& hint,
                      const BSONObj& minObj,
                      const BSONObj& maxObj) {
        QueryTestServiceContext serviceContext;
        auto opCtx = serviceContext.makeOperationContext();

        // Clean up any previous state from a call to runQueryFull or runQueryAsCommand.
        solns.clear();

        auto qr = std::make_unique<QueryRequest>(nss);
        qr->setFilter(query);
        qr->setSort(sort);
        qr->setProj(proj);
        if (skip) {
            qr->setSkip(skip);
        }
        if (limit) {
            qr->setLimit(limit);
        }
        qr->setHint(hint);
        qr->setMin(minObj);
        qr->setMax(maxObj);
        const boost::intrusive_ptr<ExpressionContext> expCtx;
        auto statusWithCQ =
            CanonicalQuery::canonicalize(opCtx.get(),
                                         std::move(qr),
                                         expCtx,
                                         ExtensionsCallbackNoop(),
                                         MatchExpressionParser::kAllowAllSpecialFeatures);
        ASSERT_OK(statusWithCQ.getStatus());
        auto statusWithSolutions = QueryPlanner::plan(*statusWithCQ.getValue(), params);
        ASSERT_OK(statusWithSolutions.getStatus());
        solns = std::move(statusWithSolutions.getValue());
    }

    void runQueryAsCommand(const BSONObj& cmdObj) {
        QueryTestServiceContext serviceContext;
        auto opCtx = serviceContext.makeOperationContext();

        // Clean up any previous state from a call to runQueryFull or runQueryAsCommand.
        solns.clear();

        const bool isExplain = false;
        std::unique_ptr<QueryRequest> qr(
            assertGet(QueryRequest::makeFromFindCommand(nss, cmdObj, isExplain)));

        const boost::intrusive_ptr<ExpressionContext> expCtx;
        auto statusWithCQ =
            CanonicalQuery::canonicalize(opCtx.get(),
                                         std::move(qr),
                                         expCtx,
                                         ExtensionsCallbackNoop(),
                                         MatchExpressionParser::kAllowAllSpecialFeatures);
        ASSERT_OK(statusWithCQ.getStatus());
        auto statusWithSolutions = QueryPlanner::plan(*statusWithCQ.getValue(), params);
        ASSERT_OK(statusWithSolutions.getStatus());
        solns = std::move(statusWithSolutions.getValue());
    }

    //
    // Solution introspection.
    //

    void dumpSolutions(str::stream& ost) const {
        for (auto&& soln : solns) {
            ost << soln->toString() << '\n';
        }
    }

    /**
     * Returns number of generated solutions matching JSON.
     */
    size_t numSolutionMatches(const string& solnJson) const {
        BSONObj testSoln = fromjson(solnJson);
        size_t matches = 0;
        for (auto&& soln : solns) {
            QuerySolutionNode* root = soln->root.get();
            if (QueryPlannerTestLib::solutionMatches(testSoln, root)) {
                ++matches;
            }
        }
        return matches;
    }

    /**
     * Verifies that the solution tree represented in json by 'solnJson' is
     * one of the solutions generated by QueryPlanner.
     *
     * The number of expected matches, 'numMatches', could be greater than
     * 1 if solutions differ only by the pattern of index tags on a filter.
     */
    void assertSolutionExists(const string& solnJson, size_t numMatches = 1) const {
        size_t matches = numSolutionMatches(solnJson);
        if (numMatches == matches) {
            return;
        }
        str::stream ss;
        ss << "expected " << numMatches << " matches for solution " << solnJson << " but got "
           << matches << " instead. all solutions generated: " << '\n';
        dumpSolutions(ss);
        FAIL(ss);
    }

    /**
     * Plan 'query' from the cache with sort order 'sort', projection 'proj', and collation
     * 'collation'. A mock cache entry is created using the cacheData stored inside the
     * QuerySolution 'soln'.
     */
    std::unique_ptr<QuerySolution> planQueryFromCache(const BSONObj& query,
                                                      const BSONObj& sort,
                                                      const BSONObj& proj,
                                                      const BSONObj& collation,
                                                      const QuerySolution& soln) const {
        QueryTestServiceContext serviceContext;
        auto opCtx = serviceContext.makeOperationContext();

        auto qr = stdx::make_unique<QueryRequest>(nss);
        qr->setFilter(query);
        qr->setSort(sort);
        qr->setProj(proj);
        qr->setCollation(collation);
        const boost::intrusive_ptr<ExpressionContext> expCtx;
        auto statusWithCQ =
            CanonicalQuery::canonicalize(opCtx.get(),
                                         std::move(qr),
                                         expCtx,
                                         ExtensionsCallbackNoop(),
                                         MatchExpressionParser::kAllowAllSpecialFeatures);
        ASSERT_OK(statusWithCQ.getStatus());
        unique_ptr<CanonicalQuery> scopedCq = std::move(statusWithCQ.getValue());

        // Create a CachedSolution the long way..
        // QuerySolution -> PlanCacheEntry -> CachedSolution
        QuerySolution qs;
        qs.cacheData.reset(soln.cacheData->clone());
        std::vector<QuerySolution*> solutions;
        solutions.push_back(&qs);

        uint32_t queryHash = PlanCache::computeQueryHash(ck);
        PlanCacheEntry entry(solutions, createDecision(1U).release(), queryHash);
        CachedSolution cachedSoln(ck, entry);

        auto statusWithQs = QueryPlanner::planFromCache(*scopedCq, params, cachedSoln);
        ASSERT_OK(statusWithQs.getStatus());
        return std::move(statusWithQs.getValue());
    }

    /**
     * @param solnJson -- a json representation of a query solution.
     *
     * Returns the first solution matching 'solnJson', or fails if
     * no match is found.
     */
    QuerySolution* firstMatchingSolution(const string& solnJson) const {
        BSONObj testSoln = fromjson(solnJson);
        for (auto&& soln : solns) {
            QuerySolutionNode* root = soln->root.get();
            if (QueryPlannerTestLib::solutionMatches(testSoln, root)) {
                return soln.get();
            }
        }

        str::stream ss;
        ss << "Could not find a match for solution " << solnJson
           << " All solutions generated: " << '\n';
        dumpSolutions(ss);
        FAIL(ss);

        return NULL;
    }

    /**
     * Assert that the QuerySolution 'trueSoln' matches the JSON-based representation
     * of the solution in 'solnJson'.
     *
     * Relies on solutionMatches() -- see query_planner_test_lib.h
     */
    void assertSolutionMatches(QuerySolution* trueSoln, const string& solnJson) const {
        BSONObj testSoln = fromjson(solnJson);
        if (!QueryPlannerTestLib::solutionMatches(testSoln, trueSoln->root.get())) {
            str::stream ss;
            ss << "Expected solution " << solnJson
               << " did not match true solution: " << trueSoln->toString() << '\n';
            FAIL(ss);
        }
    }

    /**
     * Overloaded so that it is not necessary to specificy sort and project.
     */
    void assertPlanCacheRecoversSolution(const BSONObj& query, const string& solnJson) {
        assertPlanCacheRecoversSolution(query, BSONObj(), BSONObj(), BSONObj(), solnJson);
    }

    /**
     * First, the solution matching 'solnJson' is retrieved from the vector
     * of solutions generated by QueryPlanner::plan. This solution is
     * then passed into planQueryFromCache(). Asserts that the solution
     * generated by QueryPlanner::planFromCache matches 'solnJson'.
     *
     * Must be called after calling one of the runQuery* methods.
     *
     * Together, 'query', 'sort', 'proj', and 'collation' should specify the query which was
     * previously run using one of the runQuery* methods.
     */
    void assertPlanCacheRecoversSolution(const BSONObj& query,
                                         const BSONObj& sort,
                                         const BSONObj& proj,
                                         const BSONObj& collation,
                                         const string& solnJson) {
        auto bestSoln = firstMatchingSolution(solnJson);
        auto planSoln = planQueryFromCache(query, sort, proj, collation, *bestSoln);
        assertSolutionMatches(planSoln.get(), solnJson);
    }

    /**
     * Check that the solution will not be cached. The planner will store
     * cache data inside non-cachable solutions, but will not do so for
     * non-cachable solutions. Therefore, we just have to check that
     * cache data is NULL.
     */
    void assertNotCached(const string& solnJson) {
        QuerySolution* bestSoln = firstMatchingSolution(solnJson);
        ASSERT(NULL != bestSoln);
        ASSERT(NULL == bestSoln->cacheData.get());
    }

    static const PlanCacheKey ck;

    BSONObj queryObj;
    QueryPlannerParams params;
    std::vector<std::unique_ptr<QuerySolution>> solns;
};

const PlanCacheKey CachePlanSelectionTest::ck = "mock_cache_key";

//
// Equality
//

TEST_F(CachePlanSelectionTest, EqualityIndexScan) {
    addIndex(BSON("x" << 1), "x_1");
    runQuery(BSON("x" << 5));

    assertPlanCacheRecoversSolution(BSON("x" << 5),
                                    "{fetch: {filter: null, node: {ixscan: {pattern: {x: 1}}}}}");
}

TEST_F(CachePlanSelectionTest, EqualityIndexScanWithTrailingFields) {
    addIndex(BSON("x" << 1 << "y" << 1), "x_1_y_1");
    runQuery(BSON("x" << 5));

    assertPlanCacheRecoversSolution(
        BSON("x" << 5), "{fetch: {filter: null, node: {ixscan: {pattern: {x: 1, y: 1}}}}}");
}

//
// Geo
//

TEST_F(CachePlanSelectionTest, Basic2DSphereNonNear) {
    addIndex(BSON("a"
                  << "2dsphere"),
             "a_2dsphere");
    BSONObj query;

    query = fromjson(
        "{a: {$geoIntersects: {$geometry: {type: 'Point',"
        "coordinates: [10.0, 10.0]}}}}");
    runQuery(query);
    assertPlanCacheRecoversSolution(query, "{fetch: {node: {ixscan: {pattern: {a: '2dsphere'}}}}}");

    query = fromjson("{a : { $geoWithin : { $centerSphere : [[ 10, 20 ], 0.01 ] } }}");
    runQuery(query);
    assertPlanCacheRecoversSolution(query, "{fetch: {node: {ixscan: {pattern: {a: '2dsphere'}}}}}");
}

TEST_F(CachePlanSelectionTest, Basic2DSphereGeoNear) {
    addIndex(BSON("a"
                  << "2dsphere"),
             "a_2dsphere");
    BSONObj query;

    query = fromjson("{a: {$nearSphere: [0,0], $maxDistance: 0.31 }}");
    runQuery(query);
    assertPlanCacheRecoversSolution(query,
                                    "{geoNear2dsphere: {pattern: {a: '2dsphere'}, "
                                    "bounds: {a: [['MinKey', 'MaxKey', true, true]]}}}");

    query = fromjson(
        "{a: {$geoNear: {$geometry: {type: 'Point', coordinates: [0,0]},"
        "$maxDistance:100}}}");
    runQuery(query);
    assertPlanCacheRecoversSolution(query,
                                    "{geoNear2dsphere: {pattern: {a: '2dsphere'}, "
                                    "bounds: {a: [['MinKey', 'MaxKey', true, true]]}}}");
}

TEST_F(CachePlanSelectionTest, Basic2DSphereGeoNearReverseCompound) {
    addIndex(BSON("x" << 1), "x_1");
    addIndex(BSON("x" << 1 << "a"
                      << "2dsphere"),
             "x_1_a_2dsphere");
    BSONObj query = fromjson("{x:1, a: {$nearSphere: [0,0], $maxDistance: 0.31 }}");
    runQuery(query);
    assertPlanCacheRecoversSolution(
        query,
        "{geoNear2dsphere: {pattern: {x: 1, a: '2dsphere'}, "
        "bounds: {x: [[1, 1, true, true]], a: [['MinKey', 'MaxKey', true, true]]}}}");
}

TEST_F(CachePlanSelectionTest, TwoDSphereNoGeoPred) {
    addIndex(BSON("x" << 1 << "a"
                      << "2dsphere"),
             "x_1_a_2dsphere");
    runQuery(BSON("x" << 1));
    assertPlanCacheRecoversSolution(BSON("x" << 1),
                                    "{fetch: {node: {ixscan: {pattern: {x: 1, a: '2dsphere'}}}}}");
}

TEST_F(CachePlanSelectionTest, Or2DSphereNonNear) {
    addIndex(BSON("a"
                  << "2dsphere"),
             "a_2dsphere");
    addIndex(BSON("b"
                  << "2dsphere"),
             "b_2dsphere");
    BSONObj query = fromjson(
        "{$or: [ {a: {$geoIntersects: {$geometry: {type: 'Point', coordinates: [10.0, 10.0]}}}},"
        " {b: {$geoWithin: { $centerSphere: [[ 10, 20 ], 0.01 ] } }} ]}");

    runQuery(query);
    assertPlanCacheRecoversSolution(
        query,
        "{or: {nodes: [{fetch: {node: {ixscan: {pattern: {a: '2dsphere'}}}}},"
        "{fetch: {node: {ixscan: {pattern: {b: '2dsphere'}}}}}]}}");
}

// Regression test for SERVER-24320. Tests that the PlanCacheIndexTree has the same sort order as
// the MatchExpression used to generate the plan cache key.
TEST_F(CachePlanSelectionTest, AndWithinPolygonWithinCenterSphere) {
    addIndex(BSON("a"
                  << "2dsphere"
                  << "b"
                  << 1),
             "a_2dsphere_b_2dsphere");

    BSONObj query = fromjson(
        "{$and: [{b: 1}, {a: {$within: {$polygon: [[0, 0], [0, 0], [0, 0], [0, 0]]}}}, {a: "
        "{$within: {$centerSphere: [[0, 0], 0]}}}]}");

    runQuery(query);
    assertPlanCacheRecoversSolution(query,
                                    "{fetch: {node: {ixscan: {pattern: {a: '2dsphere', b: 1}}}}}");
}

// $** index
TEST_F(CachePlanSelectionTest, WildcardIxScan) {
    params.indices.push_back(IndexEntry(BSON("$**" << 1),
                                        IndexType::INDEX_WILDCARD,
                                        false,  // multikey
                                        {},     // multikey paths
                                        {},     // multikeyPathSet
                                        true,   // sparse
                                        false,  // unique
                                        IndexEntry::Identifier{"anIndex"},
                                        nullptr,
                                        BSONObj(),
                                        nullptr));
    BSONObj query = fromjson("{a: 1, b: 1}");
    runQuery(query);

    const auto kPlanA =
        "{fetch: {node: {ixscan: "
        "{bounds: {$_path: [['a', 'a', true, true]], a: [[1, 1, true, true]]},"
        "pattern: {$_path: 1, a:1}}}}}";

    const auto kPlanB =
        "{fetch: {node: {ixscan: "
        "{bounds: {$_path: [['b', 'b', true, true]], b: [[1, 1, true, true]]},"
        "pattern: {$_path: 1, b:1}}}}}";

    assertPlanCacheRecoversSolution(query, kPlanA);
    assertPlanCacheRecoversSolution(query, kPlanB);

    // Query with fields in a different order, so that index entry expansion results in the list of
    // indexes being in a different order. Should still yield the same plans.
    BSONObj queryOtherDir = fromjson("{b: 1, a: 1}");
    assertPlanCacheRecoversSolution(query, kPlanA);
    assertPlanCacheRecoversSolution(query, kPlanB);
}

//
// tree operations
//

TEST_F(CachePlanSelectionTest, TwoPredicatesAnding) {
    addIndex(BSON("x" << 1), "x_1");
    BSONObj query = fromjson("{$and: [ {x: {$gt: 1}}, {x: {$lt: 3}} ] }");
    runQuery(query);
    assertPlanCacheRecoversSolution(
        query, "{fetch: {filter: null, node: {ixscan: {filter: null, pattern: {x: 1}}}}}");
}

TEST_F(CachePlanSelectionTest, SimpleOr) {
    addIndex(BSON("a" << 1), "a_1");
    BSONObj query = fromjson("{$or: [{a: 20}, {a: 21}]}");
    runQuery(query);
    assertPlanCacheRecoversSolution(
        query, "{fetch: {filter: null, node: {ixscan: {filter: null, pattern: {a:1}}}}}");
}

TEST_F(CachePlanSelectionTest, OrWithAndChild) {
    addIndex(BSON("a" << 1), "a_1");
    BSONObj query = fromjson("{$or: [{a: 20}, {$and: [{a:1}, {b:7}]}]}");
    runQuery(query);
    assertPlanCacheRecoversSolution(query,
                                    "{fetch: {filter: null, node: {or: {nodes: ["
                                    "{ixscan: {filter: null, pattern: {a: 1}}}, "
                                    "{fetch: {filter: {b: 7}, node: {ixscan: "
                                    "{filter: null, pattern: {a: 1}}}}}]}}}}");
}

TEST_F(CachePlanSelectionTest, AndWithUnindexedOrChild) {
    addIndex(BSON("a" << 1), "a_1");
    BSONObj query = fromjson("{a:20, $or: [{b:1}, {c:7}]}");
    runQuery(query);
    assertPlanCacheRecoversSolution(query,
                                    "{fetch: {filter: {$or: [{b: 1}, {c: 7}]}, node: "
                                    "{ixscan: {filter: null, pattern: {a: 1}}}}}");
}


TEST_F(CachePlanSelectionTest, AndWithOrWithOneIndex) {
    addIndex(BSON("b" << 1), "b_1");
    addIndex(BSON("a" << 1), "a_1");
    BSONObj query = fromjson("{$or: [{b:1}, {c:7}], a:20}");
    runQuery(query);
    assertPlanCacheRecoversSolution(query,
                                    "{fetch: {filter: {$or: [{b: 1}, {c: 7}]}, "
                                    "node: {ixscan: {filter: null, pattern: {a: 1}}}}}");
}

//
// Sort orders
//

// SERVER-1205.
TEST_F(CachePlanSelectionTest, MergeSort) {
    addIndex(BSON("a" << 1 << "c" << 1), "a_1_c_1");
    addIndex(BSON("b" << 1 << "c" << 1), "b_1_c_1");

    BSONObj query = fromjson("{$or: [{a:1}, {b:1}]}");
    BSONObj sort = BSON("c" << 1);
    runQuerySortProj(query, sort, BSONObj());

    assertPlanCacheRecoversSolution(
        query,
        sort,
        BSONObj(),
        BSONObj(),
        "{fetch: {node: {mergeSort: {nodes: "
        "[{ixscan: {pattern: {a: 1, c: 1}}}, {ixscan: {pattern: {b: 1, c: 1}}}]}}}}");
}

// SERVER-1205 as well.
TEST_F(CachePlanSelectionTest, NoMergeSortIfNoSortWanted) {
    addIndex(BSON("a" << 1 << "c" << 1), "a_1_c_1");
    addIndex(BSON("b" << 1 << "c" << 1), "b_1_c_1");

    BSONObj query = fromjson("{$or: [{a:1}, {b:1}]}");
    runQuerySortProj(query, BSONObj(), BSONObj());

    assertPlanCacheRecoversSolution(query,
                                    BSONObj(),
                                    BSONObj(),
                                    BSONObj(),
                                    "{fetch: {filter: null, node: {or: {nodes: ["
                                    "{ixscan: {filter: null, pattern: {a: 1, c: 1}}}, "
                                    "{ixscan: {filter: null, pattern: {b: 1, c: 1}}}]}}}}");
}

// Disabled: SERVER-10801.
/*
TEST_F(CachePlanSelectionTest, SortOnGeoQuery) {
    addIndex(BSON("timestamp" << -1 << "position" << "2dsphere"), "timestamp_-1_position_2dsphere");
    BSONObj query = fromjson("{position: {$geoWithin: {$geometry: {type: \"Polygon\", "
                             "coordinates: [[[1, 1], [1, 90], [180, 90], "
                             "[180, 1], [1, 1]]]}}}}");
    BSONObj sort = fromjson("{timestamp: -1}");
    runQuerySortProj(query, sort, BSONObj());

    assertPlanCacheRecoversSolution(query, sort, BSONObj(),
        "{fetch: {node: {ixscan: {pattern: {timestamp: -1, position: '2dsphere'}}}}}");
}
*/

// SERVER-9257
TEST_F(CachePlanSelectionTest, CompoundGeoNoGeoPredicate) {
    addIndex(BSON("creationDate" << 1 << "foo.bar"
                                 << "2dsphere"),
             "creationDate_1_foo.bar_2dsphere");
    BSONObj query = fromjson("{creationDate: {$gt: 7}}");
    BSONObj sort = fromjson("{creationDate: 1}");
    runQuerySortProj(query, sort, BSONObj());

    assertPlanCacheRecoversSolution(
        query,
        sort,
        BSONObj(),
        BSONObj(),
        "{fetch: {node: {ixscan: {pattern: {creationDate: 1, 'foo.bar': '2dsphere'}}}}}");
}

TEST_F(CachePlanSelectionTest, ReverseScanForSort) {
    addIndex(BSON("_id" << 1), "_id_1");
    runQuerySortProj(BSONObj(), fromjson("{_id: -1}"), BSONObj());
    assertPlanCacheRecoversSolution(
        BSONObj(),
        fromjson("{_id: -1}"),
        BSONObj(),
        BSONObj(),
        "{fetch: {filter: null, node: {ixscan: {filter: null, pattern: {_id: 1}}}}}");
}

//
// Caching collection scans.
//

TEST_F(CachePlanSelectionTest, CollscanNoUsefulIndices) {
    addIndex(BSON("a" << 1 << "b" << 1), "a_1_b_1");
    addIndex(BSON("c" << 1), "c_1");
    runQuery(BSON("b" << 4));
    assertPlanCacheRecoversSolution(BSON("b" << 4), "{cscan: {filter: {b: 4}, dir: 1}}");
}

TEST_F(CachePlanSelectionTest, CollscanOrWithoutEnoughIndices) {
    addIndex(BSON("a" << 1), "a_1");
    BSONObj query = fromjson("{$or: [{a: 20}, {b: 21}]}");
    runQuery(query);
    assertPlanCacheRecoversSolution(query, "{cscan: {filter: {$or:[{a:20},{b:21}]}, dir: 1}}");
}

TEST_F(CachePlanSelectionTest, CollscanMergeSort) {
    addIndex(BSON("a" << 1 << "c" << 1), "a_1_c_1");
    addIndex(BSON("b" << 1 << "c" << 1), "b_1_c_1");

    BSONObj query = fromjson("{$or: [{a:1}, {b:1}]}");
    BSONObj sort = BSON("c" << 1);
    runQuerySortProj(query, sort, BSONObj());

    assertPlanCacheRecoversSolution(query,
                                    sort,
                                    BSONObj(),
                                    BSONObj(),
                                    "{sort: {pattern: {c: 1}, limit: 0, node: {sortKeyGen: "
                                    "{node: {cscan: {dir: 1}}}}}}");
}

//
// Caching plans that use multikey indexes.
//

TEST_F(CachePlanSelectionTest, CachedPlanForCompoundMultikeyIndexCanCompoundBounds) {
    params.options = QueryPlannerParams::NO_TABLE_SCAN | QueryPlannerParams::INDEX_INTERSECTION;

    const bool multikey = true;
    addIndex(BSON("a" << 1 << "b" << 1), "a_1_b_1", multikey);

    BSONObj query = fromjson("{a: 2, b: 3}");
    runQuery(query);

    assertPlanCacheRecoversSolution(
        query,
        "{fetch: {filter: null, node: {ixscan: {pattern: {a: 1, b: 1}, "
        "bounds: {a: [[2, 2, true, true]], b: [[3, 3, true, true]]}}}}}");
}

TEST_F(CachePlanSelectionTest,
       CachedPlanForSelfIntersectionOfMultikeyIndexPointRangesCannotIntersectBounds) {
    params.options = QueryPlannerParams::NO_TABLE_SCAN | QueryPlannerParams::INDEX_INTERSECTION;

    const bool multikey = true;
    addIndex(BSON("a" << 1), "a_1", multikey);

    BSONObj query = fromjson("{$and: [{a: 2}, {a: 3}]}");
    runQuery(query);

    assertPlanCacheRecoversSolution(
        query,
        "{fetch: {filter: {$and: [{a: 2}, {a: 3}]}, node: {andSorted: {nodes: ["
        "{ixscan: {pattern: {a: 1}, bounds: {a: [[2, 2, true, true]]}}}, "
        "{ixscan: {pattern: {a: 1}, bounds: {a: [[3, 3, true, true]]}}}]}}}}");
}

TEST_F(CachePlanSelectionTest,
       CachedPlanForSelfIntersectionOfMultikeyIndexNonPointRangesCannotIntersectBounds) {
    // Enable a hash-based index intersection plan to be generated because we are scanning a
    // non-point range on the "a" field.
    bool oldEnableHashIntersection = internalQueryPlannerEnableHashIntersection.load();
    ON_BLOCK_EXIT([oldEnableHashIntersection] {
        internalQueryPlannerEnableHashIntersection.store(oldEnableHashIntersection);
    });
    internalQueryPlannerEnableHashIntersection.store(true);
    params.options = QueryPlannerParams::NO_TABLE_SCAN | QueryPlannerParams::INDEX_INTERSECTION;

    const bool multikey = true;
    addIndex(BSON("a" << 1), "a_1", multikey);

    BSONObj query = fromjson("{$and: [{a: {$gte: 2}}, {a: {$lt: 3}}]}");
    runQuery(query);

    assertPlanCacheRecoversSolution(
        query,
        "{fetch: {filter: {$and:[{a:{$gte:2}},{a:{$lt:3}}]}, node: {andHash: {nodes: ["
        "{ixscan: {pattern: {a: 1}, bounds: {a: [[2, Infinity, true, true]]}}}, "
        "{ixscan: {pattern: {a: 1}, bounds: {a: [[-Infinity, 3, true, false]]}}}]}}}}");
}


TEST_F(CachePlanSelectionTest, CachedPlanForIntersectionOfMultikeyIndexesWhenUsingElemMatch) {
    params.options = QueryPlannerParams::NO_TABLE_SCAN | QueryPlannerParams::INDEX_INTERSECTION;

    const bool multikey = true;
    addIndex(BSON("a.b" << 1), "a.b_1", multikey);
    addIndex(BSON("a.c" << 1), "a.c_1", multikey);

    BSONObj query = fromjson("{a: {$elemMatch: {b: 2, c: 3}}}");
    runQuery(query);

    assertPlanCacheRecoversSolution(
        query,
        "{fetch: {filter: {a: {$elemMatch: {b: 2, c: 3}}}, node: {andSorted: {nodes: ["
        "{ixscan: {pattern: {'a.b': 1}, bounds: {'a.b': [[2, 2, true, true]]}}},"
        "{ixscan: {pattern: {'a.c': 1}, bounds: {'a.c': [[3, 3, true, true]]}}}]}}}}");
}

TEST_F(CachePlanSelectionTest, CachedPlanForIntersectionWithNonMultikeyIndexCanIntersectBounds) {
    // Enable a hash-based index intersection plan to be generated because we are scanning a
    // non-point range on the "a.c" field.
    bool oldEnableHashIntersection = internalQueryPlannerEnableHashIntersection.load();
    ON_BLOCK_EXIT([oldEnableHashIntersection] {
        internalQueryPlannerEnableHashIntersection.store(oldEnableHashIntersection);
    });
    internalQueryPlannerEnableHashIntersection.store(true);
    params.options = QueryPlannerParams::NO_TABLE_SCAN | QueryPlannerParams::INDEX_INTERSECTION;

    const bool multikey = true;
    addIndex(BSON("a.b" << 1), "a.b_1", multikey);
    addIndex(BSON("a.c" << 1), "a.c_1", !multikey);

    BSONObj query = fromjson("{'a.b': 2, 'a.c': {$gte: 0, $lt: 10}}}}");
    runQuery(query);

    assertPlanCacheRecoversSolution(
        query,
        "{fetch: {node: {andHash: {nodes: ["
        "{ixscan: {pattern: {'a.b': 1}, bounds: {'a.b': [[2, 2, true, true]]}}},"
        "{ixscan: {pattern: {'a.c': 1}, bounds: {'a.c': [[0, 10, true, false]]}}}]}}}}");
}

//
// Check queries that, at least for now, are not cached.
//

TEST_F(CachePlanSelectionTest, GeoNear2DNotCached) {
    addIndex(BSON("a"
                  << "2d"),
             "a_2d");
    runQuery(fromjson("{a: {$near: [0,0], $maxDistance:0.3 }}"));
    assertNotCached("{geoNear2d: {a: '2d'}}");
}

TEST_F(CachePlanSelectionTest, MinNotCached) {
    addIndex(BSON("a" << 1), "a_1");
    runQueryHintMinMax(BSONObj(), BSONObj(), fromjson("{a: 1}"), BSONObj());
    assertNotCached(
        "{fetch: {filter: null, "
        "node: {ixscan: {filter: null, pattern: {a: 1}}}}}");
}

TEST_F(CachePlanSelectionTest, MaxNotCached) {
    addIndex(BSON("a" << 1), "a_1");
    runQueryHintMinMax(BSONObj(), BSONObj(), BSONObj(), fromjson("{a: 1}"));
    assertNotCached(
        "{fetch: {filter: null, "
        "node: {ixscan: {filter: null, pattern: {a: 1}}}}}");
}

TEST_F(CachePlanSelectionTest, NaturalHintNotCached) {
    addIndex(BSON("a" << 1), "a_1");
    addIndex(BSON("b" << 1), "b_1");
    runQuerySortHint(BSON("a" << 1), BSON("b" << 1), BSON("$natural" << 1));
    assertNotCached(
        "{sort: {pattern: {b: 1}, limit: 0, node: {sortKeyGen: {node: "
        "{cscan: {filter: {a: 1}, dir: 1}}}}}}");
}

TEST_F(CachePlanSelectionTest, HintValidNotCached) {
    addIndex(BSON("a" << 1), "a_1");
    runQueryHint(BSONObj(), fromjson("{a: 1}"));
    assertNotCached(
        "{fetch: {filter: null, "
        "node: {ixscan: {filter: null, pattern: {a: 1}}}}}");
}

//
// Queries using '2d' indices are not cached.
//

TEST_F(CachePlanSelectionTest, Basic2DNonNearNotCached) {
    addIndex(BSON("a"
                  << "2d"),
             "a_2d");
    BSONObj query;

    // Polygon
    query = fromjson("{a : { $within: { $polygon : [[0,0], [2,0], [4,0]] } }}");
    runQuery(query);
    assertNotCached("{fetch: {node: {ixscan: {pattern: {a: '2d'}}}}}");

    // Center
    query = fromjson("{a : { $within : { $center : [[ 5, 5 ], 7 ] } }}");
    runQuery(query);
    assertNotCached("{fetch: {node: {ixscan: {pattern: {a: '2d'}}}}}");

    // Centersphere
    query = fromjson("{a : { $within : { $centerSphere : [[ 10, 20 ], 0.01 ] } }}");
    runQuery(query);
    assertNotCached("{fetch: {node: {ixscan: {pattern: {a: '2d'}}}}}");

    // Within box.
    query = fromjson("{a : {$within: {$box : [[0,0],[9,9]]}}}");
    runQuery(query);
    assertNotCached("{fetch: {node: {ixscan: {pattern: {a: '2d'}}}}}");
}

TEST_F(CachePlanSelectionTest, Or2DNonNearNotCached) {
    addIndex(BSON("a"
                  << "2d"),
             "a_2d");
    addIndex(BSON("b"
                  << "2d"),
             "b_2d");
    BSONObj query = fromjson(
        "{$or: [ {a : { $within : { $polygon : [[0,0], [2,0], [4,0]] } }},"
        " {b : { $within : { $center : [[ 5, 5 ], 7 ] } }} ]}");

    runQuery(query);
    assertNotCached(
        "{or: {nodes: [{fetch: {node: {ixscan: {pattern: {a: '2d'}}}}},"
        "{fetch: {node: {ixscan: {pattern: {b: '2d'}}}}}]}}");
}

//
// Collation.
//

TEST_F(CachePlanSelectionTest, MatchingCollation) {
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
    addIndex(BSON("x" << 1), "x_1", &collator);
    runQueryAsCommand(fromjson(
        "{find: 'testns', filter: {x: 'foo'}, collation: {locale: 'mock_reverse_string'}}"));

    assertPlanCacheRecoversSolution(BSON("x"
                                         << "bar"),
                                    BSONObj(),
                                    BSONObj(),
                                    BSON("locale"
                                         << "mock_reverse_string"),
                                    "{fetch: {node: {ixscan: {pattern: {x: 1}}}}}");
}

TEST_F(CachePlanSelectionTest, ContainedOr) {
    addIndex(BSON("b" << 1 << "a" << 1), "b_1_a_1");
    addIndex(BSON("c" << 1 << "a" << 1), "c_1_a_1");
    BSONObj query = fromjson("{$and: [{a: 5}, {$or: [{b: 6}, {c: 7}]}]}");
    runQuery(query);
    assertPlanCacheRecoversSolution(
        query,
        "{fetch: {filter: null, node: {or: {nodes: ["
        "{ixscan: {pattern: {b: 1, a: 1}, bounds: {b: [[6, 6, true, true]], a: [[5, 5, true, "
        "true]]}}},"
        "{ixscan: {pattern: {c: 1, a: 1}, bounds: {c: [[7, 7, true, true]], a: [[5, 5, true, "
        "true]]}}}"
        "]}}}}");
}

TEST_F(CachePlanSelectionTest, ContainedOrAndIntersection) {
    bool oldEnableHashIntersection = internalQueryPlannerEnableHashIntersection.load();
    ON_BLOCK_EXIT([oldEnableHashIntersection] {
        internalQueryPlannerEnableHashIntersection.store(oldEnableHashIntersection);
    });
    internalQueryPlannerEnableHashIntersection.store(true);
    params.options = QueryPlannerParams::INCLUDE_COLLSCAN | QueryPlannerParams::INDEX_INTERSECTION;
    addIndex(BSON("a" << 1 << "b" << 1), "a_1_b_1");
    addIndex(BSON("c" << 1), "c_1");
    BSONObj query = fromjson("{$and: [{a: 5}, {$or: [{b: 6}, {c: 7}]}]}");
    runQuery(query);
    assertPlanCacheRecoversSolution(
        query,
        "{fetch: {filter: {$and:[{a:5},{$or:[{a:5,b:6},{c:7}]}]}, node: {andHash: {nodes: ["
        "{or: {nodes: ["
        "{ixscan: {pattern: {a: 1, b: 1}, bounds: {a: [[5, 5, true, true]], b: [[6, 6, true, "
        "true]]}}},"
        "{ixscan: {pattern: {c: 1}, bounds: {c: [[7, 7, true, true]]}}}]}},"
        "{ixscan: {pattern: {a: 1, b: 1}, bounds: {a: [[5, 5, true, true]], b: [['MinKey', "
        "'MaxKey', true, true]]}}}"
        "]}}}}");
}

/**
 * Test functions for computeKey.  Cache keys are intentionally obfuscated and are
 * meaningful only within the current lifetime of the server process. Users should treat plan
 * cache keys as opaque.
 */
void testComputeKey(const char* queryStr,
                    const char* sortStr,
                    const char* projStr,
                    const char* expectedStr) {
    PlanCache planCache;
    const char* collationStr = "{}";
    unique_ptr<CanonicalQuery> cq(canonicalize(queryStr, sortStr, projStr, collationStr));
    PlanCacheKey key = planCache.computeKey(*cq);
    PlanCacheKey expectedKey(expectedStr);
    if (key == expectedKey) {
        return;
    }
    str::stream ss;
    ss << "Unexpected plan cache key. Expected: " << expectedKey << ". Actual: " << key
       << ". Query: " << cq->toString();
    FAIL(ss);
}

TEST(PlanCacheTest, ComputeKey) {
    // Generated cache keys should be treated as opaque to the user.

    // No sorts
    testComputeKey("{}", "{}", "{}", "an");
    testComputeKey("{$or: [{a: 1}, {b: 2}]}", "{}", "{}", "or[eqa,eqb]");
    testComputeKey("{$or: [{a: 1}, {b: 1}, {c: 1}], d: 1}", "{}", "{}", "an[or[eqa,eqb,eqc],eqd]");
    testComputeKey("{$or: [{a: 1}, {b: 1}], c: 1, d: 1}", "{}", "{}", "an[or[eqa,eqb],eqc,eqd]");
    testComputeKey("{a: 1, b: 1, c: 1}", "{}", "{}", "an[eqa,eqb,eqc]");
    testComputeKey("{a: 1, beqc: 1}", "{}", "{}", "an[eqa,eqbeqc]");
    testComputeKey("{ap1a: 1}", "{}", "{}", "eqap1a");
    testComputeKey("{aab: 1}", "{}", "{}", "eqaab");

    // With sort
    testComputeKey("{}", "{a: 1}", "{}", "an~aa");
    testComputeKey("{}", "{a: -1}", "{}", "an~da");
    testComputeKey("{}",
                   "{a: {$meta: 'textScore'}}",
                   "{a: {$meta: 'textScore'}}",
                   "an~ta|{ $meta: \"textScore\" }a");
    testComputeKey("{a: 1}", "{b: 1}", "{}", "eqa~ab");

    // With projection
    testComputeKey("{}", "{}", "{a: 1}", "an|ia");
    testComputeKey("{}", "{}", "{a: -1}", "an|ia");
    testComputeKey("{}", "{}", "{a: -1.0}", "an|ia");
    testComputeKey("{}", "{}", "{a: true}", "an|ia");
    testComputeKey("{}", "{}", "{a: 0}", "an|ea");
    testComputeKey("{}", "{}", "{a: false}", "an|ea");
    testComputeKey("{}", "{}", "{a: 99}", "an|ia");
    testComputeKey("{}", "{}", "{a: 'foo'}", "an|ia");
    testComputeKey("{}", "{}", "{a: {$slice: [3, 5]}}", "an|{ $slice: \\[ 3\\, 5 \\] }a");
    testComputeKey("{}", "{}", "{a: {$elemMatch: {x: 2}}}", "an|{ $elemMatch: { x: 2 } }a");
    testComputeKey("{}", "{}", "{a: ObjectId('507f191e810c19729de860ea')}", "an|ia");
    testComputeKey("{a: 1}", "{}", "{'a.$': 1}", "eqa|ia.$");
    testComputeKey("{a: 1}", "{}", "{a: 1}", "eqa|ia");

    // Projection should be order-insensitive
    testComputeKey("{}", "{}", "{a: 1, b: 1}", "an|iaib");
    testComputeKey("{}", "{}", "{b: 1, a: 1}", "an|iaib");

    // With or-elimination and projection
    testComputeKey("{$or: [{a: 1}]}", "{}", "{_id: 0, a: 1}", "eqa|e_idia");
    testComputeKey("{$or: [{a: 1}]}", "{}", "{'a.$': 1}", "eqa|ia.$");
}

// Delimiters found in user field names or non-standard projection field values
// must be escaped.
TEST(PlanCacheTest, ComputeKeyEscaped) {
    // Field name in query.
    testComputeKey("{'a,[]~|<>': 1}", "{}", "{}", "eqa\\,\\[\\]\\~\\|\\<\\>");

    // Field name in sort.
    testComputeKey("{}", "{'a,[]~|<>': 1}", "{}", "an~aa\\,\\[\\]\\~\\|\\<\\>");

    // Field name in projection.
    testComputeKey("{}", "{}", "{'a,[]~|<>': 1}", "an|ia\\,\\[\\]\\~\\|\\<\\>");

    // Value in projection.
    testComputeKey("{}", "{}", "{a: 'foo,[]~|<>'}", "an|ia");
}

// Cache keys for $geoWithin queries with legacy and GeoJSON coordinates should
// not be the same.
TEST(PlanCacheTest, ComputeKeyGeoWithin) {
    PlanCache planCache;

    // Legacy coordinates.
    unique_ptr<CanonicalQuery> cqLegacy(
        canonicalize("{a: {$geoWithin: "
                     "{$box: [[-180, -90], [180, 90]]}}}"));
    // GeoJSON coordinates.
    unique_ptr<CanonicalQuery> cqNew(
        canonicalize("{a: {$geoWithin: "
                     "{$geometry: {type: 'Polygon', coordinates: "
                     "[[[0, 0], [0, 90], [90, 0], [0, 0]]]}}}}"));
    ASSERT_NOT_EQUALS(planCache.computeKey(*cqLegacy), planCache.computeKey(*cqNew));
}

// GEO_NEAR cache keys should include information on geometry and CRS in addition
// to the match type and field name.
TEST(PlanCacheTest, ComputeKeyGeoNear) {
    testComputeKey("{a: {$near: [0,0], $maxDistance:0.3 }}", "{}", "{}", "gnanrfl");
    testComputeKey("{a: {$nearSphere: [0,0], $maxDistance: 0.31 }}", "{}", "{}", "gnanssp");
    testComputeKey(
        "{a: {$geoNear: {$geometry: {type: 'Point', coordinates: [0,0]},"
        "$maxDistance:100}}}",
        "{}",
        "{}",
        "gnanrsp");
}

TEST(PlanCacheTest, ComputeKeyRegexDependsOnFlags) {
    testComputeKey("{a: {$regex: \"sometext\"}}", "{}", "{}", "rea");
    testComputeKey("{a: {$regex: \"sometext\", $options: \"\"}}", "{}", "{}", "rea");

    testComputeKey("{a: {$regex: \"sometext\", $options: \"s\"}}", "{}", "{}", "reas");
    testComputeKey("{a: {$regex: \"sometext\", $options: \"ms\"}}", "{}", "{}", "reams");

    // Test that the ordering of $options doesn't matter.
    testComputeKey("{a: {$regex: \"sometext\", $options: \"im\"}}", "{}", "{}", "reaim");
    testComputeKey("{a: {$regex: \"sometext\", $options: \"mi\"}}", "{}", "{}", "reaim");

    // Test that only the options affect the key. Two regex match expressions with the same options
    // but different $regex values should have the same shape.
    testComputeKey("{a: {$regex: \"abc\", $options: \"mi\"}}", "{}", "{}", "reaim");
    testComputeKey("{a: {$regex: \"efg\", $options: \"mi\"}}", "{}", "{}", "reaim");

    testComputeKey("{a: {$regex: \"\", $options: \"ms\"}}", "{}", "{}", "reams");
    testComputeKey("{a: {$regex: \"___\", $options: \"ms\"}}", "{}", "{}", "reams");
}

// When a sparse index is present, computeKey() should generate different keys depending on
// whether or not the predicates in the given query can use the index.
TEST(PlanCacheTest, ComputeKeySparseIndex) {
    PlanCache planCache;
    planCache.notifyOfIndexEntries({IndexEntry(BSON("a" << 1),
                                               false,                       // multikey
                                               true,                        // sparse
                                               false,                       // unique
                                               IndexEntry::Identifier{""},  // name
                                               nullptr,                     // filterExpr
                                               BSONObj())});

    unique_ptr<CanonicalQuery> cqEqNumber(canonicalize("{a: 0}}"));
    unique_ptr<CanonicalQuery> cqEqString(canonicalize("{a: 'x'}}"));
    unique_ptr<CanonicalQuery> cqEqNull(canonicalize("{a: null}}"));

    // 'cqEqNumber' and 'cqEqString' get the same key, since both are compatible with this
    // index.
    ASSERT_EQ(planCache.computeKey(*cqEqNumber), planCache.computeKey(*cqEqString));

    // 'cqEqNull' gets a different key, since it is not compatible with this index.
    ASSERT_NOT_EQUALS(planCache.computeKey(*cqEqNull), planCache.computeKey(*cqEqNumber));
}

// When a partial index is present, computeKey() should generate different keys depending on
// whether or not the predicates in the given query "match" the predicates in the partial index
// filter.
TEST(PlanCacheTest, ComputeKeyPartialIndex) {
    BSONObj filterObj = BSON("f" << BSON("$gt" << 0));
    unique_ptr<MatchExpression> filterExpr(parseMatchExpression(filterObj));

    PlanCache planCache;
    planCache.notifyOfIndexEntries({IndexEntry(BSON("a" << 1),
                                               false,                       // multikey
                                               false,                       // sparse
                                               false,                       // unique
                                               IndexEntry::Identifier{""},  // name
                                               filterExpr.get(),
                                               BSONObj())});

    unique_ptr<CanonicalQuery> cqGtNegativeFive(canonicalize("{f: {$gt: -5}}"));
    unique_ptr<CanonicalQuery> cqGtZero(canonicalize("{f: {$gt: 0}}"));
    unique_ptr<CanonicalQuery> cqGtFive(canonicalize("{f: {$gt: 5}}"));

    // 'cqGtZero' and 'cqGtFive' get the same key, since both are compatible with this index.
    ASSERT_EQ(planCache.computeKey(*cqGtZero), planCache.computeKey(*cqGtFive));

    // 'cqGtNegativeFive' gets a different key, since it is not compatible with this index.
    ASSERT_NOT_EQUALS(planCache.computeKey(*cqGtNegativeFive), planCache.computeKey(*cqGtZero));
}

// Query shapes should get the same plan cache key if they have the same collation indexability.
TEST(PlanCacheTest, ComputeKeyCollationIndex) {
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);

    PlanCache planCache;
    IndexEntry entry(BSON("a" << 1),
                     false,                       // multikey
                     false,                       // sparse
                     false,                       // unique
                     IndexEntry::Identifier{""},  // name
                     nullptr,                     // filterExpr
                     BSONObj());
    entry.collator = &collator;
    planCache.notifyOfIndexEntries({entry});

    unique_ptr<CanonicalQuery> containsString(canonicalize("{a: 'abc'}"));
    unique_ptr<CanonicalQuery> containsObject(canonicalize("{a: {b: 'abc'}}"));
    unique_ptr<CanonicalQuery> containsArray(canonicalize("{a: ['abc', 'xyz']}"));
    unique_ptr<CanonicalQuery> noStrings(canonicalize("{a: 5}"));
    unique_ptr<CanonicalQuery> containsStringHasCollation(
        canonicalize("{a: 'abc'}", "{}", "{}", "{locale: 'mock_reverse_string'}"));

    // 'containsString', 'containsObject', and 'containsArray' have the same key, since none are
    // compatible with the index.
    ASSERT_EQ(planCache.computeKey(*containsString), planCache.computeKey(*containsObject));
    ASSERT_EQ(planCache.computeKey(*containsString), planCache.computeKey(*containsArray));

    // 'noStrings' gets a different key since it is compatible with the index.
    ASSERT_NOT_EQUALS(planCache.computeKey(*containsString), planCache.computeKey(*noStrings));

    // 'noStrings' and 'containsStringHasCollation' get the same key since they compatible with the
    // index.
    ASSERT_EQ(planCache.computeKey(*noStrings), planCache.computeKey(*containsStringHasCollation));

    unique_ptr<CanonicalQuery> inContainsString(canonicalize("{a: {$in: [1, 'abc', 2]}}"));
    unique_ptr<CanonicalQuery> inContainsObject(canonicalize("{a: {$in: [1, {b: 'abc'}, 2]}}"));
    unique_ptr<CanonicalQuery> inContainsArray(canonicalize("{a: {$in: [1, ['abc', 'xyz'], 2]}}"));
    unique_ptr<CanonicalQuery> inNoStrings(canonicalize("{a: {$in: [1, 2]}}"));
    unique_ptr<CanonicalQuery> inContainsStringHasCollation(
        canonicalize("{a: {$in: [1, 'abc', 2]}}", "{}", "{}", "{locale: 'mock_reverse_string'}"));

    // 'inContainsString', 'inContainsObject', and 'inContainsArray' have the same key, since none
    // are compatible with the index.
    ASSERT_EQ(planCache.computeKey(*inContainsString), planCache.computeKey(*inContainsObject));
    ASSERT_EQ(planCache.computeKey(*inContainsString), planCache.computeKey(*inContainsArray));

    // 'inNoStrings' gets a different key since it is compatible with the index.
    ASSERT_NOT_EQUALS(planCache.computeKey(*inContainsString), planCache.computeKey(*inNoStrings));

    // 'inNoStrings' and 'inContainsStringHasCollation' get the same key since they compatible with
    // the index.
    ASSERT_EQ(planCache.computeKey(*inNoStrings),
              planCache.computeKey(*inContainsStringHasCollation));
}

TEST(PlanCacheTest, ComputeKeyWildcardIndex) {
    PlanCache planCache;
    IndexEntry entry(BSON("a.$**" << 1),
                     false,                       // multikey
                     false,                       // sparse
                     false,                       // unique
                     IndexEntry::Identifier{""},  // name
                     nullptr,                     // filterExpr
                     BSONObj());
    planCache.notifyOfIndexEntries({entry});

    // Used to check that two queries have the same shape when no indexes are present.
    PlanCache planCacheWithNoIndexes;

    // Compatible with index.
    unique_ptr<CanonicalQuery> usesPathWithScalar(canonicalize("{a: 'abcdef'}"));
    unique_ptr<CanonicalQuery> usesPathWithEmptyArray(canonicalize("{a: []}"));

    // Not compatible with index.
    unique_ptr<CanonicalQuery> usesPathWithObject(canonicalize("{a: {b: 'abc'}}"));
    unique_ptr<CanonicalQuery> usesPathWithArray(canonicalize("{a: [1, 2]}"));
    unique_ptr<CanonicalQuery> usesPathWithArrayContainingObject(canonicalize("{a: [1, {b: 1}]}"));
    unique_ptr<CanonicalQuery> usesPathWithEmptyObject(canonicalize("{a: {}}"));
    unique_ptr<CanonicalQuery> doesNotUsePath(canonicalize("{b: 1234}"));

    // Check that the queries which are compatible with the index have the same key.
    ASSERT_EQ(planCache.computeKey(*usesPathWithScalar),
              planCache.computeKey(*usesPathWithEmptyArray));

    // Check that the queries which have the same path as the index, but aren't supported, have
    // different keys.
    ASSERT_EQ(planCacheWithNoIndexes.computeKey(*usesPathWithScalar),
              planCacheWithNoIndexes.computeKey(*usesPathWithObject));
    ASSERT_NE(planCache.computeKey(*usesPathWithScalar), planCache.computeKey(*usesPathWithObject));

    ASSERT_EQ(planCache.computeKey(*usesPathWithObject), planCache.computeKey(*usesPathWithArray));
    ASSERT_EQ(planCache.computeKey(*usesPathWithObject),
              planCache.computeKey(*usesPathWithArrayContainingObject));
    ASSERT_EQ(planCache.computeKey(*usesPathWithObject),
              planCache.computeKey(*usesPathWithEmptyObject));

    // The query on 'b' should have a completely different plan cache key (both with and without a
    // wildcard index).
    ASSERT_NE(planCacheWithNoIndexes.computeKey(*usesPathWithScalar),
              planCacheWithNoIndexes.computeKey(*doesNotUsePath));
    ASSERT_NE(planCache.computeKey(*usesPathWithScalar), planCache.computeKey(*doesNotUsePath));
    ASSERT_NE(planCacheWithNoIndexes.computeKey(*usesPathWithObject),
              planCacheWithNoIndexes.computeKey(*doesNotUsePath));
    ASSERT_NE(planCache.computeKey(*usesPathWithObject), planCache.computeKey(*doesNotUsePath));

    // More complex queries with similar shapes. This is to ensure that plan cache key encoding
    // correctly traverses the expression tree.
    auto orQueryAllowed = canonicalize("{$or: [{a: 3}, {a: {$gt: [1,2]}}]}");
    // Same shape except 'a' is compared to an object.
    auto orQueryNotAllowed = canonicalize("{$or: [{a: {someobject: 1}}, {a: {$gt: [1,2]}}]}");
    // The two queries should have the same shape when no indexes are present, but different shapes
    // when a $** index is present.
    ASSERT_EQ(planCacheWithNoIndexes.computeKey(*orQueryAllowed),
              planCacheWithNoIndexes.computeKey(*orQueryNotAllowed));
    ASSERT_NE(planCache.computeKey(*orQueryAllowed), planCache.computeKey(*orQueryNotAllowed));
}

}  // namespace
