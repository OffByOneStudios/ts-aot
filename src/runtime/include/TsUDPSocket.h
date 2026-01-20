#pragma once
#include "TsEventEmitter.h"
#include <uv.h>

/*
 * TsUDPSocket - Node.js dgram.Socket implementation
 *
 * UDP is message-oriented (NOT stream-based), so TsUDPSocket inherits from
 * TsEventEmitter directly, NOT from TsDuplex.
 *
 * Events:
 *   - 'message' (msg: Buffer, rinfo: { address, family, port, size })
 *   - 'listening' - socket bound and ready
 *   - 'close' - socket closed
 *   - 'error' (err: Error)
 *   - 'connect' - after connect() succeeds
 *
 * APIs implemented:
 *   - dgram.createSocket(type) - Create UDP socket ('udp4' or 'udp6')
 *   - socket.bind(port, address, callback) - Bind to address/port
 *   - socket.send(msg, offset, length, port, address, callback) - Send datagram
 *   - socket.close(callback) - Close socket
 *   - socket.address() - Get bound address { address, family, port }
 *   - socket.setBroadcast(flag) - Enable/disable broadcast
 *   - socket.setMulticastTTL(ttl) - Set multicast TTL
 *   - socket.addMembership(multicastAddr, multicastInterface) - Join multicast
 *   - socket.dropMembership(multicastAddr, multicastInterface) - Leave multicast
 *   - socket.connect(port, address, callback) - Associate remote address
 *   - socket.disconnect() - Disassociate remote address
 *   - socket.ref() / socket.unref() - Event loop reference control
 */

// Forward declarations
class TsBuffer;
class TsUDPSocket;

// Context for async send operations
struct UdpSendContext {
    TsUDPSocket* socket;
    void* callback;
    uv_udp_send_t req;
    uv_buf_t buf;
    size_t length;
};

class TsUDPSocket : public TsEventEmitter {
public:
    static constexpr uint32_t MAGIC = 0x55445053; // "UDPS"

    TsUDPSocket(bool ipv6 = false);
    virtual ~TsUDPSocket();

    // Create new socket
    static TsUDPSocket* Create(const char* type);

    // Safe casting helper
    virtual TsUDPSocket* AsUDPSocket() { return this; }

    // Core methods
    void Bind(int port, const char* address, void* callback);
    void Send(void* msg, int offset, int length, int port, const char* address, void* callback);
    void Close(void* callback);

    // Returns { address, family, port } object
    void* Address();

    // Socket configuration
    void SetBroadcast(bool flag);
    void SetMulticastTTL(int ttl);
    void SetMulticastLoopback(bool flag);
    void SetMulticastInterface(const char* interfaceAddr);
    void AddMembership(const char* multicastAddr, const char* interfaceAddr);
    void DropMembership(const char* multicastAddr, const char* interfaceAddr);

    // Connected mode (optional)
    void Connect(int port, const char* address, void* callback);
    void Disconnect();

    // Handle reference control
    void Ref();
    void Unref();

    // Buffer size configuration
    void SetRecvBufferSize(int size);
    void SetSendBufferSize(int size);
    int GetRecvBufferSize();
    int GetSendBufferSize();

    // TTL configuration
    void SetTTL(int ttl);

    // Socket options (applied during bind)
    void SetReuseAddr(bool flag) { reuseAddr_ = flag; }
    void SetIPv6Only(bool flag) { ipv6Only_ = flag; }

    // EventEmitter override
    virtual void On(const char* event, void* callback) override;

    // State accessors
    bool IsBound() const { return bound_; }
    bool IsConnected() const { return connected_; }
    bool IsDestroyed() const { return destroyed_; }
    size_t GetBytesRead() const { return bytesRead_; }
    size_t GetBytesWritten() const { return bytesWritten_; }

    // Address accessors
    const char* GetLocalAddress() const { return localAddress_.c_str(); }
    int GetLocalPort() const { return localPort_; }
    const char* GetLocalFamily() const { return localFamily_.c_str(); }
    const char* GetRemoteAddress() const { return remoteAddress_.c_str(); }
    int GetRemotePort() const { return remotePort_; }

protected:
    uv_udp_t* handle_;
    bool ipv6_;
    bool bound_;
    bool connected_;
    bool receiving_;
    bool destroyed_;
    bool closing_;

    // Socket options (applied during bind)
    bool reuseAddr_;
    bool ipv6Only_;

    // Bound address info
    std::string localAddress_;
    int localPort_;
    std::string localFamily_;

    // Connected remote (optional)
    std::string remoteAddress_;
    int remotePort_;

    // Stats
    size_t bytesRead_;
    size_t bytesWritten_;

    // Callbacks
    void* closeCallback_;

    // Internal methods
    void StartReceiving();
    void StopReceiving();
    void UpdateLocalAddress();
    void HandleReceive(ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags);
    void HandleSendComplete(int status, size_t length);
    void EmitError(const char* message);
    void EmitError(int uvError);

    // Static libuv callbacks
    static void OnAlloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void OnRecv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                       const struct sockaddr* addr, unsigned flags);
    static void OnSend(uv_udp_send_t* req, int status);
    static void OnClose(uv_handle_t* handle);
};

// C API for dgram module
extern "C" {
    // dgram.createSocket(type) - type is 'udp4' or 'udp6'
    void* ts_dgram_create_socket(void* type);

    // dgram.createSocket(options) - options object form
    // type: 'udp4' or 'udp6', reuseAddr: boolean, ipv6Only: boolean,
    // recvBufferSize: number, sendBufferSize: number
    void* ts_dgram_create_socket_options(void* type, int64_t reuseAddr, int64_t ipv6Only,
                                         int64_t recvBufferSize, int64_t sendBufferSize);

    // socket.bind(port, address, callback)
    void ts_udp_socket_bind(void* socket, void* port, void* address, void* callback);

    // socket.send(msg, offset, length, port, address, callback)
    void ts_udp_socket_send(void* socket, void* msg, void* offset, void* length,
                            void* port, void* address, void* callback);

    // socket.close(callback)
    void ts_udp_socket_close(void* socket, void* callback);

    // socket.address() -> { address, family, port }
    void* ts_udp_socket_address(void* socket);

    // Multicast/Broadcast configuration
    void ts_udp_socket_set_broadcast(void* socket, void* flag);
    void ts_udp_socket_set_multicast_ttl(void* socket, void* ttl);
    void ts_udp_socket_set_multicast_loopback(void* socket, void* flag);
    void ts_udp_socket_set_multicast_interface(void* socket, void* interfaceAddr);
    void ts_udp_socket_add_membership(void* socket, void* multicastAddr, void* interfaceAddr);
    void ts_udp_socket_drop_membership(void* socket, void* multicastAddr, void* interfaceAddr);

    // Connected mode
    void ts_udp_socket_connect(void* socket, void* port, void* address, void* callback);
    void ts_udp_socket_disconnect(void* socket);

    // Handle reference control
    void ts_udp_socket_ref(void* socket);
    void ts_udp_socket_unref(void* socket);

    // Buffer size and TTL
    void ts_udp_socket_set_recv_buffer_size(void* socket, void* size);
    void ts_udp_socket_set_send_buffer_size(void* socket, void* size);
    int64_t ts_udp_socket_get_recv_buffer_size(void* socket);
    int64_t ts_udp_socket_get_send_buffer_size(void* socket);
    void ts_udp_socket_set_ttl(void* socket, void* ttl);

    // Remote address (connected mode)
    void* ts_udp_socket_remote_address(void* socket);

    // Property accessors
    void* ts_udp_socket_get_local_address(void* socket);
    int64_t ts_udp_socket_get_local_port(void* socket);
    void* ts_udp_socket_get_local_family(void* socket);
    void* ts_udp_socket_get_remote_address(void* socket);
    int64_t ts_udp_socket_get_remote_port(void* socket);
    int64_t ts_udp_socket_get_bytes_read(void* socket);
    int64_t ts_udp_socket_get_bytes_written(void* socket);
    bool ts_udp_socket_is_bound(void* socket);
    bool ts_udp_socket_is_connected(void* socket);
    bool ts_udp_socket_is_destroyed(void* socket);
}
