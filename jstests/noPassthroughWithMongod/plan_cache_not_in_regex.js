/**
 * Tests that a $not-$in-$regex query, which cannot be supported by an index, cannot incorrectly
 * hijack the cached plan for an earlier $not-$in query.
 */
(function() {
    "use strict";

    load('jstests/libs/analyze_plan.js');  // For isCollScan.

    const coll = db.plan_cache_not_in_regex;
    coll.drop();

    // Helper function which obtains the cached plan, if any, for a given query shape.
    function getPlanForCacheEntry(query, proj, sort) {
        const cacheDetails =
            coll.getPlanCache().getPlansByQuery({query: query, sort: sort, projection: proj});
        const plans = cacheDetails.filter(plan => plan.feedback.nfeedback >= 0);
        assert.eq(plans.length, 1, `Expected one cached plan, found: ${tojson(plans)}`);
        return plans.shift();
    }

    // Insert a document containing a field 'a', and create two indexes that can support queries on
    // this field. This is to ensure that the plan we choose will be cached, since if only a single
    // index is available, the solution will not be cached.
    assert.writeOK(coll.insert({a: "foo"}));
    assert.commandWorked(coll.createIndex({a: 1}));
    assert.commandWorked(coll.createIndex({a: 1, b: 1}));

    // Repeat the test for query, query with projection, and query with projection and sort.
    for (let [proj, sort] of[[{}, {}], [{_id: 0, a: 1}, {}], [{_id: 0, a: 1}, {a: 1}]]) {
        // Perform a plain $not-$in query on 'a' and confirm that the plan is cached.
        const queryShape = {a: {$not: {$in: [32, 33]}}};
        assert.eq(1, coll.find(queryShape, proj).sort(sort).itcount());
        let cacheEntry = getPlanForCacheEntry(queryShape, proj, sort);
        assert(cacheEntry);

        // If the cached plan is inactive, perform the same query to activate it.
        if (cacheEntry.isActive === false) {
            assert.eq(1, coll.find(queryShape, proj).sort(sort).itcount());
            cacheEntry = getPlanForCacheEntry(queryShape, proj, sort);
            assert(cacheEntry);
            assert(cacheEntry.isActive);
        }

        // Now perform a $not-$in-$regex query, confirm that it obtains the correct results, and
        // that it used a COLLSCAN rather than planning from the cache.
        const explainOutput = assert.commandWorked(
            coll.find({a: {$not: {$in: [34, /bar/]}}}).explain("executionStats"));
        assert(isCollscan(explainOutput.queryPlanner.winningPlan));
        assert.eq(1, explainOutput.executionStats.nReturned);

        // Flush the plan cache before the next iteration.
        coll.getPlanCache().clear();
    }
})();