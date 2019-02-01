
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

#include "mongo/db/catalog/collection.h"
#include "mongo/db/exec/plan_stage.h"
#include "mongo/db/query/canonical_query.h"
#include "mongo/db/record_id.h"

namespace mongo {

class IndexAccessMethod;
class RecordCursor;

/**
 * A standalone stage implementing the fast path for key-value retrievals via the _id index. Since
 * the _id index always has the collection default collation, the IDHackStage can only be used when
 * the query's collation is equal to the collection default.
 */
class IDHackStage final : public PlanStage {
public:
    /** Takes ownership of all the arguments -collection. */
    IDHackStage(OperationContext* opCtx,
                const Collection* collection,
                CanonicalQuery* query,
                WorkingSet* ws,
                const IndexDescriptor* descriptor);

    IDHackStage(OperationContext* opCtx,
                Collection* collection,
                const BSONObj& key,
                WorkingSet* ws,
                const IndexDescriptor* descriptor);

    ~IDHackStage();

    bool isEOF() final;
    StageState doWork(WorkingSetID* out) final;

    void doSaveState() final;
    void doRestoreState() final;
    void doDetachFromOperationContext() final;
    void doReattachToOperationContext() final;

    /**
     * ID Hack has a very strict criteria for the queries it supports.
     */
    static bool supportsQuery(Collection* collection, const CanonicalQuery& query);

    StageType stageType() const final {
        return STAGE_IDHACK;
    }

    std::unique_ptr<PlanStageStats> getStats();

    const SpecificStats* getSpecificStats() const final;

    static const char* kStageType;

private:
    /**
     * Marks this stage as done, optionally adds key metadata, and returns PlanStage::ADVANCED.
     *
     * Called whenever we have a WSM containing the matching obj.
     */
    StageState advance(WorkingSetID id, WorkingSetMember* member, WorkingSetID* out);

    // Not owned here.
    const Collection* _collection;

    std::unique_ptr<SeekableRecordCursor> _recordCursor;

    // The WorkingSet we annotate with results.  Not owned by us.
    WorkingSet* _workingSet;

    // Not owned here.
    const IndexAccessMethod* _accessMethod;

    // The value to match against the _id field.
    BSONObj _key;

    // Have we returned our one document?
    bool _done;

    // Do we need to add index key metadata for returnKey?
    bool _addKeyMetadata;

    IDHackStats _specificStats;
};

}  // namespace mongo
