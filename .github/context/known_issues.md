# Known Issues & Technical Debt

## Critical Blockers
*   *None currently identified.*

## Runtime Gaps
*   **Missing Types:** `TypedArray`, `DataView`, `BigInt`, `Symbol`.
*   **Missing APIs:** Most of `fs`, `path`, `process` are unimplemented.
*   **Error Handling:** `try/catch` is not yet implemented (no C++ exception mapping).

## Compiler Limitations
*   **Parser:** We rely on `dump_ast.js` (Node.js script) to parse TypeScript. We cannot self-host yet.
*   **Generics:** Implementation is basic (monomorphization), might explode code size for complex types.
*   **Source Maps:** No source map generation for debugging generated binaries.

## Developer Experience
*   **Manual Workflow:** User must run `node dump_ast.js` -> `ts-aot` -> `cmake build`. (Addressed in Phase 16).
