#ifndef TS_NET_EXT_H
#define TS_NET_EXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Socket Functions
// =============================================================================

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

// Handle reference control
void* ts_net_socket_ref(void* socket);
void* ts_net_socket_unref(void* socket);

// =============================================================================
// Server Functions
// =============================================================================

void* ts_net_create_server(void* callback);
void ts_net_server_listen(void* server, void* port, void* host, void* callback);
void ts_net_server_close(void* server);
void* ts_net_server_address(void* server);
void* ts_net_server_ref(void* server);
void* ts_net_server_unref(void* server);
void ts_net_server_get_connections(void* server, void* callback);
int64_t ts_net_server_get_maxConnections(void* server);
void ts_net_server_set_maxConnections(void* server, void* value);

// =============================================================================
// Module Utilities
// =============================================================================

int64_t ts_net_is_ip(void* input);
bool ts_net_is_ipv4(void* input);
bool ts_net_is_ipv6(void* input);
bool ts_net_get_default_auto_select_family();
void ts_net_set_default_auto_select_family(bool value);
int64_t ts_net_get_default_auto_select_family_attempt_timeout();
void ts_net_set_default_auto_select_family_attempt_timeout(int64_t value);

// =============================================================================
// SocketAddress Class
// =============================================================================

void* ts_net_socket_address_create(void* options);
void* ts_net_socket_address_parse(void* input);
void* ts_net_socket_address_get_address(void* addr);
void* ts_net_socket_address_get_family(void* addr);
int64_t ts_net_socket_address_get_flowlabel(void* addr);
int64_t ts_net_socket_address_get_port(void* addr);

// =============================================================================
// BlockList Class
// =============================================================================

void* ts_net_block_list_create();
bool ts_net_block_list_is_block_list(void* value);
void ts_net_block_list_add_address(void* blockList, void* address, void* type);
void ts_net_block_list_add_range(void* blockList, void* start, void* end, void* type);
void ts_net_block_list_add_subnet(void* blockList, void* net, int64_t prefix, void* type);
bool ts_net_block_list_check(void* blockList, void* address, void* type);
void* ts_net_block_list_get_rules(void* blockList);
void* ts_net_block_list_to_json(void* blockList);
void ts_net_block_list_from_json(void* blockList, void* value);

#ifdef __cplusplus
}
#endif

#endif // TS_NET_EXT_H
