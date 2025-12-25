#include "TsWriteStream.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "IO.h"
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

TsWriteStream::TsWriteStream(int fd) : fd(fd), closed(false) {
}

TsWriteStream::~TsWriteStream() {
    if (!closed) {
        uv_fs_t close_req;
        uv_fs_close(uv_default_loop(), &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
    }
}

void TsWriteStream::Write(void* data, size_t length) {
    if (closed) return;

    uv_fs_t* write_req = (uv_fs_t*)malloc(sizeof(uv_fs_t));
    uv_buf_t buf = uv_buf_init((char*)data, (unsigned int)length);
    
    write_req->data = this;
    int r = uv_fs_write(uv_default_loop(), write_req, fd, &buf, 1, -1, OnWrite);
    if (r < 0) {
        free(write_req);
    }
}

void TsWriteStream::OnWrite(uv_fs_t* req) {
    uv_fs_req_cleanup(req);
    free(req);
}

void TsWriteStream::End() {
    if (closed) return;
    closed = true;
    
    uv_fs_t* close_req = (uv_fs_t*)malloc(sizeof(uv_fs_t));
    close_req->data = this;
    uv_fs_close(uv_default_loop(), close_req, fd, [](uv_fs_t* req) {
        TsWriteStream* self = (TsWriteStream*)req->data;
        self->Emit("finish", 0, nullptr);
        self->Emit("close", 0, nullptr);
        uv_fs_req_cleanup(req);
        free(req);
    });
}

extern "C" {
    void* ts_fs_createWriteStream(void* path) {
        TsString* p = (TsString*)path;
        std::printf("Creating WriteStream for path: %s\n", p->ToUtf8());
        uv_fs_t open_req;
        int fd = uv_fs_open(uv_default_loop(), &open_req, p->ToUtf8(), O_WRONLY | O_CREAT | O_TRUNC, 0644, NULL);
        uv_fs_req_cleanup(&open_req);

        if (fd < 0) {
            std::printf("Failed to open file for writing: %s (error %d)\n", p->ToUtf8(), fd);
            return nullptr;
        }
        std::printf("uv_fs_open result: %d\n", fd);
        return new TsWriteStream(fd);
    }

    void ts_fs_write_stream_write(void* stream, void* data) {
        TsWriteStream* s = (TsWriteStream*)stream;
        if (!data) return;

        // Check magic numbers to distinguish between Buffer and String
        uint32_t magic = *(uint32_t*)data;
        if (magic == 0x53545247) { // "STRG"
            TsString* str = (TsString*)data;
            const char* utf8 = str->ToUtf8();
            s->Write((void*)utf8, strlen(utf8));
        } else {
            // Try TsBuffer
            TsBuffer* buf = (TsBuffer*)data;
            if (buf->magic == 0x42554646) { // "BUFF"
                s->Write(buf->GetData(), buf->GetLength());
            } else {
                std::printf("ts_fs_write_stream_write: Unknown data type (magic 0x%08x)\n", magic);
            }
        }
    }

    void ts_fs_write_stream_end(void* stream) {
        TsWriteStream* s = (TsWriteStream*)stream;
        s->End();
    }
}
