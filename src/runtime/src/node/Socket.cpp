#include "TsSocket.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "GC.h"
#include <string.h>
#include <stdlib.h>
#include <new>

TsSocket::TsSocket() : connected(false) {
    flowing = false;
    reading = false;
    closed = false;
    bufferedAmount = 0;
    highWaterMark = 16384;
    needDrain = false;
    handle = (uv_tcp_t*)ts_alloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), handle);
    handle->data = this;
}

TsSocket::TsSocket(uv_tcp_t* h) : handle(h), connected(true) {
    flowing = false;
    reading = false;
    closed = false;
    bufferedAmount = 0;
    highWaterMark = 16384;
    needDrain = false;
    handle->data = this;
}

TsSocket::~TsSocket() {
    if (!closed) {
        uv_close((uv_handle_t*)handle, OnClose);
    }
}

void TsSocket::Connect(const char* host, int port, void* callback) {
    if (closed) return;

    struct sockaddr_in dest;
    uv_ip4_addr(host, port, &dest);

    uv_connect_t* req = (uv_connect_t*)ts_alloc(sizeof(uv_connect_t));
    
    struct ConnectContext {
        TsSocket* socket;
        void* callback;
    };
    ConnectContext* ctx = (ConnectContext*)ts_alloc(sizeof(ConnectContext));
    ctx->socket = this;
    ctx->callback = callback;
    req->data = ctx;

    uv_tcp_connect(req, handle, (const struct sockaddr*)&dest, [](uv_connect_t* req, int status) {
        ConnectContext* ctx = (ConnectContext*)req->data;
        TsSocket* self = ctx->socket;
        
        if (status == 0) {
            self->connected = true;
            self->Emit("connect", 0, nullptr);
            if (ctx->callback) {
                // Call the callback
                // TODO: Implement calling JS function from C++
            }
        } else {
            // Emit error
            self->Emit("error", 0, nullptr);
        }
    });
}

void TsSocket::On(const char* event, void* callback) {
    TsEventEmitter::On(event, callback);
    if (strcmp(event, "data") == 0) {
        Resume();
    }
}

void TsSocket::Pause() {
    if (flowing) {
        flowing = false;
        uv_read_stop((uv_stream_t*)handle);
    }
}

void TsSocket::Resume() {
    if (!flowing && connected && !closed) {
        flowing = true;
        uv_read_start((uv_stream_t*)handle, OnAlloc, OnRead);
    }
}

void TsSocket::OnAlloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)ts_alloc(suggested_size);
    buf->len = (unsigned int)suggested_size;
}

void TsSocket::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    TsSocket* self = (TsSocket*)stream->data;
    
    if (nread > 0) {
        TsBuffer* chunk = TsBuffer::Create(nread);
        memcpy(chunk->GetData(), buf->base, nread);
        
        TsValue* arg0 = ts_value_make_object(chunk);
        void* args[] = { arg0 };
        self->Emit("data", 1, args);
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            self->Emit("end", 0, nullptr);
        } else {
            self->Emit("error", 0, nullptr);
        }
        self->End();
    }
}

bool TsSocket::Write(void* data, size_t length) {
    if (closed || !connected) return false;

    bufferedAmount += length;

    uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    
    struct WriteContext {
        TsSocket* socket;
        size_t length;
    };
    WriteContext* ctx = (WriteContext*)ts_alloc(sizeof(WriteContext));
    ctx->socket = this;
    ctx->length = length;
    req->data = ctx;

    uv_buf_t buf = uv_buf_init((char*)data, (unsigned int)length);
    
    uv_write(req, (uv_stream_t*)handle, &buf, 1, [](uv_write_t* req, int status) {
        WriteContext* ctx = (WriteContext*)req->data;
        TsSocket* self = ctx->socket;
        self->bufferedAmount -= ctx->length;
        
        if (self->needDrain && self->bufferedAmount < self->highWaterMark) {
            self->needDrain = false;
            self->Emit("drain", 0, nullptr);
        }
    });

    if (bufferedAmount >= highWaterMark) {
        needDrain = true;
        return false;
    }
    return true;
}

void TsSocket::End() {
    if (closed) return;
    closed = true;
    uv_close((uv_handle_t*)handle, OnClose);
}

void TsSocket::OnClose(uv_handle_t* handle) {
    TsSocket* self = (TsSocket*)handle->data;
    self->Emit("close", 0, nullptr);
}

extern "C" {
    void* ts_net_create_socket() {
        void* mem = ts_alloc(sizeof(TsSocket));
        return new (mem) TsSocket();
    }

    void ts_net_socket_connect(void* socket, void* port, void* host, void* callback) {
        TsSocket* s = (TsSocket*)socket;
        TsValue* p = (TsValue*)port;
        TsString* h = (TsString*)host;
        s->Connect(h->ToUtf8(), (int)p->i_val, callback);
    }
}
