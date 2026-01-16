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

    // --- stream.Readable.from() implementation ---
    // Creates a readable stream from an iterable (array)

    class TsArrayReadable : public TsReadable {
    public:
        TsArrayReadable(TsArray* source) : TsReadable(), source_(source), index_(0), started_(false) {}

        virtual TsReadable* AsReadable() override { return this; }

        void Start() {
            if (started_ || !source_) return;
            started_ = true;

            // Schedule data emission on next tick
            EmitNextChunk();
        }

        void EmitNextChunk() {
            if (destroyed_ || ended_) return;

            if (index_ < source_->Length()) {
                // Emit 'data' event with current element
                TsValue* chunk = (TsValue*)source_->Get(index_);
                index_++;

                void* args[] = { chunk };
                Emit("data", 1, args);

                // Schedule next chunk emission if flowing
                if (flowing && !destroyed_ && !ended_) {
                    // Use setImmediate to emit next chunk asynchronously
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
                // End of array
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

        // Unbox if needed
        void* rawPtr = ts_value_get_object((TsValue*)iterable);
        if (!rawPtr) rawPtr = iterable;

        // Check if this is a TsArray by checking magic number
        // TsArray has magic at offset 8 (after vtable pointer - but TsArray has no vtable!)
        // TsArray memory layout: magic(4 bytes) + padding + other fields
        uint32_t magic = *(uint32_t*)rawPtr;
        if (magic != TsArray::MAGIC) {
            return nullptr;
        }

        TsArray* arr = (TsArray*)rawPtr;

        // Create the readable stream
        void* mem = ts_alloc(sizeof(TsArrayReadable));
        TsArrayReadable* stream = new(mem) TsArrayReadable(arr);

        return stream;
    }

    void* ts_readable_set_encoding(void* stream, void* encoding) {
        if (!stream) return nullptr;

        // Unbox stream if needed
        void* rawStream = ts_value_get_object((TsValue*)stream);
        if (!rawStream) rawStream = stream;

        TsReadable* r = dynamic_cast<TsReadable*>((TsEventEmitter*)rawStream);
        if (!r) return stream;  // Return original for chaining

        // Get encoding string
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

        return stream;  // Return for chaining
    }

    void* ts_readable_readable_encoding(void* stream) {
        if (!stream) return nullptr;

        // Unbox stream if needed
        void* rawStream = ts_value_get_object((TsValue*)stream);
        if (!rawStream) rawStream = stream;

        TsReadable* r = dynamic_cast<TsReadable*>((TsEventEmitter*)rawStream);
        if (!r) return nullptr;

        const char* encoding = r->GetEncoding();
        if (!encoding) return nullptr;

        return TsString::Create(encoding);
    }

    void ts_readable_unshift(void* stream, void* chunk) {
        if (!stream || !chunk) return;

        // Unbox stream if needed
        void* rawStream = ts_value_get_object((TsValue*)stream);
        if (!rawStream) rawStream = stream;

        TsReadable* r = dynamic_cast<TsReadable*>((TsEventEmitter*)rawStream);
        if (!r) return;

        r->Unshift(chunk);
    }

    // --- readable.wrap() implementation ---
    // Wraps an old-style stream (with 'data' and 'end' events) into a new Readable
    // The wrapped stream will emit data through the new Readable

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

        // Push data from old stream to new readable
        if (argc > 0 && argv[0]) {
            ctx->readable->Unshift(argv[0]);
            // Emit 'data' event on the new readable
            void* args[] = { argv[0] };
            ctx->readable->Emit("data", 1, args);
        }
        return ts_value_make_undefined();
    }

    TsValue* wrap_end_cb(void* context, int argc, TsValue** argv) {
        WrapContext* ctx = (WrapContext*)context;
        if (!ctx || !ctx->readable) return ts_value_make_undefined();

        // Emit 'end' event on the new readable
        ctx->readable->Emit("end", 0, nullptr);
        return ts_value_make_undefined();
    }

    TsValue* wrap_error_cb(void* context, int argc, TsValue** argv) {
        WrapContext* ctx = (WrapContext*)context;
        if (!ctx || !ctx->readable) return ts_value_make_undefined();

        // Forward error to the new readable
        ctx->readable->Emit("error", argc, (void**)argv);
        return ts_value_make_undefined();
    }

    TsValue* wrap_close_cb(void* context, int argc, TsValue** argv) {
        WrapContext* ctx = (WrapContext*)context;
        if (!ctx || !ctx->readable) return ts_value_make_undefined();

        // Emit 'close' event on the new readable
        ctx->readable->Emit("close", 0, nullptr);
        return ts_value_make_undefined();
    }

    void* ts_readable_wrap(void* readable, void* oldStream) {
        if (!readable || !oldStream) return readable;

        // Unbox readable if needed
        void* rawReadable = ts_value_get_object((TsValue*)readable);
        if (!rawReadable) rawReadable = readable;

        TsReadable* r = dynamic_cast<TsReadable*>((TsEventEmitter*)rawReadable);
        if (!r) r = ((TsObject*)rawReadable)->AsReadable();
        if (!r) return readable;

        // Unbox old stream if needed
        void* rawOldStream = ts_value_get_object((TsValue*)oldStream);
        if (!rawOldStream) rawOldStream = oldStream;

        TsEventEmitter* oldEmitter = dynamic_cast<TsEventEmitter*>((TsObject*)rawOldStream);
        if (!oldEmitter) oldEmitter = ((TsObject*)rawOldStream)->AsEventEmitter();
        if (!oldEmitter) return readable;

        // Create wrap context
        WrapContext* ctx = (WrapContext*)ts_alloc(sizeof(WrapContext));
        ctx->readable = r;
        ctx->oldStream = oldEmitter;

        // Create event listeners
        ctx->dataListener = ts_value_make_native_function((void*)wrap_data_cb, ctx);
        ctx->endListener = ts_value_make_native_function((void*)wrap_end_cb, ctx);
        ctx->errorListener = ts_value_make_native_function((void*)wrap_error_cb, ctx);
        ctx->closeListener = ts_value_make_native_function((void*)wrap_close_cb, ctx);

        // Listen to events on the old stream
        oldEmitter->On("data", ctx->dataListener);
        oldEmitter->On("end", ctx->endListener);
        oldEmitter->On("error", ctx->errorListener);
        oldEmitter->On("close", ctx->closeListener);

        // Return the new readable stream for chaining
        return readable;
    }
}
