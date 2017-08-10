/**
 *    Copyright 2017 (C) MongoDB Inc.
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

#include "mongo/db/repl/sync_tail_test_fixture.h"

#include "mongo/db/catalog/document_validation.h"
#include "mongo/db/curop.h"
#include "mongo/db/repl/drop_pending_collection_reaper.h"
#include "mongo/db/repl/replication_consistency_markers_mock.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/repl/replication_process.h"
#include "mongo/db/repl/replication_recovery_mock.h"
#include "mongo/db/repl/storage_interface_mock.h"

namespace mongo {
namespace repl {

void SyncTailTest::setUp() {
    ServiceContextMongoDTest::setUp();

    auto service = getServiceContext();
    ReplicationCoordinator::set(service, stdx::make_unique<ReplicationCoordinatorMock>(service));
    auto storageInterface = stdx::make_unique<StorageInterfaceMock>();
    _storageInterface = storageInterface.get();
    storageInterface->insertDocumentsFn =
        [](OperationContext*, const NamespaceString&, const std::vector<InsertStatement>&) {
            return Status::OK();
        };
    StorageInterface::set(service, std::move(storageInterface));
    DropPendingCollectionReaper::set(
        service, stdx::make_unique<DropPendingCollectionReaper>(_storageInterface));

    _replicationProcess =
        new ReplicationProcess(_storageInterface,
                               stdx::make_unique<ReplicationConsistencyMarkersMock>(),
                               stdx::make_unique<ReplicationRecoveryMock>());
    ReplicationProcess::set(cc().getServiceContext(),
                            std::unique_ptr<ReplicationProcess>(_replicationProcess));


    _opCtx = cc().makeOperationContext();
    _opsApplied = 0;
    _applyOp = [](OperationContext* opCtx,
                  Database* db,
                  const BSONObj& op,
                  bool inSteadyStateReplication,
                  stdx::function<void()>) { return Status::OK(); };
    _applyCmd = [](OperationContext* opCtx, const BSONObj& op, bool) { return Status::OK(); };
    _incOps = [this]() { _opsApplied++; };
}

void SyncTailTest::tearDown() {
    auto service = getServiceContext();
    _opCtx.reset();
    ReplicationProcess::set(service, {});
    DropPendingCollectionReaper::set(service, {});
    StorageInterface::set(service, {});
    ServiceContextMongoDTest::tearDown();
}

void SyncTailTest::_testSyncApplyInsertDocument(ErrorCodes::Error expectedError,
                                                const BSONObj* explicitOp) {
    const BSONObj op = explicitOp ? *explicitOp : BSON("op"
                                                       << "i"
                                                       << "ns"
                                                       << "test.t");
    bool applyOpCalled = false;
    SyncTail::ApplyOperationInLockFn applyOp = [&](OperationContext* opCtx,
                                                   Database* db,
                                                   const BSONObj& theOperation,
                                                   bool inSteadyStateReplication,
                                                   stdx::function<void()>) {
        applyOpCalled = true;
        ASSERT_TRUE(opCtx);
        ASSERT_TRUE(opCtx->lockState()->isDbLockedForMode("test", MODE_IX));
        ASSERT_FALSE(opCtx->lockState()->isDbLockedForMode("test", MODE_X));
        ASSERT_TRUE(opCtx->lockState()->isCollectionLockedForMode("test.t", MODE_IX));
        ASSERT_FALSE(opCtx->writesAreReplicated());
        ASSERT_TRUE(documentValidationDisabled(opCtx));
        ASSERT_TRUE(db);
        ASSERT_BSONOBJ_EQ(op, theOperation);
        ASSERT_TRUE(inSteadyStateReplication);
        return Status::OK();
    };
    ASSERT_TRUE(_opCtx->writesAreReplicated());
    ASSERT_FALSE(documentValidationDisabled(_opCtx.get()));
    ASSERT_EQ(SyncTail::syncApply(_opCtx.get(), op, true, applyOp, failedApplyCommand, _incOps),
              expectedError);
    ASSERT_EQ(applyOpCalled, expectedError == ErrorCodes::OK);
}

Status failedApplyCommand(OperationContext* opCtx, const BSONObj& theOperation, bool) {
    FAIL("applyCommand unexpectedly invoked.");
    return Status::OK();
}

}  // namespace repl
}  // namespace mongo
