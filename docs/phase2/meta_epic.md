# Phase 2 Meta Epic: Advent of Code Readiness

**Status:** Planned
**Goal:** Extend the `ts-aot` compiler with enough features to solve typical Advent of Code problems. This involves adding loops, arrays, file I/O, and string manipulation.

## Epic 7: Control Flow (Loops)
**Goal:** Implement iterative control flow structures.
- **While Loops:** Support `while (cond) { ... }`.
- **For Loops:** Support C-style `for (init; cond; incr) { ... }`.
- **Break/Continue:** Support `break` and `continue` statements.

## Epic 8: Arrays & Collections
**Goal:** Implement dynamic arrays and basic collection types.
- **Runtime Array:** Implement `TsArray<T>` in C++ runtime (GC-managed vector).
- **AST/Parser:** Support `[]` literals and `Array<T>` type syntax.
- **Operations:** Support indexing `arr[i]`, `push()`, `pop()`, and `length`.
- **Iteration:** Support `for (const x of arr)` (optional, or map to C-style for).

## Epic 9: File I/O & System Interop
**Goal:** Allow reading input files, which is essential for AOC.
- **Runtime FS:** Implement `fs.readFileSync` (synchronous file read).
- **String Parsing:** Implement `parseInt(str)`, `parseFloat(str)`.
- **Console:** Expand `console.log` to handle multiple arguments and different types.

## Epic 10: Enhanced String Manipulation
**Goal:** Support string processing operations.
- **Methods:** Implement `split(delimiter)`, `trim()`, `substring()`, `indexOf()`.
- **Indexing:** Support string character access `str[i]`.
- **Comparison:** Ensure string equality and comparison works correctly.

## Epic 11: Phase 2 Integration & Testing
**Goal:** Verify Phase 2 features with real AOC problems.
- **Test Suite:** Add integration tests for loops, arrays, and I/O.
- **AOC Solver:** Implement a solution for a simple AOC day (e.g., Day 1 of a recent year) to prove capability.
