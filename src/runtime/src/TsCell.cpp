#include "../include/TsCell.h"
#include "../include/GC.h"
#include <new>
#include <cstdio>

TsCell* TsCell::Create(TsValue* initialValue) {
    void* mem = ts_alloc(sizeof(TsCell));
    TsCell* cell = new (mem) TsCell();
    cell->value = initialValue;
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
}

}
