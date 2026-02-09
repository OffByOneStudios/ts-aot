#include "TsServer.h"
#include "TsSocket.h"
#include "TsRuntime.h"
#include "TsMap.h"
#include "TsString.h"
#include "GC.h"
#include <new>
#include <uv.h>

TsServer::TsServer() : listening(false), closed(false), maxConnections(-1) {
    this->magic = MAGIC;
    handle = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), handle);
    handle->data = this;
}

TsServer::~TsServer() {
    if (!closed) {
        Close();
    }
    free(handle);
}

// Callback data for deferred "listening" emit
struct ListenEmitData {
    TsServer* server;
    uv_timer_t timer;
};

static void on_deferred_listening(uv_timer_t* timer) {
    ListenEmitData* data = (ListenEmitData*)timer->data;
    data->server->Emit("listening", 0, nullptr);
    uv_close((uv_handle_t*)timer, [](uv_handle_t* h) {
        free(h->data);
    });
}

void TsServer::Listen(int port, void* callback) {
    if (closed) return;

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", port, &addr);

    int r = uv_tcp_bind(handle, (const struct sockaddr*)&addr, 0);

    r = uv_listen((uv_stream_t*)handle, 128, OnConnection);

    if (r == 0) {
        listening = true;
        if (callback) {
            On("listening", callback);
        }
        // Defer the "listening" event to the next event loop iteration.
        // Node.js does this to ensure user_main() returns and the event loop
        // is running before callbacks fire (important for async callbacks that
        // need the event loop, e.g. fetch() inside the listen callback).
        ListenEmitData* data = (ListenEmitData*)malloc(sizeof(ListenEmitData));
        data->server = this;
        uv_timer_init(uv_default_loop(), &data->timer);
        data->timer.data = data;
        uv_timer_start(&data->timer, on_deferred_listening, 0, 0);
    } else {
        Emit("error", 0, nullptr);
    }
}

void TsServer::OnConnection(uv_stream_t* server, int status) {
    TsServer* self = (TsServer*)server->data;
    self->HandleConnection(status);
}

void TsServer::HandleConnection(int status) {
    if (status < 0) return;

    uv_tcp_t* client = (uv_tcp_t*)ts_alloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), client);

    if (uv_accept((uv_stream_t*)handle, (uv_stream_t*)client) == 0) {
        void* mem = ts_alloc(sizeof(TsSocket));
        TsSocket* socket = new (mem) TsSocket(client);

        TsValue* arg0 = ts_value_make_object(socket);
        void* args[] = { arg0 };
        Emit("connection", 1, args);
    } else {
        uv_close((uv_handle_t*)client, nullptr);
    }
}

void TsServer::Close() {
    if (closed) return;
    closed = true;
    uv_close((uv_handle_t*)handle, OnClose);
}

void TsServer::OnClose(uv_handle_t* handle) {
    TsServer* self = (TsServer*)handle->data;
    self->Emit("close", 0, nullptr);
}

void* TsServer::Address() {
    if (!listening || closed) return nullptr;

    struct sockaddr_storage addr;
    int addrLen = sizeof(addr);

    int result = uv_tcp_getsockname(handle, (struct sockaddr*)&addr, &addrLen);
    if (result != 0) return nullptr;

    TsMap* obj = TsMap::Create();

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
        char ipStr[INET_ADDRSTRLEN];
        uv_inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));

        // Use TsValue constructors directly instead of ts_value_make_* which return TsValue*
        // TsMap::Set takes TsValue by value, and TsValue* would be double-wrapped via void* constructor
        obj->Set(TsString::Create("address"), TsValue(TsString::Create(ipStr)));
        obj->Set(TsString::Create("family"), TsValue(TsString::Create("IPv4")));
        obj->Set(TsString::Create("port"), TsValue((int64_t)ntohs(addr4->sin_port)));
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
        char ipStr[INET6_ADDRSTRLEN];
        uv_inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));

        // Use TsValue constructors directly instead of ts_value_make_* which return TsValue*
        obj->Set(TsString::Create("address"), TsValue(TsString::Create(ipStr)));
        obj->Set(TsString::Create("family"), TsValue(TsString::Create("IPv6")));
        obj->Set(TsString::Create("port"), TsValue((int64_t)ntohs(addr6->sin6_port)));
    } else {
        return nullptr;
    }

    return obj;
}

void TsServer::Ref() {
    if (handle && !closed) {
        uv_ref((uv_handle_t*)handle);
    }
}

void TsServer::Unref() {
    if (handle && !closed) {
        uv_unref((uv_handle_t*)handle);
    }
}

void TsServer::GetConnections(void* callback) {
    // For now, we don't track active connections
    // Just call the callback with 0 connections
    // In a full implementation, we'd track all accepted sockets
    if (callback) {
        TsValue* err = ts_value_make_null();
        TsValue* count = ts_value_make_int(0);
        TsValue* args[] = { err, count };
        ts_function_call((TsValue*)callback, 2, args);
    }
}

// Moved to extensions/node/net/src/net.cpp - now a separate library (ts_net)
