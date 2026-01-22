#pragma once

/*
 * Zlib Runtime Implementation
 *
 * Before editing this file, read: .github/instructions/runtime-extensions.instructions.md
 *
 * CRITICAL RULES:
 * 1. Memory: Use ts_alloc() for GC objects, NOT new/malloc
 * 2. Strings: Use TsString::Create(), NOT std::string
 * 3. Casting: Use AsXxx() or dynamic_cast, NOT C-style casts
 * 4. Boxing: Use ts_value_get_object() to unbox void* params
 * 5. Errors: ts_error_create() returns ALREADY-BOXED TsValue*
 *
 * Zlib uses:
 * - zlib library for gzip/deflate compression
 * - brotli library for Brotli compression
 */

#include "TsString.h"
#include "TsObject.h"
#include "TsBuffer.h"
#include "TsMap.h"
#include "TsDuplex.h"
#include <zlib.h>
#include <brotli/encode.h>
#include <brotli/decode.h>

// Forward declarations
class TsZlibBase;
class TsGzip;
class TsGunzip;
class TsDeflate;
class TsInflate;
class TsDeflateRaw;
class TsInflateRaw;
class TsUnzip;
class TsBrotliCompress;
class TsBrotliDecompress;

// =============================================================================
// Zlib Options Structure
// =============================================================================

struct TsZlibOptions {
    int flush = Z_NO_FLUSH;
    int finishFlush = Z_FINISH;
    int chunkSize = 16 * 1024;
    int windowBits = 15;          // 15 for deflate, 15+16 for gzip, -15 for raw
    int level = Z_DEFAULT_COMPRESSION;
    int memLevel = 8;
    int strategy = Z_DEFAULT_STRATEGY;
    TsBuffer* dictionary = nullptr;
    size_t maxOutputLength = 0;   // 0 = unlimited

    static TsZlibOptions FromValue(TsValue* options);
};

// =============================================================================
// Brotli Options Structure
// =============================================================================

struct TsBrotliOptions {
    BrotliEncoderOperation flush = BROTLI_OPERATION_PROCESS;
    BrotliEncoderOperation finishFlush = BROTLI_OPERATION_FINISH;
    int chunkSize = 16 * 1024;
    int quality = BROTLI_DEFAULT_QUALITY;
    int lgwin = BROTLI_DEFAULT_WINDOW;
    BrotliEncoderMode mode = BROTLI_MODE_GENERIC;
    size_t maxOutputLength = 0;

    static TsBrotliOptions FromValue(TsValue* options);
};

// =============================================================================
// TsZlibBase - Base class for all zlib streams
// =============================================================================

class TsZlibBase : public TsDuplex {
public:
    static constexpr uint32_t MAGIC = 0x5A4C4942; // "ZLIB"

    // Properties
    size_t bytesWritten = 0;

    // Methods
    virtual void Flush(int kind, void* callback);
    virtual void Close(void* callback);
    virtual void Reset();

    // AsXxx() helpers
    virtual TsZlibBase* AsZlibBase() { return this; }

protected:
    TsZlibBase();
    virtual ~TsZlibBase();

    int chunkSize = 16 * 1024;
    void* closeCallback = nullptr;
};

// =============================================================================
// Gzip/Gunzip - gzip format compression/decompression
// =============================================================================

class TsGzip : public TsZlibBase {
public:
    static TsGzip* Create(TsValue* options);

    // TsDuplex overrides
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void Flush(int kind, void* callback) override;
    virtual void Reset() override;

protected:
    TsGzip();
    ~TsGzip();
    void Init(TsValue* options);

    z_stream strm;
    bool initialized = false;
    int level;
    int windowBits;
    int memLevel;
    int strategy;
};

class TsGunzip : public TsZlibBase {
public:
    static TsGunzip* Create(TsValue* options);

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void Flush(int kind, void* callback) override;
    virtual void Reset() override;

protected:
    TsGunzip();
    ~TsGunzip();
    void Init(TsValue* options);

    z_stream strm;
    bool initialized = false;
    int windowBits;
};

// =============================================================================
// Deflate/Inflate - deflate format compression/decompression
// =============================================================================

class TsDeflate : public TsZlibBase {
public:
    static TsDeflate* Create(TsValue* options);

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void Flush(int kind, void* callback) override;
    virtual void Params(int level, int strategy, void* callback);
    virtual void Reset() override;

protected:
    TsDeflate();
    ~TsDeflate();
    void Init(TsValue* options);

    z_stream strm;
    bool initialized = false;
    int level;
    int windowBits;
    int memLevel;
    int strategy;
};

class TsInflate : public TsZlibBase {
public:
    static TsInflate* Create(TsValue* options);

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void Flush(int kind, void* callback) override;
    virtual void Reset() override;

protected:
    TsInflate();
    ~TsInflate();
    void Init(TsValue* options);

    z_stream strm;
    bool initialized = false;
    int windowBits;
};

// =============================================================================
// DeflateRaw/InflateRaw - raw deflate (no header/trailer)
// =============================================================================

class TsDeflateRaw : public TsZlibBase {
public:
    static TsDeflateRaw* Create(TsValue* options);

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void Flush(int kind, void* callback) override;
    virtual void Reset() override;

protected:
    TsDeflateRaw();
    ~TsDeflateRaw();
    void Init(TsValue* options);

    z_stream strm;
    bool initialized = false;
    int level;
    int windowBits;
    int memLevel;
    int strategy;
};

class TsInflateRaw : public TsZlibBase {
public:
    static TsInflateRaw* Create(TsValue* options);

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void Flush(int kind, void* callback) override;
    virtual void Reset() override;

protected:
    TsInflateRaw();
    ~TsInflateRaw();
    void Init(TsValue* options);

    z_stream strm;
    bool initialized = false;
    int windowBits;
};

// =============================================================================
// Unzip - auto-detect gzip or deflate
// =============================================================================

class TsUnzip : public TsZlibBase {
public:
    static TsUnzip* Create(TsValue* options);

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void Flush(int kind, void* callback) override;
    virtual void Reset() override;

protected:
    TsUnzip();
    ~TsUnzip();
    void Init(TsValue* options);

    z_stream strm;
    bool initialized = false;
    int windowBits;
};

// =============================================================================
// Brotli Compression/Decompression
// =============================================================================

class TsBrotliCompress : public TsZlibBase {
public:
    static TsBrotliCompress* Create(TsValue* options);

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void Flush(int kind, void* callback) override;

protected:
    TsBrotliCompress();
    ~TsBrotliCompress();
    void Init(TsValue* options);

    BrotliEncoderState* state = nullptr;
    int quality;
    int lgwin;
    BrotliEncoderMode mode;
};

class TsBrotliDecompress : public TsZlibBase {
public:
    static TsBrotliDecompress* Create(TsValue* options);

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void Flush(int kind, void* callback) override;

protected:
    TsBrotliDecompress();
    ~TsBrotliDecompress();
    void Init(TsValue* options);

    BrotliDecoderState* state = nullptr;
};

// =============================================================================
// C API - Module Functions
// =============================================================================

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

    // Stream property getters
    int64_t ts_zlib_get_bytes_written(void* stream);

    // Utilities
    int64_t ts_zlib_crc32(void* data, int64_t value);

    // Constants
    void* ts_zlib_get_constants();
}

// =============================================================================
// Zlib Constants
// =============================================================================

namespace ZlibConstants {
    // Flush modes
    constexpr int Z_NO_FLUSH_VAL = Z_NO_FLUSH;
    constexpr int Z_PARTIAL_FLUSH_VAL = Z_PARTIAL_FLUSH;
    constexpr int Z_SYNC_FLUSH_VAL = Z_SYNC_FLUSH;
    constexpr int Z_FULL_FLUSH_VAL = Z_FULL_FLUSH;
    constexpr int Z_FINISH_VAL = Z_FINISH;
    constexpr int Z_BLOCK_VAL = Z_BLOCK;

    // Compression levels
    constexpr int Z_NO_COMPRESSION_VAL = Z_NO_COMPRESSION;
    constexpr int Z_BEST_SPEED_VAL = Z_BEST_SPEED;
    constexpr int Z_BEST_COMPRESSION_VAL = Z_BEST_COMPRESSION;
    constexpr int Z_DEFAULT_COMPRESSION_VAL = Z_DEFAULT_COMPRESSION;

    // Compression strategies
    constexpr int Z_FILTERED_VAL = Z_FILTERED;
    constexpr int Z_HUFFMAN_ONLY_VAL = Z_HUFFMAN_ONLY;
    constexpr int Z_RLE_VAL = Z_RLE;
    constexpr int Z_FIXED_VAL = Z_FIXED;
    constexpr int Z_DEFAULT_STRATEGY_VAL = Z_DEFAULT_STRATEGY;

    // Return codes
    constexpr int Z_OK_VAL = Z_OK;
    constexpr int Z_STREAM_END_VAL = Z_STREAM_END;
    constexpr int Z_NEED_DICT_VAL = Z_NEED_DICT;
    constexpr int Z_ERRNO_VAL = Z_ERRNO;
    constexpr int Z_STREAM_ERROR_VAL = Z_STREAM_ERROR;
    constexpr int Z_DATA_ERROR_VAL = Z_DATA_ERROR;
    constexpr int Z_MEM_ERROR_VAL = Z_MEM_ERROR;
    constexpr int Z_BUF_ERROR_VAL = Z_BUF_ERROR;
    constexpr int Z_VERSION_ERROR_VAL = Z_VERSION_ERROR;

    // Brotli flush operations
    constexpr int BROTLI_OPERATION_PROCESS_VAL = BROTLI_OPERATION_PROCESS;
    constexpr int BROTLI_OPERATION_FLUSH_VAL = BROTLI_OPERATION_FLUSH;
    constexpr int BROTLI_OPERATION_FINISH_VAL = BROTLI_OPERATION_FINISH;
    constexpr int BROTLI_OPERATION_EMIT_METADATA_VAL = BROTLI_OPERATION_EMIT_METADATA;

    // Brotli encoder modes
    constexpr int BROTLI_MODE_GENERIC_VAL = BROTLI_MODE_GENERIC;
    constexpr int BROTLI_MODE_TEXT_VAL = BROTLI_MODE_TEXT;
    constexpr int BROTLI_MODE_FONT_VAL = BROTLI_MODE_FONT;

    // Brotli quality range
    constexpr int BROTLI_MIN_QUALITY_VAL = BROTLI_MIN_QUALITY;
    constexpr int BROTLI_MAX_QUALITY_VAL = BROTLI_MAX_QUALITY;
    constexpr int BROTLI_DEFAULT_QUALITY_VAL = BROTLI_DEFAULT_QUALITY;

    // Brotli window bits range
    constexpr int BROTLI_MIN_WINDOW_BITS_VAL = BROTLI_MIN_WINDOW_BITS;
    constexpr int BROTLI_MAX_WINDOW_BITS_VAL = BROTLI_MAX_WINDOW_BITS;
    constexpr int BROTLI_DEFAULT_WINDOW_VAL = BROTLI_DEFAULT_WINDOW;

    // Brotli encoder parameters
    constexpr int BROTLI_PARAM_MODE_VAL = BROTLI_PARAM_MODE;
    constexpr int BROTLI_PARAM_QUALITY_VAL = BROTLI_PARAM_QUALITY;
    constexpr int BROTLI_PARAM_LGWIN_VAL = BROTLI_PARAM_LGWIN;
    constexpr int BROTLI_PARAM_LGBLOCK_VAL = BROTLI_PARAM_LGBLOCK;
    constexpr int BROTLI_PARAM_SIZE_HINT_VAL = BROTLI_PARAM_SIZE_HINT;

    // Brotli decoder parameters
    constexpr int BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION_VAL = BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION;
    constexpr int BROTLI_DECODER_PARAM_LARGE_WINDOW_VAL = BROTLI_DECODER_PARAM_LARGE_WINDOW;
}
