#include "TsReadStream.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "GC.h"
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <new>

TsReadStream::TsReadStream(int fd) : fd(fd), closed(false) {
    buffer = TsBuffer::Create(64 * 1024); // 64KB buffer
}

TsReadStream::~TsReadStream() {
    if (!closed) {
        Close();
    }
}

void TsReadStream::Start() {
    uv_fs_t* req = (uv_fs_t*)ts_alloc(sizeof(uv_fs_t));
    req->data = this;
    
    uv_buf_t buf = uv_buf_init((char*)buffer->GetData(), (unsigned int)buffer->GetLength());
    uv_fs_read(uv_default_loop(), req, fd, &buf, 1, -1, OnRead);
}

void TsReadStream::OnRead(uv_fs_t* req) {
    TsReadStream* self = (TsReadStream*)req->data;
    int result = (int)req->result;
    
    if (result > 0) {
        // Emit 'data' event
        TsBuffer* chunk = TsBuffer::Create(result);
        memcpy(chunk->GetData(), self->buffer->GetData(), result);
        
        TsValue* arg0 = ts_value_make_object(chunk);
        void* args[] = { arg0 };
        self->Emit("data", 1, args);
        
        // Read next chunk
        uv_fs_req_cleanup(req);
        uv_buf_t buf = uv_buf_init((char*)self->buffer->GetData(), (unsigned int)self->buffer->GetLength());
        uv_fs_read(uv_default_loop(), req, self->fd, &buf, 1, -1, OnRead);
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
        
        // Start on next tick to allow listeners to be attached
        uv_idle_t* idle = (uv_idle_t*)ts_alloc(sizeof(uv_idle_t));
        uv_idle_init(uv_default_loop(), idle);
        idle->data = stream;
        uv_idle_start(idle, [](uv_idle_t* handle) {
            TsReadStream* s = (TsReadStream*)handle->data;
            s->Start();
            uv_idle_stop(handle);
            uv_close((uv_handle_t*)handle, nullptr);
        });

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
}
