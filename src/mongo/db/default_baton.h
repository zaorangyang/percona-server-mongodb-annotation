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

#include <vector>

#include "mongo/db/baton.h"
#include "mongo/platform/condition_variable.h"
#include "mongo/platform/mutex.h"
#include "mongo/util/functional.h"

namespace mongo {

class OperationContext;

/**
 * The most basic Baton implementation.
 */
class DefaultBaton : public Baton {
public:
    explicit DefaultBaton(OperationContext* opCtx);

    ~DefaultBaton();

    void markKillOnClientDisconnect() noexcept override;

    void schedule(Task func) noexcept override;

    void notify() noexcept override;

    Waitable::TimeoutState run_until(ClockSource* clkSource, Date_t oldDeadline) noexcept override;

    void run(ClockSource* clkSource) noexcept override;

private:
    void detachImpl() noexcept override;

    Mutex _mutex = MONGO_MAKE_LATCH("DefaultBaton::_mutex");
    stdx::condition_variable _cv;
    bool _notified = false;
    bool _sleeping = false;

    OperationContext* _opCtx;

    bool _hasIngressSocket = false;

    std::vector<Task> _scheduled;
};

}  // namespace mongo
