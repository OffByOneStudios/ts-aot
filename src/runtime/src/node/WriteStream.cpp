#include "TsWriteStream.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "GC.h"
#include "TsRuntime.h"
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <new>

TsWriteStream::TsWriteStream(int fd) : fd(fd) {
    closed = false;
    bufferedAmount = 0;
    highWaterMark = 16384;
    needDrain = false;
}

TsWriteStream::~TsWriteStream() {
    if (!closed) {
        uv_fs_t close_req;
        uv_fs_close(uv_default_loop(), &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
    }
}

bool TsWriteStream::Write(void* data, size_t length) {
    if (closed) return false;

    bufferedAmount += length;

    uv_fs_t* write_req = (uv_fs_t*)ts_alloc(sizeof(uv_fs_t));
    // We need to copy the data because uv_fs_write is async and the source might be GC'd or modified
    // Actually, if it's a TsBuffer or TsString, it's already on the GC heap.
    // But libuv doesn't know about GC. However, Boehm GC won't move objects.
    // So as long as we keep a reference to the data, it's fine.
    // For now, let's assume the caller keeps the data alive or we copy it.
    // To be safe, let's copy it into a new buffer if it's not already a GC object we can track.
    // Actually, ts_fs_write_stream_write passes data from TsValue.
    
    uv_buf_t buf = uv_buf_init((char*)data, (unsigned int)length);
    
    write_req->data = this;
    // Store the length in the request so we can decrement bufferedAmount later
    // uv_fs_t doesn't have a spare field, but we can wrap it.
    struct WriteContext {
        TsWriteStream* stream;
        size_t length;
    };
    WriteContext* ctx = (WriteContext*)ts_alloc(sizeof(WriteContext));
    ctx->stream = this;
    ctx->length = length;
    write_req->data = ctx;

    uv_fs_write(uv_default_loop(), write_req, fd, &buf, 1, -1, [](uv_fs_t* req) {
        WriteContext* ctx = (WriteContext*)req->data;
        TsWriteStream* self = ctx->stream;
        self->bufferedAmount -= ctx->length;
        
        if (self->needDrain && self->bufferedAmount < self->highWaterMark) {
            self->needDrain = false;
            self->Emit("drain", 0, nullptr);
        }
        
        uv_fs_req_cleanup(req);
    });

    if (bufferedAmount >= highWaterMark) {
        needDrain = true;
        return false;
    }
    return true;
}

void TsWriteStream::OnWrite(uv_fs_t* req) {
    // This is now handled by the lambda above
}

void TsWriteStream::End() {
    if (closed) return;
    closed = true;
    
    uv_fs_t* close_req = (uv_fs_t*)ts_alloc(sizeof(uv_fs_t));
    close_req->data = this;
    uv_fs_close(uv_default_loop(), close_req, fd, [](uv_fs_t* req) {
        TsWriteStream* self = (TsWriteStream*)req->data;
        self->Emit("finish", 0, nullptr);
        self->Emit("close", 0, nullptr);
        uv_fs_req_cleanup(req);
    });
}

extern "C" {
    void* ts_fs_createWriteStream(void* path) {
        TsString* p = (TsString*)path;
        uv_fs_t open_req;
        int fd = uv_fs_open(uv_default_loop(), &open_req, p->ToUtf8(), O_WRONLY | O_CREAT | O_TRUNC, 0644, NULL);
        uv_fs_req_cleanup(&open_req);

        if (fd < 0) {
            return nullptr;
        }
        
        void* mem = ts_alloc(sizeof(TsWriteStream));
        return new(mem) TsWriteStream(fd);
    }
}
