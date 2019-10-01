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

#include "mongo/platform/basic.h"

#include "mongo/db/pipeline/accumulator.h"

#include "mongo/db/exec/document_value/value.h"
#include "mongo/db/pipeline/accumulation_statement.h"

namespace mongo {

using boost::intrusive_ptr;

REGISTER_ACCUMULATOR(last, AccumulatorLast::create);

const char* AccumulatorLast::getOpName() const {
    return "$last";
}

void AccumulatorLast::processInternal(const Value& input, bool merging) {
    /* always remember the last value seen */
    _last = input;
    _memUsageBytes = sizeof(*this) + _last.getApproximateSize() - sizeof(Value);
}

Value AccumulatorLast::getValue(bool toBeMerged) {
    return _last;
}

AccumulatorLast::AccumulatorLast(const boost::intrusive_ptr<ExpressionContext>& expCtx)
    : Accumulator(expCtx) {
    _memUsageBytes = sizeof(*this);
}

void AccumulatorLast::reset() {
    _memUsageBytes = sizeof(*this);
    _last = Value();
}

intrusive_ptr<Accumulator> AccumulatorLast::create(
    const boost::intrusive_ptr<ExpressionContext>& expCtx) {
    return new AccumulatorLast(expCtx);
}
}  // namespace mongo
