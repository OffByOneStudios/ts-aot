# Known Issues & Technical Debt

## Critical Blockers
*   *None currently identified.*

## Recently Fixed (Dec 2025)
*   **Contextual Typing:** Callback parameters now correctly infer types from call context (was always `Any`).
*   **Virtual Inheritance Casting:** Added AsXxx() helpers for safe casting in stream class hierarchy.
*   **Boxing/Unboxing Inconsistency:** Added ts_value_get_* helpers for consistent unboxing.

## Runtime Gaps
*   **Missing Types:** `TypedArray`, `DataView`, `BigInt`, `Symbol` (partial).
*   **Missing APIs:** Most of `util`, `url`, `crypto` are unimplemented.
*   **Error Handling:** `try/catch` is not yet implemented (no C++ exception mapping).

## Compiler Limitations
*   **Parser:** We rely on `dump_ast.js` (Node.js script) to parse TypeScript. We cannot self-host yet.
*   **Generics:** Implementation is basic (monomorphization), might explode code size for complex types.
*   **Source Maps:** No source map generation for debugging generated binaries.
*   **Contextual Typing Scope:** Only covers known APIs (http, https, net, fs). Custom callbacks still need explicit types.

## Developer Experience
*   **Manual Workflow:** User must run `node dump_ast.js` -> `ts-aot` -> `cmake build`. (Addressed in Phase 16).
*   **Debug Output:** Use `--dump-types` flag or spdlog for diagnostics. See `.github/DEVELOPMENT.md`.
