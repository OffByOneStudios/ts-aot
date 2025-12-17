# Phase 4: Structure & Algorithms

**Status:** Completed
**Goal:** Move beyond script-like execution to structured programming with functions and objects, and integrate external C++ libraries.

## Overview
Phase 4 introduces the core building blocks of application structure: reusable functions and data objects. It also demonstrates the power of AOT compilation by linking against native cryptography libraries to solve compute-intensive problems.

## Epics

### [Epic 14: Functions & Scopes](./epic_14_functions.md)
Implement user-defined functions to allow code reuse and recursion.
- Function declarations `function foo() { ... }`.
- Return values.
- Local scope and parameter binding.

### [Epic 15: Object Literals](./epic_15_objects.md)
Support basic JavaScript objects for grouping data.
- Object literals `{ x: 1, y: 2 }`.
- Property access `obj.prop`.
- Dynamic property assignment `obj.prop = val`.

### [Epic 16: Cryptography](./epic_16_crypto.md)
Integrate an MD5 implementation to support hashing puzzles.
- Link an MD5 library (e.g., from OpenSSL or a standalone C++ file).
- Expose `crypto.md5(string)` to TypeScript.

### [Epic 17: AoC 2015 Solutions II](./epic_17_aoc_2015_ii.md)
Validate the new features with the next set of puzzles.
- Day 4: The Ideal Stocking Stuffer (Requires MD5).
- Day 5: Doesn't He Have Intern-Elves (Requires complex string logic).
- Day 6: Probably a Fire Hazard (Requires 2D arrays/performance).
