# Epic 45: Async/Await Transformation

Implement the compiler pass to transform `async` functions into state-machine based generators.

## Milestones
- [ ] Update AST to support `AsyncKeyword` and `AwaitExpression`.
- [ ] Implement the "Async-to-Generator" transformation in the `Monomorphizer` or a new pass.
- [ ] Generate state machine IR that can suspend and resume execution.

## Action Items
- [ ] Add `AsyncKeyword` to `TokenKind` and `AstLoader`.
- [ ] Add `AwaitExpression` to AST and `AstLoader`.
- [ ] Update `Analyzer` to handle `async` functions and `await` expressions.
- [ ] Implement state machine transformation logic.
- [ ] Generate IR for state machine transitions.
- [ ] Verify with `tests/integration/async_test.ts`.
