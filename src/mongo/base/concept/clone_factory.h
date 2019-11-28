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

#include "mongo/base/concept/assignable.h"
#include "mongo/base/concept/constructible.h"
#include "mongo/base/concept/unique_ptr.h"

namespace mongo {
namespace concept {
    /*!
     * Objects conforming to the `CloneFactory` concept are function-like constructs which return
     * objects that are dynamically allocated copies of their inputs.
     * These copies can be made without knowing the actual dynamic type.  The `CloneFactory` type
     * itself must be `Assignable`, in that it can be used with automatically generated copy
     * constructors and copy assignment operators.
     */
    template <typename T>
    struct CloneFactory : Assignable {
        Constructible<UniquePtr<T>> operator()(const T*) const;
    };
}  // namespace concept
}  // namespace mongo
