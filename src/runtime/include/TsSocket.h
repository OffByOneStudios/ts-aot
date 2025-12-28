#pragma once
#include "TsDuplex.h"
#include <uv.h>

class TsSocket : public TsDuplex {
public:
    TsSocket();
    TsSocket(uv_tcp_t* handle);
    virtual ~TsSocket();

    void Connect(const char* host, int port, void* callback);
    
    // TsDuplex implementation
    virtual void Pause() override;
    virtual void Resume() override;
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void On(const char* event, void* callback) override;

private:
    uv_tcp_t* handle;
    bool connected;
    bool closed;
    bool flowing;
    bool reading;
    size_t bufferedAmount;
    size_t highWaterMark;
    bool needDrain;

    static void OnConnect(uv_connect_t* req, int status);
    static void OnAlloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnWrite(uv_write_t* req, int status);
    static void OnClose(uv_handle_t* handle);
};

extern "C" {
    void* ts_net_create_socket();
    void ts_net_socket_connect(void* socket, void* port, void* host, void* callback);
    bool ts_net_socket_write(void* socket, void* data);
    void ts_net_socket_end(void* socket);
}
