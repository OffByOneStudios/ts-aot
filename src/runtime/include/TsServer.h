#pragma once
#include "TsEventEmitter.h"
#include <uv.h>

class TsServer : public TsEventEmitter {
public:
    TsServer();
    virtual ~TsServer();

    void Listen(int port, void* callback);
    void Close();

protected:
    uv_tcp_t* handle;
    bool listening;
    bool closed;

    virtual void HandleConnection(int status);

    static void OnConnection(uv_stream_t* server, int status);
    static void OnClose(uv_handle_t* handle);
};

extern "C" {
    void* ts_net_create_server(void* callback);
    void ts_net_server_listen(void* server, void* port, void* callback);
}
