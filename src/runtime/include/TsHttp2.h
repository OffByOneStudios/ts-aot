#pragma once

/*
 * HTTP/2 Runtime Implementation
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
 * HTTP/2 uses nghttp2 library for protocol handling:
 * - Frame parsing and serialization
 * - HPACK header compression
 * - Stream multiplexing
 * - Flow control
 */

#include "TsString.h"
#include "TsObject.h"
#include "TsMap.h"
#include "TsArray.h"
#include "TsBuffer.h"
#include "TsEventEmitter.h"
#include "TsDuplex.h"
#include "TsSocket.h"
#include "TsSecureSocket.h"
#include "TsServer.h"
#include <nghttp2/nghttp2.h>
#include <uv.h>
#include <map>
#include <vector>
#include <string>

// Forward declarations
class TsHttp2Session;
class TsHttp2Stream;
class TsServerHttp2Session;
class TsClientHttp2Session;
class TsServerHttp2Stream;
class TsClientHttp2Stream;
class TsHttp2Server;

// =============================================================================
// HTTP/2 Settings
// =============================================================================

struct TsHttp2Settings {
    uint32_t headerTableSize = 4096;      // SETTINGS_HEADER_TABLE_SIZE
    uint32_t enablePush = 1;              // SETTINGS_ENABLE_PUSH
    uint32_t maxConcurrentStreams = 100;  // SETTINGS_MAX_CONCURRENT_STREAMS
    uint32_t initialWindowSize = 65535;   // SETTINGS_INITIAL_WINDOW_SIZE
    uint32_t maxFrameSize = 16384;        // SETTINGS_MAX_FRAME_SIZE
    uint32_t maxHeaderListSize = 65535;   // SETTINGS_MAX_HEADER_LIST_SIZE
};

// =============================================================================
// TsHttp2Session - Base session class
// =============================================================================

class TsHttp2Session : public TsEventEmitter {
public:
    static constexpr uint32_t MAGIC = 0x48325353; // "H2SS"

    // Properties
    TsString* alpnProtocol = nullptr;
    bool closed = false;
    bool connecting = false;
    bool destroyed = false;
    bool encrypted = false;
    TsHttp2Settings localSettings;
    TsHttp2Settings remoteSettings;
    TsSocket* socket = nullptr;
    int type = 0;  // 0 = server, 1 = client

    // Methods
    void Close(void* callback = nullptr);
    void Destroy(void* error = nullptr, int code = 0);
    void Goaway(int code = 0, int lastStreamId = 0, TsBuffer* data = nullptr);
    bool Ping(TsBuffer* payload = nullptr, void* callback = nullptr);
    void Ref();
    void Unref();
    void SetTimeout(int msecs, void* callback = nullptr);
    void Settings(TsHttp2Settings* settings);

    // State object (returns session state info)
    TsMap* GetState();

    // Internal: nghttp2 session handle
    nghttp2_session* session = nullptr;

    // Stream management
    TsHttp2Stream* GetStream(int32_t streamId);
    void AddStream(int32_t streamId, TsHttp2Stream* stream);
    void RemoveStream(int32_t streamId);

    // AsXxx() helpers for safe casting
    virtual TsHttp2Session* AsHttp2Session() { return this; }

    // Send pending data to socket (public so streams can call it)
    void SendPendingData();

protected:
    TsHttp2Session();
    virtual ~TsHttp2Session();

    // Initialize nghttp2 session
    void InitSession(bool isServer);

    // Process incoming data from socket
    void OnData(const uint8_t* data, size_t length);

    // nghttp2 callbacks
    static ssize_t OnSend(nghttp2_session* session, const uint8_t* data,
                          size_t length, int flags, void* user_data);
    static int OnFrameRecv(nghttp2_session* session, const nghttp2_frame* frame,
                           void* user_data);
    static int OnStreamClose(nghttp2_session* session, int32_t stream_id,
                             uint32_t error_code, void* user_data);
    static int OnBeginHeaders(nghttp2_session* session, const nghttp2_frame* frame,
                              void* user_data);
    static int OnHeader(nghttp2_session* session, const nghttp2_frame* frame,
                        const uint8_t* name, size_t namelen,
                        const uint8_t* value, size_t valuelen,
                        uint8_t flags, void* user_data);
    static int OnDataChunkRecv(nghttp2_session* session, uint8_t flags,
                               int32_t stream_id, const uint8_t* data,
                               size_t len, void* user_data);

    std::map<int32_t, TsHttp2Stream*> streams;
    uv_timer_t* timeoutTimer = nullptr;
    void* closeCallback = nullptr;
};

// =============================================================================
// TsServerHttp2Session - Server-side session
// =============================================================================

class TsServerHttp2Session : public TsHttp2Session {
public:
    static TsServerHttp2Session* Create(TsSocket* socket);

    // Server-specific methods
    void Altsvc(const char* alt, const char* origin);
    void Origin(TsArray* origins);

    // Process incoming data from socket (public wrapper for base OnData)
    void ProcessData(const uint8_t* data, size_t length) { OnData(data, length); }

protected:
    TsServerHttp2Session();
    void Init(TsSocket* socket);
};

// =============================================================================
// TsClientHttp2Session - Client-side session
// =============================================================================

class TsClientHttp2Session : public TsHttp2Session {
public:
    static TsClientHttp2Session* Create(const char* authority, TsValue* options);

    // Client-specific methods
    TsClientHttp2Stream* Request(TsMap* headers, TsValue* options = nullptr);

    // Socket event handlers (called by static callbacks)
    void OnSocketConnect();
    void OnSocketData(const uint8_t* data, size_t length);
    void OnSocketClose();

protected:
    TsClientHttp2Session();
    void Init(const char* authority, TsValue* options);

    std::string authority;
};

// =============================================================================
// TsHttp2Stream - Base stream class (extends Duplex)
// =============================================================================

class TsHttp2Stream : public TsDuplex {
public:
    static constexpr uint32_t MAGIC = 0x48325354; // "H2ST"

    // Properties
    bool aborted = false;
    size_t bufferSize = 0;
    bool closed = false;
    bool destroyed = false;
    bool endAfterHeaders = false;
    int32_t id = 0;
    bool pending = true;
    int rstCode = 0;
    TsMap* sentHeaders = nullptr;
    TsArray* sentInfoHeaders = nullptr;
    TsMap* sentTrailers = nullptr;
    TsHttp2Session* session = nullptr;

    // Methods
    void Close(int code = 0, void* callback = nullptr);
    void Priority(TsValue* options);
    void SetTimeout(int msecs, void* callback = nullptr);

    // State object
    TsMap* GetState();

    // TsDuplex implementation
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;
    virtual void Pause() override;
    virtual void Resume() override;

    // AsXxx() helpers
    virtual TsHttp2Stream* AsHttp2Stream() { return this; }

protected:
    TsHttp2Stream(TsHttp2Session* session, int32_t streamId);
    virtual ~TsHttp2Stream();

    // Internal: receive data from session
    void OnData(const uint8_t* data, size_t length);
    void OnHeaders(TsMap* headers, uint8_t flags);
    void OnTrailers(TsMap* trailers);
    void OnEnd();
    void OnError(int code);

    uv_timer_t* timeoutTimer = nullptr;
    void* closeCallback = nullptr;

    friend class TsHttp2Session;
};

// =============================================================================
// TsServerHttp2Stream - Server-side stream
// =============================================================================

class TsServerHttp2Stream : public TsHttp2Stream {
public:
    // Properties
    bool headersSent = false;
    bool pushAllowed = true;

    // Methods
    void AdditionalHeaders(TsMap* headers);
    void PushStream(TsMap* headers, TsValue* options, void* callback);
    void Respond(TsMap* headers = nullptr, TsValue* options = nullptr);
    void RespondWithFD(int fd, TsMap* headers = nullptr, TsValue* options = nullptr);
    void RespondWithFile(const char* path, TsMap* headers = nullptr, TsValue* options = nullptr);

    static TsServerHttp2Stream* Create(TsHttp2Session* session, int32_t streamId);

protected:
    TsServerHttp2Stream(TsHttp2Session* session, int32_t streamId);
};

// =============================================================================
// TsClientHttp2Stream - Client-side stream
// =============================================================================

class TsClientHttp2Stream : public TsHttp2Stream {
public:
    // Events: 'continue', 'headers', 'push', 'response'

    static TsClientHttp2Stream* Create(TsHttp2Session* session, int32_t streamId);

protected:
    TsClientHttp2Stream(TsHttp2Session* session, int32_t streamId);
};

// =============================================================================
// TsHttp2Server - HTTP/2 Server
// =============================================================================

class TsHttp2Server : public TsServer {
public:
    static constexpr uint32_t MAGIC = 0x48325356; // "H2SV"
    static TsHttp2Server* Create(TsValue* options, void* callback);

    // Stream callback
    void* streamCallback = nullptr;

protected:
    TsHttp2Server();
    virtual void HandleConnection(int status) override;
};

// =============================================================================
// TsHttp2SecureServer - HTTPS/2 Server (HTTP/2 over TLS)
// =============================================================================

class TsHttp2SecureServer : public TsHttp2Server {
public:
    static TsHttp2SecureServer* Create(TsValue* options, void* callback);

protected:
    TsHttp2SecureServer(TsValue* options);
    virtual void HandleConnection(int status) override;

    TsValue* tlsOptions = nullptr;
};

// =============================================================================
// C API - Module Functions
// =============================================================================

extern "C" {
    // Module functions
    void* ts_http2_create_server(void* options, void* callback);
    void* ts_http2_create_secure_server(void* options, void* callback);
    void* ts_http2_connect(void* authority, void* options, void* callback);
    void* ts_http2_get_default_settings();
    void* ts_http2_get_packed_settings(void* settings);
    void* ts_http2_get_unpacked_settings(void* buffer);
    void* ts_http2_get_constants();

    // Session property getters
    void* ts_http2_session_get_alpn_protocol(void* session);
    bool ts_http2_session_get_closed(void* session);
    bool ts_http2_session_get_connecting(void* session);
    bool ts_http2_session_get_destroyed(void* session);
    bool ts_http2_session_get_encrypted(void* session);
    void* ts_http2_session_get_local_settings(void* session);
    void* ts_http2_session_get_remote_settings(void* session);
    void* ts_http2_session_get_socket(void* session);
    int64_t ts_http2_session_get_type(void* session);
    void* ts_http2_session_get_state(void* session);

    // Session methods
    void ts_http2_session_close(void* session, void* callback);
    void ts_http2_session_destroy(void* session, void* error, int64_t code);
    void ts_http2_session_goaway(void* session, int64_t code, int64_t lastStreamId, void* data);
    bool ts_http2_session_ping(void* session, void* payload, void* callback);
    void ts_http2_session_ref(void* session);
    void ts_http2_session_unref(void* session);
    void ts_http2_session_set_timeout(void* session, int64_t msecs, void* callback);
    void ts_http2_session_settings(void* session, void* settings);

    // ServerHttp2Session specific
    void ts_http2_server_session_altsvc(void* session, void* alt, void* origin);
    void ts_http2_server_session_origin(void* session, void* origins);

    // ClientHttp2Session specific
    void* ts_http2_client_session_request(void* session, void* headers, void* options);

    // Stream property getters
    bool ts_http2_stream_get_aborted(void* stream);
    int64_t ts_http2_stream_get_buffer_size(void* stream);
    bool ts_http2_stream_get_closed(void* stream);
    bool ts_http2_stream_get_destroyed(void* stream);
    bool ts_http2_stream_get_end_after_headers(void* stream);
    int64_t ts_http2_stream_get_id(void* stream);
    bool ts_http2_stream_get_pending(void* stream);
    int64_t ts_http2_stream_get_rst_code(void* stream);
    void* ts_http2_stream_get_sent_headers(void* stream);
    void* ts_http2_stream_get_sent_info_headers(void* stream);
    void* ts_http2_stream_get_sent_trailers(void* stream);
    void* ts_http2_stream_get_session(void* stream);
    void* ts_http2_stream_get_state(void* stream);

    // Stream methods
    void ts_http2_stream_close(void* stream, int64_t code, void* callback);
    void ts_http2_stream_priority(void* stream, void* options);
    void ts_http2_stream_set_timeout(void* stream, int64_t msecs, void* callback);

    // ServerHttp2Stream specific
    bool ts_http2_server_stream_get_headers_sent(void* stream);
    bool ts_http2_server_stream_get_push_allowed(void* stream);
    void ts_http2_server_stream_additional_headers(void* stream, void* headers);
    void ts_http2_server_stream_push_stream(void* stream, void* headers, void* options, void* callback);
    void ts_http2_server_stream_respond(void* stream, void* headers, void* options);
    void ts_http2_server_stream_respond_with_fd(void* stream, int64_t fd, void* headers, void* options);
    void ts_http2_server_stream_respond_with_file(void* stream, void* path, void* headers, void* options);
}

// =============================================================================
// HTTP/2 Constants
// =============================================================================

// nghttp2 error codes exposed to JavaScript
namespace Http2Constants {
    // Error codes
    constexpr int NGHTTP2_NO_ERROR = 0x0;
    constexpr int NGHTTP2_PROTOCOL_ERROR = 0x1;
    constexpr int NGHTTP2_INTERNAL_ERROR = 0x2;
    constexpr int NGHTTP2_FLOW_CONTROL_ERROR = 0x3;
    constexpr int NGHTTP2_SETTINGS_TIMEOUT = 0x4;
    constexpr int NGHTTP2_STREAM_CLOSED = 0x5;
    constexpr int NGHTTP2_FRAME_SIZE_ERROR = 0x6;
    constexpr int NGHTTP2_REFUSED_STREAM = 0x7;
    constexpr int NGHTTP2_CANCEL = 0x8;
    constexpr int NGHTTP2_COMPRESSION_ERROR = 0x9;
    constexpr int NGHTTP2_CONNECT_ERROR = 0xa;
    constexpr int NGHTTP2_ENHANCE_YOUR_CALM = 0xb;
    constexpr int NGHTTP2_INADEQUATE_SECURITY = 0xc;
    constexpr int NGHTTP2_HTTP_1_1_REQUIRED = 0xd;

    // Settings identifiers
    constexpr int NGHTTP2_SETTINGS_HEADER_TABLE_SIZE = 0x1;
    constexpr int NGHTTP2_SETTINGS_ENABLE_PUSH = 0x2;
    constexpr int NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS = 0x3;
    constexpr int NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE = 0x4;
    constexpr int NGHTTP2_SETTINGS_MAX_FRAME_SIZE = 0x5;
    constexpr int NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE = 0x6;

    // Session types
    constexpr int NGHTTP2_SESSION_SERVER = 0;
    constexpr int NGHTTP2_SESSION_CLIENT = 1;

    // Default settings
    constexpr int DEFAULT_SETTINGS_HEADER_TABLE_SIZE = 4096;
    constexpr int DEFAULT_SETTINGS_ENABLE_PUSH = 1;
    constexpr int DEFAULT_SETTINGS_MAX_CONCURRENT_STREAMS = 100;
    constexpr int DEFAULT_SETTINGS_INITIAL_WINDOW_SIZE = 65535;
    constexpr int DEFAULT_SETTINGS_MAX_FRAME_SIZE = 16384;
    constexpr int DEFAULT_SETTINGS_MAX_HEADER_LIST_SIZE = 65535;
}
