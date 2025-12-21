# Issue: Runtime Binary Size and DLL Weight

## Status
**Priority:** Medium  
**Type:** Architecture / Optimization  
**Current State:** Investigated, deferred.

## Problem Statement
The current `ts-aot` runtime dependencies result in a very large footprint for generated executables. A "Hello World" program currently requires several large DLLs to be present, or would result in a 40MB+ executable if statically linked.

### Current Dependency Weight (Release x64)
| Dependency | DLL Name | Size | Purpose |
| :--- | :--- | :--- | :--- |
| **ICU (Data)** | `icudt74.dll` | ~30.7 MB | Unicode tables, locales, normalization. |
| **ICU (UC)** | `icuuc74.dll` | ~1.7 MB | UTF-16 string manipulation. |
| **OpenSSL** | `libcrypto-3-x64.dll` | ~5.0 MB | TLS for HTTPS and WebCrypto. |
| **OpenSSL** | `libssl-3-x64.dll` | ~1.0 MB | SSL protocol implementation. |
| **libuv** | `libuv.dll` | ~0.3 MB | Event loop and I/O. |

**Total "Weight": ~39 MB**

## Proposed Alternatives

### 1. Replace ICU
ICU is the largest contributor due to its massive data blob.
*   **Alternative A: Native OS APIs.** Use Win32 `MultiByteToWideChar` / `CompareStringEx` on Windows and `iconv` / `CoreFoundation` on macOS/Linux.
*   **Alternative B: Custom UTF-16 Implementation.** Implement `TsString` using `std::u16string` or raw `uint16_t` buffers. Use a lightweight Regex engine like `PCRE2` or `RE2`.
*   **Alternative C: Minimal ICU.** Build a custom ICU data package containing only the bare essentials (e.g., only `en_US` and basic normalization).

### 2. Replace OpenSSL
OpenSSL is feature-complete but excessively large for a JS runtime that only needs HTTPS and basic crypto.
*   **Alternative A: mbedtls.** A modular, small-footprint TLS library (~200-500KB).
*   **Alternative B: Native TLS.** Use `SChannel` on Windows and `Security.framework` on macOS.

### 3. Modular Runtime Linking
Currently, `tsruntime` is a single static library that pulls in all dependencies.
*   **Goal:** Split the runtime into `core`, `net`, and `crypto`.
*   **Implementation:** The compiler should only emit linker instructions for `net` or `crypto` if the source code actually references `fetch`, `Request`, `Response`, or `crypto`.

## Action Items
- [ ] Prototype a "Data-less" `TsString` implementation using raw UTF-16.
- [ ] Evaluate `mbedtls` as a drop-in replacement for OpenSSL in `TsFetch.cpp`.
- [ ] Refactor `CMakeLists.txt` to support optional runtime components.
