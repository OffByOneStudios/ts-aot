# Epic 33: Error Handling (try/catch/throw)

**Goal:** Implement robust exception handling using `setjmp`/`longjmp` (cross-platform alternative to LLVM `invoke`/`landingpad`).

**Dependencies:**
- Phase 1-7 (Core Compiler)

## Milestones

### Milestone 33.1: Throw Statement
- [x] **AST:** Support `throw` statement.
- [x] **Analyzer:** Validate that `throw` takes an expression.
- [x] **CodeGen:** Implement `throw` by calling a runtime helper `ts_throw`.

### Milestone 33.2: Try/Catch Blocks
- [x] **AST:** Support `try`, `catch`, and `finally` blocks.
- [x] **Analyzer:** Implement scoping for the `catch` variable.
- [x] **CodeGen:** Use `_setjmp` and runtime exception stack to manage handlers.
- [x] **CodeGen:** Implement branching to `catch` block on exception.

### Milestone 33.3: Finally Blocks
- [x] **CodeGen:** Ensure `finally` blocks execute regardless of whether an exception was thrown or caught.
- [x] **CodeGen:** Handle `return` inside `try`/`catch` while still executing `finally`.

## Action Items
- [x] **Task 1:** Add `TryStatement` and `ThrowStatement` to AST and Parser.
- [x] **Task 2:** Implement `throw` in `IRGenerator` (calling runtime `ts_throw`).
- [x] **Task 3:** Implement `try/catch` using `_setjmp` and runtime stack.
- [x] **Task 4:** Implement `finally` block logic.
- [x] **Task 5:** Create integration tests for nested try/catch and finally.
