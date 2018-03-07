/**
 *    Copyright (C) 2016 MongoDB Inc.
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

#include "mongo/db/repl/data_replicator_external_state.h"

namespace mongo {
namespace repl {

class ReplicationCoordinator;
class ReplicationCoordinatorExternalState;

/**
 * Data replicator external state implementation using a replication coordinator.
 */

class DataReplicatorExternalStateImpl : public DataReplicatorExternalState {
public:
    DataReplicatorExternalStateImpl(
        ReplicationCoordinator* replicationCoordinator,
        ReplicationCoordinatorExternalState* replicationCoordinatorExternalState);

    executor::TaskExecutor* getTaskExecutor() const override;

    ThreadPool* getDbWorkThreadPool() const override;

    OpTimeWithTerm getCurrentTermAndLastCommittedOpTime() override;

    void processMetadata(const rpc::ReplSetMetadata& replMetadata,
                         boost::optional<rpc::OplogQueryMetadata> oqMetadata) override;

    bool shouldStopFetching(const HostAndPort& source,
                            const rpc::ReplSetMetadata& replMetadata,
                            boost::optional<rpc::OplogQueryMetadata> oqMetadata) override;

    std::unique_ptr<OplogBuffer> makeInitialSyncOplogBuffer(OperationContext* opCtx) const override;

    std::unique_ptr<OplogBuffer> makeSteadyStateOplogBuffer(OperationContext* opCtx) const override;

    StatusWith<ReplSetConfig> getCurrentConfig() const override;

private:
    StatusWith<OpTime> _multiApply(OperationContext* opCtx,
                                   MultiApplier::Operations ops,
                                   MultiApplier::ApplyOperationFn applyOperation) override;

    Status _multiInitialSyncApply(MultiApplier::OperationPtrs* ops,
                                  const HostAndPort& source,
                                  AtomicUInt32* fetchCount,
                                  WorkerMultikeyPathInfo* workerMultikeyPathInfo) override;

protected:
    ReplicationCoordinator* getReplicationCoordinator() const;
    ReplicationCoordinatorExternalState* getReplicationCoordinatorExternalState() const;

private:
    // Not owned by us.
    ReplicationCoordinator* _replicationCoordinator;

    // Not owned by us.
    ReplicationCoordinatorExternalState* _replicationCoordinatorExternalState;
};


}  // namespace repl
}  // namespace mongo
