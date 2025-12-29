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
*   **Virtual Inheritance & C API Bindings (CRITICAL):**
    *   The runtime uses virtual inheritance for Node.js-like stream classes (e.g., `TsReadable : public virtual TsEventEmitter`).
    *   **Pointer Layout:** TsObject has: offset 0 = C++ vtable (8 bytes), offset 8 = explicit magic/vtable member.
    *   **NEVER use pointer arithmetic** for virtual inheritance classes. With virtual inheritance, base class pointers are NOT at predictable offsets.
    *   **ALWAYS use `dynamic_cast<TargetClass*>`** when casting between base and derived classes with virtual inheritance.
    *   **Boxed Values:** Many C API functions receive `void*` that may be either:
        1. A raw object pointer (e.g., `TsEventEmitter*`)
        2. A boxed `TsValue*` (with `type` field <= 10 and `ptr_val` containing the actual object)
    *   **Unboxing Pattern:** Before using a `void*` parameter, check if it's a boxed TsValue:
        ```cpp
        TsValue* val = (TsValue*)param;
        void* rawPtr = param;
        if ((uint8_t)val->type <= 10 && val->type == ValueType::OBJECT_PTR) {
            rawPtr = val->ptr_val;  // Unbox
        }
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>((TsObject*)rawPtr);
        ```
    *   **TsMap Magic:** TsMap has magic value `0x4D415053` at offset 16 (not 8!) due to TsObject layout.
    *   **Error Objects:** `ts_error_create` returns an already-boxed `TsValue*` - do NOT double-box.
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
    *   Run `cmake --build build` to ensure the compiler and runtime are up to date.
    *   Run the compiler directly: `build/src/compiler/Release/ts-aot.exe examples/your_test.ts -o examples/your_test.exe`.
    *   **Performance Testing:** Performance regression guards MUST be run against Release builds.
    *   **Regression Guard:** Use `// CHECK:` for IR verification and `// TYPE-CHECK:` for type inference snapshots in the `.ts` file footer.
    *   **Stop** if the build or verification fails and fix it.
6.  **Commit:** Run `git add .` and `git commit` with a descriptive message referencing the task.
7.  **Update:** Check off `[x]` the completed task in the Epic markdown file.
8.  **Loop:** Report completion to the user and ask to proceed to the next Action Item.
