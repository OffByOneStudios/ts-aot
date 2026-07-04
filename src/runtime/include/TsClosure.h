#pragma once
#include "TsObject.h"
#include "TsCell.h"
#include "TsTyped.h"

class TsMap;  // Forward declaration for properties field

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
    static constexpr uint32_t MAGIC = 0x434C5352; // 'CLSR'

    void* func_ptr;          // Function pointer (callable)
    int64_t num_captures;    // Number of captured variables
    TsCell** cells;          // Array of capture cells
    TsString* name = nullptr; // Function name for .name and .toString()
    bool is_method = false;  // True for method trampolines (expect 'this' as arg 2)
    // True if this function has [[Construct]] and therefore an own `.prototype`
    // (plain functions and generators, incl. generator methods). FALSE for
    // regular methods, getters, setters — not constructors → no `.prototype`.
    bool is_constructor = true;
    // ES IsConstructor: arrows, async fns, generators, and async generators
    // have no [[Construct]] regardless of prototype-slot semantics (which
    // is_constructor above also drives — generators KEEP .prototype but are
    // still not constructable). Set false via ts_closure_set_not_constructable.
    bool constructable = true;
    TsMap* properties = nullptr;  // For storing properties like .prototype
    int32_t arity = 0;           // Number of user-visible parameters (for Function.length)
    uint8_t genKind = 0;         // 0 = plain, 1 = generator fn, 2 = async generator fn
                                 // (drives getPrototypeOf(fn) -> %(Async)GeneratorFunction.prototype%)
    // PHYSICAL user param count (the compiled trampoline's positional params,
    // i.e. counting params WITH defaults — unlike `arity`/.length which stops at
    // the first default). Used to dispatch >10-param calls with the exact arg
    // count the LLVM signature expects. 0 = unset (fall back to arity).
    int32_t num_params = 0;
    // ECMA-262 rest-parameter dispatch. Set by ts_closure_set_rest_index when
    // the underlying function declares `...rest`. -1 means no rest parameter.
    // Used by ts_call_N to pack trailing args[rest_param_index..N-1] into a
    // single TsArray before forwarding to the (fixed-arity) compiled function.
    int32_t rest_param_index = -1;

    TsClosure() : func_ptr(nullptr), num_captures(0), cells(nullptr) {
        magic = MAGIC;
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

TS_DECLARE_TAG(TsClosure);  // magic at offset 16 (TsObject subclass)

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

    // Share one cell across all closures capturing the same outer variable.
    // *slot is the canonical shared TsCell* (caller-owned entry alloca); the
    // first caller creates+publishes it, later callers reuse it. See the .cpp
    // for the full rationale (multi-closure capture desync fix).
    void ts_closure_share_or_init_cell(TsClosure* closure, int64_t index,
                                       TsCell** slot, TsValue* initialValue);

    // Set the arity (user-visible parameter count) on a TsClosure
    void ts_closure_set_arity(TsClosure* closure, int32_t arity);
    void ts_closure_set_gen_kind(TsClosure* closure, int32_t kind);
    // Set the PHYSICAL user param count (counts params with defaults too).
    void ts_closure_set_num_params(TsClosure* closure, int32_t n);

    // Set the rest-parameter index on a TsClosure. idx is the zero-based
    // position of the rest binding in the declared parameter list (excluding
    // `__closure__`, `this`, hidden `__arg*`). Passing idx < 0 clears it.
    // When set, ts_call_N packs trailing args into a TsArray before forwarding.
    void ts_closure_set_rest_index(TsClosure* closure, int32_t idx);

    // Check if a pointer is a TsClosure (by checking magic number)
    bool ts_is_closure(void* ptr);

    // Mark a closure as a method trampoline (expects 'this' as second arg)
    void ts_closure_set_method(TsClosure* closure);
    void ts_closure_set_no_prototype(TsClosure* closure);

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
