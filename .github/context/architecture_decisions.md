# Architecture Decision Record (ADR)

## 1. Language & Build System
*   **Language:** C++20. Chosen for modern features (concepts, ranges) and performance.
*   **Build System:** CMake + vcpkg. Standard for cross-platform C++ development.

## 2. Compiler Backend
*   **LLVM 18:** Chosen for latest optimization passes and opaque pointer support.
*   **Opaque Pointers:** We strictly use `builder->getPtrTy()` and explicit types for GEP/Load/Store to match LLVM 15+ requirements.

## 3. Memory Management
*   **Garbage Collector:** Boehm GC (`bdwgc`).
    *   *Reason:* Simplest way to get a working GC for a prototype.
    *   *Future:* Plan to replace with a custom Generational GC in Phase 21.
*   **Allocations:** All runtime objects use `ts_alloc` (wraps `GC_malloc`).
*   **Escape Analysis:** Stack allocation (`alloca`) used for non-escaping objects (Epic 70).

## 4. Runtime Types
*   **Strings:** `TsString` wraps `icu::UnicodeString`.
    *   *Reason:* Full Unicode correctness is required for JS compatibility.
    *   *Optimization:* Small String Optimization (SSO) for strings < 16 bytes.
*   **Numbers:** `double` (standard JS number).
    *   *Optimization:* Unboxed integers used in loops where possible (Epic 71).
*   **Objects:** `TsObject` uses a `TsMap` (hash map) for properties.
    *   *Future:* Hidden Classes / Shapes for faster property access.

## 5. Asynchronous I/O
*   **Library:** `libuv`.
    *   *Reason:* It is the event loop of Node.js, ensuring behavior parity.
*   **HTTP:** `llhttp` (Node.js parser) + `libuv` TCP handles.
*   **HTTPS:** OpenSSL for TLS + BIO memory buffers for non-blocking I/O.

## 6. Type System (Dec 2025)
*   **Contextual Typing:** Arrow function parameters infer types from call context.
    *   *Mechanism:* `contextualTypeStack` in Analyzer, pushed before visiting callbacks.
    *   *Coverage:* http, https, net, fs, and EventEmitter patterns.
*   **Boxing Convention:** 
    *   Compiler tracks boxed values in `boxedValues` set.
    *   Runtime uses `ts_value_get_*` helpers for consistent unboxing.
*   **Virtual Inheritance Casting:**
    *   Stream classes use `AsXxx()` virtual methods for safe downcasting.
    *   Never use C-style casts or static_cast with virtual inheritance.
