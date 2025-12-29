#pragma once
#include "TsDuplex.h"
#include <uv.h>

/*
 * ⚠️ VIRTUAL INHERITANCE WARNING ⚠️
 * 
 * TsSocket inherits from TsDuplex which uses virtual inheritance.
 * 
 * To cast to TsSocket:
 *   TsSocket* s = ((TsObject*)ptr)->AsSocket();        // ✅ CORRECT
 *   TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)ptr); // ✅ CORRECT
 *   TsSocket* s = (TsSocket*)ptr;                      // ❌ BROKEN!
 * 
 * When receiving void* params, ALWAYS unbox first:
 *   void* rawPtr = ts_value_get_object((TsValue*)param);
 *   if (!rawPtr) rawPtr = param;
 *   TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)rawPtr);
 */

class TsSocket : public TsDuplex {
public:
    TsSocket();
    TsSocket(uv_tcp_t* handle);
    virtual ~TsSocket();
    
    // Safe casting helper
    virtual TsSocket* AsSocket() override { return this; }

    void Connect(const char* host, int port, void* callback);
    
    // TsDuplex implementation
    virtual void Pause() override;
    virtual void Resume() override;
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    virtual void On(const char* event, void* callback) override;

    uv_stream_t* GetStream() { return (uv_stream_t*)handle; }

protected:
    uv_tcp_t* handle;
    bool connected;

    virtual void OnConnected();
    virtual void HandleRead(ssize_t nread, const uv_buf_t* buf);
    virtual void HandleWrite(int status, size_t length);

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
