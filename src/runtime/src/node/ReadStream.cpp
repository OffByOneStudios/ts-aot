#include "TsReadStream.h"
#include "TsReadable.h"
#include "TsWritable.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "GC.h"
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <new>

TsReadStream::TsReadStream(int fd) : fd(fd), closed(false) {
    flowing = false;
    reading = false;
    buffer = TsBuffer::Create(65536); // 64KB buffer
}

TsReadStream::~TsReadStream() {
    if (!closed) {
        Close();
    }
}

void TsReadStream::On(const char* event, void* callback) {
    TsEventEmitter::On(event, callback);
    if (strcmp(event, "data") == 0) {
        Resume();
    }
}

void TsReadStream::Pause() {
    flowing = false;
}

void TsReadStream::Resume() {
    if (!flowing) {
        flowing = true;
        if (!reading) {
            Start();
        }
    }
}

void TsReadStream::Start() {
    if (closed || reading || !flowing) return;
    reading = true;
    
    uv_fs_t* req = (uv_fs_t*)ts_alloc(sizeof(uv_fs_t));
    req->data = this;
    
    uv_buf_t buf = uv_buf_init((char*)buffer->GetData(), (unsigned int)buffer->GetLength());
    uv_fs_read(uv_default_loop(), req, fd, &buf, 1, -1, OnRead);
}

void TsReadStream::OnRead(uv_fs_t* req) {
    TsReadStream* self = (TsReadStream*)req->data;
    self->reading = false;
    int result = (int)req->result;
    
    if (result > 0) {
        // Emit 'data' event
        TsBuffer* chunk = TsBuffer::Create(result);
        memcpy(chunk->GetData(), self->buffer->GetData(), result);
        
        TsValue* arg0 = ts_value_make_object(chunk);
        void* args[] = { arg0 };
        self->Emit("data", 1, args);
        
        // Read next chunk if still flowing
        if (self->flowing && !self->closed) {
            self->Start();
        }
        uv_fs_req_cleanup(req);
    } else {
        // EOF or error
        if (result == 0) {
            self->Emit("end", 0, nullptr);
        }
        self->Close();
        uv_fs_req_cleanup(req);
    }
}

void TsReadStream::Close() {
    if (closed) return;
    closed = true;
    
    uv_fs_t req;
    uv_fs_close(uv_default_loop(), &req, fd, nullptr);
    uv_fs_req_cleanup(&req);
    
    Emit("close", 0, nullptr);
}

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
        
        return stream;
    }

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
        TsReadStream* s = (TsReadStream*)stream;
        s->Pause();
    }

    void ts_stream_resume(void* stream) {
        TsReadStream* s = (TsReadStream*)stream;
        s->Resume();
    }

    // Readable stream state property accessors
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

    // --- stream.pipeline() implementation ---
    // Pipeline connects multiple streams and handles errors
    struct PipelineContext {
        TsValue* callback;
        int streamCount;
        bool errorOccurred;
        TsEventEmitter** streams;
    };

    static void pipeline_cleanup(PipelineContext* ctx) {
        if (!ctx) return;
        // Destroy all streams on error
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

        // Call the callback with the error
        if (ctx->callback) {
            ts_function_call(ctx->callback, argc, argv);
        }
        return ts_value_make_undefined();
    }

    TsValue* pipeline_finish_cb(void* context, int argc, TsValue** argv) {
        PipelineContext* ctx = (PipelineContext*)context;
        if (ctx->errorOccurred) return ts_value_make_undefined();

        // Call the callback with no error (success)
        if (ctx->callback) {
            TsValue* args[] = { nullptr };
            ts_function_call(ctx->callback, 0, args);
        }
        return ts_value_make_undefined();
    }

    void* ts_stream_pipeline(void* streams, void* callback) {
        if (!streams) return nullptr;

        // Get the array of streams
        TsArray* arr = (TsArray*)streams;
        int count = (int)arr->Length();
        if (count < 2) return nullptr;  // Need at least source and destination

        // Create pipeline context
        PipelineContext* ctx = (PipelineContext*)ts_alloc(sizeof(PipelineContext));
        ctx->callback = (TsValue*)callback;
        ctx->streamCount = count;
        ctx->errorOccurred = false;
        ctx->streams = (TsEventEmitter**)ts_alloc(sizeof(TsEventEmitter*) * count);

        // Get raw stream pointers
        for (int i = 0; i < count; i++) {
            TsValue* streamVal = (TsValue*)arr->Get(i);
            void* rawPtr = ts_value_get_object(streamVal);
            if (!rawPtr) rawPtr = (void*)streamVal;
            ctx->streams[i] = dynamic_cast<TsEventEmitter*>((TsObject*)rawPtr);
            if (!ctx->streams[i]) {
                ctx->streams[i] = ((TsObject*)rawPtr)->AsEventEmitter();
            }
        }

        // Create error handler
        TsValue* errorCb = ts_value_make_native_function((void*)pipeline_error_cb, ctx);
        TsValue* finishCb = ts_value_make_native_function((void*)pipeline_finish_cb, ctx);

        // Pipe each stream to the next
        for (int i = 0; i < count - 1; i++) {
            TsEventEmitter* src = ctx->streams[i];
            TsEventEmitter* dest = ctx->streams[i + 1];

            if (!src || !dest) continue;

            // Listen for errors on each stream
            src->On("error", errorCb);
            dest->On("error", errorCb);

            // Pipe source to destination
            ts_stream_pipe(src, dest);
        }

        // Listen for finish on the last writable stream
        TsEventEmitter* lastStream = ctx->streams[count - 1];
        if (lastStream) {
            lastStream->On("finish", finishCb);
            lastStream->On("close", finishCb);
        }

        // Return the last stream (destination)
        TsValue* lastVal = (TsValue*)arr->Get(count - 1);
        return ts_value_get_object(lastVal) ? ts_value_get_object(lastVal) : lastVal;
    }

    // --- stream.finished() implementation ---
    // Detects when a stream is no longer readable, writable, or has experienced an error
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

    // Cleanup function returned by stream.finished()
    TsValue* finished_cleanup_fn(void* context, int argc, TsValue** argv) {
        FinishedContext* ctx = (FinishedContext*)context;
        if (!ctx || !ctx->stream) return ts_value_make_undefined();

        // Remove all our listeners
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

        // Get raw stream pointer
        void* rawPtr = ts_value_get_object((TsValue*)stream);
        if (!rawPtr) rawPtr = stream;

        TsEventEmitter* emitter = dynamic_cast<TsEventEmitter*>((TsObject*)rawPtr);
        if (!emitter) {
            emitter = ((TsObject*)rawPtr)->AsEventEmitter();
        }
        if (!emitter) return ts_value_make_native_function((void*)finished_cleanup_fn, nullptr);

        // Handle overloaded signature: finished(stream, callback) or finished(stream, options, callback)
        TsValue* cb = (TsValue*)callback;
        if (!cb || cb->type == ValueType::UNDEFINED) {
            cb = (TsValue*)optionsOrCallback;
        }

        // Create context
        FinishedContext* ctx = (FinishedContext*)ts_alloc(sizeof(FinishedContext));
        ctx->callback = cb;
        ctx->stream = emitter;
        ctx->called = false;

        // Create listeners
        ctx->errorListener = ts_value_make_native_function((void*)finished_error_cb, ctx);
        ctx->closeListener = ts_value_make_native_function((void*)finished_close_cb, ctx);
        ctx->endListener = ts_value_make_native_function((void*)finished_end_cb, ctx);
        ctx->finishListener = ts_value_make_native_function((void*)finished_finish_cb, ctx);

        // Check if stream is already finished
        TsReadable* readable = dynamic_cast<TsReadable*>(emitter);
        TsWritable* writable = dynamic_cast<TsWritable*>(emitter);

        if (readable && readable->IsDestroyed()) {
            // Stream already destroyed, call callback immediately
            finished_call_callback(ctx, nullptr);
        } else if (writable && writable->IsDestroyed()) {
            finished_call_callback(ctx, nullptr);
        } else {
            // Register listeners
            emitter->Once("error", ctx->errorListener);
            emitter->Once("close", ctx->closeListener);

            if (readable) {
                emitter->Once("end", ctx->endListener);
            }
            if (writable) {
                emitter->Once("finish", ctx->finishListener);
            }
        }

        // Return cleanup function
        return ts_value_make_native_function((void*)finished_cleanup_fn, ctx);
    }
}
