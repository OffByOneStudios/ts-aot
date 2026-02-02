#ifndef TS_STREAM_EXT_H
#define TS_STREAM_EXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// ReadStream (fs.createReadStream)
// =============================================================================

// Create a readable file stream
void* ts_fs_createReadStream(void* path);
void* ts_fs_createReadStream_opts(void* path, void* options);

// ReadStream property accessors
int64_t ts_read_stream_bytes_read(void* stream);
void* ts_read_stream_path(void* stream);
bool ts_read_stream_pending(void* stream);

// =============================================================================
// WriteStream (fs.createWriteStream)
// =============================================================================

// Create a writable file stream
void* ts_fs_createWriteStream(void* path);
void* ts_fs_createWriteStream_opts(void* path, void* options);

// WriteStream methods
bool ts_fs_write_stream_write(void* stream, void* buffer);
void ts_fs_write_stream_end(void* stream);

// WriteStream property accessors
int64_t ts_write_stream_bytes_written(void* stream);
void* ts_write_stream_path(void* stream);
bool ts_write_stream_pending(void* stream);

// =============================================================================
// Readable stream interface
// =============================================================================

// Readable state accessors
bool ts_readable_destroyed(void* stream);
bool ts_readable_readable(void* stream);
bool ts_readable_is_paused(void* stream);
bool ts_readable_readable_ended(void* stream);
bool ts_readable_readable_flowing(void* stream);
bool ts_readable_readable_aborted(void* stream);
bool ts_readable_readable_did_read(void* stream);
bool ts_readable_readable_object_mode(void* stream);
int64_t ts_readable_readable_high_water_mark(void* stream);
int64_t ts_readable_readable_length(void* stream);
void* ts_readable_readable_encoding(void* stream);

// Readable methods
void ts_readable_unpipe(void* stream);
void* ts_readable_set_encoding(void* stream, void* encoding);
void ts_readable_unshift(void* stream, void* chunk);
void* ts_readable_read(void* stream, int64_t size);
void* ts_readable_wrap(void* readable, void* oldStream);

// Readable.from() - create readable from iterable
void* ts_readable_from(void* iterable);

// =============================================================================
// Writable stream interface
// =============================================================================

// Writable methods
bool ts_writable_write(void* writable, void* v);
void ts_writable_end(void* writable, void* data);
void ts_writable_cork(void* stream);
void ts_writable_uncork(void* stream);
void* ts_writable_set_default_encoding(void* stream, void* encoding);

// Writable state accessors
bool ts_writable_destroyed(void* stream);
bool ts_writable_writable(void* stream);
bool ts_writable_writable_ended(void* stream);
bool ts_writable_writable_finished(void* stream);
bool ts_writable_writable_need_drain(void* stream);
bool ts_writable_writable_aborted(void* stream);
bool ts_writable_writable_object_mode(void* stream);
int64_t ts_writable_writable_high_water_mark(void* stream);
int64_t ts_writable_writable_length(void* stream);

// =============================================================================
// Stream utilities (pipe, pipeline, finished)
// =============================================================================

// Stream pipe
void* ts_stream_pipe(void* src, void* dest);
void ts_stream_pause(void* stream);
void ts_stream_resume(void* stream);

// stream.pipeline() - connect streams with error handling
void* ts_stream_pipeline(void* streams, void* callback);

// stream.finished() - detect when stream is done
void* ts_stream_finished(void* stream, void* optionsOrCallback, void* callback);

// =============================================================================
// Transform stream
// =============================================================================

void* ts_stream_transform_create(void* options);
void ts_stream_transform_push(void* transformPtr, void* data);
void ts_stream_transform_push_null(void* transformPtr);
void ts_stream_transform_set_transform(void* transformPtr, void* callback);
void ts_stream_transform_set_flush(void* transformPtr, void* callback);

#ifdef __cplusplus
}
#endif

#endif // TS_STREAM_EXT_H
