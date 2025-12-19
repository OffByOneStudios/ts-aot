# Phase 12: Runtime Feature Parity

**Status:** In Progress
**Goal:** Achieve feature parity for the TypeScript/JavaScript standard library in the `ts-aot` runtime. This phase focuses on filling the gaps identified in the runtime audit, ensuring that common built-in methods and global objects behave as expected.

## Milestones

### Epic 52: Collection Parity (String, Array, Map)
Implement missing functional and utility methods for core collection types.
- [ ] **String**: `replace`, `match`, `slice`, `concat`, `padStart`/`padEnd`, `repeat`.
- [ ] **Array**: `map`, `filter`, `reduce`, `forEach`, `some`, `every`, `find`, `findIndex`, `pop`, `shift`, `unshift`.
- [ ] **Array**: `at`, `flat`, `flatMap`, `includes`.
- [ ] **Map**: `clear`, `delete`, `entries`, `values`, `forEach`.

### Epic 53: Date & Math Parity
Complete the implementation of temporal and mathematical utilities.
- [x] **Date**: Setters (`setFullYear`, `setMonth`, etc.).
- [x] **Date**: UTC methods (`getUTCFullYear`, `setUTCFullYear`, etc.).
- [x] **Date**: Parsing improvements (ICU-based).
- [x] **Math**: Missing trigonometric and logarithmic functions (e.g., `log10`, `exp`).

### Epic 54: RegExp & JSON Parity
Enhance regular expression support and JSON serialization.
- [ ] **RegExp**: Support for flags (`g`, `i`, `m`, `u`, `y`).
- [ ] **RegExp**: Integration with `String.prototype.match`, `replace`, `split`.
- [ ] **RegExp**: Instance properties (`lastIndex`, `source`, `flags`).
- [ ] **JSON**: Support for `replacer` and `space` arguments in `stringify`.

### Epic 55: Promise & Async Parity
Finalize the asynchronous programming model.
- [ ] **Promise**: `catch` and `finally` methods.
- [ ] **Promise**: Static methods `all`, `race`, `allSettled`, `any`.
- [ ] **Promise**: Proper rejection handling and unhandled rejection tracking.

### Epic 56: Global Objects & Node.js Parity
Implement essential global objects and environment-specific APIs.
- [ ] **Global**: `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`.
- [ ] **fs**: Complete the `fs` module (e.g., `exists`, `mkdir`, `unlink`).
- [ ] **process**: `argv`, `env`, `exit`, `cwd`.
- [ ] **Buffer**: Basic implementation for binary data handling.
- [ ] **fetch**: Complete the `fetch` implementation in `IO.cpp`.

## Action Items
- [ ] Create detailed epic files for each of the above.
- [ ] Prioritize `Array` functional methods (`map`, `filter`, `reduce`) as they are most commonly used in AoC.
- [ ] Implement `String.replace` and `String.match` for complex parsing tasks.
