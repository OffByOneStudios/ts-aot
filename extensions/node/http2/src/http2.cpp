// HTTP/2 module extension - extern "C" wrappers
// Class implementations stay in tsruntime (TsHttp2Server, TsHttp2Session, TsHttp2Stream, etc.)

#include "TsHttp2.h"
#include "TsRuntime.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsBuffer.h"
#include "TsSymbol.h"
#include "TsError.h"
#include "GC.h"
#include "TsGC.h"

#include <cstring>
#include <cstdio>

extern "C" {

// Module functions
void* ts_http2_create_server(void* options, void* callback) {
    TsValue* opts = (TsValue*)options;
    return TsHttp2Server::Create(opts, callback);
}

void* ts_http2_create_secure_server(void* options, void* callback) {
    TsValue* opts = (TsValue*)options;
    return TsHttp2SecureServer::Create(opts, callback);
}

void* ts_http2_connect(void* authority, void* options, void* callback) {
    // Use ts_value_get_string for the authority parameter (it's a string, not an object)
    TsString* authStr = (TsString*)ts_value_get_string((TsValue*)authority);
    if (!authStr) {
        // Fallback: try as raw TsString pointer
        authStr = dynamic_cast<TsString*>((TsObject*)authority);
    }
    if (!authStr) return nullptr;

    TsValue* opts = (TsValue*)options;
    return TsClientHttp2Session::Create(authStr->ToUtf8(), opts);
}

void* ts_http2_get_default_settings() {
    TsMap* settings = TsMap::Create();
    settings->Set(TsString::Create("headerTableSize"),
                  ts_value_make_int(Http2Constants::DEFAULT_SETTINGS_HEADER_TABLE_SIZE));
    settings->Set(TsString::Create("enablePush"),
                  ts_value_make_bool(Http2Constants::DEFAULT_SETTINGS_ENABLE_PUSH));
    settings->Set(TsString::Create("maxConcurrentStreams"),
                  ts_value_make_int(Http2Constants::DEFAULT_SETTINGS_MAX_CONCURRENT_STREAMS));
    settings->Set(TsString::Create("initialWindowSize"),
                  ts_value_make_int(Http2Constants::DEFAULT_SETTINGS_INITIAL_WINDOW_SIZE));
    settings->Set(TsString::Create("maxFrameSize"),
                  ts_value_make_int(Http2Constants::DEFAULT_SETTINGS_MAX_FRAME_SIZE));
    settings->Set(TsString::Create("maxHeaderListSize"),
                  ts_value_make_int(Http2Constants::DEFAULT_SETTINGS_MAX_HEADER_LIST_SIZE));
    return settings;
}

void* ts_http2_get_packed_settings(void* settings) {
    // Pack settings into a Buffer
    // Each setting is 6 bytes: 2 byte ID + 4 byte value
    TsBuffer* buf = TsBuffer::Create(6 * 6);  // 6 settings
    uint8_t* data = buf->GetData();
    size_t offset = 0;

    // Get settings from map
    TsMap* settingsMap = dynamic_cast<TsMap*>((TsObject*)ts_value_get_object((TsValue*)settings));
    if (!settingsMap) settingsMap = dynamic_cast<TsMap*>((TsObject*)settings);

    auto writeSetting = [&](uint16_t id, uint32_t value) {
        data[offset++] = (id >> 8) & 0xFF;
        data[offset++] = id & 0xFF;
        data[offset++] = (value >> 24) & 0xFF;
        data[offset++] = (value >> 16) & 0xFF;
        data[offset++] = (value >> 8) & 0xFF;
        data[offset++] = value & 0xFF;
    };

    writeSetting(NGHTTP2_SETTINGS_HEADER_TABLE_SIZE,
                 Http2Constants::DEFAULT_SETTINGS_HEADER_TABLE_SIZE);
    writeSetting(NGHTTP2_SETTINGS_ENABLE_PUSH,
                 Http2Constants::DEFAULT_SETTINGS_ENABLE_PUSH);
    writeSetting(NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
                 Http2Constants::DEFAULT_SETTINGS_MAX_CONCURRENT_STREAMS);
    writeSetting(NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
                 Http2Constants::DEFAULT_SETTINGS_INITIAL_WINDOW_SIZE);
    writeSetting(NGHTTP2_SETTINGS_MAX_FRAME_SIZE,
                 Http2Constants::DEFAULT_SETTINGS_MAX_FRAME_SIZE);
    writeSetting(NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
                 Http2Constants::DEFAULT_SETTINGS_MAX_HEADER_LIST_SIZE);

    return buf;
}

void* ts_http2_get_unpacked_settings(void* buffer) {
    void* rawBuffer = ts_nanbox_safe_unbox(buffer);

    TsBuffer* buf = dynamic_cast<TsBuffer*>((TsObject*)rawBuffer);
    if (!buf) return ts_http2_get_default_settings();

    TsMap* settings = TsMap::Create();
    uint8_t* data = buf->GetData();
    size_t len = buf->GetLength();

    for (size_t i = 0; i + 6 <= len; i += 6) {
        uint16_t id = (data[i] << 8) | data[i + 1];
        uint32_t value = (data[i + 2] << 24) | (data[i + 3] << 16) |
                         (data[i + 4] << 8) | data[i + 5];

        switch (id) {
            case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
                settings->Set(TsString::Create("headerTableSize"), ts_value_make_int(value));
                break;
            case NGHTTP2_SETTINGS_ENABLE_PUSH:
                settings->Set(TsString::Create("enablePush"), ts_value_make_bool(value != 0));
                break;
            case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
                settings->Set(TsString::Create("maxConcurrentStreams"), ts_value_make_int(value));
                break;
            case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
                settings->Set(TsString::Create("initialWindowSize"), ts_value_make_int(value));
                break;
            case NGHTTP2_SETTINGS_MAX_FRAME_SIZE:
                settings->Set(TsString::Create("maxFrameSize"), ts_value_make_int(value));
                break;
            case NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
                settings->Set(TsString::Create("maxHeaderListSize"), ts_value_make_int(value));
                break;
        }
    }

    return settings;
}

void* ts_http2_get_constants() {
    TsMap* constants = TsMap::Create();

    // Error codes - use TsValue constructor directly
    constants->Set(TsValue(TsString::Create("NGHTTP2_NO_ERROR")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_NO_ERROR));
    constants->Set(TsValue(TsString::Create("NGHTTP2_PROTOCOL_ERROR")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_PROTOCOL_ERROR));
    constants->Set(TsValue(TsString::Create("NGHTTP2_INTERNAL_ERROR")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_INTERNAL_ERROR));
    constants->Set(TsValue(TsString::Create("NGHTTP2_FLOW_CONTROL_ERROR")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_FLOW_CONTROL_ERROR));
    constants->Set(TsValue(TsString::Create("NGHTTP2_SETTINGS_TIMEOUT")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_SETTINGS_TIMEOUT));
    constants->Set(TsValue(TsString::Create("NGHTTP2_STREAM_CLOSED")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_STREAM_CLOSED));
    constants->Set(TsValue(TsString::Create("NGHTTP2_FRAME_SIZE_ERROR")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_FRAME_SIZE_ERROR));
    constants->Set(TsValue(TsString::Create("NGHTTP2_REFUSED_STREAM")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_REFUSED_STREAM));
    constants->Set(TsValue(TsString::Create("NGHTTP2_CANCEL")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_CANCEL));
    constants->Set(TsValue(TsString::Create("NGHTTP2_COMPRESSION_ERROR")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_COMPRESSION_ERROR));
    constants->Set(TsValue(TsString::Create("NGHTTP2_CONNECT_ERROR")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_CONNECT_ERROR));
    constants->Set(TsValue(TsString::Create("NGHTTP2_ENHANCE_YOUR_CALM")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_ENHANCE_YOUR_CALM));
    constants->Set(TsValue(TsString::Create("NGHTTP2_INADEQUATE_SECURITY")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_INADEQUATE_SECURITY));
    constants->Set(TsValue(TsString::Create("NGHTTP2_HTTP_1_1_REQUIRED")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_HTTP_1_1_REQUIRED));

    // Session types
    constants->Set(TsValue(TsString::Create("NGHTTP2_SESSION_SERVER")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_SESSION_SERVER));
    constants->Set(TsValue(TsString::Create("NGHTTP2_SESSION_CLIENT")),
                   TsValue((int64_t)Http2Constants::NGHTTP2_SESSION_CLIENT));

    return constants;
}

// Session property getters
void* ts_http2_session_get_alpn_protocol(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->alpnProtocol : nullptr;
}

bool ts_http2_session_get_closed(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->closed : true;
}

bool ts_http2_session_get_connecting(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->connecting : false;
}

bool ts_http2_session_get_destroyed(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->destroyed : true;
}

bool ts_http2_session_get_encrypted(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->encrypted : false;
}

void* ts_http2_session_get_local_settings(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (!s) return nullptr;

    TsMap* settings = TsMap::Create();
    settings->Set(TsString::Create("headerTableSize"),
                  ts_value_make_int(s->localSettings.headerTableSize));
    settings->Set(TsString::Create("enablePush"),
                  ts_value_make_bool(s->localSettings.enablePush));
    settings->Set(TsString::Create("maxConcurrentStreams"),
                  ts_value_make_int(s->localSettings.maxConcurrentStreams));
    settings->Set(TsString::Create("initialWindowSize"),
                  ts_value_make_int(s->localSettings.initialWindowSize));
    settings->Set(TsString::Create("maxFrameSize"),
                  ts_value_make_int(s->localSettings.maxFrameSize));
    settings->Set(TsString::Create("maxHeaderListSize"),
                  ts_value_make_int(s->localSettings.maxHeaderListSize));
    return settings;
}

void* ts_http2_session_get_remote_settings(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (!s) return nullptr;

    TsMap* settings = TsMap::Create();
    settings->Set(TsString::Create("headerTableSize"),
                  ts_value_make_int(s->remoteSettings.headerTableSize));
    settings->Set(TsString::Create("enablePush"),
                  ts_value_make_bool(s->remoteSettings.enablePush));
    settings->Set(TsString::Create("maxConcurrentStreams"),
                  ts_value_make_int(s->remoteSettings.maxConcurrentStreams));
    settings->Set(TsString::Create("initialWindowSize"),
                  ts_value_make_int(s->remoteSettings.initialWindowSize));
    settings->Set(TsString::Create("maxFrameSize"),
                  ts_value_make_int(s->remoteSettings.maxFrameSize));
    settings->Set(TsString::Create("maxHeaderListSize"),
                  ts_value_make_int(s->remoteSettings.maxHeaderListSize));
    return settings;
}

void* ts_http2_session_get_socket(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->socket : nullptr;
}

int64_t ts_http2_session_get_type(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->type : -1;
}

void* ts_http2_session_get_state(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->GetState() : nullptr;
}

// Session methods
void ts_http2_session_close(void* session, void* callback) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (s) s->Close(callback);
}

void ts_http2_session_destroy(void* session, void* error, int64_t code) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (s) s->Destroy(error, (int)code);
}

void ts_http2_session_goaway(void* session, int64_t code, int64_t lastStreamId, void* data) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (!s) return;

    TsBuffer* buf = nullptr;
    if (data) {
        void* rawData = ts_nanbox_safe_unbox(data);
        buf = dynamic_cast<TsBuffer*>((TsObject*)rawData);
    }

    s->Goaway((int)code, (int)lastStreamId, buf);
}

bool ts_http2_session_ping(void* session, void* payload, void* callback) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (!s) return false;

    TsBuffer* buf = nullptr;
    if (payload) {
        void* rawPayload = ts_nanbox_safe_unbox(payload);
        buf = dynamic_cast<TsBuffer*>((TsObject*)rawPayload);
    }

    return s->Ping(buf, callback);
}

void ts_http2_session_ref(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (s) s->Ref();
}

void ts_http2_session_unref(void* session) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (s) s->Unref();
}

void ts_http2_session_set_timeout(void* session, int64_t msecs, void* callback) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (s) s->SetTimeout((int)msecs, callback);
}

void ts_http2_session_settings(void* session, void* settings) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (!s) return;

    // TODO: Parse settings from TsMap
    TsHttp2Settings newSettings;
    s->Settings(&newSettings);
}

// ServerHttp2Session specific
void ts_http2_server_session_altsvc(void* session, void* alt, void* origin) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsServerHttp2Session* s = dynamic_cast<TsServerHttp2Session*>((TsObject*)raw);
    if (!s) return;

    void* rawAlt = ts_nanbox_safe_unbox(alt);
    TsString* altStr = dynamic_cast<TsString*>((TsObject*)rawAlt);

    void* rawOrigin = ts_nanbox_safe_unbox(origin);
    TsString* originStr = dynamic_cast<TsString*>((TsObject*)rawOrigin);

    s->Altsvc(altStr ? altStr->ToUtf8() : nullptr,
              originStr ? originStr->ToUtf8() : nullptr);
}

void ts_http2_server_session_origin(void* session, void* origins) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsServerHttp2Session* s = dynamic_cast<TsServerHttp2Session*>((TsObject*)raw);
    if (!s) return;

    void* rawOrigins = ts_nanbox_safe_unbox(origins);
    TsArray* originsArr = dynamic_cast<TsArray*>((TsObject*)rawOrigins);

    s->Origin(originsArr);
}

// ClientHttp2Session specific
void* ts_http2_client_session_request(void* session, void* headers, void* options) {
    void* raw = ts_nanbox_safe_unbox(session);
    TsClientHttp2Session* s = dynamic_cast<TsClientHttp2Session*>((TsObject*)raw);
    if (!s) return nullptr;

    void* rawHeaders = ts_nanbox_safe_unbox(headers);
    TsMap* headersMap = dynamic_cast<TsMap*>((TsObject*)rawHeaders);

    TsClientHttp2Stream* stream = s->Request(headersMap, (TsValue*)options);
    if (!stream) return nullptr;

    // IMPORTANT: Due to virtual inheritance, we must cast to TsObject* first
    // before boxing. The raw TsClientHttp2Stream* pointer is at a different
    // address than the TsObject* subobject, so storing the derived pointer
    // and later casting via (TsObject*)raw won't work.
    TsObject* objPtr = dynamic_cast<TsObject*>(stream);
    return ts_value_make_object(objPtr);
}

// Stream property getters
bool ts_http2_stream_get_aborted(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->aborted : true;
}

int64_t ts_http2_stream_get_buffer_size(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? (int64_t)s->bufferSize : 0;
}

bool ts_http2_stream_get_closed(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->closed : true;
}

bool ts_http2_stream_get_destroyed(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->destroyed : true;
}

bool ts_http2_stream_get_end_after_headers(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->endAfterHeaders : false;
}

int64_t ts_http2_stream_get_id(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->id : -1;
}

bool ts_http2_stream_get_pending(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->pending : false;
}

int64_t ts_http2_stream_get_rst_code(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->rstCode : 0;
}

void* ts_http2_stream_get_sent_headers(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->sentHeaders : nullptr;
}

void* ts_http2_stream_get_sent_info_headers(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->sentInfoHeaders : nullptr;
}

void* ts_http2_stream_get_sent_trailers(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->sentTrailers : nullptr;
}

void* ts_http2_stream_get_session(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->session : nullptr;
}

void* ts_http2_stream_get_state(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->GetState() : nullptr;
}

// Stream methods
void ts_http2_stream_close(void* stream, int64_t code, void* callback) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    if (s) s->Close((int)code, callback);
}

void ts_http2_stream_priority(void* stream, void* options) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    if (s) s->Priority((TsValue*)options);
}

void ts_http2_stream_set_timeout(void* stream, int64_t msecs, void* callback) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    if (s) s->SetTimeout((int)msecs, callback);
}

// ServerHttp2Stream specific
bool ts_http2_server_stream_get_headers_sent(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    return s ? s->headersSent : false;
}

bool ts_http2_server_stream_get_push_allowed(void* stream) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    return s ? s->pushAllowed : false;
}

void ts_http2_server_stream_additional_headers(void* stream, void* headers) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    if (!s) return;

    void* rawHeaders = ts_nanbox_safe_unbox(headers);
    TsMap* headersMap = dynamic_cast<TsMap*>((TsObject*)rawHeaders);

    s->AdditionalHeaders(headersMap);
}

void ts_http2_server_stream_push_stream(void* stream, void* headers, void* options, void* callback) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    if (!s) return;

    void* rawHeaders = ts_nanbox_safe_unbox(headers);
    TsMap* headersMap = dynamic_cast<TsMap*>((TsObject*)rawHeaders);

    s->PushStream(headersMap, (TsValue*)options, callback);
}

void ts_http2_server_stream_respond(void* stream, void* headers, void* options) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    if (!s) return;

    void* rawHeaders = ts_nanbox_safe_unbox(headers);
    TsMap* headersMap = dynamic_cast<TsMap*>((TsObject*)rawHeaders);

    s->Respond(headersMap, (TsValue*)options);
}

void ts_http2_server_stream_respond_with_fd(void* stream, int64_t fd, void* headers, void* options) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    if (!s) return;

    void* rawHeaders = ts_nanbox_safe_unbox(headers);
    TsMap* headersMap = dynamic_cast<TsMap*>((TsObject*)rawHeaders);

    s->RespondWithFD((int)fd, headersMap, (TsValue*)options);
}

void ts_http2_server_stream_respond_with_file(void* stream, void* path, void* headers, void* options) {
    void* raw = ts_nanbox_safe_unbox(stream);
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    if (!s) return;

    void* rawPath = ts_nanbox_safe_unbox(path);
    TsString* pathStr = dynamic_cast<TsString*>((TsObject*)rawPath);

    void* rawHeaders = ts_nanbox_safe_unbox(headers);
    TsMap* headersMap = dynamic_cast<TsMap*>((TsObject*)rawHeaders);

    s->RespondWithFile(pathStr ? pathStr->ToUtf8() : nullptr, headersMap, (TsValue*)options);
}

// Module property - sensitiveHeaders symbol
void* ts_http2_get_sensitive_headers() {
    // Create a unique symbol for sensitiveHeaders
    // This is used to mark headers that should not be logged
    static TsSymbol* sensitiveHeadersSymbol = nullptr;
    if (!sensitiveHeadersSymbol) {
        sensitiveHeadersSymbol = TsSymbol::Create(TsString::Create("nodejs.http2.sensitiveHeaders"));
    }
    return sensitiveHeadersSymbol;
}

// =============================================================================
// Http2Server functions
// =============================================================================

void* ts_http2_server_listen(void* server, int64_t port, void* host, void* callback) {
    void* raw = ts_nanbox_safe_unbox(server);
    TsHttp2Server* srv = dynamic_cast<TsHttp2Server*>((TsObject*)raw);
    if (!srv) return nullptr;

    // Detect when host is actually a callback: server.listen(port, callback)
    // Due to optional parameter ordering in ext.json, the callback may arrive in the host
    // position when called as server.listen(port, callback) with no host argument.
    // The host param has "ptr" lowering, so it's a raw pointer (not TsValue*).
    // Use GC_base + TsString magic to distinguish a GC-allocated string from a function pointer.
    if (host && !callback) {
        bool isString = false;
        void* base = ts_gc_base(host);
        if (base) {
            // GC-allocated object - check if it's a TsString (magic 0x53545247 at offset 0)
            uint32_t magic = *(uint32_t*)host;
            isString = (magic == 0x53545247);
        }
        if (!isString) {
            // Not a string - it's actually the callback
            callback = host;
            host = nullptr;
        }
    }

    // Host is optional - if provided, extract it
    const char* hostStr = nullptr;
    if (host) {
        void* rawHost = ts_nanbox_safe_unbox(host);
        TsString* hostString = dynamic_cast<TsString*>((TsObject*)rawHost);
        if (hostString) {
            hostStr = hostString->ToUtf8();
        }
    }

    // Wrap raw callback pointer in TsValue* for EventEmitter compatibility.
    // The "ptr" lowering passes raw TsClosure*/function pointers, but EventEmitter's
    // Emit → ts_function_call → ts_extract_closure chain expects TsValue*.
    void* wrappedCallback = callback ? ts_value_make_object(callback) : nullptr;
    srv->Listen((int)port, hostStr, wrappedCallback);
    return srv;
}

void ts_http2_server_close(void* server, void* callback) {
    void* raw = ts_nanbox_safe_unbox(server);
    TsHttp2Server* srv = dynamic_cast<TsHttp2Server*>((TsObject*)raw);
    if (!srv) return;

    // Register callback for 'close' event if provided
    if (callback) {
        srv->Once("close", callback);
    }
    srv->Close();
}

void* ts_http2_server_address(void* server) {
    void* raw = ts_nanbox_safe_unbox(server);
    TsHttp2Server* srv = dynamic_cast<TsHttp2Server*>((TsObject*)raw);
    if (!srv) return nullptr;

    return srv->Address();
}

void ts_http2_server_set_timeout(void* server, int64_t msecs, void* callback) {
    void* raw = ts_nanbox_safe_unbox(server);
    TsHttp2Server* srv = dynamic_cast<TsHttp2Server*>((TsObject*)raw);
    if (!srv) return;

    // TODO: Implement server timeout - for now just emit 'timeout' event if callback provided
    if (callback) {
        srv->On("timeout", callback);
    }
}

} // extern "C"
