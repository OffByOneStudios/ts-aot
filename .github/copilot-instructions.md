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

## Development Workflow
Follow this cycle for all development tasks:

1.  **Context:** Always check the active Epic in `docs/epics/` to understand the current goal.
2.  **Plan:** If the Epic is incomplete, help the user flesh out Milestones and Action Items.
3.  **Pick:** Identify the next unchecked Action Item.
4.  **Implement:** Write the code, ensuring it compiles and follows the Technical Constraints.
5.  **Verify:**
    *   Run `cmake --build build`.
    *   Run tests (create new ones if needed).
    *   **Stop** if the build fails and fix it.
6.  **Commit:** Run `git add .` and `git commit` with a descriptive message referencing the task.
7.  **Update:** Check off `[x]` the completed task in the Epic markdown file.
8.  **Loop:** Report completion to the user and ask to proceed to the next Action Item.
