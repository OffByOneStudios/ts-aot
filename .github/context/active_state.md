# Active Project State

**Last Updated:** 2025-12-24
**Current Phase:** Phase 17 (Language Completeness)

## Current Focus
We are in **Phase 17: Language Completeness**, focusing on high-performance implementations of modern TypeScript features. We have just completed **Epic 93: Error Handling & Stack Traces**.

## Active Tasks
1.  **Epic 94:** Modern Syntax Sugar (Optional Chaining, Nullish Coalescing).
2.  **Epic 95:** Extended Types & Runtime (BigInt, Symbols, Set).

## Recent Accomplishments
*   **Epic 93: Error Handling & Stack Traces:** Implemented `try/catch/finally` with `setjmp/longjmp`. Added `Error` class and symbolicated stack traces on Windows using `DbgHelp` and LLVM `DIBuilder` for source line mapping.
*   **Advanced Classes:** Implemented Private Fields (`#field`), Static Blocks (`static {}`), and basic Comptime literal inlining (`ts_aot.comptime`). Fixed monomorphization issues with private fields in generic classes.
*   **Workspace Cleanup:** Removed obsolete test targets and redundant `find_package` calls from the root `CMakeLists.txt`. Cleaned up build artifacts (`.exe`, `.lib`, `.obj`, `.ll`, `.json`) from the source tree. Updated benchmark scripts to point to new executable locations.
*   **Build Hygiene:** Resolved all compiler warnings in the compiler and runtime. Fixed build errors in example projects (HTTP server, raytracer) by correcting CMake custom commands and adding missing Windows system libraries (`crypt32`).
*   **Async Iteration Fix:** Resolved a critical "double-boxing" crash in `for await...of` loops by ensuring boxing is idempotent and pointers remain stable across state machine suspensions.
*   **Logging Infrastructure:** Refactored the compiler to use `spdlog` macros (`SPDLOG_DEBUG`, etc.), enabling source-location metadata (file/line) and a cleaner, compiler-like output format without timestamps.
*   **CLI Enhancements:** Added `--log-level` flag to control compiler output verbosity.
*   **Phase 16:** Completed Single Binary Static Build and CLI Driver.
*   **Epic 90/91:** Implemented Async/Await and Generators with state machine transformation.

## Next Steps
1.  Implement the `ts-aot` CLI (Epic 85).
2.  Verify protocol compliance for custom async iterators (Task 90.7).
