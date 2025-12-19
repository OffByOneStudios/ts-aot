# Epic 52: Collection Parity (String, Array, Map)

**Status:** In Progress
**Goal:** Implement missing functional and utility methods for `String`, `Array`, and `Map` types in the runtime.

## Background
The current runtime has basic support for strings, arrays, and maps, but lacks many of the higher-order functions (like `map`, `filter`) and utility methods (like `replace`, `slice`) that are standard in JavaScript.

## Action Items

### String Methods
- [x] Implement `String.prototype.slice(start, end)` in `TsString.cpp`.
- [ ] Implement `String.prototype.concat(...strings)` in `TsString.cpp`.
- [x] Implement `String.prototype.padStart(targetLength, padString)` and `padEnd`.
- [x] Implement `String.prototype.repeat(count)`.
- [ ] Implement `String.prototype.replace(pattern, replacement)` (Basic string replacement).
- [ ] Implement `String.prototype.replaceAll(pattern, replacement)`.

### Array Methods (Functional)
- [ ] Implement `Array.prototype.forEach(callback)`.
- [ ] Implement `Array.prototype.map(callback)`.
- [ ] Implement `Array.prototype.filter(callback)`.
- [ ] Implement `Array.prototype.reduce(callback, initialValue)`.
- [ ] Implement `Array.prototype.some(callback)` and `every(callback)`.
- [ ] Implement `Array.prototype.find(callback)` and `findIndex(callback)`.

### Array Methods (Utility)
- [x] Implement `Array.prototype.pop()`.
- [x] Implement `Array.prototype.shift()` and `unshift(...items)`.
- [x] Implement `Array.prototype.at(index)`.
- [x] Implement `Array.prototype.includes(searchElement, fromIndex)`.
- [ ] Implement `Array.prototype.flat(depth)` and `flatMap(callback)`.
- [x] Implement `Array.prototype.sort()`.
- [x] Implement `Array.prototype.join(separator)`.

### Map Methods
- [x] Implement `Map.prototype.clear()`.
- [x] Implement `Map.prototype.delete(key)`.
- [ ] Implement `Map.prototype.entries()`.
- [ ] Implement `Map.prototype.values()`.
- [ ] Implement `Map.prototype.forEach(callback)`.

## Verification Plan
- Create integration tests in `tests/integration/` for each new method.
- Ensure `tsc` correctly identifies these methods in the `Analyzer`.
