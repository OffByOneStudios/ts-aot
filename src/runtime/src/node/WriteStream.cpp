#include "TsWriteStream.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "GC.h"
#include "TsGC.h"
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

// libuv links the request struct into its worker-thread queue (uv__queue_insert_tail
// writes the intrusive queue node from a pool thread). The request and its context
// therefore must NOT live in GC memory: a GC cycle between dispatch and completion
// would free/move the block -> worker-thread access violation (GC-001). They are
// malloc'd here and freed in the completion callback. The TsWriteStream* GC pointer
// they reference is registered as a GC root for the operation lifetime, and the
// payload bytes are copied (the worker thread reads them asynchronously).
struct WriteContext {
    TsWriteStream* stream;   // GC pointer - rooted for op lifetime
    size_t length;
    char* data;              // owned copy of payload (worker reads async)
};

bool TsWriteStream::Write(void* data, size_t length) {
    if (closed) return false;
    started = true;

    bufferedAmount += length;

    uv_fs_t* write_req = (uv_fs_t*)malloc(sizeof(uv_fs_t));

    WriteContext* ctx = (WriteContext*)malloc(sizeof(WriteContext));
    ctx->stream = this;
    ctx->length = length;
    ctx->data = (char*)malloc(length ? length : 1);
    if (length) memcpy(ctx->data, data, length);
    // Root the stream so a GC during the async write cannot collect it.
    ts_gc_register_root((void**)&ctx->stream);
    write_req->data = ctx;

    uv_buf_t buf = uv_buf_init(ctx->data, (unsigned int)length);

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
        ts_gc_unregister_root((void**)&ctx->stream);
        free(ctx->data);
        free(ctx);
        free(req);
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

    // Request struct must not live in GC memory (see Write() above). Box the
    // GC stream pointer in a malloc'd, rooted slot for the close lifetime.
    uv_fs_t* close_req = (uv_fs_t*)malloc(sizeof(uv_fs_t));
    void** streamSlot = (void**)malloc(sizeof(void*));
    *streamSlot = this;
    ts_gc_register_root(streamSlot);
    close_req->data = streamSlot;
    uv_fs_close(uv_default_loop(), close_req, fd, [](uv_fs_t* req) {
        void** streamSlot = (void**)req->data;
        TsWriteStream* self = (TsWriteStream*)*streamSlot;
        self->Emit("finish", 0, nullptr);
        self->Emit("close", 0, nullptr);
        uv_fs_req_cleanup(req);
        ts_gc_unregister_root(streamSlot);
        free(streamSlot);
        free(req);
    });
}

// extern "C" wrappers moved to extensions/node/stream/src/stream.cpp
