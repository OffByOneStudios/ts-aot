#include "../include/TsCell.h"
#include "../include/GC.h"
#include "../include/TsGC.h"
#include <new>
#include <cstdio>

TsCell* TsCell::Create(TsValue* initialValue) {
    // Tenure cells directly into the old generation. Cells back captured
    // variables and are long-lived; nursery promotion MOVES them, but a live
    // reference held only transiently on the stack / in a register at minor-GC
    // time is not always tracked precisely, leaving a stale (then-zeroed)
    // pointer. Old-gen objects never move, so references stay valid. (Same
    // reasoning TsString/TsRegExp already use ts_gc_alloc_old_gen.)
    void* mem = ts_gc_alloc_old_gen(sizeof(TsCell));
    TsCell* cell = new (mem) TsCell();
    cell->value = initialValue;
    // Write barrier: initialValue may be a nursery pointer
    ts_gc_write_barrier(&cell->value, initialValue);
    return cell;
}

extern "C" {

TsCell* ts_cell_create(TsValue* initialValue) {
    return TsCell::Create(initialValue);
}

TsValue* ts_cell_get(TsCell* cell) {
    if (!cell) return nullptr;
    return cell->value;
}

void ts_cell_set(TsCell* cell, TsValue* value) {
    if (!cell) return;
    cell->value = value;
    // Write barrier: value may be a nursery pointer
    ts_gc_write_barrier(&cell->value, value);
}

}
