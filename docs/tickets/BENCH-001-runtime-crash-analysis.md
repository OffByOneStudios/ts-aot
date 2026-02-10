# BENCH-001: Benchmark Runtime Crash Analysis

**Status:** Category 1 & 2 Fixed
**Created:** 2026-02-09
**Updated:** 2026-02-10

## Summary

All 11 benchmarks compile successfully. Category 1 (virtual property dispatch crashes) and Category 2 (null array elements) have been fixed. 6/11 benchmarks now produce output (vs 1/11 before).

## Before Fix (2026-02-09)

| Benchmark | Compiles | Runs | Crash Site |
|-----------|----------|------|------------|
| cold_start | yes | PASS | - |
| cli_tool | yes | CRASH | `ts_array_get_unchecked` - null elements |
| fibonacci | yes | CRASH | `ts_try_virtual_dispatch_via_vbase` |
| json_parse | yes | CRASH | `ts_try_virtual_dispatch_via_vbase` |
| regex | yes | CRASH | `ts_try_virtual_dispatch_via_vbase` |
| sorting | yes | CRASH | `ts_try_virtual_dispatch_via_vbase` |
| allocation | yes | CRASH | `ts_try_virtual_dispatch_via_vbase` |
| gc_stress | yes | CRASH | `ts_try_virtual_dispatch_via_vbase` |
| file_copy | yes | CRASH | `ts_try_virtual_dispatch_via_vbase` |
| http_hello | yes | CRASH | `ts_try_virtual_dispatch_via_vbase` |
| tcp_echo | yes | CRASH | `ts_try_virtual_dispatch_via_vbase` |

## After Fix (2026-02-10)

| Benchmark | Runs | Status | Notes |
|-----------|------|--------|-------|
| cold_start | PASS | Same | Exits cleanly in ~33ms |
| cli_tool | CRASH | Improved | Different crash (no longer null elements) |
| fibonacci | TIMEOUT | **FIXED** | Benchmark harness works; fib(35) too slow (boxing overhead) |
| json_parse | PARTIAL | **IMPROVED** | Small/medium JSON works; crash on large 2.5MB parse |
| regex | TIMEOUT | **IMPROVED** | Runs but text generation slow |
| sorting | PARTIAL | **IMPROVED** | QuickSort verified; crash on Array.sort(100k) |
| allocation | PARTIAL | **IMPROVED** | 10k objects OK; crash on 100k objects |
| gc_stress | CRASH | **IMPROVED** | Now hits GC limit ("Too many heap sections") |
| file_copy | PARTIAL | **IMPROVED** | Read/write work partially; crash on chunked copy |
| http_hello | CRASH | Different | IO/network startup issue |
| tcp_echo | CRASH | Different | IO/network startup issue |

## Category 1 Fix: Virtual Property Dispatch (DONE)

**Files modified:** `src/runtime/src/TsObject.cpp`

**Root cause:** `ts_try_virtual_dispatch_via_vbase` used `reinterpret_cast<TsReadable*>(obj)` on ALL objects during property lookup. For non-stream objects (TsMap), this produced garbage vtables, and virtual function calls jumped to address 0x0 (NX fault). SEH `__except` couldn't catch it because Boehm GC's vectored exception handler fires first.

**Fix applied:**
1. `ts_try_virtual_dispatch_via_vbase` - Replaced body with simple `return undefined`. No stream class actually overrides `GetPropertyVirtual()`, making the entire function unnecessary.
2. `ts_try_virtual_property_dispatch` - Replaced SEH-guarded cast with TsPromise-only check. Only TsPromise implements `GetPropertyVirtual()` with meaningful behavior.

**Key discovery:** The runtime library was NOT being rebuilt due to a pre-existing compile error in TsArray.cpp (`private member access`). The stale `.lib` from the previous session was being linked, making all TsObject.cpp fixes appear ineffective.

## Category 2 Fix: Array Null Elements (DONE)

**File modified:** `src/runtime/src/TsArray.cpp`

**Root cause:** `ts_array_get_unchecked` accessed `array->elements` and `array->length` directly (private members). The code compiled with the old runtime but failed with the new one.

**Fix:** Use `array->Length()` and `array->GetElementsPtr()` public accessors instead of private member access.

## Remaining Issues (New Categories)

### Category 3: GC Pressure at Scale
- fib(40) crashes from excessive boxing (330M TsValue allocations)
- gc_stress hits "Too many heap sections"
- allocation/100k objects causes access violation
- json_parse/2.5MB causes access violation

### Category 4: IO/Network Startup
- http_hello and tcp_echo crash immediately (not property access related)

### Category 5: Performance
- Recursive fibonacci is compiled with boxed arithmetic (`ts_value_add` instead of `fadd`)
- Each recursive call allocates 3 TsValue objects
- Number formatting uses scientific notation instead of integer format

## Test Results

- Golden IR: 146/146 (100%) - no regressions
- Node tests: 283/283 (100%) - no regressions
- New test: `tests/node/object/object_dynamic_property_access.ts` (7 tests)
