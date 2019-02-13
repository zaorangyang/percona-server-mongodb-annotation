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

#include "mongo/util/tick_source.h"

namespace mongo {

/**
 * Tick source based on platform specific clock ticks. Should be of reasonably high
 * performance. The maximum span measurable by the counter and convertible to microseconds
 * is about 10 trillion ticks. As long as there are fewer than 100 ticks per nanosecond,
 * timer durations of 2.5 years will be supported. Since a typical tick duration will be
 * under 10 per nanosecond, if not below 1 per nanosecond, this should not be an issue.
 */
class SystemTickSource final : public TickSource {
public:
    TickSource::Tick getTicks() override;

    TickSource::Tick getTicksPerSecond() override;

    /**
     * Gets the singleton instance of SystemTickSource. Should not be called before
     * the global initializers are done.
     */
    static SystemTickSource* get();
};
}  // namespace mongo
