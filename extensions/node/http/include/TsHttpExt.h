#ifndef TS_HTTP_EXT_H
#define TS_HTTP_EXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OutgoingMessage header methods
void ts_outgoing_message_set_header(void* msg, void* name, void* value);
void* ts_outgoing_message_get_header(void* msg, void* name);
void* ts_outgoing_message_get_headers(void* msg);
bool ts_outgoing_message_has_header(void* msg, void* name);
void ts_outgoing_message_remove_header(void* msg, void* name);
void* ts_outgoing_message_get_header_names(void* msg);
void ts_outgoing_message_flush_headers(void* msg);

// Server creation
void* ts_http_create_server(void* options, void* callback);
void* ts_https_create_server(void* options, void* callback);
void ts_http_server_listen(void* server, void* port_val, void* host, void* callback);

// ServerResponse methods
void ts_http_server_response_write_head(void* res, int64_t status, void* headers);
bool ts_http_server_response_write(void* res, void* data);
void ts_http_server_response_end(void* res, void* data);

// IncomingMessage property getters
void* ts_incoming_message_url(void* ctx, void* msg);
void* ts_incoming_message_method(void* ctx, void* msg);
void* ts_incoming_message_headers(void* ctx, void* msg);
void* ts_incoming_message_statusCode(void* ctx, void* msg);
void* ts_incoming_message_statusMessage(void* ctx, void* msg);
void* ts_incoming_message_httpVersion(void* ctx, void* msg);
bool ts_incoming_message_complete(void* ctx, void* msg);
void* ts_incoming_message_rawHeaders(void* ctx, void* msg);
void* ts_incoming_message_rawTrailers(void* ctx, void* msg);
void* ts_incoming_message_trailers(void* ctx, void* msg);
void* ts_incoming_message_socket(void* ctx, void* msg);
bool ts_incoming_message_aborted(void* ctx, void* msg);

// OutgoingMessage property getters
bool ts_outgoing_message_get_headers_sent(void* msg);
bool ts_outgoing_message_get_writable_ended(void* msg);
bool ts_outgoing_message_get_writable_finished(void* msg);

// ServerResponse property getters/setters
int64_t ts_server_response_get_status_code(void* res);
void* ts_server_response_get_status_message(void* res);
void ts_server_response_set_status_code(void* res, int64_t code);
void ts_server_response_set_status_message(void* res, void* msg);
void ts_server_response_set_timeout(void* res, int64_t msecs, void* callback);
void ts_server_response_add_trailers(void* res, void* trailers);

// ClientRequest property getters/setters
void* ts_client_request_get_path(void* req);
void* ts_client_request_get_method(void* req);
void* ts_client_request_get_host(void* req);
void* ts_client_request_get_protocol(void* req);
void* ts_client_request_get_header(void* req, void* name);
void ts_client_request_set_header(void* req, void* name, void* value);
void* ts_client_request_get_socket(void* req);
void ts_client_request_set_timeout(void* req, int64_t msecs, void* callback);
void ts_client_request_set_no_delay(void* req, int64_t noDelay);
void ts_client_request_set_socket_keep_alive(void* req, int64_t enable, int64_t initialDelay);
bool ts_client_request_get_reused_socket(void* req);
int64_t ts_client_request_get_max_headers_count(void* req);
void ts_client_request_set_max_headers_count(void* req, int64_t count);
void* ts_client_request_get_raw_header_names(void* req);

// HTTP request functions
void* ts_http_request(void* options, void* callback);
void* ts_http_get(void* options, void* callback);
void* ts_https_request(void* options, void* callback);
void* ts_https_get(void* options, void* callback);

// HTTP module utilities
void* ts_http_get_methods();
void* ts_http_get_status_codes();
int64_t ts_http_get_max_header_size();
void ts_http_validate_header_name(void* name);
void ts_http_validate_header_value(void* name, void* value);

// Agent functions
void* ts_http_agent_create(void* options);
void* ts_https_agent_create(void* options);
void* ts_http_get_global_agent();
void* ts_https_get_global_agent();
void ts_http_agent_destroy(void* agent);
int64_t ts_http_get_max_idle_http_parsers();
void ts_http_set_max_idle_http_parsers(int64_t max);

// Server timeout/configuration
void ts_http_server_set_timeout(void* server, int64_t msecs, void* callback);
int64_t ts_http_server_get_timeout(void* server);
int64_t ts_http_server_get_keep_alive_timeout(void* server);
void ts_http_server_set_keep_alive_timeout(void* server, int64_t msecs);
int64_t ts_http_server_get_headers_timeout(void* server);
void ts_http_server_set_headers_timeout(void* server, int64_t msecs);
int64_t ts_http_server_get_request_timeout(void* server);
void ts_http_server_set_request_timeout(void* server, int64_t msecs);
int64_t ts_http_server_get_max_headers_count(void* server);
void ts_http_server_set_max_headers_count(void* server, int64_t count);

// CloseEvent (WebSocket)
void* ts_close_event_create(int64_t code, void* reason);
void* ts_close_event_get_code(void* event);
void* ts_close_event_get_reason(void* event);
void* ts_close_event_get_was_clean(void* event);

// MessageEvent (WebSocket)
void* ts_message_event_create();
void* ts_message_event_get_data(void* event);
void ts_message_event_set_data(void* event, void* data);
void* ts_message_event_get_origin(void* event);
void* ts_message_event_get_last_event_id(void* event);
void* ts_message_event_get_source(void* event);
void* ts_message_event_get_ports(void* event);

// WebSocket
void* ts_websocket_create(void* url, void* protocols);
void ts_websocket_send(void* ws, void* data);
void ts_websocket_close(void* ws, int64_t code, void* reason);
void ts_websocket_ping(void* ws, void* data);
void ts_websocket_pong(void* ws, void* data);
int64_t ts_websocket_get_ready_state(void* ws);
void* ts_websocket_get_url(void* ws);
void* ts_websocket_get_protocol(void* ws);
void* ts_websocket_get_extensions(void* ws);
int64_t ts_websocket_get_buffered_amount(void* ws);
void* ts_websocket_get_binary_type(void* ws);
void ts_websocket_set_binary_type(void* ws, void* type);
void ts_websocket_set_onopen(void* ws, void* callback);
void ts_websocket_set_onmessage(void* ws, void* callback);
void ts_websocket_set_onclose(void* ws, void* callback);
void ts_websocket_set_onerror(void* ws, void* callback);
void* ts_websocket_get_onopen(void* ws);
void* ts_websocket_get_onmessage(void* ws);
void* ts_websocket_get_onclose(void* ws);
void* ts_websocket_get_onerror(void* ws);
int64_t ts_websocket_connecting();
int64_t ts_websocket_open();
int64_t ts_websocket_closing();
int64_t ts_websocket_closed();

#ifdef __cplusplus
}
#endif

#endif // TS_HTTP_EXT_H
