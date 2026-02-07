#include "TsWriteStream.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "GC.h"
#include "TsRuntime.h"
#include "TsObject.h"
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <new>

TsWriteStream::TsWriteStream(int fd, const WriteStreamOptions& opts)
    : fd(fd), position(opts.start), bytesWritten(0), path(nullptr),
      autoClose(opts.autoClose), started(false), closed(false) {
    bufferedAmount = 0;
    highWaterMark = 16384;
    needDrain = false;
}

TsWriteStream::~TsWriteStream() {
    if (!closed && autoClose) {
        uv_fs_t close_req;
        uv_fs_close(uv_default_loop(), &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
    }
}

void TsWriteStream::SetPath(const char* p) {
    if (p) {
        size_t len = strlen(p);
        path = (char*)ts_alloc(len + 1);
        memcpy(path, p, len + 1);
    } else {
        path = nullptr;
    }
}

bool TsWriteStream::Write(void* data, size_t length) {
    if (closed) return false;
    started = true;

    bufferedAmount += length;

    uv_fs_t* write_req = (uv_fs_t*)ts_alloc(sizeof(uv_fs_t));

    uv_buf_t buf = uv_buf_init((char*)data, (unsigned int)length);

    struct WriteContext {
        TsWriteStream* stream;
        size_t length;
    };
    WriteContext* ctx = (WriteContext*)ts_alloc(sizeof(WriteContext));
    ctx->stream = this;
    ctx->length = length;
    write_req->data = ctx;

    // Use position if set, otherwise append (-1)
    int64_t writePos = position;
    if (position >= 0) {
        position += length;  // Update position for next write
    }

    uv_fs_write(uv_default_loop(), write_req, fd, &buf, 1, writePos, [](uv_fs_t* req) {
        WriteContext* ctx = (WriteContext*)req->data;
        TsWriteStream* self = ctx->stream;
        self->bufferedAmount -= ctx->length;
        self->bytesWritten += ctx->length;

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

    if (!autoClose) {
        // Don't close the file descriptor, but emit events
        Emit("finish", 0, nullptr);
        return;
    }

    uv_fs_t* close_req = (uv_fs_t*)ts_alloc(sizeof(uv_fs_t));
    close_req->data = this;
    uv_fs_close(uv_default_loop(), close_req, fd, [](uv_fs_t* req) {
        TsWriteStream* self = (TsWriteStream*)req->data;
        self->Emit("finish", 0, nullptr);
        self->Emit("close", 0, nullptr);
        uv_fs_req_cleanup(req);
    });
}

// extern "C" wrappers moved to extensions/node/stream/src/stream.cpp
