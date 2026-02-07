#include "TsHttp2.h"
#include "TsError.h"
#include "TsSymbol.h"
#include "GC.h"
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <algorithm>
#include <cctype>

// =============================================================================
// TsHttp2Session Implementation
// =============================================================================

TsHttp2Session::TsHttp2Session() : TsEventEmitter() {
    alpnProtocol = TsString::Create("h2");
}

TsHttp2Session::~TsHttp2Session() {
    if (session) {
        nghttp2_session_del(session);
        session = nullptr;
    }
    if (timeoutTimer) {
        uv_close((uv_handle_t*)timeoutTimer, nullptr);
        timeoutTimer = nullptr;
    }
}

void TsHttp2Session::InitSession(bool isServer) {
    nghttp2_session_callbacks* callbacks;
    nghttp2_session_callbacks_new(&callbacks);

    // Set callbacks for frame handling
    nghttp2_session_callbacks_set_send_callback(callbacks, OnSend);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, OnFrameRecv);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, OnStreamClose);
    nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, OnBeginHeaders);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, OnHeader);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, OnDataChunkRecv);

    // Create session
    if (isServer) {
        nghttp2_session_server_new(&session, callbacks, this);
        type = 0;
    } else {
        nghttp2_session_client_new(&session, callbacks, this);
        type = 1;
    }

    nghttp2_session_callbacks_del(callbacks);

    // Send initial settings
    nghttp2_settings_entry iv[6] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, localSettings.maxConcurrentStreams},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, localSettings.initialWindowSize},
        {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, localSettings.maxFrameSize},
        {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, localSettings.maxHeaderListSize},
        {NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, localSettings.headerTableSize},
        {NGHTTP2_SETTINGS_ENABLE_PUSH, localSettings.enablePush}
    };
    nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, iv, 6);
}

void TsHttp2Session::OnData(const uint8_t* data, size_t length) {
    if (!session || destroyed) return;

    ssize_t rv = nghttp2_session_mem_recv(session, data, length);
    if (rv < 0) {
        // Error receiving data
        void* err = ts_error_create(TsString::Create(nghttp2_strerror((int)rv)));
        void* args[] = { err };
        Emit("error", 1, args);
        return;
    }

    // Send any pending data
    SendPendingData();
}

void TsHttp2Session::SendPendingData() {
    if (!session || !socket) return;

    // nghttp2_session_send will call OnSend callback
    int rv = nghttp2_session_send(session);
    if (rv != 0) {
        void* err = ts_error_create(TsString::Create(nghttp2_strerror(rv)));
        void* args[] = { err };
        Emit("error", 1, args);
    }
}

ssize_t TsHttp2Session::OnSend(nghttp2_session* session, const uint8_t* data,
                                size_t length, int flags, void* user_data) {
    TsHttp2Session* h2session = (TsHttp2Session*)user_data;
    if (!h2session || !h2session->socket) return NGHTTP2_ERR_CALLBACK_FAILURE;

    // Write to socket
    TsWritable* writable = h2session->socket->AsWritable();
    if (writable) {
        TsBuffer* buf = TsBuffer::Create(length);
        memcpy(buf->GetData(), data, length);
        writable->Write(buf->GetData(), length);
    }

    return (ssize_t)length;
}

int TsHttp2Session::OnFrameRecv(nghttp2_session* session, const nghttp2_frame* frame,
                                 void* user_data) {
    TsHttp2Session* h2session = (TsHttp2Session*)user_data;
    if (!h2session) return 0;

    switch (frame->hd.type) {
        case NGHTTP2_SETTINGS:
            if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
                // Settings acknowledged
                h2session->Emit("localSettings", 0, nullptr);
            } else {
                // Received remote settings
                h2session->Emit("remoteSettings", 0, nullptr);
            }
            break;

        case NGHTTP2_PING:
            if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
                // Ping acknowledged
                TsBuffer* payload = TsBuffer::Create(8);
                memcpy(payload->GetData(), frame->ping.opaque_data, 8);
                void* args[] = { payload };
                h2session->Emit("ping", 1, args);
            }
            break;

        case NGHTTP2_GOAWAY:
            {
                TsMap* goawayInfo = TsMap::Create();
                goawayInfo->Set(TsString::Create("errorCode"),
                               ts_value_make_int(frame->goaway.error_code));
                goawayInfo->Set(TsString::Create("lastStreamID"),
                               ts_value_make_int(frame->goaway.last_stream_id));
                void* args[] = { goawayInfo };
                h2session->Emit("goaway", 1, args);
            }
            break;

        case NGHTTP2_DATA:
            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                TsHttp2Stream* stream = h2session->GetStream(frame->hd.stream_id);
                if (stream) {
                    stream->OnEnd();
                }
            }
            break;

        case NGHTTP2_HEADERS:
            {
                TsHttp2Stream* stream = h2session->GetStream(frame->hd.stream_id);
                if (stream) {
                    // For server sessions receiving request headers, emit 'stream' event
                    if (h2session->type == 0 && frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
                        // Emit 'stream' event with (stream, headers, flags)
                        TsValue* flagsVal = ts_value_make_int(frame->hd.flags);
                        void* args[] = { stream, stream->sentHeaders, flagsVal };
                        h2session->Emit("stream", 3, args);
                    }

                    // For client sessions receiving response headers, emit 'response' event
                    if (h2session->type == 1 && frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
                        // Emit 'response' event with (headers, flags)
                        TsValue* flagsVal = ts_value_make_int(frame->hd.flags);
                        void* args[] = { stream->sentHeaders, flagsVal };
                        stream->Emit("response", 2, args);
                    }

                    // For informational headers (1xx), emit 'headers' event
                    if (h2session->type == 1 && frame->headers.cat == NGHTTP2_HCAT_HEADERS) {
                        TsValue* flagsVal = ts_value_make_int(frame->hd.flags);
                        void* args[] = { stream->sentHeaders, flagsVal };
                        stream->Emit("headers", 2, args);
                    }

                    if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                        stream->endAfterHeaders = true;
                        stream->OnEnd();
                    }
                }
            }
            break;

        default:
            break;
    }

    return 0;
}

int TsHttp2Session::OnStreamClose(nghttp2_session* session, int32_t stream_id,
                                   uint32_t error_code, void* user_data) {
    TsHttp2Session* h2session = (TsHttp2Session*)user_data;
    if (!h2session) return 0;

    TsHttp2Stream* stream = h2session->GetStream(stream_id);
    if (stream) {
        stream->rstCode = error_code;
        stream->closed = true;

        if (error_code != NGHTTP2_NO_ERROR) {
            stream->OnError(error_code);
        }

        stream->Emit("close", 0, nullptr);
        h2session->RemoveStream(stream_id);
    }

    return 0;
}

int TsHttp2Session::OnBeginHeaders(nghttp2_session* session, const nghttp2_frame* frame,
                                    void* user_data) {
    TsHttp2Session* h2session = (TsHttp2Session*)user_data;
    if (!h2session) return 0;

    if (frame->hd.type == NGHTTP2_HEADERS) {
        if (frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
            // New request stream on server
            TsServerHttp2Stream* stream = TsServerHttp2Stream::Create(h2session, frame->hd.stream_id);
            h2session->streams[frame->hd.stream_id] = stream;
            stream->pending = false;
        } else if (frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
            // Response headers on client
            TsHttp2Stream* stream = h2session->GetStream(frame->hd.stream_id);
            if (stream) {
                stream->pending = false;
            }
        }
    }

    return 0;
}

int TsHttp2Session::OnHeader(nghttp2_session* session, const nghttp2_frame* frame,
                              const uint8_t* name, size_t namelen,
                              const uint8_t* value, size_t valuelen,
                              uint8_t flags, void* user_data) {
    TsHttp2Session* h2session = (TsHttp2Session*)user_data;
    if (!h2session) return 0;

    TsHttp2Stream* stream = h2session->GetStream(frame->hd.stream_id);
    if (!stream) return 0;

    // Create header map if needed
    if (!stream->sentHeaders) {
        stream->sentHeaders = TsMap::Create();
    }

    // Copy to null-terminated strings since TsString::Create doesn't take length
    char* nameBuf = (char*)ts_alloc(namelen + 1);
    memcpy(nameBuf, name, namelen);
    nameBuf[namelen] = '\0';
    char* valueBuf = (char*)ts_alloc(valuelen + 1);
    memcpy(valueBuf, value, valuelen);
    valueBuf[valuelen] = '\0';
    TsString* nameStr = TsString::Create(nameBuf);
    TsString* valueStr = TsString::Create(valueBuf);
    stream->sentHeaders->Set(nameStr, ts_value_make_string(valueStr));

    return 0;
}

int TsHttp2Session::OnDataChunkRecv(nghttp2_session* session, uint8_t flags,
                                     int32_t stream_id, const uint8_t* data,
                                     size_t len, void* user_data) {
    TsHttp2Session* h2session = (TsHttp2Session*)user_data;
    if (!h2session) return 0;

    TsHttp2Stream* stream = h2session->GetStream(stream_id);
    if (stream) {
        stream->OnData(data, len);
    }

    return 0;
}

void TsHttp2Session::Close(void* callback) {
    if (closed) return;
    closeCallback = callback;

    // Send GOAWAY frame
    Goaway(0, 0, nullptr);

    closed = true;
    Emit("close", 0, nullptr);
}

void TsHttp2Session::Destroy(void* error, int code) {
    if (destroyed) return;
    destroyed = true;

    // Close all streams
    for (auto& pair : streams) {
        pair.second->destroyed = true;
        pair.second->Emit("close", 0, nullptr);
    }
    streams.clear();

    if (socket) {
        TsWritable* writable = socket->AsWritable();
        if (writable) {
            writable->End();
        }
    }

    Emit("close", 0, nullptr);
}

void TsHttp2Session::Goaway(int code, int lastStreamId, TsBuffer* data) {
    if (!session) return;

    const uint8_t* opaque_data = nullptr;
    size_t opaque_data_len = 0;

    if (data) {
        opaque_data = data->GetData();
        opaque_data_len = data->GetLength();
    }

    nghttp2_submit_goaway(session, NGHTTP2_FLAG_NONE, lastStreamId, code,
                          opaque_data, opaque_data_len);
    SendPendingData();
}

bool TsHttp2Session::Ping(TsBuffer* payload, void* callback) {
    if (!session || closed) return false;

    uint8_t opaque_data[8] = {0};
    if (payload && payload->GetLength() >= 8) {
        memcpy(opaque_data, payload->GetData(), 8);
    }

    int rv = nghttp2_submit_ping(session, NGHTTP2_FLAG_NONE, opaque_data);
    if (rv != 0) return false;

    SendPendingData();
    return true;
}

void TsHttp2Session::Ref() {
    if (socket) {
        TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)socket);
        if (s) s->Ref();
    }
}

void TsHttp2Session::Unref() {
    if (socket) {
        TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)socket);
        if (s) s->Unref();
    }
}

static void OnSessionTimeout(uv_timer_t* handle) {
    TsHttp2Session* session = (TsHttp2Session*)handle->data;
    if (session) {
        session->Emit("timeout", 0, nullptr);
    }
}

void TsHttp2Session::SetTimeout(int msecs, void* callback) {
    if (msecs <= 0) {
        if (timeoutTimer) {
            uv_timer_stop(timeoutTimer);
        }
        return;
    }

    if (!timeoutTimer) {
        timeoutTimer = (uv_timer_t*)ts_alloc(sizeof(uv_timer_t));
        uv_timer_init(uv_default_loop(), timeoutTimer);
        timeoutTimer->data = this;
    }

    uv_timer_start(timeoutTimer, OnSessionTimeout, msecs, 0);
}

void TsHttp2Session::Settings(TsHttp2Settings* settings) {
    if (!session || !settings) return;

    localSettings = *settings;

    nghttp2_settings_entry iv[6] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, settings->maxConcurrentStreams},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, settings->initialWindowSize},
        {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, settings->maxFrameSize},
        {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, settings->maxHeaderListSize},
        {NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, settings->headerTableSize},
        {NGHTTP2_SETTINGS_ENABLE_PUSH, settings->enablePush}
    };

    nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, iv, 6);
    SendPendingData();
}

TsMap* TsHttp2Session::GetState() {
    TsMap* state = TsMap::Create();

    if (session) {
        state->Set(TsString::Create("localWindowSize"),
                   ts_value_make_int(nghttp2_session_get_local_window_size(session)));
        state->Set(TsString::Create("remoteWindowSize"),
                   ts_value_make_int(nghttp2_session_get_remote_window_size(session)));
        state->Set(TsString::Create("deflateDynamicTableSize"),
                   ts_value_make_int(nghttp2_session_get_hd_deflate_dynamic_table_size(session)));
        state->Set(TsString::Create("inflateDynamicTableSize"),
                   ts_value_make_int(nghttp2_session_get_hd_inflate_dynamic_table_size(session)));
    }

    return state;
}

TsHttp2Stream* TsHttp2Session::GetStream(int32_t streamId) {
    auto it = streams.find(streamId);
    if (it != streams.end()) {
        return it->second;
    }
    return nullptr;
}

void TsHttp2Session::AddStream(int32_t streamId, TsHttp2Stream* stream) {
    streams[streamId] = stream;
}

void TsHttp2Session::RemoveStream(int32_t streamId) {
    streams.erase(streamId);
}

// =============================================================================
// TsServerHttp2Session Implementation
// =============================================================================

TsServerHttp2Session::TsServerHttp2Session() : TsHttp2Session() {
    type = 0;
}

TsServerHttp2Session* TsServerHttp2Session::Create(TsSocket* socket) {
    void* mem = ts_alloc(sizeof(TsServerHttp2Session));
    TsServerHttp2Session* session = new (mem) TsServerHttp2Session();
    session->Init(socket);
    return session;
}

void TsServerHttp2Session::Init(TsSocket* socket) {
    this->socket = socket;
    encrypted = dynamic_cast<TsSecureSocket*>((TsObject*)socket) != nullptr;

    // Initialize nghttp2 server session
    InitSession(true);

    // Set up data handler from socket
    // Socket data will be passed to OnData
}

void TsServerHttp2Session::Altsvc(const char* alt, const char* origin) {
    if (!session || !alt) return;

    size_t origin_len = origin ? strlen(origin) : 0;
    nghttp2_submit_altsvc(session, NGHTTP2_FLAG_NONE, 0,
                          (const uint8_t*)origin, origin_len,
                          (const uint8_t*)alt, strlen(alt));
    SendPendingData();
}

void TsServerHttp2Session::Origin(TsArray* origins) {
    // ORIGIN frame support (RFC 8336)
    // Not directly supported by nghttp2, would need custom frame
}

// =============================================================================
// TsClientHttp2Session Implementation
// =============================================================================

TsClientHttp2Session::TsClientHttp2Session() : TsHttp2Session() {
    type = 1;
}

TsClientHttp2Session* TsClientHttp2Session::Create(const char* authority, TsValue* options) {
    void* mem = ts_alloc(sizeof(TsClientHttp2Session));
    TsClientHttp2Session* session = new (mem) TsClientHttp2Session();
    session->Init(authority, options);
    return session;
}

// Static callbacks for client socket events
static void* client_session_on_connect(void* ctx, int argc, void** argv) {
    TsClientHttp2Session* session = (TsClientHttp2Session*)ctx;
    if (!session) return nullptr;

    session->OnSocketConnect();
    return nullptr;
}

static void* client_session_on_data(void* ctx, int argc, void** argv) {
    TsClientHttp2Session* session = (TsClientHttp2Session*)ctx;
    if (!session || argc < 1) return nullptr;

    // argv[0] is the data buffer
    TsBuffer* buf = dynamic_cast<TsBuffer*>((TsObject*)argv[0]);
    if (buf) {
        session->OnSocketData(buf->GetData(), buf->GetLength());
    }
    return nullptr;
}

static void* client_session_on_error(void* ctx, int argc, void** argv) {
    TsClientHttp2Session* session = (TsClientHttp2Session*)ctx;
    if (!session) return nullptr;

    // Forward error to session
    void* err = (argc > 0) ? argv[0] : ts_error_create(TsString::Create("Socket error"));
    void* args[] = { err };
    session->Emit("error", 1, args);
    return nullptr;
}

static void* client_session_on_close(void* ctx, int argc, void** argv) {
    TsClientHttp2Session* session = (TsClientHttp2Session*)ctx;
    if (!session) return nullptr;

    session->OnSocketClose();
    return nullptr;
}

void TsClientHttp2Session::Init(const char* authority, TsValue* options) {
    this->authority = authority;
    connecting = true;

    // Parse authority URL: [protocol://]host[:port]
    std::string url = authority;
    bool isSecure = false;
    std::string host;
    int port = 80;

    // Check for protocol
    if (url.find("https://") == 0) {
        isSecure = true;
        url = url.substr(8);  // Remove "https://"
        port = 443;
    } else if (url.find("http://") == 0) {
        url = url.substr(7);  // Remove "http://"
        port = 80;
    }

    // Parse host:port
    size_t colonPos = url.find(':');
    size_t slashPos = url.find('/');

    if (colonPos != std::string::npos && (slashPos == std::string::npos || colonPos < slashPos)) {
        host = url.substr(0, colonPos);
        size_t endPos = (slashPos != std::string::npos) ? slashPos : url.length();
        port = std::stoi(url.substr(colonPos + 1, endPos - colonPos - 1));
    } else if (slashPos != std::string::npos) {
        host = url.substr(0, slashPos);
    } else {
        host = url;
    }

    encrypted = isSecure;

    // Initialize nghttp2 client session
    InitSession(false);

    // Create socket
    if (isSecure) {
        TsSecureSocket* secureSocket = new (ts_alloc(sizeof(TsSecureSocket))) TsSecureSocket(false);
        socket = secureSocket;

        // Configure TLS options from options parameter
        bool rejectUnauthorized = true;
        TsValue* ca = nullptr;

        if (options && options->type == ValueType::OBJECT_PTR) {
            TsMap* opts = (TsMap*)options->ptr_val;
            TsValue v_reject = opts->Get(TsValue(TsString::Create("rejectUnauthorized")));
            if (v_reject.type == ValueType::BOOLEAN) rejectUnauthorized = v_reject.b_val;

            TsValue v_ca = opts->Get(TsValue(TsString::Create("ca")));
            if (v_ca.type != ValueType::UNDEFINED) ca = &v_ca;
        }

        secureSocket->SetVerify(rejectUnauthorized, ca);

        // Register event handlers
        secureSocket->On("secureConnect", ts_value_make_native_function((void*)client_session_on_connect, this));
    } else {
        socket = new (ts_alloc(sizeof(TsSocket))) TsSocket();
        socket->On("connect", ts_value_make_native_function((void*)client_session_on_connect, this));
    }

    // Common event handlers
    socket->On("data", ts_value_make_native_function((void*)client_session_on_data, this));
    socket->On("error", ts_value_make_native_function((void*)client_session_on_error, this));
    socket->On("close", ts_value_make_native_function((void*)client_session_on_close, this));

    // Connect
    socket->Connect(host.c_str(), port, nullptr);
}

void TsClientHttp2Session::OnSocketConnect() {
    connecting = false;

    // Send client connection preface and initial settings
    SendPendingData();

    Emit("connect", 0, nullptr);
}

void TsClientHttp2Session::OnSocketData(const uint8_t* data, size_t length) {
    // Feed data to nghttp2
    OnData(data, length);
}

void TsClientHttp2Session::OnSocketClose() {
    if (!destroyed) {
        Destroy(nullptr, 0);
    }
}

TsClientHttp2Stream* TsClientHttp2Session::Request(TsMap* headers, TsValue* options) {
    if (!session || closed || destroyed) return nullptr;

    // Build nghttp2 header array
    std::vector<nghttp2_nv> nva;

    // Add pseudo-headers first
    TsString* methodKey = TsString::Create(":method");
    TsString* pathKey = TsString::Create(":path");
    TsString* schemeKey = TsString::Create(":scheme");
    TsString* authorityKey = TsString::Create(":authority");

    // Default pseudo-headers if not provided
    std::string method = "GET";
    std::string path = "/";
    std::string scheme = encrypted ? "https" : "http";

    if (headers) {
        TsValue methodVal = headers->Get(TsValue(methodKey));
        if (methodVal.type == ValueType::STRING_PTR && methodVal.ptr_val) {
            TsString* methodStr = (TsString*)methodVal.ptr_val;
            method = methodStr->ToUtf8();
        }

        TsValue pathVal = headers->Get(TsValue(pathKey));
        if (pathVal.type == ValueType::STRING_PTR && pathVal.ptr_val) {
            TsString* pathStr = (TsString*)pathVal.ptr_val;
            path = pathStr->ToUtf8();
        }

        TsValue schemeVal = headers->Get(TsValue(schemeKey));
        if (schemeVal.type == ValueType::STRING_PTR && schemeVal.ptr_val) {
            TsString* schemeStr = (TsString*)schemeVal.ptr_val;
            scheme = schemeStr->ToUtf8();
        }
    }

    // Add pseudo-headers
    nva.push_back({(uint8_t*)":method", (uint8_t*)method.c_str(),
                   7, method.length(), NGHTTP2_NV_FLAG_NONE});
    nva.push_back({(uint8_t*)":path", (uint8_t*)path.c_str(),
                   5, path.length(), NGHTTP2_NV_FLAG_NONE});
    nva.push_back({(uint8_t*)":scheme", (uint8_t*)scheme.c_str(),
                   7, scheme.length(), NGHTTP2_NV_FLAG_NONE});
    nva.push_back({(uint8_t*)":authority", (uint8_t*)authority.c_str(),
                   10, authority.length(), NGHTTP2_NV_FLAG_NONE});

    // TODO: Add regular headers from headers map

    // Submit request
    int32_t stream_id = nghttp2_submit_request(session, nullptr,
                                                nva.data(), nva.size(),
                                                nullptr, nullptr);
    if (stream_id < 0) {
        return nullptr;
    }

    TsClientHttp2Stream* stream = TsClientHttp2Stream::Create(this, stream_id);
    streams[stream_id] = stream;

    SendPendingData();

    return stream;
}

// =============================================================================
// TsHttp2Stream Implementation
// =============================================================================

TsHttp2Stream::TsHttp2Stream(TsHttp2Session* session, int32_t streamId)
    : TsDuplex(), session(session), id(streamId) {
}

TsHttp2Stream::~TsHttp2Stream() {
    if (timeoutTimer) {
        uv_close((uv_handle_t*)timeoutTimer, nullptr);
        timeoutTimer = nullptr;
    }
}

void TsHttp2Stream::OnData(const uint8_t* data, size_t length) {
    if (destroyed) return;

    TsBuffer* buf = TsBuffer::Create(length);
    memcpy(buf->GetData(), data, length);

    void* args[] = { buf };
    Emit("data", 1, args);
}

void TsHttp2Stream::OnHeaders(TsMap* headers, uint8_t flags) {
    sentHeaders = headers;
    Emit("ready", 0, nullptr);
}

void TsHttp2Stream::OnTrailers(TsMap* trailers) {
    sentTrailers = trailers;
    Emit("trailers", 0, nullptr);
}

void TsHttp2Stream::OnEnd() {
    Emit("end", 0, nullptr);
}

void TsHttp2Stream::OnError(int code) {
    TsString* msg = TsString::Create(nghttp2_strerror(code));
    void* err = ts_error_create(msg);
    void* args[] = { err };
    Emit("error", 1, args);
}

bool TsHttp2Stream::Write(void* data, size_t length) {
    if (!session || !session->session || closed || destroyed) return false;

    // TODO: Implement DATA frame sending via nghttp2_submit_data
    return true;
}

void TsHttp2Stream::End() {
    if (!session || !session->session || closed || destroyed) return;

    // Send END_STREAM flag
    // TODO: Implement proper stream ending
    Emit("finish", 0, nullptr);
}

void TsHttp2Stream::Pause() {
    // Pause reading from this stream
    // TODO: Implement flow control
}

void TsHttp2Stream::Resume() {
    // Resume reading from this stream
    // TODO: Implement flow control
}

void TsHttp2Stream::Close(int code, void* callback) {
    if (closed) return;
    closeCallback = callback;

    if (session && session->session) {
        nghttp2_submit_rst_stream(session->session, NGHTTP2_FLAG_NONE, id, code);
        session->SendPendingData();
    }

    closed = true;
    rstCode = code;
}

void TsHttp2Stream::Priority(TsValue* options) {
    if (!session || !session->session || closed) return;

    nghttp2_priority_spec pri_spec;
    nghttp2_priority_spec_default_init(&pri_spec);

    // TODO: Parse options and set priority spec

    nghttp2_submit_priority(session->session, NGHTTP2_FLAG_NONE, id, &pri_spec);
    session->SendPendingData();
}

static void OnStreamTimeout(uv_timer_t* handle) {
    TsHttp2Stream* stream = (TsHttp2Stream*)handle->data;
    if (stream) {
        stream->Emit("timeout", 0, nullptr);
    }
}

void TsHttp2Stream::SetTimeout(int msecs, void* callback) {
    if (msecs <= 0) {
        if (timeoutTimer) {
            uv_timer_stop(timeoutTimer);
        }
        return;
    }

    if (!timeoutTimer) {
        timeoutTimer = (uv_timer_t*)ts_alloc(sizeof(uv_timer_t));
        uv_timer_init(uv_default_loop(), timeoutTimer);
        timeoutTimer->data = this;
    }

    uv_timer_start(timeoutTimer, OnStreamTimeout, msecs, 0);
}

TsMap* TsHttp2Stream::GetState() {
    TsMap* state = TsMap::Create();

    state->Set(TsString::Create("localWindowSize"), ts_value_make_int(0));
    state->Set(TsString::Create("state"), ts_value_make_int(0));
    state->Set(TsString::Create("localClose"), ts_value_make_int(0));
    state->Set(TsString::Create("remoteClose"), ts_value_make_int(0));
    state->Set(TsString::Create("sumDependencyWeight"), ts_value_make_int(0));
    state->Set(TsString::Create("weight"), ts_value_make_int(16));

    return state;
}

// =============================================================================
// TsServerHttp2Stream Implementation
// =============================================================================

TsServerHttp2Stream::TsServerHttp2Stream(TsHttp2Session* session, int32_t streamId)
    : TsHttp2Stream(session, streamId) {
}

TsServerHttp2Stream* TsServerHttp2Stream::Create(TsHttp2Session* session, int32_t streamId) {
    void* mem = ts_alloc(sizeof(TsServerHttp2Stream));
    return new (mem) TsServerHttp2Stream(session, streamId);
}

void TsServerHttp2Stream::AdditionalHeaders(TsMap* headers) {
    if (!session || !session->session || closed || headersSent) return;
    if (!headers) return;

    // Build header array for informational (1xx) headers
    std::vector<nghttp2_nv> nva;
    std::vector<std::string> headerStorage;  // Keep strings alive

    // Get :status pseudo-header (must be 1xx for informational)
    TsString* statusKey = TsString::Create(":status");
    TsValue statusVal = headers->Get(TsValue(statusKey));
    std::string status = "100";
    if (statusVal.type == ValueType::STRING_PTR && statusVal.ptr_val) {
        TsString* statusStr = (TsString*)statusVal.ptr_val;
        status = statusStr->ToUtf8();
    } else if (statusVal.type == ValueType::NUMBER_INT) {
        status = std::to_string(statusVal.i_val);
    }

    // Validate it's an informational status (1xx)
    int statusCode = std::stoi(status);
    if (statusCode < 100 || statusCode >= 200) return;

    headerStorage.push_back(status);
    nva.push_back({(uint8_t*)":status", (uint8_t*)headerStorage.back().c_str(),
                   7, headerStorage.back().length(), NGHTTP2_NV_FLAG_NONE});

    // Add other headers from the map
    TsArray* keys = (TsArray*)headers->GetKeys();
    if (keys) {
        for (size_t i = 0; i < keys->Length(); i++) {
            TsValue keyVal = keys->Get(i);
            if (keyVal.type != ValueType::STRING_PTR || !keyVal.ptr_val) continue;

            TsString* key = (TsString*)keyVal.ptr_val;
            std::string keyStr = key->ToUtf8();

            // Skip :status, we already handled it
            if (keyStr == ":status") continue;

            TsValue val = headers->Get(keyVal);
            if (val.type == ValueType::STRING_PTR && val.ptr_val) {
                TsString* valStr = (TsString*)val.ptr_val;
                headerStorage.push_back(keyStr);
                headerStorage.push_back(valStr->ToUtf8());

                nva.push_back({
                    (uint8_t*)headerStorage[headerStorage.size()-2].c_str(),
                    (uint8_t*)headerStorage[headerStorage.size()-1].c_str(),
                    headerStorage[headerStorage.size()-2].length(),
                    headerStorage[headerStorage.size()-1].length(),
                    NGHTTP2_NV_FLAG_NONE
                });
            }
        }
    }

    // Submit informational headers (no END_STREAM flag)
    nghttp2_submit_headers(session->session, NGHTTP2_FLAG_NONE, id, nullptr,
                           nva.data(), nva.size(), nullptr);
    session->SendPendingData();

    // Track sent info headers
    if (!sentInfoHeaders) {
        sentInfoHeaders = TsArray::Create();
    }
    ts_array_push(sentInfoHeaders, ts_value_make_object(headers));
}

void TsServerHttp2Stream::PushStream(TsMap* headers, TsValue* options, void* callback) {
    TsValue* cbFunc = (TsValue*)callback;

    if (!session || !session->session || closed || !pushAllowed) {
        // Call callback with error
        if (cbFunc) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Push stream not allowed"));
            ts_call_3(cbFunc, (TsValue*)err, nullptr, nullptr);
        }
        return;
    }

    if (!headers) {
        if (cbFunc) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Headers required for push stream"));
            ts_call_3(cbFunc, (TsValue*)err, nullptr, nullptr);
        }
        return;
    }

    // Build header array for PUSH_PROMISE
    std::vector<nghttp2_nv> nva;
    std::vector<std::string> headerStorage;

    // Required pseudo-headers for push: :method, :path, :scheme, :authority
    const char* pseudoHeaders[] = { ":method", ":path", ":scheme", ":authority" };
    for (const char* ph : pseudoHeaders) {
        TsString* key = TsString::Create(ph);
        TsValue val = headers->Get(TsValue(key));
        if (val.type == ValueType::STRING_PTR && val.ptr_val) {
            TsString* valStr = (TsString*)val.ptr_val;
            headerStorage.push_back(ph);
            headerStorage.push_back(valStr->ToUtf8());
            nva.push_back({
                (uint8_t*)headerStorage[headerStorage.size()-2].c_str(),
                (uint8_t*)headerStorage[headerStorage.size()-1].c_str(),
                headerStorage[headerStorage.size()-2].length(),
                headerStorage[headerStorage.size()-1].length(),
                NGHTTP2_NV_FLAG_NONE
            });
        }
    }

    // Add other headers
    TsArray* keys = (TsArray*)headers->GetKeys();
    if (keys) {
        for (size_t i = 0; i < keys->Length(); i++) {
            TsValue keyVal = keys->Get(i);
            if (keyVal.type != ValueType::STRING_PTR || !keyVal.ptr_val) continue;

            TsString* key = (TsString*)keyVal.ptr_val;
            std::string keyStr = key->ToUtf8();

            // Skip pseudo-headers we already handled
            if (keyStr[0] == ':') continue;

            TsValue val = headers->Get(keyVal);
            if (val.type == ValueType::STRING_PTR && val.ptr_val) {
                TsString* valStr = (TsString*)val.ptr_val;
                headerStorage.push_back(keyStr);
                headerStorage.push_back(valStr->ToUtf8());
                nva.push_back({
                    (uint8_t*)headerStorage[headerStorage.size()-2].c_str(),
                    (uint8_t*)headerStorage[headerStorage.size()-1].c_str(),
                    headerStorage[headerStorage.size()-2].length(),
                    headerStorage[headerStorage.size()-1].length(),
                    NGHTTP2_NV_FLAG_NONE
                });
            }
        }
    }

    // Submit PUSH_PROMISE - returns the promised stream ID
    int32_t promisedStreamId = nghttp2_submit_push_promise(
        session->session, NGHTTP2_FLAG_NONE, id, nva.data(), nva.size(), nullptr
    );

    if (promisedStreamId < 0) {
        if (cbFunc) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Failed to create push promise"));
            ts_call_3(cbFunc, (TsValue*)err, nullptr, nullptr);
        }
        return;
    }

    // Create the new server stream for the push
    TsServerHttp2Stream* pushStream = TsServerHttp2Stream::Create(session, promisedStreamId);
    pushStream->sentHeaders = headers;

    // Register the stream with the session
    session->AddStream(promisedStreamId, pushStream);

    session->SendPendingData();

    // Call callback with (null, pushStream, headers)
    if (cbFunc) {
        TsObject* objPtr = dynamic_cast<TsObject*>(pushStream);
        ts_call_3(cbFunc, nullptr, (TsValue*)ts_value_make_object(objPtr), (TsValue*)ts_value_make_object(headers));
    }
}

void TsServerHttp2Stream::Respond(TsMap* headers, TsValue* options) {
    if (!session || !session->session || closed || headersSent) return;

    headersSent = true;

    // Build header array
    std::vector<nghttp2_nv> nva;

    // Add :status pseudo-header
    std::string status = "200";
    if (headers) {
        TsString* statusKey = TsString::Create(":status");
        TsValue statusVal = headers->Get(TsValue(statusKey));
        if (statusVal.type == ValueType::STRING_PTR && statusVal.ptr_val) {
            TsString* statusStr = (TsString*)statusVal.ptr_val;
            status = statusStr->ToUtf8();
        }
    }

    nva.push_back({(uint8_t*)":status", (uint8_t*)status.c_str(),
                   7, status.length(), NGHTTP2_NV_FLAG_NONE});

    // TODO: Add other headers

    nghttp2_submit_response(session->session, id, nva.data(), nva.size(), nullptr);
    session->SendPendingData();
}

void TsServerHttp2Stream::RespondWithFD(int fd, TsMap* headers, TsValue* options) {
    if (!session || !session->session || closed || headersSent) return;
    if (fd < 0) return;

    // Parse options: offset, length
    int64_t offset = 0;
    int64_t length = -1;  // -1 means read to end

    if (options && options->type == ValueType::OBJECT_PTR) {
        TsMap* opts = dynamic_cast<TsMap*>((TsObject*)options->ptr_val);
        if (opts) {
            TsValue offsetVal = opts->Get(TsValue(TsString::Create("offset")));
            if (offsetVal.type == ValueType::NUMBER_INT) offset = offsetVal.i_val;

            TsValue lengthVal = opts->Get(TsValue(TsString::Create("length")));
            if (lengthVal.type == ValueType::NUMBER_INT) length = lengthVal.i_val;
        }
    }

    // Get file size using fstat
    uv_fs_t statReq;
    int statResult = uv_fs_fstat(uv_default_loop(), &statReq, fd, nullptr);
    if (statResult < 0) {
        uv_fs_req_cleanup(&statReq);
        // Emit error
        TsValue* err = (TsValue*)ts_error_create(TsString::Create("Failed to stat file descriptor"));
        void* args[] = { err };
        Emit("error", 1, args);
        return;
    }

    int64_t fileSize = statReq.statbuf.st_size;
    uv_fs_req_cleanup(&statReq);

    // Calculate actual length to read
    if (length < 0 || offset + length > fileSize) {
        length = fileSize - offset;
    }

    // Add Content-Length header if not present
    TsMap* responseHeaders = headers ? headers : TsMap::Create();
    TsString* clKey = TsString::Create("content-length");
    TsValue clVal = responseHeaders->Get(TsValue(clKey));
    if (clVal.type == ValueType::UNDEFINED) {
        responseHeaders->Set(TsValue(clKey), TsValue(TsString::Create(std::to_string(length).c_str())));
    }

    // Send response headers
    Respond(responseHeaders, nullptr);

    // Read file in chunks and write to stream
    const size_t chunkSize = 16384;
    std::vector<uint8_t> buffer(chunkSize);
    int64_t bytesRemaining = length;
    int64_t currentOffset = offset;

    while (bytesRemaining > 0 && !closed && !destroyed) {
        size_t toRead = (size_t)(std::min)((int64_t)chunkSize, bytesRemaining);

        uv_buf_t uvBuf = uv_buf_init((char*)buffer.data(), (unsigned int)toRead);
        uv_fs_t readReq;
        int bytesRead = uv_fs_read(uv_default_loop(), &readReq, fd, &uvBuf, 1, currentOffset, nullptr);
        uv_fs_req_cleanup(&readReq);

        if (bytesRead <= 0) break;

        // Write to HTTP/2 stream
        Write(buffer.data(), bytesRead);

        currentOffset += bytesRead;
        bytesRemaining -= bytesRead;
    }

    // End the stream
    End();
}

void TsServerHttp2Stream::RespondWithFile(const char* path, TsMap* headers, TsValue* options) {
    if (!session || !session->session || closed || headersSent) return;
    if (!path) return;

    // Parse options: offset, length, onError callback
    int64_t offset = 0;
    int64_t length = -1;
    void* onError = nullptr;

    if (options && options->type == ValueType::OBJECT_PTR) {
        TsMap* opts = dynamic_cast<TsMap*>((TsObject*)options->ptr_val);
        if (opts) {
            TsValue offsetVal = opts->Get(TsValue(TsString::Create("offset")));
            if (offsetVal.type == ValueType::NUMBER_INT) offset = offsetVal.i_val;

            TsValue lengthVal = opts->Get(TsValue(TsString::Create("length")));
            if (lengthVal.type == ValueType::NUMBER_INT) length = lengthVal.i_val;

            TsValue onErrorVal = opts->Get(TsValue(TsString::Create("onError")));
            if (onErrorVal.type == ValueType::FUNCTION_PTR || onErrorVal.type == ValueType::OBJECT_PTR) {
                onError = onErrorVal.ptr_val;
            }
        }
    }

    // Open the file
    uv_fs_t openReq;
    int fd = uv_fs_open(uv_default_loop(), &openReq, path, O_RDONLY, 0, nullptr);
    uv_fs_req_cleanup(&openReq);

    if (fd < 0) {
        // File open failed
        if (onError) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Failed to open file"));
            ts_call_1((TsValue*)onError, err);
        } else {
            // Emit error on stream
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Failed to open file"));
            void* args[] = { err };
            Emit("error", 1, args);
        }
        return;
    }

    // Get file size
    uv_fs_t statReq;
    int statResult = uv_fs_fstat(uv_default_loop(), &statReq, fd, nullptr);
    if (statResult < 0) {
        uv_fs_req_cleanup(&statReq);
        uv_fs_t closeReq;
        uv_fs_close(uv_default_loop(), &closeReq, fd, nullptr);
        uv_fs_req_cleanup(&closeReq);

        if (onError) {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Failed to stat file"));
            ts_call_1((TsValue*)onError, err);
        } else {
            TsValue* err = (TsValue*)ts_error_create(TsString::Create("Failed to stat file"));
            void* args[] = { err };
            Emit("error", 1, args);
        }
        return;
    }

    int64_t fileSize = statReq.statbuf.st_size;
    uv_fs_req_cleanup(&statReq);

    // Calculate actual length
    if (length < 0 || offset + length > fileSize) {
        length = fileSize - offset;
    }

    // Set up response headers
    TsMap* responseHeaders = headers ? headers : TsMap::Create();

    // Add Content-Length if not present
    TsString* clKey = TsString::Create("content-length");
    TsValue clVal = responseHeaders->Get(TsValue(clKey));
    if (clVal.type == ValueType::UNDEFINED) {
        responseHeaders->Set(TsValue(clKey), TsValue(TsString::Create(std::to_string(length).c_str())));
    }

    // Try to set Content-Type based on file extension
    TsString* ctKey = TsString::Create("content-type");
    TsValue ctVal = responseHeaders->Get(TsValue(ctKey));
    if (ctVal.type == ValueType::UNDEFINED) {
        std::string pathStr = path;
        std::string contentType = "application/octet-stream";

        size_t dotPos = pathStr.rfind('.');
        if (dotPos != std::string::npos) {
            std::string ext = pathStr.substr(dotPos + 1);
            // Convert to lowercase
            for (char& c : ext) c = tolower(c);

            if (ext == "html" || ext == "htm") contentType = "text/html";
            else if (ext == "css") contentType = "text/css";
            else if (ext == "js") contentType = "application/javascript";
            else if (ext == "json") contentType = "application/json";
            else if (ext == "txt") contentType = "text/plain";
            else if (ext == "png") contentType = "image/png";
            else if (ext == "jpg" || ext == "jpeg") contentType = "image/jpeg";
            else if (ext == "gif") contentType = "image/gif";
            else if (ext == "svg") contentType = "image/svg+xml";
            else if (ext == "pdf") contentType = "application/pdf";
            else if (ext == "xml") contentType = "application/xml";
        }

        responseHeaders->Set(TsValue(ctKey), TsValue(TsString::Create(contentType.c_str())));
    }

    // Send response headers
    Respond(responseHeaders, nullptr);

    // Read and stream file content
    const size_t chunkSize = 16384;
    std::vector<uint8_t> buffer(chunkSize);
    int64_t bytesRemaining = length;
    int64_t currentOffset = offset;

    while (bytesRemaining > 0 && !closed && !destroyed) {
        size_t toRead = (size_t)(std::min)((int64_t)chunkSize, bytesRemaining);

        uv_buf_t uvBuf = uv_buf_init((char*)buffer.data(), (unsigned int)toRead);
        uv_fs_t readReq;
        int bytesRead = uv_fs_read(uv_default_loop(), &readReq, fd, &uvBuf, 1, currentOffset, nullptr);
        uv_fs_req_cleanup(&readReq);

        if (bytesRead <= 0) break;

        Write(buffer.data(), bytesRead);

        currentOffset += bytesRead;
        bytesRemaining -= bytesRead;
    }

    // Close file descriptor
    uv_fs_t closeReq;
    uv_fs_close(uv_default_loop(), &closeReq, fd, nullptr);
    uv_fs_req_cleanup(&closeReq);

    // End the stream
    End();
}

// =============================================================================
// TsClientHttp2Stream Implementation
// =============================================================================

TsClientHttp2Stream::TsClientHttp2Stream(TsHttp2Session* session, int32_t streamId)
    : TsHttp2Stream(session, streamId) {
}

TsClientHttp2Stream* TsClientHttp2Stream::Create(TsHttp2Session* session, int32_t streamId) {
    void* mem = ts_alloc(sizeof(TsClientHttp2Stream));
    return new (mem) TsClientHttp2Stream(session, streamId);
}

// =============================================================================
// TsHttp2Server Implementation
// =============================================================================

// Context for routing socket data to HTTP/2 session
struct Http2DataContext {
    TsServerHttp2Session* session;
};

// Native callback for handling socket data
static void* http2_socket_data_handler(void* ctx, int argc, void** argv) {
    Http2DataContext* dataCtx = (Http2DataContext*)ctx;
    if (!dataCtx || !dataCtx->session || argc < 1) return nullptr;

    // argv[0] is the data chunk (TsBuffer)
    TsBuffer* chunk = dynamic_cast<TsBuffer*>((TsObject*)ts_value_get_object((TsValue*)argv[0]));
    if (!chunk) chunk = dynamic_cast<TsBuffer*>((TsObject*)argv[0]);

    if (chunk && chunk->GetData()) {
        dataCtx->session->ProcessData(chunk->GetData(), chunk->GetLength());
    }

    return nullptr;
}

// Native callback for handling socket close
static void* http2_socket_close_handler(void* ctx, int argc, void** argv) {
    Http2DataContext* dataCtx = (Http2DataContext*)ctx;
    if (!dataCtx || !dataCtx->session) return nullptr;

    dataCtx->session->Destroy(nullptr, 0);
    return nullptr;
}

// Native callback for handling socket error
static void* http2_socket_error_handler(void* ctx, int argc, void** argv) {
    Http2DataContext* dataCtx = (Http2DataContext*)ctx;
    if (!dataCtx || !dataCtx->session) return nullptr;

    void* err = (argc > 0) ? argv[0] : ts_error_create(TsString::Create("Socket error"));
    void* args[] = { err };
    dataCtx->session->Emit("error", 1, args);
    return nullptr;
}

TsHttp2Server::TsHttp2Server() : TsServer() {
}

TsHttp2Server* TsHttp2Server::Create(TsValue* options, void* callback) {
    void* mem = ts_alloc(sizeof(TsHttp2Server));
    TsHttp2Server* server = new (mem) TsHttp2Server();
    server->streamCallback = callback;
    return server;
}

void TsHttp2Server::HandleConnection(int status) {
    if (status < 0) return;

    // Create TCP handle for the client
    uv_tcp_t* client = (uv_tcp_t*)ts_alloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), client);

    if (uv_accept((uv_stream_t*)handle, (uv_stream_t*)client) != 0) {
        uv_close((uv_handle_t*)client, nullptr);
        return;
    }

    // Create TsSocket with the accepted handle
    void* mem = ts_alloc(sizeof(TsSocket));
    TsSocket* clientSocket = new (mem) TsSocket(client);

    // Create HTTP/2 session for this connection
    TsServerHttp2Session* session = TsServerHttp2Session::Create(clientSocket);

    // Create context for data routing
    Http2DataContext* ctx = (Http2DataContext*)ts_alloc(sizeof(Http2DataContext));
    ctx->session = session;

    // Register data handler on socket
    TsValue* dataHandler = ts_value_make_native_function((void*)http2_socket_data_handler, ctx);
    clientSocket->On("data", dataHandler);

    // Register close handler
    TsValue* closeHandler = ts_value_make_native_function((void*)http2_socket_close_handler, ctx);
    clientSocket->On("close", closeHandler);

    // Register error handler
    TsValue* errorHandler = ts_value_make_native_function((void*)http2_socket_error_handler, ctx);
    clientSocket->On("error", errorHandler);

    // Start reading from socket
    clientSocket->Resume();

    // Emit 'session' event on the server
    void* sessionArgs[] = { session };
    Emit("session", 1, sessionArgs);

    // If there's a stream callback, register it on the session
    if (streamCallback) {
        session->On("stream", streamCallback);
    }
}

// =============================================================================
// TsHttp2SecureServer Implementation
// =============================================================================

TsHttp2SecureServer::TsHttp2SecureServer(TsValue* options) : TsHttp2Server() {
    tlsOptions = options;
}

TsHttp2SecureServer* TsHttp2SecureServer::Create(TsValue* options, void* callback) {
    void* mem = ts_alloc(sizeof(TsHttp2SecureServer));
    TsHttp2SecureServer* server = new (mem) TsHttp2SecureServer(options);
    server->streamCallback = callback;
    return server;
}

void TsHttp2SecureServer::HandleConnection(int status) {
    if (status < 0) return;

    // Create TCP handle for the client
    uv_tcp_t* client = (uv_tcp_t*)ts_alloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), client);

    if (uv_accept((uv_stream_t*)handle, (uv_stream_t*)client) != 0) {
        uv_close((uv_handle_t*)client, nullptr);
        return;
    }

    // Create TsSecureSocket with the accepted handle (server mode)
    void* mem = ts_alloc(sizeof(TsSecureSocket));
    TsSecureSocket* clientSocket = new (mem) TsSecureSocket(client, true);

    // Apply TLS options (certificate, key, etc.) if provided
    if (tlsOptions) {
        TsMap* optsMap = dynamic_cast<TsMap*>((TsObject*)ts_value_get_object(tlsOptions));
        if (!optsMap) optsMap = dynamic_cast<TsMap*>((TsObject*)tlsOptions);

        if (optsMap) {
            // Get cert and key
            TsValue certVal = optsMap->Get(TsValue(TsString::Create("cert")));
            TsValue keyVal = optsMap->Get(TsValue(TsString::Create("key")));

            if (certVal.type == ValueType::OBJECT_PTR && keyVal.type == ValueType::OBJECT_PTR) {
                TsBuffer* cert = dynamic_cast<TsBuffer*>((TsObject*)certVal.ptr_val);
                TsBuffer* key = dynamic_cast<TsBuffer*>((TsObject*)keyVal.ptr_val);
                if (cert && key) {
                    clientSocket->SetCertificate(cert, key);
                }
            }
        }
    }

    // Set ALPN to prefer h2
    TsArray* alpnProtocols = TsArray::Create();
    ts_array_push(alpnProtocols, ts_value_make_string(TsString::Create("h2")));
    clientSocket->SetALPNProtocols(alpnProtocols);

    // Create HTTP/2 session for this connection
    TsServerHttp2Session* session = TsServerHttp2Session::Create((TsSocket*)clientSocket);
    session->encrypted = true;

    // Create context for data routing
    Http2DataContext* ctx = (Http2DataContext*)ts_alloc(sizeof(Http2DataContext));
    ctx->session = session;

    // Register data handler on socket
    TsValue* dataHandler = ts_value_make_native_function((void*)http2_socket_data_handler, ctx);
    clientSocket->On("data", dataHandler);

    // Register close handler
    TsValue* closeHandler = ts_value_make_native_function((void*)http2_socket_close_handler, ctx);
    clientSocket->On("close", closeHandler);

    // Register error handler
    TsValue* errorHandler = ts_value_make_native_function((void*)http2_socket_error_handler, ctx);
    clientSocket->On("error", errorHandler);

    // Start reading from socket (TLS handshake will happen first)
    clientSocket->Resume();

    // Emit 'session' event on the server
    void* sessionArgs[] = { session };
    Emit("session", 1, sessionArgs);

    // If there's a stream callback, register it on the session
    if (streamCallback) {
        session->On("stream", streamCallback);
    }
}

// =============================================================================
// C API Implementation
// =============================================================================

