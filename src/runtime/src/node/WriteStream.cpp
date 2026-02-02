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
#if 0  // Moved to ts_stream extension
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
            void* rawOpts = ts_value_get_object((TsValue*)options);
            if (!rawOpts) rawOpts = options;

            // Try to get properties
            TsValue* startVal = ts_object_get_property(rawOpts, "start");
            if (startVal && (startVal->type == ValueType::NUMBER_INT || startVal->type == ValueType::NUMBER_DBL)) {
                opts.start = ts_value_get_int(startVal);
                // If start is specified, don't truncate the file
                openFlags = O_WRONLY | O_CREAT;
            }

            TsValue* autoCloseVal = ts_object_get_property(rawOpts, "autoClose");
            if (autoCloseVal && autoCloseVal->type == ValueType::BOOLEAN) {
                opts.autoClose = ts_value_get_bool(autoCloseVal);
            }

            TsValue* flagsVal = ts_object_get_property(rawOpts, "flags");
            if (flagsVal && flagsVal->type == ValueType::STRING_PTR) {
                TsString* flagsStr = (TsString*)ts_value_get_string(flagsVal);
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
        TsWriteStream* s = (TsWriteStream*)stream;
        TsBuffer* b = (TsBuffer*)ts_value_get_object((TsValue*)buffer);
        if (!b) b = (TsBuffer*)buffer; // Fallback if not boxed
        return s->Write(b->GetData(), b->GetLength());
    }

    void ts_fs_write_stream_end(void* stream) {
        TsWriteStream* s = (TsWriteStream*)stream;
        s->End();
    }

    // WriteStream property accessors
    int64_t ts_write_stream_bytes_written(void* stream) {
        if (!stream) return 0;
        void* rawPtr = ts_value_get_object((TsValue*)stream);
        if (!rawPtr) rawPtr = stream;
        TsWriteStream* ws = dynamic_cast<TsWriteStream*>((TsEventEmitter*)rawPtr);
        if (!ws) return 0;
        return ws->GetBytesWritten();
    }

    void* ts_write_stream_path(void* stream) {
        if (!stream) return nullptr;
        void* rawPtr = ts_value_get_object((TsValue*)stream);
        if (!rawPtr) rawPtr = stream;
        TsWriteStream* ws = dynamic_cast<TsWriteStream*>((TsEventEmitter*)rawPtr);
        if (!ws) return nullptr;
        const char* path = ws->GetPath();
        if (!path) return nullptr;
        return TsString::Create(path);
    }

    bool ts_write_stream_pending(void* stream) {
        if (!stream) return true;
        void* rawPtr = ts_value_get_object((TsValue*)stream);
        if (!rawPtr) rawPtr = stream;
        TsWriteStream* ws = dynamic_cast<TsWriteStream*>((TsEventEmitter*)rawPtr);
        if (!ws) return true;
        return ws->IsPending();
    }
}
#endif  // Moved to ts_stream extension
