/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include <boost/optional.hpp>
#include <string>
#include <vector>

#include "mongo/db/logical_session_id.h"
#include "mongo/db/ops/write_ops.h"
#include "mongo/executor/task_executor_pool.h"

namespace mongo {

class BSONObj;
class BSONObjBuilder;
class NamespaceString;
class OperationContext;
class ShardRegistry;
template <typename T>
class StatusWith;
class TaskExecutor;

/**
 * Set of functions used to update a document's shard key.
 */
namespace documentShardKeyUpdateUtil {

/**
 * Coordinating method and external point of entry for updating a document's shard key. This method
 * creates the necessary extra operations. It will then run each operation using the ClusterWriter.
 * If any statement throws, an exception will leave this method, and must be handled by external
 * callers.
 *
 * This is the only method that should be called outside of this class.
 */
void updateShardKeyForDocument(OperationContext* opCtx,
                               const NamespaceString& nss,
                               const WouldChangeOwningShardInfo& documentKeyChangeInfo,
                               int stmtId);

/**
 * Creates the BSONObj that will be used to delete the pre-image document. Will also attach
 * necessary generic transaction and passthrough field transaction information.
 *
 * This method should not be called outside of this class. It is only temporarily exposed for
 * intermediary test coverage.
 */
BSONObj constructShardKeyDeleteCmdObj(const NamespaceString& nss,
                                      const BSONObj& originalQueryPredicate,
                                      const BSONObj& updatePostImage,
                                      int stmtId);

/*
 * Creates the BSONObj that will be used to insert the new document with the post-update image.
 * Will attach all necessary generic transaction and passthrough field transaction information.
 *
 * This method should not be called outside of this class. It is only temporarily exposed for
 * intermediary test coverage.
 */
BSONObj constructShardKeyInsertCmdObj(const NamespaceString& nss,
                                      const BSONObj& updatePostImage,
                                      int stmtId);
}  // namespace documentShardKeyUpdateUtil
}  // namespace mongo
