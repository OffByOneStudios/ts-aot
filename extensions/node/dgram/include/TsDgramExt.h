#ifndef TS_DGRAM_EXT_H
#define TS_DGRAM_EXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif // TS_DGRAM_EXT_H
