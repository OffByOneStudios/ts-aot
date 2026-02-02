#ifndef TS_ZLIB_EXT_H
#define TS_ZLIB_EXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif // TS_ZLIB_EXT_H
