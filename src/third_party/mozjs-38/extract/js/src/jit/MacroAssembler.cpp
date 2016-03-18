/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/MacroAssembler.h"

#include "jsprf.h"

#include "builtin/TypedObject.h"
#include "gc/GCTrace.h"
#include "jit/AtomicOp.h"
#include "jit/Bailouts.h"
#include "jit/BaselineFrame.h"
#include "jit/BaselineIC.h"
#include "jit/BaselineJIT.h"
#include "jit/Lowering.h"
#include "jit/MIR.h"
#include "js/Conversions.h"
#include "vm/TraceLogging.h"

#include "jsgcinlines.h"
#include "jsobjinlines.h"
#include "vm/Interpreter-inl.h"

using namespace js;
using namespace js::jit;

using JS::GenericNaN;
using JS::ToInt32;

namespace {

// Emulate a TypeSet logic from a Type object to avoid duplicating the guard
// logic.
class TypeWrapper {
    TypeSet::Type t_;

  public:
    explicit TypeWrapper(TypeSet::Type t) : t_(t) {}

    inline bool unknown() const {
        return t_.isUnknown();
    }
    inline bool hasType(TypeSet::Type t) const {
        if (t == TypeSet::Int32Type())
            return t == t_ || t_ == TypeSet::DoubleType();
        return t == t_;
    }
    inline unsigned getObjectCount() const {
        if (t_.isAnyObject() || t_.isUnknown() || !t_.isObject())
            return 0;
        return 1;
    }
    inline JSObject* getSingletonNoBarrier(unsigned) const {
        if (t_.isSingleton())
            return t_.singletonNoBarrier();
        return nullptr;
    }
    inline ObjectGroup* getGroupNoBarrier(unsigned) const {
        if (t_.isGroup())
            return t_.groupNoBarrier();
        return nullptr;
    }
};

} /* anonymous namespace */

template <typename Source, typename Set> void
MacroAssembler::guardTypeSet(const Source& address, const Set *types, BarrierKind kind,
                             Register scratch, Label* miss)
{
    MOZ_ASSERT(kind == BarrierKind::TypeTagOnly || kind == BarrierKind::TypeSet);
    MOZ_ASSERT(!types->unknown());

    Label matched;
    TypeSet::Type tests[8] = {
        TypeSet::Int32Type(),
        TypeSet::UndefinedType(),
        TypeSet::BooleanType(),
        TypeSet::StringType(),
        TypeSet::SymbolType(),
        TypeSet::NullType(),
        TypeSet::MagicArgType(),
        TypeSet::AnyObjectType()
    };

    // The double type also implies Int32.
    // So replace the int32 test with the double one.
    if (types->hasType(TypeSet::DoubleType())) {
        MOZ_ASSERT(types->hasType(TypeSet::Int32Type()));
        tests[0] = TypeSet::DoubleType();
    }

    Register tag = extractTag(address, scratch);

    // Emit all typed tests.
    BranchType lastBranch;
    for (size_t i = 0; i < mozilla::ArrayLength(tests); i++) {
        if (!types->hasType(tests[i]))
            continue;

        if (lastBranch.isInitialized())
            lastBranch.emit(*this);
        lastBranch = BranchType(Equal, tag, tests[i], &matched);
    }

    // If this is the last check, invert the last branch.
    if (types->hasType(TypeSet::AnyObjectType()) || !types->getObjectCount()) {
        if (!lastBranch.isInitialized()) {
            jump(miss);
            return;
        }

        lastBranch.invertCondition();
        lastBranch.relink(miss);
        lastBranch.emit(*this);

        bind(&matched);
        return;
    }

    if (lastBranch.isInitialized())
        lastBranch.emit(*this);

    // Test specific objects.
    MOZ_ASSERT(scratch != InvalidReg);
    branchTestObject(NotEqual, tag, miss);
    if (kind != BarrierKind::TypeTagOnly) {
        Register obj = extractObject(address, scratch);
        guardObjectType(obj, types, scratch, miss);
    } else {
#ifdef DEBUG
        Label fail;
        Register obj = extractObject(address, scratch);
        guardObjectType(obj, types, scratch, &fail);
        jump(&matched);
        bind(&fail);

        // Type set guards might miss when an object's type changes and its
        // properties become unknown, so check for this case.
        if (obj == scratch)
            extractObject(address, scratch);
        loadPtr(Address(obj, JSObject::offsetOfGroup()), scratch);
        branchTestPtr(Assembler::NonZero,
                      Address(scratch, ObjectGroup::offsetOfFlags()),
                      Imm32(OBJECT_FLAG_UNKNOWN_PROPERTIES), &matched);

        assumeUnreachable("Unexpected object type");
#endif
    }

    bind(&matched);
}

template <typename Set> void
MacroAssembler::guardObjectType(Register obj, const Set *types,
                                Register scratch, Label* miss)
{
    MOZ_ASSERT(!types->unknown());
    MOZ_ASSERT(!types->hasType(TypeSet::AnyObjectType()));
    MOZ_ASSERT(types->getObjectCount());
    MOZ_ASSERT(scratch != InvalidReg);

    // Note: this method elides read barriers on values read from type sets, as
    // this may be called off the main thread during Ion compilation. This is
    // safe to do as the final JitCode object will be allocated during the
    // incremental GC (or the compilation canceled before we start sweeping),
    // see CodeGenerator::link. Other callers should use TypeSet::readBarrier
    // to trigger the barrier on the contents of type sets passed in here.
    Label matched;

    BranchGCPtr lastBranch;
    MOZ_ASSERT(!lastBranch.isInitialized());
    bool hasObjectGroups = false;
    unsigned count = types->getObjectCount();
    for (unsigned i = 0; i < count; i++) {
        if (!types->getSingletonNoBarrier(i)) {
            hasObjectGroups = hasObjectGroups || types->getGroupNoBarrier(i);
            continue;
        }

        if (lastBranch.isInitialized())
            lastBranch.emit(*this);

        JSObject* object = types->getSingletonNoBarrier(i);
        lastBranch = BranchGCPtr(Equal, obj, ImmGCPtr(object), &matched);
    }

    if (hasObjectGroups) {
        // We are possibly going to overwrite the obj register. So already
        // emit the branch, since branch depends on previous value of obj
        // register and there is definitely a branch following. So no need
        // to invert the condition.
        if (lastBranch.isInitialized())
            lastBranch.emit(*this);
        lastBranch = BranchGCPtr();

        // Note: Some platforms give the same register for obj and scratch.
        // Make sure when writing to scratch, the obj register isn't used anymore!
        loadPtr(Address(obj, JSObject::offsetOfGroup()), scratch);

        for (unsigned i = 0; i < count; i++) {
            if (!types->getGroupNoBarrier(i))
                continue;

            if (lastBranch.isInitialized())
                lastBranch.emit(*this);

            ObjectGroup* group = types->getGroupNoBarrier(i);
            lastBranch = BranchGCPtr(Equal, scratch, ImmGCPtr(group), &matched);
        }
    }

    if (!lastBranch.isInitialized()) {
        jump(miss);
        return;
    }

    lastBranch.invertCondition();
    lastBranch.relink(miss);
    lastBranch.emit(*this);

    bind(&matched);
    return;
}

template <typename Source> void
MacroAssembler::guardType(const Source& address, TypeSet::Type type,
                          Register scratch, Label* miss)
{
    TypeWrapper wrapper(type);
    guardTypeSet(address, &wrapper, BarrierKind::TypeSet, scratch, miss);
}

template void MacroAssembler::guardTypeSet(const Address& address, const TemporaryTypeSet* types,
                                           BarrierKind kind, Register scratch, Label* miss);
template void MacroAssembler::guardTypeSet(const ValueOperand& value, const TemporaryTypeSet* types,
                                           BarrierKind kind, Register scratch, Label* miss);

template void MacroAssembler::guardTypeSet(const Address& address, const HeapTypeSet* types,
                                           BarrierKind kind, Register scratch, Label* miss);
template void MacroAssembler::guardTypeSet(const ValueOperand& value, const HeapTypeSet* types,
                                           BarrierKind kind, Register scratch, Label* miss);
template void MacroAssembler::guardTypeSet(const TypedOrValueRegister& reg, const HeapTypeSet* types,
                                           BarrierKind kind, Register scratch, Label* miss);

template void MacroAssembler::guardTypeSet(const Address& address, const TypeSet* types,
                                           BarrierKind kind, Register scratch, Label* miss);
template void MacroAssembler::guardTypeSet(const ValueOperand& value, const TypeSet* types,
                                           BarrierKind kind, Register scratch, Label* miss);

template void MacroAssembler::guardTypeSet(const Address& address, const TypeWrapper* types,
                                           BarrierKind kind, Register scratch, Label* miss);
template void MacroAssembler::guardTypeSet(const ValueOperand& value, const TypeWrapper* types,
                                           BarrierKind kind, Register scratch, Label* miss);

template void MacroAssembler::guardObjectType(Register obj, const TemporaryTypeSet* types,
                                              Register scratch, Label* miss);
template void MacroAssembler::guardObjectType(Register obj, const TypeSet* types,
                                              Register scratch, Label* miss);
template void MacroAssembler::guardObjectType(Register obj, const TypeWrapper* types,
                                              Register scratch, Label* miss);

template void MacroAssembler::guardType(const Address& address, TypeSet::Type type,
                                        Register scratch, Label* miss);
template void MacroAssembler::guardType(const ValueOperand& value, TypeSet::Type type,
                                        Register scratch, Label* miss);

template<typename S, typename T>
static void
StoreToTypedFloatArray(MacroAssembler& masm, int arrayType, const S& value, const T& dest)
{
    switch (arrayType) {
      case Scalar::Float32:
        masm.storeFloat32(value, dest);
        break;
      case Scalar::Float64:
#ifdef JS_MORE_DETERMINISTIC
        // See the comment in TypedArrayObjectTemplate::doubleToNative.
        masm.canonicalizeDouble(value);
#endif
        masm.storeDouble(value, dest);
        break;
      default:
        MOZ_CRASH("Invalid typed array type");
    }
}

void
MacroAssembler::storeToTypedFloatArray(Scalar::Type arrayType, FloatRegister value,
                                       const BaseIndex& dest)
{
    StoreToTypedFloatArray(*this, arrayType, value, dest);
}
void
MacroAssembler::storeToTypedFloatArray(Scalar::Type arrayType, FloatRegister value,
                                       const Address& dest)
{
    StoreToTypedFloatArray(*this, arrayType, value, dest);
}

template<typename T>
void
MacroAssembler::loadFromTypedArray(Scalar::Type arrayType, const T& src, AnyRegister dest, Register temp,
                                   Label* fail, bool canonicalizeDoubles)
{
    switch (arrayType) {
      case Scalar::Int8:
        load8SignExtend(src, dest.gpr());
        break;
      case Scalar::Uint8:
      case Scalar::Uint8Clamped:
        load8ZeroExtend(src, dest.gpr());
        break;
      case Scalar::Int16:
        load16SignExtend(src, dest.gpr());
        break;
      case Scalar::Uint16:
        load16ZeroExtend(src, dest.gpr());
        break;
      case Scalar::Int32:
        load32(src, dest.gpr());
        break;
      case Scalar::Uint32:
        if (dest.isFloat()) {
            load32(src, temp);
            convertUInt32ToDouble(temp, dest.fpu());
        } else {
            load32(src, dest.gpr());

            // Bail out if the value doesn't fit into a signed int32 value. This
            // is what allows MLoadTypedArrayElement to have a type() of
            // MIRType_Int32 for UInt32 array loads.
            branchTest32(Assembler::Signed, dest.gpr(), dest.gpr(), fail);
        }
        break;
      case Scalar::Float32:
        loadFloat32(src, dest.fpu());
        canonicalizeFloat(dest.fpu());
        break;
      case Scalar::Float64:
        loadDouble(src, dest.fpu());
        if (canonicalizeDoubles)
            canonicalizeDouble(dest.fpu());
        break;
      default:
        MOZ_CRASH("Invalid typed array type");
    }
}

template void MacroAssembler::loadFromTypedArray(Scalar::Type arrayType, const Address& src, AnyRegister dest,
                                                 Register temp, Label* fail, bool canonicalizeDoubles);
template void MacroAssembler::loadFromTypedArray(Scalar::Type arrayType, const BaseIndex& src, AnyRegister dest,
                                                 Register temp, Label* fail, bool canonicalizeDoubles);

template<typename T>
void
MacroAssembler::loadFromTypedArray(Scalar::Type arrayType, const T& src, const ValueOperand& dest,
                                   bool allowDouble, Register temp, Label* fail)
{
    switch (arrayType) {
      case Scalar::Int8:
      case Scalar::Uint8:
      case Scalar::Uint8Clamped:
      case Scalar::Int16:
      case Scalar::Uint16:
      case Scalar::Int32:
        loadFromTypedArray(arrayType, src, AnyRegister(dest.scratchReg()), InvalidReg, nullptr);
        tagValue(JSVAL_TYPE_INT32, dest.scratchReg(), dest);
        break;
      case Scalar::Uint32:
        // Don't clobber dest when we could fail, instead use temp.
        load32(src, temp);
        if (allowDouble) {
            // If the value fits in an int32, store an int32 type tag.
            // Else, convert the value to double and box it.
            Label done, isDouble;
            branchTest32(Assembler::Signed, temp, temp, &isDouble);
            {
                tagValue(JSVAL_TYPE_INT32, temp, dest);
                jump(&done);
            }
            bind(&isDouble);
            {
                convertUInt32ToDouble(temp, ScratchDoubleReg);
                boxDouble(ScratchDoubleReg, dest);
            }
            bind(&done);
        } else {
            // Bailout if the value does not fit in an int32.
            branchTest32(Assembler::Signed, temp, temp, fail);
            tagValue(JSVAL_TYPE_INT32, temp, dest);
        }
        break;
      case Scalar::Float32:
        loadFromTypedArray(arrayType, src, AnyRegister(ScratchFloat32Reg), dest.scratchReg(),
                           nullptr);
        convertFloat32ToDouble(ScratchFloat32Reg, ScratchDoubleReg);
        boxDouble(ScratchDoubleReg, dest);
        break;
      case Scalar::Float64:
        loadFromTypedArray(arrayType, src, AnyRegister(ScratchDoubleReg), dest.scratchReg(),
                           nullptr);
        boxDouble(ScratchDoubleReg, dest);
        break;
      default:
        MOZ_CRASH("Invalid typed array type");
    }
}

template void MacroAssembler::loadFromTypedArray(Scalar::Type arrayType, const Address& src, const ValueOperand& dest,
                                                 bool allowDouble, Register temp, Label* fail);
template void MacroAssembler::loadFromTypedArray(Scalar::Type arrayType, const BaseIndex& src, const ValueOperand& dest,
                                                 bool allowDouble, Register temp, Label* fail);

template<typename T>
void
MacroAssembler::compareExchangeToTypedIntArray(Scalar::Type arrayType, const T& mem,
                                               Register oldval, Register newval,
                                               Register temp, AnyRegister output)
{
    switch (arrayType) {
      case Scalar::Int8:
        compareExchange8SignExtend(mem, oldval, newval, output.gpr());
        break;
      case Scalar::Uint8:
        compareExchange8ZeroExtend(mem, oldval, newval, output.gpr());
        break;
      case Scalar::Uint8Clamped:
        compareExchange8ZeroExtend(mem, oldval, newval, output.gpr());
        break;
      case Scalar::Int16:
        compareExchange16SignExtend(mem, oldval, newval, output.gpr());
        break;
      case Scalar::Uint16:
        compareExchange16ZeroExtend(mem, oldval, newval, output.gpr());
        break;
      case Scalar::Int32:
        compareExchange32(mem, oldval, newval, output.gpr());
        break;
      case Scalar::Uint32:
        // At the moment, the code in MCallOptimize.cpp requires the output
        // type to be double for uint32 arrays.  See bug 1077305.
        MOZ_ASSERT(output.isFloat());
        compareExchange32(mem, oldval, newval, temp);
        convertUInt32ToDouble(temp, output.fpu());
        break;
      default:
        MOZ_CRASH("Invalid typed array type");
    }
}

template void
MacroAssembler::compareExchangeToTypedIntArray(Scalar::Type arrayType, const Address& mem,
                                               Register oldval, Register newval, Register temp,
                                               AnyRegister output);
template void
MacroAssembler::compareExchangeToTypedIntArray(Scalar::Type arrayType, const BaseIndex& mem,
                                               Register oldval, Register newval, Register temp,
                                               AnyRegister output);

template<typename S, typename T>
void
MacroAssembler::atomicBinopToTypedIntArray(AtomicOp op, Scalar::Type arrayType, const S& value,
                                           const T& mem, Register temp1, Register temp2, AnyRegister output)
{
    // Uint8Clamped is explicitly not supported here
    switch (arrayType) {
      case Scalar::Int8:
        switch (op) {
          case AtomicFetchAddOp:
            atomicFetchAdd8SignExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchSubOp:
            atomicFetchSub8SignExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchAndOp:
            atomicFetchAnd8SignExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchOrOp:
            atomicFetchOr8SignExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchXorOp:
            atomicFetchXor8SignExtend(value, mem, temp1, output.gpr());
            break;
          default:
            MOZ_CRASH("Invalid typed array atomic operation");
        }
        break;
      case Scalar::Uint8:
        switch (op) {
          case AtomicFetchAddOp:
            atomicFetchAdd8ZeroExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchSubOp:
            atomicFetchSub8ZeroExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchAndOp:
            atomicFetchAnd8ZeroExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchOrOp:
            atomicFetchOr8ZeroExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchXorOp:
            atomicFetchXor8ZeroExtend(value, mem, temp1, output.gpr());
            break;
          default:
            MOZ_CRASH("Invalid typed array atomic operation");
        }
        break;
      case Scalar::Int16:
        switch (op) {
          case AtomicFetchAddOp:
            atomicFetchAdd16SignExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchSubOp:
            atomicFetchSub16SignExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchAndOp:
            atomicFetchAnd16SignExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchOrOp:
            atomicFetchOr16SignExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchXorOp:
            atomicFetchXor16SignExtend(value, mem, temp1, output.gpr());
            break;
          default:
            MOZ_CRASH("Invalid typed array atomic operation");
        }
        break;
      case Scalar::Uint16:
        switch (op) {
          case AtomicFetchAddOp:
            atomicFetchAdd16ZeroExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchSubOp:
            atomicFetchSub16ZeroExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchAndOp:
            atomicFetchAnd16ZeroExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchOrOp:
            atomicFetchOr16ZeroExtend(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchXorOp:
            atomicFetchXor16ZeroExtend(value, mem, temp1, output.gpr());
            break;
          default:
            MOZ_CRASH("Invalid typed array atomic operation");
        }
        break;
      case Scalar::Int32:
        switch (op) {
          case AtomicFetchAddOp:
            atomicFetchAdd32(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchSubOp:
            atomicFetchSub32(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchAndOp:
            atomicFetchAnd32(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchOrOp:
            atomicFetchOr32(value, mem, temp1, output.gpr());
            break;
          case AtomicFetchXorOp:
            atomicFetchXor32(value, mem, temp1, output.gpr());
            break;
          default:
            MOZ_CRASH("Invalid typed array atomic operation");
        }
        break;
      case Scalar::Uint32:
        // At the moment, the code in MCallOptimize.cpp requires the output
        // type to be double for uint32 arrays.  See bug 1077305.
        MOZ_ASSERT(output.isFloat());
        switch (op) {
          case AtomicFetchAddOp:
            atomicFetchAdd32(value, mem, InvalidReg, temp1);
            break;
          case AtomicFetchSubOp:
            atomicFetchSub32(value, mem, InvalidReg, temp1);
            break;
          case AtomicFetchAndOp:
            atomicFetchAnd32(value, mem, temp2, temp1);
            break;
          case AtomicFetchOrOp:
            atomicFetchOr32(value, mem, temp2, temp1);
            break;
          case AtomicFetchXorOp:
            atomicFetchXor32(value, mem, temp2, temp1);
            break;
          default:
            MOZ_CRASH("Invalid typed array atomic operation");
        }
        convertUInt32ToDouble(temp1, output.fpu());
        break;
      default:
        MOZ_CRASH("Invalid typed array type");
    }
}

template void
MacroAssembler::atomicBinopToTypedIntArray(AtomicOp op, Scalar::Type arrayType,
                                           const Imm32& value, const Address& mem,
                                           Register temp1, Register temp2, AnyRegister output);
template void
MacroAssembler::atomicBinopToTypedIntArray(AtomicOp op, Scalar::Type arrayType,
                                           const Imm32& value, const BaseIndex& mem,
                                           Register temp1, Register temp2, AnyRegister output);
template void
MacroAssembler::atomicBinopToTypedIntArray(AtomicOp op, Scalar::Type arrayType,
                                           const Register& value, const Address& mem,
                                           Register temp1, Register temp2, AnyRegister output);
template void
MacroAssembler::atomicBinopToTypedIntArray(AtomicOp op, Scalar::Type arrayType,
                                           const Register& value, const BaseIndex& mem,
                                           Register temp1, Register temp2, AnyRegister output);

template <typename T>
void
MacroAssembler::loadUnboxedProperty(T address, JSValueType type, TypedOrValueRegister output)
{
    switch (type) {
      case JSVAL_TYPE_BOOLEAN:
      case JSVAL_TYPE_INT32:
      case JSVAL_TYPE_STRING: {
        Register outReg;
        if (output.hasValue()) {
            outReg = output.valueReg().scratchReg();
        } else {
            MOZ_ASSERT(output.type() == MIRTypeFromValueType(type));
            outReg = output.typedReg().gpr();
        }

        switch (type) {
          case JSVAL_TYPE_BOOLEAN:
            load8ZeroExtend(address, outReg);
            break;
          case JSVAL_TYPE_INT32:
            load32(address, outReg);
            break;
          case JSVAL_TYPE_STRING:
            loadPtr(address, outReg);
            break;
          default:
            MOZ_CRASH();
        }

        if (output.hasValue())
            tagValue(type, outReg, output.valueReg());
        break;
      }

      case JSVAL_TYPE_OBJECT:
        if (output.hasValue()) {
            Register scratch = output.valueReg().scratchReg();
            loadPtr(address, scratch);

            Label notNull, done;
            branchPtr(Assembler::NotEqual, scratch, ImmWord(0), &notNull);

            moveValue(NullValue(), output.valueReg());
            jump(&done);

            bind(&notNull);
            tagValue(JSVAL_TYPE_OBJECT, scratch, output.valueReg());

            bind(&done);
        } else {
            // Reading null can't be possible here, as otherwise the result
            // would be a value (either because null has been read before or
            // because there is a barrier).
            Register reg = output.typedReg().gpr();
            loadPtr(address, reg);
#ifdef DEBUG
            Label ok;
            branchTestPtr(Assembler::NonZero, reg, reg, &ok);
            assumeUnreachable("Null not possible");
            bind(&ok);
#endif
        }
        break;

      case JSVAL_TYPE_DOUBLE:
        // Note: doubles in unboxed objects are not accessed through other
        // views and do not need canonicalization.
        if (output.hasValue())
            loadValue(address, output.valueReg());
        else
            loadDouble(address, output.typedReg().fpu());
        break;

      default:
        MOZ_CRASH();
    }
}

template void
MacroAssembler::loadUnboxedProperty(Address address, JSValueType type,
                                    TypedOrValueRegister output);

template void
MacroAssembler::loadUnboxedProperty(BaseIndex address, JSValueType type,
                                    TypedOrValueRegister output);

template <typename T>
void
MacroAssembler::storeUnboxedProperty(T address, JSValueType type,
                                     ConstantOrRegister value, Label* failure)
{
    switch (type) {
      case JSVAL_TYPE_BOOLEAN:
        if (value.constant()) {
            if (value.value().isBoolean())
                store8(Imm32(value.value().toBoolean()), address);
            else
                jump(failure);
        } else if (value.reg().hasTyped()) {
            if (value.reg().type() == MIRType_Boolean)
                store8(value.reg().typedReg().gpr(), address);
            else
                jump(failure);
        } else {
            if (failure)
                branchTestBoolean(Assembler::NotEqual, value.reg().valueReg(), failure);
            storeUnboxedPayload(value.reg().valueReg(), address, /* width = */ 1);
        }
        break;

      case JSVAL_TYPE_INT32:
        if (value.constant()) {
            if (value.value().isInt32())
                store32(Imm32(value.value().toInt32()), address);
            else
                jump(failure);
        } else if (value.reg().hasTyped()) {
            if (value.reg().type() == MIRType_Int32)
                store32(value.reg().typedReg().gpr(), address);
            else
                jump(failure);
        } else {
            if (failure)
                branchTestInt32(Assembler::NotEqual, value.reg().valueReg(), failure);
            storeUnboxedPayload(value.reg().valueReg(), address, /* width = */ 4);
        }
        break;

      case JSVAL_TYPE_DOUBLE:
        if (value.constant()) {
            if (value.value().isNumber()) {
                loadConstantDouble(value.value().toNumber(), ScratchDoubleReg);
                storeDouble(ScratchDoubleReg, address);
            } else {
                jump(failure);
            }
        } else if (value.reg().hasTyped()) {
            if (value.reg().type() == MIRType_Int32) {
                convertInt32ToDouble(value.reg().typedReg().gpr(), ScratchDoubleReg);
                storeDouble(ScratchDoubleReg, address);
            } else if (value.reg().type() == MIRType_Double) {
                storeDouble(value.reg().typedReg().fpu(), address);
            } else {
                jump(failure);
            }
        } else {
            ValueOperand reg = value.reg().valueReg();
            Label notInt32, end;
            branchTestInt32(Assembler::NotEqual, reg, &notInt32);
            int32ValueToDouble(reg, ScratchDoubleReg);
            storeDouble(ScratchDoubleReg, address);
            jump(&end);
            bind(&notInt32);
            if (failure)
                branchTestDouble(Assembler::NotEqual, reg, failure);
            storeValue(reg, address);
            bind(&end);
        }
        break;

      case JSVAL_TYPE_OBJECT:
        if (value.constant()) {
            if (value.value().isObjectOrNull())
                storePtr(ImmGCPtr(value.value().toObjectOrNull()), address);
            else
                jump(failure);
        } else if (value.reg().hasTyped()) {
            MOZ_ASSERT(value.reg().type() != MIRType_Null);
            if (value.reg().type() == MIRType_Object)
                storePtr(value.reg().typedReg().gpr(), address);
            else
                jump(failure);
        } else {
            if (failure) {
                Label ok;
                branchTestNull(Assembler::Equal, value.reg().valueReg(), &ok);
                branchTestObject(Assembler::NotEqual, value.reg().valueReg(), failure);
                bind(&ok);
            }
            storeUnboxedPayload(value.reg().valueReg(), address, /* width = */ sizeof(uintptr_t));
        }
        break;

      case JSVAL_TYPE_STRING:
        if (value.constant()) {
            if (value.value().isString())
                storePtr(ImmGCPtr(value.value().toString()), address);
            else
                jump(failure);
        } else if (value.reg().hasTyped()) {
            if (value.reg().type() == MIRType_String)
                storePtr(value.reg().typedReg().gpr(), address);
            else
                jump(failure);
        } else {
            if (failure)
                branchTestString(Assembler::NotEqual, value.reg().valueReg(), failure);
            storeUnboxedPayload(value.reg().valueReg(), address, /* width = */ sizeof(uintptr_t));
        }
        break;

      default:
        MOZ_CRASH();
    }
}

template void
MacroAssembler::storeUnboxedProperty(Address address, JSValueType type,
                                     ConstantOrRegister value, Label* failure);

template void
MacroAssembler::storeUnboxedProperty(BaseIndex address, JSValueType type,
                                     ConstantOrRegister value, Label* failure);

// Inlined version of gc::CheckAllocatorState that checks the bare essentials
// and bails for anything that cannot be handled with our jit allocators.
void
MacroAssembler::checkAllocatorState(Label* fail)
{
    // Don't execute the inline path if we are tracing allocations.
    if (js::gc::TraceEnabled())
        jump(fail);

# ifdef JS_GC_ZEAL
    // Don't execute the inline path if gc zeal or tracing are active.
    branch32(Assembler::NotEqual,
             AbsoluteAddress(GetJitContext()->runtime->addressOfGCZeal()), Imm32(0),
             fail);
# endif

    // Don't execute the inline path if the compartment has an object metadata callback,
    // as the metadata to use for the object may vary between executions of the op.
    if (GetJitContext()->compartment->hasObjectMetadataCallback())
        jump(fail);
}

// Inline version of ShouldNurseryAllocate.
bool
MacroAssembler::shouldNurseryAllocate(gc::AllocKind allocKind, gc::InitialHeap initialHeap)
{
    // Note that Ion elides barriers on writes to objects known to be in the
    // nursery, so any allocation that can be made into the nursery must be made
    // into the nursery, even if the nursery is disabled. At runtime these will
    // take the out-of-line path, which is required to insert a barrier for the
    // initializing writes.
    return IsNurseryAllocable(allocKind) && initialHeap != gc::TenuredHeap;
}

// Inline version of Nursery::allocateObject.
void
MacroAssembler::nurseryAllocate(Register result, Register slots, gc::AllocKind allocKind,
                                size_t nDynamicSlots, gc::InitialHeap initialHeap, Label* fail)
{
    MOZ_ASSERT(IsNurseryAllocable(allocKind));
    MOZ_ASSERT(initialHeap != gc::TenuredHeap);

    // We still need to allocate in the nursery, per the comment in
    // shouldNurseryAllocate; however, we need to insert into hugeSlots, so
    // bail to do the nursery allocation in the interpreter.
    if (nDynamicSlots >= Nursery::MaxNurserySlots) {
        jump(fail);
        return;
    }

    // No explicit check for nursery.isEnabled() is needed, as the comparison
    // with the nursery's end will always fail in such cases.
    const Nursery& nursery = GetJitContext()->runtime->gcNursery();
    Register temp = slots;
    int thingSize = int(gc::Arena::thingSize(allocKind));
    int totalSize = thingSize + nDynamicSlots * sizeof(HeapSlot);
    loadPtr(AbsoluteAddress(nursery.addressOfPosition()), result);
    computeEffectiveAddress(Address(result, totalSize), temp);
    branchPtr(Assembler::Below, AbsoluteAddress(nursery.addressOfCurrentEnd()), temp, fail);
    storePtr(temp, AbsoluteAddress(nursery.addressOfPosition()));

    if (nDynamicSlots)
        computeEffectiveAddress(Address(result, thingSize), slots);
}

// Inlined version of FreeList::allocate.
void
MacroAssembler::freeListAllocate(Register result, Register temp, gc::AllocKind allocKind, Label* fail)
{
    CompileZone* zone = GetJitContext()->compartment->zone();
    int thingSize = int(gc::Arena::thingSize(allocKind));

    Label fallback;
    Label success;

    // Load FreeList::head::first of |zone|'s freeLists for |allocKind|. If
    // there is no room remaining in the span, fall back to get the next one.
    loadPtr(AbsoluteAddress(zone->addressOfFreeListFirst(allocKind)), result);
    branchPtr(Assembler::BelowOrEqual, AbsoluteAddress(zone->addressOfFreeListLast(allocKind)), result, &fallback);
    computeEffectiveAddress(Address(result, thingSize), temp);
    storePtr(temp, AbsoluteAddress(zone->addressOfFreeListFirst(allocKind)));
    jump(&success);

    bind(&fallback);
    // If there are no FreeSpans left, we bail to finish the allocation. The
    // interpreter will call |refillFreeLists|, setting up a new FreeList so
    // that we can continue allocating in the jit.
    branchPtr(Assembler::Equal, result, ImmPtr(0), fail);
    // Point the free list head at the subsequent span (which may be empty).
    loadPtr(Address(result, js::gc::FreeSpan::offsetOfFirst()), temp);
    storePtr(temp, AbsoluteAddress(zone->addressOfFreeListFirst(allocKind)));
    loadPtr(Address(result, js::gc::FreeSpan::offsetOfLast()), temp);
    storePtr(temp, AbsoluteAddress(zone->addressOfFreeListLast(allocKind)));

    bind(&success);
}

void
MacroAssembler::callMallocStub(size_t nbytes, Register result, Label* fail)
{
    // This register must match the one in JitRuntime::generateMallocStub.
    const Register regNBytes = CallTempReg0;

    MOZ_ASSERT(nbytes > 0);
    MOZ_ASSERT(nbytes <= INT32_MAX);

    if (regNBytes != result)
        push(regNBytes);
    move32(Imm32(nbytes), regNBytes);
    call(GetJitContext()->runtime->jitRuntime()->mallocStub());
    if (regNBytes != result) {
        movePtr(regNBytes, result);
        pop(regNBytes);
    }
    branchTest32(Assembler::Zero, result, result, fail);
}

void
MacroAssembler::callFreeStub(Register slots)
{
    // This register must match the one in JitRuntime::generateFreeStub.
    const Register regSlots = CallTempReg0;

    push(regSlots);
    movePtr(slots, regSlots);
    call(GetJitContext()->runtime->jitRuntime()->freeStub());
    pop(regSlots);
}

// Inlined equivalent of gc::AllocateObject, without failure case handling.
void
MacroAssembler::allocateObject(Register result, Register slots, gc::AllocKind allocKind,
                               uint32_t nDynamicSlots, gc::InitialHeap initialHeap, Label* fail)
{
    MOZ_ASSERT(allocKind >= gc::FINALIZE_OBJECT0 && allocKind <= gc::FINALIZE_OBJECT_LAST);

    checkAllocatorState(fail);

    if (shouldNurseryAllocate(allocKind, initialHeap))
        return nurseryAllocate(result, slots, allocKind, nDynamicSlots, initialHeap, fail);

    if (!nDynamicSlots)
        return freeListAllocate(result, slots, allocKind, fail);

    callMallocStub(nDynamicSlots * sizeof(HeapValue), slots, fail);

    Label failAlloc;
    Label success;

    push(slots);
    freeListAllocate(result, slots, allocKind, &failAlloc);
    pop(slots);
    jump(&success);

    bind(&failAlloc);
    pop(slots);
    callFreeStub(slots);
    jump(fail);

    breakpoint();
}

void
MacroAssembler::newGCThing(Register result, Register temp, JSObject* templateObj,
                           gc::InitialHeap initialHeap, Label* fail)
{
    // This method does not initialize the object: if external slots get
    // allocated into |temp|, there is no easy way for us to ensure the caller
    // frees them. Instead just assert this case does not happen.
    MOZ_ASSERT_IF(templateObj->isNative(), !templateObj->as<NativeObject>().numDynamicSlots());

    gc::AllocKind allocKind = templateObj->asTenured().getAllocKind();
    MOZ_ASSERT(allocKind >= gc::FINALIZE_OBJECT0 && allocKind <= gc::FINALIZE_OBJECT_LAST);

    allocateObject(result, temp, allocKind, 0, initialHeap, fail);
}

void
MacroAssembler::createGCObject(Register obj, Register temp, JSObject* templateObj,
                               gc::InitialHeap initialHeap, Label* fail, bool initFixedSlots)
{
    gc::AllocKind allocKind = templateObj->asTenured().getAllocKind();
    MOZ_ASSERT(allocKind >= gc::FINALIZE_OBJECT0 && allocKind <= gc::FINALIZE_OBJECT_LAST);

    uint32_t nDynamicSlots = 0;
    if (templateObj->isNative()) {
        nDynamicSlots = templateObj->as<NativeObject>().numDynamicSlots();

        // Arrays with copy on write elements do not need fixed space for an
        // elements header. The template object, which owns the original
        // elements, might have another allocation kind.
        if (templateObj->as<NativeObject>().denseElementsAreCopyOnWrite())
            allocKind = gc::FINALIZE_OBJECT0_BACKGROUND;
    }

    allocateObject(obj, temp, allocKind, nDynamicSlots, initialHeap, fail);
    initGCThing(obj, temp, templateObj, initFixedSlots);
}


// Inlined equivalent of gc::AllocateNonObject, without failure case handling.
// Non-object allocation does not need to worry about slots, so can take a
// simpler path.
void
MacroAssembler::allocateNonObject(Register result, Register temp, gc::AllocKind allocKind, Label* fail)
{
    checkAllocatorState(fail);
    freeListAllocate(result, temp, allocKind, fail);
}

void
MacroAssembler::newGCString(Register result, Register temp, Label* fail)
{
    allocateNonObject(result, temp, js::gc::FINALIZE_STRING, fail);
}

void
MacroAssembler::newGCFatInlineString(Register result, Register temp, Label* fail)
{
    allocateNonObject(result, temp, js::gc::FINALIZE_FAT_INLINE_STRING, fail);
}

void
MacroAssembler::copySlotsFromTemplate(Register obj, const NativeObject* templateObj,
                                      uint32_t start, uint32_t end)
{
    uint32_t nfixed = Min(templateObj->numFixedSlots(), end);
    for (unsigned i = start; i < nfixed; i++)
        storeValue(templateObj->getFixedSlot(i), Address(obj, NativeObject::getFixedSlotOffset(i)));
}

void
MacroAssembler::fillSlotsWithConstantValue(Address base, Register temp,
                                           uint32_t start, uint32_t end, const Value& v)
{
    MOZ_ASSERT(v.isUndefined() || IsUninitializedLexical(v));

    if (start >= end)
        return;

#ifdef JS_NUNBOX32
    // We only have a single spare register, so do the initialization as two
    // strided writes of the tag and body.
    jsval_layout jv = JSVAL_TO_IMPL(v);

    Address addr = base;
    move32(Imm32(jv.s.payload.i32), temp);
    for (unsigned i = start; i < end; ++i, addr.offset += sizeof(HeapValue))
        store32(temp, ToPayload(addr));

    addr = base;
    move32(Imm32(jv.s.tag), temp);
    for (unsigned i = start; i < end; ++i, addr.offset += sizeof(HeapValue))
        store32(temp, ToType(addr));
#else
    moveValue(v, temp);
    for (uint32_t i = start; i < end; ++i, base.offset += sizeof(HeapValue))
        storePtr(temp, base);
#endif
}

void
MacroAssembler::fillSlotsWithUndefined(Address base, Register temp, uint32_t start, uint32_t end)
{
    fillSlotsWithConstantValue(base, temp, start, end, UndefinedValue());
}

void
MacroAssembler::fillSlotsWithUninitialized(Address base, Register temp, uint32_t start, uint32_t end)
{
    fillSlotsWithConstantValue(base, temp, start, end, MagicValue(JS_UNINITIALIZED_LEXICAL));
}

static void
FindStartOfUndefinedAndUninitializedSlots(NativeObject* templateObj, uint32_t nslots,
                                          uint32_t* startOfUndefined, uint32_t* startOfUninitialized)
{
    MOZ_ASSERT(nslots == templateObj->lastProperty()->slotSpan(templateObj->getClass()));
    MOZ_ASSERT(nslots > 0);
    uint32_t first = nslots;
    for (; first != 0; --first) {
        if (!IsUninitializedLexical(templateObj->getSlot(first - 1)))
            break;
    }
    *startOfUninitialized = first;
    for (; first != 0; --first) {
        if (templateObj->getSlot(first - 1) != UndefinedValue()) {
            *startOfUndefined = first;
            return;
        }
    }
    *startOfUndefined = 0;
}

void
MacroAssembler::initGCSlots(Register obj, Register slots, NativeObject* templateObj,
                            bool initFixedSlots)
{
    // Slots of non-array objects are required to be initialized.
    // Use the values currently in the template object.
    uint32_t nslots = templateObj->lastProperty()->slotSpan(templateObj->getClass());
    if (nslots == 0)
        return;

    uint32_t nfixed = templateObj->numUsedFixedSlots();
    uint32_t ndynamic = templateObj->numDynamicSlots();

    // Attempt to group slot writes such that we minimize the amount of
    // duplicated data we need to embed in code and load into registers. In
    // general, most template object slots will be undefined except for any
    // reserved slots. Since reserved slots come first, we split the object
    // logically into independent non-UndefinedValue writes to the head and
    // duplicated writes of UndefinedValue to the tail. For the majority of
    // objects, the "tail" will be the entire slot range.
    //
    // The template object may be a CallObject, in which case we need to
    // account for uninitialized lexical slots as well as undefined
    // slots. Unitialized lexical slots always appear at the very end of
    // slots, after undefined.
    uint32_t startOfUndefined = nslots;
    uint32_t startOfUninitialized = nslots;
    FindStartOfUndefinedAndUninitializedSlots(templateObj, nslots,
                                              &startOfUndefined, &startOfUninitialized);
    MOZ_ASSERT(startOfUndefined <= nfixed); // Reserved slots must be fixed.
    MOZ_ASSERT_IF(startOfUndefined != nfixed, startOfUndefined <= startOfUninitialized);
    MOZ_ASSERT_IF(!templateObj->is<CallObject>(), startOfUninitialized == nslots);

    // Copy over any preserved reserved slots.
    copySlotsFromTemplate(obj, templateObj, 0, startOfUndefined);

    // Fill the rest of the fixed slots with undefined and uninitialized.
    if (initFixedSlots) {
        fillSlotsWithUndefined(Address(obj, NativeObject::getFixedSlotOffset(startOfUndefined)), slots,
                               startOfUndefined, Min(startOfUninitialized, nfixed));
        size_t offset = NativeObject::getFixedSlotOffset(startOfUninitialized);
        fillSlotsWithUninitialized(Address(obj, offset), slots, startOfUninitialized, nfixed);
    }

    if (ndynamic) {
        // We are short one register to do this elegantly. Borrow the obj
        // register briefly for our slots base address.
        push(obj);
        loadPtr(Address(obj, NativeObject::offsetOfSlots()), obj);

        // Initially fill all dynamic slots with undefined.
        fillSlotsWithUndefined(Address(obj, 0), slots, 0, ndynamic);

        // Fill uninitialized slots if necessary.
        fillSlotsWithUninitialized(Address(obj, 0), slots, startOfUninitialized - nfixed,
                                   nslots - startOfUninitialized);

        pop(obj);
    }
}

void
MacroAssembler::initGCThing(Register obj, Register slots, JSObject* templateObj,
                            bool initFixedSlots)
{
    // Fast initialization of an empty object returned by allocateObject().

    storePtr(ImmGCPtr(templateObj->lastProperty()), Address(obj, JSObject::offsetOfShape()));
    storePtr(ImmGCPtr(templateObj->group()), Address(obj, JSObject::offsetOfGroup()));

    if (templateObj->isNative()) {
        NativeObject* ntemplate = &templateObj->as<NativeObject>();
        MOZ_ASSERT_IF(!ntemplate->denseElementsAreCopyOnWrite(), !ntemplate->hasDynamicElements());

        if (ntemplate->hasDynamicSlots())
            storePtr(slots, Address(obj, NativeObject::offsetOfSlots()));
        else
            storePtr(ImmPtr(nullptr), Address(obj, NativeObject::offsetOfSlots()));

        if (ntemplate->denseElementsAreCopyOnWrite()) {
            storePtr(ImmPtr((const Value*) ntemplate->getDenseElements()),
                     Address(obj, NativeObject::offsetOfElements()));
        } else if (ntemplate->is<ArrayObject>()) {
            Register temp = slots;
            int elementsOffset = NativeObject::offsetOfFixedElements();

            computeEffectiveAddress(Address(obj, elementsOffset), temp);
            storePtr(temp, Address(obj, NativeObject::offsetOfElements()));

            // Fill in the elements header.
            store32(Imm32(ntemplate->getDenseCapacity()),
                    Address(obj, elementsOffset + ObjectElements::offsetOfCapacity()));
            store32(Imm32(ntemplate->getDenseInitializedLength()),
                    Address(obj, elementsOffset + ObjectElements::offsetOfInitializedLength()));
            store32(Imm32(ntemplate->as<ArrayObject>().length()),
                    Address(obj, elementsOffset + ObjectElements::offsetOfLength()));
            store32(Imm32(ntemplate->shouldConvertDoubleElements()
                          ? ObjectElements::CONVERT_DOUBLE_ELEMENTS
                          : 0),
                    Address(obj, elementsOffset + ObjectElements::offsetOfFlags()));
            MOZ_ASSERT(!ntemplate->hasPrivate());
        } else {
            storePtr(ImmPtr(emptyObjectElements), Address(obj, NativeObject::offsetOfElements()));

            initGCSlots(obj, slots, ntemplate, initFixedSlots);

            if (ntemplate->hasPrivate()) {
                uint32_t nfixed = ntemplate->numFixedSlots();
                storePtr(ImmPtr(ntemplate->getPrivate()),
                         Address(obj, NativeObject::getPrivateDataOffset(nfixed)));
            }
        }
    } else if (templateObj->is<InlineTypedObject>()) {
        size_t nbytes = templateObj->as<InlineTypedObject>().size();
        const uint8_t* memory = templateObj->as<InlineTypedObject>().inlineTypedMem();

        // Memcpy the contents of the template object to the new object.
        size_t offset = 0;
        while (nbytes) {
            uintptr_t value = *(uintptr_t*)(memory + offset);
            storePtr(ImmWord(value),
                     Address(obj, InlineTypedObject::offsetOfDataStart() + offset));
            nbytes = (nbytes < sizeof(uintptr_t)) ? 0 : nbytes - sizeof(uintptr_t);
            offset += sizeof(uintptr_t);
        }
    } else if (templateObj->is<UnboxedPlainObject>()) {
        const UnboxedLayout& layout = templateObj->as<UnboxedPlainObject>().layout();

        // Initialize reference fields of the object, per UnboxedPlainObject::create.
        if (const int32_t* list = layout.traceList()) {
            while (*list != -1) {
                storePtr(ImmGCPtr(GetJitContext()->runtime->names().empty),
                         Address(obj, UnboxedPlainObject::offsetOfData() + *list));
                list++;
            }
            list++;
            while (*list != -1) {
                storePtr(ImmWord(0),
                         Address(obj, UnboxedPlainObject::offsetOfData() + *list));
                list++;
            }
            // Unboxed objects don't have Values to initialize.
            MOZ_ASSERT(*(list + 1) == -1);
        }
    } else {
        MOZ_CRASH("Unknown object");
    }

#ifdef JS_GC_TRACE
    RegisterSet regs = RegisterSet::Volatile();
    PushRegsInMask(regs);
    regs.takeUnchecked(obj);
    Register temp = regs.takeGeneral();

    setupUnalignedABICall(2, temp);
    passABIArg(obj);
    movePtr(ImmGCPtr(templateObj->type()), temp);
    passABIArg(temp);
    callWithABI(JS_FUNC_TO_DATA_PTR(void*, js::gc::TraceCreateObject));

    PopRegsInMask(RegisterSet::Volatile());
#endif
}

void
MacroAssembler::compareStrings(JSOp op, Register left, Register right, Register result,
                               Label* fail)
{
    MOZ_ASSERT(IsEqualityOp(op));

    Label done;
    Label notPointerEqual;
    // Fast path for identical strings.
    branchPtr(Assembler::NotEqual, left, right, &notPointerEqual);
    move32(Imm32(op == JSOP_EQ || op == JSOP_STRICTEQ), result);
    jump(&done);

    bind(&notPointerEqual);

    Label notAtom;
    // Optimize the equality operation to a pointer compare for two atoms.
    Imm32 atomBit(JSString::ATOM_BIT);
    branchTest32(Assembler::Zero, Address(left, JSString::offsetOfFlags()), atomBit, &notAtom);
    branchTest32(Assembler::Zero, Address(right, JSString::offsetOfFlags()), atomBit, &notAtom);

    cmpPtrSet(JSOpToCondition(MCompare::Compare_String, op), left, right, result);
    jump(&done);

    bind(&notAtom);
    // Strings of different length can never be equal.
    loadStringLength(left, result);
    branch32(Assembler::Equal, Address(right, JSString::offsetOfLength()), result, fail);
    move32(Imm32(op == JSOP_NE || op == JSOP_STRICTNE), result);

    bind(&done);
}

void
MacroAssembler::loadStringChars(Register str, Register dest)
{
    Label isInline, done;
    branchTest32(Assembler::NonZero, Address(str, JSString::offsetOfFlags()),
                 Imm32(JSString::INLINE_CHARS_BIT), &isInline);

    loadPtr(Address(str, JSString::offsetOfNonInlineChars()), dest);
    jump(&done);

    bind(&isInline);
    computeEffectiveAddress(Address(str, JSInlineString::offsetOfInlineStorage()), dest);

    bind(&done);
}

void
MacroAssembler::loadStringChar(Register str, Register index, Register output)
{
    MOZ_ASSERT(str != output);
    MOZ_ASSERT(index != output);

    loadStringChars(str, output);

    Label isLatin1, done;
    branchTest32(Assembler::NonZero, Address(str, JSString::offsetOfFlags()),
                 Imm32(JSString::LATIN1_CHARS_BIT), &isLatin1);
    load16ZeroExtend(BaseIndex(output, index, TimesTwo), output);
    jump(&done);

    bind(&isLatin1);
    load8ZeroExtend(BaseIndex(output, index, TimesOne), output);

    bind(&done);
}

// Save an exit frame (which must be aligned to the stack pointer) to
// PerThreadData::jitTop of the main thread.
void
MacroAssembler::linkExitFrame()
{
    AbsoluteAddress jitTop(GetJitContext()->runtime->addressOfJitTop());
    storePtr(StackPointer, jitTop);
}

static void
ReportOverRecursed(JSContext* cx)
{
    js_ReportOverRecursed(cx);
}

void
MacroAssembler::generateBailoutTail(Register scratch, Register bailoutInfo)
{
    enterExitFrame();

    Label baseline;

    // The return value from Bailout is tagged as:
    // - 0x0: done (enter baseline)
    // - 0x1: error (handle exception)
    // - 0x2: overrecursed
    JS_STATIC_ASSERT(BAILOUT_RETURN_OK == 0);
    JS_STATIC_ASSERT(BAILOUT_RETURN_FATAL_ERROR == 1);
    JS_STATIC_ASSERT(BAILOUT_RETURN_OVERRECURSED == 2);

    branch32(Equal, ReturnReg, Imm32(BAILOUT_RETURN_OK), &baseline);
    branch32(Equal, ReturnReg, Imm32(BAILOUT_RETURN_FATAL_ERROR), exceptionLabel());

    // Fall-through: overrecursed.
    {
        loadJSContext(ReturnReg);
        setupUnalignedABICall(1, scratch);
        passABIArg(ReturnReg);
        callWithABI(JS_FUNC_TO_DATA_PTR(void*, ReportOverRecursed));
        jump(exceptionLabel());
    }

    bind(&baseline);
    {
        // Prepare a register set for use in this case.
        GeneralRegisterSet regs(GeneralRegisterSet::All());
        MOZ_ASSERT(!regs.has(BaselineStackReg));
        regs.take(bailoutInfo);

        // Reset SP to the point where clobbering starts.
        loadPtr(Address(bailoutInfo, offsetof(BaselineBailoutInfo, incomingStack)),
                BaselineStackReg);

        Register copyCur = regs.takeAny();
        Register copyEnd = regs.takeAny();
        Register temp = regs.takeAny();

        // Copy data onto stack.
        loadPtr(Address(bailoutInfo, offsetof(BaselineBailoutInfo, copyStackTop)), copyCur);
        loadPtr(Address(bailoutInfo, offsetof(BaselineBailoutInfo, copyStackBottom)), copyEnd);
        {
            Label copyLoop;
            Label endOfCopy;
            bind(&copyLoop);
            branchPtr(Assembler::BelowOrEqual, copyCur, copyEnd, &endOfCopy);
            subPtr(Imm32(4), copyCur);
            subPtr(Imm32(4), BaselineStackReg);
            load32(Address(copyCur, 0), temp);
            store32(temp, Address(BaselineStackReg, 0));
            jump(&copyLoop);
            bind(&endOfCopy);
        }

        // Enter exit frame for the FinishBailoutToBaseline call.
        loadPtr(Address(bailoutInfo, offsetof(BaselineBailoutInfo, resumeFramePtr)), temp);
        load32(Address(temp, BaselineFrame::reverseOffsetOfFrameSize()), temp);
        makeFrameDescriptor(temp, JitFrame_BaselineJS);
        push(temp);
        push(Address(bailoutInfo, offsetof(BaselineBailoutInfo, resumeAddr)));
        // No GC things to mark on the stack, push a bare token.
        enterFakeExitFrame(ExitFrameLayout::BareToken());

        // If monitorStub is non-null, handle resumeAddr appropriately.
        Label noMonitor;
        Label done;
        branchPtr(Assembler::Equal,
                  Address(bailoutInfo, offsetof(BaselineBailoutInfo, monitorStub)),
                  ImmPtr(nullptr),
                  &noMonitor);

        //
        // Resuming into a monitoring stub chain.
        //
        {
            // Save needed values onto stack temporarily.
            pushValue(Address(bailoutInfo, offsetof(BaselineBailoutInfo, valueR0)));
            push(Address(bailoutInfo, offsetof(BaselineBailoutInfo, resumeFramePtr)));
            push(Address(bailoutInfo, offsetof(BaselineBailoutInfo, resumeAddr)));
            push(Address(bailoutInfo, offsetof(BaselineBailoutInfo, monitorStub)));

            // Call a stub to free allocated memory and create arguments objects.
            setupUnalignedABICall(1, temp);
            passABIArg(bailoutInfo);
            callWithABI(JS_FUNC_TO_DATA_PTR(void*, FinishBailoutToBaseline));
            branchTest32(Zero, ReturnReg, ReturnReg, exceptionLabel());

            // Restore values where they need to be and resume execution.
            GeneralRegisterSet enterMonRegs(GeneralRegisterSet::All());
            enterMonRegs.take(R0);
            enterMonRegs.take(BaselineStubReg);
            enterMonRegs.take(BaselineFrameReg);
            enterMonRegs.takeUnchecked(BaselineTailCallReg);

            pop(BaselineStubReg);
            pop(BaselineTailCallReg);
            pop(BaselineFrameReg);
            popValue(R0);

            // Discard exit frame.
            addPtr(Imm32(ExitFrameLayout::SizeWithFooter()), StackPointer);

#if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
            push(BaselineTailCallReg);
#endif
            jump(Address(BaselineStubReg, ICStub::offsetOfStubCode()));
        }

        //
        // Resuming into main jitcode.
        //
        bind(&noMonitor);
        {
            // Save needed values onto stack temporarily.
            pushValue(Address(bailoutInfo, offsetof(BaselineBailoutInfo, valueR0)));
            pushValue(Address(bailoutInfo, offsetof(BaselineBailoutInfo, valueR1)));
            push(Address(bailoutInfo, offsetof(BaselineBailoutInfo, resumeFramePtr)));
            push(Address(bailoutInfo, offsetof(BaselineBailoutInfo, resumeAddr)));

            // Call a stub to free allocated memory and create arguments objects.
            setupUnalignedABICall(1, temp);
            passABIArg(bailoutInfo);
            callWithABI(JS_FUNC_TO_DATA_PTR(void*, FinishBailoutToBaseline));
            branchTest32(Zero, ReturnReg, ReturnReg, exceptionLabel());

            // Restore values where they need to be and resume execution.
            GeneralRegisterSet enterRegs(GeneralRegisterSet::All());
            enterRegs.take(R0);
            enterRegs.take(R1);
            enterRegs.take(BaselineFrameReg);
            Register jitcodeReg = enterRegs.takeAny();

            pop(jitcodeReg);
            pop(BaselineFrameReg);
            popValue(R1);
            popValue(R0);

            // Discard exit frame.
            addPtr(Imm32(ExitFrameLayout::SizeWithFooter()), StackPointer);

            jump(jitcodeReg);
        }
    }
}

void
MacroAssembler::loadBaselineOrIonRaw(Register script, Register dest, Label* failure)
{
    loadPtr(Address(script, JSScript::offsetOfBaselineOrIonRaw()), dest);
    if (failure)
        branchTestPtr(Assembler::Zero, dest, dest, failure);
}

void
MacroAssembler::loadBaselineOrIonNoArgCheck(Register script, Register dest, Label* failure)
{
    loadPtr(Address(script, JSScript::offsetOfBaselineOrIonSkipArgCheck()), dest);
    if (failure)
        branchTestPtr(Assembler::Zero, dest, dest, failure);
}

void
MacroAssembler::loadBaselineFramePtr(Register framePtr, Register dest)
{
    if (framePtr != dest)
        movePtr(framePtr, dest);
    subPtr(Imm32(BaselineFrame::Size()), dest);
}

void
MacroAssembler::handleFailure()
{
    // Re-entry code is irrelevant because the exception will leave the
    // running function and never come back
    JitCode* excTail = GetJitContext()->runtime->jitRuntime()->getExceptionTail();
    jump(excTail);
}

#ifdef DEBUG
static void
AssumeUnreachable_(const char* output) {
    MOZ_ReportAssertionFailure(output, __FILE__, __LINE__);
}
#endif

void
MacroAssembler::assumeUnreachable(const char* output)
{
#ifdef DEBUG
    if (!IsCompilingAsmJS()) {
        RegisterSet regs = RegisterSet::Volatile();
        PushRegsInMask(regs);
        Register temp = regs.takeGeneral();

        setupUnalignedABICall(1, temp);
        movePtr(ImmPtr(output), temp);
        passABIArg(temp);
        callWithABI(JS_FUNC_TO_DATA_PTR(void*, AssumeUnreachable_));

        PopRegsInMask(RegisterSet::Volatile());
    }
#endif

    breakpoint();
}

template<typename T>
void
MacroAssembler::assertTestInt32(Condition cond, const T& value, const char* output)
{
#ifdef DEBUG
    Label ok;
    branchTestInt32(cond, value, &ok);
    assumeUnreachable(output);
    bind(&ok);
#endif
}

template void MacroAssembler::assertTestInt32(Condition, const Address&, const char*);

static void
Printf0_(const char* output) {
    // Use stderr instead of stdout because this is only used for debug
    // output. stderr is less likely to interfere with the program's normal
    // output, and it's always unbuffered.
    fprintf(stderr, "%s", output);
}

void
MacroAssembler::printf(const char* output)
{
    RegisterSet regs = RegisterSet::Volatile();
    PushRegsInMask(regs);

    Register temp = regs.takeGeneral();

    setupUnalignedABICall(1, temp);
    movePtr(ImmPtr(output), temp);
    passABIArg(temp);
    callWithABI(JS_FUNC_TO_DATA_PTR(void*, Printf0_));

    PopRegsInMask(RegisterSet::Volatile());
}

static void
Printf1_(const char* output, uintptr_t value) {
    char* line = JS_sprintf_append(nullptr, output, value);
    fprintf(stderr, "%s", line);
    js_free(line);
}

void
MacroAssembler::printf(const char* output, Register value)
{
    RegisterSet regs = RegisterSet::Volatile();
    PushRegsInMask(regs);

    regs.takeUnchecked(value);

    Register temp = regs.takeGeneral();

    setupUnalignedABICall(2, temp);
    movePtr(ImmPtr(output), temp);
    passABIArg(temp);
    passABIArg(value);
    callWithABI(JS_FUNC_TO_DATA_PTR(void*, Printf1_));

    PopRegsInMask(RegisterSet::Volatile());
}

#ifdef JS_TRACE_LOGGING
void
MacroAssembler::tracelogStartId(Register logger, uint32_t textId, bool force)
{
    if (!force && !TraceLogTextIdEnabled(textId))
        return;

    PushRegsInMask(RegisterSet::Volatile());

    RegisterSet regs = RegisterSet::Volatile();
    regs.takeUnchecked(logger);

    Register temp = regs.takeGeneral();

    setupUnalignedABICall(2, temp);
    passABIArg(logger);
    move32(Imm32(textId), temp);
    passABIArg(temp);
    callWithABI(JS_FUNC_TO_DATA_PTR(void*, TraceLogStartEventPrivate));

    PopRegsInMask(RegisterSet::Volatile());
}

void
MacroAssembler::tracelogStartId(Register logger, Register textId)
{
    PushRegsInMask(RegisterSet::Volatile());

    RegisterSet regs = RegisterSet::Volatile();
    regs.takeUnchecked(logger);
    regs.takeUnchecked(textId);

    Register temp = regs.takeGeneral();

    setupUnalignedABICall(2, temp);
    passABIArg(logger);
    passABIArg(textId);
    callWithABI(JS_FUNC_TO_DATA_PTR(void*, TraceLogStartEventPrivate));

    PopRegsInMask(RegisterSet::Volatile());
}

void
MacroAssembler::tracelogStartEvent(Register logger, Register event)
{
    void (&TraceLogFunc)(TraceLoggerThread*, const TraceLoggerEvent&) = TraceLogStartEvent;

    PushRegsInMask(RegisterSet::Volatile());

    RegisterSet regs = RegisterSet::Volatile();
    regs.takeUnchecked(logger);
    regs.takeUnchecked(event);

    Register temp = regs.takeGeneral();

    setupUnalignedABICall(2, temp);
    passABIArg(logger);
    passABIArg(event);
    callWithABI(JS_FUNC_TO_DATA_PTR(void*, TraceLogFunc));

    PopRegsInMask(RegisterSet::Volatile());
}

void
MacroAssembler::tracelogStopId(Register logger, uint32_t textId, bool force)
{
    if (!force && !TraceLogTextIdEnabled(textId))
        return;

    PushRegsInMask(RegisterSet::Volatile());

    RegisterSet regs = RegisterSet::Volatile();
    regs.takeUnchecked(logger);

    Register temp = regs.takeGeneral();

    setupUnalignedABICall(2, temp);
    passABIArg(logger);
    move32(Imm32(textId), temp);
    passABIArg(temp);

    callWithABI(JS_FUNC_TO_DATA_PTR(void*, TraceLogStopEventPrivate));

    PopRegsInMask(RegisterSet::Volatile());
}

void
MacroAssembler::tracelogStopId(Register logger, Register textId)
{
    PushRegsInMask(RegisterSet::Volatile());
    RegisterSet regs = RegisterSet::Volatile();
    regs.takeUnchecked(logger);

    regs.takeUnchecked(textId);

    Register temp = regs.takeGeneral();

    setupUnalignedABICall(2, temp);
    passABIArg(logger);
    passABIArg(textId);
    callWithABI(JS_FUNC_TO_DATA_PTR(void*, TraceLogStopEventPrivate));

    PopRegsInMask(RegisterSet::Volatile());
}
#endif

void
MacroAssembler::convertInt32ValueToDouble(const Address& address, Register scratch, Label* done)
{
    branchTestInt32(Assembler::NotEqual, address, done);
    unboxInt32(address, scratch);
    convertInt32ToDouble(scratch, ScratchDoubleReg);
    storeDouble(ScratchDoubleReg, address);
}

void
MacroAssembler::convertValueToFloatingPoint(ValueOperand value, FloatRegister output,
                                            Label* fail, MIRType outputType)
{
    Register tag = splitTagForTest(value);

    Label isDouble, isInt32, isBool, isNull, done;

    branchTestDouble(Assembler::Equal, tag, &isDouble);
    branchTestInt32(Assembler::Equal, tag, &isInt32);
    branchTestBoolean(Assembler::Equal, tag, &isBool);
    branchTestNull(Assembler::Equal, tag, &isNull);
    branchTestUndefined(Assembler::NotEqual, tag, fail);

    // fall-through: undefined
    loadConstantFloatingPoint(GenericNaN(), float(GenericNaN()), output, outputType);
    jump(&done);

    bind(&isNull);
    loadConstantFloatingPoint(0.0, 0.0f, output, outputType);
    jump(&done);

    bind(&isBool);
    boolValueToFloatingPoint(value, output, outputType);
    jump(&done);

    bind(&isInt32);
    int32ValueToFloatingPoint(value, output, outputType);
    jump(&done);

    bind(&isDouble);
    FloatRegister tmp = output;
    if (outputType == MIRType_Float32 && hasMultiAlias())
        tmp = ScratchDoubleReg;

    unboxDouble(value, tmp);
    if (outputType == MIRType_Float32)
        convertDoubleToFloat32(tmp, output);

    bind(&done);
}

bool
MacroAssembler::convertValueToFloatingPoint(JSContext* cx, const Value& v, FloatRegister output,
                                            Label* fail, MIRType outputType)
{
    if (v.isNumber() || v.isString()) {
        double d;
        if (v.isNumber())
            d = v.toNumber();
        else if (!StringToNumber(cx, v.toString(), &d))
            return false;

        loadConstantFloatingPoint(d, (float)d, output, outputType);
        return true;
    }

    if (v.isBoolean()) {
        if (v.toBoolean())
            loadConstantFloatingPoint(1.0, 1.0f, output, outputType);
        else
            loadConstantFloatingPoint(0.0, 0.0f, output, outputType);
        return true;
    }

    if (v.isNull()) {
        loadConstantFloatingPoint(0.0, 0.0f, output, outputType);
        return true;
    }

    if (v.isUndefined()) {
        loadConstantFloatingPoint(GenericNaN(), float(GenericNaN()), output, outputType);
        return true;
    }

    MOZ_ASSERT(v.isObject());
    jump(fail);
    return true;
}

void
MacroAssembler::PushEmptyRooted(VMFunction::RootType rootType)
{
    switch (rootType) {
      case VMFunction::RootNone:
        MOZ_CRASH("Handle must have root type");
      case VMFunction::RootObject:
      case VMFunction::RootString:
      case VMFunction::RootPropertyName:
      case VMFunction::RootFunction:
      case VMFunction::RootCell:
        Push(ImmPtr(nullptr));
        break;
      case VMFunction::RootValue:
        Push(UndefinedValue());
        break;
    }
}

void
MacroAssembler::popRooted(VMFunction::RootType rootType, Register cellReg,
                          const ValueOperand& valueReg)
{
    switch (rootType) {
      case VMFunction::RootNone:
        MOZ_CRASH("Handle must have root type");
      case VMFunction::RootObject:
      case VMFunction::RootString:
      case VMFunction::RootPropertyName:
      case VMFunction::RootFunction:
      case VMFunction::RootCell:
        Pop(cellReg);
        break;
      case VMFunction::RootValue:
        Pop(valueReg);
        break;
    }
}

bool
MacroAssembler::convertConstantOrRegisterToFloatingPoint(JSContext* cx, ConstantOrRegister src,
                                                         FloatRegister output, Label* fail,
                                                         MIRType outputType)
{
    if (src.constant())
        return convertValueToFloatingPoint(cx, src.value(), output, fail, outputType);

    convertTypedOrValueToFloatingPoint(src.reg(), output, fail, outputType);
    return true;
}

void
MacroAssembler::convertTypedOrValueToFloatingPoint(TypedOrValueRegister src, FloatRegister output,
                                                   Label* fail, MIRType outputType)
{
    MOZ_ASSERT(IsFloatingPointType(outputType));

    if (src.hasValue()) {
        convertValueToFloatingPoint(src.valueReg(), output, fail, outputType);
        return;
    }

    bool outputIsDouble = outputType == MIRType_Double;
    switch (src.type()) {
      case MIRType_Null:
        loadConstantFloatingPoint(0.0, 0.0f, output, outputType);
        break;
      case MIRType_Boolean:
      case MIRType_Int32:
        convertInt32ToFloatingPoint(src.typedReg().gpr(), output, outputType);
        break;
      case MIRType_Float32:
        if (outputIsDouble) {
            convertFloat32ToDouble(src.typedReg().fpu(), output);
        } else {
            if (src.typedReg().fpu() != output)
                moveFloat32(src.typedReg().fpu(), output);
        }
        break;
      case MIRType_Double:
        if (outputIsDouble) {
            if (src.typedReg().fpu() != output)
                moveDouble(src.typedReg().fpu(), output);
        } else {
            convertDoubleToFloat32(src.typedReg().fpu(), output);
        }
        break;
      case MIRType_Object:
      case MIRType_String:
      case MIRType_Symbol:
        jump(fail);
        break;
      case MIRType_Undefined:
        loadConstantFloatingPoint(GenericNaN(), float(GenericNaN()), output, outputType);
        break;
      default:
        MOZ_CRASH("Bad MIRType");
    }
}

void
MacroAssembler::convertDoubleToInt(FloatRegister src, Register output, FloatRegister temp,
                                   Label* truncateFail, Label* fail,
                                   IntConversionBehavior behavior)
{
    switch (behavior) {
      case IntConversion_Normal:
      case IntConversion_NegativeZeroCheck:
        convertDoubleToInt32(src, output, fail, behavior == IntConversion_NegativeZeroCheck);
        break;
      case IntConversion_Truncate:
        branchTruncateDouble(src, output, truncateFail ? truncateFail : fail);
        break;
      case IntConversion_ClampToUint8:
        // Clamping clobbers the input register, so use a temp.
        moveDouble(src, temp);
        clampDoubleToUint8(temp, output);
        break;
    }
}

void
MacroAssembler::convertValueToInt(ValueOperand value, MDefinition* maybeInput,
                                  Label* handleStringEntry, Label* handleStringRejoin,
                                  Label* truncateDoubleSlow,
                                  Register stringReg, FloatRegister temp, Register output,
                                  Label* fail, IntConversionBehavior behavior,
                                  IntConversionInputKind conversion)
{
    Register tag = splitTagForTest(value);
    bool handleStrings = (behavior == IntConversion_Truncate ||
                          behavior == IntConversion_ClampToUint8) &&
                         handleStringEntry &&
                         handleStringRejoin;

    MOZ_ASSERT_IF(handleStrings, conversion == IntConversion_Any);

    Label done, isInt32, isBool, isDouble, isNull, isString;

    branchEqualTypeIfNeeded(MIRType_Int32, maybeInput, tag, &isInt32);
    if (conversion == IntConversion_Any || conversion == IntConversion_NumbersOrBoolsOnly)
        branchEqualTypeIfNeeded(MIRType_Boolean, maybeInput, tag, &isBool);
    branchEqualTypeIfNeeded(MIRType_Double, maybeInput, tag, &isDouble);

    if (conversion == IntConversion_Any) {
        // If we are not truncating, we fail for anything that's not
        // null. Otherwise we might be able to handle strings and objects.
        switch (behavior) {
          case IntConversion_Normal:
          case IntConversion_NegativeZeroCheck:
            branchTestNull(Assembler::NotEqual, tag, fail);
            break;

          case IntConversion_Truncate:
          case IntConversion_ClampToUint8:
            branchEqualTypeIfNeeded(MIRType_Null, maybeInput, tag, &isNull);
            if (handleStrings)
                branchEqualTypeIfNeeded(MIRType_String, maybeInput, tag, &isString);
            branchEqualTypeIfNeeded(MIRType_Object, maybeInput, tag, fail);
            branchTestUndefined(Assembler::NotEqual, tag, fail);
            break;
        }
    } else {
        jump(fail);
    }

    // The value is null or undefined in truncation contexts - just emit 0.
    if (isNull.used())
        bind(&isNull);
    mov(ImmWord(0), output);
    jump(&done);

    // Try converting a string into a double, then jump to the double case.
    if (handleStrings) {
        bind(&isString);
        unboxString(value, stringReg);
        jump(handleStringEntry);
    }

    // Try converting double into integer.
    if (isDouble.used() || handleStrings) {
        if (isDouble.used()) {
            bind(&isDouble);
            unboxDouble(value, temp);
        }

        if (handleStrings)
            bind(handleStringRejoin);

        convertDoubleToInt(temp, output, temp, truncateDoubleSlow, fail, behavior);
        jump(&done);
    }

    // Just unbox a bool, the result is 0 or 1.
    if (isBool.used()) {
        bind(&isBool);
        unboxBoolean(value, output);
        jump(&done);
    }

    // Integers can be unboxed.
    if (isInt32.used()) {
        bind(&isInt32);
        unboxInt32(value, output);
        if (behavior == IntConversion_ClampToUint8)
            clampIntToUint8(output);
    }

    bind(&done);
}

bool
MacroAssembler::convertValueToInt(JSContext* cx, const Value& v, Register output, Label* fail,
                                  IntConversionBehavior behavior)
{
    bool handleStrings = (behavior == IntConversion_Truncate ||
                          behavior == IntConversion_ClampToUint8);

    if (v.isNumber() || (handleStrings && v.isString())) {
        double d;
        if (v.isNumber())
            d = v.toNumber();
        else if (!StringToNumber(cx, v.toString(), &d))
            return false;

        switch (behavior) {
          case IntConversion_Normal:
          case IntConversion_NegativeZeroCheck: {
            // -0 is checked anyways if we have a constant value.
            int i;
            if (mozilla::NumberIsInt32(d, &i))
                move32(Imm32(i), output);
            else
                jump(fail);
            break;
          }
          case IntConversion_Truncate:
            move32(Imm32(ToInt32(d)), output);
            break;
          case IntConversion_ClampToUint8:
            move32(Imm32(ClampDoubleToUint8(d)), output);
            break;
        }

        return true;
    }

    if (v.isBoolean()) {
        move32(Imm32(v.toBoolean() ? 1 : 0), output);
        return true;
    }

    if (v.isNull() || v.isUndefined()) {
        move32(Imm32(0), output);
        return true;
    }

    MOZ_ASSERT(v.isObject());

    jump(fail);
    return true;
}

bool
MacroAssembler::convertConstantOrRegisterToInt(JSContext* cx, ConstantOrRegister src,
                                               FloatRegister temp, Register output,
                                               Label* fail, IntConversionBehavior behavior)
{
    if (src.constant())
        return convertValueToInt(cx, src.value(), output, fail, behavior);

    convertTypedOrValueToInt(src.reg(), temp, output, fail, behavior);
    return true;
}

void
MacroAssembler::convertTypedOrValueToInt(TypedOrValueRegister src, FloatRegister temp,
                                         Register output, Label* fail,
                                         IntConversionBehavior behavior)
{
    if (src.hasValue()) {
        convertValueToInt(src.valueReg(), temp, output, fail, behavior);
        return;
    }

    switch (src.type()) {
      case MIRType_Undefined:
      case MIRType_Null:
        move32(Imm32(0), output);
        break;
      case MIRType_Boolean:
      case MIRType_Int32:
        if (src.typedReg().gpr() != output)
            move32(src.typedReg().gpr(), output);
        if (src.type() == MIRType_Int32 && behavior == IntConversion_ClampToUint8)
            clampIntToUint8(output);
        break;
      case MIRType_Double:
        convertDoubleToInt(src.typedReg().fpu(), output, temp, nullptr, fail, behavior);
        break;
      case MIRType_Float32:
        // Conversion to Double simplifies implementation at the expense of performance.
        convertFloat32ToDouble(src.typedReg().fpu(), temp);
        convertDoubleToInt(temp, output, temp, nullptr, fail, behavior);
        break;
      case MIRType_String:
      case MIRType_Symbol:
      case MIRType_Object:
        jump(fail);
        break;
      default:
        MOZ_CRASH("Bad MIRType");
    }
}

void
MacroAssembler::finish()
{
    if (failureLabel_.used()) {
        bind(&failureLabel_);
        handleFailure();
    }

    MacroAssemblerSpecific::finish();
}

void
MacroAssembler::link(JitCode* code)
{
    MOZ_ASSERT(!oom());
    // If this code can transition to C++ code and witness a GC, then we need to store
    // the JitCode onto the stack in order to GC it correctly.  exitCodePatch should
    // be unset if the code never needed to push its JitCode*.
    if (hasEnteredExitFrame()) {
        exitCodePatch_.fixup(this);
        PatchDataWithValueCheck(CodeLocationLabel(code, exitCodePatch_),
                                ImmPtr(code),
                                ImmPtr((void*)-1));
    }

    // Fix up the code pointers to be written for locations where profilerCallSite
    // emitted moves of RIP to a register.
    for (size_t i = 0; i < profilerCallSites_.length(); i++) {
        CodeOffsetLabel offset = profilerCallSites_[i];
        offset.fixup(this);
        CodeLocationLabel location(code, offset);
        PatchDataWithValueCheck(location, ImmPtr(location.raw()), ImmPtr((void*)-1));
    }
}

void
MacroAssembler::branchIfNotInterpretedConstructor(Register fun, Register scratch, Label* label)
{
    // 16-bit loads are slow and unaligned 32-bit loads may be too so
    // perform an aligned 32-bit load and adjust the bitmask accordingly.
    MOZ_ASSERT(JSFunction::offsetOfNargs() % sizeof(uint32_t) == 0);
    MOZ_ASSERT(JSFunction::offsetOfFlags() == JSFunction::offsetOfNargs() + 2);

    // Emit code for the following test:
    //
    // bool isInterpretedConstructor() const {
    //     return isInterpreted() && !isFunctionPrototype() && !isArrow() &&
    //         (!isSelfHostedBuiltin() || isSelfHostedConstructor());
    // }

    // First, ensure it's a scripted function.
    load32(Address(fun, JSFunction::offsetOfNargs()), scratch);
    int32_t bits = IMM32_16ADJ(JSFunction::INTERPRETED);
    branchTest32(Assembler::Zero, scratch, Imm32(bits), label);

    // Common case: if IS_FUN_PROTO, ARROW and SELF_HOSTED are not set,
    // the function is an interpreted constructor and we're done.
    Label done;
    bits = IMM32_16ADJ( (JSFunction::IS_FUN_PROTO | JSFunction::ARROW | JSFunction::SELF_HOSTED) );
    branchTest32(Assembler::Zero, scratch, Imm32(bits), &done);
    {
        // The callee is either Function.prototype, an arrow function or
        // self-hosted. None of these are constructible, except self-hosted
        // constructors, so branch to |label| if SELF_HOSTED_CTOR is not set.
        bits = IMM32_16ADJ(JSFunction::SELF_HOSTED_CTOR);
        branchTest32(Assembler::Zero, scratch, Imm32(bits), label);

#ifdef DEBUG
        bits = IMM32_16ADJ(JSFunction::IS_FUN_PROTO);
        branchTest32(Assembler::Zero, scratch, Imm32(bits), &done);
        assumeUnreachable("Function.prototype should not have the SELF_HOSTED_CTOR flag");
#endif
    }
    bind(&done);
}

void
MacroAssembler::branchEqualTypeIfNeeded(MIRType type, MDefinition* maybeDef, Register tag,
                                        Label* label)
{
    if (!maybeDef || maybeDef->mightBeType(type)) {
        switch (type) {
          case MIRType_Null:
            branchTestNull(Equal, tag, label);
            break;
          case MIRType_Boolean:
            branchTestBoolean(Equal, tag, label);
            break;
          case MIRType_Int32:
            branchTestInt32(Equal, tag, label);
            break;
          case MIRType_Double:
            branchTestDouble(Equal, tag, label);
            break;
          case MIRType_String:
            branchTestString(Equal, tag, label);
            break;
          case MIRType_Symbol:
            branchTestSymbol(Equal, tag, label);
            break;
          case MIRType_Object:
            branchTestObject(Equal, tag, label);
            break;
          default:
            MOZ_CRASH("Unsupported type");
        }
    }
}

void
MacroAssembler::profilerPreCallImpl()
{
    Register reg = CallTempReg0;
    Register reg2 = CallTempReg1;
    push(reg);
    push(reg2);
    profilerPreCallImpl(reg, reg2);
    pop(reg2);
    pop(reg);
}

void
MacroAssembler::profilerPreCallImpl(Register reg, Register reg2)
{
    JitContext* icx = GetJitContext();
    AbsoluteAddress profilingActivation(icx->runtime->addressOfProfilingActivation());

    CodeOffsetLabel label = movWithPatch(ImmWord(uintptr_t(-1)), reg);
    loadPtr(profilingActivation, reg2);
    storePtr(reg, Address(reg2, JitActivation::offsetOfLastProfilingCallSite()));

    appendProfilerCallSite(label);
}

void
MacroAssembler::alignJitStackBasedOnNArgs(Register nargs)
{
    const uint32_t alignment = JitStackAlignment / sizeof(Value);
    if (alignment == 1)
        return;

    // A JitFrameLayout is composed of the following:
    // [padding?] [argN] .. [arg1] [this] [[argc] [callee] [descr] [raddr]]
    //
    // We want to ensure that the |raddr| address is aligned.
    // Which implies that we want to ensure that |this| is aligned.
    static_assert(sizeof(JitFrameLayout) % JitStackAlignment == 0,
      "No need to consider the JitFrameLayout for aligning the stack");

    // Which implies that |argN| is aligned if |nargs| is even, and offset by
    // |sizeof(Value)| if |nargs| is odd.
    MOZ_ASSERT(alignment == 2);

    // Thus the |padding| is offset by |sizeof(Value)| if |nargs| is even, and
    // aligned if |nargs| is odd.

    // if (nargs % 2 == 0) {
    //     if (sp % JitStackAlignment == 0)
    //         sp -= sizeof(Value);
    //     MOZ_ASSERT(sp % JitStackAlignment == JitStackAlignment - sizeof(Value));
    // } else {
    //     sp = sp & ~(JitStackAlignment - 1);
    // }
    Label odd, end;
    Label* maybeAssert = &end;
#ifdef DEBUG
    Label assert;
    maybeAssert = &assert;
#endif
    assertStackAlignment(sizeof(Value), 0);
    branchTestPtr(Assembler::NonZero, nargs, Imm32(1), &odd);
    branchTestPtr(Assembler::NonZero, StackPointer, Imm32(JitStackAlignment - 1), maybeAssert);
    subPtr(Imm32(sizeof(Value)), StackPointer);
#ifdef DEBUG
    bind(&assert);
#endif
    assertStackAlignment(JitStackAlignment, sizeof(Value));
    jump(&end);
    bind(&odd);
    andPtr(Imm32(~(JitStackAlignment - 1)), StackPointer);
    bind(&end);
}

void
MacroAssembler::alignJitStackBasedOnNArgs(uint32_t nargs)
{
    const uint32_t alignment = JitStackAlignment / sizeof(Value);
    if (alignment == 1)
        return;

    // A JitFrameLayout is composed of the following:
    // [padding?] [argN] .. [arg1] [this] [[argc] [callee] [descr] [raddr]]
    //
    // We want to ensure that the |raddr| address is aligned.
    // Which implies that we want to ensure that |this| is aligned.
    static_assert(sizeof(JitFrameLayout) % JitStackAlignment == 0,
      "No need to consider the JitFrameLayout for aligning the stack");

    // Which implies that |argN| is aligned if |nargs| is even, and offset by
    // |sizeof(Value)| if |nargs| is odd.
    MOZ_ASSERT(alignment == 2);

    // Thus the |padding| is offset by |sizeof(Value)| if |nargs| is even, and
    // aligned if |nargs| is odd.

    assertStackAlignment(sizeof(Value), 0);
    if (nargs % 2 == 0) {
        Label end;
        branchTestPtr(Assembler::NonZero, StackPointer, Imm32(JitStackAlignment - 1), &end);
        subPtr(Imm32(sizeof(Value)), StackPointer);
        bind(&end);
        assertStackAlignment(JitStackAlignment, sizeof(Value));
    } else {
        andPtr(Imm32(~(JitStackAlignment - 1)), StackPointer);
    }
}
