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

TsReadStream::TsReadStream(int fd, const ReadStreamOptions& opts)
    : fd(fd), closed(false), started(false), position(opts.start),
      endPosition(opts.end), bytesRead(0), path(nullptr), autoClose(opts.autoClose) {
    flowing = false;
    reading = false;
    buffer = TsBuffer::Create(opts.highWaterMark);
}

TsReadStream::~TsReadStream() {
    if (!closed && autoClose) {
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
    started = true;
    reading = true;

    uv_fs_t* req = (uv_fs_t*)ts_alloc(sizeof(uv_fs_t));
    req->data = this;

    // Calculate how many bytes to read
    size_t toRead = buffer->GetLength();
    if (endPosition >= 0) {
        int64_t remaining = endPosition - position + 1;
        if (remaining <= 0) {
            // Reached end position
            reading = false;
            Emit("end", 0, nullptr);
            if (autoClose) Close();
            return;
        }
        if ((int64_t)toRead > remaining) {
            toRead = (size_t)remaining;
        }
    }

    uv_buf_t buf = uv_buf_init((char*)buffer->GetData(), (unsigned int)toRead);
    uv_fs_read(uv_default_loop(), req, fd, &buf, 1, position, OnRead);
}

void TsReadStream::OnRead(uv_fs_t* req) {
    TsReadStream* self = (TsReadStream*)req->data;
    self->reading = false;
    int result = (int)req->result;

    if (result > 0) {
        self->position += result;
        self->bytesRead += result;

        // Emit 'data' event
        TsBuffer* chunk = TsBuffer::Create(result);
        memcpy(chunk->GetData(), self->buffer->GetData(), result);

        TsValue* arg0 = ts_value_make_object(chunk);
        void* args[] = { arg0 };
        self->Emit("data", 1, args);

        // Check if we've reached end position
        if (self->endPosition >= 0 && self->position > self->endPosition) {
            self->Emit("end", 0, nullptr);
            if (self->autoClose) self->Close();
            uv_fs_req_cleanup(req);
            return;
        }

        // Read next chunk if still flowing
        if (self->flowing && !self->closed) {
            self->Start();
        }
        uv_fs_req_cleanup(req);
    } else {
        // EOF or error
        if (result == 0) {
            self->Emit("end", 0, nullptr);
        } else {
            // Error - emit error event
            void* err = ts_error_create(TsString::Create("Read error"));
            void* args[] = { err };
            self->Emit("error", 1, args);
        }
        if (self->autoClose) self->Close();
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

void TsReadStream::SetPath(const char* p) {
    if (p) {
        size_t len = strlen(p);
        path = (char*)ts_alloc(len + 1);
        memcpy(path, p, len + 1);
    } else {
        path = nullptr;
    }
}

// extern "C" wrappers moved to extensions/node/stream/src/stream.cpp
