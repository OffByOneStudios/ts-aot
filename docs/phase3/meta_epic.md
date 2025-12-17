# Phase 3: Advent of Code Readiness

**Status:** Planned
**Goal:** Extend the compiler and runtime to support solving the first few days of Advent of Code 2015.

## Overview
Phase 3 focuses on filling the gaps in the standard library and runtime required to solve actual algorithmic problems. This includes advanced string manipulation, hash maps, and math functions.

## Epics

### [Epic 10: Advanced Strings](./epic_10_strings_ii.md)
Implement string properties and methods required for parsing input.
- `length` property.
- `charCodeAt(index)` or `[]` access.
- `split(separator)`.

### [Epic 11: Standard Library](./epic_11_stdlib.md)
Implement the `Math` object and other global utilities.
- `Math.min`, `Math.max`, `Math.abs`, `Math.floor`.
- Array sorting (maybe).

### [Epic 12: Hash Maps](./epic_12_maps.md)
Implement the `Map` class for coordinate tracking and lookups.
- `new Map()`.
- `set(key, value)`.
- `get(key)`.
- `has(key)`.

### [Epic 13: AoC 2015 Solutions](./epic_13_aoc_2015.md)
Validate the compiler by solving actual puzzles.
- Day 1: Not Quite Lisp.
- Day 2: I Was Told There Would Be No Math.
- Day 3: Perfectly Spherical Houses in a Vacuum.
