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

#include "mongo/platform/basic.h"

#include "mongo/db/ops/parsed_update.h"
#include "mongo/db/ops/update_request.h"
#include "mongo/db/query/canonical_query.h"
#include "mongo/db/query/query_planner_common.h"

namespace mongo {

ParsedUpdate::ParsedUpdate(OperationContext* txn, const UpdateRequest* request)
    : _txn(txn), _request(request), _driver(UpdateDriver::Options()), _canonicalQuery() {}

Status ParsedUpdate::parseRequest() {
    // It is invalid to request that the UpdateStage return the prior or newly-updated version
    // of a document during a multi-update.
    invariant(!(_request->shouldReturnAnyDocs() && _request->isMulti()));

    // It is invalid to request that a ProjectionStage be applied to the UpdateStage if the
    // UpdateStage would not return any document.
    invariant(_request->getProj().isEmpty() || _request->shouldReturnAnyDocs());

    // We parse the update portion before the query portion because the dispostion of the update
    // may determine whether or not we need to produce a CanonicalQuery at all.  For example, if
    // the update involves the positional-dollar operator, we must have a CanonicalQuery even if
    // it isn't required for query execution.
    Status status = parseUpdate();
    if (!status.isOK())
        return status;
    status = parseQuery();
    if (!status.isOK())
        return status;
    return Status::OK();
}

Status ParsedUpdate::parseQuery() {
    dassert(!_canonicalQuery.get());

    if (!_driver.needMatchDetails() && CanonicalQuery::isSimpleIdQuery(_request->getQuery())) {
        return Status::OK();
    }

    return parseQueryToCQ();
}

Status ParsedUpdate::parseQueryToCQ() {
    dassert(!_canonicalQuery.get());

    const WhereCallbackReal whereCallback(_txn, _request->getNamespaceString().db());

    // Limit should only used for the findAndModify command when a sort is specified. If a sort
    // is requested, we want to use a top-k sort for efficiency reasons, so should pass the
    // limit through. Generally, a update stage expects to be able to skip documents that were
    // deleted/modified under it, but a limit could inhibit that and give an EOF when the update
    // has not actually updated a document. This behavior is fine for findAndModify, but should
    // not apply to update in general.
    long long limit = (!_request->isMulti() && !_request->getSort().isEmpty()) ? -1 : 0;

    // The projection needs to be applied after the update operation, so we specify an empty
    // BSONObj as the projection during canonicalization.
    const BSONObj emptyObj;
    auto statusWithCQ = CanonicalQuery::canonicalize(_request->getNamespaceString().ns(),
                                                     _request->getQuery(),
                                                     _request->getSort(),
                                                     emptyObj,  // projection
                                                     0,         // skip
                                                     limit,
                                                     emptyObj,  // hint
                                                     emptyObj,  // min
                                                     emptyObj,  // max
                                                     false,     // snapshot
                                                     _request->isExplain(),
                                                     whereCallback);
    if (statusWithCQ.isOK()) {
        _canonicalQuery = std::move(statusWithCQ.getValue());
    }

    return statusWithCQ.getStatus();
}

Status ParsedUpdate::parseUpdate() {
    const NamespaceString& ns(_request->getNamespaceString());

    // Should the modifiers validate their embedded docs via okForStorage
    // Only user updates should be checked. Any system or replication stuff should pass through.
    // Config db docs shouldn't get checked for valid field names since the shard key can have
    // a dot (".") in it.
    const bool shouldValidate =
        !(!_txn->writesAreReplicated() || ns.isConfigDB() || _request->isFromMigration());

    _driver.setLogOp(true);
    _driver.setModOptions(ModifierInterface::Options(!_txn->writesAreReplicated(), shouldValidate));

    return _driver.parse(_request->getUpdates(), _request->isMulti());
}

bool ParsedUpdate::canYield() const {
    return !_request->isGod() && PlanExecutor::YIELD_AUTO == _request->getYieldPolicy() &&
        !isIsolated();
}

bool ParsedUpdate::isIsolated() const {
    return _canonicalQuery.get()
        ? QueryPlannerCommon::hasNode(_canonicalQuery->root(), MatchExpression::ATOMIC)
        : LiteParsedQuery::isQueryIsolated(_request->getQuery());
}

bool ParsedUpdate::hasParsedQuery() const {
    return _canonicalQuery.get() != NULL;
}

std::unique_ptr<CanonicalQuery> ParsedUpdate::releaseParsedQuery() {
    invariant(_canonicalQuery.get() != NULL);
    return std::move(_canonicalQuery);
}

const UpdateRequest* ParsedUpdate::getRequest() const {
    return _request;
}

UpdateDriver* ParsedUpdate::getDriver() {
    return &_driver;
}

}  // namespace mongo
