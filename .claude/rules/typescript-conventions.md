---
paths: examples/**/*.ts
priority: medium
---

# TypeScript File Conventions

Guidelines for writing TypeScript files in this project.

## CRITICAL: File Location Rules

**⛔ NEVER create test files or debug scripts in `examples/`**

| File Type | Location | Purpose |
|-----------|----------|---------|
| Temporary tests | `tmp/` | Ad-hoc testing, debugging, reproduction scripts |
| Debug scripts | `tmp/` | Crash investigation, minimal reproductions |
| Benchmarks | `examples/benchmarks/` | Performance comparison suite |
| Production templates | `examples/production/` | Real-world application patterns |
| Conformance tests | `tests/node/` | Node.js API conformance tests |
| Golden IR tests | `tests/golden_ir/` | Compiler regression tests |

The `examples/` directory is **reserved** for polished, production-ready code that demonstrates ts-aot capabilities. It is NOT a scratch pad for testing.

## Entry Point

**All test files must have a `user_main` function:**

```typescript
function user_main(): number {
  // Your test code here
  return 0;  // Return 0 for success
}
```

**Why:** The compiler generates a `main()` function that calls `user_main()`.

## Type Annotations

**Prefer explicit types for function parameters:**

```typescript
// ✅ GOOD - explicit types
function add(a: number, b: number): number {
  return a + b;
}

// ⚠️ OK but less optimal - contextual typing works for some APIs
http.createServer((req, res) => {
  // req and res are inferred from context
});

// ❌ AVOID - any type (slow path)
function add(a, b) {
  return a + b;
}
```

## Console Output

**Use `console.log` for output:**

```typescript
console.log("Hello, world!");
console.log(42);
console.log(3.14);
console.log(arr.join(','));
```

**Don't use:**
- `console.error` (not implemented)
- `console.warn` (not implemented)
- `console.dir` (not implemented)

## Arrays

**Typed arrays are optimized:**

```typescript
const numbers: number[] = [1, 2, 3, 4, 5];
const result = numbers.map(x => x * 2);
```

**Array methods available:**
- `push`, `pop`, `shift`, `unshift`
- `map`, `filter`, `reduce`
- `forEach`, `find`, `findIndex`
- `indexOf`, `includes`
- `join`, `reverse`, `sort`
- `slice`, `concat`, `flat`

## Objects

**Object literals work as expected:**

```typescript
const obj = {
  name: "Alice",
  age: 30,
  greet() {
    console.log(`Hello, ${this.name}!`);
  }
};
```

**Property access:**
```typescript
console.log(obj.name);           // Static access
console.log(obj["age"]);         // Dynamic access
```

**Object methods:**
- `Object.keys(obj)`
- `Object.values(obj)`
- `Object.entries(obj)` ⚠️ (currently returns empty array)
- `Object.assign(target, source)` ⚠️ (crashes compiler)

## Classes

**Classes are fully supported:**

```typescript
class Person {
  private name: string;
  age: number;

  constructor(name: string, age: number) {
    this.name = name;
    this.age = age;
  }

  greet(): void {
    console.log(`Hello, I'm ${this.name}!`);
  }
}

const alice = new Person("Alice", 30);
alice.greet();
```

## Functions

**Function declarations are hoisted:**

```typescript
greet();  // Works - hoisted

function greet() {
  console.log("Hello!");
}
```

**Arrow functions:**

```typescript
const add = (a: number, b: number) => a + b;
const greet = () => console.log("Hello!");
```

**Closures:**

```typescript
function makeCounter(): () => number {
  let count = 0;
  return () => ++count;
}

const counter = makeCounter();
console.log(counter());  // 1
console.log(counter());  // 2
```

## Control Flow

**All standard control flow works:**

```typescript
// if-else
if (x > 5) {
  console.log("greater");
} else {
  console.log("lesser");
}

// for loop
for (let i = 0; i < 10; i++) {
  console.log(i);
}

// while loop
while (condition) {
  // ...
}

// switch
switch (value) {
  case 1:
    console.log("one");
    break;
  case 2:
    console.log("two");
    break;
  default:
    console.log("other");
}
```

## Async/Await

**Promises and async/await work:**

```typescript
async function fetchData(): Promise<string> {
  return "data";
}

async function user_main(): Promise<number> {
  const data = await fetchData();
  console.log(data);
  return 0;
}
```

## Node.js APIs

**Available modules:**
- `fs` - File system (sync and async)
- `http`, `https` - HTTP server and client
- `net` - TCP sockets
- `path` - Path utilities
- `stream` - Streams and events
- `buffer` - Binary buffers
- `process` - Process information
- `url` - URL parsing
- `util` - Utility functions

**Import style:**

```typescript
import * as fs from 'fs';
import * as http from 'http';

// Use them
const content = fs.readFileSync('file.txt', 'utf8');
```

## Known Limitations

**Not Yet Implemented:**
- `try/catch` - No exception handling
- `async iterators` - `for await`
- `generators` - `function*` and `yield`
- `decorators` - `@decorator`
- `TypedArray` - `Uint8Array`, etc.
- `Symbol` - Partial support only
- `BigInt` - Not implemented
- `Proxy`, `Reflect` - Not implemented

## Golden IR Tests

**Add regression tests for new features:**

```typescript
// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_array_push
// OUTPUT: 1,2,3

function user_main(): number {
  const arr: number[] = [];
  arr.push(1, 2, 3);
  console.log(arr.join(','));
  return 0;
}
```

See `tests/golden_ir/` for examples.

## File Organization

```
examples/                    # Production-ready examples ONLY
├── benchmarks/             # Performance benchmarks
│   ├── harness/           # Benchmark utilities
│   ├── startup/           # Startup time benchmarks
│   ├── compute/           # CPU-intensive benchmarks
│   ├── io/                # I/O benchmarks
│   └── memory/            # Memory allocation benchmarks
└── production/            # Production-ready templates
    ├── api-server/        # REST API server
    ├── cli-app/           # Command-line application
    ├── worker/            # Background job processor
    └── data-processor/    # ETL data pipeline

tmp/                        # Temporary files (gitignored)
├── test.ts                # Ad-hoc test files
├── debug_*.ts             # Debug scripts
└── repro_*.ts             # Bug reproductions

tests/                      # Formal test suites
├── node/                  # Node.js API conformance tests
└── golden_ir/             # Compiler regression tests
```

**Remember:** When debugging or testing, use `tmp/` - NEVER `examples/`.

## Debugging Tips

**Dump inferred types:**
```bash
build/src/compiler/Release/ts-aot.exe tmp/test.ts --dump-types
```

**Dump generated IR:**
```bash
build/src/compiler/Release/ts-aot.exe tmp/test.ts --dump-ir
```

**Analyze crashes:**
```powershell
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath tmp\test.exe
```

## Performance Considerations

**Avoid:**
- Using `any` type (forces boxing/unboxing)
- Dynamic property access when static access works
- Unnecessary object allocations in loops

**Prefer:**
- Explicit types for parameters and returns
- Static property access (`obj.prop` vs `obj["prop"]`)
- Primitive types when possible
- Array methods over manual loops (they're optimized)

## Example Test File

```typescript
// RUN: ts-aot %s -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: Sum: 55

function sum(arr: number[]): number {
  return arr.reduce((acc, val) => acc + val, 0);
}

function user_main(): number {
  const numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
  const result = sum(numbers);
  console.log(`Sum: ${result}`);
  return 0;
}
```

## Related Documentation

- @docs/phase19/epic_106_golden_ir_tests.md - Test suite documentation
- @.github/DEVELOPMENT.md - Development guidelines
- @.claude/rules/llvm-ir-patterns.md - IR generation patterns
