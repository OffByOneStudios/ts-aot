# Golden IR Test Runner Skill

Runs the golden IR test suite to validate compiler output and runtime behavior.

## Quick Reference

```powershell
# Run all tests
python E:\src\github.com\cgrinker\ts-aoc\tests\golden_ir\runner.py E:\src\github.com\cgrinker\ts-aoc\tests\golden_ir

# Run all tests with details
python E:\src\github.com\cgrinker\ts-aoc\tests\golden_ir\runner.py E:\src\github.com\cgrinker\ts-aoc\tests\golden_ir --details

# Run a specific test file
python E:\src\github.com\cgrinker\ts-aoc\tests\golden_ir\runner.py E:\src\github.com\cgrinker\ts-aoc\tests\golden_ir\typescript\objects\optional_chaining.ts

# Run a specific test directory
python E:\src\github.com\cgrinker\ts-aoc\tests\golden_ir\runner.py E:\src\github.com\cgrinker\ts-aoc\tests\golden_ir\typescript\arrays
```

## Test File Format

Each test file should have directives in comments at the top:

```typescript
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: expected output line 1
// OUTPUT: expected output line 2
// CHECK: pattern to find in IR
// CHECK-NOT: pattern that should NOT appear in IR

function user_main(): number {
    console.log("expected output line 1");
    console.log("expected output line 2");
    return 0;
}
```

### Directives

| Directive | Description |
|-----------|-------------|
| `// RUN: <command>` | The command to execute. Use placeholders below. |
| `// OUTPUT: <text>` | Expected stdout line (order matters) |
| `// CHECK: <pattern>` | Pattern that must appear in IR output |
| `// CHECK-NOT: <pattern>` | Pattern that must NOT appear in IR output |
| `// CHECK-NEXT: <pattern>` | Pattern that must appear immediately after previous CHECK |
| `// CHECK-DAG: <pattern>` | Pattern that can appear in any order |
| `// EXIT-CODE: <n>` | Expected exit code (default: 0) |
| `// XFAIL: <reason>` | Mark test as expected failure with reason |

### Placeholders

| Placeholder | Expands To |
|-------------|------------|
| `%ts-aot` | Full path to `build/src/compiler/Release/ts-aot.exe` |
| `%s` | Full path to the test source file |
| `%t.exe` | Temp directory path for output executable |
| `%t.ll` | Temp directory path for IR output |
| `%t` | Base temp path (use as `%t.anything`) |

**⚠️ CRITICAL:** Always use `%ts-aot` (with the `%` prefix), NOT `ts-aot`. The bare command will not be found.

## Test Result States

| State | Meaning |
|-------|---------|
| `PASSED` | Test compiled, ran, and produced expected output |
| `FAILED` | Test did not produce expected output |
| `XFAIL` | Test was expected to fail and it did fail |
| `XPASS` | Test was expected to fail but it passed (good news!) |

## Directory Structure

```
tests/golden_ir/
├── runner.py           # Main test runner
├── runner.ps1          # PowerShell wrapper (deprecated)
├── javascript/
│   ├── closures/       # Closure tests
│   ├── dynamic_types/  # Dynamic type tests
│   └── property_access/# Property access tests
└── typescript/
    ├── arrays/         # Array method tests
    ├── control_flow/   # Control flow tests
    ├── functions/      # Function tests
    ├── objects/        # Object literal tests
    └── regression/     # Regression tests
```

## When to Use

- After making changes to the compiler or runtime
- Before committing code
- When adding new language features
- To verify a specific test case works

## Common Issues

### "ts-aot is not recognized"
The test file uses `ts-aot` instead of `%ts-aot`. Fix by adding the `%` prefix.

### Test passes locally but fails in CI
Make sure the test uses placeholders (`%ts-aot`, `%s`, `%t.exe`) instead of hardcoded paths.

### XFAIL test now passes
Remove the `// XFAIL:` line from the test file - this is good news!
