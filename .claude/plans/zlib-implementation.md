# Plan: Implement Zlib Module (0% → 100%)

## Overview

Implement the Node.js `zlib` module for compression and decompression support. This enables gzip, deflate, and brotli compression/decompression using industry-standard libraries.

## Current State

- **Zlib Coverage**: 0% (0/X features)
- **Required Libraries**: zlib (deflate/gzip), brotli (Brotli compression)
- **Existing Infrastructure**: TsBuffer, TsDuplex, stream.Transform patterns

## Dependencies to Add

```json
{
  "dependencies": [
    "zlib",
    "brotli"
  ]
}
```

Note: Zstd is experimental in Node.js - defer for later if needed.

## Implementation Phases

### Phase 1: Core Infrastructure (~800 LOC)
- Add zlib and brotli dependencies via vcpkg
- Create TsZlib.h/TsZlib.cpp with base classes
- Implement ZlibBase class extending TsDuplex/Transform
- Register types in Analyzer

### Phase 2: Sync Convenience Methods (~600 LOC)
- `gzipSync()` / `gunzipSync()`
- `deflateSync()` / `inflateSync()`
- `deflateRawSync()` / `inflateRawSync()`
- `unzipSync()` (auto-detect)
- `brotliCompressSync()` / `brotliDecompressSync()`

### Phase 3: Async Convenience Methods (~400 LOC)
- `gzip()` / `gunzip()` with callbacks
- `deflate()` / `inflate()` with callbacks
- `deflateRaw()` / `inflateRaw()` with callbacks
- `unzip()` with callback
- `brotliCompress()` / `brotliDecompress()` with callbacks

### Phase 4: Stream Classes (~800 LOC)
- `createGzip()` / `createGunzip()`
- `createDeflate()` / `createInflate()`
- `createDeflateRaw()` / `createInflateRaw()`
- `createUnzip()`
- `createBrotliCompress()` / `createBrotliDecompress()`

### Phase 5: Constants and Utilities (~200 LOC)
- `zlib.constants` object with all Z_* and BROTLI_* constants
- `zlib.crc32()` utility function
- Stream methods: `flush()`, `close()`, `params()`, `reset()`

**Total Estimated LOC**: ~2,800

## Runtime Class Hierarchy

```
TsDuplex (existing base)
└── TsZlibBase
    ├── TsGzip / TsGunzip
    ├── TsDeflate / TsInflate
    ├── TsDeflateRaw / TsInflateRaw
    ├── TsUnzip
    ├── TsBrotliCompress / TsBrotliDecompress
    └── (future: TsZstdCompress / TsZstdDecompress)
```

## Files to Create

| File | Purpose | Phase |
|------|---------|-------|
| `src/runtime/include/TsZlib.h` | Class declarations, C API | 1 |
| `src/runtime/src/TsZlib.cpp` | zlib/brotli integration | 1-5 |
| `src/compiler/analysis/Analyzer_StdLib_Zlib.cpp` | Type registration | 1 |
| `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin_Zlib.cpp` | LLVM codegen | 2-4 |
| `tests/node/zlib/zlib_sync.ts` | Sync method tests | 2 |
| `tests/node/zlib/zlib_async.ts` | Async method tests | 3 |
| `tests/node/zlib/zlib_streams.ts` | Stream tests | 4 |

## Files to Modify

| File | Changes |
|------|---------|
| `vcpkg.json` | Add `"zlib"`, `"brotli"` dependencies |
| `src/runtime/CMakeLists.txt` | Add TsZlib.cpp, link zlib and brotli |
| `src/compiler/CMakeLists.txt` | Add analyzer/codegen files |
| `src/compiler/analysis/Analyzer_StdLib.cpp` | Call `registerZlib()` |
| `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` | Add Zlib dispatcher |
| `docs/conformance/nodejs-features.md` | Update coverage |

## Key C API Functions

```cpp
extern "C" {
    // Sync convenience methods
    void* ts_zlib_gzip_sync(void* buffer, void* options);
    void* ts_zlib_gunzip_sync(void* buffer, void* options);
    void* ts_zlib_deflate_sync(void* buffer, void* options);
    void* ts_zlib_inflate_sync(void* buffer, void* options);
    void* ts_zlib_deflate_raw_sync(void* buffer, void* options);
    void* ts_zlib_inflate_raw_sync(void* buffer, void* options);
    void* ts_zlib_unzip_sync(void* buffer, void* options);
    void* ts_zlib_brotli_compress_sync(void* buffer, void* options);
    void* ts_zlib_brotli_decompress_sync(void* buffer, void* options);

    // Async convenience methods (with callbacks)
    void ts_zlib_gzip(void* buffer, void* options, void* callback);
    void ts_zlib_gunzip(void* buffer, void* options, void* callback);
    void ts_zlib_deflate(void* buffer, void* options, void* callback);
    void ts_zlib_inflate(void* buffer, void* options, void* callback);
    void ts_zlib_deflate_raw(void* buffer, void* options, void* callback);
    void ts_zlib_inflate_raw(void* buffer, void* options, void* callback);
    void ts_zlib_unzip(void* buffer, void* options, void* callback);
    void ts_zlib_brotli_compress(void* buffer, void* options, void* callback);
    void ts_zlib_brotli_decompress(void* buffer, void* options, void* callback);

    // Stream creators
    void* ts_zlib_create_gzip(void* options);
    void* ts_zlib_create_gunzip(void* options);
    void* ts_zlib_create_deflate(void* options);
    void* ts_zlib_create_inflate(void* options);
    void* ts_zlib_create_deflate_raw(void* options);
    void* ts_zlib_create_inflate_raw(void* options);
    void* ts_zlib_create_unzip(void* options);
    void* ts_zlib_create_brotli_compress(void* options);
    void* ts_zlib_create_brotli_decompress(void* options);

    // Stream methods
    void ts_zlib_flush(void* stream, int64_t kind, void* callback);
    void ts_zlib_close(void* stream, void* callback);
    void ts_zlib_params(void* stream, int64_t level, int64_t strategy, void* callback);
    void ts_zlib_reset(void* stream);

    // Utilities
    int64_t ts_zlib_crc32(void* data, int64_t value);

    // Constants
    void* ts_zlib_get_constants();
}
```

## Zlib Integration Pattern

```cpp
// Sync compression with zlib
void* ts_zlib_gzip_sync(void* buffer, void* options) {
    TsBuffer* input = (TsBuffer*)ts_value_get_object((TsValue*)buffer);

    // Set up zlib stream
    z_stream strm = {};
    deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                 16 + MAX_WBITS,  // 16 = gzip header
                 8, Z_DEFAULT_STRATEGY);

    // Allocate output buffer (pessimistic: input size + overhead)
    size_t maxOutput = deflateBound(&strm, input->length);
    TsBuffer* output = TsBuffer::Create(maxOutput);

    strm.next_in = input->data;
    strm.avail_in = input->length;
    strm.next_out = output->data;
    strm.avail_out = maxOutput;

    int ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&strm);
        return nullptr;  // Or throw error
    }

    output->length = strm.total_out;
    deflateEnd(&strm);

    return ts_value_make_object(output);
}
```

## Brotli Integration Pattern

```cpp
// Sync compression with Brotli
void* ts_zlib_brotli_compress_sync(void* buffer, void* options) {
    TsBuffer* input = (TsBuffer*)ts_value_get_object((TsValue*)buffer);

    // Calculate max output size
    size_t maxOutput = BrotliEncoderMaxCompressedSize(input->length);
    TsBuffer* output = TsBuffer::Create(maxOutput);

    size_t encodedSize = maxOutput;
    BROTLI_BOOL result = BrotliEncoderCompress(
        BROTLI_DEFAULT_QUALITY,
        BROTLI_DEFAULT_WINDOW,
        BROTLI_MODE_GENERIC,
        input->length,
        input->data,
        &encodedSize,
        output->data
    );

    if (result != BROTLI_TRUE) {
        return nullptr;  // Or throw error
    }

    output->length = encodedSize;
    return ts_value_make_object(output);
}
```

## Stream Transform Pattern

```cpp
class TsGzip : public TsDuplex {
public:
    static TsGzip* Create(TsValue* options);

    // Transform implementation
    void _transform(TsBuffer* chunk, const char* encoding, void* callback);
    void _flush(void* callback);

private:
    z_stream strm;
    int level;
    int windowBits;
    int memLevel;
    int strategy;

    TsGzip() {
        memset(&strm, 0, sizeof(strm));
    }

    void Init(TsValue* options) {
        // Parse options for level, windowBits, etc.
        deflateInit2(&strm, level, Z_DEFLATED,
                     16 + windowBits, memLevel, strategy);
    }

    ~TsGzip() {
        deflateEnd(&strm);
    }
};
```

## Key Node.js API Surface

### Sync Convenience Methods (Priority 1)
- `zlib.gzipSync(buffer[, options])` - Most common use case
- `zlib.gunzipSync(buffer[, options])`
- `zlib.deflateSync(buffer[, options])`
- `zlib.inflateSync(buffer[, options])`
- `zlib.brotliCompressSync(buffer[, options])`
- `zlib.brotliDecompressSync(buffer[, options])`

### Constants (Priority 1)
- `Z_NO_COMPRESSION` (0)
- `Z_BEST_SPEED` (1)
- `Z_BEST_COMPRESSION` (9)
- `Z_DEFAULT_COMPRESSION` (-1)
- `Z_NO_FLUSH` (0)
- `Z_FINISH` (4)
- All BROTLI_* constants

### Stream Creators (Priority 2)
- `zlib.createGzip([options])`
- `zlib.createGunzip([options])`
- `zlib.createDeflate([options])`
- `zlib.createInflate([options])`

## Risk Assessment

| Risk | Level | Mitigation |
|------|-------|------------|
| Memory management with zlib | Medium | Use ts_alloc for output buffers, cleanup with deflateEnd/inflateEnd |
| Streaming large files | Medium | Use chunked processing in _transform |
| Error handling | Medium | Map zlib error codes to JavaScript errors |
| Option parsing | Low | Parse level, windowBits, memLevel from TsValue options |
| Brotli API differences | Low | Brotli API is simpler than zlib |

## Feature Count Estimate

| Category | Count |
|----------|-------|
| Sync methods | 12 (gzip, gunzip, deflate, inflate, deflateRaw, inflateRaw, unzip, brotliCompress, brotliDecompress × 1 each + unzipSync) |
| Async methods | 10 |
| Stream creators | 10 |
| Stream classes | 10 |
| Class methods | 4 (flush, close, params, reset) |
| Properties | 1 (bytesWritten) |
| Utilities | 1 (crc32) |
| Constants object | 1 |
| **Total** | ~49 features |

## Verification

```powershell
# 1. Install dependencies
vcpkg install zlib:x64-windows-static brotli:x64-windows-static

# 2. Build
cmake --build build --config Release

# 3. Test sync methods
build/src/compiler/Release/ts-aot.exe tests/node/zlib/zlib_sync.ts -o tests/test_zlib_sync.exe
./tests/test_zlib_sync.exe

# 4. Run full test suite
python tests/node/run_tests.py
```

## Test Plan

### Sync Tests (`zlib_sync.ts`)
```typescript
import * as zlib from 'zlib';

function user_main(): number {
    // Test gzip roundtrip
    const input = Buffer.from('Hello, World!');
    const compressed = zlib.gzipSync(input);
    const decompressed = zlib.gunzipSync(compressed);
    console.log('Roundtrip:', decompressed.toString() === 'Hello, World!' ? 'PASS' : 'FAIL');

    // Test deflate roundtrip
    const deflated = zlib.deflateSync(input);
    const inflated = zlib.inflateSync(deflated);
    console.log('Deflate:', inflated.toString() === 'Hello, World!' ? 'PASS' : 'FAIL');

    // Test Brotli
    const brotliCompressed = zlib.brotliCompressSync(input);
    const brotliDecompressed = zlib.brotliDecompressSync(brotliCompressed);
    console.log('Brotli:', brotliDecompressed.toString() === 'Hello, World!' ? 'PASS' : 'FAIL');

    return 0;
}
```

## Implementation Order

1. **Phase 1**: Infrastructure + Sync gzip/gunzip (most common)
2. **Phase 2**: Remaining sync methods
3. **Phase 3**: Async methods with libuv thread pool
4. **Phase 4**: Stream classes
5. **Phase 5**: Constants and utilities

This order prioritizes the most commonly used functionality first (sync gzip/gunzip).
