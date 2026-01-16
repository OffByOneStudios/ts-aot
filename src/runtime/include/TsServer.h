#pragma once
#include "TsEventEmitter.h"
#include <uv.h>

class TsServer : public TsEventEmitter {
public:
    TsServer();
    virtual ~TsServer();
    
    // Safe casting helper
    virtual TsServer* AsServer() override { return this; }

    void Listen(int port, void* callback);
    void Close();

    // Returns { address, family, port } object or null if not listening
    void* Address();

    // Handle reference control
    void Ref();
    void Unref();

    // Get connection count
    void GetConnections(void* callback);

    // Max connections property (-1 = unlimited)
    int maxConnections;

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
    void ts_net_server_close(void* server);
    void* ts_net_server_address(void* server);

    // Handle reference control
    void* ts_net_server_ref(void* server);
    void* ts_net_server_unref(void* server);

    // Get connection count
    void ts_net_server_get_connections(void* server, void* callback);

    // Max connections property
    int64_t ts_net_server_get_maxConnections(void* server);
    void ts_net_server_set_maxConnections(void* server, void* value);
}
