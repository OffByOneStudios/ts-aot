#include "TsWriteStream.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "GC.h"
#include "TsRuntime.h"
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <new>

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

    uv_fs_t* write_req = (uv_fs_t*)ts_alloc(sizeof(uv_fs_t));
    uv_buf_t buf = uv_buf_init((char*)data, (unsigned int)length);
    
    write_req->data = this;
    uv_fs_write(uv_default_loop(), write_req, fd, &buf, 1, -1, OnWrite);
}

void TsWriteStream::OnWrite(uv_fs_t* req) {
    uv_fs_req_cleanup(req);
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

    void ts_fs_write_stream_write(void* stream, void* data) {
        TsWriteStream* s = (TsWriteStream*)stream;
        if (!data) return;

        TsValue* val = (TsValue*)data;
        void* ptr = nullptr;
        if (val->type == ValueType::STRING_PTR || val->type == ValueType::OBJECT_PTR) {
            ptr = val->ptr_val;
        } else {
            // If it's not a TsValue, maybe it's a raw pointer? 
            // (This can happen if the compiler didn't box it)
            ptr = data;
        }

        if (!ptr) return;

        // Check magic numbers to distinguish between Buffer and String
        uint32_t magic0 = *(uint32_t*)ptr;
        uint32_t magic8 = *(uint32_t*)((char*)ptr + 8);

        if (magic0 == 0x53545247) { // "STRG"
            TsString* str = (TsString*)ptr;
            const char* utf8 = str->ToUtf8();
            s->Write((void*)utf8, strlen(utf8));
        } else if (magic8 == 0x42554646) { // "BUFF"
            TsBuffer* buf = (TsBuffer*)ptr;
            s->Write(buf->GetData(), buf->GetLength());
        }
    }

    void ts_fs_write_stream_end(void* stream) {
        TsWriteStream* s = (TsWriteStream*)stream;
        s->End();
    }
}
