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
#include <cstring>
#include <map>
#include <set>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

// Debug-only set of every closure-object address ts_closure_create ever
// returned. Lets ts_closure_get_cell answer: was this bad pointer EVER a real
// closure (→ zeroed later) or NEVER created (→ a non-closure passed as
// __closure, a calling-convention bug)? Built only under TS_CLOSURE_PROVENANCE.
static std::set<uintptr_t>* g_createdClosures = nullptr;

// Debug-only function-address registry for TS_CLOSURE_PROVENANCE. Maps each
// generated function's ENTRY address (closure->func_ptr) to its name, captured
// at MakeClosure time via ts_closure_set_name. symbolize_addr() resolves an
// arbitrary code address (e.g. a return address) to the nearest function entry
// at or below it — naming the function that contains the address. Only built
// up when TS_CLOSURE_PROVENANCE is set (avoid overhead otherwise).
static std::map<uintptr_t, const char*>* g_funcAddrNames = nullptr;
static bool g_prov_enabled_checked = false;
static bool g_prov_enabled = false;

static bool prov_enabled() {
    if (!g_prov_enabled_checked) {
        g_prov_enabled = (getenv("TS_CLOSURE_PROVENANCE") != nullptr);
        g_prov_enabled_checked = true;
    }
    return g_prov_enabled;
}

static void prov_register_func(void* funcPtr, const char* name) {
    if (!prov_enabled() || !funcPtr || !name) return;
    if (!g_funcAddrNames) g_funcAddrNames = new std::map<uintptr_t, const char*>();
    uintptr_t a = (uintptr_t)funcPtr;
    if (g_funcAddrNames->find(a) == g_funcAddrNames->end())
        (*g_funcAddrNames)[a] = _strdup(name);
}

// Compiler-emitted (only under -DTS_EMIT_CLOSURE_NAMES build / env at compile
// time) registration of a function's MANGLED name (e.g. "__fn_expr_1042").
// Overwrites the JS-name entry so anonymous functions get a useful id in the
// [PROV] symbolization. Always registers (the call only exists in instrumented
// builds), allocating the map if needed.
extern "C" void ts_closure_register_debug_name(void* funcPtr, const char* name) {
    if (!funcPtr || !name) return;
    if (!g_funcAddrNames) g_funcAddrNames = new std::map<uintptr_t, const char*>();
    (*g_funcAddrNames)[(uintptr_t)funcPtr] = _strdup(name);
}

// Resolve a code address to "<name>+0xNNN" of the nearest registered entry.
static const char* prov_symbolize(void* addr, char* buf, size_t buflen) {
    if (!g_funcAddrNames || g_funcAddrNames->empty()) { snprintf(buf, buflen, "?"); return buf; }
    uintptr_t a = (uintptr_t)addr;
    auto it = g_funcAddrNames->upper_bound(a); // first entry > a
    if (it == g_funcAddrNames->begin()) { snprintf(buf, buflen, "<before-first>"); return buf; }
    --it; // largest entry <= a
    snprintf(buf, buflen, "%s+0x%llX", it->second, (unsigned long long)(a - it->first));
    return buf;
}

TsClosure* TsClosure::Create(void* funcPtr, int64_t numCaptures) {
    // Tenure closures (and their cell-pointer arrays) directly into the old
    // generation. Closures are long-lived function objects held across many
    // allocations during init; nursery promotion MOVES them, but a live
    // reference held only transiently on the stack / in a register at minor-GC
    // time is not always tracked precisely, leaving a stale (then-zeroed)
    // pointer that later reads as magic 0. Old-gen objects never move.
    void* mem = ts_gc_alloc_old_gen(sizeof(TsClosure));
    TsClosure* closure = new (mem) TsClosure();
    closure->func_ptr = funcPtr;
    closure->num_captures = numCaptures;

    if (numCaptures > 0) {
        // Allocate array for cell pointers (also tenured — it holds GC pointers
        // to cells and must not move out from under a transient reference).
        closure->cells = (TsCell**)ts_gc_alloc_old_gen(numCaptures * sizeof(TsCell*));
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
    TsClosure* c = TsClosure::Create(funcPtr, numCaptures);
    if (prov_enabled()) {
        if (!g_createdClosures) g_createdClosures = new std::set<uintptr_t>();
        g_createdClosures->insert((uintptr_t)c);
    }
    return c;
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
            // Resolve retaddr (in the caller F) to the nearest registered
            // function entry — names the function whose code holds the bad
            // ts_closure_get_cell call.
            char sym[256];
            prov_symbolize(retaddr, sym, sizeof(sym));
            fprintf(stderr, "[PROV] not-a-closure %p idx=%lld in=%s | m0=%08X m16=%08X m24=%08X\n",
                    (void*)closure, (long long)index, sym, rd(0), rd(16), rd(24));
            fflush(stderr);
        }
        const char* prov = "";
        if (g_createdClosures)
            prov = g_createdClosures->count((uintptr_t)closure) ? " [WAS-CREATED→zeroed]" : " [NEVER-CREATED→non-closure]";
        fprintf(stderr, "[BUG] ts_closure_get_cell: closure=%p has bad magic 0x%08X (expected CLSR), index=%lld%s\n",
                (void*)closure, closure->magic, (long long)index, prov);
        fflush(stderr);
        return nullptr;
    }
    TsCell* cell = closure->getCell(index);
    if (cell) {
        uintptr_t cellAddr = (uintptr_t)cell;
        // Crash-safe validation. A closure's cells array (an old-gen block) can
        // be freed-and-reused while the closure still references it (a GC
        // rooting gap surfacing under lodash, where the block came back holding
        // NaN-boxed doubles), so `cell` may be a tagged primitive OR a stale
        // pointer into a decommitted block. NEVER deref `cell` or `closure->name`
        // without first confirming they are live heap objects, or the validator
        // itself faults (the actual observed crash, TsClosure.cpp:171/178). On
        // any failure return nullptr — the caller reads the captured slot as
        // undefined (a wrong result for that read) instead of crashing, so the
        // run completes.
        bool cellLive = !(cellAddr < 0x10000 || (cellAddr >> 48) != 0)
                        && ts_gc_is_heap_object(cell);
        if (!cellLive || cell->magic != 0x43454C4C) {
            // closure->name is itself possibly stale — guard before deref.
            const char* nameStr = (closure->name && ts_gc_is_heap_object(closure->name)
                                   && closure->name->magic == TsString::MAGIC)
                                      ? closure->name->ToUtf8() : "<anon>";
            uint32_t cellMagic = cellLive ? cell->magic : 0;
            fprintf(stderr, "[BUG] ts_closure_get_cell: closure=%p cell[%lld]=%p invalid "
                            "(live=%d magic=0x%08X), name='%s'\n",
                    (void*)closure, (long long)index, (void*)cell,
                    (int)cellLive, cellMagic, nameStr);
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

// Share ONE cell across every closure that captures the same outer variable.
// `slot` points to a caller-owned (stack/entry-alloca) TsCell* that is the
// canonical shared cell for a given captured variable within a function. The
// FIRST closure to capture the variable finds *slot == null, creates the cell
// (seeded with initialValue), and publishes it into *slot. Every subsequent
// closure finds *slot already populated and merely points its own capture
// index at that SAME cell. This guarantees that a write through any closure's
// cell (or the parent's primary cell, which is one of these) is visible to all
// the others — fixing the multi-closure capture desync where each closure used
// to get its own cell and the parent read only the first one.
void ts_closure_share_or_init_cell(TsClosure* closure, int64_t index,
                                   TsCell** slot, TsValue* initialValue) {
    if (!closure) return;
    TsCell* cell = slot ? *slot : nullptr;
    if (!cell) {
        cell = ts_cell_create(initialValue);
        if (slot) *slot = cell;
    }
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

void ts_closure_set_num_params(TsClosure* closure, int32_t n) {
    if (closure) closure->num_params = n;
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
        // Register func_ptr -> name for provenance symbolication (debug only).
        if (prov_enabled() && closure->func_ptr && name &&
            ((TsString*)name)->magic == TsString::MAGIC) {
            prov_register_func(closure->func_ptr, ((TsString*)name)->ToUtf8());
        }
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

// Mark a closure as a NON-constructor (no own `.prototype`): regular methods,
// getters, setters. Generators (incl. generator methods) must NOT be marked.
void ts_closure_set_no_prototype(TsClosure* closure) {
    if (closure) {
        closure->is_constructor = false;
        closure->constructable = false;  // methods/accessors: not constructors
    }
}

// ES IsConstructor=false WITHOUT touching prototype-slot semantics:
// arrows, async functions, generators, async generators.
void ts_closure_set_not_constructable(TsClosure* closure) {
    if (closure) closure->constructable = false;
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
