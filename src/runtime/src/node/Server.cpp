#include "TsServer.h"
#include "TsSocket.h"
#include "TsRuntime.h"
#include "GC.h"
#include <new>

TsServer::TsServer() : listening(false), closed(false) {
    handle = (uv_tcp_t*)ts_alloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), handle);
    handle->data = this;
}

TsServer::~TsServer() {
    if (!closed) {
        Close();
    }
}

void TsServer::Listen(int port, void* callback) {
    if (closed) return;

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", port, &addr);

    uv_tcp_bind(handle, (const struct sockaddr*)&addr, 0);
    
    int r = uv_listen((uv_stream_t*)handle, 128, OnConnection);
    if (r == 0) {
        listening = true;
        if (callback) {
            // TODO: Call callback
        }
        Emit("listening", 0, nullptr);
    } else {
        Emit("error", 0, nullptr);
    }
}

void TsServer::OnConnection(uv_stream_t* server, int status) {
    if (status < 0) return;

    TsServer* self = (TsServer*)server->data;
    
    uv_tcp_t* client = (uv_tcp_t*)ts_alloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), client);
    
    if (uv_accept(server, (uv_stream_t*)client) == 0) {
        void* mem = ts_alloc(sizeof(TsSocket));
        TsSocket* socket = new (mem) TsSocket(client);
        
        TsValue* arg0 = ts_value_make_object(socket);
        void* args[] = { arg0 };
        self->Emit("connection", 1, args);
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
}
