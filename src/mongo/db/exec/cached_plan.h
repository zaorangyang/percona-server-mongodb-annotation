
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

#pragma once

#include <memory>
#include <queue>

#include "mongo/db/exec/plan_stage.h"
#include "mongo/db/exec/working_set.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/query/canonical_query.h"
#include "mongo/db/query/query_planner_params.h"
#include "mongo/db/query/query_solution.h"
#include "mongo/db/record_id.h"

namespace mongo {

class PlanYieldPolicy;

/**
 * This stage outputs its mainChild, and possibly its backup child
 * and also updates the cache.
 *
 * Preconditions: Valid RecordId.
 *
 */
class CachedPlanStage final : public PlanStage {
public:
    CachedPlanStage(OperationContext* opCtx,
                    Collection* collection,
                    WorkingSet* ws,
                    CanonicalQuery* cq,
                    const QueryPlannerParams& params,
                    size_t decisionWorks,
                    PlanStage* root);

    bool isEOF() final;

    StageState doWork(WorkingSetID* out) final;

    StageType stageType() const final {
        return STAGE_CACHED_PLAN;
    }

    std::unique_ptr<PlanStageStats> getStats() final;

    const SpecificStats* getSpecificStats() const final;

    static const char* kStageType;

    /**
     * Runs the cached plan for a trial period, yielding during the trial period according to
     * 'yieldPolicy'.
     *
     * Feedback from the trial period is passed to the plan cache. If the performance is lower
     * than expected, the old plan is evicted and a new plan is selected from scratch (again
     * yielding according to 'yieldPolicy'). Otherwise, the cached plan is run.
     */
    Status pickBestPlan(PlanYieldPolicy* yieldPolicy);

private:
    /**
     * Passes stats from the trial period run of the cached plan to the plan cache.
     *
     * If the plan cache entry is deleted before we get a chance to update it, then this
     * is a no-op.
     */
    void updatePlanCache();

    /**
     * Uses the QueryPlanner and the MultiPlanStage to re-generate candidate plans for this
     * query and select a new winner.
     *
     * We fallback to a new plan if updatePlanCache() tells us that the performance was worse
     * than anticipated during the trial period.
     *
     * We only modify the plan cache if 'shouldCache' is true.
     */
    Status replan(PlanYieldPolicy* yieldPolicy, bool shouldCache);

    /**
     * May yield during the cached plan stage's trial period or replanning phases.
     *
     * Returns a non-OK status if query planning fails. In particular, this function returns
     * ErrorCodes::QueryPlanKilled if the query plan was killed during a yield, or
     * ErrorCodes::MaxTimeMSExpired if the operation exceeded its time limit.
     */
    Status tryYield(PlanYieldPolicy* yieldPolicy);

    // Not owned. Must be non-null.
    Collection* _collection;

    // Not owned.
    WorkingSet* _ws;

    // Not owned.
    CanonicalQuery* _canonicalQuery;

    QueryPlannerParams _plannerParams;

    // The number of work cycles taken to decide on a winning plan when the plan was first
    // cached.
    size_t _decisionWorks;

    // If we fall back to re-planning the query, and there is just one resulting query solution,
    // that solution is owned here.
    std::unique_ptr<QuerySolution> _replannedQs;

    // Any results produced during trial period execution are kept here.
    std::queue<WorkingSetID> _results;

    // Stats
    CachedPlanStats _specificStats;
};

}  // namespace mongo
