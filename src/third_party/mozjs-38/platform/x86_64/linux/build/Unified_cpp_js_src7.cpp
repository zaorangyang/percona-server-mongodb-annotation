#define MOZ_UNIFIED_BUILD
#include "jit/x64/Assembler-x64.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jit/x64/Assembler-x64.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jit/x64/Assembler-x64.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jit/x64/Bailouts-x64.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jit/x64/Bailouts-x64.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jit/x64/Bailouts-x64.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jit/x64/BaselineCompiler-x64.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jit/x64/BaselineCompiler-x64.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jit/x64/BaselineCompiler-x64.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jit/x64/BaselineIC-x64.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jit/x64/BaselineIC-x64.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jit/x64/BaselineIC-x64.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jit/x64/CodeGenerator-x64.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jit/x64/CodeGenerator-x64.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jit/x64/CodeGenerator-x64.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jit/x64/Lowering-x64.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jit/x64/Lowering-x64.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jit/x64/Lowering-x64.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jit/x64/MacroAssembler-x64.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jit/x64/MacroAssembler-x64.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jit/x64/MacroAssembler-x64.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jit/x64/Trampoline-x64.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jit/x64/Trampoline-x64.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jit/x64/Trampoline-x64.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jsalloc.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jsalloc.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jsalloc.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jsapi.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jsapi.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jsapi.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jsbool.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jsbool.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jsbool.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jscntxt.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jscntxt.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jscntxt.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jscompartment.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jscompartment.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jscompartment.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jsdate.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jsdate.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jsdate.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jsdtoa.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jsdtoa.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jsdtoa.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif
#include "jsexn.cpp"
#ifdef PL_ARENA_CONST_ALIGN_MASK
#error "jsexn.cpp uses PL_ARENA_CONST_ALIGN_MASK, so it cannot be built in unified mode."
#undef PL_ARENA_CONST_ALIGN_MASK
#endif
#ifdef INITGUID
#error "jsexn.cpp defines INITGUID, so it cannot be built in unified mode."
#undef INITGUID
#endif