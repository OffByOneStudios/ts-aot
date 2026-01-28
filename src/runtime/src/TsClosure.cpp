#include "../include/TsClosure.h"
#include "../include/GC.h"
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
}

TsCell* ts_closure_get_cell(TsClosure* closure, int64_t index) {
    if (!closure) return nullptr;
    return closure->getCell(index);
}

void* ts_closure_get_func(TsClosure* closure) {
    if (!closure) return nullptr;
    return closure->func_ptr;
}

void ts_closure_init_capture(TsClosure* closure, int64_t index, TsValue* initialValue) {
    if (!closure) return;
    TsCell* cell = ts_cell_create(initialValue);
    closure->setCell(index, cell);
}

}
