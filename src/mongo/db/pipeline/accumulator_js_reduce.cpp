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

#include "mongo/platform/basic.h"

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/pipeline/accumulator_js_reduce.h"
#include "mongo/db/pipeline/make_js_function.h"

namespace mongo {

REGISTER_ACCUMULATOR_WITH_MIN_VERSION(
    _internalJsReduce,
    AccumulatorInternalJsReduce::parseInternalJsReduce,
    ServerGlobalParams::FeatureCompatibility::Version::kFullyUpgradedTo44);

AccumulationExpression AccumulatorInternalJsReduce::parseInternalJsReduce(
    boost::intrusive_ptr<ExpressionContext> expCtx, BSONElement elem, VariablesParseState vps) {
    uassert(31326,
            str::stream() << kAccumulatorName << " requires a document argument, but found "
                          << elem.type(),
            elem.type() == BSONType::Object);
    BSONObj obj = elem.embeddedObject();

    std::string funcSource;
    boost::intrusive_ptr<Expression> argument;

    for (auto&& element : obj) {
        if (element.fieldNameStringData() == "eval") {
            funcSource = parseReduceFunction(element);
        } else if (element.fieldNameStringData() == "data") {
            argument = Expression::parseOperand(expCtx, element, vps);
        } else {
            uasserted(31243,
                      str::stream() << "Invalid argument specified to " << kAccumulatorName << ": "
                                    << element.toString());
        }
    }
    uassert(31245,
            str::stream() << kAccumulatorName
                          << " requires 'eval' argument, recieved input: " << obj.toString(false),
            !funcSource.empty());
    uassert(31349,
            str::stream() << kAccumulatorName
                          << " requires 'data' argument, recieved input: " << obj.toString(false),
            argument);

    auto factory = [expCtx, funcSource = funcSource]() {
        return AccumulatorInternalJsReduce::create(expCtx, funcSource);
    };

    auto initializer = ExpressionConstant::create(expCtx, Value(BSONNULL));
    return {std::move(initializer), std::move(argument), std::move(factory)};
}

std::string AccumulatorInternalJsReduce::parseReduceFunction(BSONElement func) {
    uassert(
        31244,
        str::stream() << kAccumulatorName
                      << " requires the 'eval' argument to be of type string, or code but found "
                      << func.type(),
        func.type() == BSONType::String || func.type() == BSONType::Code);
    return func._asCode();
}

void AccumulatorInternalJsReduce::processInternal(const Value& input, bool merging) {
    if (input.missing()) {
        return;
    }
    uassert(31242,
            str::stream() << kAccumulatorName << " requires a document argument, but found "
                          << input.getType(),
            input.getType() == BSONType::Object);
    Document data = input.getDocument();

    uassert(
        31251,
        str::stream() << kAccumulatorName
                      << " requires the 'data' argument to have a 'k' and 'v' field. Instead found"
                      << data.toString(),
        data.computeSize() == 2ull && !data["k"].missing() && !data["v"].missing());

    _key = data["k"];

    _values.push_back(data["v"]);
    _memUsageBytes += data["v"].getApproximateSize();
}

Value AccumulatorInternalJsReduce::getValue(bool toBeMerged) {
    if (_values.size() < 1) {
        return Value{};
    }

    Value result;
    // Keep reducing until we have exactly one value.
    while (true) {
        BSONArrayBuilder bsonValues;
        size_t numLeft = _values.size();
        for (; numLeft > 0; numLeft--) {
            Value val = _values[numLeft - 1];

            // Do not insert if doing so would exceed the the maximum allowed BSONObj size.
            if (bsonValues.len() + _key.getApproximateSize() + val.getApproximateSize() >
                BSONObjMaxUserSize) {
                // If we have reached the threshold for maximum allowed BSONObj size and only have a
                // single value then no progress will be made on reduce. We must fail when this
                // scenario is encountered.
                size_t numNextReduce = _values.size() - numLeft;
                uassert(31392, "Value too large to reduce", numNextReduce > 1);
                break;
            }
            bsonValues << val;
        }

        auto expCtx = getExpressionContext();
        auto reduceFunc = makeJsFunc(expCtx, _funcSource.toString());

        // Function signature: reduce(key, values).
        BSONObj params = BSON_ARRAY(_key << bsonValues.arr());
        // For reduce, the key and values are both passed as 'params' so there's no need to set
        // 'this'.
        BSONObj thisObj;
        Value reduceResult =
            expCtx->getJsExecWithScope()->callFunction(reduceFunc, params, thisObj);
        if (numLeft == 0) {
            result = reduceResult;
            break;
        } else {
            // Remove all values which have been reduced.
            _values.resize(numLeft);
            // Include most recent result in the set of values to be reduced.
            _values.push_back(reduceResult);
        }
    }

    // If we're merging after this, wrap the value in the same format it was inserted in.
    if (toBeMerged) {
        MutableDocument output;
        output.addField("k", _key);
        output.addField("v", result);
        return Value(output.freeze());
    } else {
        return result;
    }
}

boost::intrusive_ptr<AccumulatorState> AccumulatorInternalJsReduce::create(
    const boost::intrusive_ptr<ExpressionContext>& expCtx, StringData funcSource) {

    return make_intrusive<AccumulatorInternalJsReduce>(expCtx, funcSource);
}

void AccumulatorInternalJsReduce::reset() {
    _values.clear();
    _memUsageBytes = sizeof(*this);
    _key = Value{};
}

// Returns this accumulator serialized as a Value along with the reduce function.
Document AccumulatorInternalJsReduce::serialize(boost::intrusive_ptr<Expression> initializer,
                                                boost::intrusive_ptr<Expression> argument,
                                                bool explain) const {
    return DOC(getOpName() << DOC("data" << argument->serialize(explain) << "eval" << _funcSource));
}

REGISTER_ACCUMULATOR_WITH_MIN_VERSION(
    accumulator,
    AccumulatorJs::parse,
    ServerGlobalParams::FeatureCompatibility::Version::kFullyUpgradedTo44);

boost::intrusive_ptr<AccumulatorState> AccumulatorJs::create(
    const boost::intrusive_ptr<ExpressionContext>& expCtx,
    std::string init,
    std::string accumulate,
    std::string merge,
    std::string finalize) {
    return new AccumulatorJs(
        expCtx, std::move(init), std::move(accumulate), std::move(merge), std::move(finalize));
}

namespace {
// Parses a constant expression of type String or Code.
std::string parseFunction(StringData fieldName,
                          boost::intrusive_ptr<ExpressionContext> expCtx,
                          BSONElement elem,
                          VariablesParseState vps) {
    boost::intrusive_ptr<Expression> expr = Expression::parseOperand(expCtx, elem, vps);
    expr = expr->optimize();
    ExpressionConstant* ec = dynamic_cast<ExpressionConstant*>(expr.get());
    uassert(4544701,
            str::stream() << "$accumulator '" << fieldName << "' must be a constant expression",
            ec);
    Value v = ec->getValue();
    uassert(4544702,
            str::stream() << "$accumulator '" << fieldName << "' must be a String or Code",
            v.getType() == BSONType::String || v.getType() == BSONType::Code);
    return v.coerceToString();
}
}  // namespace


Document AccumulatorJs::serialize(boost::intrusive_ptr<Expression> initializer,
                                  boost::intrusive_ptr<Expression> argument,
                                  bool explain) const {
    MutableDocument args;
    args.addField("init", Value(_init));
    args.addField("initArgs", Value(initializer->serialize(explain)));
    args.addField("accumulate", Value(_accumulate));
    args.addField("accumulateArgs", Value(argument->serialize(explain)));
    args.addField("merge", Value(_merge));
    args.addField("finalize", Value(_finalize));
    args.addField("lang", Value("js"_sd));
    return DOC(getOpName() << args.freeze());
}

AccumulationExpression AccumulatorJs::parse(boost::intrusive_ptr<ExpressionContext> expCtx,
                                            BSONElement elem,
                                            VariablesParseState vps) {
    /*
     * {$accumulator: {
     *   init: <code>,
     *   accumulate: <code>,
     *   merge: <code>,
     *   finalize: <code>,
     *
     *   accumulateArgs: <expr>,  // evaluated once per document
     *
     *   initArgs: <expr>,  // evaluated once per group
     *
     *   lang: 'js',
     * }}
     */
    uassert(4544703,
            str::stream() << "$accumulator expects an object as an argument; found: "
                          << typeName(elem.type()),
            elem.type() == BSONType::Object);
    BSONObj obj = elem.embeddedObject();

    std::string init, accumulate, merge, finalize;
    boost::intrusive_ptr<Expression> initArgs, accumulateArgs;

    for (auto&& element : obj) {
        auto name = element.fieldNameStringData();
        if (name == "init") {
            init = parseFunction("init", expCtx, element, vps);
        } else if (name == "accumulate") {
            accumulate = parseFunction("accumulate", expCtx, element, vps);
        } else if (name == "merge") {
            merge = parseFunction("merge", expCtx, element, vps);
        } else if (name == "finalize") {
            finalize = parseFunction("finalize", expCtx, element, vps);
        } else if (name == "initArgs") {
            initArgs = Expression::parseOperand(expCtx, element, vps);
        } else if (name == "accumulateArgs") {
            accumulateArgs = Expression::parseOperand(expCtx, element, vps);
        } else if (name == "lang") {
            uassert(4544704,
                    str::stream() << "$accumulator lang must be a string; found: "
                                  << element.type(),
                    element.type() == BSONType::String);
            uassert(4544705,
                    "$accumulator only supports lang: 'js'",
                    element.valueStringData() == "js");
        } else {
            // unexpected field
            uassert(
                4544706, str::stream() << "$accumulator got an unexpected field: " << name, false);
        }
    }
    uassert(4544707, "$accumulator missing required argument 'init'", !init.empty());
    uassert(4544708, "$accumulator missing required argument 'accumulate'", !accumulate.empty());
    uassert(4544709, "$accumulator missing required argument 'merge'", !merge.empty());
    if (finalize.empty()) {
        // finalize is optional because many custom accumulators will return the final state
        // unchanged.
        finalize = "function(state) { return state; }";
    }
    if (!initArgs) {
        // initArgs is optional because most custom accumulators don't need the state to depend on
        // the group key.
        initArgs = ExpressionConstant::create(expCtx, Value(BSONArray()));
    }
    // accumulateArgs is required because it's the only way to communicate a value from the input
    // stream into the accumulator state.
    uassert(4544710, "$accumulator missing required argument 'accumulateArgs'", accumulateArgs);

    auto factory = [expCtx = expCtx,
                    init = std::move(init),
                    accumulate = std::move(accumulate),
                    merge = std::move(merge),
                    finalize = std::move(finalize)]() {
        return new AccumulatorJs(expCtx, init, accumulate, merge, finalize);
    };
    return {std::move(initArgs), std::move(accumulateArgs), std::move(factory)};
}

Value AccumulatorJs::getValue(bool toBeMerged) {
    // _state is initialized when we encounter the first document in each group. We never create
    // empty groups: even in a {$group: {_id: 1, ...}}, we will return zero groups rather than one
    // empty group.
    invariant(_state);

    // If toBeMerged then we return the current state, to be fed back in to accumulate / merge /
    // finalize later. If not toBeMerged then we return the final value, by calling finalize.
    if (toBeMerged) {
        return *_state;
    }

    // Get the final value given the current accumulator state.

    auto& expCtx = getExpressionContext();
    auto jsExec = expCtx->getJsExecWithScope();
    auto func = makeJsFunc(expCtx, _finalize);

    return jsExec->callFunction(func, BSON_ARRAY(*_state), {});
}

void AccumulatorJs::startNewGroup(Value const& input) {
    // Between groups the _state should be empty: we initialize it to be empty it in the
    // constructor, and we clear it at the end of each group (in .reset()).
    invariant(!_state);

    auto& expCtx = getExpressionContext();
    auto jsExec = expCtx->getJsExecWithScope();
    auto func = makeJsFunc(expCtx, _init);

    // input is a value produced by our AccumulationExpression::initializer.
    uassert(4544711,
            str::stream() << "$accumulator initArgs must evaluate to an array: "
                          << input.toString(),
            input.getType() == BSONType::Array);

    size_t index = 0;
    BSONArrayBuilder bob;
    for (auto&& arg : input.getArray()) {
        arg.addToBsonArray(&bob, index++);
    }

    _state = jsExec->callFunction(func, bob.arr(), {});

    recomputeMemUsageBytes();
}

void AccumulatorJs::reset() {
    _state = std::nullopt;
    recomputeMemUsageBytes();
}

void AccumulatorJs::processInternal(const Value& input, bool merging) {
    // _state should be nonempty because we populate it in startNewGroup.
    invariant(_state);

    auto& expCtx = getExpressionContext();
    auto jsExec = expCtx->getJsExecWithScope();

    if (merging) {
        // input is an intermediate state from another instance of this kind of accumulator. Call
        // the user's merge function.
        auto func = makeJsFunc(expCtx, _merge);
        _state = jsExec->callFunction(func, BSON_ARRAY(*_state << input), {});
        recomputeMemUsageBytes();
    } else {
        // input is a value produced by our AccumulationExpression::argument. Call the user's
        // accumulate function.
        auto func = makeJsFunc(expCtx, _accumulate);
        uassert(4544712,
                str::stream() << "$accumulator accumulateArgs must evaluate to an array: "
                              << input.toString(),
                input.getType() == BSONType::Array);

        size_t index = 0;
        BSONArrayBuilder bob;
        _state->addToBsonArray(&bob, index++);
        for (auto&& arg : input.getArray()) {
            arg.addToBsonArray(&bob, index++);
        }

        _state = jsExec->callFunction(func, bob.done(), {});
        recomputeMemUsageBytes();
    }
}

void AccumulatorJs::recomputeMemUsageBytes() {
    auto stateSize = _state.value_or(Value{}).getApproximateSize();
    uassert(4544713,
            str::stream() << "$accumulator state exceeded max BSON size: " << stateSize,
            stateSize <= BSONObjMaxUserSize);
    _memUsageBytes = sizeof(*this) + stateSize + _init.capacity() + _accumulate.capacity() +
        _merge.capacity() + _finalize.capacity();
}

}  // namespace mongo
