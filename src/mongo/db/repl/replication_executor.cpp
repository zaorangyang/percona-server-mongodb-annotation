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

#include "mongo/db/repl/replication_executor.h"

#include <limits>
#include <thread>

#include "mongo/db/repl/database_task.h"
#include "mongo/db/repl/storage_interface.h"
#include "mongo/executor/network_interface.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {
namespace repl {

namespace {
    stdx::function<void ()> makeNoExcept(const stdx::function<void ()> &fn);
}  // namespace

    using executor::NetworkInterface;

    ReplicationExecutor::ReplicationExecutor(NetworkInterface* netInterface,
                                             StorageInterface* storageInterface,
                                             int64_t prngSeed) :
        _random(prngSeed),
        _networkInterface(netInterface),
        _storageInterface(storageInterface),
        _totalEventWaiters(0),
        _inShutdown(false),
        _dblockWorkers(threadpool::ThreadPool::DoNotStartThreadsTag(),
                       3,
                       "replExecDBWorker-"),
        _dblockTaskRunner(
            &_dblockWorkers,
            stdx::bind(&StorageInterface::createOperationContext, storageInterface)),
        _dblockExclusiveLockTaskRunner(
            &_dblockWorkers,
            stdx::bind(&StorageInterface::createOperationContext, storageInterface)),
        _nextId(0) {
    }

    ReplicationExecutor::~ReplicationExecutor() {}

    std::string ReplicationExecutor::getDiagnosticString() {
        boost::lock_guard<boost::mutex> lk(_mutex);
        return _getDiagnosticString_inlock();
    }

    std::string ReplicationExecutor::_getDiagnosticString_inlock() const {
        str::stream output;
        output << "ReplicationExecutor";
        output << " networkInProgress:" << _networkInProgressQueue.size();
        output << " dbWorkInProgress:" << _dbWorkInProgressQueue.size();
        output << " exclusiveInProgress:" << _exclusiveLockInProgressQueue.size();
        output << " sleeperQueue:" << _sleepersQueue.size();
        output << " ready:" << _readyQueue.size();
        output << " free:" << _freeQueue.size();
        output << " unsignaledEvents:" << _unsignaledEvents.size();
        output << " eventWaiters:" << _totalEventWaiters;
        output << " shuttingDown:" << _inShutdown;
        output << " networkInterface:" << _networkInterface->getDiagnosticString();
        return output;
    }

    Date_t ReplicationExecutor::now() {
        return _networkInterface->now();
    }

    void ReplicationExecutor::run() {
        setThreadName("ReplicationExecutor");
        _networkInterface->startup();
        _dblockWorkers.startThreads();
        std::pair<WorkItem, CallbackHandle> work;
        while ((work = getWork()).first.callback.isValid()) {
            {
                boost::lock_guard<boost::mutex> lk(_terribleExLockSyncMutex);
                const Callback* callback = _getCallbackFromHandle(work.first.callback);
                const Status inStatus = callback->_isCanceled ?
                    Status(ErrorCodes::CallbackCanceled, "Callback canceled") :
                    Status::OK();
                makeNoExcept(stdx::bind(callback->_callbackFn,
                                        CallbackArgs(this, work.second, inStatus)))();
            }
            signalEvent(work.first.finishedEvent);
        }
        finishShutdown();
        _networkInterface->shutdown();
    }

    void ReplicationExecutor::shutdown() {
        // Correct shutdown needs to:
        // * Disable future work queueing.
        // * drain all of the unsignaled events, sleepers, and ready queue, by running those
        //   callbacks with a "shutdown" or "canceled" status.
        // * Signal all threads blocked in waitForEvent, and wait for them to return from that method.
        boost::lock_guard<boost::mutex> lk(_mutex);
        _inShutdown = true;

        _readyQueue.splice(_readyQueue.end(), _dbWorkInProgressQueue);
        _readyQueue.splice(_readyQueue.end(), _exclusiveLockInProgressQueue);
        _readyQueue.splice(_readyQueue.end(), _networkInProgressQueue);
        _readyQueue.splice(_readyQueue.end(), _sleepersQueue);
        for (auto event : _unsignaledEvents) {
            _readyQueue.splice(_readyQueue.end(), _getEventFromHandle(event)->_waiters);
        }
        for (auto readyWork : _readyQueue) {
            _getCallbackFromHandle(readyWork.callback)->_isCanceled = true;
        }
        _networkInterface->signalWorkAvailable();
    }

    void ReplicationExecutor::finishShutdown() {
        _dblockExclusiveLockTaskRunner.cancel();
        _dblockTaskRunner.cancel();
        _dblockWorkers.join();
        boost::unique_lock<boost::mutex> lk(_mutex);
        invariant(_inShutdown);
        invariant(_dbWorkInProgressQueue.empty());
        invariant(_exclusiveLockInProgressQueue.empty());
        invariant(_readyQueue.empty());
        invariant(_sleepersQueue.empty());

        while (!_unsignaledEvents.empty()) {
            EventList::iterator eventIter = _unsignaledEvents.begin();
            invariant(_getEventFromHandle(*eventIter)->_waiters.empty());
            signalEvent_inlock(*eventIter);
        }

        while (_totalEventWaiters > 0)
            _noMoreWaitingThreads.wait(lk);

        invariant(_dbWorkInProgressQueue.empty());
        invariant(_exclusiveLockInProgressQueue.empty());
        invariant(_readyQueue.empty());
        invariant(_sleepersQueue.empty());
        invariant(_unsignaledEvents.empty());
    }

    void ReplicationExecutor::maybeNotifyShutdownComplete_inlock() {
        if (_totalEventWaiters == 0)
            _noMoreWaitingThreads.notify_all();
    }

    StatusWith<ReplicationExecutor::EventHandle> ReplicationExecutor::makeEvent() {
        boost::lock_guard<boost::mutex> lk(_mutex);
        return makeEvent_inlock();
    }

    StatusWith<ReplicationExecutor::EventHandle> ReplicationExecutor::makeEvent_inlock() {
        if (_inShutdown)
            return StatusWith<EventHandle>(ErrorCodes::ShutdownInProgress, "Shutdown in progress");

        _unsignaledEvents.emplace_back();
        auto event = std::make_shared<Event>(this, --_unsignaledEvents.end());
        setEventForHandle(&_unsignaledEvents.back(), std::move(event));
        return _unsignaledEvents.back();
    }

    void ReplicationExecutor::signalEvent(const EventHandle& eventHandle) {
        boost::lock_guard<boost::mutex> lk(_mutex);
        signalEvent_inlock(eventHandle);
    }

    void ReplicationExecutor::signalEvent_inlock(const EventHandle& eventHandle) {
        Event* event = _getEventFromHandle(eventHandle);
        event->_signal_inlock();
        _unsignaledEvents.erase(event->_iter);
    }

    void ReplicationExecutor::waitForEvent(const EventHandle& event) {
        _getEventFromHandle(event)->waitUntilSignaled();
    }

    void ReplicationExecutor::cancel(const CallbackHandle& cbHandle) {
        _getCallbackFromHandle(cbHandle)->cancel();
    };

    void ReplicationExecutor::wait(const CallbackHandle& cbHandle) {
        _getCallbackFromHandle(cbHandle)->waitForCompletion();
    };

    StatusWith<ReplicationExecutor::CallbackHandle> ReplicationExecutor::onEvent(
            const EventHandle& eventHandle,
            const CallbackFn& work) {
        boost::lock_guard<boost::mutex> lk(_mutex);
        WorkQueue* queue = &_readyQueue;
        Event* event = _getEventFromHandle(eventHandle);
        if (!event->_isSignaled) {
            queue = &event->_waiters;
        }
        else {
            queue = &_readyQueue;
        }
        return enqueueWork_inlock(queue, work);
    }

    static void remoteCommandFinished(
            const ReplicationExecutor::CallbackArgs& cbData,
            const ReplicationExecutor::RemoteCommandCallbackFn& cb,
            const RemoteCommandRequest& request,
            const ResponseStatus& response) {

        if (cbData.status.isOK()) {
            cb(ReplicationExecutor::RemoteCommandCallbackArgs(
                       cbData.executor, cbData.myHandle, request, response));
        }
        else {
            cb(ReplicationExecutor::RemoteCommandCallbackArgs(
                       cbData.executor,
                       cbData.myHandle,
                       request,
                       ResponseStatus(cbData.status)));
        }
    }

    static void remoteCommandFailedEarly(
            const ReplicationExecutor::CallbackArgs& cbData,
            const ReplicationExecutor::RemoteCommandCallbackFn& cb,
            const RemoteCommandRequest& request) {

        invariant(!cbData.status.isOK());
        cb(ReplicationExecutor::RemoteCommandCallbackArgs(
                   cbData.executor,
                   cbData.myHandle,
                   request,
                   ResponseStatus(cbData.status)));
    }

    void ReplicationExecutor::_finishRemoteCommand(
            const RemoteCommandRequest& request,
            const ResponseStatus& response,
            const CallbackHandle& cbHandle,
            const uint64_t expectedHandleGeneration,
            const RemoteCommandCallbackFn& cb) {

        Callback* callback = _getCallbackFromHandle(cbHandle);
        const WorkQueue::iterator iter = callback->_iter;
        boost::lock_guard<boost::mutex> lk(_mutex);
        if (_inShutdown) {
            return;
        }
        if (expectedHandleGeneration != iter->generation) {
            return;
        }
        callback->_callbackFn = stdx::bind(remoteCommandFinished,
                                           stdx::placeholders::_1,
                                           cb,
                                           request,
                                           response);
        _readyQueue.splice(_readyQueue.end(), _networkInProgressQueue, iter);
    }

    StatusWith<ReplicationExecutor::CallbackHandle> ReplicationExecutor::scheduleRemoteCommand(
            const RemoteCommandRequest& request,
            const RemoteCommandCallbackFn& cb) {
        RemoteCommandRequest scheduledRequest = request;
        if (request.timeout == RemoteCommandRequest::kNoTimeout) {
            scheduledRequest.expirationDate = RemoteCommandRequest::kNoExpirationDate;
        }
        else {
            scheduledRequest.expirationDate = _networkInterface->now() + scheduledRequest.timeout;
        }
        boost::lock_guard<boost::mutex> lk(_mutex);
        StatusWith<CallbackHandle> handle = enqueueWork_inlock(
                &_networkInProgressQueue,
                stdx::bind(remoteCommandFailedEarly,
                           stdx::placeholders::_1,
                           cb,
                           scheduledRequest));
        if (handle.isOK()) {
            _getCallbackFromHandle(handle.getValue())->_iter->isNetworkOperation = true;
            _networkInterface->startCommand(
                    handle.getValue(),
                    scheduledRequest,
                    stdx::bind(&ReplicationExecutor::_finishRemoteCommand,
                               this,
                               scheduledRequest,
                               stdx::placeholders::_1,
                               handle.getValue(),
                               _getCallbackFromHandle(handle.getValue())->_iter->generation,
                               cb));
        }
        return handle;
    }

    StatusWith<ReplicationExecutor::CallbackHandle> ReplicationExecutor::scheduleWork(
            const CallbackFn& work) {
        boost::lock_guard<boost::mutex> lk(_mutex);
        _networkInterface->signalWorkAvailable();
        return enqueueWork_inlock(&_readyQueue, work);
    }

    StatusWith<ReplicationExecutor::CallbackHandle> ReplicationExecutor::scheduleWorkAt(
            Date_t when,
            const CallbackFn& work) {

        boost::lock_guard<boost::mutex> lk(_mutex);
        WorkQueue temp;
        StatusWith<CallbackHandle> cbHandle = enqueueWork_inlock(&temp, work);
        if (!cbHandle.isOK())
            return cbHandle;
        _getCallbackFromHandle(cbHandle.getValue())->_iter->readyDate = when;
        WorkQueue::iterator insertBefore = _sleepersQueue.begin();
        while (insertBefore != _sleepersQueue.end() && insertBefore->readyDate <= when)
            ++insertBefore;
        _sleepersQueue.splice(insertBefore, temp, temp.begin());
        return cbHandle;
    }

    StatusWith<ReplicationExecutor::CallbackHandle>
    ReplicationExecutor::scheduleDBWork(const CallbackFn& work) {
        return scheduleDBWork(work, NamespaceString(), MODE_NONE);
    }

    StatusWith<ReplicationExecutor::CallbackHandle>
    ReplicationExecutor::scheduleDBWork(const CallbackFn& work,
                                        const NamespaceString& nss,
                                        LockMode mode) {

        boost::lock_guard<boost::mutex> lk(_mutex);
        StatusWith<CallbackHandle> handle = enqueueWork_inlock(&_dbWorkInProgressQueue,
                                                               work);
        if (handle.isOK()) {
            auto doOp = stdx::bind(
                    &ReplicationExecutor::_doOperation,
                    this,
                    stdx::placeholders::_1,
                    stdx::placeholders::_2,
                    handle.getValue(),
                    &_dbWorkInProgressQueue,
                    nullptr);
            auto task = [doOp](OperationContext* txn, const Status& status) {
                makeNoExcept(stdx::bind(doOp, txn, status))();
                return TaskRunner::NextAction::kDisposeOperationContext;
            };
            if (mode == MODE_NONE && nss.ns().empty()) {
                _dblockTaskRunner.schedule(task);
            }
            else {
                _dblockTaskRunner.schedule(DatabaseTask::makeCollectionLockTask(task, nss, mode));
            }
        }
        return handle;
    }

    void ReplicationExecutor::_doOperation(OperationContext* txn,
                                           const Status& taskRunnerStatus,
                                           const CallbackHandle& cbHandle,
                                           WorkQueue* workQueue,
                                           boost::mutex* terribleExLockSyncMutex) {
        boost::unique_lock<boost::mutex> lk(_mutex);
        if (_inShutdown)
            return;
        Callback* callback = _getCallbackFromHandle(cbHandle);
        const WorkQueue::iterator iter = callback->_iter;
        iter->callback = CallbackHandle();
        _freeQueue.splice(_freeQueue.begin(), *workQueue, iter);
        lk.unlock();
        {
            std::unique_ptr<boost::lock_guard<boost::mutex> > terribleLock(
                terribleExLockSyncMutex ?
                new boost::lock_guard<boost::mutex>(*terribleExLockSyncMutex) :
                nullptr);
            // Only possible task runner error status is CallbackCanceled.
            callback->_callbackFn(CallbackArgs(this,
                                               cbHandle,
                                               (callback->_isCanceled || !taskRunnerStatus.isOK() ?
                                                       Status(ErrorCodes::CallbackCanceled,
                                                              "Callback canceled") :
                                                       Status::OK()),
                                       txn));
        }
        lk.lock();
        signalEvent_inlock(callback->_finishedEvent);
    }

    StatusWith<ReplicationExecutor::CallbackHandle>
    ReplicationExecutor::scheduleWorkWithGlobalExclusiveLock(
            const CallbackFn& work) {

        boost::lock_guard<boost::mutex> lk(_mutex);
        StatusWith<CallbackHandle> handle = enqueueWork_inlock(&_exclusiveLockInProgressQueue,
                                                               work);
        if (handle.isOK()) {
            auto doOp = stdx::bind(
                    &ReplicationExecutor::_doOperation,
                    this,
                    stdx::placeholders::_1,
                    stdx::placeholders::_2,
                    handle.getValue(),
                    &_exclusiveLockInProgressQueue,
                    &_terribleExLockSyncMutex);
            _dblockExclusiveLockTaskRunner.schedule(
                DatabaseTask::makeGlobalExclusiveLockTask(
                    [doOp](OperationContext* txn, const Status& status) {
                makeNoExcept(stdx::bind(doOp, txn, status))();
                return TaskRunner::NextAction::kDisposeOperationContext;
            }));
        }
        return handle;
    }

    std::pair<ReplicationExecutor::WorkItem, ReplicationExecutor::CallbackHandle>
    ReplicationExecutor::getWork() {
        boost::unique_lock<boost::mutex> lk(_mutex);
        while (true) {
            const Date_t now = _networkInterface->now();
            Date_t nextWakeupDate = scheduleReadySleepers_inlock(now);
            if (!_readyQueue.empty()) {
                break;
            }
            else if (_inShutdown) {
                return std::make_pair(WorkItem(), CallbackHandle());
            }
            lk.unlock();
            if (nextWakeupDate == Date_t::max()) {
                _networkInterface->waitForWork();
            }
            else {
                _networkInterface->waitForWorkUntil(nextWakeupDate);
            }
            lk.lock();
        }
        const WorkItem work = *_readyQueue.begin();
        const CallbackHandle cbHandle = work.callback;
        _readyQueue.begin()->callback = CallbackHandle();
        _freeQueue.splice(_freeQueue.begin(), _readyQueue, _readyQueue.begin());
        return std::make_pair(work, cbHandle);
    }

    int64_t ReplicationExecutor::nextRandomInt64(int64_t limit) {
        return _random.nextInt64(limit);
    }

    Date_t ReplicationExecutor::scheduleReadySleepers_inlock(const Date_t now) {
        WorkQueue::iterator iter = _sleepersQueue.begin();
        while ((iter != _sleepersQueue.end()) && (iter->readyDate <= now)) {
            ++iter;
        }
        _readyQueue.splice(_readyQueue.end(), _sleepersQueue, _sleepersQueue.begin(), iter);
        if (iter == _sleepersQueue.end()) {
            // indicate no sleeper to wait for
            return Date_t::max();
        }
        return iter->readyDate;
    }

    StatusWith<ReplicationExecutor::CallbackHandle> ReplicationExecutor::enqueueWork_inlock(
            WorkQueue* queue, const CallbackFn& callbackFn) {

        invariant(callbackFn);
        StatusWith<EventHandle> event = makeEvent_inlock();
        if (!event.isOK())
            return StatusWith<CallbackHandle>(event.getStatus());

        if (_freeQueue.empty())
            _freeQueue.push_front(WorkItem());
        const WorkQueue::iterator iter = _freeQueue.begin();
        WorkItem& work = *iter;

        invariant(!work.callback.isValid());
        setCallbackForHandle(&work.callback, std::shared_ptr<executor::TaskExecutor::CallbackState>(
                new Callback(this, callbackFn, iter, event.getValue())));

        work.generation++;
        work.finishedEvent = event.getValue();
        work.readyDate = Date_t();
        queue->splice(queue->end(), _freeQueue, iter);
        return StatusWith<CallbackHandle>(work.callback);
    }

    ReplicationExecutor::WorkItem::WorkItem() : generation(0U),
                                                isNetworkOperation(false) {}

    ReplicationExecutor::Event::Event(ReplicationExecutor* executor,
                                      const EventList::iterator& iter) :
        executor::TaskExecutor::EventState(), _executor(executor), _isSignaled(false), _iter(iter) {}

    ReplicationExecutor::Event::~Event() {}

    void ReplicationExecutor::Event::signal() {
        // Must go through executor to signal so that this can be removed from the _unsignaledEvents
        // EventList.
        _executor->signalEvent(*_iter);
    }

    void ReplicationExecutor::Event::_signal_inlock() {
        invariant(!_isSignaled);
        _isSignaled = true;

        if (!_waiters.empty()) {
            _executor->_readyQueue.splice(_executor->_readyQueue.end(), _waiters);
            _executor->_networkInterface->signalWorkAvailable();
        }

        _isSignaledCondition.notify_all();
    }

    void ReplicationExecutor::Event::waitUntilSignaled() {
        boost::unique_lock<boost::mutex> lk(_executor->_mutex);
        ++_executor->_totalEventWaiters;
        while (!_isSignaled) {
            _isSignaledCondition.wait(lk);
        }
        --_executor->_totalEventWaiters;
        _executor->maybeNotifyShutdownComplete_inlock();
    }

    bool ReplicationExecutor::Event::isSignaled() {
        boost::lock_guard<boost::mutex> lk(_executor->_mutex);
        return _isSignaled;
    }

    ReplicationExecutor::Callback::Callback(ReplicationExecutor* executor,
                                            const CallbackFn callbackFn,
                                            const WorkQueue::iterator& iter,
                                            const EventHandle& finishedEvent) :
            executor::TaskExecutor::CallbackState(),
            _executor(executor),
            _callbackFn(callbackFn),
            _isCanceled(false),
            _iter(iter),
            _finishedEvent(finishedEvent) {}

    ReplicationExecutor::Callback::~Callback() {}

    void ReplicationExecutor::Callback::cancel() {
        boost::unique_lock<boost::mutex> lk(_executor->_mutex);
        _isCanceled = true;
        if (_iter->isNetworkOperation) {
            lk.unlock();
            _executor->_networkInterface->cancelCommand(_iter->callback);
        }
    }

    void ReplicationExecutor::Callback::waitForCompletion() {
        _executor->waitForEvent(_finishedEvent);
    }

    ReplicationExecutor::Event* ReplicationExecutor::_getEventFromHandle(
            const EventHandle& eventHandle) {
        return static_cast<Event*>(getEventFromHandle(eventHandle));
    }

    ReplicationExecutor::Callback* ReplicationExecutor::_getCallbackFromHandle(
            const CallbackHandle& callbackHandle) {
        return static_cast<Callback*>(getCallbackFromHandle(callbackHandle));
    }

namespace {

    void callNoExcept(const stdx::function<void ()>& fn) {
        try {
            fn();
        }
        catch (...) {
            std::terminate();
        }
    }

    stdx::function<void ()> makeNoExcept(const stdx::function<void ()> &fn) {
        return stdx::bind(callNoExcept, fn);
    }

}  // namespace

}  // namespace repl
}  // namespace mongo
