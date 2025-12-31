#pragma once
#include "TsObject.h"

/**
 * TsCell - A mutable reference cell for closure capture
 * 
 * When a variable is captured by a closure and mutated (either in the outer
 * scope or the closure), we need both scopes to share the same storage.
 * TsCell provides this shared storage.
 * 
 * Usage:
 *   - Outer scope allocates a cell with ts_cell_create(initial_value)
 *   - Both outer scope and closure read/write through ts_cell_get/ts_cell_set
 *   - Closure context stores the cell pointer (not the value)
 */

class TsCell : public TsObject {
public:
    TsValue* value;
    
    TsCell() : value(nullptr) {
        magic = 0x43454C4C; // 'CELL'
    }
    
    static TsCell* Create(TsValue* initialValue);
};

extern "C" {
    // Create a new cell with an initial boxed value
    TsCell* ts_cell_create(TsValue* initialValue);
    
    // Get the value from a cell
    TsValue* ts_cell_get(TsCell* cell);
    
    // Set the value in a cell
    void ts_cell_set(TsCell* cell, TsValue* value);
}
