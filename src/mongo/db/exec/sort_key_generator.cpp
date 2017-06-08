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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kQuery

#include "mongo/platform/basic.h"

#include "mongo/db/exec/sort_key_generator.h"

#include <vector>

#include "mongo/bson/bsonobj_comparator.h"
#include "mongo/db/catalog/collection.h"
#include "mongo/db/exec/scoped_timer.h"
#include "mongo/db/exec/working_set.h"
#include "mongo/db/exec/working_set_common.h"
#include "mongo/db/exec/working_set_computed_data.h"
#include "mongo/db/matcher/extensions_callback_noop.h"
#include "mongo/db/query/collation/collator_interface.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/log.h"

namespace mongo {

//
// SortKeyGenerator
//

SortKeyGenerator::SortKeyGenerator(OperationContext* opCtx,
                                   const BSONObj& sortSpec,
                                   const CollatorInterface* collator)
    : _collator(collator), _rawSortSpec(sortSpec) {
    // 'sortSpec' can be a mix of $meta and index key expressions.  We pick it apart so that
    // we only generate Btree keys for the index key expressions.

    // The Btree key fields go in here.  We pass this fake index key pattern to the Btree
    // key generator below as part of generating sort keys for the docs.
    BSONObjBuilder btreeBob;

    for (auto&& elt : sortSpec) {
        if (elt.isNumber()) {
            btreeBob.append(elt);
        } else {
            // If this field of the sort pattern is non-numeric, we expect it to be a text-score
            // meta sort. This is validated upstream.
            invariant(QueryRequest::isTextScoreMeta(elt));
            _sortHasMeta = true;
        }
    }

    // The fake index key pattern used to generate Btree keys.
    _btreeObj = btreeBob.obj();

    // If we're just sorting by meta, don't bother with all the key stuff.
    if (_btreeObj.isEmpty()) {
        return;
    }

    // We'll need to treat arrays as if we were to create an index over them. that is,
    // we may need to unnest the first level and consider each array element to decide
    // the sort order.
    std::vector<const char*> fieldNames;
    std::vector<BSONElement> fixed;
    for (auto&& patternElt : _btreeObj) {
        fieldNames.push_back(patternElt.fieldName());
        fixed.push_back(BSONElement());
    }

    constexpr bool isSparse = false;
    _keyGen = stdx::make_unique<BtreeKeyGeneratorV1>(fieldNames, fixed, isSparse, _collator);
}

Status SortKeyGenerator::getSortKey(const WorkingSetMember& member, BSONObj* objOut) const {
    StatusWith<BSONObj> sortKey = BSONObj();

    if (member.hasObj()) {
        sortKey = getSortKeyFromObject(member);
    } else {
        sortKey = getSortKeyFromIndexKey(member);
    }
    if (!sortKey.isOK()) {
        return sortKey.getStatus();
    }

    if (!_sortHasMeta) {
        *objOut = sortKey.getValue();
        return Status::OK();
    }

    BSONObjBuilder mergedKeyBob;

    // Merge metadata into the key.
    BSONObjIterator it(_rawSortSpec);
    BSONObjIterator sortKeyIt(sortKey.getValue());
    while (it.more()) {
        BSONElement elt = it.next();
        if (elt.isNumber()) {
            // Merge btree key elt.
            mergedKeyBob.append(sortKeyIt.next());
        } else if (QueryRequest::isTextScoreMeta(elt)) {
            // Add text score metadata
            double score = 0.0;
            if (member.hasComputed(WSM_COMPUTED_TEXT_SCORE)) {
                const TextScoreComputedData* scoreData = static_cast<const TextScoreComputedData*>(
                    member.getComputed(WSM_COMPUTED_TEXT_SCORE));
                score = scoreData->getScore();
            }
            mergedKeyBob.append("$metaTextScore", score);
        }
    }

    *objOut = mergedKeyBob.obj();
    return Status::OK();
}

StatusWith<BSONObj> SortKeyGenerator::getSortKeyFromIndexKey(const WorkingSetMember& member) const {
    invariant(member.getState() == WorkingSetMember::RID_AND_IDX);
    invariant(!_sortHasMeta);

    BSONObjBuilder sortKeyObj;
    for (BSONElement specElt : _rawSortSpec) {
        invariant(specElt.isNumber());
        BSONElement sortKeyElt;
        invariant(member.getFieldDotted(specElt.fieldName(), &sortKeyElt));
        sortKeyObj.appendAs(sortKeyElt, "");
    }

    return sortKeyObj.obj();
}

StatusWith<BSONObj> SortKeyGenerator::getSortKeyFromObject(const WorkingSetMember& member) const {
    // Not sorting by anything in the key, just bail out early.
    if (_btreeObj.isEmpty()) {
        return BSONObj();
    }

    // We will sort '_data' in the same order an index over '_pattern' would have.  This is
    // tricky.  Consider the sort pattern {a:1} and the document {a:[1, 10]}. We have
    // potentially two keys we could use to sort on. Here we extract these keys.
    const StringData::ComparatorInterface* stringComparator = nullptr;
    BSONObjComparator patternCmp(
        _btreeObj, BSONObjComparator::FieldNamesMode::kConsider, stringComparator);
    BSONObjSet keys = patternCmp.makeBSONObjSet();

    try {
        // There's no need to compute the prefixes of the indexed fields that cause the index to be
        // multikey when getting the index keys for sorting.
        MultikeyPaths* multikeyPaths = nullptr;
        _keyGen->getKeys(member.obj.value(), &keys, multikeyPaths);
    } catch (const UserException& e) {
        // Probably a parallel array.
        if (ErrorCodes::CannotIndexParallelArrays == e.getCode()) {
            return Status(ErrorCodes::BadValue, "cannot sort with keys that are parallel arrays");
        } else {
            return e.toStatus();
        }
    } catch (...) {
        return Status(ErrorCodes::InternalError, "unknown error during sort key generation");
    }

    // Key generator isn't sparse so we should at least get an all-null key.
    invariant(!keys.empty());

    // The sort key is the first index key, ordered according to the pattern '_btreeObj'.
    return *keys.begin();
}

//
// SortKeyGeneratorStage
//

const char* SortKeyGeneratorStage::kStageType = "SORT_KEY_GENERATOR";

SortKeyGeneratorStage::SortKeyGeneratorStage(OperationContext* opCtx,
                                             PlanStage* child,
                                             WorkingSet* ws,
                                             const BSONObj& sortSpecObj,
                                             const CollatorInterface* collator)
    : PlanStage(kStageType, opCtx), _ws(ws), _sortSpec(sortSpecObj), _collator(collator) {
    _children.emplace_back(child);
}

bool SortKeyGeneratorStage::isEOF() {
    return child()->isEOF();
}

PlanStage::StageState SortKeyGeneratorStage::doWork(WorkingSetID* out) {
    if (!_sortKeyGen) {
        _sortKeyGen = stdx::make_unique<SortKeyGenerator>(getOpCtx(), _sortSpec, _collator);
        return PlanStage::NEED_TIME;
    }

    auto stageState = child()->work(out);
    if (stageState == PlanStage::ADVANCED) {
        WorkingSetMember* member = _ws->get(*out);

        BSONObj sortKey;
        Status sortKeyStatus = _sortKeyGen->getSortKey(*member, &sortKey);
        if (!sortKeyStatus.isOK()) {
            *out = WorkingSetCommon::allocateStatusMember(_ws, sortKeyStatus);
            return PlanStage::FAILURE;
        }

        // Add the sort key to the WSM as computed data.
        member->addComputed(new SortKeyComputedData(sortKey));

        return PlanStage::ADVANCED;
    }

    if (stageState == PlanStage::IS_EOF) {
        _commonStats.isEOF = true;
    }

    return stageState;
}

std::unique_ptr<PlanStageStats> SortKeyGeneratorStage::getStats() {
    auto ret = stdx::make_unique<PlanStageStats>(_commonStats, STAGE_SORT_KEY_GENERATOR);
    ret->children.emplace_back(child()->getStats());
    return ret;
}

const SpecificStats* SortKeyGeneratorStage::getSpecificStats() const {
    // No specific stats are tracked for the sort key generation stage.
    return nullptr;
}

}  // namespace mongo
