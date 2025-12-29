# TypeScript AOT Compiler (ts-aot) - Copilot Instructions

You are an expert C++ developer working on `ts-aot`, an Ahead-of-Time compiler for TypeScript.

**See also:** `.github/DEVELOPMENT.md` for detailed development guidelines.

## ⛔ STOP AND CHECK - Before ANY Runtime Edit ⛔

Before editing ANY file in `src/runtime/`, verify your code uses these patterns:

| Task | ✅ CORRECT | ❌ WRONG |
|------|-----------|----------|
| Allocate object | `ts_alloc(sizeof(T))` + placement new | `new T()` or `malloc` |
| Create string | `TsString::Create("...")` | `std::string` |
| Cast to base/derived | `obj->AsEventEmitter()` or `dynamic_cast<T*>(obj)` | `(T*)ptr` C-style cast |
| Unbox void* param | `ts_value_get_object((TsValue*)p)` | Assume it's raw pointer |
| Create error | `ts_error_create("msg")` (already boxed!) | Double-box with `ts_value_make_object` |

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
*   **Virtual Inheritance & Safe Casting (CRITICAL):**
    *   The runtime uses virtual inheritance for Node.js-like stream classes (e.g., `TsReadable : public virtual TsEventEmitter`).
    *   **NEVER use C-style casts or pointer arithmetic** for virtual inheritance classes.
    *   **ALWAYS use `AsXxx()` helpers or `dynamic_cast`** when casting between base and derived classes:
        ```cpp
        // CORRECT - use AsXxx() helpers
        TsEventEmitter* e = ((TsObject*)ptr)->AsEventEmitter();
        TsWritable* w = ((TsObject*)ptr)->AsWritable();
        
        // ALSO CORRECT - use dynamic_cast
        TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)ptr);
        ```
*   **Boxing & Unboxing Convention:**
    *   Many C API functions receive `void*` that may be boxed `TsValue*` or raw pointers.
    *   **ALWAYS use `ts_value_get_*` helpers** for consistent unboxing:
        ```cpp
        void* rawPtr = ts_value_get_object((TsValue*)param);
        if (!rawPtr) rawPtr = param;  // Fallback if not boxed
        ```
    *   **Unboxing helpers:** `ts_value_get_object`, `ts_value_get_int`, `ts_value_get_double`, `ts_value_get_bool`, `ts_value_get_string`
    *   **Track boxed values:** In codegen, always call `boxedValues.insert(value)` after boxing.
*   **LLVM 18 (Opaque Pointers):**
    *   **Pointers:** Use `builder->getPtrTy()` for all pointer types. NEVER use `getPointerTo()`.
    *   **GEP:** Always provide the source element type: `builder->CreateGEP(type, ptr, indices)`.
    *   **Load/Store:** Always provide the type to `CreateLoad(type, ptr)`.
    *   **Calls:** Always provide the `FunctionType` to `CreateCall(ft, callee, args)`.
    *   **Type Safety:** LLVM 18 STRICTLY checks that the types in `args` match the `FunctionType` passed to `CreateCall`.

## Contextual Typing (NEW)
*   Arrow function parameters now infer types from call context.
*   To add contextual typing for a new API, edit `getExpectedCallbackType()` in `Analyzer_Expressions_Calls.cpp`.
*   The analyzer uses a `contextualTypeStack` to propagate expected types to nested arrow functions.

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
    *   Run `cmake --build build` to ensure the compiler and runtime are up to date.
    *   Run the compiler directly: `build/src/compiler/Release/ts-aot.exe examples/your_test.ts -o examples/your_test.exe`.
    *   **Debug types:** Use `--dump-types` flag to see inferred types.
    *   **Performance Testing:** Performance regression guards MUST be run against Release builds.
    *   **Regression Guard:** Use `// CHECK:` for IR verification and `// TYPE-CHECK:` for type inference snapshots in the `.ts` file footer.
    *   **Stop** if the build or verification fails and fix it.
6.  **Commit:** Run `git add .` and `git commit` with a descriptive message referencing the task.
7.  **Update:** Check off `[x]` the completed task in the Epic markdown file.
8.  **Loop:** Report completion to the user and ask to proceed to the next Action Item.
