# Epic 52: Collection Parity (String, Array, Map)

**Status:** Not Started
**Goal:** Implement missing functional and utility methods for `String`, `Array`, and `Map` types in the runtime.

## Background
The current runtime has basic support for strings, arrays, and maps, but lacks many of the higher-order functions (like `map`, `filter`) and utility methods (like `replace`, `slice`) that are standard in JavaScript.

## Action Items

### String Methods
- [ ] Implement `String.prototype.slice(start, end)` in `TsString.cpp`.
- [ ] Implement `String.prototype.concat(...strings)` in `TsString.cpp`.
- [ ] Implement `String.prototype.padStart(targetLength, padString)` and `padEnd`.
- [ ] Implement `String.prototype.repeat(count)`.
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
- [ ] Implement `Array.prototype.pop()`.
- [ ] Implement `Array.prototype.shift()` and `unshift(...items)`.
- [ ] Implement `Array.prototype.at(index)`.
- [ ] Implement `Array.prototype.includes(searchElement, fromIndex)`.
- [ ] Implement `Array.prototype.flat(depth)` and `flatMap(callback)`.

### Map Methods
- [ ] Implement `Map.prototype.clear()`.
- [ ] Implement `Map.prototype.delete(key)`.
- [ ] Implement `Map.prototype.entries()`.
- [ ] Implement `Map.prototype.values()`.
- [ ] Implement `Map.prototype.forEach(callback)`.

## Verification Plan
- Create integration tests in `tests/integration/` for each new method.
- Ensure `tsc` correctly identifies these methods in the `Analyzer`.
