# Epic 45: Async/Await Transformation

Implement the compiler pass to transform `async` functions into state-machine based generators.

## Milestones
- [x] Update AST to support `AsyncKeyword` and `AwaitExpression`.
- [x] Implement the "Async-to-Generator" transformation in the `Monomorphizer` or a new pass.
- [x] Generate state machine IR that can suspend and resume execution.

## Action Items
- [x] Add `AsyncKeyword` to `TokenKind` and `AstLoader`.
- [x] Add `AwaitExpression` to AST and `AstLoader`.
- [x] Update `Analyzer` to handle `async` functions and `await` expressions.
- [x] Implement state machine transformation logic.
- [x] Generate IR for state machine transitions.
- [x] Verify with `tests/integration/async_test.ts`.
