/*
 * Zlib Module Type Registration
 *
 * Registers types for the Node.js zlib module.
 */

#include "Analyzer.h"

namespace ts {

void Analyzer::registerZlib() {
    // =========================================================================
    // ZlibOptions type
    // =========================================================================
    auto zlibOptionsType = std::make_shared<ObjectType>();
    zlibOptionsType->fields["flush"] = std::make_shared<Type>(TypeKind::Int);
    zlibOptionsType->fields["finishFlush"] = std::make_shared<Type>(TypeKind::Int);
    zlibOptionsType->fields["chunkSize"] = std::make_shared<Type>(TypeKind::Int);
    zlibOptionsType->fields["windowBits"] = std::make_shared<Type>(TypeKind::Int);
    zlibOptionsType->fields["level"] = std::make_shared<Type>(TypeKind::Int);
    zlibOptionsType->fields["memLevel"] = std::make_shared<Type>(TypeKind::Int);
    zlibOptionsType->fields["strategy"] = std::make_shared<Type>(TypeKind::Int);
    zlibOptionsType->fields["dictionary"] = std::make_shared<ClassType>("Buffer");
    zlibOptionsType->fields["maxOutputLength"] = std::make_shared<Type>(TypeKind::Int);
    symbols.defineType("ZlibOptions", zlibOptionsType);

    // =========================================================================
    // BrotliOptions type
    // =========================================================================
    auto brotliOptionsType = std::make_shared<ObjectType>();
    brotliOptionsType->fields["flush"] = std::make_shared<Type>(TypeKind::Int);
    brotliOptionsType->fields["finishFlush"] = std::make_shared<Type>(TypeKind::Int);
    brotliOptionsType->fields["chunkSize"] = std::make_shared<Type>(TypeKind::Int);
    brotliOptionsType->fields["params"] = std::make_shared<Type>(TypeKind::Any);
    brotliOptionsType->fields["maxOutputLength"] = std::make_shared<Type>(TypeKind::Int);
    symbols.defineType("BrotliOptions", brotliOptionsType);

    // =========================================================================
    // Zlib stream classes - base type
    // =========================================================================
    auto zlibBaseClass = std::make_shared<ClassType>("Zlib");
    zlibBaseClass->fields["bytesWritten"] = std::make_shared<Type>(TypeKind::Int);

    // Methods
    auto flushMethod = std::make_shared<FunctionType>();
    flushMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // kind
    flushMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // callback
    flushMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibBaseClass->fields["flush"] = flushMethod;

    auto closeMethod = std::make_shared<FunctionType>();
    closeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // callback
    closeMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibBaseClass->fields["close"] = closeMethod;

    auto resetMethod = std::make_shared<FunctionType>();
    resetMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibBaseClass->fields["reset"] = resetMethod;

    symbols.defineType("Zlib", zlibBaseClass);

    // =========================================================================
    // Gzip class
    // =========================================================================
    auto gzipClass = std::make_shared<ClassType>("Gzip");
    for (const auto& [name, type] : zlibBaseClass->fields) {
        gzipClass->fields[name] = type;
    }
    symbols.defineType("Gzip", gzipClass);

    // =========================================================================
    // Gunzip class
    // =========================================================================
    auto gunzipClass = std::make_shared<ClassType>("Gunzip");
    for (const auto& [name, type] : zlibBaseClass->fields) {
        gunzipClass->fields[name] = type;
    }
    symbols.defineType("Gunzip", gunzipClass);

    // =========================================================================
    // Deflate class
    // =========================================================================
    auto deflateClass = std::make_shared<ClassType>("Deflate");
    for (const auto& [name, type] : zlibBaseClass->fields) {
        deflateClass->fields[name] = type;
    }
    // Deflate-specific params method
    auto paramsMethod = std::make_shared<FunctionType>();
    paramsMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // level
    paramsMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // strategy
    paramsMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // callback
    paramsMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    deflateClass->fields["params"] = paramsMethod;
    symbols.defineType("Deflate", deflateClass);

    // =========================================================================
    // Inflate class
    // =========================================================================
    auto inflateClass = std::make_shared<ClassType>("Inflate");
    for (const auto& [name, type] : zlibBaseClass->fields) {
        inflateClass->fields[name] = type;
    }
    symbols.defineType("Inflate", inflateClass);

    // =========================================================================
    // DeflateRaw class
    // =========================================================================
    auto deflateRawClass = std::make_shared<ClassType>("DeflateRaw");
    for (const auto& [name, type] : zlibBaseClass->fields) {
        deflateRawClass->fields[name] = type;
    }
    symbols.defineType("DeflateRaw", deflateRawClass);

    // =========================================================================
    // InflateRaw class
    // =========================================================================
    auto inflateRawClass = std::make_shared<ClassType>("InflateRaw");
    for (const auto& [name, type] : zlibBaseClass->fields) {
        inflateRawClass->fields[name] = type;
    }
    symbols.defineType("InflateRaw", inflateRawClass);

    // =========================================================================
    // Unzip class
    // =========================================================================
    auto unzipClass = std::make_shared<ClassType>("Unzip");
    for (const auto& [name, type] : zlibBaseClass->fields) {
        unzipClass->fields[name] = type;
    }
    symbols.defineType("Unzip", unzipClass);

    // =========================================================================
    // BrotliCompress class
    // =========================================================================
    auto brotliCompressClass = std::make_shared<ClassType>("BrotliCompress");
    for (const auto& [name, type] : zlibBaseClass->fields) {
        brotliCompressClass->fields[name] = type;
    }
    symbols.defineType("BrotliCompress", brotliCompressClass);

    // =========================================================================
    // BrotliDecompress class
    // =========================================================================
    auto brotliDecompressClass = std::make_shared<ClassType>("BrotliDecompress");
    for (const auto& [name, type] : zlibBaseClass->fields) {
        brotliDecompressClass->fields[name] = type;
    }
    symbols.defineType("BrotliDecompress", brotliDecompressClass);

    // =========================================================================
    // Constants type
    // =========================================================================
    auto constantsType = std::make_shared<ObjectType>();
    // Flush modes
    constantsType->fields["Z_NO_FLUSH"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_PARTIAL_FLUSH"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_SYNC_FLUSH"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_FULL_FLUSH"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_FINISH"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_BLOCK"] = std::make_shared<Type>(TypeKind::Int);
    // Compression levels
    constantsType->fields["Z_NO_COMPRESSION"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_BEST_SPEED"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_BEST_COMPRESSION"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_DEFAULT_COMPRESSION"] = std::make_shared<Type>(TypeKind::Int);
    // Strategies
    constantsType->fields["Z_FILTERED"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_HUFFMAN_ONLY"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_RLE"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_FIXED"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_DEFAULT_STRATEGY"] = std::make_shared<Type>(TypeKind::Int);
    // Return codes
    constantsType->fields["Z_OK"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_STREAM_END"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_NEED_DICT"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_ERRNO"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_STREAM_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_DATA_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_MEM_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_BUF_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["Z_VERSION_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    // Brotli constants
    constantsType->fields["BROTLI_OPERATION_PROCESS"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_OPERATION_FLUSH"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_OPERATION_FINISH"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_OPERATION_EMIT_METADATA"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_MODE_GENERIC"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_MODE_TEXT"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_MODE_FONT"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_MIN_QUALITY"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_MAX_QUALITY"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_DEFAULT_QUALITY"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_MIN_WINDOW_BITS"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_MAX_WINDOW_BITS"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_DEFAULT_WINDOW"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_PARAM_MODE"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_PARAM_QUALITY"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_PARAM_LGWIN"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_PARAM_LGBLOCK"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["BROTLI_PARAM_SIZE_HINT"] = std::make_shared<Type>(TypeKind::Int);

    // =========================================================================
    // Zlib module
    // =========================================================================
    auto zlibModule = std::make_shared<ObjectType>();

    // Sync convenience methods - return Buffer
    // Use the existing Buffer type from the symbol table so properties like .length work
    auto bufferType = symbols.lookupType("Buffer");
    if (!bufferType) {
        // Fallback to creating a new ClassType if Buffer not yet registered
        bufferType = std::make_shared<ClassType>("Buffer");
    }

    auto gzipSyncMethod = std::make_shared<FunctionType>();
    gzipSyncMethod->paramTypes.push_back(bufferType);
    gzipSyncMethod->paramTypes.push_back(zlibOptionsType);
    gzipSyncMethod->returnType = bufferType;
    zlibModule->fields["gzipSync"] = gzipSyncMethod;

    auto gunzipSyncMethod = std::make_shared<FunctionType>();
    gunzipSyncMethod->paramTypes.push_back(bufferType);
    gunzipSyncMethod->paramTypes.push_back(zlibOptionsType);
    gunzipSyncMethod->returnType = bufferType;
    zlibModule->fields["gunzipSync"] = gunzipSyncMethod;

    auto deflateSyncMethod = std::make_shared<FunctionType>();
    deflateSyncMethod->paramTypes.push_back(bufferType);
    deflateSyncMethod->paramTypes.push_back(zlibOptionsType);
    deflateSyncMethod->returnType = bufferType;
    zlibModule->fields["deflateSync"] = deflateSyncMethod;

    auto inflateSyncMethod = std::make_shared<FunctionType>();
    inflateSyncMethod->paramTypes.push_back(bufferType);
    inflateSyncMethod->paramTypes.push_back(zlibOptionsType);
    inflateSyncMethod->returnType = bufferType;
    zlibModule->fields["inflateSync"] = inflateSyncMethod;

    auto deflateRawSyncMethod = std::make_shared<FunctionType>();
    deflateRawSyncMethod->paramTypes.push_back(bufferType);
    deflateRawSyncMethod->paramTypes.push_back(zlibOptionsType);
    deflateRawSyncMethod->returnType = bufferType;
    zlibModule->fields["deflateRawSync"] = deflateRawSyncMethod;

    auto inflateRawSyncMethod = std::make_shared<FunctionType>();
    inflateRawSyncMethod->paramTypes.push_back(bufferType);
    inflateRawSyncMethod->paramTypes.push_back(zlibOptionsType);
    inflateRawSyncMethod->returnType = bufferType;
    zlibModule->fields["inflateRawSync"] = inflateRawSyncMethod;

    auto unzipSyncMethod = std::make_shared<FunctionType>();
    unzipSyncMethod->paramTypes.push_back(bufferType);
    unzipSyncMethod->paramTypes.push_back(zlibOptionsType);
    unzipSyncMethod->returnType = bufferType;
    zlibModule->fields["unzipSync"] = unzipSyncMethod;

    auto brotliCompressSyncMethod = std::make_shared<FunctionType>();
    brotliCompressSyncMethod->paramTypes.push_back(bufferType);
    brotliCompressSyncMethod->paramTypes.push_back(brotliOptionsType);
    brotliCompressSyncMethod->returnType = bufferType;
    zlibModule->fields["brotliCompressSync"] = brotliCompressSyncMethod;

    auto brotliDecompressSyncMethod = std::make_shared<FunctionType>();
    brotliDecompressSyncMethod->paramTypes.push_back(bufferType);
    brotliDecompressSyncMethod->paramTypes.push_back(brotliOptionsType);
    brotliDecompressSyncMethod->returnType = bufferType;
    zlibModule->fields["brotliDecompressSync"] = brotliDecompressSyncMethod;

    // Async convenience methods - return void, take callback
    auto gzipMethod = std::make_shared<FunctionType>();
    gzipMethod->paramTypes.push_back(bufferType);
    gzipMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options or callback
    gzipMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // callback
    gzipMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibModule->fields["gzip"] = gzipMethod;

    auto gunzipMethod = std::make_shared<FunctionType>();
    gunzipMethod->paramTypes.push_back(bufferType);
    gunzipMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    gunzipMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    gunzipMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibModule->fields["gunzip"] = gunzipMethod;

    auto deflateMethod = std::make_shared<FunctionType>();
    deflateMethod->paramTypes.push_back(bufferType);
    deflateMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    deflateMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    deflateMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibModule->fields["deflate"] = deflateMethod;

    auto inflateMethod = std::make_shared<FunctionType>();
    inflateMethod->paramTypes.push_back(bufferType);
    inflateMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    inflateMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    inflateMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibModule->fields["inflate"] = inflateMethod;

    auto deflateRawMethod = std::make_shared<FunctionType>();
    deflateRawMethod->paramTypes.push_back(bufferType);
    deflateRawMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    deflateRawMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    deflateRawMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibModule->fields["deflateRaw"] = deflateRawMethod;

    auto inflateRawMethod = std::make_shared<FunctionType>();
    inflateRawMethod->paramTypes.push_back(bufferType);
    inflateRawMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    inflateRawMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    inflateRawMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibModule->fields["inflateRaw"] = inflateRawMethod;

    auto unzipMethod = std::make_shared<FunctionType>();
    unzipMethod->paramTypes.push_back(bufferType);
    unzipMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    unzipMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    unzipMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibModule->fields["unzip"] = unzipMethod;

    auto brotliCompressMethod = std::make_shared<FunctionType>();
    brotliCompressMethod->paramTypes.push_back(bufferType);
    brotliCompressMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    brotliCompressMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    brotliCompressMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibModule->fields["brotliCompress"] = brotliCompressMethod;

    auto brotliDecompressMethod = std::make_shared<FunctionType>();
    brotliDecompressMethod->paramTypes.push_back(bufferType);
    brotliDecompressMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    brotliDecompressMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    brotliDecompressMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    zlibModule->fields["brotliDecompress"] = brotliDecompressMethod;

    // Stream creators
    auto createGzipMethod = std::make_shared<FunctionType>();
    createGzipMethod->paramTypes.push_back(zlibOptionsType);
    createGzipMethod->returnType = gzipClass;
    zlibModule->fields["createGzip"] = createGzipMethod;

    auto createGunzipMethod = std::make_shared<FunctionType>();
    createGunzipMethod->paramTypes.push_back(zlibOptionsType);
    createGunzipMethod->returnType = gunzipClass;
    zlibModule->fields["createGunzip"] = createGunzipMethod;

    auto createDeflateMethod = std::make_shared<FunctionType>();
    createDeflateMethod->paramTypes.push_back(zlibOptionsType);
    createDeflateMethod->returnType = deflateClass;
    zlibModule->fields["createDeflate"] = createDeflateMethod;

    auto createInflateMethod = std::make_shared<FunctionType>();
    createInflateMethod->paramTypes.push_back(zlibOptionsType);
    createInflateMethod->returnType = inflateClass;
    zlibModule->fields["createInflate"] = createInflateMethod;

    auto createDeflateRawMethod = std::make_shared<FunctionType>();
    createDeflateRawMethod->paramTypes.push_back(zlibOptionsType);
    createDeflateRawMethod->returnType = deflateRawClass;
    zlibModule->fields["createDeflateRaw"] = createDeflateRawMethod;

    auto createInflateRawMethod = std::make_shared<FunctionType>();
    createInflateRawMethod->paramTypes.push_back(zlibOptionsType);
    createInflateRawMethod->returnType = inflateRawClass;
    zlibModule->fields["createInflateRaw"] = createInflateRawMethod;

    auto createUnzipMethod = std::make_shared<FunctionType>();
    createUnzipMethod->paramTypes.push_back(zlibOptionsType);
    createUnzipMethod->returnType = unzipClass;
    zlibModule->fields["createUnzip"] = createUnzipMethod;

    auto createBrotliCompressMethod = std::make_shared<FunctionType>();
    createBrotliCompressMethod->paramTypes.push_back(brotliOptionsType);
    createBrotliCompressMethod->returnType = brotliCompressClass;
    zlibModule->fields["createBrotliCompress"] = createBrotliCompressMethod;

    auto createBrotliDecompressMethod = std::make_shared<FunctionType>();
    createBrotliDecompressMethod->paramTypes.push_back(brotliOptionsType);
    createBrotliDecompressMethod->returnType = brotliDecompressClass;
    zlibModule->fields["createBrotliDecompress"] = createBrotliDecompressMethod;

    // Utility functions
    auto crc32Method = std::make_shared<FunctionType>();
    crc32Method->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // data (Buffer or string)
    crc32Method->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // initial value
    crc32Method->returnType = std::make_shared<Type>(TypeKind::Int);
    zlibModule->fields["crc32"] = crc32Method;

    // Constants
    zlibModule->fields["constants"] = constantsType;

    symbols.define("zlib", zlibModule);
}

} // namespace ts
