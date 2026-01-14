#include "TsServer.h"
#include "TsSocket.h"
#include "TsRuntime.h"
#include "TsMap.h"
#include "TsString.h"
#include "GC.h"
#include <new>

TsServer::TsServer() : listening(false), closed(false) {
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
        Emit("listening", 0, nullptr);
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

extern "C" {
    void* ts_net_create_server(void* callback) {
        void* mem = ts_alloc(sizeof(TsServer));
        TsServer* server = new (mem) TsServer();
        if (callback) {
            server->On("connection", callback);
        }
        return server;
    }

    void ts_net_server_listen(void* server, void* port, void* callback) {
        TsServer* s = (TsServer*)server;
        TsValue* p = (TsValue*)port;
        s->Listen((int)p->i_val, callback);
    }

    void ts_net_server_close(void* server) {
        TsServer* s = (TsServer*)server;
        if (s) s->Close();
    }

    void* ts_net_server_address(void* server) {
        TsServer* s = (TsServer*)server;
        if (!s) return nullptr;
        return s->Address();
    }
}
