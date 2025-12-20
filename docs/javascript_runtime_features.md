# JavaScript Runtime Features Audit

This document tracks the implementation status of standard JavaScript/TypeScript built-in APIs in the `ts-aot` runtime.

## Core Types

### String (`TsString`)
Implemented using ICU for UTF-16 support.

| Method | Status | Notes |
| :--- | :--- | :--- |
| `length` | ✅ Implemented | |
| `charAt` | ✅ Implemented | |
| `charCodeAt` | ✅ Implemented | |
| `concat` | ❌ Missing | |
| `includes` | ✅ Implemented | |
| `endsWith` | ❌ Missing | |
| `indexOf` | ✅ Implemented | |
| `lastIndexOf` | ❌ Missing | |
| `match` | ❌ Missing | Needs `TsRegExp` integration |
| `matchAll` | ❌ Missing | |
| `padEnd` | ❌ Missing | |
| `padStart` | ❌ Missing | |
| `repeat` | ❌ Missing | |
| `replace` | ❌ Missing | |
| `replaceAll` | ❌ Missing | |
| `search` | ❌ Missing | |
| `slice` | ❌ Missing | |
| `split` | ✅ Implemented | Basic string split |
| `startsWith` | ✅ Implemented | |
| `substring` | ✅ Implemented | |
| `toLowerCase` | ✅ Implemented | |
| `toUpperCase` | ✅ Implemented | |
| `trim` | ✅ Implemented | |
| `trimEnd` | ❌ Missing | |
| `trimStart` | ❌ Missing | |

### Array (`TsArray`)
Implemented as a dynamic array of `int64_t` (often pointers to `TsValue` or objects).

| Method | Status | Notes |
| :--- | :--- | :--- |
| `length` | ✅ Implemented | |
| `at` | ❌ Missing | |
| `concat` | ❌ Missing | |
| `every` | ❌ Missing | |
| `fill` | ❌ Missing | |
| `filter` | ❌ Missing | |
| `find` | ❌ Missing | |
| `findIndex` | ❌ Missing | |
| `flat` | ❌ Missing | |
| `flatMap` | ❌ Missing | |
| `forEach` | ❌ Missing | |
| `includes` | ❌ Missing | |
| `indexOf` | ✅ Implemented | |
| `join` | ✅ Implemented | |
| `lastIndexOf` | ❌ Missing | |
| `map` | ❌ Missing | |
| `pop` | ❌ Missing | |
| `push` | ✅ Implemented | |
| `reduce` | ❌ Missing | |
| `reduceRight` | ❌ Missing | |
| `reverse` | ❌ Missing | |
| `shift` | ❌ Missing | |
| `slice` | ✅ Implemented | |
| `some` | ❌ Missing | |
| `sort` | ✅ Implemented | Basic numeric/string sort |
| `splice` | ❌ Missing | |
| `unshift` | ❌ Missing | |

### Map (`TsMap`)
Implemented using `std::unordered_map`.

| Method | Status | Notes |
| :--- | :--- | :--- |
| `size` | ✅ Implemented | |
| `clear` | ❌ Missing | |
| `delete` | ❌ Missing | |
| `entries` | ❌ Missing | |
| `forEach` | ❌ Missing | |
| `get` | ✅ Implemented | |
| `has` | ✅ Implemented | |
| `keys` | ✅ Implemented | Returns `TsArray` |
| `set` | ✅ Implemented | |
| `values` | ❌ Missing | |

### Math
Implemented using `<cmath>` and `<random>`.

| Method | Status | Notes |
| :--- | :--- | :--- |
| `abs` | ✅ Implemented | |
| `ceil` | ✅ Implemented | |
| `floor` | ✅ Implemented | |
| `max` | ✅ Implemented | |
| `min` | ✅ Implemented | |
| `pow` | ✅ Implemented | |
| `random` | ✅ Implemented | |
| `round` | ✅ Implemented | |
| `sqrt` | ✅ Implemented | |
| `sin`/`cos`/`tan` | ✅ Implemented | |
| `asin`/`acos`/`atan`/`atan2` | ✅ Implemented | |
| `log`/`log10`/`log2` | ✅ Implemented | |
| `exp` | ✅ Implemented | |
| `trunc` | ✅ Implemented | |
| `sign` | ✅ Implemented | |
| `clz32` | ✅ Implemented | |
| `PI` | ✅ Implemented | |
| `E` | ✅ Implemented | |

### JSON
Implemented using `nlohmann/json`.

| Method | Status | Notes |
| :--- | :--- | :--- |
| `parse` | ✅ Implemented | |
| `stringify` | ✅ Implemented | |

### Date (`TsDate`)
Basic implementation using `<chrono>`.

| Method | Status | Notes |
| :--- | :--- | :--- |
| `now()` | ✅ Implemented | |
| `getTime` | ✅ Implemented | |
| `getFullYear` | ✅ Implemented | |
| `getMonth` | ✅ Implemented | |
| `getDate` | ✅ Implemented | |
| `getDay` | ✅ Implemented | |
| `getHours` | ✅ Implemented | |
| `getMinutes` | ✅ Implemented | |
| `getSeconds` | ✅ Implemented | |
| `getMilliseconds` | ✅ Implemented | |
| `getUTC*` | ✅ Implemented | All UTC getters implemented |
| `set*` | ✅ Implemented | All local setters implemented |
| `setUTC*` | ✅ Implemented | All UTC setters implemented |
| `toISOString` | ✅ Implemented | |
| `toString` | ✅ Implemented | |

### RegExp (`TsRegExp`)
Implemented using ICU Regex.

| Method | Status | Notes |
| :--- | :--- | :--- |
| `test` | ✅ Implemented | |
| `exec` | ✅ Implemented | |
| `flags` | ❌ Missing | |
| `source` | ❌ Missing | |

### Promise (`TsPromise`)
Minimal implementation for `async`/`await` support.

| Method | Status | Notes |
| :--- | :--- | :--- |
| `then` | ✅ Implemented | |
| `catch` | ❌ Missing | |
| `finally` | ❌ Missing | |
| `all` | ❌ Missing | |
| `race` | ❌ Missing | |
| `resolve` | ✅ Implemented | |
| `reject` | ❌ Missing | |

### Object
The base object type.

| Method | Status | Notes |
| :--- | :--- | :--- |
| `assign` | ❌ Missing | |
| `create` | ❌ Missing | |
| `entries` | ❌ Missing | |
| `freeze` | ❌ Missing | |
| `fromEntries` | ❌ Missing | |
| `getOwnPropertyDescriptor` | ❌ Missing | |
| `getOwnPropertyNames` | ❌ Missing | |
| `getPrototypeOf` | ❌ Missing | |
| `hasOwn` | ❌ Missing | |
| `hasOwnProperty` | ❌ Missing | |
| `is` | ❌ Missing | |
| `keys` | ❌ Missing | |
| `values` | ❌ Missing | |

## Global Objects

| Object | Status | Notes |
| :--- | :--- | :--- |
| `console` | ✅ Partial | `log` implemented |
| `fs` | ✅ Partial | `readFileSync`, `writeFileSync`, `readFile` (async) |
| `crypto` | ✅ Partial | `md5` implemented |
| `process` | ✅ Implemented | `argv`, `env`, `cwd`, `exit` |
| `Buffer` | ✅ Implemented | `alloc`, `from`, `toString`, `length`, indexing |
| `URL` | ✅ Implemented | `href`, `protocol`, `host`, `hostname`, `port`, `pathname`, `search`, `hash` |
| `fetch` | ✅ Implemented | `Response`, `Headers`, `text()`, `json()` |
| `setTimeout` | ❌ Missing | |
| `setInterval` | ❌ Missing | |
