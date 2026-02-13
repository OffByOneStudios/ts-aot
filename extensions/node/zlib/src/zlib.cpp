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
 */

#include "TsZlib.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "TsError.h"
#include "GC.h"
#include <cstring>
#include <algorithm>
#include <cstdio>

// =============================================================================
// TsZlibOptions - Parse options from TsValue
// =============================================================================

TsZlibOptions TsZlibOptions::FromValue(TsValue* options) {
    TsZlibOptions opts;
    if (!options) return opts;

    void* rawOpts = ts_value_get_object(options);
    if (!rawOpts) rawOpts = options;

    TsMap* map = dynamic_cast<TsMap*>((TsObject*)rawOpts);
    if (!map) return opts;

    // Parse flush
    TsValue flushVal = map->Get(TsValue(TsString::Create("flush")));
    if (flushVal.type == ValueType::NUMBER_INT) {
        opts.flush = (int)flushVal.i_val;
    }

    // Parse finishFlush
    TsValue finishFlushVal = map->Get(TsValue(TsString::Create("finishFlush")));
    if (finishFlushVal.type == ValueType::NUMBER_INT) {
        opts.finishFlush = (int)finishFlushVal.i_val;
    }

    // Parse chunkSize
    TsValue chunkSizeVal = map->Get(TsValue(TsString::Create("chunkSize")));
    if (chunkSizeVal.type == ValueType::NUMBER_INT) {
        opts.chunkSize = (int)chunkSizeVal.i_val;
    }

    // Parse windowBits
    TsValue windowBitsVal = map->Get(TsValue(TsString::Create("windowBits")));
    if (windowBitsVal.type == ValueType::NUMBER_INT) {
        opts.windowBits = (int)windowBitsVal.i_val;
    }

    // Parse level
    TsValue levelVal = map->Get(TsValue(TsString::Create("level")));
    if (levelVal.type == ValueType::NUMBER_INT) {
        opts.level = (int)levelVal.i_val;
    }

    // Parse memLevel
    TsValue memLevelVal = map->Get(TsValue(TsString::Create("memLevel")));
    if (memLevelVal.type == ValueType::NUMBER_INT) {
        opts.memLevel = (int)memLevelVal.i_val;
    }

    // Parse strategy
    TsValue strategyVal = map->Get(TsValue(TsString::Create("strategy")));
    if (strategyVal.type == ValueType::NUMBER_INT) {
        opts.strategy = (int)strategyVal.i_val;
    }

    // Parse maxOutputLength
    TsValue maxOutputLengthVal = map->Get(TsValue(TsString::Create("maxOutputLength")));
    if (maxOutputLengthVal.type == ValueType::NUMBER_INT) {
        opts.maxOutputLength = (size_t)maxOutputLengthVal.i_val;
    }

    return opts;
}

// =============================================================================
// TsBrotliOptions - Parse options from TsValue
// =============================================================================

TsBrotliOptions TsBrotliOptions::FromValue(TsValue* options) {
    TsBrotliOptions opts;
    if (!options) return opts;

    void* rawOpts = ts_value_get_object(options);
    if (!rawOpts) rawOpts = options;

    TsMap* map = dynamic_cast<TsMap*>((TsObject*)rawOpts);
    if (!map) return opts;

    // Parse chunkSize
    TsValue chunkSizeVal = map->Get(TsValue(TsString::Create("chunkSize")));
    if (chunkSizeVal.type == ValueType::NUMBER_INT) {
        opts.chunkSize = (int)chunkSizeVal.i_val;
    }

    // Parse maxOutputLength
    TsValue maxOutputLengthVal = map->Get(TsValue(TsString::Create("maxOutputLength")));
    if (maxOutputLengthVal.type == ValueType::NUMBER_INT) {
        opts.maxOutputLength = (size_t)maxOutputLengthVal.i_val;
    }

    // Parse params object for Brotli-specific options
    TsValue paramsVal = map->Get(TsValue(TsString::Create("params")));
    if (paramsVal.type == ValueType::OBJECT_PTR && paramsVal.ptr_val) {
        TsMap* params = dynamic_cast<TsMap*>((TsObject*)paramsVal.ptr_val);
        if (params) {
            // Parse quality (BROTLI_PARAM_QUALITY)
            char qualityKey[32];
            snprintf(qualityKey, sizeof(qualityKey), "%d", BROTLI_PARAM_QUALITY);
            TsValue qualityVal = params->Get(TsValue(TsString::Create(qualityKey)));
            if (qualityVal.type == ValueType::NUMBER_INT) {
                opts.quality = (int)qualityVal.i_val;
            }

            // Parse lgwin (BROTLI_PARAM_LGWIN)
            char lgwinKey[32];
            snprintf(lgwinKey, sizeof(lgwinKey), "%d", BROTLI_PARAM_LGWIN);
            TsValue lgwinVal = params->Get(TsValue(TsString::Create(lgwinKey)));
            if (lgwinVal.type == ValueType::NUMBER_INT) {
                opts.lgwin = (int)lgwinVal.i_val;
            }

            // Parse mode (BROTLI_PARAM_MODE)
            char modeKey[32];
            snprintf(modeKey, sizeof(modeKey), "%d", BROTLI_PARAM_MODE);
            TsValue modeVal = params->Get(TsValue(TsString::Create(modeKey)));
            if (modeVal.type == ValueType::NUMBER_INT) {
                opts.mode = (BrotliEncoderMode)modeVal.i_val;
            }
        }
    }

    return opts;
}

// =============================================================================
// TsZlibBase - Base class implementation
// =============================================================================

TsZlibBase::TsZlibBase() {
    magic = MAGIC;
}

TsZlibBase::~TsZlibBase() {
}

void TsZlibBase::Flush(int kind, void* callback) {
    // Base implementation - subclasses override
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, nullptr);
    }
}

void TsZlibBase::Close(void* callback) {
    closeCallback = callback;
    // Subclasses should clean up their resources
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, nullptr);
    }
}

void TsZlibBase::Reset() {
    // Base implementation - subclasses override
}

// =============================================================================
// Sync Convenience Methods - Gzip/Gunzip
// =============================================================================

extern "C" void* ts_zlib_gzip_sync(void* buffer, void* options) {
    // Unbox buffer
    void* rawBuf = ts_nanbox_safe_unbox(buffer);
    TsBuffer* input = dynamic_cast<TsBuffer*>((TsObject*)rawBuf);
    if (!input) {
        return ts_error_create((void*)TsString::Create("First argument must be a Buffer"));
    }

    // Parse options
    TsZlibOptions opts = TsZlibOptions::FromValue((TsValue*)options);

    // Initialize zlib stream for gzip (windowBits + 16 = gzip header)
    z_stream strm = {};
    int ret = deflateInit2(&strm, opts.level, Z_DEFLATED,
                           15 + 16,  // 15 + 16 = gzip format
                           opts.memLevel, opts.strategy);
    if (ret != Z_OK) {
        return ts_error_create((void*)TsString::Create("Failed to initialize gzip compression"));
    }

    // Calculate max output size
    size_t maxOutput = deflateBound(&strm, (uLong)input->GetLength());
    if (opts.maxOutputLength > 0 && maxOutput > opts.maxOutputLength) {
        deflateEnd(&strm);
        return ts_error_create((void*)TsString::Create("Output would exceed maxOutputLength"));
    }

    // Allocate output buffer
    TsBuffer* output = TsBuffer::Create(maxOutput);

    strm.next_in = input->GetData();
    strm.avail_in = (uInt)input->GetLength();
    strm.next_out = output->GetData();
    strm.avail_out = (uInt)maxOutput;

    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&strm);
        return ts_error_create((void*)TsString::Create("Gzip compression failed"));
    }

    // Create final buffer with correct size
    TsBuffer* result = output->Subarray(0, strm.total_out);
    deflateEnd(&strm);

    return ts_value_make_object(result);
}

extern "C" void* ts_zlib_gunzip_sync(void* buffer, void* options) {
    // Unbox buffer
    void* rawBuf = ts_nanbox_safe_unbox(buffer);
    TsBuffer* input = dynamic_cast<TsBuffer*>((TsObject*)rawBuf);
    if (!input) {
        return ts_error_create((void*)TsString::Create("First argument must be a Buffer"));
    }

    // Parse options
    TsZlibOptions opts = TsZlibOptions::FromValue((TsValue*)options);

    // Initialize zlib stream for gunzip (windowBits + 16 = gzip auto-detect)
    z_stream strm = {};
    int ret = inflateInit2(&strm, 15 + 16);  // 15 + 16 = gzip format
    if (ret != Z_OK) {
        return ts_error_create((void*)TsString::Create("Failed to initialize gzip decompression"));
    }

    // Start with estimated output size (4x input as initial guess)
    size_t outputSize = input->GetLength() * 4;
    if (outputSize < 1024) outputSize = 1024;
    if (opts.maxOutputLength > 0 && outputSize > opts.maxOutputLength) {
        outputSize = opts.maxOutputLength;
    }

    TsBuffer* output = TsBuffer::Create(outputSize);
    size_t totalOut = 0;

    strm.next_in = input->GetData();
    strm.avail_in = (uInt)input->GetLength();

    // Decompress in chunks, growing buffer as needed
    while (true) {
        strm.next_out = output->GetData() + totalOut;
        strm.avail_out = (uInt)(outputSize - totalOut);

        ret = inflate(&strm, Z_NO_FLUSH);

        if (ret == Z_STREAM_END) {
            totalOut = strm.total_out;
            break;
        }

        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return ts_error_create((void*)TsString::Create("Gzip decompression failed"));
        }

        totalOut = strm.total_out;

        // Need more output space
        if (strm.avail_out == 0) {
            size_t newSize = outputSize * 2;
            if (opts.maxOutputLength > 0 && newSize > opts.maxOutputLength) {
                inflateEnd(&strm);
                return ts_error_create((void*)TsString::Create("Output would exceed maxOutputLength"));
            }

            TsBuffer* newOutput = TsBuffer::Create(newSize);
            memcpy(newOutput->GetData(), output->GetData(), totalOut);
            output = newOutput;
            outputSize = newSize;
        }
    }

    // Create final buffer with correct size
    TsBuffer* result = output->Subarray(0, totalOut);
    inflateEnd(&strm);

    return ts_value_make_object(result);
}

// =============================================================================
// Sync Convenience Methods - Deflate/Inflate
// =============================================================================

extern "C" void* ts_zlib_deflate_sync(void* buffer, void* options) {
    void* rawBuf = ts_nanbox_safe_unbox(buffer);
    TsBuffer* input = dynamic_cast<TsBuffer*>((TsObject*)rawBuf);
    if (!input) {
        return ts_error_create((void*)TsString::Create("First argument must be a Buffer"));
    }

    TsZlibOptions opts = TsZlibOptions::FromValue((TsValue*)options);

    z_stream strm = {};
    int ret = deflateInit2(&strm, opts.level, Z_DEFLATED,
                           opts.windowBits, opts.memLevel, opts.strategy);
    if (ret != Z_OK) {
        return ts_error_create((void*)TsString::Create("Failed to initialize deflate compression"));
    }

    size_t maxOutput = deflateBound(&strm, (uLong)input->GetLength());
    if (opts.maxOutputLength > 0 && maxOutput > opts.maxOutputLength) {
        deflateEnd(&strm);
        return ts_error_create((void*)TsString::Create("Output would exceed maxOutputLength"));
    }

    TsBuffer* output = TsBuffer::Create(maxOutput);

    strm.next_in = input->GetData();
    strm.avail_in = (uInt)input->GetLength();
    strm.next_out = output->GetData();
    strm.avail_out = (uInt)maxOutput;

    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&strm);
        return ts_error_create((void*)TsString::Create("Deflate compression failed"));
    }

    TsBuffer* result = output->Subarray(0, strm.total_out);
    deflateEnd(&strm);

    return ts_value_make_object(result);
}

extern "C" void* ts_zlib_inflate_sync(void* buffer, void* options) {
    void* rawBuf = ts_nanbox_safe_unbox(buffer);
    TsBuffer* input = dynamic_cast<TsBuffer*>((TsObject*)rawBuf);
    if (!input) {
        return ts_error_create((void*)TsString::Create("First argument must be a Buffer"));
    }

    TsZlibOptions opts = TsZlibOptions::FromValue((TsValue*)options);

    z_stream strm = {};
    int ret = inflateInit2(&strm, opts.windowBits);
    if (ret != Z_OK) {
        return ts_error_create((void*)TsString::Create("Failed to initialize inflate decompression"));
    }

    size_t outputSize = input->GetLength() * 4;
    if (outputSize < 1024) outputSize = 1024;
    if (opts.maxOutputLength > 0 && outputSize > opts.maxOutputLength) {
        outputSize = opts.maxOutputLength;
    }

    TsBuffer* output = TsBuffer::Create(outputSize);
    size_t totalOut = 0;

    strm.next_in = input->GetData();
    strm.avail_in = (uInt)input->GetLength();

    while (true) {
        strm.next_out = output->GetData() + totalOut;
        strm.avail_out = (uInt)(outputSize - totalOut);

        ret = inflate(&strm, Z_NO_FLUSH);

        if (ret == Z_STREAM_END) {
            totalOut = strm.total_out;
            break;
        }

        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return ts_error_create((void*)TsString::Create("Inflate decompression failed"));
        }

        totalOut = strm.total_out;

        if (strm.avail_out == 0) {
            size_t newSize = outputSize * 2;
            if (opts.maxOutputLength > 0 && newSize > opts.maxOutputLength) {
                inflateEnd(&strm);
                return ts_error_create((void*)TsString::Create("Output would exceed maxOutputLength"));
            }

            TsBuffer* newOutput = TsBuffer::Create(newSize);
            memcpy(newOutput->GetData(), output->GetData(), totalOut);
            output = newOutput;
            outputSize = newSize;
        }
    }

    TsBuffer* result = output->Subarray(0, totalOut);
    inflateEnd(&strm);

    return ts_value_make_object(result);
}

// =============================================================================
// Sync Convenience Methods - DeflateRaw/InflateRaw
// =============================================================================

extern "C" void* ts_zlib_deflate_raw_sync(void* buffer, void* options) {
    void* rawBuf = ts_nanbox_safe_unbox(buffer);
    TsBuffer* input = dynamic_cast<TsBuffer*>((TsObject*)rawBuf);
    if (!input) {
        return ts_error_create((void*)TsString::Create("First argument must be a Buffer"));
    }

    TsZlibOptions opts = TsZlibOptions::FromValue((TsValue*)options);

    z_stream strm = {};
    // Negative windowBits = raw deflate (no header/trailer)
    int ret = deflateInit2(&strm, opts.level, Z_DEFLATED,
                           -opts.windowBits, opts.memLevel, opts.strategy);
    if (ret != Z_OK) {
        return ts_error_create((void*)TsString::Create("Failed to initialize raw deflate compression"));
    }

    size_t maxOutput = deflateBound(&strm, (uLong)input->GetLength());
    if (opts.maxOutputLength > 0 && maxOutput > opts.maxOutputLength) {
        deflateEnd(&strm);
        return ts_error_create((void*)TsString::Create("Output would exceed maxOutputLength"));
    }

    TsBuffer* output = TsBuffer::Create(maxOutput);

    strm.next_in = input->GetData();
    strm.avail_in = (uInt)input->GetLength();
    strm.next_out = output->GetData();
    strm.avail_out = (uInt)maxOutput;

    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&strm);
        return ts_error_create((void*)TsString::Create("Raw deflate compression failed"));
    }

    TsBuffer* result = output->Subarray(0, strm.total_out);
    deflateEnd(&strm);

    return ts_value_make_object(result);
}

extern "C" void* ts_zlib_inflate_raw_sync(void* buffer, void* options) {
    void* rawBuf = ts_nanbox_safe_unbox(buffer);
    TsBuffer* input = dynamic_cast<TsBuffer*>((TsObject*)rawBuf);
    if (!input) {
        return ts_error_create((void*)TsString::Create("First argument must be a Buffer"));
    }

    TsZlibOptions opts = TsZlibOptions::FromValue((TsValue*)options);

    z_stream strm = {};
    // Negative windowBits = raw inflate
    int ret = inflateInit2(&strm, -opts.windowBits);
    if (ret != Z_OK) {
        return ts_error_create((void*)TsString::Create("Failed to initialize raw inflate decompression"));
    }

    size_t outputSize = input->GetLength() * 4;
    if (outputSize < 1024) outputSize = 1024;
    if (opts.maxOutputLength > 0 && outputSize > opts.maxOutputLength) {
        outputSize = opts.maxOutputLength;
    }

    TsBuffer* output = TsBuffer::Create(outputSize);
    size_t totalOut = 0;

    strm.next_in = input->GetData();
    strm.avail_in = (uInt)input->GetLength();

    while (true) {
        strm.next_out = output->GetData() + totalOut;
        strm.avail_out = (uInt)(outputSize - totalOut);

        ret = inflate(&strm, Z_NO_FLUSH);

        if (ret == Z_STREAM_END) {
            totalOut = strm.total_out;
            break;
        }

        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return ts_error_create((void*)TsString::Create("Raw inflate decompression failed"));
        }

        totalOut = strm.total_out;

        if (strm.avail_out == 0) {
            size_t newSize = outputSize * 2;
            if (opts.maxOutputLength > 0 && newSize > opts.maxOutputLength) {
                inflateEnd(&strm);
                return ts_error_create((void*)TsString::Create("Output would exceed maxOutputLength"));
            }

            TsBuffer* newOutput = TsBuffer::Create(newSize);
            memcpy(newOutput->GetData(), output->GetData(), totalOut);
            output = newOutput;
            outputSize = newSize;
        }
    }

    TsBuffer* result = output->Subarray(0, totalOut);
    inflateEnd(&strm);

    return ts_value_make_object(result);
}

// =============================================================================
// Sync Convenience Methods - Unzip (auto-detect)
// =============================================================================

extern "C" void* ts_zlib_unzip_sync(void* buffer, void* options) {
    void* rawBuf = ts_nanbox_safe_unbox(buffer);
    TsBuffer* input = dynamic_cast<TsBuffer*>((TsObject*)rawBuf);
    if (!input) {
        return ts_error_create((void*)TsString::Create("First argument must be a Buffer"));
    }

    TsZlibOptions opts = TsZlibOptions::FromValue((TsValue*)options);

    z_stream strm = {};
    // windowBits + 32 = automatic header detection (gzip or zlib)
    int ret = inflateInit2(&strm, 15 + 32);
    if (ret != Z_OK) {
        return ts_error_create((void*)TsString::Create("Failed to initialize unzip decompression"));
    }

    size_t outputSize = input->GetLength() * 4;
    if (outputSize < 1024) outputSize = 1024;
    if (opts.maxOutputLength > 0 && outputSize > opts.maxOutputLength) {
        outputSize = opts.maxOutputLength;
    }

    TsBuffer* output = TsBuffer::Create(outputSize);
    size_t totalOut = 0;

    strm.next_in = input->GetData();
    strm.avail_in = (uInt)input->GetLength();

    while (true) {
        strm.next_out = output->GetData() + totalOut;
        strm.avail_out = (uInt)(outputSize - totalOut);

        ret = inflate(&strm, Z_NO_FLUSH);

        if (ret == Z_STREAM_END) {
            totalOut = strm.total_out;
            break;
        }

        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return ts_error_create((void*)TsString::Create("Unzip decompression failed"));
        }

        totalOut = strm.total_out;

        if (strm.avail_out == 0) {
            size_t newSize = outputSize * 2;
            if (opts.maxOutputLength > 0 && newSize > opts.maxOutputLength) {
                inflateEnd(&strm);
                return ts_error_create((void*)TsString::Create("Output would exceed maxOutputLength"));
            }

            TsBuffer* newOutput = TsBuffer::Create(newSize);
            memcpy(newOutput->GetData(), output->GetData(), totalOut);
            output = newOutput;
            outputSize = newSize;
        }
    }

    TsBuffer* result = output->Subarray(0, totalOut);
    inflateEnd(&strm);

    return ts_value_make_object(result);
}

// =============================================================================
// Sync Convenience Methods - Brotli
// =============================================================================

extern "C" void* ts_zlib_brotli_compress_sync(void* buffer, void* options) {
    void* rawBuf = ts_nanbox_safe_unbox(buffer);
    TsBuffer* input = dynamic_cast<TsBuffer*>((TsObject*)rawBuf);
    if (!input) {
        return ts_error_create((void*)TsString::Create("First argument must be a Buffer"));
    }

    TsBrotliOptions opts = TsBrotliOptions::FromValue((TsValue*)options);

    // Calculate max output size
    size_t maxOutput = BrotliEncoderMaxCompressedSize(input->GetLength());
    if (maxOutput == 0) {
        maxOutput = input->GetLength() + 1024;  // Fallback for small inputs
    }
    if (opts.maxOutputLength > 0 && maxOutput > opts.maxOutputLength) {
        return ts_error_create((void*)TsString::Create("Output would exceed maxOutputLength"));
    }

    TsBuffer* output = TsBuffer::Create(maxOutput);

    size_t encodedSize = maxOutput;
    BROTLI_BOOL result = BrotliEncoderCompress(
        opts.quality,
        opts.lgwin,
        opts.mode,
        input->GetLength(),
        input->GetData(),
        &encodedSize,
        output->GetData()
    );

    if (result != BROTLI_TRUE) {
        return ts_error_create((void*)TsString::Create("Brotli compression failed"));
    }

    TsBuffer* finalResult = output->Subarray(0, encodedSize);
    return ts_value_make_object(finalResult);
}

extern "C" void* ts_zlib_brotli_decompress_sync(void* buffer, void* options) {
    void* rawBuf = ts_nanbox_safe_unbox(buffer);
    TsBuffer* input = dynamic_cast<TsBuffer*>((TsObject*)rawBuf);
    if (!input) {
        return ts_error_create((void*)TsString::Create("First argument must be a Buffer"));
    }

    TsBrotliOptions opts = TsBrotliOptions::FromValue((TsValue*)options);

    // Start with a larger estimated output size
    // Brotli can achieve very high compression ratios (100:1 or more for repetitive data)
    // Use a generous initial estimate and grow if needed
    size_t outputSize = input->GetLength() * 100;  // 100x compression ratio
    if (outputSize < 4096) outputSize = 4096;
    if (opts.maxOutputLength > 0 && outputSize > opts.maxOutputLength) {
        outputSize = opts.maxOutputLength;
    }

    BrotliDecoderResult result = BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;
    TsBuffer* output = nullptr;
    size_t decodedSize = 0;

    // Keep trying with larger buffers until decompression succeeds
    while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
        output = TsBuffer::Create(outputSize);
        decodedSize = outputSize;
        result = BrotliDecoderDecompress(
            input->GetLength(),
            input->GetData(),
            &decodedSize,
            output->GetData()
        );

        if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
            outputSize *= 2;
            if (opts.maxOutputLength > 0 && outputSize > opts.maxOutputLength) {
                return ts_error_create((void*)TsString::Create("Output would exceed maxOutputLength"));
            }
        }
    }

    if (result != BROTLI_DECODER_RESULT_SUCCESS) {
        return ts_error_create((void*)TsString::Create("Brotli decompression failed"));
    }

    TsBuffer* finalResult = output->Subarray(0, decodedSize);
    return ts_value_make_object(finalResult);
}

// =============================================================================
// Async Convenience Methods - Use libuv work queue
// =============================================================================

struct ZlibAsyncWork {
    void* buffer;
    void* options;
    void* callback;
    void* result;
    bool isCompress;
    int type;  // 0=gzip, 1=gunzip, 2=deflate, 3=inflate, 4=deflateRaw, 5=inflateRaw, 6=unzip, 7=brotliCompress, 8=brotliDecompress
};

static void zlib_async_execute(void* work) {
    ZlibAsyncWork* w = (ZlibAsyncWork*)work;

    switch (w->type) {
        case 0: w->result = ts_zlib_gzip_sync(w->buffer, w->options); break;
        case 1: w->result = ts_zlib_gunzip_sync(w->buffer, w->options); break;
        case 2: w->result = ts_zlib_deflate_sync(w->buffer, w->options); break;
        case 3: w->result = ts_zlib_inflate_sync(w->buffer, w->options); break;
        case 4: w->result = ts_zlib_deflate_raw_sync(w->buffer, w->options); break;
        case 5: w->result = ts_zlib_inflate_raw_sync(w->buffer, w->options); break;
        case 6: w->result = ts_zlib_unzip_sync(w->buffer, w->options); break;
        case 7: w->result = ts_zlib_brotli_compress_sync(w->buffer, w->options); break;
        case 8: w->result = ts_zlib_brotli_decompress_sync(w->buffer, w->options); break;
    }
}

static void zlib_async_complete(ZlibAsyncWork* w) {
    if (w->callback) {
        // Node.js callback pattern: callback(err, result)
        ts_call_2((TsValue*)w->callback, nullptr, (TsValue*)w->result);
    }
}

extern "C" void ts_zlib_gzip(void* buffer, void* options, void* callback) {
    // For simplicity, run synchronously and call callback
    void* result = ts_zlib_gzip_sync(buffer, options);
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, (TsValue*)result);
    }
}

extern "C" void ts_zlib_gunzip(void* buffer, void* options, void* callback) {
    void* result = ts_zlib_gunzip_sync(buffer, options);
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, (TsValue*)result);
    }
}

extern "C" void ts_zlib_deflate(void* buffer, void* options, void* callback) {
    void* result = ts_zlib_deflate_sync(buffer, options);
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, (TsValue*)result);
    }
}

extern "C" void ts_zlib_inflate(void* buffer, void* options, void* callback) {
    void* result = ts_zlib_inflate_sync(buffer, options);
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, (TsValue*)result);
    }
}

extern "C" void ts_zlib_deflate_raw(void* buffer, void* options, void* callback) {
    void* result = ts_zlib_deflate_raw_sync(buffer, options);
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, (TsValue*)result);
    }
}

extern "C" void ts_zlib_inflate_raw(void* buffer, void* options, void* callback) {
    void* result = ts_zlib_inflate_raw_sync(buffer, options);
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, (TsValue*)result);
    }
}

extern "C" void ts_zlib_unzip(void* buffer, void* options, void* callback) {
    void* result = ts_zlib_unzip_sync(buffer, options);
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, (TsValue*)result);
    }
}

extern "C" void ts_zlib_brotli_compress(void* buffer, void* options, void* callback) {
    void* result = ts_zlib_brotli_compress_sync(buffer, options);
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, (TsValue*)result);
    }
}

extern "C" void ts_zlib_brotli_decompress(void* buffer, void* options, void* callback) {
    void* result = ts_zlib_brotli_decompress_sync(buffer, options);
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, (TsValue*)result);
    }
}

// =============================================================================
// Stream Creators
// =============================================================================

extern "C" void* ts_zlib_create_gzip(void* options) {
    TsGzip* gzip = TsGzip::Create((TsValue*)options);
    return ts_value_make_object(gzip);
}

extern "C" void* ts_zlib_create_gunzip(void* options) {
    TsGunzip* gunzip = TsGunzip::Create((TsValue*)options);
    return ts_value_make_object(gunzip);
}

extern "C" void* ts_zlib_create_deflate(void* options) {
    TsDeflate* deflate = TsDeflate::Create((TsValue*)options);
    return ts_value_make_object(deflate);
}

extern "C" void* ts_zlib_create_inflate(void* options) {
    TsInflate* inflate = TsInflate::Create((TsValue*)options);
    return ts_value_make_object(inflate);
}

extern "C" void* ts_zlib_create_deflate_raw(void* options) {
    TsDeflateRaw* deflateRaw = TsDeflateRaw::Create((TsValue*)options);
    return ts_value_make_object(deflateRaw);
}

extern "C" void* ts_zlib_create_inflate_raw(void* options) {
    TsInflateRaw* inflateRaw = TsInflateRaw::Create((TsValue*)options);
    return ts_value_make_object(inflateRaw);
}

extern "C" void* ts_zlib_create_unzip(void* options) {
    TsUnzip* unzip = TsUnzip::Create((TsValue*)options);
    return ts_value_make_object(unzip);
}

extern "C" void* ts_zlib_create_brotli_compress(void* options) {
    TsBrotliCompress* brotli = TsBrotliCompress::Create((TsValue*)options);
    return ts_value_make_object(brotli);
}

extern "C" void* ts_zlib_create_brotli_decompress(void* options) {
    TsBrotliDecompress* brotli = TsBrotliDecompress::Create((TsValue*)options);
    return ts_value_make_object(brotli);
}

// =============================================================================
// Stream Methods
// =============================================================================

extern "C" void ts_zlib_flush(void* stream, int64_t kind, void* callback) {
    void* rawStream = ts_nanbox_safe_unbox(stream);
    TsZlibBase* zlib = dynamic_cast<TsZlibBase*>((TsObject*)rawStream);
    if (zlib) {
        zlib->Flush((int)kind, callback);
    }
}

extern "C" void ts_zlib_close(void* stream, void* callback) {
    void* rawStream = ts_nanbox_safe_unbox(stream);
    TsZlibBase* zlib = dynamic_cast<TsZlibBase*>((TsObject*)rawStream);
    if (zlib) {
        zlib->Close(callback);
    }
}

extern "C" void ts_zlib_params(void* stream, int64_t level, int64_t strategy, void* callback) {
    void* rawStream = ts_nanbox_safe_unbox(stream);
    TsDeflate* deflate = dynamic_cast<TsDeflate*>((TsObject*)rawStream);
    if (deflate) {
        deflate->Params((int)level, (int)strategy, callback);
    }
}

extern "C" void ts_zlib_reset(void* stream) {
    void* rawStream = ts_nanbox_safe_unbox(stream);
    TsZlibBase* zlib = dynamic_cast<TsZlibBase*>((TsObject*)rawStream);
    if (zlib) {
        zlib->Reset();
    }
}

extern "C" int64_t ts_zlib_get_bytes_written(void* stream) {
    void* rawStream = ts_nanbox_safe_unbox(stream);
    TsZlibBase* zlib = dynamic_cast<TsZlibBase*>((TsObject*)rawStream);
    if (zlib) {
        return (int64_t)zlib->bytesWritten;
    }
    return 0;
}

// =============================================================================
// Utilities
// =============================================================================

extern "C" int64_t ts_zlib_crc32(void* data, int64_t value) {
    void* rawData = ts_nanbox_safe_unbox(data);

    // Check if it's a Buffer
    TsBuffer* buffer = dynamic_cast<TsBuffer*>((TsObject*)rawData);
    if (buffer) {
        uLong crc = (value == 0) ? crc32(0L, Z_NULL, 0) : (uLong)value;
        return (int64_t)crc32(crc, buffer->GetData(), (uInt)buffer->GetLength());
    }

    // Check if it's a String
    TsString* str = dynamic_cast<TsString*>((TsObject*)rawData);
    if (str) {
        const char* utf8 = str->ToUtf8();
        uLong crc = (value == 0) ? crc32(0L, Z_NULL, 0) : (uLong)value;
        return (int64_t)crc32(crc, (const Bytef*)utf8, (uInt)strlen(utf8));
    }

    return 0;
}

// =============================================================================
// Constants
// =============================================================================

extern "C" void* ts_zlib_get_constants() {
    TsMap* constants = TsMap::Create();

    // Flush modes
    constants->Set(TsValue(TsString::Create("Z_NO_FLUSH")), TsValue((int64_t)Z_NO_FLUSH));
    constants->Set(TsValue(TsString::Create("Z_PARTIAL_FLUSH")), TsValue((int64_t)Z_PARTIAL_FLUSH));
    constants->Set(TsValue(TsString::Create("Z_SYNC_FLUSH")), TsValue((int64_t)Z_SYNC_FLUSH));
    constants->Set(TsValue(TsString::Create("Z_FULL_FLUSH")), TsValue((int64_t)Z_FULL_FLUSH));
    constants->Set(TsValue(TsString::Create("Z_FINISH")), TsValue((int64_t)Z_FINISH));
    constants->Set(TsValue(TsString::Create("Z_BLOCK")), TsValue((int64_t)Z_BLOCK));

    // Compression levels
    constants->Set(TsValue(TsString::Create("Z_NO_COMPRESSION")), TsValue((int64_t)Z_NO_COMPRESSION));
    constants->Set(TsValue(TsString::Create("Z_BEST_SPEED")), TsValue((int64_t)Z_BEST_SPEED));
    constants->Set(TsValue(TsString::Create("Z_BEST_COMPRESSION")), TsValue((int64_t)Z_BEST_COMPRESSION));
    constants->Set(TsValue(TsString::Create("Z_DEFAULT_COMPRESSION")), TsValue((int64_t)Z_DEFAULT_COMPRESSION));

    // Compression strategies
    constants->Set(TsValue(TsString::Create("Z_FILTERED")), TsValue((int64_t)Z_FILTERED));
    constants->Set(TsValue(TsString::Create("Z_HUFFMAN_ONLY")), TsValue((int64_t)Z_HUFFMAN_ONLY));
    constants->Set(TsValue(TsString::Create("Z_RLE")), TsValue((int64_t)Z_RLE));
    constants->Set(TsValue(TsString::Create("Z_FIXED")), TsValue((int64_t)Z_FIXED));
    constants->Set(TsValue(TsString::Create("Z_DEFAULT_STRATEGY")), TsValue((int64_t)Z_DEFAULT_STRATEGY));

    // Return codes
    constants->Set(TsValue(TsString::Create("Z_OK")), TsValue((int64_t)Z_OK));
    constants->Set(TsValue(TsString::Create("Z_STREAM_END")), TsValue((int64_t)Z_STREAM_END));
    constants->Set(TsValue(TsString::Create("Z_NEED_DICT")), TsValue((int64_t)Z_NEED_DICT));
    constants->Set(TsValue(TsString::Create("Z_ERRNO")), TsValue((int64_t)Z_ERRNO));
    constants->Set(TsValue(TsString::Create("Z_STREAM_ERROR")), TsValue((int64_t)Z_STREAM_ERROR));
    constants->Set(TsValue(TsString::Create("Z_DATA_ERROR")), TsValue((int64_t)Z_DATA_ERROR));
    constants->Set(TsValue(TsString::Create("Z_MEM_ERROR")), TsValue((int64_t)Z_MEM_ERROR));
    constants->Set(TsValue(TsString::Create("Z_BUF_ERROR")), TsValue((int64_t)Z_BUF_ERROR));
    constants->Set(TsValue(TsString::Create("Z_VERSION_ERROR")), TsValue((int64_t)Z_VERSION_ERROR));

    // Brotli flush operations
    constants->Set(TsValue(TsString::Create("BROTLI_OPERATION_PROCESS")), TsValue((int64_t)BROTLI_OPERATION_PROCESS));
    constants->Set(TsValue(TsString::Create("BROTLI_OPERATION_FLUSH")), TsValue((int64_t)BROTLI_OPERATION_FLUSH));
    constants->Set(TsValue(TsString::Create("BROTLI_OPERATION_FINISH")), TsValue((int64_t)BROTLI_OPERATION_FINISH));
    constants->Set(TsValue(TsString::Create("BROTLI_OPERATION_EMIT_METADATA")), TsValue((int64_t)BROTLI_OPERATION_EMIT_METADATA));

    // Brotli encoder modes
    constants->Set(TsValue(TsString::Create("BROTLI_MODE_GENERIC")), TsValue((int64_t)BROTLI_MODE_GENERIC));
    constants->Set(TsValue(TsString::Create("BROTLI_MODE_TEXT")), TsValue((int64_t)BROTLI_MODE_TEXT));
    constants->Set(TsValue(TsString::Create("BROTLI_MODE_FONT")), TsValue((int64_t)BROTLI_MODE_FONT));

    // Brotli quality range
    constants->Set(TsValue(TsString::Create("BROTLI_MIN_QUALITY")), TsValue((int64_t)BROTLI_MIN_QUALITY));
    constants->Set(TsValue(TsString::Create("BROTLI_MAX_QUALITY")), TsValue((int64_t)BROTLI_MAX_QUALITY));
    constants->Set(TsValue(TsString::Create("BROTLI_DEFAULT_QUALITY")), TsValue((int64_t)BROTLI_DEFAULT_QUALITY));

    // Brotli window bits range
    constants->Set(TsValue(TsString::Create("BROTLI_MIN_WINDOW_BITS")), TsValue((int64_t)BROTLI_MIN_WINDOW_BITS));
    constants->Set(TsValue(TsString::Create("BROTLI_MAX_WINDOW_BITS")), TsValue((int64_t)BROTLI_MAX_WINDOW_BITS));
    constants->Set(TsValue(TsString::Create("BROTLI_DEFAULT_WINDOW")), TsValue((int64_t)BROTLI_DEFAULT_WINDOW));

    // Brotli encoder parameters
    constants->Set(TsValue(TsString::Create("BROTLI_PARAM_MODE")), TsValue((int64_t)BROTLI_PARAM_MODE));
    constants->Set(TsValue(TsString::Create("BROTLI_PARAM_QUALITY")), TsValue((int64_t)BROTLI_PARAM_QUALITY));
    constants->Set(TsValue(TsString::Create("BROTLI_PARAM_LGWIN")), TsValue((int64_t)BROTLI_PARAM_LGWIN));
    constants->Set(TsValue(TsString::Create("BROTLI_PARAM_LGBLOCK")), TsValue((int64_t)BROTLI_PARAM_LGBLOCK));
    constants->Set(TsValue(TsString::Create("BROTLI_PARAM_SIZE_HINT")), TsValue((int64_t)BROTLI_PARAM_SIZE_HINT));

    // Brotli decoder parameters
    constants->Set(TsValue(TsString::Create("BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION")), TsValue((int64_t)BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION));
    constants->Set(TsValue(TsString::Create("BROTLI_DECODER_PARAM_LARGE_WINDOW")), TsValue((int64_t)BROTLI_DECODER_PARAM_LARGE_WINDOW));

    return ts_value_make_object(constants);
}

// =============================================================================
// Stream Class Implementations
// =============================================================================

// TsGzip
TsGzip::TsGzip() : TsZlibBase() {
    memset(&strm, 0, sizeof(strm));
}

TsGzip::~TsGzip() {
    if (initialized) {
        deflateEnd(&strm);
    }
}

TsGzip* TsGzip::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsGzip));
    TsGzip* gzip = new (mem) TsGzip();
    gzip->Init(options);
    return gzip;
}

void TsGzip::Init(TsValue* options) {
    TsZlibOptions opts = TsZlibOptions::FromValue(options);
    level = opts.level;
    windowBits = 15;
    memLevel = opts.memLevel;
    strategy = opts.strategy;
    chunkSize = opts.chunkSize;

    int ret = deflateInit2(&strm, level, Z_DEFLATED, 15 + 16, memLevel, strategy);
    initialized = (ret == Z_OK);
}

bool TsGzip::Write(void* data, size_t length) {
    if (!initialized) return false;
    // Streaming implementation would process data here
    bytesWritten += length;
    return true;
}

void TsGzip::End() {
    // Flush remaining data
}

void TsGzip::Flush(int kind, void* callback) {
    TsZlibBase::Flush(kind, callback);
}

void TsGzip::Reset() {
    if (initialized) {
        deflateReset(&strm);
    }
}

// TsGunzip
TsGunzip::TsGunzip() : TsZlibBase() {
    memset(&strm, 0, sizeof(strm));
}

TsGunzip::~TsGunzip() {
    if (initialized) {
        inflateEnd(&strm);
    }
}

TsGunzip* TsGunzip::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsGunzip));
    TsGunzip* gunzip = new (mem) TsGunzip();
    gunzip->Init(options);
    return gunzip;
}

void TsGunzip::Init(TsValue* options) {
    TsZlibOptions opts = TsZlibOptions::FromValue(options);
    windowBits = 15;
    chunkSize = opts.chunkSize;

    int ret = inflateInit2(&strm, 15 + 16);
    initialized = (ret == Z_OK);
}

bool TsGunzip::Write(void* data, size_t length) {
    if (!initialized) return false;
    bytesWritten += length;
    return true;
}

void TsGunzip::End() {}

void TsGunzip::Flush(int kind, void* callback) {
    TsZlibBase::Flush(kind, callback);
}

void TsGunzip::Reset() {
    if (initialized) {
        inflateReset(&strm);
    }
}

// TsDeflate
TsDeflate::TsDeflate() : TsZlibBase() {
    memset(&strm, 0, sizeof(strm));
}

TsDeflate::~TsDeflate() {
    if (initialized) {
        deflateEnd(&strm);
    }
}

TsDeflate* TsDeflate::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsDeflate));
    TsDeflate* deflate = new (mem) TsDeflate();
    deflate->Init(options);
    return deflate;
}

void TsDeflate::Init(TsValue* options) {
    TsZlibOptions opts = TsZlibOptions::FromValue(options);
    level = opts.level;
    windowBits = opts.windowBits;
    memLevel = opts.memLevel;
    strategy = opts.strategy;
    chunkSize = opts.chunkSize;

    int ret = deflateInit2(&strm, level, Z_DEFLATED, windowBits, memLevel, strategy);
    initialized = (ret == Z_OK);
}

bool TsDeflate::Write(void* data, size_t length) {
    if (!initialized) return false;
    bytesWritten += length;
    return true;
}

void TsDeflate::End() {}

void TsDeflate::Flush(int kind, void* callback) {
    TsZlibBase::Flush(kind, callback);
}

void TsDeflate::Params(int newLevel, int newStrategy, void* callback) {
    if (initialized) {
        deflateParams(&strm, newLevel, newStrategy);
        level = newLevel;
        strategy = newStrategy;
    }
    if (callback) {
        ts_call_2((TsValue*)callback, nullptr, nullptr);
    }
}

void TsDeflate::Reset() {
    if (initialized) {
        deflateReset(&strm);
    }
}

// TsInflate
TsInflate::TsInflate() : TsZlibBase() {
    memset(&strm, 0, sizeof(strm));
}

TsInflate::~TsInflate() {
    if (initialized) {
        inflateEnd(&strm);
    }
}

TsInflate* TsInflate::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsInflate));
    TsInflate* inflate = new (mem) TsInflate();
    inflate->Init(options);
    return inflate;
}

void TsInflate::Init(TsValue* options) {
    TsZlibOptions opts = TsZlibOptions::FromValue(options);
    windowBits = opts.windowBits;
    chunkSize = opts.chunkSize;

    int ret = inflateInit2(&strm, windowBits);
    initialized = (ret == Z_OK);
}

bool TsInflate::Write(void* data, size_t length) {
    if (!initialized) return false;
    bytesWritten += length;
    return true;
}

void TsInflate::End() {}

void TsInflate::Flush(int kind, void* callback) {
    TsZlibBase::Flush(kind, callback);
}

void TsInflate::Reset() {
    if (initialized) {
        inflateReset(&strm);
    }
}

// TsDeflateRaw
TsDeflateRaw::TsDeflateRaw() : TsZlibBase() {
    memset(&strm, 0, sizeof(strm));
}

TsDeflateRaw::~TsDeflateRaw() {
    if (initialized) {
        deflateEnd(&strm);
    }
}

TsDeflateRaw* TsDeflateRaw::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsDeflateRaw));
    TsDeflateRaw* deflateRaw = new (mem) TsDeflateRaw();
    deflateRaw->Init(options);
    return deflateRaw;
}

void TsDeflateRaw::Init(TsValue* options) {
    TsZlibOptions opts = TsZlibOptions::FromValue(options);
    level = opts.level;
    windowBits = opts.windowBits;
    memLevel = opts.memLevel;
    strategy = opts.strategy;
    chunkSize = opts.chunkSize;

    int ret = deflateInit2(&strm, level, Z_DEFLATED, -windowBits, memLevel, strategy);
    initialized = (ret == Z_OK);
}

bool TsDeflateRaw::Write(void* data, size_t length) {
    if (!initialized) return false;
    bytesWritten += length;
    return true;
}

void TsDeflateRaw::End() {}

void TsDeflateRaw::Flush(int kind, void* callback) {
    TsZlibBase::Flush(kind, callback);
}

void TsDeflateRaw::Reset() {
    if (initialized) {
        deflateReset(&strm);
    }
}

// TsInflateRaw
TsInflateRaw::TsInflateRaw() : TsZlibBase() {
    memset(&strm, 0, sizeof(strm));
}

TsInflateRaw::~TsInflateRaw() {
    if (initialized) {
        inflateEnd(&strm);
    }
}

TsInflateRaw* TsInflateRaw::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsInflateRaw));
    TsInflateRaw* inflateRaw = new (mem) TsInflateRaw();
    inflateRaw->Init(options);
    return inflateRaw;
}

void TsInflateRaw::Init(TsValue* options) {
    TsZlibOptions opts = TsZlibOptions::FromValue(options);
    windowBits = opts.windowBits;
    chunkSize = opts.chunkSize;

    int ret = inflateInit2(&strm, -windowBits);
    initialized = (ret == Z_OK);
}

bool TsInflateRaw::Write(void* data, size_t length) {
    if (!initialized) return false;
    bytesWritten += length;
    return true;
}

void TsInflateRaw::End() {}

void TsInflateRaw::Flush(int kind, void* callback) {
    TsZlibBase::Flush(kind, callback);
}

void TsInflateRaw::Reset() {
    if (initialized) {
        inflateReset(&strm);
    }
}

// TsUnzip
TsUnzip::TsUnzip() : TsZlibBase() {
    memset(&strm, 0, sizeof(strm));
}

TsUnzip::~TsUnzip() {
    if (initialized) {
        inflateEnd(&strm);
    }
}

TsUnzip* TsUnzip::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsUnzip));
    TsUnzip* unzip = new (mem) TsUnzip();
    unzip->Init(options);
    return unzip;
}

void TsUnzip::Init(TsValue* options) {
    TsZlibOptions opts = TsZlibOptions::FromValue(options);
    windowBits = 15;
    chunkSize = opts.chunkSize;

    int ret = inflateInit2(&strm, 15 + 32);  // Auto-detect gzip or zlib
    initialized = (ret == Z_OK);
}

bool TsUnzip::Write(void* data, size_t length) {
    if (!initialized) return false;
    bytesWritten += length;
    return true;
}

void TsUnzip::End() {}

void TsUnzip::Flush(int kind, void* callback) {
    TsZlibBase::Flush(kind, callback);
}

void TsUnzip::Reset() {
    if (initialized) {
        inflateReset(&strm);
    }
}

// TsBrotliCompress
TsBrotliCompress::TsBrotliCompress() : TsZlibBase() {
    state = nullptr;
}

TsBrotliCompress::~TsBrotliCompress() {
    if (state) {
        BrotliEncoderDestroyInstance(state);
    }
}

TsBrotliCompress* TsBrotliCompress::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsBrotliCompress));
    TsBrotliCompress* brotli = new (mem) TsBrotliCompress();
    brotli->Init(options);
    return brotli;
}

void TsBrotliCompress::Init(TsValue* options) {
    TsBrotliOptions opts = TsBrotliOptions::FromValue(options);
    quality = opts.quality;
    lgwin = opts.lgwin;
    mode = opts.mode;
    chunkSize = opts.chunkSize;

    state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
    if (state) {
        BrotliEncoderSetParameter(state, BROTLI_PARAM_QUALITY, quality);
        BrotliEncoderSetParameter(state, BROTLI_PARAM_LGWIN, lgwin);
        BrotliEncoderSetParameter(state, BROTLI_PARAM_MODE, mode);
    }
}

bool TsBrotliCompress::Write(void* data, size_t length) {
    if (!state) return false;
    bytesWritten += length;
    return true;
}

void TsBrotliCompress::End() {}

void TsBrotliCompress::Flush(int kind, void* callback) {
    TsZlibBase::Flush(kind, callback);
}

// TsBrotliDecompress
TsBrotliDecompress::TsBrotliDecompress() : TsZlibBase() {
    state = nullptr;
}

TsBrotliDecompress::~TsBrotliDecompress() {
    if (state) {
        BrotliDecoderDestroyInstance(state);
    }
}

TsBrotliDecompress* TsBrotliDecompress::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsBrotliDecompress));
    TsBrotliDecompress* brotli = new (mem) TsBrotliDecompress();
    brotli->Init(options);
    return brotli;
}

void TsBrotliDecompress::Init(TsValue* options) {
    TsBrotliOptions opts = TsBrotliOptions::FromValue(options);
    chunkSize = opts.chunkSize;

    state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
}

bool TsBrotliDecompress::Write(void* data, size_t length) {
    if (!state) return false;
    bytesWritten += length;
    return true;
}

void TsBrotliDecompress::End() {}

void TsBrotliDecompress::Flush(int kind, void* callback) {
    TsZlibBase::Flush(kind, callback);
}
