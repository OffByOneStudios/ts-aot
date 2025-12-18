# Phase 11: Language Parity & Generics

**Status:** Not Started
**Goal:** Complete the remaining core TypeScript language features to reach full feature parity with the roadmap. This includes Generics, Modules, and various "syntactic sugar" and advanced type features that were previously deferred.

## Milestones

### Epic 47: Generics
Support parameterized types for classes, interfaces, and functions.
- [ ] Generic classes (`class Box<T>`).
- [ ] Generic functions (`function identity<T>(arg: T): T`).
- [ ] Type constraints (`T extends Base`).
- [ ] Generic interfaces.

### Epic 48: Modules (ESM)
Support multi-file compilation and code organization.
- [ ] `export` statement.
- [ ] `import` statement.
- [ ] Module resolution and linking.
- [ ] Support for `index.ts` and relative imports.

### Epic 49: Structural Types & Sugar
Implement missing structural types and common syntactic sugar.
- [ ] **Tuples**: Fixed-length, mixed-type arrays.
- [ ] **Enums**: Numeric and String enums.
- [ ] **Type Aliases**: `type ID = string`.
- [ ] **for..in**: Object key iteration.
- [ ] **Function Expressions**: `const foo = function() {}`.

### Epic 50: Advanced Function Features
Enhance function definitions with modern flexibility.
- [ ] **Optional Parameters**: `function foo(x?: number)`.
- [ ] **Default Parameters**: `function foo(x = 1)`.
- [ ] **Rest Parameters**: `function foo(...args)`.

### Epic 51: Advanced Types & Parity
Implement safer top/bottom types and finalize the type system.
- [ ] **unknown**: Safer `any`.
- [ ] **never**: Unreachable code analysis.
- [ ] **Strict Null Checks**: (Optional/Partial) Better handling of `null` and `undefined`.

## Action Items
- [ ] Create detailed epic files for each of the above.
- [ ] Prioritize Generics as the first major hurdle.
- [ ] Implement `import`/`export` to allow splitting the runtime and tests.
