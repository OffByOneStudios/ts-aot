# Phase 5: Syntactic Sugar & Iterators

**Status:** Planned
**Goal:** Make code more expressive and closer to modern TypeScript standards by implementing common syntactic sugar and iteration patterns.

## Overview
Phase 5 focuses on developer experience improvements. It introduces features that make writing code more concise and readable, such as arrow functions, template literals, and enhanced control flow with `switch` and `for..of`. It also adds structural types like Tuples and Enums.

## Epics

### [Epic 18: Advanced Control Flow](./epic_18_control_flow.md)
Implement `switch` statements and `for..of` loops.
- `switch` / `case` / `default`.
- `for (const x of arr)` iteration.

### [Epic 19: Modern Functions & Strings](./epic_19_modern_syntax.md)
Support modern ES6+ syntax for functions and strings.
- Arrow Functions `() => {}`.
- Template Literals `` `Hello ${name}` ``.

### [Epic 20: Structural Types](./epic_20_structural_types.md)
Add support for fixed-structure types.
- Tuples `[string, number]`.
- Enums `enum Direction { Up, Down }`.
- Type Aliases `type ID = string`.

### [Epic 21: AoC 2015 Solutions III](./epic_21_aoc_2015_iii.md)
Validate new features with more puzzles.
- Day 7: Some Assembly Required (Bitwise logic, memoization).
- Day 8: Matchsticks (String escaping/memory).
- Day 9: All in a Single Night (TSP/Permutations).
