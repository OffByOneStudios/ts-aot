#include "../include/TsClosure.h"
#include "../include/TsObject.h"
#include "../include/TsRuntime.h"
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
    return closure->getCell(index);
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
