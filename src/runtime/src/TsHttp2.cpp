#include "TsHttp2.h"
#include "TsError.h"
#include "TsSymbol.h"
#include "GC.h"
#include <cstring>
#include <cstdio>

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

void TsClientHttp2Session::Init(const char* authority, TsValue* options) {
    this->authority = authority;
    connecting = true;

    // Initialize nghttp2 client session
    InitSession(false);

    // TODO: Connect to authority using TsSocket or TsSecureSocket
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

    // TODO: Send informational (1xx) headers
}

void TsServerHttp2Stream::PushStream(TsMap* headers, TsValue* options, void* callback) {
    if (!session || !session->session || closed || !pushAllowed) {
        // TODO: Call callback with error
        return;
    }

    // TODO: Implement PUSH_PROMISE
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
    // Respond using file descriptor
    Respond(headers, options);
    // TODO: Implement file descriptor reading
}

void TsServerHttp2Stream::RespondWithFile(const char* path, TsMap* headers, TsValue* options) {
    // Respond with file contents
    Respond(headers, options);
    // TODO: Implement file reading and streaming
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
    void* rawBuffer = ts_value_get_object((TsValue*)buffer);
    if (!rawBuffer) rawBuffer = buffer;

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
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->alpnProtocol : nullptr;
}

bool ts_http2_session_get_closed(void* session) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->closed : true;
}

bool ts_http2_session_get_connecting(void* session) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->connecting : false;
}

bool ts_http2_session_get_destroyed(void* session) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->destroyed : true;
}

bool ts_http2_session_get_encrypted(void* session) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->encrypted : false;
}

void* ts_http2_session_get_local_settings(void* session) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
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
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
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
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->socket : nullptr;
}

int64_t ts_http2_session_get_type(void* session) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->type : -1;
}

void* ts_http2_session_get_state(void* session) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    return s ? s->GetState() : nullptr;
}

// Session methods
void ts_http2_session_close(void* session, void* callback) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (s) s->Close(callback);
}

void ts_http2_session_destroy(void* session, void* error, int64_t code) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (s) s->Destroy(error, (int)code);
}

void ts_http2_session_goaway(void* session, int64_t code, int64_t lastStreamId, void* data) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (!s) return;

    TsBuffer* buf = nullptr;
    if (data) {
        void* rawData = ts_value_get_object((TsValue*)data);
        if (!rawData) rawData = data;
        buf = dynamic_cast<TsBuffer*>((TsObject*)rawData);
    }

    s->Goaway((int)code, (int)lastStreamId, buf);
}

bool ts_http2_session_ping(void* session, void* payload, void* callback) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (!s) return false;

    TsBuffer* buf = nullptr;
    if (payload) {
        void* rawPayload = ts_value_get_object((TsValue*)payload);
        if (!rawPayload) rawPayload = payload;
        buf = dynamic_cast<TsBuffer*>((TsObject*)rawPayload);
    }

    return s->Ping(buf, callback);
}

void ts_http2_session_ref(void* session) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (s) s->Ref();
}

void ts_http2_session_unref(void* session) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (s) s->Unref();
}

void ts_http2_session_set_timeout(void* session, int64_t msecs, void* callback) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (s) s->SetTimeout((int)msecs, callback);
}

void ts_http2_session_settings(void* session, void* settings) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsHttp2Session* s = dynamic_cast<TsHttp2Session*>((TsObject*)raw);
    if (!s) return;

    // TODO: Parse settings from TsMap
    TsHttp2Settings newSettings;
    s->Settings(&newSettings);
}

// ServerHttp2Session specific
void ts_http2_server_session_altsvc(void* session, void* alt, void* origin) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsServerHttp2Session* s = dynamic_cast<TsServerHttp2Session*>((TsObject*)raw);
    if (!s) return;

    void* rawAlt = ts_value_get_object((TsValue*)alt);
    if (!rawAlt) rawAlt = alt;
    TsString* altStr = dynamic_cast<TsString*>((TsObject*)rawAlt);

    void* rawOrigin = ts_value_get_object((TsValue*)origin);
    if (!rawOrigin) rawOrigin = origin;
    TsString* originStr = dynamic_cast<TsString*>((TsObject*)rawOrigin);

    s->Altsvc(altStr ? altStr->ToUtf8() : nullptr,
              originStr ? originStr->ToUtf8() : nullptr);
}

void ts_http2_server_session_origin(void* session, void* origins) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsServerHttp2Session* s = dynamic_cast<TsServerHttp2Session*>((TsObject*)raw);
    if (!s) return;

    void* rawOrigins = ts_value_get_object((TsValue*)origins);
    if (!rawOrigins) rawOrigins = origins;
    TsArray* originsArr = dynamic_cast<TsArray*>((TsObject*)rawOrigins);

    s->Origin(originsArr);
}

// ClientHttp2Session specific
void* ts_http2_client_session_request(void* session, void* headers, void* options) {
    void* raw = ts_value_get_object((TsValue*)session);
    if (!raw) raw = session;
    TsClientHttp2Session* s = dynamic_cast<TsClientHttp2Session*>((TsObject*)raw);
    if (!s) return nullptr;

    void* rawHeaders = ts_value_get_object((TsValue*)headers);
    if (!rawHeaders) rawHeaders = headers;
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
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->aborted : true;
}

int64_t ts_http2_stream_get_buffer_size(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? (int64_t)s->bufferSize : 0;
}

bool ts_http2_stream_get_closed(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->closed : true;
}

bool ts_http2_stream_get_destroyed(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->destroyed : true;
}

bool ts_http2_stream_get_end_after_headers(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->endAfterHeaders : false;
}

int64_t ts_http2_stream_get_id(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->id : -1;
}

bool ts_http2_stream_get_pending(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->pending : false;
}

int64_t ts_http2_stream_get_rst_code(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->rstCode : 0;
}

void* ts_http2_stream_get_sent_headers(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->sentHeaders : nullptr;
}

void* ts_http2_stream_get_sent_info_headers(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->sentInfoHeaders : nullptr;
}

void* ts_http2_stream_get_sent_trailers(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->sentTrailers : nullptr;
}

void* ts_http2_stream_get_session(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->session : nullptr;
}

void* ts_http2_stream_get_state(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    return s ? s->GetState() : nullptr;
}

// Stream methods
void ts_http2_stream_close(void* stream, int64_t code, void* callback) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    if (s) s->Close((int)code, callback);
}

void ts_http2_stream_priority(void* stream, void* options) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    if (s) s->Priority((TsValue*)options);
}

void ts_http2_stream_set_timeout(void* stream, int64_t msecs, void* callback) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsHttp2Stream* s = dynamic_cast<TsHttp2Stream*>((TsObject*)raw);
    if (s) s->SetTimeout((int)msecs, callback);
}

// ServerHttp2Stream specific
bool ts_http2_server_stream_get_headers_sent(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    return s ? s->headersSent : false;
}

bool ts_http2_server_stream_get_push_allowed(void* stream) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    return s ? s->pushAllowed : false;
}

void ts_http2_server_stream_additional_headers(void* stream, void* headers) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    if (!s) return;

    void* rawHeaders = ts_value_get_object((TsValue*)headers);
    if (!rawHeaders) rawHeaders = headers;
    TsMap* headersMap = dynamic_cast<TsMap*>((TsObject*)rawHeaders);

    s->AdditionalHeaders(headersMap);
}

void ts_http2_server_stream_push_stream(void* stream, void* headers, void* options, void* callback) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    if (!s) return;

    void* rawHeaders = ts_value_get_object((TsValue*)headers);
    if (!rawHeaders) rawHeaders = headers;
    TsMap* headersMap = dynamic_cast<TsMap*>((TsObject*)rawHeaders);

    s->PushStream(headersMap, (TsValue*)options, callback);
}

void ts_http2_server_stream_respond(void* stream, void* headers, void* options) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    if (!s) return;

    void* rawHeaders = ts_value_get_object((TsValue*)headers);
    if (!rawHeaders) rawHeaders = headers;
    TsMap* headersMap = dynamic_cast<TsMap*>((TsObject*)rawHeaders);

    s->Respond(headersMap, (TsValue*)options);
}

void ts_http2_server_stream_respond_with_fd(void* stream, int64_t fd, void* headers, void* options) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    if (!s) return;

    void* rawHeaders = ts_value_get_object((TsValue*)headers);
    if (!rawHeaders) rawHeaders = headers;
    TsMap* headersMap = dynamic_cast<TsMap*>((TsObject*)rawHeaders);

    s->RespondWithFD((int)fd, headersMap, (TsValue*)options);
}

void ts_http2_server_stream_respond_with_file(void* stream, void* path, void* headers, void* options) {
    void* raw = ts_value_get_object((TsValue*)stream);
    if (!raw) raw = stream;
    TsServerHttp2Stream* s = dynamic_cast<TsServerHttp2Stream*>((TsObject*)raw);
    if (!s) return;

    void* rawPath = ts_value_get_object((TsValue*)path);
    if (!rawPath) rawPath = path;
    TsString* pathStr = dynamic_cast<TsString*>((TsObject*)rawPath);

    void* rawHeaders = ts_value_get_object((TsValue*)headers);
    if (!rawHeaders) rawHeaders = headers;
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
    void* raw = ts_value_get_object((TsValue*)server);
    if (!raw) raw = server;
    TsHttp2Server* srv = dynamic_cast<TsHttp2Server*>((TsObject*)raw);
    if (!srv) return nullptr;

    // Host is optional - if provided, extract it
    const char* hostStr = nullptr;
    if (host) {
        void* rawHost = ts_value_get_object((TsValue*)host);
        if (!rawHost) rawHost = host;
        TsString* hostString = dynamic_cast<TsString*>((TsObject*)rawHost);
        if (hostString) {
            hostStr = hostString->ToUtf8();
        }
    }

    // For now, ignore host and just listen on the port
    srv->Listen((int)port, callback);
    return srv;
}

void ts_http2_server_close(void* server, void* callback) {
    void* raw = ts_value_get_object((TsValue*)server);
    if (!raw) raw = server;
    TsHttp2Server* srv = dynamic_cast<TsHttp2Server*>((TsObject*)raw);
    if (!srv) return;

    // Register callback for 'close' event if provided
    if (callback) {
        srv->Once("close", callback);
    }
    srv->Close();
}

void* ts_http2_server_address(void* server) {
    void* raw = ts_value_get_object((TsValue*)server);
    if (!raw) raw = server;
    TsHttp2Server* srv = dynamic_cast<TsHttp2Server*>((TsObject*)raw);
    if (!srv) return nullptr;

    return srv->Address();
}

void ts_http2_server_set_timeout(void* server, int64_t msecs, void* callback) {
    void* raw = ts_value_get_object((TsValue*)server);
    if (!raw) raw = server;
    TsHttp2Server* srv = dynamic_cast<TsHttp2Server*>((TsObject*)raw);
    if (!srv) return;

    // TODO: Implement server timeout - for now just emit 'timeout' event if callback provided
    if (callback) {
        srv->On("timeout", callback);
    }
}

} // extern "C"
