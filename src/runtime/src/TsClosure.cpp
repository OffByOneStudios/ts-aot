#include "../include/TsClosure.h"
#include "../include/TsObject.h"
#include "../include/TsRuntime.h"
#include "../include/TsString.h"
#include "../include/TsMap.h"
#include "../include/TsHashTable.h"
#include "../include/GC.h"
#include "../include/TsGC.h"
#include "../include/TsNanBox.h"
#include <new>
#include <cstdio>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

TsClosure* TsClosure::Create(void* funcPtr, int64_t numCaptures) {
    void* mem = ts_alloc(sizeof(TsClosure));
    TsClosure* closure = new (mem) TsClosure();
    closure->func_ptr = funcPtr;
    closure->num_captures = numCaptures;

    if (numCaptures > 0) {
        // Allocate array for cell pointers
        closure->cells = (TsCell**)ts_alloc(numCaptures * sizeof(TsCell*));
        for (int64_t i = 0; i < numCaptures; ++i) {
            closure->cells[i] = nullptr;
        }
    } else {
        closure->cells = nullptr;
    }

    return closure;
}

extern "C" {

TsClosure* ts_closure_create(void* funcPtr, int64_t numCaptures) {
    return TsClosure::Create(funcPtr, numCaptures);
}

void ts_closure_set_cell(TsClosure* closure, int64_t index, TsCell* cell) {
    if (!closure) return;
    closure->setCell(index, cell);
    // Write barrier: cell pointer stored into closure's cells array
    if (closure->cells && index >= 0 && index < closure->num_captures)
        ts_gc_write_barrier(&closure->cells[index], cell);
}

TsCell* ts_closure_get_cell(TsClosure* closure, int64_t index) {
    if (!closure) return nullptr;
    // Validate closure pointer range (must be a real heap address, not a small int or NaN-boxed)
    uintptr_t addr = (uintptr_t)closure;
    if (addr < 0x10000 || (addr >> 48) != 0) {
        fprintf(stderr, "[BUG] ts_closure_get_cell: closure=%p is NOT a valid pointer (likely corrupt), index=%lld\n",
                (void*)closure, (long long)index);
        fflush(stderr);
        return nullptr;
    }
    // Validate closure magic
    if (closure->magic != 0x434C5352) {
        // PROVENANCE INSTRUMENTATION (TS_CLOSURE_PROVENANCE=1): the pointer is
        // a valid heap address but not a closure. Dump magics at every object
        // offset to identify what it actually is (cell? function? other? or
        // zeroed). Helps localize the mis-passed __closure (lodash BUG 4).
        if (getenv("TS_CLOSURE_PROVENANCE")) {
            char* p = (char*)closure;
            auto rd = [&](int off) -> uint32_t { return *(uint32_t*)(p + off); };
            auto name4 = [](uint32_t m, char* out) {
                out[0]=(char)(m&0xFF); out[1]=(char)((m>>8)&0xFF);
                out[2]=(char)((m>>16)&0xFF); out[3]=(char)((m>>24)&0xFF); out[4]=0;
                for (int i=0;i<4;i++) if (out[i]<32||out[i]>126) out[i]='.';
            };
            char a0[5],a8[5],a16[5],a20[5],a24[5];
            name4(rd(0),a0); name4(rd(8),a8); name4(rd(16),a16); name4(rd(20),a20); name4(rd(24),a24);
            void* retaddr = nullptr;
#if defined(_MSC_VER)
            retaddr = _ReturnAddress();
#elif defined(__GNUC__)
            retaddr = __builtin_return_address(0);
#endif
            // retaddr is in the caller (the generated function F whose
            // __closure is bad). RVA = retaddr - module_base maps to F via the
            // IR function list / a dumpbin /symbols on the exe.
            fprintf(stderr, "[PROV] not-a-closure %p idx=%lld ret=%p | m0=%08X(%s) m8=%08X(%s) m16=%08X(%s) m20=%08X(%s) m24=%08X(%s)\n",
                    (void*)closure, (long long)index, retaddr,
                    rd(0),a0, rd(8),a8, rd(16),a16, rd(20),a20, rd(24),a24);
            fflush(stderr);
        }
        fprintf(stderr, "[BUG] ts_closure_get_cell: closure=%p has bad magic 0x%08X (expected CLSR), index=%lld\n",
                (void*)closure, closure->magic, (long long)index);
        fflush(stderr);
        return nullptr;
    }
    TsCell* cell = closure->getCell(index);
    if (cell) {
        uintptr_t cellAddr = (uintptr_t)cell;
        if (cellAddr < 0x10000 || (cellAddr >> 48) != 0) {
            const char* nameStr = (closure->name && closure->name->magic == TsString::MAGIC) ? closure->name->ToUtf8() : "<anon>";
            fprintf(stderr, "[BUG] ts_closure_get_cell: closure=%p cell[%lld]=%p is NOT valid pointer, name='%s'\n",
                    (void*)closure, (long long)index, (void*)cell, nameStr);
            fflush(stderr);
            return nullptr;
        }
        // Validate cell magic
        if (cell->magic != 0x43454C4C) {
            const char* nameStr = (closure->name && closure->name->magic == TsString::MAGIC) ? closure->name->ToUtf8() : "<anon>";
            fprintf(stderr, "[BUG] ts_closure_get_cell: closure=%p cell[%lld]=%p has bad magic 0x%08X (expected CELL), name='%s'\n",
                    (void*)closure, (long long)index, (void*)cell, cell->magic, nameStr);
            fflush(stderr);
            return nullptr;
        }
    }
    return cell;
}

void* ts_closure_get_func(TsClosure* closure) {
    if (!closure) return nullptr;

    // Check if this is a NaN-boxed pointer
    uint64_t nb = (uint64_t)(uintptr_t)closure;
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return nullptr;
        // Check magic to determine type
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 == 0x46554E43) { // TsFunction::MAGIC
            TsFunction* func = (TsFunction*)ptr;
            return func->funcPtr;
        }
        if (magic16 == 0x434C5352) { // TsClosure 'CLSR'
            TsClosure* cls = (TsClosure*)ptr;
            return cls->func_ptr;
        }
        // Fall through to raw pointer checks below
        closure = (TsClosure*)ptr;
    } else if (!nanbox_is_special(nb) && nanbox_is_number(nb)) {
        // NaN-boxed number - not a function
        return nullptr;
    }

    // Raw pointer - safe to read magic at offset 16
    TsObject* obj = (TsObject*)closure;
    if (obj->magic == 0x434C5352) {
        return closure->func_ptr;
    }

    if (obj->magic == 0x46554E43) {
        TsFunction* func = (TsFunction*)closure;
        return func->funcPtr;
    }

    // Fallback - assume it's a TsClosure with func_ptr at expected offset
    return closure->func_ptr;
}

void ts_closure_init_capture(TsClosure* closure, int64_t index, TsValue* initialValue) {
    if (!closure) return;
    TsCell* cell = ts_cell_create(initialValue);
    closure->setCell(index, cell);
    // Write barrier: cell pointer stored into closure's cells array
    if (closure->cells && index >= 0 && index < closure->num_captures)
        ts_gc_write_barrier(&closure->cells[index], cell);
}

// Check if a pointer is a TsClosure (by checking magic number).
// Disambiguates raw TsClosure* from NaN-boxed TsValue* (small tag values
// 0..10 like NANBOX_UNDEFINED=0x0A, NANBOX_HOLE=0x08 etc.). The disambiguator
// MUST be applied to the pointer-as-integer, not to the first byte at the
// pointer's target: a real heap pointer's vtable LSB is arbitrary and can
// happen to land in [0,10] depending on link layout — when it does, the
// previous heuristic `*(uint8_t*)ptr <= 10` reported false negatives and
// every consumer (e.g. Array.prototype.filter taking the slow-path branch
// instead of the closure fast-path) silently went wrong. This was the
// path-length-dependent codegen bug surfaced 2026-05-18.
bool ts_is_closure(void* ptr) {
    if (!ptr) return false;
    uintptr_t addr = (uintptr_t)ptr;
    // NaN-boxed sentinels (undefined, null, true, false, hole, deleted)
    // all have addr ≤ 10. Real heap pointers are at addr ≥ 0x10000.
    if (addr <= 10) return false;
    // NaN-boxed numbers/strings have the top 16 bits set (canonical NaN).
    // Real heap pointers have top 16 bits zero on x64 user-space.
    if ((addr >> 48) != 0) return false;
    TsObject* obj = (TsObject*)ptr;
    return obj->magic == 0x434C5352; // 'CLSR'
}

// Helper: ensure closure->properties TsMap exists, store a key/value with
// ES spec attributes. Called from set_arity and set_name so .length/.name
// are real own properties in the TsMap — enabling hasOwnProperty,
// getOwnPropertyDescriptor, delete, and Object.defineProperty to work
// through the standard TsMap property machinery.
//
// Safe to call here because ts_closure_set_arity is emitted inside
// generated function bodies that run AFTER ts_main() → ts_runtime_init(),
// so GC and string interning are fully initialized.
static void closure_store_own_property(TsClosure* cl, const char* keyName, TsValue val, uint8_t attrs) {
    if (!cl->properties) {
        cl->properties = TsMap::Create();
        ts_gc_write_barrier(&cl->properties, cl->properties);
    }
    TsValue key;
    key.type = ValueType::STRING_PTR;
    key.ptr_val = TsString::GetInterned(keyName);
    cl->properties->SetWithAttrs(key, val, attrs);
}

void ts_closure_set_arity(TsClosure* closure, int32_t arity) {
    if (closure) {
        closure->arity = arity;
        // Per ES spec: Function.length is {writable:false, enumerable:false, configurable:true}
        TsValue val;
        val.type = ValueType::NUMBER_INT;
        val.i_val = arity;
        closure_store_own_property(closure, "length", val, TsHashTable::ATTR_CONFIGURABLE);
    }
}

void ts_closure_set_rest_index(TsClosure* closure, int32_t idx) {
    if (closure) {
        closure->rest_param_index = idx;
    }
}

void ts_closure_set_name(TsClosure* closure, void* name) {
    if (closure) {
        closure->name = (TsString*)name;
        // Per ES spec: Function.name is {value, writable:false, enumerable:false, configurable:true}.
        // Always install as own-property so verifyProperty(fn, "name", ...)
        // and Object.getOwnPropertyDescriptor(fn, "name") work correctly,
        // even for anonymous functions where name is "".
        TsValue val;
        val.type = ValueType::STRING_PTR;
        val.ptr_val = name ? name : (void*)TsString::Create("");
        closure_store_own_property(closure, "name", val, TsHashTable::ATTR_CONFIGURABLE);
    }
}

void ts_closure_set_method(TsClosure* closure) {
    if (closure) {
        closure->is_method = true;
    }
}

// Invoke a closure with one double argument, returns double
// Used for map/filter callbacks with number arrays
// HIR generates functions that expect boxed TsValue* params, so we box the double
double ts_closure_invoke_1d(TsClosure* closure, double arg1) {
    if (!closure || !closure->func_ptr) return 0.0;
    // HIR-generated closures expect (ptr, ptr) -> ptr, where params are boxed
    typedef TsValue* (*Fn)(void*, TsValue*);
    TsValue* boxedArg = ts_value_make_double(arg1);
    TsValue* result = ((Fn)closure->func_ptr)(closure, boxedArg);
    return ts_value_get_double(result);
}

// Invoke a closure with one double argument, returns void
// Used for forEach callbacks with number arrays
void ts_closure_invoke_1d_void(TsClosure* closure, double arg1) {
    if (!closure || !closure->func_ptr) return;
    // HIR-generated closures use trampolines with signature (ptr, ptr) -> ptr
    // where params AND return values are boxed (void returns undefined TsValue*)
    typedef TsValue* (*Fn)(void*, TsValue*);
    TsValue* boxedArg = ts_value_make_double(arg1);
    ((Fn)closure->func_ptr)(closure, boxedArg);  // Ignore return value
}

// Invoke a closure with one double argument, returns bool
// Used for find/filter/some/every callbacks with number arrays
bool ts_closure_invoke_1d_bool(TsClosure* closure, double arg1) {
    if (!closure || !closure->func_ptr) return false;
    // HIR-generated closures use trampolines with signature (ptr, ptr) -> ptr
    // where params AND return values are boxed as TsValue*
    typedef TsValue* (*Fn)(void*, TsValue*);
    TsValue* boxedArg = ts_value_make_double(arg1);
    TsValue* result = ((Fn)closure->func_ptr)(closure, boxedArg);
    // Handle both i1 and ptr return conventions:
    // Compiler may generate arrow functions like `x => x > 5` with LLVM return
    // type i1 (boolean). The callee sets only the low byte of RAX, so the full
    // 64-bit value will be 0 or 1. NaN-boxed booleans use values 6/7, and valid
    // heap pointers are never 0x0 or 0x1, so this check is safe.
    uintptr_t raw = (uintptr_t)result;
    if (raw <= 1) return (bool)raw;
    return ts_value_get_bool(result);
}

// Invoke a closure with two TsValue* arguments, returns TsValue*
// Used for reduce callbacks (accumulator, current value)
// HIR generates closures with signature (ptr %closure, ptr %acc, ptr %x) -> ptr
TsValue* ts_closure_invoke_2v(TsClosure* closure, TsValue* arg1, TsValue* arg2) {
    if (!closure || !closure->func_ptr) return ts_value_make_undefined();
    // HIR-generated closures expect (ptr, ptr, ptr) -> ptr
    typedef TsValue* (*Fn)(void*, TsValue*, TsValue*);
    return ((Fn)closure->func_ptr)(closure, arg1, arg2);
}

}
