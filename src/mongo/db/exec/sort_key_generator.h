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

#include <memory>

#include "mongo/bson/bsonobj.h"
#include "mongo/db/exec/plan_stage.h"
#include "mongo/db/index/btree_key_generator.h"
#include "mongo/db/query/index_bounds.h"
#include "mongo/db/query/stage_types.h"

namespace mongo {

class Collection;
class WorkingSetMember;

/**
 * Maps a WSM value to a BSONObj key that can then be sorted via BSONObjCmp.
 */
class SortKeyGenerator {
public:
    /**
     * 'sortSpec' is the BSONObj in the .sort(...) clause.
     *
     * 'queryObj' is the BSONObj in the .find(...) clause.  For multikey arrays we have to
     * ensure that the value we select to sort by is within bounds generated by
     * executing 'queryObj' using the virtual index with key pattern 'sortSpec'.
     *
     * 'txn' must point to a valid OperationContext, but 'txn' does not need to outlive the
     * constructed SortKeyGenerator.
     */
    SortKeyGenerator(OperationContext* txn, const BSONObj& sortSpec, const BSONObj& queryObj);

    /**
     * Returns the key used to sort 'member'. If the member is in LOC_AND_IDX state, it must not
     * contain a $meta textScore in its sort spec, and this function will use the index key data
     * stored in 'member' to extract the sort key. Otherwise, if the member is in LOC_AND_OBJ or
     * OWNED_OBJ state, this function will use the object data stored in 'member' to extract the
     * sort key.
     */
    Status getSortKey(const WorkingSetMember& member, BSONObj* objOut) const;

private:
    StatusWith<BSONObj> getSortKeyFromIndexKey(const WorkingSetMember& member) const;
    StatusWith<BSONObj> getSortKeyFromObject(const WorkingSetMember& member) const;

    /**
     * In order to emulate the existing sort behavior we must make unindexed sort behavior as
     * consistent as possible with indexed sort behavior.  As such, we must only consider index
     * keys that we would encounter if we were answering the query using the sort-providing
     * index.
     *
     * Populates _hasBounds and _bounds.
     */
    void getBoundsForSort(OperationContext* txn, const BSONObj& queryObj, const BSONObj& sortObj);

    // The raw object in .sort()
    BSONObj _rawSortSpec;

    // The sort pattern with any non-Btree sort pulled out.
    BSONObj _btreeObj;

    // If we're not sorting with a $meta value we can short-cut some work.
    bool _sortHasMeta;

    // True if the bounds are valid.
    bool _hasBounds;

    // The bounds generated from the query we're sorting.
    IndexBounds _bounds;

    // Helper to extract sorting keys from documents.
    std::unique_ptr<BtreeKeyGenerator> _keyGen;

    // Helper to filter keys, ensuring keys generated with _keyGen are within _bounds.
    std::unique_ptr<IndexBoundsChecker> _boundsChecker;
};

/**
 * Passes results from the child through after adding the sort key for each result as
 * WorkingSetMember computed data.
 */
class SortKeyGeneratorStage final : public PlanStage {
public:
    SortKeyGeneratorStage(OperationContext* opCtx,
                          PlanStage* child,
                          WorkingSet* ws,
                          const BSONObj& sortSpecObj,
                          const BSONObj& queryObj);

    bool isEOF() final;

    StageState doWork(WorkingSetID* out) final;

    StageType stageType() const final {
        return STAGE_SORT_KEY_GENERATOR;
    }

    std::unique_ptr<PlanStageStats> getStats() final;

    const SpecificStats* getSpecificStats() const final;

    static const char* kStageType;

private:
    WorkingSet* const _ws;

    // The raw sort pattern as expressed by the user.
    const BSONObj _sortSpec;

    // The raw query as expressed by the user.
    const BSONObj _query;

    std::unique_ptr<SortKeyGenerator> _sortKeyGen;
};

}  // namespace mongo
