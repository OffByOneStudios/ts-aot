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

    // Socket address properties
    const char* GetRemoteAddress();
    int GetRemotePort();
    const char* GetRemoteFamily();
    const char* GetLocalAddress();
    int GetLocalPort();
    const char* GetLocalFamily();

    // Socket state properties
    size_t GetBytesRead() const { return bytesRead_; }
    size_t GetBytesWritten() const { return bytesWritten_; }
    bool IsConnecting() const { return connecting_; }
    bool IsDestroyed() const { return closed; }
    bool IsPending() const { return connecting_; }
    const char* GetReadyState() const;

    // Socket configuration methods
    void SetTimeout(int msecs, void* callback);
    void SetNoDelay(bool noDelay);
    void SetKeepAlive(bool enable, int initialDelay);

    // Returns { address, family, port } object
    void* Address();

protected:
    uv_tcp_t* handle;
    bool connected;
    bool connecting_ = false;
    size_t bytesRead_ = 0;
    size_t bytesWritten_ = 0;
    int timeout_ = 0;
    void* timeoutCallback_ = nullptr;
    uv_timer_t* timeoutTimer_ = nullptr;

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

    // Socket address properties
    void* ts_net_socket_get_remote_address(void* socket);
    int64_t ts_net_socket_get_remote_port(void* socket);
    void* ts_net_socket_get_remote_family(void* socket);
    void* ts_net_socket_get_local_address(void* socket);
    int64_t ts_net_socket_get_local_port(void* socket);
    void* ts_net_socket_get_local_family(void* socket);

    // Socket state properties
    int64_t ts_net_socket_get_bytes_read(void* socket);
    int64_t ts_net_socket_get_bytes_written(void* socket);
    bool ts_net_socket_get_connecting(void* socket);
    bool ts_net_socket_get_destroyed(void* socket);
    bool ts_net_socket_get_pending(void* socket);
    void* ts_net_socket_get_ready_state(void* socket);

    // Socket configuration methods
    void* ts_net_socket_set_timeout(void* socket, void* msecs, void* callback);
    void* ts_net_socket_set_no_delay(void* socket, void* noDelay);
    void* ts_net_socket_set_keep_alive(void* socket, void* enable, void* initialDelay);
    void* ts_net_socket_address(void* socket);
    
    // net module utilities
    int64_t ts_net_is_ip(void* input);
    bool ts_net_is_ipv4(void* input);
    bool ts_net_is_ipv6(void* input);
    bool ts_net_get_default_auto_select_family();
    void ts_net_set_default_auto_select_family(bool value);
    int64_t ts_net_get_default_auto_select_family_attempt_timeout();
    void ts_net_set_default_auto_select_family_attempt_timeout(int64_t value);
    
    // SocketAddress class
    void* ts_net_socket_address_create(void* options);
    void* ts_net_socket_address_parse(void* input);
    void* ts_net_socket_address_get_address(void* addr);
    void* ts_net_socket_address_get_family(void* addr);
    int64_t ts_net_socket_address_get_flowlabel(void* addr);
    int64_t ts_net_socket_address_get_port(void* addr);
    
    // BlockList class
    void* ts_net_block_list_create();
    bool ts_net_block_list_is_block_list(void* value);
    void ts_net_block_list_add_address(void* blockList, void* address, void* type);
    void ts_net_block_list_add_range(void* blockList, void* start, void* end, void* type);
    void ts_net_block_list_add_subnet(void* blockList, void* net, int64_t prefix, void* type);
    bool ts_net_block_list_check(void* blockList, void* address, void* type);
    void* ts_net_block_list_get_rules(void* blockList);
    void* ts_net_block_list_to_json(void* blockList);
    void ts_net_block_list_from_json(void* blockList, void* value);
}
