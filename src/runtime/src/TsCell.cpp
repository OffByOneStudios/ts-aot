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
    if (!cell) {
        fprintf(stderr, "[ts_cell_get] cell is null!\n");
        return nullptr;
    }
    TsValue* val = cell->value;
    if (val) {
        fprintf(stderr, "[ts_cell_get] cell=%p value=%p type=%d\n", (void*)cell, (void*)val, (int)val->type);
    } else {
        fprintf(stderr, "[ts_cell_get] cell=%p value=null\n", (void*)cell);
    }
    return val;
}

void ts_cell_set(TsCell* cell, TsValue* value) {
    if (!cell) return;
    if (value) {
        fprintf(stderr, "[ts_cell_set] cell=%p value=%p type=%d\n", (void*)cell, (void*)value, (int)value->type);
    } else {
        fprintf(stderr, "[ts_cell_set] cell=%p value=null\n", (void*)cell);
    }
    cell->value = value;
}

}
