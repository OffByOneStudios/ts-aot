# Project Roadmap: From Prototype to Production

This document outlines the strategic roadmap for `ts-aot`, moving from a proof-of-concept compiler to a production-grade runtime capable of self-hosting.

## Phase 16: Developer Experience & Workflow (Immediate)
**Goal:** Streamline the development process for both the AI agents and human users.
*   **Epic 85: The `ts-aot` CLI:** A unified command-line tool to replace manual CMake/Script steps.
*   **Epic 86: Context Persistence:** A structured system for AI context recovery.
*   **Epic 87: Portable Distribution:** Standalone binary releases.

## Phase 17: Language Completeness
**Goal:** Support the full TypeScript language specification.
*   **Epic 90: Async/Await:** State machine transformation for asynchronous code.
*   **Epic 91: Generators & Iterators:** Implementation of `function*` and `yield`.
*   **Epic 92: Advanced Classes:** Decorators, Mixins, and Abstract Classes.
*   **Epic 93: Error Handling:** Full `try/catch/finally` support.

## Phase 18: Node.js Runtime Compatibility
**Goal:** Implement the core Node.js APIs required by the ecosystem.
*   **Epic 95: File System (fs):** Sync and Async file operations via libuv.
*   **Epic 96: Streams & Events:** The backbone of Node.js I/O.
*   **Epic 97: Module Resolution:** Node-style `require` and `node_modules` support.

## Phase 19: Ecosystem Validation
**Goal:** Prove capability by compiling popular npm packages.
*   **Epic 100: Utility Libraries:** `lodash`, `date-fns`, `uuid`.
*   **Epic 101: Server Frameworks:** `express`, `fastify`.
*   **Epic 102: Reactive Programming:** `rxjs`.

## Phase 20: Self-Hosting (Bootstrapping)
**Goal:** Compile the TypeScript compiler itself.
*   **Epic 105: Compiling `tsc`:** Run the TypeScript compiler source through `ts-aot`.
*   **Epic 106: In-Process Compilation:** Use the compiled `tsc` to parse TS files at runtime, removing the `dump_ast.js` dependency.

## Phase 21: Advanced Runtime Engineering
**Goal:** Optimize the runtime for size and memory performance.
*   **Epic 110: Static Linking:** Remove DLL dependencies (ICU, OpenSSL).
*   **Epic 111: Custom Garbage Collector:** Replace Boehm GC with a generational, compacting GC.
