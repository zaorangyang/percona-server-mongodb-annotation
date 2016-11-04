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

#pragma once

#include "mongo/db/concurrency/lock_manager.h"
#include "mongo/db/concurrency/lock_state.h"

namespace mongo {

class LockerForTests : public LockerImpl<false> {
public:
    explicit LockerForTests(LockMode globalLockMode) {
        lockGlobal(globalLockMode);
    }

    ~LockerForTests() {
        unlockGlobal();
    }
};


class TrackingLockGrantNotification : public LockGrantNotification {
public:
    TrackingLockGrantNotification() : numNotifies(0), lastResult(LOCK_INVALID) {}

    virtual void notify(ResourceId resId, LockResult result) {
        numNotifies++;
        lastResId = resId;
        lastResult = result;
    }

public:
    int numNotifies;

    ResourceId lastResId;
    LockResult lastResult;
};


struct LockRequestCombo : public LockRequest, TrackingLockGrantNotification {
public:
    explicit LockRequestCombo(Locker* locker) {
        initNew(locker, this);
    }
};

}  // namespace mongo
