#pragma once

/*
 * ⚠️ HTTP RUNTIME DEVELOPMENT GUIDE ⚠️
 * 
 * Before editing this file, read: .github/instructions/runtime-extensions.instructions.md
 * 
 * CRITICAL RULES:
 * 1. Memory: Use ts_alloc() for GC objects, NOT new/malloc
 * 2. Strings: Use TsString::Create(), NOT std::string
 * 3. Casting: Use AsXxx() or dynamic_cast, NOT C-style casts
 * 4. Boxing: Use ts_value_get_object() to unbox void* params
 * 5. Errors: ts_error_create() returns ALREADY-BOXED TsValue*
 * 
 * ADDING NEW PROPERTIES:
 * 1. Add field to class (e.g., int statusCode = 0;)
 * 2. Set in callback (e.g., on_headers_complete)
 * 3. Handle in ts_object_get_property() using dynamic_cast
 * 4. Add extern "C" getter if needed for codegen
 */

#include "TsString.h"
#include "TsObject.h"
#include "TsMap.h"
#include "TsBuffer.h"
#include "TsFetch.h"
#include "TsReadable.h"
#include "TsWritable.h"
#include "TsServer.h"
#include "TsSocket.h"
#include <uv.h>
#include <llhttp.h>
#include <vector>
#include <map>
#include <string>

class TsSocket;

class TsIncomingMessage : public TsReadable {
public:
    static constexpr uint32_t MAGIC = 0x494E434D; // "INCM"
    static uint64_t s_vtable;  // Primary vtable pointer for type identification
    static TsIncomingMessage* Create();

    TsString* method;
    TsString* url;
    TsHeaders* headers;
    int statusCode = 0;  // For HTTP client responses
    TsString* statusMessage = nullptr;  // For HTTP client responses
    TsString* httpVersion = nullptr;  // HTTP version string (e.g., "1.1")
    bool complete = false;  // True when message has been fully received
    bool aborted = false;  // True when request was aborted by client
    TsArray* rawHeaders = nullptr;  // Alternating key/value array of raw headers
    TsArray* rawTrailers = nullptr;  // Alternating key/value array of raw trailer headers
    TsMap* trailers = nullptr;  // Object with trailer headers (lowercase keys)
    TsSocket* socket = nullptr;  // Underlying socket connection

    // Virtual property dispatch for nodecore decoupling
    TsValue GetPropertyVirtual(const char* key) override;

    // TsReadable implementation
    virtual void Pause() override;
    virtual void Resume() override;

private:
    TsIncomingMessage();
};

// Base class for ServerResponse and ClientRequest (matches Node.js http.OutgoingMessage)
class TsOutgoingMessage : public TsWritable {
public:
    static constexpr uint32_t MAGIC = 0x4F55544D; // "OUTM"
    
    // Common properties
    bool headersSent = false;
    TsMap* headers = nullptr;
    bool writableEnded = false;
    bool writableFinished = false;
    bool destroyed = false;
    
    // Common methods
    void SetHeader(TsString* name, TsValue* value);
    TsValue* GetHeader(TsString* name);
    void* GetHeaders();
    bool HasHeader(TsString* name);
    void RemoveHeader(TsString* name);
    void AppendHeader(TsString* name, TsValue* value);
    TsArray* GetHeaderNames();
    TsArray* GetRawHeaderNames();
    void FlushHeaders();
    
    // Property access
    virtual TsValue GetPropertyVirtual(const char* key) override;

    // TsWritable implementation (to be overridden by subclasses)
    virtual bool Write(void* data, size_t length) override { return false; }
    virtual void End() override {}

protected:
    TsOutgoingMessage();
};

class TsServerResponse : public TsOutgoingMessage {
public:
    static constexpr uint32_t MAGIC = 0x53524553; // "SRES"
    static uint64_t s_vtable;  // Primary vtable pointer for type identification
    static TsServerResponse* Create(TsSocket* socket);

    // Dynamic property access for untyped JS modules
    TsValue GetPropertyVirtual(const char* key) override;

    void WriteHead(int status, TsObject* headers);

    // TsWritable implementation
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;
    void End(TsValue data);

    // ServerResponse-specific properties
    int statusCode = 200;
    TsString* statusMessage = nullptr;
    bool hasContentLength = false;  // Set by WriteHead; controls chunked vs non-chunked

    // Trailer support
    void AddTrailers(TsMap* trailers);
    TsMap* pendingTrailers = nullptr;  // Trailers to be sent after body

    // Timeout support
    void SetTimeout(int msecs, void* callback = nullptr);
    void* timeoutCallback = nullptr;
    uv_timer_t* timeoutTimer = nullptr;

    // Get underlying socket
    TsSocket* GetSocket() { return socket; }

private:
    TsServerResponse(TsSocket* socket);
    TsSocket* socket;

    static void OnTimeout(uv_timer_t* handle);
};

class TsHttpServer : public TsServer {
public:
    static constexpr uint32_t MAGIC = 0x48535256; // "HSRV"
    static TsHttpServer* Create(TsValue* options, void* callback);

    // Timeout configuration (in milliseconds)
    int timeout = 0;                // Socket timeout (0 = no timeout)
    int keepAliveTimeout = 5000;    // Keep-alive timeout (default: 5 seconds)
    int headersTimeout = 60000;     // Headers receive timeout (default: 60 seconds)
    int requestTimeout = 0;         // Request body timeout (0 = no timeout)
    int maxHeadersCount = 2000;     // Maximum number of headers

    // Set timeout for all sockets
    void SetTimeout(int msecs, void* callback = nullptr);
    void* timeoutCallback = nullptr;

    // Direct request callback — stored separately from EventEmitter
    // to avoid NaN-boxing/GC issues with event dispatch
    void* requestCallback = nullptr;

    // Dynamic property access for untyped JS modules
    TsValue GetPropertyVirtual(const char* key) override;

protected:
    TsHttpServer();
};

class TsHttpsServer : public TsHttpServer {
public:
    static TsHttpsServer* Create(TsValue* options, void* callback);

protected:
    virtual void HandleConnection(int status) override;

private:
    TsHttpsServer(TsValue* options);
    TsValue* options;
};

class TsClientRequest : public TsOutgoingMessage {
public:
    static constexpr uint32_t MAGIC = 0x43524551; // "CREQ"
    static TsClientRequest* Create(TsValue* options, void* callback, bool is_https = false);

    // TsWritable implementation
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;
    void End(TsValue data);

    TsSocket* socket;
    llhttp_t parser;
    llhttp_settings_t settings;
    TsIncomingMessage* response;
    void* callback;
    bool connected = false;
    bool endCalled = false;
    std::string method = "GET";
    std::string host = "localhost";
    int port = 80;
    std::string path = "/";
    TsString* currentHeaderField = nullptr;
    TsString* currentHeaderFieldRaw = nullptr;  // Unmodified header field for rawHeaders
    bool is_https = false;
    TsValue options;
    class TsHttpAgent* agent = nullptr;  // Agent for connection pooling (forward declare)
    bool socketFromAgent = false;        // True if socket was reused from agent
    std::string agentKey;                // Key used for agent socket pool

    void SendHeaders();
    virtual void Connect();
    void ReturnSocketToAgent();          // Return socket to agent pool after request

    // Timeout support
    void SetTimeout(int msecs, void* callback = nullptr);
    void* timeoutCallback = nullptr;
    uv_timer_t* timeoutTimer = nullptr;

    // Socket configuration
    void SetNoDelay(bool noDelay);
    void SetSocketKeepAlive(bool enable, int initialDelay = 0);

    // Additional properties
    bool reusedSocket = false;           // True if socket was reused from pool
    int maxHeadersCount = 2000;          // Maximum number of headers

protected:
    TsClientRequest(TsValue* options, void* callback, bool is_https = false);

    static void OnTimeout(uv_timer_t* handle);
};

// HTTP Agent for connection pooling
class TsHttpAgent : public TsEventEmitter {
public:
    static constexpr uint32_t MAGIC = 0x41475448; // "AGTH"
    static TsHttpAgent* Create(TsValue* options);

    // Agent options
    bool keepAlive = false;         // Keep sockets around for reuse
    int keepAliveMsecs = 1000;      // Initial delay for TCP keepalive
    int maxSockets = 256;           // Max concurrent sockets per origin (Infinity in Node.js)
    int maxTotalSockets = 256;      // Max total sockets
    int maxFreeSockets = 256;       // Max sockets to leave open in free state
    int timeout = 0;                // Socket timeout in ms (0 = no timeout)
    std::string scheduling = "lifo"; // "lifo" or "fifo"

    // Socket pools - keyed by "host:port"
    std::map<std::string, std::vector<TsSocket*>> freeSockets;
    std::map<std::string, std::vector<TsSocket*>> sockets;
    std::map<std::string, std::vector<TsClientRequest*>> requests; // pending requests

    // Methods
    TsSocket* GetFreeSocket(const std::string& key);
    void AddSocket(const std::string& key, TsSocket* socket);
    void RemoveSocket(const std::string& key, TsSocket* socket);
    void ReuseSocket(const std::string& key, TsSocket* socket);
    void Destroy();
    std::string GetName(const std::string& host, int port, const std::string& localAddress = "");

protected:
    TsHttpAgent(TsValue* options);
};

// HTTPS Agent (extends HTTP Agent)
class TsHttpsAgent : public TsHttpAgent {
public:
    static constexpr uint32_t MAGIC = 0x41475453; // "AGTS"
    static TsHttpsAgent* Create(TsValue* options);

protected:
    TsHttpsAgent(TsValue* options);
};

// Global agent instances
extern TsHttpAgent* globalHttpAgent;
extern TsHttpsAgent* globalHttpsAgent;

extern "C" {
    void* ts_http_create_server(TsValue* options, void* callback);
    void* ts_https_create_server(TsValue* options, void* callback);
    void ts_http_server_listen(void* server, void* port_val, void* host, void* callback);
    void ts_http_server_response_write_head(void* res, int64_t status, TsValue* headers);
    bool ts_http_server_response_write(void* res, void* data);
    void ts_http_server_response_end(void* res, void* data);

    void* ts_http_request(TsValue* options, void* callback);
    void* ts_http_get(TsValue* options, void* callback);
    void* ts_https_request(TsValue* options, void* callback);
    void* ts_https_get(TsValue* options, void* callback);

    // http module constants and utilities
    void* ts_http_get_methods();
    void* ts_http_get_status_codes();
    int64_t ts_http_get_max_header_size();
    void ts_http_validate_header_name(void* name);
    void ts_http_validate_header_value(void* name, void* value);
    
    // Agent API
    void* ts_http_agent_create(TsValue* options);
    void* ts_https_agent_create(TsValue* options);
    void* ts_http_get_global_agent();
    void* ts_https_get_global_agent();
    void ts_http_agent_destroy(void* agent);
    int64_t ts_http_get_max_idle_http_parsers();
    void ts_http_set_max_idle_http_parsers(int64_t max);
    
    // OutgoingMessage methods
    void ts_outgoing_message_set_header(void* msg, void* name, void* value);
    void* ts_outgoing_message_get_header(void* msg, void* name);
    void* ts_outgoing_message_get_headers(void* msg);
    bool ts_outgoing_message_has_header(void* msg, void* name);
    void ts_outgoing_message_remove_header(void* msg, void* name);
    void* ts_outgoing_message_get_header_names(void* msg);
    void ts_outgoing_message_flush_headers(void* msg);

    // IncomingMessage property getters
    void* ts_incoming_message_httpVersion(void* ctx, void* msg);
    bool ts_incoming_message_complete(void* ctx, void* msg);
    void* ts_incoming_message_rawHeaders(void* ctx, void* msg);

    // ServerResponse property getters/setters
    int64_t ts_server_response_get_status_code(void* res);
    void* ts_server_response_get_status_message(void* res);
    void ts_server_response_set_status_code(void* res, int64_t code);
    void ts_server_response_set_status_message(void* res, void* msg);

    // ClientRequest property getters
    void* ts_client_request_get_path(void* req);
    void* ts_client_request_get_method(void* req);
    void* ts_client_request_get_host(void* req);
    void* ts_client_request_get_protocol(void* req);
    void* ts_client_request_get_header(void* req, void* name);
    void ts_client_request_set_header(void* req, void* name, void* value);
    void* ts_client_request_get_socket(void* req);

    // Server timeout configuration
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

    // ServerResponse timeout
    void ts_server_response_set_timeout(void* res, int64_t msecs, void* callback);

    // ServerResponse trailers
    void ts_server_response_add_trailers(void* res, void* trailers);

    // IncomingMessage trailers
    void* ts_incoming_message_rawTrailers(void* ctx, void* msg);
    void* ts_incoming_message_trailers(void* ctx, void* msg);

    // IncomingMessage socket and aborted
    void* ts_incoming_message_socket(void* ctx, void* msg);
    bool ts_incoming_message_aborted(void* ctx, void* msg);

    // ClientRequest timeout and socket config
    void ts_client_request_set_timeout(void* req, int64_t msecs, void* callback);
    void ts_client_request_set_no_delay(void* req, int64_t noDelay);
    void ts_client_request_set_socket_keep_alive(void* req, int64_t enable, int64_t initialDelay);
    bool ts_client_request_get_reused_socket(void* req);
    int64_t ts_client_request_get_max_headers_count(void* req);
    void ts_client_request_set_max_headers_count(void* req, int64_t count);
    void* ts_client_request_get_raw_header_names(void* req);
}

// HTTP CloseEvent class (for WebSocket and other close events)
class TsCloseEvent : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x434C4F53; // "CLOS"
    static TsCloseEvent* Create(int64_t code, TsString* reason);
    
    int64_t code = 1000;        // Close code (1000 = normal)
    TsString* reason = nullptr; // Close reason message
    bool wasClean = true;       // Whether the connection closed cleanly
};

// HTTP MessageEvent class (for WebSocket and SSE messages)
class TsMessageEvent : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x4D534745; // "MSGE"
    static TsMessageEvent* Create();
    
    void* data = nullptr;           // The message data (any type)
    TsString* origin = nullptr;     // Origin of the message
    TsString* lastEventId = nullptr; // Last event ID (for SSE)
    TsArray* ports = nullptr;       // Message ports (for postMessage)
    TsObject* source = nullptr;     // Source window/worker
};

extern "C" {
    // CloseEvent functions
    TsValue* ts_close_event_create(int64_t code, void* reason);
    void* ts_close_event_get_code(void* event);
    void* ts_close_event_get_reason(void* event);
    void* ts_close_event_get_was_clean(void* event);
    
    // MessageEvent functions
    TsValue* ts_message_event_create();
    void* ts_message_event_get_data(void* event);
    void ts_message_event_set_data(void* event, void* data);
    void* ts_message_event_get_origin(void* event);
    void* ts_message_event_get_last_event_id(void* event);
    void* ts_message_event_get_source(void* event);
    void* ts_message_event_get_ports(void* event);
}

// WebSocket ready states (RFC 6455)
enum WebSocketReadyState {
    WS_CONNECTING = 0,
    WS_OPEN = 1,
    WS_CLOSING = 2,
    WS_CLOSED = 3
};

// WebSocket opcodes (RFC 6455)
enum WebSocketOpcode {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
};

// WebSocket class (browser-compatible, Node.js http.WebSocket)
class TsWebSocket : public TsEventEmitter {
public:
    static constexpr uint32_t MAGIC = 0x57534F43; // "WSOC"
    static TsWebSocket* Create(TsString* url, TsValue* protocols = nullptr);
    
    // Properties (browser API)
    TsString* url = nullptr;              // The URL of the WebSocket
    TsString* protocol = nullptr;         // Selected sub-protocol
    TsString* extensions = nullptr;       // Selected extensions
    WebSocketReadyState readyState = WS_CONNECTING;
    int64_t bufferedAmount = 0;           // Bytes queued but not yet sent
    TsString* binaryType = nullptr;       // "blob" or "arraybuffer" (default: "blob")
    
    // Event handler properties (browser API)
    void* onopen = nullptr;
    void* onmessage = nullptr;
    void* onclose = nullptr;
    void* onerror = nullptr;
    
    // Methods
    void Send(TsValue* data);
    void Close(int64_t code = 1000, TsString* reason = nullptr);
    void Ping(TsValue* data = nullptr);
    void Pong(TsValue* data = nullptr);
    
    // Internal methods
    void HandleData(const uint8_t* data, size_t length);
    void HandleClose();
    void HandleError(TsString* error);
    
private:
    TsWebSocket(TsString* url, TsValue* protocols);
    
    void PerformHandshake();
    void SendFrame(WebSocketOpcode opcode, const uint8_t* data, size_t length);
    bool ParseFrame(const uint8_t* data, size_t length, size_t& bytesConsumed);
    TsString* GenerateSecWebSocketKey();
    TsString* ComputeSecWebSocketAccept(TsString* key);
    
    TsSocket* socket = nullptr;
    TsString* secWebSocketKey = nullptr;
    std::vector<uint8_t> receiveBuffer;
    std::string requestedProtocols;
    bool handshakeComplete = false;
    bool isMasking = true;  // Client frames must be masked
};

extern "C" {
    // WebSocket API
    void* ts_websocket_create(void* url, void* protocols);
    void ts_websocket_send(void* ws, void* data);
    void ts_websocket_close(void* ws, int64_t code, void* reason);
    void ts_websocket_ping(void* ws, void* data);
    void ts_websocket_pong(void* ws, void* data);
    
    // WebSocket property getters
    int64_t ts_websocket_get_ready_state(void* ws);
    void* ts_websocket_get_url(void* ws);
    void* ts_websocket_get_protocol(void* ws);
    void* ts_websocket_get_extensions(void* ws);
    int64_t ts_websocket_get_buffered_amount(void* ws);
    void* ts_websocket_get_binary_type(void* ws);
    void ts_websocket_set_binary_type(void* ws, void* type);
    
    // WebSocket event handler setters and getters
    void ts_websocket_set_onopen(void* ws, void* callback);
    void ts_websocket_set_onmessage(void* ws, void* callback);
    void ts_websocket_set_onclose(void* ws, void* callback);
    void ts_websocket_set_onerror(void* ws, void* callback);
    void* ts_websocket_get_onopen(void* ws);
    void* ts_websocket_get_onmessage(void* ws);
    void* ts_websocket_get_onclose(void* ws);
    void* ts_websocket_get_onerror(void* ws);
    
    // WebSocket constants
    int64_t ts_websocket_connecting();
    int64_t ts_websocket_open();
    int64_t ts_websocket_closing();
    int64_t ts_websocket_closed();
}
