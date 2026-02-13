// HTTP module extension - extern "C" wrappers
// Class implementations stay in tsruntime (TsHttpServer, TsClientRequest, etc.)

#include "TsHttp.h"  // Runtime header with class definitions and extern "C" declarations
#include "TsRuntime.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsBuffer.h"
#include "GC.h"

#include <cstring>
#include <cstdio>

extern "C" {

// ===== Server creation and management =====

void* ts_http_create_server(TsValue* options, void* callback) {
    // Node.js convention: createServer(callback) or createServer(options, callback)
    // When called with just a callback, the ext.json lowering passes the function
    // pointer as the first arg (options) and callback is null.
    if (options && !callback) {
        callback = (void*)options;
        options = nullptr;
    }

    return TsHttpServer::Create(options, callback);
}

void* ts_https_create_server(TsValue* options, void* callback) {
    // Same swap logic as HTTP createServer
    if (options && !callback) {
        uint32_t magic = *(uint32_t*)((char*)options);
        if (magic == 0x434C5352) {
            callback = (void*)options;
            options = nullptr;
        }
    }
    return TsHttpsServer::Create(options, callback);
}

void ts_http_server_listen(void* server, void* port_val, void* host, void* callback) {
    int port = 0;
    if (port_val) {
        TsValue pv = nanbox_to_tagged((TsValue*)port_val);
        if (pv.type == ValueType::NUMBER_DBL) {
            port = (int)pv.d_val;
        } else if (pv.type == ValueType::NUMBER_INT) {
            port = (int)pv.i_val;
        }
    }
    const char* hostStr = nullptr;
    if (host && !ts_value_is_null((TsValue*)host) && !ts_value_is_undefined((TsValue*)host)) {
        void* raw = ts_value_get_string((TsValue*)host);
        if (raw) hostStr = ((TsString*)raw)->ToUtf8();
    }
    ((TsHttpServer*)server)->Listen(port, hostStr, callback);
}

// Helper: extract raw pointer from potentially boxed TsValue*
static void* unbox_ptr(void* ptr) {
    void* raw = ts_value_get_object((TsValue*)ptr);
    return raw ? raw : ptr;
}

void ts_http_server_response_write_head(void* res, int64_t status, TsValue* headers) {
    TsServerResponse* r = (TsServerResponse*)unbox_ptr(res);
    TsObject* h = nullptr;
    if (headers) {
        TsValue hv = nanbox_to_tagged(headers);
        if (hv.type == ValueType::OBJECT_PTR && hv.ptr_val) {
            h = (TsObject*)hv.ptr_val;
        }
    }
    r->WriteHead((int)status, h);
}

bool ts_http_server_response_write(void* res, void* data) {
    TsServerResponse* r = (TsServerResponse*)unbox_ptr(res);
    if (!data) return false;
    TsValue val = nanbox_to_tagged((TsValue*)data);
    if (val.type == ValueType::STRING_PTR && val.ptr_val) {
        TsString* str = (TsString*)val.ptr_val;
        return r->Write((void*)str->ToUtf8(), str->Length());
    } else if (val.type == ValueType::OBJECT_PTR && val.ptr_val) {
        TsBuffer* buf = (TsBuffer*)val.ptr_val;
        return r->Write(buf->GetData(), buf->GetLength());
    }
    return false;
}

void ts_http_server_response_end(void* res, void* data) {
    TsServerResponse* r = (TsServerResponse*)unbox_ptr(res);
    if (data) {
        r->End(*(TsValue*)data);
    } else {
        r->End();
    }
}

// ===== IncomingMessage property getters =====

void* ts_incoming_message_url(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)unbox_ptr(msg);
    return m->url;
}

void* ts_incoming_message_method(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)unbox_ptr(msg);
    return m->method;
}

void* ts_incoming_message_headers(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)unbox_ptr(msg);
    return m->headers;
}

void* ts_incoming_message_statusCode(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)unbox_ptr(msg);
    return ts_value_make_int(m->statusCode);
}

void* ts_incoming_message_statusMessage(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)unbox_ptr(msg);
    return m->statusMessage;
}

void* ts_incoming_message_httpVersion(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)unbox_ptr(msg);
    return m->httpVersion ? m->httpVersion : TsString::Create("");
}

bool ts_incoming_message_complete(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)unbox_ptr(msg);
    return m->complete;
}

void* ts_incoming_message_rawHeaders(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)unbox_ptr(msg);
    return m->rawHeaders ? m->rawHeaders : TsArray::Create();
}

void* ts_incoming_message_rawTrailers(void* ctx, void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsIncomingMessage* m = dynamic_cast<TsIncomingMessage*>((TsObject*)rawPtr);
    if (!m) return TsArray::Create();
    return m->rawTrailers ? m->rawTrailers : TsArray::Create();
}

void* ts_incoming_message_trailers(void* ctx, void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsIncomingMessage* m = dynamic_cast<TsIncomingMessage*>((TsObject*)rawPtr);
    if (!m) {
        return TsMap::Create();
    }
    if (!m->trailers) {
        m->trailers = TsMap::Create();
    }
    return m->trailers;
}

void* ts_incoming_message_socket(void* ctx, void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsIncomingMessage* m = dynamic_cast<TsIncomingMessage*>((TsObject*)rawPtr);
    if (!m || !m->socket) {
        return nullptr;
    }
    return m->socket;
}

bool ts_incoming_message_aborted(void* ctx, void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsIncomingMessage* m = dynamic_cast<TsIncomingMessage*>((TsObject*)rawPtr);
    if (!m) {
        return false;
    }
    return m->aborted;
}

// ===== OutgoingMessage/ServerResponse property getters =====

bool ts_outgoing_message_get_headers_sent(void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsOutgoingMessage* m = dynamic_cast<TsOutgoingMessage*>((TsObject*)rawPtr);
    if (!m) return false;
    return m->headersSent;
}

bool ts_outgoing_message_get_writable_ended(void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsOutgoingMessage* m = dynamic_cast<TsOutgoingMessage*>((TsObject*)rawPtr);
    if (!m) return false;
    return m->writableEnded;
}

bool ts_outgoing_message_get_writable_finished(void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsOutgoingMessage* m = dynamic_cast<TsOutgoingMessage*>((TsObject*)rawPtr);
    if (!m) return false;
    return m->writableFinished;
}

int64_t ts_server_response_get_status_code(void* res) {
    void* rawPtr = ts_nanbox_safe_unbox(res);
    TsServerResponse* r = dynamic_cast<TsServerResponse*>((TsObject*)rawPtr);
    if (!r) return 200;
    return r->statusCode;
}

void* ts_server_response_get_status_message(void* res) {
    void* rawPtr = ts_nanbox_safe_unbox(res);
    TsServerResponse* r = dynamic_cast<TsServerResponse*>((TsObject*)rawPtr);
    if (!r) return nullptr;
    return r->statusMessage;
}

void ts_server_response_set_status_code(void* res, int64_t code) {
    void* rawPtr = ts_nanbox_safe_unbox(res);
    TsServerResponse* r = dynamic_cast<TsServerResponse*>((TsObject*)rawPtr);
    if (!r) return;
    r->statusCode = (int)code;
}

void ts_server_response_set_status_message(void* res, void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(res);
    TsServerResponse* r = dynamic_cast<TsServerResponse*>((TsObject*)rawPtr);
    if (!r) return;

    void* rawMsg = ts_value_get_string((TsValue*)msg);
    if (rawMsg) {
        r->statusMessage = (TsString*)rawMsg;
    } else if (msg) {
        r->statusMessage = (TsString*)msg;
    }
}

void ts_server_response_set_timeout(void* res, int64_t msecs, void* callback) {
    void* rawPtr = ts_nanbox_safe_unbox(res);
    TsServerResponse* r = dynamic_cast<TsServerResponse*>((TsObject*)rawPtr);
    if (!r) return;
    r->SetTimeout((int)msecs, callback);
}

void ts_server_response_add_trailers(void* res, void* trailers) {
    void* rawPtr = ts_nanbox_safe_unbox(res);
    TsServerResponse* r = dynamic_cast<TsServerResponse*>((TsObject*)rawPtr);
    if (!r) return;

    void* rawTrailers = ts_nanbox_safe_unbox(trailers);
    TsMap* trailersMap = dynamic_cast<TsMap*>((TsObject*)rawTrailers);
    if (trailersMap) {
        r->AddTrailers(trailersMap);
    }
}

// ===== ClientRequest property getters =====

void* ts_client_request_get_path(void* req) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return nullptr;
    return TsString::Create(r->path.c_str());
}

void* ts_client_request_get_method(void* req) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return nullptr;
    return TsString::Create(r->method.c_str());
}

void* ts_client_request_get_host(void* req) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return nullptr;
    return TsString::Create(r->host.c_str());
}

void* ts_client_request_get_protocol(void* req) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return nullptr;
    return TsString::Create(r->is_https ? "https:" : "http:");
}

void* ts_client_request_get_header(void* req, void* name) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return nullptr;

    void* rawName = ts_value_get_string((TsValue*)name);
    if (!rawName) rawName = name;
    TsString* nameStr = (TsString*)rawName;
    if (!nameStr) return nullptr;

    if (r->headers) {
        TsValue* val = r->GetHeader(nameStr);
        return val;
    }
    return nullptr;
}

void ts_client_request_set_header(void* req, void* name, void* value) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return;

    void* rawName = ts_value_get_string((TsValue*)name);
    if (!rawName) rawName = name;
    TsString* nameStr = (TsString*)rawName;
    if (!nameStr) return;

    TsValue* val = (TsValue*)value;
    r->SetHeader(nameStr, val);
}

void* ts_client_request_get_socket(void* req) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return nullptr;
    return r->socket;
}

void ts_client_request_set_timeout(void* req, int64_t msecs, void* callback) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return;
    r->SetTimeout((int)msecs, callback);
}

void ts_client_request_set_no_delay(void* req, int64_t noDelay) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return;
    r->SetNoDelay(noDelay != 0);
}

void ts_client_request_set_socket_keep_alive(void* req, int64_t enable, int64_t initialDelay) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return;
    r->SetSocketKeepAlive(enable != 0, (int)initialDelay);
}

bool ts_client_request_get_reused_socket(void* req) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return false;
    return r->reusedSocket;
}

int64_t ts_client_request_get_max_headers_count(void* req) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return 2000;
    return r->maxHeadersCount;
}

void ts_client_request_set_max_headers_count(void* req, int64_t count) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return;
    r->maxHeadersCount = (int)count;
}

void* ts_client_request_get_raw_header_names(void* req) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return TsArray::Create();
    return r->GetRawHeaderNames();
}

// ===== HTTP request functions =====

void* ts_http_request(TsValue* options, void* callback) {
    TsClientRequest* req = TsClientRequest::Create(options, callback);
    return static_cast<TsEventEmitter*>(req);
}

void* ts_http_get(TsValue* options, void* callback) {
    TsClientRequest* req = TsClientRequest::Create(options, callback);
    req->End();
    return static_cast<TsEventEmitter*>(req);
}

void* ts_https_request(TsValue* options, void* callback) {
    TsClientRequest* req = TsClientRequest::Create(options, callback, true);
    return static_cast<TsEventEmitter*>(req);
}

void* ts_https_get(TsValue* options, void* callback) {
    TsClientRequest* req = TsClientRequest::Create(options, callback, true);
    req->End();
    return static_cast<TsEventEmitter*>(req);
}

// ===== HTTP module utilities =====

void* ts_http_get_methods() {
    static TsArray* methods = nullptr;
    if (!methods) {
        methods = TsArray::Create();
        const char* methodNames[] = {
            "ACL", "BIND", "CHECKOUT", "CONNECT", "COPY", "DELETE", "GET", "HEAD",
            "LINK", "LOCK", "M-SEARCH", "MERGE", "MKACTIVITY", "MKCALENDAR", "MKCOL",
            "MOVE", "NOTIFY", "OPTIONS", "PATCH", "POST", "PRI", "PROPFIND", "PROPPATCH",
            "PURGE", "PUT", "REBIND", "REPORT", "SEARCH", "SOURCE", "SUBSCRIBE", "TRACE",
            "UNBIND", "UNLINK", "UNLOCK", "UNSUBSCRIBE"
        };
        for (const char* m : methodNames) {
            TsString* str = TsString::Create(m);
            TsValue* val = ts_value_make_string(str);
            methods->Push((int64_t)val);
        }
    }
    return ts_value_make_object(methods);
}

void* ts_http_get_status_codes() {
    static TsMap* statusCodes = nullptr;
    if (!statusCodes) {
        statusCodes = TsMap::Create();
        struct StatusCode { int code; const char* message; };
        StatusCode codes[] = {
            {100, "Continue"}, {101, "Switching Protocols"}, {102, "Processing"},
            {103, "Early Hints"},
            {200, "OK"}, {201, "Created"}, {202, "Accepted"},
            {203, "Non-Authoritative Information"}, {204, "No Content"},
            {205, "Reset Content"}, {206, "Partial Content"},
            {207, "Multi-Status"}, {208, "Already Reported"}, {226, "IM Used"},
            {300, "Multiple Choices"}, {301, "Moved Permanently"}, {302, "Found"},
            {303, "See Other"}, {304, "Not Modified"}, {305, "Use Proxy"},
            {307, "Temporary Redirect"}, {308, "Permanent Redirect"},
            {400, "Bad Request"}, {401, "Unauthorized"}, {402, "Payment Required"},
            {403, "Forbidden"}, {404, "Not Found"}, {405, "Method Not Allowed"},
            {406, "Not Acceptable"}, {407, "Proxy Authentication Required"},
            {408, "Request Timeout"}, {409, "Conflict"}, {410, "Gone"},
            {411, "Length Required"}, {412, "Precondition Failed"},
            {413, "Payload Too Large"}, {414, "URI Too Long"},
            {415, "Unsupported Media Type"}, {416, "Range Not Satisfiable"},
            {417, "Expectation Failed"}, {418, "I'm a Teapot"},
            {421, "Misdirected Request"}, {422, "Unprocessable Entity"},
            {423, "Locked"}, {424, "Failed Dependency"}, {425, "Too Early"},
            {426, "Upgrade Required"}, {428, "Precondition Required"},
            {429, "Too Many Requests"}, {431, "Request Header Fields Too Large"},
            {451, "Unavailable For Legal Reasons"},
            {500, "Internal Server Error"}, {501, "Not Implemented"},
            {502, "Bad Gateway"}, {503, "Service Unavailable"},
            {504, "Gateway Timeout"}, {505, "HTTP Version Not Supported"},
            {506, "Variant Also Negotiates"}, {507, "Insufficient Storage"},
            {508, "Loop Detected"}, {510, "Not Extended"},
            {511, "Network Authentication Required"}
        };
        for (const auto& sc : codes) {
            char keyBuf[16];
            snprintf(keyBuf, sizeof(keyBuf), "%d", sc.code);
            TsValue key;
            key.type = ValueType::STRING_PTR;
            key.ptr_val = TsString::Create(keyBuf);
            TsValue val;
            val.type = ValueType::STRING_PTR;
            val.ptr_val = TsString::Create(sc.message);
            statusCodes->Set(key, val);
        }
    }
    return ts_value_make_object(statusCodes);
}

int64_t ts_http_get_max_header_size() {
    return 16384;
}

void ts_http_validate_header_name(void* name) {
    if (!name) return;
    TsString* str = nullptr;
    TsValue decoded = nanbox_to_tagged((TsValue*)name);
    if (decoded.type == ValueType::STRING_PTR && decoded.ptr_val) {
        str = (TsString*)decoded.ptr_val;
    } else if (decoded.type == ValueType::OBJECT_PTR && decoded.ptr_val) {
        str = dynamic_cast<TsString*>((TsObject*)decoded.ptr_val);
    }
    if (!str) return;
    // Validation stub - would throw in real Node.js on invalid chars
}

void ts_http_validate_header_value(void* name, void* value) {
    if (!name || !value) return;
    // Validation stub - would throw in real Node.js on invalid chars
}

// ===== OutgoingMessage header methods =====

void ts_outgoing_message_set_header(void* msg, void* name, void* value) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsOutgoingMessage* m = dynamic_cast<TsOutgoingMessage*>((TsObject*)rawPtr);
    if (!m) return;

    void* rawName = ts_value_get_string((TsValue*)name);
    if (!rawName) rawName = name;
    TsString* nameStr = (TsString*)rawName;

    m->SetHeader(nameStr, (TsValue*)value);
}

void* ts_outgoing_message_get_header(void* msg, void* name) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsOutgoingMessage* m = dynamic_cast<TsOutgoingMessage*>((TsObject*)rawPtr);
    if (!m) return nullptr;

    void* rawName = ts_value_get_string((TsValue*)name);
    if (!rawName) rawName = name;
    TsString* nameStr = (TsString*)rawName;

    return m->GetHeader(nameStr);
}

void* ts_outgoing_message_get_headers(void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsOutgoingMessage* m = dynamic_cast<TsOutgoingMessage*>((TsObject*)rawPtr);
    if (!m) return TsMap::Create();
    return m->GetHeaders();
}

bool ts_outgoing_message_has_header(void* msg, void* name) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsOutgoingMessage* m = dynamic_cast<TsOutgoingMessage*>((TsObject*)rawPtr);
    if (!m) return false;

    void* rawName = ts_value_get_string((TsValue*)name);
    if (!rawName) rawName = name;
    TsString* nameStr = (TsString*)rawName;

    return m->HasHeader(nameStr);
}

void ts_outgoing_message_remove_header(void* msg, void* name) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsOutgoingMessage* m = dynamic_cast<TsOutgoingMessage*>((TsObject*)rawPtr);
    if (!m) return;

    void* rawName = ts_value_get_string((TsValue*)name);
    if (!rawName) rawName = name;
    TsString* nameStr = (TsString*)rawName;

    m->RemoveHeader(nameStr);
}

void* ts_outgoing_message_get_header_names(void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsOutgoingMessage* m = dynamic_cast<TsOutgoingMessage*>((TsObject*)rawPtr);
    if (!m) return TsArray::Create();
    return m->GetHeaderNames();
}

void ts_outgoing_message_flush_headers(void* msg) {
    void* rawPtr = ts_nanbox_safe_unbox(msg);
    TsOutgoingMessage* m = dynamic_cast<TsOutgoingMessage*>((TsObject*)rawPtr);
    if (!m) return;
    m->FlushHeaders();
}

// ===== Agent functions =====

// Global agents defined in runtime
extern TsHttpAgent* globalHttpAgent;
extern TsHttpsAgent* globalHttpsAgent;

void* ts_http_agent_create(TsValue* options) {
    return TsHttpAgent::Create(options);
}

void* ts_https_agent_create(TsValue* options) {
    return TsHttpsAgent::Create(options);
}

void* ts_http_get_global_agent() {
    if (!globalHttpAgent) {
        globalHttpAgent = TsHttpAgent::Create(nullptr);
    }
    return globalHttpAgent;
}

void* ts_https_get_global_agent() {
    if (!globalHttpsAgent) {
        globalHttpsAgent = TsHttpsAgent::Create(nullptr);
    }
    return globalHttpsAgent;
}

void ts_http_agent_destroy(void* agent) {
    if (!agent) return;
    TsHttpAgent* a = (TsHttpAgent*)agent;
    a->Destroy();
}

// Max idle HTTP parsers (owned by http extension)
int64_t maxIdleHttpParsers = 1000;

int64_t ts_http_get_max_idle_http_parsers() {
    return maxIdleHttpParsers;
}

void ts_http_set_max_idle_http_parsers(int64_t max) {
    maxIdleHttpParsers = max;
}

// ===== Server timeout/configuration =====

void ts_http_server_set_timeout(void* server, int64_t msecs, void* callback) {
    void* rawPtr = ts_nanbox_safe_unbox(server);
    TsHttpServer* s = dynamic_cast<TsHttpServer*>((TsObject*)rawPtr);
    if (!s) return;
    s->SetTimeout((int)msecs, callback);
}

int64_t ts_http_server_get_timeout(void* server) {
    void* rawPtr = ts_nanbox_safe_unbox(server);
    TsHttpServer* s = dynamic_cast<TsHttpServer*>((TsObject*)rawPtr);
    if (!s) return 0;
    return s->timeout;
}

int64_t ts_http_server_get_keep_alive_timeout(void* server) {
    void* rawPtr = ts_nanbox_safe_unbox(server);
    TsHttpServer* s = dynamic_cast<TsHttpServer*>((TsObject*)rawPtr);
    if (!s) return 5000;
    return s->keepAliveTimeout;
}

void ts_http_server_set_keep_alive_timeout(void* server, int64_t msecs) {
    void* rawPtr = ts_nanbox_safe_unbox(server);
    TsHttpServer* s = dynamic_cast<TsHttpServer*>((TsObject*)rawPtr);
    if (!s) return;
    s->keepAliveTimeout = (int)msecs;
}

int64_t ts_http_server_get_headers_timeout(void* server) {
    void* rawPtr = ts_nanbox_safe_unbox(server);
    TsHttpServer* s = dynamic_cast<TsHttpServer*>((TsObject*)rawPtr);
    if (!s) return 60000;
    return s->headersTimeout;
}

void ts_http_server_set_headers_timeout(void* server, int64_t msecs) {
    void* rawPtr = ts_nanbox_safe_unbox(server);
    TsHttpServer* s = dynamic_cast<TsHttpServer*>((TsObject*)rawPtr);
    if (!s) return;
    s->headersTimeout = (int)msecs;
}

int64_t ts_http_server_get_request_timeout(void* server) {
    void* rawPtr = ts_nanbox_safe_unbox(server);
    TsHttpServer* s = dynamic_cast<TsHttpServer*>((TsObject*)rawPtr);
    if (!s) return 0;
    return s->requestTimeout;
}

void ts_http_server_set_request_timeout(void* server, int64_t msecs) {
    void* rawPtr = ts_nanbox_safe_unbox(server);
    TsHttpServer* s = dynamic_cast<TsHttpServer*>((TsObject*)rawPtr);
    if (!s) return;
    s->requestTimeout = (int)msecs;
}

int64_t ts_http_server_get_max_headers_count(void* server) {
    void* rawPtr = ts_nanbox_safe_unbox(server);
    TsHttpServer* s = dynamic_cast<TsHttpServer*>((TsObject*)rawPtr);
    if (!s) return 2000;
    return s->maxHeadersCount;
}

void ts_http_server_set_max_headers_count(void* server, int64_t count) {
    void* rawPtr = ts_nanbox_safe_unbox(server);
    TsHttpServer* s = dynamic_cast<TsHttpServer*>((TsObject*)rawPtr);
    if (!s) return;
    s->maxHeadersCount = (int)count;
}

// ===== WebSocket constants =====

int64_t ts_websocket_connecting() { return 0; }  // WS_CONNECTING
int64_t ts_websocket_open() { return 1; }        // WS_OPEN
int64_t ts_websocket_closing() { return 2; }     // WS_CLOSING
int64_t ts_websocket_closed() { return 3; }      // WS_CLOSED

// ===== CloseEvent (WebSocket) =====

TsValue* ts_close_event_create(int64_t code, void* reason) {
    return ts_value_make_object(TsCloseEvent::Create(code, (TsString*)reason));
}

void* ts_close_event_get_code(void* event) {
    if (!event) return ts_value_make_int(0);
    TsCloseEvent* e = (TsCloseEvent*)ts_value_get_object((TsValue*)event);
    if (!e) e = (TsCloseEvent*)event;
    return ts_value_make_int(e->code);
}

void* ts_close_event_get_reason(void* event) {
    if (!event) return TsString::Create("");
    TsCloseEvent* e = (TsCloseEvent*)ts_value_get_object((TsValue*)event);
    if (!e) e = (TsCloseEvent*)event;
    return e->reason;
}

void* ts_close_event_get_was_clean(void* event) {
    if (!event) return ts_value_make_bool(false);
    TsCloseEvent* e = (TsCloseEvent*)ts_value_get_object((TsValue*)event);
    if (!e) e = (TsCloseEvent*)event;
    return ts_value_make_bool(e->wasClean);
}

// ===== MessageEvent (WebSocket) =====

TsValue* ts_message_event_create() {
    return ts_value_make_object(TsMessageEvent::Create());
}

void* ts_message_event_get_data(void* event) {
    if (!event) return nullptr;
    TsMessageEvent* e = (TsMessageEvent*)ts_value_get_object((TsValue*)event);
    if (!e) e = (TsMessageEvent*)event;
    return e->data;
}

void ts_message_event_set_data(void* event, void* data) {
    if (!event) return;
    TsMessageEvent* e = (TsMessageEvent*)ts_value_get_object((TsValue*)event);
    if (!e) e = (TsMessageEvent*)event;
    e->data = data;
}

void* ts_message_event_get_origin(void* event) {
    if (!event) return TsString::Create("");
    TsMessageEvent* e = (TsMessageEvent*)ts_value_get_object((TsValue*)event);
    if (!e) e = (TsMessageEvent*)event;
    return e->origin;
}

void* ts_message_event_get_last_event_id(void* event) {
    if (!event) return TsString::Create("");
    TsMessageEvent* e = (TsMessageEvent*)ts_value_get_object((TsValue*)event);
    if (!e) e = (TsMessageEvent*)event;
    return e->lastEventId;
}

void* ts_message_event_get_source(void* event) {
    if (!event) return nullptr;
    TsMessageEvent* e = (TsMessageEvent*)ts_value_get_object((TsValue*)event);
    if (!e) e = (TsMessageEvent*)event;
    return e->source;
}

void* ts_message_event_get_ports(void* event) {
    if (!event) return TsArray::Create();
    TsMessageEvent* e = (TsMessageEvent*)ts_value_get_object((TsValue*)event);
    if (!e) e = (TsMessageEvent*)event;
    return e->ports;
}

// ===== WebSocket =====

void* ts_websocket_create(void* url, void* protocols) {
    TsString* urlStr = nullptr;
    void* rawUrl = ts_value_get_string((TsValue*)url);
    if (rawUrl) {
        urlStr = (TsString*)rawUrl;
    } else {
        urlStr = (TsString*)url;
    }
    return TsWebSocket::Create(urlStr, (TsValue*)protocols);
}

void ts_websocket_send(void* ws, void* data) {
    if (!ws) return;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return;
    w->Send((TsValue*)data);
}

void ts_websocket_close(void* ws, int64_t code, void* reason) {
    if (!ws) return;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return;
    TsString* reasonStr = (TsString*)ts_value_get_string((TsValue*)reason);
    w->Close(code, reasonStr);
}

void ts_websocket_ping(void* ws, void* data) {
    if (!ws) return;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return;
    w->Ping((TsValue*)data);
}

void ts_websocket_pong(void* ws, void* data) {
    if (!ws) return;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return;
    w->Pong((TsValue*)data);
}

int64_t ts_websocket_get_ready_state(void* ws) {
    if (!ws) return 3;  // CLOSED
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return 3;
    return w->readyState;
}

void* ts_websocket_get_url(void* ws) {
    if (!ws) return TsString::Create("");
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return TsString::Create("");
    return w->url;
}

void* ts_websocket_get_protocol(void* ws) {
    if (!ws) return TsString::Create("");
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return TsString::Create("");
    return w->protocol;
}

void* ts_websocket_get_extensions(void* ws) {
    if (!ws) return TsString::Create("");
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return TsString::Create("");
    return w->extensions;
}

int64_t ts_websocket_get_buffered_amount(void* ws) {
    if (!ws) return 0;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return 0;
    return w->bufferedAmount;
}

void* ts_websocket_get_binary_type(void* ws) {
    if (!ws) return TsString::Create("blob");
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return TsString::Create("blob");
    return w->binaryType;
}

void ts_websocket_set_binary_type(void* ws, void* type) {
    if (!ws) return;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return;
    TsString* typeStr = (TsString*)ts_value_get_string((TsValue*)type);
    if (typeStr) w->binaryType = typeStr;
}

void ts_websocket_set_onopen(void* ws, void* callback) {
    if (!ws) return;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return;
    w->onopen = callback;
}

void ts_websocket_set_onmessage(void* ws, void* callback) {
    if (!ws) return;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return;
    w->onmessage = callback;
}

void ts_websocket_set_onclose(void* ws, void* callback) {
    if (!ws) return;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return;
    w->onclose = callback;
}

void ts_websocket_set_onerror(void* ws, void* callback) {
    if (!ws) return;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return;
    w->onerror = callback;
}

void* ts_websocket_get_onopen(void* ws) {
    if (!ws) return nullptr;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return nullptr;
    return w->onopen;
}

void* ts_websocket_get_onmessage(void* ws) {
    if (!ws) return nullptr;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return nullptr;
    return w->onmessage;
}

void* ts_websocket_get_onclose(void* ws) {
    if (!ws) return nullptr;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return nullptr;
    return w->onclose;
}

void* ts_websocket_get_onerror(void* ws) {
    if (!ws) return nullptr;
    void* rawWs = ts_nanbox_safe_unbox(ws);
    TsWebSocket* w = dynamic_cast<TsWebSocket*>((TsObject*)rawWs);
    if (!w) return nullptr;
    return w->onerror;
}

void ts_client_request_flush_headers(void* req) {
    void* rawPtr = ts_nanbox_safe_unbox(req);
    TsClientRequest* r = dynamic_cast<TsClientRequest*>((TsObject*)rawPtr);
    if (!r) return;
    // flushHeaders is a no-op hint - headers are sent on first write/end
}

} // extern "C"
