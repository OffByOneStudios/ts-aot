#pragma once
#include "TsObject.h"
#include "TsCell.h"

/**
 * TsClosure - Runtime representation of a closure
 *
 * A closure bundles a function pointer with an array of capture cells.
 * Each captured variable lives in a TsCell, allowing mutations to be
 * visible across all scopes that share the capture.
 *
 * Layout:
 *   - func_ptr: Pointer to the actual function (with hidden closure param)
 *   - num_captures: Number of captured variables
 *   - cells: Array of TsCell* pointers
 *
 * For nested closures, inner closures share the same TsCell instances
 * as their enclosing closures for shared captured variables.
 */

class TsClosure : public TsObject {
public:
    void* func_ptr;          // Function pointer (callable)
    int64_t num_captures;    // Number of captured variables
    TsCell** cells;          // Array of capture cells
    TsString* name = nullptr; // Function name for .name and .toString()
    bool is_method = false;  // True for method trampolines (expect 'this' as arg 2)

    TsClosure() : func_ptr(nullptr), num_captures(0), cells(nullptr) {
        magic = 0x434C5352; // 'CLSR'
    }

    static TsClosure* Create(void* funcPtr, int64_t numCaptures);

    // Get a capture cell by index
    TsCell* getCell(int64_t index) const {
        if (index < 0 || index >= num_captures || !cells) return nullptr;
        return cells[index];
    }

    // Set a capture cell by index
    void setCell(int64_t index, TsCell* cell) {
        if (index >= 0 && index < num_captures && cells) {
            cells[index] = cell;
        }
    }
};

extern "C" {
    // Create a new closure with the given function pointer and number of captures
    // The cells array is allocated but not initialized - use ts_closure_set_cell
    TsClosure* ts_closure_create(void* funcPtr, int64_t numCaptures);

    // Set a capture cell at the given index
    void ts_closure_set_cell(TsClosure* closure, int64_t index, TsCell* cell);

    // Get a capture cell at the given index
    TsCell* ts_closure_get_cell(TsClosure* closure, int64_t index);

    // Get the function pointer from a closure
    void* ts_closure_get_func(TsClosure* closure);

    // Create a cell and store it in the closure at the given index
    // This is a convenience function that combines ts_cell_create and ts_closure_set_cell
    void ts_closure_init_capture(TsClosure* closure, int64_t index, TsValue* initialValue);

    // Check if a pointer is a TsClosure (by checking magic number)
    bool ts_is_closure(void* ptr);

    // Mark a closure as a method trampoline (expects 'this' as second arg)
    void ts_closure_set_method(TsClosure* closure);

    // Set the name on a TsClosure
    void ts_closure_set_name(TsClosure* closure, void* name);

    // Invoke a closure with one double argument, returns double
    // Used for map/filter callbacks with number arrays
    double ts_closure_invoke_1d(TsClosure* closure, double arg1);

    // Invoke a closure with one double argument, returns void
    // Used for forEach callbacks with number arrays
    void ts_closure_invoke_1d_void(TsClosure* closure, double arg1);

    // Invoke a closure with one double argument, returns bool
    // Used for find/filter/some/every callbacks with number arrays
    bool ts_closure_invoke_1d_bool(TsClosure* closure, double arg1);

    // Invoke a closure with two TsValue* arguments, returns TsValue*
    // Used for reduce callbacks (accumulator, current value)
    TsValue* ts_closure_invoke_2v(TsClosure* closure, TsValue* arg1, TsValue* arg2);
}
