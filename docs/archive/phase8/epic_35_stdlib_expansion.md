# Epic 35: Standard Library Expansion

**Goal:** Implement core built-in objects and functions to improve compatibility with standard TypeScript/JavaScript environments.

**Dependencies:**
- Phase 1-7 (Core Compiler)
- Epic 34 (Destructuring & Spread)

## Milestones

### Milestone 35.1: JSON Support
- [x] **Runtime:** Implement `JSON.parse()` and `JSON.stringify()`.
- [x] **Compiler:** Add `JSON` as a global object in `SymbolTable`.
- [x] **Integration:** Support basic object/array serialization.

### Milestone 35.2: Date Object
- [x] **Runtime:** Implement `Date` class with `now()`, `getTime()`, `getFullYear()`, etc.
- [x] **Compiler:** Register `Date` class in global scope.

### Milestone 35.3: RegExp Support
- [x] **Runtime:** Integrate a regex library (ICU Regex).
- [x] **Compiler:** Support `new RegExp()`.

### Milestone 35.4: Math and IO Expansion
- [x] **Runtime:** Add `Math.random()`, `Math.ceil()`, `Math.round()`, `Math.sqrt()`, `Math.pow()`, `Math.PI`.
- [x] **Runtime:** Add `fs.writeFileSync()`.
- [x] **Compiler:** Register new methods in `Analyzer`.

### Milestone 35.5: String and Array Enhancements
- [x] **Runtime:** Add `String.includes()`, `String.indexOf()`, `String.toLowerCase()`, `String.toUpperCase()`.
- [x] **Runtime:** Add `Array.indexOf()`, `Array.slice()`, `Array.join()`.
- [x] **Compiler:** Register new methods in `Analyzer`.

## Action Items
- [x] **Task 1:** Create `src/runtime/src/TsJSON.cpp` and `TsJSON.h`.
- [x] **Task 2:** Integrate a JSON library (e.g., `nlohmann/json`) via `vcpkg`.
- [x] **Task 3:** Implement `ts_json_parse` and `ts_json_stringify` in runtime.
- [x] **Task 4:** Register `JSON` global in `Analyzer`.
- [x] **Task 5:** Create `src/runtime/src/TsDate.cpp` and `TsDate.h`.
- [x] **Task 6:** Implement `Date` methods using `<chrono>` or similar.
- [x] **Task 7:** Register `Date` class in `Analyzer`.
- [x] **Task 8:** Implement `RegExp` basic support.
- [x] **Task 9:** Expand `Math` object with more methods and constants.
- [x] **Task 10:** Add `fs.writeFileSync` to `IO.cpp`.
- [x] **Task 11:** Implement `String` methods: `includes`, `indexOf`, `toLowerCase`, `toUpperCase`.
- [x] **Task 12:** Implement `Array` methods: `indexOf`, `slice`, `join`.
