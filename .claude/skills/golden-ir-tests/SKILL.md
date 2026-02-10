---
name: golden-ir-tests
description: Run golden IR regression test suite. Use when validating compiler correctness, checking for IR regressions, or user mentions "golden tests", "IR tests", "test runner", "regression tests", or "validate codegen".
allowed-tools: Bash, Read, Grep
---

# Golden IR Tests Skill

Run the comprehensive golden IR regression test suite to validate compiler correctness and prevent IR regressions.

## When to Use

**Trigger terms:** golden tests, IR tests, regression tests, test runner, validate codegen, check tests, run tests

- After making compiler changes to verify no regressions
- Before committing code changes
- To validate specific features work correctly
- When debugging IR generation issues
- To check test suite status

## Prerequisites

- Compiler built: `cmake --build build --config Release`
- Python 3.7+ installed
- Test files in `tests/golden_ir/`

## Instructions

### 1. Run All Tests

```bash
python tests/golden_ir/runner.py
```

This runs the entire test suite (90+ tests) and reports:
- Number of tests passed
- Number of tests failed
- Overall pass rate

### 2. Run Specific Test Category

```bash
# TypeScript tests only
python tests/golden_ir/runner.py tests/golden_ir/typescript/

# JavaScript tests only
python tests/golden_ir/runner.py tests/golden_ir/javascript/

# Specific subcategory
python tests/golden_ir/runner.py tests/golden_ir/typescript/arrays/
```

### 3. Run Single Test

```bash
python tests/golden_ir/runner.py tests/golden_ir/typescript/arrays/array_map.ts
```

### 4. Verbose Output

```bash
python tests/golden_ir/runner.py --verbose tests/golden_ir/typescript/arrays/array_map.ts
```

Shows:
- Compilation command
- Generated IR
- CHECK pattern matching details
- Runtime output
- Detailed failure reasons

### 5. Update Mode (Future)

```bash
python tests/golden_ir/runner.py --update tests/golden_ir/typescript/arrays/array_map.ts
```

Re-generates CHECK patterns for the test (useful after intentional IR changes).

## Understanding Test Results

### Passed Test
```
✓ typescript/arrays/array_map.ts
```
All CHECK patterns matched and output was correct.

### Failed Test
```
✗ typescript/arrays/array_find.ts
  CHECK pattern not found: call {{.*}} @ts_array_find
  Expected output: 30
  Actual output: 2102074279760
```

Indicates:
- Which CHECK pattern failed
- What output was expected vs actual
- Line numbers of failures

## Test File Format

Each test contains:

```typescript
// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_array_push
// CHECK-NOT: ts_value_get_object
// OUTPUT: 2,4,6

function user_main(): number {
  const arr = [1, 2, 3];
  const mapped = arr.map(x => x * 2);
  console.log(mapped.join(','));
  return 0;
}
```

**Directives:**
- `// RUN:` - How to compile and run the test
- `// CHECK:` - Pattern that must appear in IR (in order)
- `// CHECK-NOT:` - Pattern that must NOT appear
- `// CHECK-NEXT:` - Pattern must be on next non-comment line
- `// CHECK-DAG:` - Pattern may appear in any order
- `// OUTPUT:` - Expected stdout
- `// OUTPUT-REGEX:` - Regex for expected stdout
- `// EXIT-CODE:` - Expected exit code (default 0)
## Common Test Patterns

### Verify Function Call

```typescript
// CHECK: call {{.*}} @ts_array_map
arr.map(x => x * 2);
```

### Verify No Unnecessary Boxing

```typescript
// CHECK-NOT: ts_value_make_int
// CHECK-NOT: ts_value_get_object
const x = 5 + 3;
```

### Verify Optimization Applied

```typescript
// CHECK: icmp slt
// CHECK-NOT: call {{.*}} @ts_value_to_bool
if (x < 10) { }
```

## Analyzing Failures

### Step 1: Run Test in Verbose Mode

```bash
python tests/golden_ir/runner.py --verbose tests/golden_ir/typescript/arrays/array_find.ts
```

### Step 2: Examine Generated IR

Look for the CHECK pattern that failed:
- Is the function being called at all?
- Is it being inlined?
- Is there a typo in the pattern?

### Step 3: Check Runtime Output

If output mismatch:
- Is the value being printed correctly?
- Is there a boxing/unboxing issue?
- Is the function returning the right type?

### Step 4: Reproduce Manually

```powershell
# Compile with IR dump
build\src\compiler\Release\ts-aot.exe tests\golden_ir\typescript\arrays\array_find.ts --dump-ir -o test.exe

# Run executable
.\test.exe

# Check IR output
# Look for the patterns mentioned in CHECK directives
```

### Step 5: Fix and Re-test

Make the necessary changes to codegen, then:

```bash
python tests/golden_ir/runner.py tests/golden_ir/typescript/arrays/array_find.ts
```

## Test Coverage

### TypeScript Tests (60 tests)
- Arrays (20 tests): map, filter, reduce, push, pop, etc.
- Objects (15 tests): literals, property access, Object.keys, etc.
- Functions (15 tests): closures, higher-order, recursion, etc.
- Control Flow (10 tests): if/else, loops, switch, etc.

### JavaScript Tests (30 tests)
- Dynamic Types (10 tests): type coercion, typeof, loose equality
- Property Access (10 tests): dynamic keys, for-in, property chains
- Closures & IIFEs (10 tests): IIFE patterns, module pattern

## Current Status

As of Epic 106 completion:
- **TypeScript**: 63/90 passing (70%)
- **JavaScript**: 30/30 passing (100%) 🎉
- **Overall**: 93/120 tests (78%)

## Integration with Development Workflow

### Before Committing

```bash
# Run full test suite
python tests/golden_ir/runner.py

# If all pass, commit
git add .
git commit -m "Fix array_find return value"
```

### After Making Codegen Changes

```bash
# Run affected category
python tests/golden_ir/runner.py tests/golden_ir/typescript/arrays/

# Check for regressions
```

### Adding New Feature

1. Write test case in appropriate directory
2. Add CHECK patterns for expected IR
3. Add OUTPUT for expected runtime behavior
4. Run test: `python tests/golden_ir/runner.py path/to/test.ts`
5. If test fails, fix the implementation
6. Re-run until passing

## Troubleshooting

### "Python not found"
Install Python 3.7+ from python.org

### "Compiler not found"
Build the compiler first: `cmake --build build --config Release`

### "Test file not found"
Check path is correct: `tests/golden_ir/typescript/...`

### "All tests failing"
- Verify compiler builds successfully
- Check compiler path in runner.py
- Ensure test files have correct format

## Advanced Usage

### Filter Tests by Pattern

```bash
# Run all array tests
python tests/golden_ir/runner.py tests/golden_ir/typescript/arrays/

# Run specific test type
find tests/golden_ir -name "*map*.ts" | xargs python tests/golden_ir/runner.py
```

### Generate Test Report

```bash
python tests/golden_ir/runner.py > test_results.txt
```

### CI Integration (Future)

```yaml
# .github/workflows/golden_ir_tests.yml
- name: Run Golden IR Tests
  run: python tests/golden_ir/runner.py
- name: Upload Results
  if: failure()
  uses: actions/upload-artifact@v2
  with:
    name: test-failures
    path: test_results.txt
```

## Related Documentation

- @docs/phase19/epic_106_golden_ir_tests.md - Full epic documentation
- @tests/golden_ir/runner.py - Test runner implementation
- @.github/DEVELOPMENT.md - Development guidelines
- LLVM FileCheck: https://llvm.org/docs/CommandGuide/FileCheck.html
