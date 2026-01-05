# Golden IR Tests

Regression tests that validate LLVM IR output against expected patterns.

## Running Tests

```bash
# Run all tests in a directory
python runner.py typescript
python runner.py javascript

# Run a single test
python runner.py typescript/arrays/array_map.ts

# Run with detailed output
python runner.py typescript --details
```

**Legacy PowerShell runner (deprecated):**
```powershell
.\runner.ps1 typescript
```

## Requirements

- Python 3.7+
- Compiler at `build/src/compiler/Release/ts-aot.exe`

## Writing Tests

Test files use embedded directives:

```typescript
// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: pattern to find in IR
// CHECK-NOT: pattern that should NOT appear
// CHECK-NEXT: pattern on next non-empty line
// CHECK-DAG: pattern anywhere in remaining IR
// OUTPUT: expected stdout
// EXIT-CODE: 0

function user_main(): number {
  // Test code here
  return 0;
}
```

### Directives

- **RUN:** Compilation command (`%s` = source file, `%t` = temp output)
- **CHECK:** IR pattern that must appear in order
- **CHECK-NOT:** Pattern that must NOT appear between adjacent CHECKs
- **CHECK-NEXT:** Must be on next non-comment line
- **CHECK-DAG:** Can appear anywhere (unordered)
- **OUTPUT:** Expected stdout text
- **OUTPUT-REGEX:** Expected stdout regex pattern
- **EXIT-CODE:** Expected exit code (default: 0)

### Pattern Syntax

- `{{.*}}` - Match anything (regex `.*`)
- `{{[0-9]+}}` - Match numbers
- `{{ptr|i64}}` - Match alternatives
- Literal text matches exactly

### Examples

**Basic array test:**
```typescript
// CHECK: call {{.*}} @__ts_array_create
const arr = [1, 2, 3];
```

**Optimization verification:**
```typescript
// CHECK-NOT: ts_value_get_object
// Ensures unboxing is not happening
```

**Output validation:**
```typescript
// OUTPUT: 42
console.log(42);
```

## Test Organization

```
typescript/
  arrays/       - Array operations
  objects/      - Object literals, property access
  functions/    - Functions, closures, IIFEs
  classes/      - Class definitions
  generics/     - Generic types
  control_flow/ - If/while/for/switch

javascript/
  dynamic_types/    - Slow-path type operations
  property_access/  - Dynamic property access
  operators/        - Dynamic operators (+ - * /)
  closures/         - JS closures and IIFEs
```

## CI Integration

Tests run automatically on:
- Every PR
- Every commit to main
- Daily at midnight

See `.github/workflows/golden_ir_tests.yml`
