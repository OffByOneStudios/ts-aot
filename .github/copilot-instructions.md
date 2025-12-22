# TypeScript AOT Compiler (ts-aot) - Copilot Instructions

You are an expert C++ developer working on `ts-aot`, an Ahead-of-Time compiler for TypeScript.

## Technical Constraints & Architecture
*   **Language Standard:** C++20.
*   **Build System:** CMake with `vcpkg` for dependency management.
*   **Project Structure:**
    *   `src/compiler`: The host compiler (runs on dev machine).
    *   `src/runtime`: The target runtime library (linked into generated code).
*   **Runtime Rules (Strict):**
    *   **Memory:** NEVER use `new`/`malloc` directly for runtime objects. Use `ts_alloc` (wraps Boehm GC).
    *   **Strings:** NEVER use `std::string` for runtime values. Use `TsString` (ICU-based).
*   **Async:** Use `libuv` for the event loop.
*   **IO:** Use `ts_console_log`.
*   **LLVM 18 (Opaque Pointers):**
    *   **Pointers:** Use `builder->getPtrTy()` for all pointer types. NEVER use `getPointerTo()`.
    *   **GEP:** Always provide the source element type: `builder->CreateGEP(type, ptr, indices)`.
    *   **Load/Store:** Always provide the type to `CreateLoad(type, ptr)`.
    *   **Calls:** Always provide the `FunctionType` to `CreateCall(ft, callee, args)`.
    *   **Type Safety:** LLVM 18 does NOT check pointer types (they are all `ptr`), but it STRICTLY checks that the types in `args` match the `FunctionType` passed to `CreateCall`. If they don't, it will crash with "Calling a function with a bad signature!".
    *   **Casting:** Use `CreateBitCast` for pointer-to-pointer (though often redundant), `CreateIntCast` for integers, and `CreateFPToSI`/`CreateSIToFP` for numeric conversions. Always ensure arguments match the `FunctionType` exactly.

## Development Workflow
Follow this cycle for all development tasks:

1.  **Context:** 
    *   **CRITICAL:** Always read `.github/context/active_state.md` at the start of a session to understand the current phase and active tasks.
    *   Check `docs/epics/` or `docs/phase*/` for the specific Epic details.
2.  **Search:** Use the `ctags-search` skill (via `.github/skills/ctags-search/search_tags.ps1`) to find symbol definitions and locations. This is PREFERRED over `grep_search` for symbol lookups.
3.  **Plan:** If the Epic is incomplete, help the user flesh out Milestones and Action Items.
4.  **Pick:** Identify the next unchecked Action Item.
4.  **Implement:** Write the code, ensuring it compiles and follows the Technical Constraints.
5.  **Verify:**
    *   Run `cmake --build build`.
    *   Run tests (create new ones if needed).
    *   **Stop** if the build fails and fix it.
6.  **Commit:** Run `git add .` and `git commit` with a descriptive message referencing the task.
7.  **Update:** Check off `[x]` the completed task in the Epic markdown file.
8.  **Loop:** Report completion to the user and ask to proceed to the next Action Item.
