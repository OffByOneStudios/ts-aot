#include "../include/TsClosure.h"
#include "../include/TsObject.h"
#include "../include/TsRuntime.h"
#include "../include/TsString.h"
#include "../include/GC.h"
#include "../include/TsGC.h"
#include "../include/TsNanBox.h"
#include <new>
#include <cstdio>

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

// Check if a pointer is a TsClosure (by checking magic number)
bool ts_is_closure(void* ptr) {
    if (!ptr) return false;
    // Check first byte - if it looks like a TsValue (0-10), it's not a raw closure
    uint8_t firstByte = *(uint8_t*)ptr;
    if (firstByte <= 10) return false;  // TsValue, not a raw object
    TsObject* obj = (TsObject*)ptr;
    return obj->magic == 0x434C5352; // 'CLSR'
}

void ts_closure_set_arity(TsClosure* closure, int32_t arity) {
    if (closure) {
        closure->arity = arity;
    }
}

void ts_closure_set_name(TsClosure* closure, void* name) {
    if (closure) {
        closure->name = (TsString*)name;
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
