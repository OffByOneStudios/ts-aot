# Epic 16: Cryptography

**Status:** Completed
**Parent:** [Phase 4 Meta Epic](./meta_epic.md)

## Overview
Add support for cryptographic functions. For AoC 2015 Day 4, we specifically need MD5. We will use `libsodium` (via vcpkg) as our primary crypto library, though we may need to supplement it if it lacks legacy algorithms like MD5.

## Milestones

### Milestone 16.1: Library Integration
- **Build:** Add `libsodium` to `vcpkg.json`.
- **Build:** Link `libsodium` in `CMakeLists.txt`.

### Milestone 16.2: MD5 Implementation
- **Runtime:** Implement `ts_crypto_md5(TsString*)`.
- **Implementation:** Use `libsodium` (or a standalone fallback if MD5 is missing).
- **API:** Expose `crypto.md5` to TypeScript.

### Milestone 16.3: Compiler Exposure
- **Codegen:** Recognize `crypto.md5(...)` and emit the appropriate call.

## Action Items
- [x] Add `libsodium` to `vcpkg.json`.
- [x] Configure CMake to link `libsodium`.
- [x] Implement `ts_crypto_md5` in Runtime.
- [x] Expose `crypto` global object in Compiler.
- [x] Test with `tests/integration/crypto.ts`.
