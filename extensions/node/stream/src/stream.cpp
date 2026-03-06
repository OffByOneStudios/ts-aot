#ifdef _MSC_VER
#define TS_NOINLINE __declspec(noinline)
#else
#define TS_NOINLINE __attribute__((noinline))
#endif
// Stream module extension for ts-aot
// Contains all extern "C" wrapper functions for stream-related functionality
// Class implementations remain in the runtime for inheritance support

#include "TsStreamExt.h"
#include "TsReadStream.h"
#include "TsWriteStream.h"
#include "TsReadable.h"
#include "TsWritable.h"
#include "TsEventEmitter.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "TsNanBox.h"
#include "TsTransform.h"
#include "GC.h"
#include <uv.h>
#include <fcntl.h>
#include <cstring>
#include <new>

// External function declarations
extern "C" TsValue* ts_function_call(TsValue* boxedFunc, int argc, TsValue** argv);
extern "C" TsValue* ts_object_get_property(void* obj, const char* key);

// =============================================================================
// ReadStream extern "C" wrappers
// =============================================================================

extern "C" {

void* ts_fs_createReadStream(void* path) {
    TsString* pathStr = (TsString*)path;

    uv_fs_t req;
    int fd = (int)uv_fs_open(uv_default_loop(), &req, pathStr->ToUtf8(), O_RDONLY, 0, nullptr);
    uv_fs_req_cleanup(&req);

    if (fd < 0) {
        return nullptr;
    }

    void* mem = ts_alloc(sizeof(TsReadStream));
    TsReadStream* stream = new(mem) TsReadStream(fd);
    stream->SetPath(pathStr->ToUtf8());
    return stream;
}

void* ts_fs_createReadStream_opts(void* path, void* options) {
    TsString* pathStr = (TsString*)path;

    // Parse options object
    ReadStreamOptions opts;

    if (options) {
        // Unbox if needed
        void* rawOpts = ts_nanbox_safe_unbox(options);

        // Try to get properties
        TsValue* startVal = ts_object_get_property(rawOpts, "start");
        TsValue startDec = nanbox_to_tagged(startVal);
        if (startDec.type == ValueType::NUMBER_INT || startDec.type == ValueType::NUMBER_DBL) {
            opts.start = ts_value_get_int(startVal);
        }

        TsValue* endVal = ts_object_get_property(rawOpts, "end");
        TsValue endDec = nanbox_to_tagged(endVal);
        if (endDec.type == ValueType::NUMBER_INT || endDec.type == ValueType::NUMBER_DBL) {
            opts.end = ts_value_get_int(endVal);
        }

        TsValue* hwmVal = ts_object_get_property(rawOpts, "highWaterMark");
        TsValue hwmDec = nanbox_to_tagged(hwmVal);
        if (hwmDec.type == ValueType::NUMBER_INT || hwmDec.type == ValueType::NUMBER_DBL) {
            opts.highWaterMark = (size_t)ts_value_get_int(hwmVal);
        }

        TsValue* autoCloseVal = ts_object_get_property(rawOpts, "autoClose");
        TsValue acDec = nanbox_to_tagged(autoCloseVal);
        if (acDec.type == ValueType::BOOLEAN) {
            opts.autoClose = ts_value_get_bool(autoCloseVal);
        }
    }

    uv_fs_t req;
    int fd = (int)uv_fs_open(uv_default_loop(), &req, pathStr->ToUtf8(), O_RDONLY, 0, nullptr);
    uv_fs_req_cleanup(&req);

    if (fd < 0) {
        return nullptr;
    }

    void* mem = ts_alloc(sizeof(TsReadStream));
    TsReadStream* stream = new(mem) TsReadStream(fd, opts);
    stream->SetPath(pathStr->ToUtf8());
    return stream;
}

// ReadStream property accessors
int64_t ts_read_stream_bytes_read(void* stream) {
    if (!stream) return 0;
    void* rawPtr = ts_nanbox_safe_unbox(stream);
    TsReadStream* rs = dynamic_cast<TsReadStream*>((TsEventEmitter*)rawPtr);
    if (!rs) return 0;
    return rs->GetBytesRead();
}

void* ts_read_stream_path(void* stream) {
    if (!stream) return nullptr;
    void* rawPtr = ts_nanbox_safe_unbox(stream);
    TsReadStream* rs = dynamic_cast<TsReadStream*>((TsEventEmitter*)rawPtr);
    if (!rs) return nullptr;
    const char* path = rs->GetPath();
    if (!path) return nullptr;
    return TsString::Create(path);
}

bool ts_read_stream_pending(void* stream) {
    if (!stream) return true;
    void* rawPtr = ts_nanbox_safe_unbox(stream);
    TsReadStream* rs = dynamic_cast<TsReadStream*>((TsEventEmitter*)rawPtr);
    if (!rs) return true;
    return rs->IsPending();
}

// =============================================================================
// WriteStream extern "C" wrappers
// =============================================================================

void* ts_fs_createWriteStream(void* path) {
    TsString* p = (TsString*)path;
    uv_fs_t open_req;
    int fd = uv_fs_open(uv_default_loop(), &open_req, p->ToUtf8(), O_WRONLY | O_CREAT | O_TRUNC, 0644, NULL);
    uv_fs_req_cleanup(&open_req);

    if (fd < 0) {
        return nullptr;
    }

    void* mem = ts_alloc(sizeof(TsWriteStream));
    TsWriteStream* stream = new(mem) TsWriteStream(fd);
    stream->SetPath(p->ToUtf8());
    return stream;
}

void* ts_fs_createWriteStream_opts(void* path, void* options) {
    TsString* p = (TsString*)path;

    // Parse options object
    WriteStreamOptions opts;
    int openFlags = O_WRONLY | O_CREAT | O_TRUNC;  // default flags

    if (options) {
        // Unbox if needed
        void* rawOpts = ts_nanbox_safe_unbox(options);

        // Try to get properties
        TsValue* startVal = ts_object_get_property(rawOpts, "start");
        TsValue startDec = nanbox_to_tagged(startVal);
        if (startDec.type == ValueType::NUMBER_INT || startDec.type == ValueType::NUMBER_DBL) {
            opts.start = ts_value_get_int(startVal);
            // If start is specified, don't truncate the file
            openFlags = O_WRONLY | O_CREAT;
        }

        TsValue* autoCloseVal = ts_object_get_property(rawOpts, "autoClose");
        TsValue acDec = nanbox_to_tagged(autoCloseVal);
        if (acDec.type == ValueType::BOOLEAN) {
            opts.autoClose = ts_value_get_bool(autoCloseVal);
        }

        TsValue* flagsVal = ts_object_get_property(rawOpts, "flags");
        TsValue flagsDec = nanbox_to_tagged(flagsVal);
        if (flagsDec.type == ValueType::STRING_PTR) {
            TsString* flagsStr = (TsString*)flagsDec.ptr_val;
            if (flagsStr) {
                const char* flagsCStr = flagsStr->ToUtf8();
                if (strcmp(flagsCStr, "a") == 0) {
                    openFlags = O_WRONLY | O_CREAT | O_APPEND;
                } else if (strcmp(flagsCStr, "ax") == 0) {
                    openFlags = O_WRONLY | O_CREAT | O_APPEND | O_EXCL;
                } else if (strcmp(flagsCStr, "w") == 0) {
                    openFlags = O_WRONLY | O_CREAT | O_TRUNC;
                } else if (strcmp(flagsCStr, "wx") == 0) {
                    openFlags = O_WRONLY | O_CREAT | O_TRUNC | O_EXCL;
                }
            }
        }
    }

    uv_fs_t open_req;
    int fd = uv_fs_open(uv_default_loop(), &open_req, p->ToUtf8(), openFlags, 0644, NULL);
    uv_fs_req_cleanup(&open_req);

    if (fd < 0) {
        return nullptr;
    }

    void* mem = ts_alloc(sizeof(TsWriteStream));
    TsWriteStream* stream = new(mem) TsWriteStream(fd, opts);
    stream->SetPath(p->ToUtf8());
    return stream;
}

bool ts_fs_write_stream_write(void* stream, void* buffer) {
    if (!stream || !buffer) return false;
    TsWriteStream* s = (TsWriteStream*)stream;
    TsBuffer* b = (TsBuffer*)ts_value_get_object((TsValue*)buffer);
    if (!b) b = (TsBuffer*)buffer;
    return s->Write(b->GetData(), b->GetLength());
}

void ts_fs_write_stream_end(void* stream) {
    if (!stream) return;
    TsWriteStream* s = (TsWriteStream*)stream;
    s->End();
}

// WriteStream property accessors
int64_t ts_write_stream_bytes_written(void* stream) {
    if (!stream) return 0;
    void* rawPtr = ts_nanbox_safe_unbox(stream);
    TsWriteStream* ws = dynamic_cast<TsWriteStream*>((TsEventEmitter*)rawPtr);
    if (!ws) return 0;
    return ws->GetBytesWritten();
}

void* ts_write_stream_path(void* stream) {
    if (!stream) return nullptr;
    void* rawPtr = ts_nanbox_safe_unbox(stream);
    TsWriteStream* ws = dynamic_cast<TsWriteStream*>((TsEventEmitter*)rawPtr);
    if (!ws) return nullptr;
    const char* path = ws->GetPath();
    if (!path) return nullptr;
    return TsString::Create(path);
}

bool ts_write_stream_pending(void* stream) {
    if (!stream) return true;
    void* rawPtr = ts_nanbox_safe_unbox(stream);
    TsWriteStream* ws = dynamic_cast<TsWriteStream*>((TsEventEmitter*)rawPtr);
    if (!ws) return true;
    return ws->IsPending();
}

// =============================================================================
// Readable stream interface
// =============================================================================

bool ts_readable_destroyed(void* stream) {
    if (!stream) return true;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (!r) return true;
    return r->IsDestroyed();
}

bool ts_readable_readable(void* stream) {
    if (!stream) return false;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (!r) return false;
    return r->IsReadable();
}

bool ts_readable_is_paused(void* stream) {
    if (!stream) return true;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (!r) return true;
    return r->IsPaused();
}

bool ts_readable_readable_ended(void* stream) {
    if (!stream) return true;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (!r) return true;
    return r->IsReadableEnded();
}

bool ts_readable_readable_flowing(void* stream) {
    if (!stream) return false;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (!r) return false;
    return r->IsReadableFlowing();
}

void ts_readable_unpipe(void* stream) {
    if (!stream) return;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (r) r->Unpipe();
}

int64_t ts_readable_readable_high_water_mark(void* stream) {
    if (!stream) return 16384;  // Default 16KB
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (!r) return 16384;
    return r->GetHighWaterMark();
}

int64_t ts_readable_readable_length(void* stream) {
    if (!stream) return 0;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (!r) return 0;
    return r->GetReadableLength();
}

bool ts_readable_readable_object_mode(void* stream) {
    if (!stream) return false;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (!r) return false;
    return r->IsObjectMode();
}

bool ts_readable_readable_aborted(void* stream) {
    if (!stream) return false;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (!r) return false;
    return r->IsReadableAborted();
}

bool ts_readable_readable_did_read(void* stream) {
    if (!stream) return false;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (!r) return false;
    return r->IsReadableDidRead();
}

void* ts_readable_readable_encoding(void* stream) {
    if (!stream) return nullptr;

    void* rawStream = ts_nanbox_safe_unbox(stream);

    TsReadable* r = dynamic_cast<TsReadable*>((TsEventEmitter*)rawStream);
    if (!r) return nullptr;

    const char* encoding = r->GetEncoding();
    if (!encoding) return nullptr;

    return TsString::Create(encoding);
}

void* ts_readable_set_encoding(void* stream, void* encoding) {
    if (!stream) return nullptr;

    void* rawStream = ts_nanbox_safe_unbox(stream);

    TsReadable* r = dynamic_cast<TsReadable*>((TsEventEmitter*)rawStream);
    if (!r) return stream;

    TsString* encStr = nullptr;
    void* rawEnc = ts_value_get_string((TsValue*)encoding);
    if (rawEnc) {
        encStr = (TsString*)rawEnc;
    } else if (encoding) {
        encStr = (TsString*)encoding;
    }

    if (encStr) {
        r->SetEncoding(encStr->ToUtf8());
    }

    return stream;
}

void ts_readable_unshift(void* stream, void* chunk) {
    if (!stream || !chunk) return;

    void* rawStream = ts_nanbox_safe_unbox(stream);

    TsReadable* r = dynamic_cast<TsReadable*>((TsEventEmitter*)rawStream);
    if (!r) return;

    r->Unshift(chunk);
}

void* ts_readable_read(void* stream, int64_t size) {
    if (!stream) return nullptr;

    void* rawStream = ts_nanbox_safe_unbox(stream);

    TsReadable* r = dynamic_cast<TsReadable*>((TsEventEmitter*)rawStream);
    if (!r) r = ((TsObject*)rawStream)->AsReadable();
    if (!r) return nullptr;

    r->SetReadableDidRead(true);

    if (!r->HasUnshiftedData()) {
        return nullptr;
    }

    if (size == 0) {
        return nullptr;
    }

    void* chunk = r->GetUnshiftedChunk();
    return chunk;
}

// =============================================================================
// Writable stream interface
// =============================================================================

bool ts_writable_write(void* writable, void* val) {
    if (!writable || !val) return false;

    TsValue vd = nanbox_to_tagged((TsValue*)val);
    TsEventEmitter* emitter = (TsEventEmitter*)writable;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) {
        w = (TsWritable*)writable;
    }

    if (vd.type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)vd.ptr_val;
        return w->Write((void*)str->ToUtf8(), str->Length());
    } else if (vd.type == ValueType::OBJECT_PTR) {
        TsObject* obj = (TsObject*)vd.ptr_val;
        if (obj && *(uint32_t*)obj == 0x42554646) { // TsBuffer::MAGIC
            TsBuffer* buf = (TsBuffer*)obj;
            return w->Write(buf->GetData(), buf->GetLength());
        }
    }
    return false;
}

TS_NOINLINE void ts_writable_end(void* writable, void* dataPtr) {
    if (!writable) {
        return;
    }

    TsValue dataDec = nanbox_to_tagged((TsValue*)dataPtr);
    TsEventEmitter* emitter = (TsEventEmitter*)writable;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) {
        w = (TsWritable*)writable;
    }

    if (dataPtr && dataDec.type != ValueType::UNDEFINED) {
        ts_writable_write(writable, dataPtr);
    }
    w->End();
}

bool ts_writable_destroyed(void* stream) {
    if (!stream) return true;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) return true;
    return w->IsDestroyed();
}

bool ts_writable_writable(void* stream) {
    if (!stream) return false;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) return false;
    return w->IsWritable();
}

bool ts_writable_writable_ended(void* stream) {
    if (!stream) return true;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) return true;
    return w->IsWritableEnded();
}

bool ts_writable_writable_finished(void* stream) {
    if (!stream) return true;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) return true;
    return w->IsWritableFinished();
}

bool ts_writable_writable_need_drain(void* stream) {
    if (!stream) return false;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) return false;
    return w->IsWritableNeedDrain();
}

int64_t ts_writable_writable_high_water_mark(void* stream) {
    if (!stream) return 0;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) return 0;
    return (int64_t)w->GetWritableHighWaterMark();
}

int64_t ts_writable_writable_length(void* stream) {
    if (!stream) return 0;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) return 0;
    return (int64_t)w->GetWritableLength();
}

bool ts_writable_writable_object_mode(void* stream) {
    if (!stream) return false;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) return false;
    return w->IsObjectMode();
}

void ts_writable_cork(void* stream) {
    if (!stream) return;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (w) w->Cork();
}

void ts_writable_uncork(void* stream) {
    if (!stream) return;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (w) w->Uncork();
}

bool ts_writable_writable_aborted(void* stream) {
    if (!stream) return false;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsWritable* w = dynamic_cast<TsWritable*>(emitter);
    if (!w) return false;
    return w->IsWritableAborted();
}

void* ts_writable_set_default_encoding(void* stream, void* encoding) {
    if (!stream) return nullptr;

    void* rawStream = ts_nanbox_safe_unbox(stream);

    TsWritable* w = dynamic_cast<TsWritable*>((TsEventEmitter*)rawStream);
    if (!w) return stream;

    TsString* encStr = nullptr;
    void* rawEnc = ts_value_get_string((TsValue*)encoding);
    if (rawEnc) {
        encStr = (TsString*)rawEnc;
    } else if (encoding) {
        encStr = (TsString*)encoding;
    }

    if (encStr) {
        w->SetDefaultEncoding(encStr->ToUtf8());
    }

    return stream;
}

// =============================================================================
// Stream utilities (pipe, pipeline, finished)
// =============================================================================

struct PipeContext {
    void* dest;
};

TsValue* pipe_data_cb(void* context, int argc, TsValue** argv) {
    PipeContext* ctx = (PipeContext*)context;
    if (argc > 0) {
        ts_fs_write_stream_write(ctx->dest, argv[0]);
    }
    return ts_value_make_undefined();
}

TsValue* pipe_end_cb(void* context, int argc, TsValue** argv) {
    PipeContext* ctx = (PipeContext*)context;
    ts_fs_write_stream_end(ctx->dest);
    return ts_value_make_undefined();
}

void* ts_stream_pipe(void* src, void* dest) {
    TsEventEmitter* s = (TsEventEmitter*)src;
    if (!s || s->magic != TsEventEmitter::MAGIC) return dest;

    PipeContext* ctx = (PipeContext*)ts_alloc(sizeof(PipeContext));
    ctx->dest = dest;

    TsValue* dataCb = ts_value_make_native_function((void*)pipe_data_cb, ctx);
    TsValue* endCb = ts_value_make_native_function((void*)pipe_end_cb, ctx);

    s->On("data", dataCb);
    s->On("end", endCb);

    return dest;
}

void ts_stream_pause(void* stream) {
    if (!stream) return;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (r) r->Pause();
}

void ts_stream_resume(void* stream) {
    if (!stream) return;
    TsEventEmitter* emitter = (TsEventEmitter*)stream;
    TsReadable* r = dynamic_cast<TsReadable*>(emitter);
    if (r) r->Resume();
}

// --- stream.pipeline() implementation ---
struct PipelineContext {
    TsValue* callback;
    int streamCount;
    bool errorOccurred;
    TsEventEmitter** streams;
};

static void pipeline_cleanup(PipelineContext* ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->streamCount; i++) {
        if (ctx->streams[i]) {
            TsReadable* r = dynamic_cast<TsReadable*>(ctx->streams[i]);
            if (r) r->Destroy();
            TsWritable* w = dynamic_cast<TsWritable*>(ctx->streams[i]);
            if (w) w->Destroy();
        }
    }
}

TsValue* pipeline_error_cb(void* context, int argc, TsValue** argv) {
    PipelineContext* ctx = (PipelineContext*)context;
    if (ctx->errorOccurred) return ts_value_make_undefined();
    ctx->errorOccurred = true;

    pipeline_cleanup(ctx);

    if (ctx->callback) {
        ts_function_call(ctx->callback, argc, argv);
    }
    return ts_value_make_undefined();
}

TsValue* pipeline_finish_cb(void* context, int argc, TsValue** argv) {
    PipelineContext* ctx = (PipelineContext*)context;
    if (ctx->errorOccurred) return ts_value_make_undefined();

    if (ctx->callback) {
        TsValue* args[] = { nullptr };
        ts_function_call(ctx->callback, 0, args);
    }
    return ts_value_make_undefined();
}

static TsEventEmitter* unbox_stream(void* arg) {
    if (!arg) return nullptr;
    void* rawPtr = ts_nanbox_safe_unbox(arg);
    TsEventEmitter* ee = dynamic_cast<TsEventEmitter*>((TsObject*)rawPtr);
    if (!ee) ee = ((TsObject*)rawPtr)->AsEventEmitter();
    return ee;
}

// 3-arg version: pipeline(source, destination, callback)
void* ts_stream_pipeline_3(void* source, void* dest, void* callback) {
    if (!source || !dest) return nullptr;

    PipelineContext* ctx = (PipelineContext*)ts_alloc(sizeof(PipelineContext));
    ctx->callback = (TsValue*)callback;
    ctx->streamCount = 2;
    ctx->errorOccurred = false;
    ctx->streams = (TsEventEmitter**)ts_alloc(sizeof(TsEventEmitter*) * 2);
    ctx->streams[0] = unbox_stream(source);
    ctx->streams[1] = unbox_stream(dest);

    if (!ctx->streams[0] || !ctx->streams[1]) return nullptr;

    TsValue* errorCb = ts_value_make_native_function((void*)pipeline_error_cb, ctx);
    TsValue* finishCb = ts_value_make_native_function((void*)pipeline_finish_cb, ctx);

    ctx->streams[0]->On("error", errorCb);
    ctx->streams[1]->On("error", errorCb);

    ts_stream_pipe(ctx->streams[0], ctx->streams[1]);

    ctx->streams[1]->On("finish", finishCb);
    ctx->streams[1]->On("close", finishCb);

    return dest;
}

void* ts_stream_pipeline(void* streams, void* callback) {
    if (!streams) return nullptr;

    TsArray* arr = (TsArray*)streams;
    int count = (int)arr->Length();
    if (count < 2) return nullptr;

    PipelineContext* ctx = (PipelineContext*)ts_alloc(sizeof(PipelineContext));
    ctx->callback = (TsValue*)callback;
    ctx->streamCount = count;
    ctx->errorOccurred = false;
    ctx->streams = (TsEventEmitter**)ts_alloc(sizeof(TsEventEmitter*) * count);

    for (int i = 0; i < count; i++) {
        TsValue* streamVal = (TsValue*)arr->Get(i);
        void* rawPtr = ts_value_get_object(streamVal);
        if (!rawPtr) rawPtr = (void*)streamVal;
        ctx->streams[i] = dynamic_cast<TsEventEmitter*>((TsObject*)rawPtr);
        if (!ctx->streams[i]) {
            ctx->streams[i] = ((TsObject*)rawPtr)->AsEventEmitter();
        }
    }

    TsValue* errorCb = ts_value_make_native_function((void*)pipeline_error_cb, ctx);
    TsValue* finishCb = ts_value_make_native_function((void*)pipeline_finish_cb, ctx);

    for (int i = 0; i < count - 1; i++) {
        TsEventEmitter* src = ctx->streams[i];
        TsEventEmitter* dest = ctx->streams[i + 1];

        if (!src || !dest) continue;

        src->On("error", errorCb);
        dest->On("error", errorCb);

        ts_stream_pipe(src, dest);
    }

    TsEventEmitter* lastStream = ctx->streams[count - 1];
    if (lastStream) {
        lastStream->On("finish", finishCb);
        lastStream->On("close", finishCb);
    }

    TsValue* lastVal = (TsValue*)arr->Get(count - 1);
    return ts_value_get_object(lastVal) ? ts_value_get_object(lastVal) : lastVal;
}

// --- stream.finished() implementation ---
struct FinishedContext {
    TsValue* callback;
    TsEventEmitter* stream;
    bool called;
    TsValue* errorListener;
    TsValue* closeListener;
    TsValue* endListener;
    TsValue* finishListener;
};

static void finished_call_callback(FinishedContext* ctx, TsValue* err) {
    if (ctx->called) return;
    ctx->called = true;

    if (ctx->callback) {
        TsValue* args[] = { err };
        ts_function_call(ctx->callback, err ? 1 : 0, args);
    }
}

TsValue* finished_error_cb(void* context, int argc, TsValue** argv) {
    FinishedContext* ctx = (FinishedContext*)context;
    TsValue* err = argc > 0 ? argv[0] : nullptr;
    finished_call_callback(ctx, err);
    return ts_value_make_undefined();
}

TsValue* finished_close_cb(void* context, int argc, TsValue** argv) {
    FinishedContext* ctx = (FinishedContext*)context;
    finished_call_callback(ctx, nullptr);
    return ts_value_make_undefined();
}

TsValue* finished_end_cb(void* context, int argc, TsValue** argv) {
    FinishedContext* ctx = (FinishedContext*)context;
    finished_call_callback(ctx, nullptr);
    return ts_value_make_undefined();
}

TsValue* finished_finish_cb(void* context, int argc, TsValue** argv) {
    FinishedContext* ctx = (FinishedContext*)context;
    finished_call_callback(ctx, nullptr);
    return ts_value_make_undefined();
}

TsValue* finished_cleanup_fn(void* context, int argc, TsValue** argv) {
    FinishedContext* ctx = (FinishedContext*)context;
    if (!ctx || !ctx->stream) return ts_value_make_undefined();

    if (ctx->errorListener)
        ctx->stream->RemoveListener("error", ctx->errorListener);
    if (ctx->closeListener)
        ctx->stream->RemoveListener("close", ctx->closeListener);
    if (ctx->endListener)
        ctx->stream->RemoveListener("end", ctx->endListener);
    if (ctx->finishListener)
        ctx->stream->RemoveListener("finish", ctx->finishListener);

    return ts_value_make_undefined();
}

void* ts_stream_finished(void* stream, void* optionsOrCallback, void* callback) {
    if (!stream) return ts_value_make_native_function((void*)finished_cleanup_fn, nullptr);

    void* rawPtr = ts_nanbox_safe_unbox(stream);

    TsEventEmitter* emitter = dynamic_cast<TsEventEmitter*>((TsObject*)rawPtr);
    if (!emitter) {
        emitter = ((TsObject*)rawPtr)->AsEventEmitter();
    }
    if (!emitter) return ts_value_make_native_function((void*)finished_cleanup_fn, nullptr);

    TsValue* cb = (TsValue*)callback;
    if (!cb || cb->type == ValueType::UNDEFINED) {
        cb = (TsValue*)optionsOrCallback;
    }

    FinishedContext* ctx = (FinishedContext*)ts_alloc(sizeof(FinishedContext));
    ctx->callback = cb;
    ctx->stream = emitter;
    ctx->called = false;

    ctx->errorListener = ts_value_make_native_function((void*)finished_error_cb, ctx);
    ctx->closeListener = ts_value_make_native_function((void*)finished_close_cb, ctx);
    ctx->endListener = ts_value_make_native_function((void*)finished_end_cb, ctx);
    ctx->finishListener = ts_value_make_native_function((void*)finished_finish_cb, ctx);

    TsReadable* readable = dynamic_cast<TsReadable*>(emitter);
    TsWritable* writable = dynamic_cast<TsWritable*>(emitter);

    if (readable && readable->IsDestroyed()) {
        finished_call_callback(ctx, nullptr);
    } else if (writable && writable->IsDestroyed()) {
        finished_call_callback(ctx, nullptr);
    } else {
        emitter->Once("error", ctx->errorListener);
        emitter->Once("close", ctx->closeListener);

        if (readable) {
            emitter->Once("end", ctx->endListener);
        }
        if (writable) {
            emitter->Once("finish", ctx->finishListener);
        }
    }

    return ts_value_make_native_function((void*)finished_cleanup_fn, ctx);
}

// =============================================================================
// Readable.from() implementation
// =============================================================================

class TsArrayReadable : public TsReadable {
public:
    TsArrayReadable(TsArray* source) : TsReadable(), source_(source), index_(0), started_(false) {}

    virtual TsReadable* AsReadable() override { return this; }

    void Start() {
        if (started_ || !source_) return;
        started_ = true;
        EmitNextChunk();
    }

    void EmitNextChunk() {
        if (destroyed_ || ended_) return;

        if (index_ < source_->Length()) {
            TsValue* chunk = (TsValue*)source_->Get(index_);
            index_++;

            void* args[] = { chunk };
            Emit("data", 1, args);

            if (flowing && !destroyed_ && !ended_) {
                uv_idle_t* idle = (uv_idle_t*)ts_alloc(sizeof(uv_idle_t));
                idle->data = this;
                uv_idle_init(uv_default_loop(), idle);
                uv_idle_start(idle, [](uv_idle_t* handle) {
                    TsArrayReadable* self = (TsArrayReadable*)handle->data;
                    uv_idle_stop(handle);
                    uv_close((uv_handle_t*)handle, nullptr);
                    self->EmitNextChunk();
                });
            }
        } else {
            ended_ = true;
            Emit("end", 0, nullptr);
        }
    }

    virtual void On(const char* event, void* callback) override {
        TsEventEmitter::On(event, callback);
        if (strcmp(event, "data") == 0) {
            Resume();
            Start();
        }
    }

    virtual void Resume() override {
        TsReadable::Resume();
        if (started_ && !ended_) {
            EmitNextChunk();
        }
    }

private:
    TsArray* source_;
    int64_t index_;
    bool started_;
};

void* ts_readable_from(void* iterable) {
    if (!iterable) return nullptr;

    void* rawPtr = ts_nanbox_safe_unbox(iterable);

    uint32_t magic = *(uint32_t*)rawPtr;
    if (magic != TsArray::MAGIC) {
        return nullptr;
    }

    TsArray* arr = (TsArray*)rawPtr;

    void* mem = ts_alloc(sizeof(TsArrayReadable));
    TsArrayReadable* stream = new(mem) TsArrayReadable(arr);

    return stream;
}

// =============================================================================
// readable.wrap() implementation
// =============================================================================

struct WrapContext {
    TsReadable* readable;
    TsEventEmitter* oldStream;
    TsValue* dataListener;
    TsValue* endListener;
    TsValue* errorListener;
    TsValue* closeListener;
};

TsValue* wrap_data_cb(void* context, int argc, TsValue** argv) {
    WrapContext* ctx = (WrapContext*)context;
    if (!ctx || !ctx->readable) return ts_value_make_undefined();

    if (argc > 0 && argv[0]) {
        ctx->readable->Unshift(argv[0]);
        void* args[] = { argv[0] };
        ctx->readable->Emit("data", 1, args);
    }
    return ts_value_make_undefined();
}

TsValue* wrap_end_cb(void* context, int argc, TsValue** argv) {
    WrapContext* ctx = (WrapContext*)context;
    if (!ctx || !ctx->readable) return ts_value_make_undefined();

    ctx->readable->Emit("end", 0, nullptr);
    return ts_value_make_undefined();
}

TsValue* wrap_error_cb(void* context, int argc, TsValue** argv) {
    WrapContext* ctx = (WrapContext*)context;
    if (!ctx || !ctx->readable) return ts_value_make_undefined();

    ctx->readable->Emit("error", argc, (void**)argv);
    return ts_value_make_undefined();
}

TsValue* wrap_close_cb(void* context, int argc, TsValue** argv) {
    WrapContext* ctx = (WrapContext*)context;
    if (!ctx || !ctx->readable) return ts_value_make_undefined();

    ctx->readable->Emit("close", 0, nullptr);
    return ts_value_make_undefined();
}

void* ts_readable_wrap(void* readable, void* oldStream) {
    if (!readable || !oldStream) return readable;

    void* rawReadable = ts_nanbox_safe_unbox(readable);

    TsReadable* r = dynamic_cast<TsReadable*>((TsEventEmitter*)rawReadable);
    if (!r) r = ((TsObject*)rawReadable)->AsReadable();
    if (!r) return readable;

    void* rawOldStream = ts_nanbox_safe_unbox(oldStream);

    TsEventEmitter* oldEmitter = dynamic_cast<TsEventEmitter*>((TsObject*)rawOldStream);
    if (!oldEmitter) oldEmitter = ((TsObject*)rawOldStream)->AsEventEmitter();
    if (!oldEmitter) return readable;

    WrapContext* ctx = (WrapContext*)ts_alloc(sizeof(WrapContext));
    ctx->readable = r;
    ctx->oldStream = oldEmitter;

    ctx->dataListener = ts_value_make_native_function((void*)wrap_data_cb, ctx);
    ctx->endListener = ts_value_make_native_function((void*)wrap_end_cb, ctx);
    ctx->errorListener = ts_value_make_native_function((void*)wrap_error_cb, ctx);
    ctx->closeListener = ts_value_make_native_function((void*)wrap_close_cb, ctx);

    oldEmitter->On("data", ctx->dataListener);
    oldEmitter->On("end", ctx->endListener);
    oldEmitter->On("error", ctx->errorListener);
    oldEmitter->On("close", ctx->closeListener);

    return readable;
}

// =============================================================================
// Transform stream
// =============================================================================

void* ts_stream_transform_create(void* options) {
    void* mem = ts_alloc(sizeof(TsTransform));
    TsTransform* transform = new (mem) TsTransform();
    return transform;
}

void ts_stream_transform_push(void* transformPtr, void* data) {
    void* rawPtr = ts_nanbox_safe_unbox(transformPtr);

    TsTransform* transform = dynamic_cast<TsTransform*>((TsObject*)rawPtr);
    if (!transform) return;

    transform->Push(data);
}

void ts_stream_transform_push_null(void* transformPtr) {
    void* rawPtr = ts_nanbox_safe_unbox(transformPtr);

    TsTransform* transform = dynamic_cast<TsTransform*>((TsObject*)rawPtr);
    if (!transform) return;

    transform->PushNull();
}

void ts_stream_transform_set_transform(void* transformPtr, void* callback) {
    void* rawPtr = ts_nanbox_safe_unbox(transformPtr);

    TsTransform* transform = dynamic_cast<TsTransform*>((TsObject*)rawPtr);
    if (!transform) return;

    transform->SetTransformCallback(callback);
}

void ts_stream_transform_set_flush(void* transformPtr, void* callback) {
    void* rawPtr = ts_nanbox_safe_unbox(transformPtr);

    TsTransform* transform = dynamic_cast<TsTransform*>((TsObject*)rawPtr);
    if (!transform) return;

    transform->SetFlushCallback(callback);
}

} // extern "C"
