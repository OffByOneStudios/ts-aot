#include "TsSocket.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include "GC.h"
#include <string.h>
#include <stdlib.h>
#include <new>
#include <iostream>
#include <stdio.h>
#include <vector>
#include <string>

TsSocket::TsSocket() : connected(false) {
    this->magic = 0x534F434B; // "SOCK"
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
    this->magic = 0x534F434B; // "SOCK"
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

    connecting_ = true;  // Set connecting state

    struct ConnectContext {
        TsSocket* socket;
        void* callback;
        int port;
    };
    ConnectContext* ctx = (ConnectContext*)ts_alloc(sizeof(ConnectContext));
    ctx->socket = this;
    ctx->callback = callback;
    ctx->port = port;

    uv_getaddrinfo_t* dns_req = (uv_getaddrinfo_t*)ts_alloc(sizeof(uv_getaddrinfo_t));
    dns_req->data = ctx;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // Force IPv4 for now (server only binds to 0.0.0.0)
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    sprintf(port_str, "%d", port);

    int r = uv_getaddrinfo(uv_default_loop(), dns_req, [](uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
        ConnectContext* ctx = (ConnectContext*)req->data;
        TsSocket* self = ctx->socket;

        if (status < 0) {
            self->Emit("error", 0, nullptr);
            return;
        }

        uv_connect_t* connect_req = (uv_connect_t*)ts_alloc(sizeof(uv_connect_t));
        connect_req->data = ctx;

        int r = uv_tcp_connect(connect_req, self->handle, res->ai_addr, [](uv_connect_t* req, int status) {
            ConnectContext* ctx = (ConnectContext*)req->data;
            TsSocket* self = ctx->socket;

            self->connecting_ = false;  // Clear connecting state

            if (status == 0) {
                self->connected = true;
                self->OnConnected();
                if (ctx->callback) {
                    // TODO: Implement calling JS function from C++
                }
            } else {
                self->Emit("error", 0, nullptr);
            }
        });

        if (r < 0) {
            self->Emit("error", 0, nullptr);
        }

        uv_freeaddrinfo(res);
    }, host, port_str, &hints);

    if (r < 0) {
        Emit("error", 0, nullptr);
    }
}

void TsSocket::OnConnected() {
    Resume();
    Emit("connect", 0, nullptr);
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
    self->HandleRead(nread, buf);
}

void TsSocket::HandleRead(ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) {
        bytesRead_ += nread;  // Track bytes read
        TsBuffer* chunk = TsBuffer::Create(nread);
        memcpy(chunk->GetData(), buf->base, nread);

        TsValue* arg0 = ts_value_make_object(chunk);
        void* args[] = { arg0 };
        Emit("data", 1, args);
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            Emit("end", 0, nullptr);
        } else {
            Emit("error", 0, nullptr);
        }
        End();
    }
}

struct WriteContext {
    TsSocket* socket;
    size_t length;
};

bool TsSocket::Write(void* data, size_t length) {
    if (closed || !connected) return false;

    bufferedAmount += length;

    uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    
    WriteContext* ctx = (WriteContext*)ts_alloc(sizeof(WriteContext));
    ctx->socket = this;
    ctx->length = length;
    req->data = ctx;

    uv_buf_t buf = uv_buf_init((char*)data, (unsigned int)length);
    
    uv_write(req, (uv_stream_t*)handle, &buf, 1, OnWrite);

    if (bufferedAmount >= highWaterMark) {
        needDrain = true;
        return false;
    }
    return true;
}

void TsSocket::OnWrite(uv_write_t* req, int status) {
    WriteContext* ctx = (WriteContext*)req->data;
    TsSocket* self = ctx->socket;
    self->HandleWrite(status, ctx->length);
}

void TsSocket::HandleWrite(int status, size_t length) {
    bufferedAmount -= length;
    bytesWritten_ += length;  // Track bytes written

    if (needDrain && bufferedAmount < highWaterMark) {
        needDrain = false;
        Emit("drain", 0, nullptr);
    }
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

// Socket address property implementations
const char* TsSocket::GetRemoteAddress() {
    if (!connected || closed) return nullptr;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    int result = uv_tcp_getpeername(handle, (struct sockaddr*)&addr, &addrLen);
    if (result != 0) return nullptr;

    static char ipStr[INET6_ADDRSTRLEN];

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
        uv_inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
        uv_inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
    } else {
        return nullptr;
    }

    return ipStr;
}

int TsSocket::GetRemotePort() {
    if (!connected || closed) return 0;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    int result = uv_tcp_getpeername(handle, (struct sockaddr*)&addr, &addrLen);
    if (result != 0) return 0;

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
        return ntohs(addr4->sin_port);
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
        return ntohs(addr6->sin6_port);
    }

    return 0;
}

const char* TsSocket::GetRemoteFamily() {
    if (!connected || closed) return nullptr;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    int result = uv_tcp_getpeername(handle, (struct sockaddr*)&addr, &addrLen);
    if (result != 0) return nullptr;

    if (addr.ss_family == AF_INET) {
        return "IPv4";
    } else if (addr.ss_family == AF_INET6) {
        return "IPv6";
    }

    return nullptr;
}

const char* TsSocket::GetLocalAddress() {
    if (closed) return nullptr;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    int result = uv_tcp_getsockname(handle, (struct sockaddr*)&addr, &addrLen);
    if (result != 0) return nullptr;

    static char ipStr[INET6_ADDRSTRLEN];

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
        uv_inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
        uv_inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
    } else {
        return nullptr;
    }

    return ipStr;
}

int TsSocket::GetLocalPort() {
    if (closed) return 0;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    int result = uv_tcp_getsockname(handle, (struct sockaddr*)&addr, &addrLen);
    if (result != 0) return 0;

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
        return ntohs(addr4->sin_port);
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
        return ntohs(addr6->sin6_port);
    }

    return 0;
}

const char* TsSocket::GetLocalFamily() {
    if (closed) return nullptr;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    int result = uv_tcp_getsockname(handle, (struct sockaddr*)&addr, &addrLen);
    if (result != 0) return nullptr;

    if (addr.ss_family == AF_INET) {
        return "IPv4";
    } else if (addr.ss_family == AF_INET6) {
        return "IPv6";
    }

    return nullptr;
}

const char* TsSocket::GetReadyState() const {
    if (closed) return "closed";
    if (connecting_) return "opening";
    if (connected) return "open";
    // New socket that hasn't connected yet - returns "closed" per Node.js spec
    // (socket.readyState is "closed" before connecting and after end)
    return "closed";
}

void TsSocket::SetTimeout(int msecs, void* callback) {
    timeout_ = msecs;
    timeoutCallback_ = callback;

    // Clean up existing timer
    if (timeoutTimer_) {
        uv_timer_stop(timeoutTimer_);
    }

    if (msecs == 0) {
        // Disable timeout
        if (timeoutTimer_) {
            uv_close((uv_handle_t*)timeoutTimer_, nullptr);
            timeoutTimer_ = nullptr;
        }
        return;
    }

    // Create timer if needed
    if (!timeoutTimer_) {
        timeoutTimer_ = (uv_timer_t*)ts_alloc(sizeof(uv_timer_t));
        uv_timer_init(uv_default_loop(), timeoutTimer_);
        timeoutTimer_->data = this;
    }

    // Start the timeout timer
    uv_timer_start(timeoutTimer_, [](uv_timer_t* timer) {
        TsSocket* self = (TsSocket*)timer->data;
        self->Emit("timeout", 0, nullptr);
    }, msecs, 0);
}

void TsSocket::SetNoDelay(bool noDelay) {
    if (handle) {
        uv_tcp_nodelay(handle, noDelay ? 1 : 0);
    }
}

void TsSocket::SetKeepAlive(bool enable, int initialDelay) {
    if (handle) {
        uv_tcp_keepalive(handle, enable ? 1 : 0, initialDelay);
    }
}

void* TsSocket::Address() {
    if (closed) return nullptr;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    int result = uv_tcp_getsockname(handle, (struct sockaddr*)&addr, &addrLen);
    if (result != 0) return nullptr;

    TsMap* obj = TsMap::Create();

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
        char ipStr[INET_ADDRSTRLEN];
        uv_inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));

        obj->Set(TsString::Create("address"), ts_value_make_string(TsString::Create(ipStr)));
        obj->Set(TsString::Create("family"), ts_value_make_string(TsString::Create("IPv4")));
        obj->Set(TsString::Create("port"), ts_value_make_int(ntohs(addr4->sin_port)));
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
        char ipStr[INET6_ADDRSTRLEN];
        uv_inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));

        obj->Set(TsString::Create("address"), ts_value_make_string(TsString::Create(ipStr)));
        obj->Set(TsString::Create("family"), ts_value_make_string(TsString::Create("IPv6")));
        obj->Set(TsString::Create("port"), ts_value_make_int(ntohs(addr6->sin6_port)));
    } else {
        return nullptr;
    }

    return obj;
}

void TsSocket::Ref() {
    if (handle && !closed) {
        uv_ref((uv_handle_t*)handle);
    }
}

void TsSocket::Unref() {
    if (handle && !closed) {
        uv_unref((uv_handle_t*)handle);
    }
}

// Moved to extensions/node/net/src/net.cpp - now a separate library (ts_net)
