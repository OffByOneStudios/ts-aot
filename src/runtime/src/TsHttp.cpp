#include "TsHttp.h"
#include "TsSecureSocket.h"
#include "GC.h"
#include "TsRuntime.h"
#include "TsSocket.h"
#include "TsMap.h"
#include "TsArray.h"
#include <uv.h>
#include <llhttp.h>
#include <iostream>
#include <string>
#include <cstring>
#include <new>
#include <algorithm>
#include <random>
#include <openssl/sha.h>
#include <openssl/evp.h>

struct HttpContext {
    TsHttpServer* server;
    TsSocket* socket;
    llhttp_t parser;
    llhttp_settings_t settings;
    TsIncomingMessage* currentRequest;
    TsServerResponse* currentResponse;
    TsString* currentHeaderField;

    HttpContext(TsHttpServer* s, TsSocket* sock) 
        : server(s), socket(sock), currentRequest(nullptr), currentResponse(nullptr), currentHeaderField(nullptr) {
        parser.data = this;
    }
};

// TsIncomingMessage
TsIncomingMessage::TsIncomingMessage() : TsEventEmitter(), TsReadable(), method(nullptr), url(nullptr) {
    this->magic = MAGIC;
    headers = TsHeaders::Create();
}

TsIncomingMessage* TsIncomingMessage::Create() {
    void* mem = ts_alloc(sizeof(TsIncomingMessage));
    return new (mem) TsIncomingMessage();
}

void TsIncomingMessage::Pause() {
    TsReadable::Pause();
}

void TsIncomingMessage::Resume() {
    TsReadable::Resume();
}

// TsOutgoingMessage - base class for ServerResponse and ClientRequest
TsOutgoingMessage::TsOutgoingMessage() : TsEventEmitter(), TsWritable() {
    this->magic = MAGIC;
    headers = (TsMap*)ts_map_create();
}

void TsOutgoingMessage::SetHeader(TsString* name, TsValue* value) {
    if (!name || !headers) return;
    // Convert name to lowercase for case-insensitive matching
    std::string s = name->ToUtf8();
    for (auto& c : s) c = std::tolower(c);
    TsString* lowerName = TsString::Create(s.c_str());
    headers->Set(lowerName, value ? *value : TsValue());
}

TsValue* TsOutgoingMessage::GetHeader(TsString* name) {
    if (!name || !headers) return nullptr;
    std::string s = name->ToUtf8();
    for (auto& c : s) c = std::tolower(c);
    TsString* lowerName = TsString::Create(s.c_str());
    TsValue v = headers->Get(TsValue(lowerName));
    if (v.type == ValueType::UNDEFINED) return nullptr;
    TsValue* result = (TsValue*)ts_alloc(sizeof(TsValue));
    *result = v;
    return result;
}

void* TsOutgoingMessage::GetHeaders() {
    return headers;
}

bool TsOutgoingMessage::HasHeader(TsString* name) {
    if (!name || !headers) return false;
    std::string s = name->ToUtf8();
    for (auto& c : s) c = std::tolower(c);
    TsString* lowerName = TsString::Create(s.c_str());
    return headers->Has(TsValue(lowerName));
}

void TsOutgoingMessage::RemoveHeader(TsString* name) {
    if (!name || !headers) return;
    std::string s = name->ToUtf8();
    for (auto& c : s) c = std::tolower(c);
    TsString* lowerName = TsString::Create(s.c_str());
    headers->Delete(TsValue(lowerName));
}

// Helper to convert TsValue to int64_t for array storage
static int64_t valueToInt64(const TsValue& v) {
    switch (v.type) {
        case ValueType::NUMBER_INT: return v.i_val;
        case ValueType::NUMBER_DBL: return (int64_t)ts_value_make_double(v.d_val);
        case ValueType::BOOLEAN: return (int64_t)ts_value_make_bool(v.b_val);
        case ValueType::STRING_PTR: return (int64_t)v.ptr_val;  // String pointer stored directly
        case ValueType::OBJECT_PTR: 
        case ValueType::ARRAY_PTR:
        case ValueType::PROMISE_PTR:
            return (int64_t)v.ptr_val;  // Object pointer stored directly
        default: return 0;
    }
}

void TsOutgoingMessage::AppendHeader(TsString* name, TsValue* value) {
    if (!name || !value || !headers) return;
    std::string s = name->ToUtf8();
    for (auto& c : s) c = std::tolower(c);
    TsString* lowerName = TsString::Create(s.c_str());
    
    TsValue existing = headers->Get(TsValue(lowerName));
    if (existing.type == ValueType::UNDEFINED) {
        headers->Set(lowerName, *value);
    } else if (existing.type == ValueType::OBJECT_PTR) {
        // If it's already an array, append to it
        TsArray* arr = dynamic_cast<TsArray*>((TsObject*)existing.ptr_val);
        if (arr) {
            arr->Push(valueToInt64(*value));
        } else {
            // Create array with both values
            TsArray* newArr = TsArray::Create();
            newArr->Push(valueToInt64(existing));
            newArr->Push(valueToInt64(*value));
            headers->Set(lowerName, TsValue(newArr));
        }
    } else {
        // Create array with both values
        TsArray* arr = TsArray::Create();
        arr->Push(valueToInt64(existing));
        arr->Push(valueToInt64(*value));
        headers->Set(lowerName, TsValue(arr));
    }
}

TsArray* TsOutgoingMessage::GetHeaderNames() {
    if (!headers) return TsArray::Create();
    return (TsArray*)headers->GetKeys();
}

TsArray* TsOutgoingMessage::GetRawHeaderNames() {
    // For now, same as GetHeaderNames - would preserve original case in full impl
    return GetHeaderNames();
}

void TsOutgoingMessage::FlushHeaders() {
    // Mark headers as sent - actual sending is done by subclass
    headersSent = true;
}

// C API for OutgoingMessage
void ts_outgoing_message_set_header(void* msg, void* name, void* value) {
    TsOutgoingMessage* m = (TsOutgoingMessage*)msg;
    TsString* n = (TsString*)name;
    TsValue* v = (TsValue*)value;
    m->SetHeader(n, v);
}

void* ts_outgoing_message_get_header(void* msg, void* name) {
    TsOutgoingMessage* m = (TsOutgoingMessage*)msg;
    TsString* n = (TsString*)name;
    return m->GetHeader(n);
}

void* ts_outgoing_message_get_headers(void* msg) {
    TsOutgoingMessage* m = (TsOutgoingMessage*)msg;
    return m->GetHeaders();
}

bool ts_outgoing_message_has_header(void* msg, void* name) {
    TsOutgoingMessage* m = (TsOutgoingMessage*)msg;
    TsString* n = (TsString*)name;
    return m->HasHeader(n);
}

void ts_outgoing_message_remove_header(void* msg, void* name) {
    TsOutgoingMessage* m = (TsOutgoingMessage*)msg;
    TsString* n = (TsString*)name;
    m->RemoveHeader(n);
}

void* ts_outgoing_message_get_header_names(void* msg) {
    TsOutgoingMessage* m = (TsOutgoingMessage*)msg;
    return m->GetHeaderNames();
}

void ts_outgoing_message_flush_headers(void* msg) {
    TsOutgoingMessage* m = (TsOutgoingMessage*)msg;
    m->FlushHeaders();
}

// TsServerResponse
TsServerResponse::TsServerResponse(TsSocket* socket) : TsOutgoingMessage(), socket(socket) {
    this->magic = MAGIC;
}

TsServerResponse* TsServerResponse::Create(TsSocket* socket) {
    void* mem = ts_alloc(sizeof(TsServerResponse));
    return new (mem) TsServerResponse(socket);
}

void TsServerResponse::WriteHead(int status, TsObject* headers) {
    this->statusCode = status;
    this->headersSent = true;
    
    std::string head = "HTTP/1.1 " + std::to_string(status) + " OK\r\n";
    head += "Transfer-Encoding: chunked\r\n";
    head += "\r\n";

    socket->Write((void*)head.c_str(), head.length());
}

bool TsServerResponse::Write(void* data, size_t length) {
    if (closed) return false;
    if (!headersSent) WriteHead(200, nullptr);

    char chunk_header[32];
    int header_len = sprintf(chunk_header, "%x\r\n", (unsigned int)length);
    
    socket->Write((void*)chunk_header, header_len);
    socket->Write(data, length);
    socket->Write((void*)"\r\n", 2);

    return true;
}

void TsServerResponse::End() {
    End(TsValue());
}

void TsServerResponse::End(TsValue data) {
    if (closed) return;
    
    if (data.type != ValueType::UNDEFINED) {
        if (data.type == ValueType::STRING_PTR) {
            TsString* str = (TsString*)data.ptr_val;
            std::string s = str->ToUtf8();
            Write((void*)s.c_str(), s.length());
        } else if (data.type == ValueType::OBJECT_PTR) {
            TsBuffer* buf = (TsBuffer*)data.ptr_val;
            Write(buf->GetData(), buf->GetLength());
        }
    }

    if (!headersSent) WriteHead(200, nullptr);

    socket->Write((void*)"0\r\n\r\n", 5);
    
    closed = true;
    Emit("finish", 0, nullptr);
}

// llhttp callbacks
static int on_url(llhttp_t* parser, const char* at, size_t length) {
    HttpContext* ctx = (HttpContext*)parser->data;
    if (!ctx) return -1;
    
    ctx->currentRequest->url = TsString::Create(std::string(at, length).c_str());
    const char* method_name = llhttp_method_name((llhttp_method_t)parser->method);
    ctx->currentRequest->method = TsString::Create(method_name ? method_name : "GET");
    return 0;
}

static int on_header_field(llhttp_t* parser, const char* at, size_t length) {
    HttpContext* ctx = (HttpContext*)parser->data;
    if (!ctx) return -1;
    std::string field(at, length);
    for (auto& c : field) c = std::tolower(c);
    ctx->currentHeaderField = TsString::Create(field.c_str());
    return 0;
}

static int on_header_value(llhttp_t* parser, const char* at, size_t length) {
    HttpContext* ctx = (HttpContext*)parser->data;
    if (!ctx) return -1;
    if (!ctx->currentRequest || !ctx->currentRequest->headers) {
        return -1;
    }
    ctx->currentRequest->headers->Append(ctx->currentHeaderField, TsString::Create(std::string(at, length).c_str()));
    return 0;
}

static int on_headers_complete(llhttp_t* parser) {
    return 0;
}

static int on_message_complete(llhttp_t* parser) {
    HttpContext* ctx = (HttpContext*)parser->data;
    if (!ctx) return -1;
    
    TsValue* reqVal = ts_value_make_object(ctx->currentRequest);
    TsValue* resVal = ts_value_make_object(ctx->currentResponse);
    TsValue* args[] = { reqVal, resVal };
    
    ctx->server->Emit("request", 2, (void**)args);
    return 0;
}

static void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)ts_alloc(suggested_size);
    buf->len = (unsigned int)suggested_size;
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    HttpContext* ctx = (HttpContext*)stream->data;
    if (nread > 0) {
        llhttp_errno_t err = llhttp_execute(&ctx->parser, buf->base, nread);
        if (err != HPE_OK) {
            uv_close((uv_handle_t*)stream, nullptr);
        }
    } else if (nread < 0) {
        uv_close((uv_handle_t*)stream, nullptr);
    }
}

// TsHttpServer
TsHttpServer::TsHttpServer() : TsServer() {
    this->magic = MAGIC;
}

TsHttpServer* TsHttpServer::Create(TsValue* options, void* callback) {
    void* mem = ts_alloc(sizeof(TsHttpServer));
    TsHttpServer* server = new (mem) TsHttpServer();
    
    if (callback) {
        server->On("request", callback);
    }

    // Listen for "connection" event from base TsServer
    server->On("connection", ts_value_make_function((void*)[](void* ctx, int argc, TsValue** argv) -> TsValue* {
        TsHttpServer* self = (TsHttpServer*)ctx;
        TsSocket* socket = (TsSocket*)argv[0]->ptr_val;

        HttpContext* httpCtx = (HttpContext*)ts_alloc(sizeof(HttpContext));
        new (httpCtx) HttpContext(self, socket);
        
        llhttp_settings_init(&httpCtx->settings);
        httpCtx->settings.on_url = on_url;
        httpCtx->settings.on_header_field = on_header_field;
        httpCtx->settings.on_header_value = on_header_value;
        httpCtx->settings.on_headers_complete = on_headers_complete;
        httpCtx->settings.on_message_complete = on_message_complete;
        
        llhttp_init(&httpCtx->parser, HTTP_REQUEST, &httpCtx->settings);
        httpCtx->parser.data = httpCtx;
        
        httpCtx->currentRequest = TsIncomingMessage::Create();
        httpCtx->currentResponse = TsServerResponse::Create(socket);
        
        socket->On("data", ts_value_make_native_function((void*)[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            HttpContext* httpCtx = (HttpContext*)ctx;
            if (argc > 0 && argv[0]->type == ValueType::OBJECT_PTR) {
                TsBuffer* buf = (TsBuffer*)argv[0]->ptr_val;
                llhttp_execute(&httpCtx->parser, (const char*)buf->GetData(), buf->GetLength());
            }
            return nullptr;
        }, httpCtx));

        return nullptr;
    }, server));

    return server;
}

// TsClientRequest
static void client_on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)ts_alloc(suggested_size);
    buf->len = (unsigned int)suggested_size;
}

TsClientRequest::TsClientRequest(TsValue* optionsPtr, void* callback, bool is_https) : TsOutgoingMessage(), callback(callback), is_https(is_https) {
    this->magic = MAGIC;
    if (optionsPtr) {
        options = *optionsPtr;
    } else {
        options.type = ValueType::UNDEFINED;
    }

    // Parse options first to get host/port before checking agent
    if (options.type == ValueType::STRING_PTR) {
        std::string url = ((TsString*)options.ptr_val)->ToUtf8();
        std::string protocol = is_https ? "https://" : "http://";
        if (url.find(protocol) == 0) {
            url = url.substr(protocol.length());
            size_t slash = url.find('/');
            if (slash != std::string::npos) {
                host = url.substr(0, slash);
                path = url.substr(slash);
            } else {
                host = url;
                path = "/";
            }
            size_t colon = host.find(':');
            if (colon != std::string::npos) {
                port = std::stoi(host.substr(colon + 1));
                host = host.substr(0, colon);
            } else {
                port = is_https ? 443 : 80;
            }
        }
    } else if (options.type == ValueType::OBJECT_PTR) {
        TsMap* obj = (TsMap*)options.ptr_val;
        TsValue v_host = obj->Get(TsValue(TsString::Create("host")));
        if (v_host.type == ValueType::STRING_PTR) host = ((TsString*)v_host.ptr_val)->ToUtf8();
        
        TsValue v_hostname = obj->Get(TsValue(TsString::Create("hostname")));
        if (v_hostname.type == ValueType::STRING_PTR) host = ((TsString*)v_hostname.ptr_val)->ToUtf8();

        TsValue v_port = obj->Get(TsValue(TsString::Create("port")));
        if (v_port.type == ValueType::NUMBER_INT) port = (int)v_port.i_val;
        else if (v_port.type == ValueType::NUMBER_DBL) port = (int)v_port.d_val;
        else port = is_https ? 443 : 80;

        TsValue v_path = obj->Get(TsValue(TsString::Create("path")));
        if (v_path.type == ValueType::STRING_PTR) path = ((TsString*)v_path.ptr_val)->ToUtf8();

        TsValue v_method = obj->Get(TsValue(TsString::Create("method")));
        if (v_method.type == ValueType::STRING_PTR) method = ((TsString*)v_method.ptr_val)->ToUtf8();
        
        TsValue v_headers = obj->Get(TsValue(TsString::Create("headers")));
        if (v_headers.type == ValueType::OBJECT_PTR && v_headers.ptr_val) {
            headers = (TsMap*)v_headers.ptr_val;
        }

        // Check for agent option
        TsValue v_agent = obj->Get(TsValue(TsString::Create("agent")));
        if (v_agent.type == ValueType::OBJECT_PTR && v_agent.ptr_val) {
            agent = (TsHttpAgent*)v_agent.ptr_val;
        }
    }

    // Use global agent if no agent specified
    if (!agent) {
        agent = is_https ? (TsHttpAgent*)globalHttpsAgent : globalHttpAgent;
    }

    // Generate agent key for socket pooling
    agentKey = host + ":" + std::to_string(port);

    // Try to get a socket from the agent's pool first
    socket = nullptr;
    if (agent && agent->keepAlive) {
        socket = agent->GetFreeSocket(agentKey);
        if (socket) {
            socketFromAgent = true;
            connected = true;  // Reused socket is already connected
        }
    }

    // Create a new socket if we didn't get one from the agent
    if (!socket) {
        if (is_https) {
            socket = new (ts_alloc(sizeof(TsSecureSocket))) TsSecureSocket();
        } else {
            socket = new (ts_alloc(sizeof(TsSocket))) TsSocket();
        }
        
        // Register with agent
        if (agent) {
            agent->AddSocket(agentKey, socket);
        }
    }

    if (callback) {
        On("response", callback);
    }

    response = TsIncomingMessage::Create();

    llhttp_settings_init(&settings);
    settings.on_header_field = [](llhttp_t* p, const char* at, size_t len) -> int {
        TsClientRequest* req = (TsClientRequest*)p->data;
        std::string field(at, len);
        for (auto& c : field) c = std::tolower(c);
        req->currentHeaderField = TsString::Create(field.c_str());
        return 0;
    };
    settings.on_header_value = [](llhttp_t* p, const char* at, size_t len) -> int {
        TsClientRequest* req = (TsClientRequest*)p->data;
        if (req->currentHeaderField) {
            req->response->headers->Set(req->currentHeaderField, TsString::Create(std::string(at, len).c_str()));
        }
        return 0;
    };
    settings.on_headers_complete = [](llhttp_t* p) -> int {
        TsClientRequest* req = (TsClientRequest*)p->data;
        req->response->statusCode = p->status_code;  // Set the status code from the response
        TsValue* resVal = ts_value_make_object(req->response);
        req->Emit("response", 1, (void**)&resVal);
        return 0;
    };
    settings.on_body = [](llhttp_t* p, const char* at, size_t len) -> int {
        TsClientRequest* req = (TsClientRequest*)p->data;
        TsBuffer* buf = TsBuffer::Create(len);
        memcpy(buf->GetData(), at, len);
        TsValue* bufVal = ts_value_make_object(buf);
        req->response->Emit("data", 1, (void**)&bufVal);
        return 0;
    };
    settings.on_message_complete = [](llhttp_t* p) -> int {
        TsClientRequest* req = (TsClientRequest*)p->data;
        req->response->Emit("end", 0, nullptr);
        // Return socket to agent pool if keepAlive is enabled
        req->ReturnSocketToAgent();
        return 0;
    };

    llhttp_init(&parser, HTTP_RESPONSE, &settings);
    parser.data = this;

    Connect();
}

TsClientRequest* TsClientRequest::Create(TsValue* options, void* callback, bool is_https) {
    void* mem = ts_alloc(sizeof(TsClientRequest));
    return new (mem) TsClientRequest(options, callback, is_https);
}

static TsValue* client_on_data(void* context, int argc, TsValue** argv) {
    TsClientRequest* self = (TsClientRequest*)context;
    if (argc > 0 && argv[0]->type == ValueType::OBJECT_PTR) {
        TsBuffer* buf = (TsBuffer*)argv[0]->ptr_val;
        llhttp_execute(&self->parser, (const char*)buf->GetData(), buf->GetLength());
    }
    return nullptr;
}

static TsValue* client_on_connect(void* context, int argc, TsValue** argv) {
    TsClientRequest* self = (TsClientRequest*)context;
    self->connected = true;
    self->SendHeaders();
    return nullptr;
}

static TsValue* client_on_error(void* context, int argc, TsValue** argv) {
    TsClientRequest* self = (TsClientRequest*)context;
    self->Emit("error", argc, (void**)argv);
    return nullptr;
}

void TsClientRequest::Connect() {
    socket->On("data", ts_value_make_native_function((void*)client_on_data, this));
    socket->On("error", ts_value_make_native_function((void*)client_on_error, this));
    
    // If socket was reused from agent, it's already connected - just send headers
    if (socketFromAgent && connected) {
        SendHeaders();
        return;
    }
    
    socket->Connect(host.c_str(), port, nullptr);
    
    if (is_https) {
        TsSecureSocket* secureSocket = (TsSecureSocket*)socket;
        bool rejectUnauthorized = true;
        TsValue* ca = nullptr;

        if (options.type == ValueType::OBJECT_PTR) {
            TsMap* opts = (TsMap*)options.ptr_val;
            TsValue v_reject = opts->Get(TsValue(TsString::Create("rejectUnauthorized")));
            if (v_reject.type == ValueType::BOOLEAN) rejectUnauthorized = v_reject.b_val;
            
            TsValue v_ca = opts->Get(TsValue(TsString::Create("ca")));
            if (v_ca.type != ValueType::UNDEFINED) ca = &v_ca;
        }
        
        secureSocket->SetVerify(rejectUnauthorized, ca);

        socket->On("secureConnect", ts_value_make_native_function((void*)client_on_connect, this));
    } else {
        socket->On("connect", ts_value_make_native_function((void*)client_on_connect, this));
    }
}

void TsClientRequest::ReturnSocketToAgent() {
    if (!agent || !agent->keepAlive) return;
    if (!socket) return;
    
    // Return the socket to the agent's free pool for reuse
    agent->ReuseSocket(agentKey, socket);
    
    // Prevent double-return
    socket = nullptr;
}

void TsClientRequest::SendHeaders() {
    if (headersSent) return;
    headersSent = true;

    std::string req = method + " " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    
    if (headers) {
        TsArray* entries = (TsArray*)headers->GetEntries();
        if (entries) {
            for (int64_t i = 0; i < entries->Length(); ++i) {
                TsArray* entry = (TsArray*)entries->Get(i);
                if (!entry) continue;
                
                TsValue* k = (TsValue*)entry->Get(0);
                TsValue* v = (TsValue*)entry->Get(1);
                
                if (k && v) {
                    TsString* sKey = (TsString*)ts_string_from_value(k);
                    TsString* sVal = (TsString*)ts_string_from_value(v);
                    
                    if (sKey && sVal) {
                        req += sKey->ToUtf8();
                        req += ": ";
                        req += sVal->ToUtf8();
                        req += "\r\n";
                    }
                }
            }
        }
    }

    if (endCalled) {
        // Use keep-alive if agent has it enabled, otherwise close
        if (agent && agent->keepAlive) {
            req += "Connection: keep-alive\r\n";
        } else {
            req += "Connection: close\r\n";
        }
    } else {
        req += "Transfer-Encoding: chunked\r\n";
        if (agent && agent->keepAlive) {
            req += "Connection: keep-alive\r\n";
        }
    }
    
    req += "\r\n";

    if (connected) {
        socket->Write((void*)req.c_str(), req.length());
    } else {
        headersSent = false; 
    }
}

bool TsClientRequest::Write(void* data, size_t length) {
    if (!headersSent) SendHeaders();
    if (!connected) {
        // Buffer data if not connected? For now just return false or wait.
        // In a real implementation we'd queue the write.
        return false;
    }

    socket->Write(data, length);
    return true;
}

void TsClientRequest::End() {
    End(TsValue());
}

void TsClientRequest::End(TsValue data) {
    if (endCalled) return;
    endCalled = true;

    if (data.type != ValueType::UNDEFINED) {
        if (data.type == ValueType::STRING_PTR) {
            TsString* str = (TsString*)data.ptr_val;
            std::string s = str->ToUtf8();
            Write((void*)s.c_str(), s.length());
        } else if (data.type == ValueType::OBJECT_PTR) {
            TsBuffer* buf = (TsBuffer*)data.ptr_val;
            Write(buf->GetData(), buf->GetLength());
        }
    }
    
    if (!headersSent) SendHeaders();
    
    // If we are connected, we might want to shutdown the write side
    if (connected) {
        uv_shutdown_t* shutdown_req = (uv_shutdown_t*)ts_alloc(sizeof(uv_shutdown_t));
        uv_shutdown(shutdown_req, (uv_stream_t*)socket, [](uv_shutdown_t* req, int status) {
            // Done
        });
    }
}

// TsHttpsServer
TsHttpsServer::TsHttpsServer(TsValue* options) : TsHttpServer(), options(options) {}

TsHttpsServer* TsHttpsServer::Create(TsValue* options, void* callback) {
    void* mem = ts_alloc(sizeof(TsHttpsServer));
    TsHttpsServer* server = new (mem) TsHttpsServer(options);
    
    if (callback) {
        server->On("request", callback);
    }

    // Reuse the same "connection" logic as TsHttpServer
    server->On("connection", ts_value_make_function((void*)[](void* ctx, int argc, TsValue** argv) -> TsValue* {
        TsHttpsServer* self = (TsHttpsServer*)ctx;
        TsSocket* socket = (TsSocket*)argv[0]->ptr_val;

        HttpContext* httpCtx = (HttpContext*)ts_alloc(sizeof(HttpContext));
        new (httpCtx) HttpContext(self, socket);
        
        llhttp_settings_init(&httpCtx->settings);
        httpCtx->settings.on_url = on_url;
        httpCtx->settings.on_header_field = on_header_field;
        httpCtx->settings.on_header_value = on_header_value;
        httpCtx->settings.on_headers_complete = on_headers_complete;
        httpCtx->settings.on_message_complete = on_message_complete;
        
        llhttp_init(&httpCtx->parser, HTTP_REQUEST, &httpCtx->settings);
        httpCtx->parser.data = httpCtx;
        
        httpCtx->currentRequest = TsIncomingMessage::Create();
        httpCtx->currentResponse = TsServerResponse::Create(socket);
        
        socket->On("data", ts_value_make_native_function((void*)[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            HttpContext* httpCtx = (HttpContext*)ctx;
            if (argc > 0 && argv[0]->type == ValueType::OBJECT_PTR) {
                TsBuffer* buf = (TsBuffer*)argv[0]->ptr_val;
                llhttp_execute(&httpCtx->parser, (const char*)buf->GetData(), buf->GetLength());
            }
            return nullptr;
        }, httpCtx));

        return nullptr;
    }, server));

    return server;
}

void TsHttpsServer::HandleConnection(int status) {
    if (status < 0) return;

    uv_tcp_t* client = (uv_tcp_t*)ts_alloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), client);
    
    if (uv_accept((uv_stream_t*)handle, (uv_stream_t*)client) == 0) {
        void* mem = ts_alloc(sizeof(TsSecureSocket));
        TsSecureSocket* socket = new (mem) TsSecureSocket(client, true);
        
        if (options && options->type == ValueType::OBJECT_PTR) {
            TsMap* opts = (TsMap*)options->ptr_val;
            
            TsValue v_key = opts->Get(TsValue(TsString::Create("key")));
            TsValue v_cert = opts->Get(TsValue(TsString::Create("cert")));
            
            TsBuffer* keyBuf = nullptr;
            TsBuffer* certBuf = nullptr;

            if (v_key.type == ValueType::OBJECT_PTR) {
                TsObject* obj = (TsObject*)v_key.ptr_val;
                if (obj->magic == 0x42554646) { // TsBuffer magic
                    keyBuf = (TsBuffer*)obj;
                } else if (obj->magic == 0x53545247) { // TsString magic
                    TsString* s = (TsString*)obj;
                    keyBuf = TsBuffer::Create(s->Length());
                    memcpy(keyBuf->GetData(), s->ToUtf8(), s->Length());
                } else {
                    // Try dynamic_cast to TsBuffer
                    TsBuffer* b = dynamic_cast<TsBuffer*>(obj);
                    if (b) {
                        keyBuf = b;
                    }
                }
            } else if (v_key.type == ValueType::STRING_PTR) {
                TsString* s = (TsString*)v_key.ptr_val;
                keyBuf = TsBuffer::Create(s->Length());
                memcpy(keyBuf->GetData(), s->ToUtf8(), s->Length());
            }

            if (v_cert.type == ValueType::OBJECT_PTR) {
                TsObject* obj = (TsObject*)v_cert.ptr_val;
                if (obj->magic == 0x42554646) { // TsBuffer magic
                    certBuf = (TsBuffer*)obj;
                } else if (obj->magic == 0x53545247) { // TsString magic
                    TsString* s = (TsString*)obj;
                    certBuf = TsBuffer::Create(s->Length());
                    memcpy(certBuf->GetData(), s->ToUtf8(), s->Length());
                } else {
                    // Try dynamic_cast to TsBuffer
                    TsBuffer* b = dynamic_cast<TsBuffer*>(obj);
                    if (b) {
                        certBuf = b;
                    }
                }
            } else if (v_cert.type == ValueType::STRING_PTR) {
                TsString* s = (TsString*)v_cert.ptr_val;
                certBuf = TsBuffer::Create(s->Length());
                memcpy(certBuf->GetData(), s->ToUtf8(), s->Length());
            }
            
            if (keyBuf && certBuf) {
                socket->SetCertificate(certBuf, keyBuf);
            }
        }
        
        TsValue* arg0 = ts_value_make_object(socket);
        void* args[] = { arg0 };
        Emit("connection", 1, args);
    } else {
        uv_close((uv_handle_t*)client, nullptr);
    }
}

extern "C" {
void* ts_http_create_server(TsValue* options, void* callback) {
    return TsHttpServer::Create(options, callback);
}

void* ts_https_create_server(TsValue* options, void* callback) {
    return TsHttpsServer::Create(options, callback);
}

void ts_http_server_listen(void* server, void* port_val, void* callback) {
    TsValue* p = (TsValue*)port_val;
    int port = 0;
    if (p->type == ValueType::NUMBER_DBL) {
        port = (int)p->d_val;
    } else if (p->type == ValueType::NUMBER_INT) {
        port = (int)p->i_val;
    }
    ((TsHttpServer*)server)->Listen(port, callback);
}

void ts_http_server_response_write_head(void* res, int64_t status, TsValue* headers) {
    TsServerResponse* r = (TsServerResponse*)res;
    TsObject* h = nullptr;
    if (headers && headers->type == ValueType::OBJECT_PTR) {
        h = (TsObject*)headers->ptr_val;
    }
    r->WriteHead((int)status, h);
}

bool ts_http_server_response_write(void* res, void* data) {
    TsServerResponse* r = (TsServerResponse*)res;
    TsValue* val = (TsValue*)data;
    if (val->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)val->ptr_val;
        return r->Write((void*)str->ToUtf8(), str->Length());
    } else if (val->type == ValueType::OBJECT_PTR) {
        TsBuffer* buf = (TsBuffer*)val->ptr_val;
        return r->Write(buf->GetData(), buf->GetLength());
    }
    return false;
}

void ts_http_server_response_end(void* res, void* data) {
    TsServerResponse* r = (TsServerResponse*)res;
    if (data) {
        r->End(*(TsValue*)data);
    } else {
        r->End();
    }
}

void* ts_incoming_message_url(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)msg;
    return m->url;
}

void* ts_incoming_message_method(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)msg;
    return m->method;
}

void* ts_incoming_message_headers(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)msg;
    return m->headers;
}

void* ts_incoming_message_statusCode(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)msg;
    return ts_value_make_int(m->statusCode);
}

void* ts_incoming_message_statusMessage(void* ctx, void* msg) {
    TsIncomingMessage* m = (TsIncomingMessage*)msg;
    return m->statusMessage;
}

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

// http.METHODS - returns array of HTTP method strings
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
    return methods;
}

// http.STATUS_CODES - returns map of status codes to descriptions
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
            TsValue* key = ts_value_make_int(sc.code);
            TsValue* val = ts_value_make_string(TsString::Create(sc.message));
            statusCodes->Set(key, val);
        }
    }
    return statusCodes;
}

// http.maxHeaderSize - default is 16KB (16384 bytes) in Node.js
int64_t ts_http_get_max_header_size() {
    return 16384;
}

// http.validateHeaderName - throws if name is invalid
void ts_http_validate_header_name(void* name) {
    if (!name) {
        // Throw error - for now just return (stub behavior)
        return;
    }
    
    TsString* str = nullptr;
    TsValue* val = (TsValue*)name;
    if (val->type == ValueType::STRING_PTR) {
        str = (TsString*)val->ptr_val;
    } else if (val->type == ValueType::OBJECT_PTR) {
        str = dynamic_cast<TsString*>((TsObject*)val->ptr_val);
    } else {
        str = (TsString*)name;
    }
    if (!str) return;
    
    std::string s = str->ToUtf8();
    if (s.empty()) return; // Would throw in real Node.js
    
    // Valid header name characters: token chars per RFC 7230
    // For now, just check for basic validity
    for (char c : s) {
        if (c <= 32 || c >= 127 || c == ':') {
            // Invalid character - would throw in real Node.js
            return;
        }
    }
}

// http.validateHeaderValue - throws if value is invalid
void ts_http_validate_header_value(void* name, void* value) {
    if (!name || !value) return;
    
    TsString* valStr = nullptr;
    TsValue* val = (TsValue*)value;
    if (val->type == ValueType::STRING_PTR) {
        valStr = (TsString*)val->ptr_val;
    } else if (val->type == ValueType::OBJECT_PTR) {
        valStr = dynamic_cast<TsString*>((TsObject*)val->ptr_val);
    } else {
        valStr = (TsString*)value;
    }
    if (!valStr) return;
    
    std::string s = valStr->ToUtf8();
    
    // Check for invalid characters per RFC 7230
    for (char c : s) {
        if (c == '\0' || c == '\r' || c == '\n') {
            // Invalid character - would throw in real Node.js
            return;
        }
    }
}

} // extern "C"

// ===== HTTP Agent Implementation =====

// Global agent instances
TsHttpAgent* globalHttpAgent = nullptr;
TsHttpsAgent* globalHttpsAgent = nullptr;
static int64_t maxIdleHttpParsers = 1000;

// TsHttpAgent implementation
TsHttpAgent::TsHttpAgent(TsValue* options) : TsEventEmitter() {
    if (options && options->type == ValueType::OBJECT_PTR) {
        TsMap* opts = (TsMap*)options->ptr_val;
        
        TsValue v_keepAlive = opts->Get(TsValue(TsString::Create("keepAlive")));
        if (v_keepAlive.type == ValueType::BOOLEAN) keepAlive = v_keepAlive.b_val;
        
        TsValue v_keepAliveMsecs = opts->Get(TsValue(TsString::Create("keepAliveMsecs")));
        if (v_keepAliveMsecs.type == ValueType::NUMBER_INT) keepAliveMsecs = (int)v_keepAliveMsecs.i_val;
        else if (v_keepAliveMsecs.type == ValueType::NUMBER_DBL) keepAliveMsecs = (int)v_keepAliveMsecs.d_val;
        
        TsValue v_maxSockets = opts->Get(TsValue(TsString::Create("maxSockets")));
        if (v_maxSockets.type == ValueType::NUMBER_INT) maxSockets = (int)v_maxSockets.i_val;
        else if (v_maxSockets.type == ValueType::NUMBER_DBL) maxSockets = (int)v_maxSockets.d_val;
        
        TsValue v_maxTotalSockets = opts->Get(TsValue(TsString::Create("maxTotalSockets")));
        if (v_maxTotalSockets.type == ValueType::NUMBER_INT) maxTotalSockets = (int)v_maxTotalSockets.i_val;
        else if (v_maxTotalSockets.type == ValueType::NUMBER_DBL) maxTotalSockets = (int)v_maxTotalSockets.d_val;
        
        TsValue v_maxFreeSockets = opts->Get(TsValue(TsString::Create("maxFreeSockets")));
        if (v_maxFreeSockets.type == ValueType::NUMBER_INT) maxFreeSockets = (int)v_maxFreeSockets.i_val;
        else if (v_maxFreeSockets.type == ValueType::NUMBER_DBL) maxFreeSockets = (int)v_maxFreeSockets.d_val;
        
        TsValue v_timeout = opts->Get(TsValue(TsString::Create("timeout")));
        if (v_timeout.type == ValueType::NUMBER_INT) timeout = (int)v_timeout.i_val;
        else if (v_timeout.type == ValueType::NUMBER_DBL) timeout = (int)v_timeout.d_val;
        
        TsValue v_scheduling = opts->Get(TsValue(TsString::Create("scheduling")));
        if (v_scheduling.type == ValueType::STRING_PTR) {
            scheduling = ((TsString*)v_scheduling.ptr_val)->ToUtf8();
        }
    }
}

TsHttpAgent* TsHttpAgent::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsHttpAgent));
    return new (mem) TsHttpAgent(options);
}

std::string TsHttpAgent::GetName(const std::string& host, int port, const std::string& localAddress) {
    std::string key = host + ":" + std::to_string(port);
    if (!localAddress.empty()) {
        key += ":" + localAddress;
    }
    return key;
}

TsSocket* TsHttpAgent::GetFreeSocket(const std::string& key) {
    auto it = freeSockets.find(key);
    if (it != freeSockets.end() && !it->second.empty()) {
        TsSocket* socket = nullptr;
        if (scheduling == "lifo") {
            socket = it->second.back();
            it->second.pop_back();
        } else { // fifo
            socket = it->second.front();
            it->second.erase(it->second.begin());
        }
        // Move to active sockets
        sockets[key].push_back(socket);
        return socket;
    }
    return nullptr;
}

void TsHttpAgent::AddSocket(const std::string& key, TsSocket* socket) {
    sockets[key].push_back(socket);
}

void TsHttpAgent::RemoveSocket(const std::string& key, TsSocket* socket) {
    auto it = sockets.find(key);
    if (it != sockets.end()) {
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), socket), vec.end());
    }
    
    auto it2 = freeSockets.find(key);
    if (it2 != freeSockets.end()) {
        auto& vec = it2->second;
        vec.erase(std::remove(vec.begin(), vec.end(), socket), vec.end());
    }
}

void TsHttpAgent::ReuseSocket(const std::string& key, TsSocket* socket) {
    // Move from active to free
    RemoveSocket(key, socket);
    
    if (keepAlive && (int)freeSockets[key].size() < maxFreeSockets) {
        freeSockets[key].push_back(socket);
    } else {
        // Close the socket if we can't reuse it
        // socket->Close(); // Would need this method
    }
}

void TsHttpAgent::Destroy() {
    // Close all sockets
    for (auto& pair : sockets) {
        pair.second.clear();
    }
    for (auto& pair : freeSockets) {
        pair.second.clear();
    }
    sockets.clear();
    freeSockets.clear();
    requests.clear();
}

// TsHttpsAgent implementation
TsHttpsAgent::TsHttpsAgent(TsValue* options) : TsHttpAgent(options) {
    // HTTPS agent uses the same base functionality
}

TsHttpsAgent* TsHttpsAgent::Create(TsValue* options) {
    void* mem = ts_alloc(sizeof(TsHttpsAgent));
    return new (mem) TsHttpsAgent(options);
}

extern "C" {

// C API for agents
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

int64_t ts_http_get_max_idle_http_parsers() {
    return maxIdleHttpParsers;
}

void ts_http_set_max_idle_http_parsers(int64_t max) {
    maxIdleHttpParsers = max;
}

// ============================================================================
// TsCloseEvent implementation
// ============================================================================

TsCloseEvent* TsCloseEvent::Create(int64_t code, TsString* reason) {
    TsCloseEvent* event = new (ts_alloc(sizeof(TsCloseEvent))) TsCloseEvent();
    event->code = code;
    event->reason = reason ? reason : TsString::Create("");
    event->wasClean = (code == 1000);  // 1000 = normal closure
    return event;
}

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

// ============================================================================
// TsMessageEvent implementation
// ============================================================================

TsMessageEvent* TsMessageEvent::Create() {
    TsMessageEvent* event = new (ts_alloc(sizeof(TsMessageEvent))) TsMessageEvent();
    event->data = nullptr;
    event->origin = TsString::Create("");
    event->lastEventId = TsString::Create("");
    event->source = nullptr;
    event->ports = TsArray::Create();
    return event;
}

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

// ============================================================================
// TsWebSocket implementation (RFC 6455)
// ============================================================================

// Base64 encoding for WebSocket key generation
static std::string base64_encode(const uint8_t* data, size_t len) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((len + 2) / 3 * 4);
    
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= data[i + 2];
        
        result += chars[(n >> 18) & 0x3F];
        result += chars[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? chars[n & 0x3F] : '=';
    }
    return result;
}

// WebSocket GUID (RFC 6455)
static const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Helper to convert TsString to std::string
static std::string ts_to_std_string(TsString* str) {
    return str ? std::string(str->ToUtf8()) : std::string();
}

TsWebSocket::TsWebSocket(TsString* url, TsValue* protocols) {
    this->magic = MAGIC;
    this->url = url;
    this->binaryType = TsString::Create("blob");
    this->protocol = TsString::Create("");
    this->extensions = TsString::Create("");
    
    // Store protocols if provided
    if (protocols) {
        TsString* protoStr = (TsString*)ts_value_get_object(protocols);
        if (!protoStr) protoStr = (TsString*)(void*)protocols;
        if (protoStr && protoStr->magic == TsString::MAGIC) {
            requestedProtocols = ts_to_std_string(protoStr);
        }
    }
}

TsWebSocket* TsWebSocket::Create(TsString* url, TsValue* protocols) {
    void* mem = ts_alloc(sizeof(TsWebSocket));
    TsWebSocket* ws = new (mem) TsWebSocket(url, protocols);
    
    // Parse URL and initiate connection
    // URL format: ws://host:port/path or wss://host:port/path
    std::string urlStr = ts_to_std_string(url);
    bool isSecure = urlStr.substr(0, 4) == "wss:";
    
    // Parse host, port, path from URL
    size_t protoEnd = urlStr.find("://");
    if (protoEnd == std::string::npos) {
        ws->HandleError(TsString::Create("Invalid WebSocket URL"));
        return ws;
    }
    
    std::string hostPortPath = urlStr.substr(protoEnd + 3);
    size_t pathStart = hostPortPath.find('/');
    std::string hostPort = (pathStart != std::string::npos) 
        ? hostPortPath.substr(0, pathStart) 
        : hostPortPath;
    std::string path = (pathStart != std::string::npos) 
        ? hostPortPath.substr(pathStart) 
        : "/";
    
    size_t colonPos = hostPort.find(':');
    std::string host = (colonPos != std::string::npos) 
        ? hostPort.substr(0, colonPos) 
        : hostPort;
    int port = (colonPos != std::string::npos) 
        ? std::stoi(hostPort.substr(colonPos + 1)) 
        : (isSecure ? 443 : 80);
    
    // Create socket and connect
    ws->socket = new (ts_alloc(sizeof(TsSocket))) TsSocket();
    if (!ws->socket) {
        ws->HandleError(TsString::Create("Failed to create socket"));
        return ws;
    }
    
    // Generate Sec-WebSocket-Key (16 random bytes base64 encoded)
    ws->secWebSocketKey = ws->GenerateSecWebSocketKey();
    
    // Store connection info for handshake
    struct WsConnectData {
        TsWebSocket* ws;
        std::string host;
        std::string path;
        int port;
        bool isSecure;
    };
    
    WsConnectData* data = new WsConnectData{ws, host, path, port, isSecure};
    
    // Connect to server
    ws->socket->On("connect", (void*)[](void* userData) {
        WsConnectData* d = (WsConnectData*)userData;
        d->ws->PerformHandshake();
        delete d;
    });
    
    ws->socket->On("data", (void*)[](void* wsPtr, void* dataPtr) {
        TsWebSocket* self = (TsWebSocket*)wsPtr;
        TsBuffer* buf = (TsBuffer*)dataPtr;
        if (buf) {
            self->HandleData((const uint8_t*)buf->GetData(), buf->GetLength());
        }
    });
    
    ws->socket->On("close", (void*)[](void* wsPtr) {
        TsWebSocket* self = (TsWebSocket*)wsPtr;
        self->HandleClose();
    });
    
    ws->socket->On("error", (void*)[](void* wsPtr, void* errPtr) {
        TsWebSocket* self = (TsWebSocket*)wsPtr;
        TsString* errStr = (TsString*)errPtr;
        self->HandleError(errStr ? errStr : TsString::Create("Socket error"));
    });
    
    // TODO: Actually connect the socket
    // ws->socket->Connect(host, port);
    
    return ws;
}

TsString* TsWebSocket::GenerateSecWebSocketKey() {
    // Generate 16 random bytes
    uint8_t bytes[16];
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 255);
    
    for (int i = 0; i < 16; i++) {
        bytes[i] = (uint8_t)distrib(gen);
    }
    
    // Base64 encode
    std::string encoded = base64_encode(bytes, 16);
    return TsString::Create(encoded.c_str());
}

TsString* TsWebSocket::ComputeSecWebSocketAccept(TsString* key) {
    // Compute SHA1(key + GUID), then base64 encode
    std::string input = ts_to_std_string(key) + WS_GUID;
    
    uint8_t hash[SHA_DIGEST_LENGTH];
    SHA1((const uint8_t*)input.c_str(), input.length(), hash);
    
    std::string encoded = base64_encode(hash, SHA_DIGEST_LENGTH);
    return TsString::Create(encoded.c_str());
}

void TsWebSocket::PerformHandshake() {
    if (!socket || !url) return;
    
    // Parse URL for path and host
    std::string urlStr = ts_to_std_string(url);
    size_t protoEnd = urlStr.find("://");
    std::string hostPortPath = urlStr.substr(protoEnd + 3);
    size_t pathStart = hostPortPath.find('/');
    std::string hostPort = (pathStart != std::string::npos) 
        ? hostPortPath.substr(0, pathStart) 
        : hostPortPath;
    std::string path = (pathStart != std::string::npos) 
        ? hostPortPath.substr(pathStart) 
        : "/";
    
    // Build HTTP upgrade request
    std::string request = "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + hostPort + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + ts_to_std_string(secWebSocketKey) + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    
    if (!requestedProtocols.empty()) {
        request += "Sec-WebSocket-Protocol: " + requestedProtocols + "\r\n";
    }
    
    request += "\r\n";
    
    // Send the request
    // socket->Write(request.data(), request.length());
}

void TsWebSocket::HandleData(const uint8_t* data, size_t length) {
    // Append to receive buffer
    receiveBuffer.insert(receiveBuffer.end(), data, data + length);
    
    if (!handshakeComplete) {
        // Look for end of HTTP response headers
        std::string response((char*)receiveBuffer.data(), receiveBuffer.size());
        size_t headerEnd = response.find("\r\n\r\n");
        
        if (headerEnd != std::string::npos) {
            // Parse response headers
            std::string headers = response.substr(0, headerEnd);
            
            // Check for 101 Switching Protocols
            if (headers.find("HTTP/1.1 101") != std::string::npos) {
                // Verify Sec-WebSocket-Accept
                size_t acceptPos = headers.find("Sec-WebSocket-Accept: ");
                if (acceptPos != std::string::npos) {
                    size_t acceptEnd = headers.find("\r\n", acceptPos);
                    std::string accept = headers.substr(acceptPos + 22, acceptEnd - acceptPos - 22);
                    
                    TsString* expectedAccept = ComputeSecWebSocketAccept(secWebSocketKey);
                    if (accept == ts_to_std_string(expectedAccept)) {
                        // Handshake successful!
                        handshakeComplete = true;
                        readyState = WS_OPEN;
                        
                        // Check for protocol
                        size_t protoPos = headers.find("Sec-WebSocket-Protocol: ");
                        if (protoPos != std::string::npos) {
                            size_t protoEnd = headers.find("\r\n", protoPos);
                            std::string proto = headers.substr(protoPos + 24, protoEnd - protoPos - 24);
                            protocol = TsString::Create(proto.c_str());
                        }
                        
                        // Remove processed header from buffer
                        receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + headerEnd + 4);
                        
                        // Emit open event
                        Emit("open", 0, nullptr);
                        if (onopen) {
                            ts_function_call((TsValue*)onopen, 0, nullptr);
                        }
                    } else {
                        HandleError(TsString::Create("Invalid Sec-WebSocket-Accept"));
                    }
                }
            } else {
                HandleError(TsString::Create("WebSocket handshake failed"));
            }
        }
        return;
    }
    
    // Process WebSocket frames
    size_t offset = 0;
    while (offset < receiveBuffer.size()) {
        size_t consumed = 0;
        if (!ParseFrame(receiveBuffer.data() + offset, receiveBuffer.size() - offset, consumed)) {
            break;  // Need more data
        }
        offset += consumed;
    }
    
    // Remove processed frames
    if (offset > 0) {
        receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + offset);
    }
}

bool TsWebSocket::ParseFrame(const uint8_t* data, size_t length, size_t& bytesConsumed) {
    if (length < 2) return false;
    
    bool fin = (data[0] & 0x80) != 0;
    uint8_t opcode = data[0] & 0x0F;
    bool masked = (data[1] & 0x80) != 0;
    uint64_t payloadLen = data[1] & 0x7F;
    
    size_t headerLen = 2;
    
    if (payloadLen == 126) {
        if (length < 4) return false;
        payloadLen = ((uint64_t)data[2] << 8) | data[3];
        headerLen = 4;
    } else if (payloadLen == 127) {
        if (length < 10) return false;
        payloadLen = 0;
        for (int i = 0; i < 8; i++) {
            payloadLen = (payloadLen << 8) | data[2 + i];
        }
        headerLen = 10;
    }
    
    if (masked) {
        headerLen += 4;  // Masking key
    }
    
    if (length < headerLen + payloadLen) return false;
    
    // Extract payload
    std::vector<uint8_t> payload(payloadLen);
    const uint8_t* payloadData = data + headerLen;
    
    if (masked) {
        const uint8_t* maskKey = data + headerLen - 4;
        for (size_t i = 0; i < payloadLen; i++) {
            payload[i] = payloadData[i] ^ maskKey[i % 4];
        }
    } else {
        memcpy(payload.data(), payloadData, payloadLen);
    }
    
    bytesConsumed = headerLen + payloadLen;
    
    // Handle frame by opcode
    switch (opcode) {
        case WS_OPCODE_TEXT:
        case WS_OPCODE_BINARY: {
            // Create MessageEvent
            TsMessageEvent* event = TsMessageEvent::Create();
            if (opcode == WS_OPCODE_TEXT) {
                event->data = TsString::Create(std::string((char*)payload.data(), payload.size()).c_str());
            } else {
                TsBuffer* buf = TsBuffer::Create(payload.size());
                memcpy(buf->GetData(), payload.data(), payload.size());
                event->data = buf;
            }
            event->origin = url;
            
            // Emit message event
            void* argv[1] = { event };
            Emit("message", 1, argv);
            if (onmessage) {
                TsValue* args[1] = { (TsValue*)event };
                ts_function_call((TsValue*)onmessage, 1, args);
            }
            break;
        }
        
        case WS_OPCODE_CLOSE: {
            int64_t code = 1000;
            TsString* reason = TsString::Create("");
            
            if (payloadLen >= 2) {
                code = ((uint16_t)payload[0] << 8) | payload[1];
                if (payloadLen > 2) {
                    reason = TsString::Create(std::string((char*)payload.data() + 2, payloadLen - 2).c_str());
                }
            }
            
            // Send close frame back if we haven't already
            if (readyState != WS_CLOSED && readyState != WS_CLOSING) {
                Close(code, reason);
            }
            
            readyState = WS_CLOSED;
            
            // Create CloseEvent
            TsCloseEvent* closeEvent = TsCloseEvent::Create(code, reason);
            
            void* closeArgv[1] = { closeEvent };
            Emit("close", 1, closeArgv);
            if (onclose) {
                TsValue* closeArgs[1] = { (TsValue*)closeEvent };
                ts_function_call((TsValue*)onclose, 1, closeArgs);
            }
            break;
        }
        
        case WS_OPCODE_PING:
            Pong(nullptr);  // Auto-respond with pong
            break;
            
        case WS_OPCODE_PONG:
            // Pong received, can be ignored
            break;
    }
    
    return true;
}

void TsWebSocket::SendFrame(WebSocketOpcode opcode, const uint8_t* data, size_t length) {
    if (!socket || readyState != WS_OPEN) return;
    
    std::vector<uint8_t> frame;
    
    // First byte: FIN + opcode
    frame.push_back(0x80 | opcode);
    
    // Second byte: mask bit + payload length
    uint8_t maskBit = isMasking ? 0x80 : 0x00;
    
    if (length < 126) {
        frame.push_back(maskBit | (uint8_t)length);
    } else if (length < 65536) {
        frame.push_back(maskBit | 126);
        frame.push_back((length >> 8) & 0xFF);
        frame.push_back(length & 0xFF);
    } else {
        frame.push_back(maskBit | 127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((length >> (i * 8)) & 0xFF);
        }
    }
    
    // Masking key (client must mask)
    if (isMasking) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, 255);
        
        uint8_t mask[4];
        for (int i = 0; i < 4; i++) {
            mask[i] = (uint8_t)distrib(gen);
            frame.push_back(mask[i]);
        }
        
        // Masked payload
        for (size_t i = 0; i < length; i++) {
            frame.push_back(data[i] ^ mask[i % 4]);
        }
    } else {
        // Unmasked payload
        frame.insert(frame.end(), data, data + length);
    }
    
    bufferedAmount += frame.size();
    // socket->Write(frame.data(), frame.size());
    bufferedAmount -= frame.size();
}

void TsWebSocket::Send(TsValue* data) {
    if (readyState != WS_OPEN) return;
    
    TsString* str = (TsString*)ts_value_get_object(data);
    if (!str) str = (TsString*)(void*)data;  // Fallback if not boxed
    // Check if it's actually a string by magic number
    if (str && str->magic == TsString::MAGIC) {
        std::string s = ts_to_std_string(str);
        SendFrame(WS_OPCODE_TEXT, (const uint8_t*)s.data(), s.length());
    } else {
        // Try as Buffer
        TsBuffer* buf = (TsBuffer*)ts_value_get_object(data);
        if (!buf) buf = (TsBuffer*)(void*)data;
        if (buf && buf->magic == TsBuffer::MAGIC) {
            SendFrame(WS_OPCODE_BINARY, (const uint8_t*)buf->GetData(), buf->GetLength());
        }
    }
}

void TsWebSocket::Close(int64_t code, TsString* reason) {
    if (readyState == WS_CLOSED || readyState == WS_CLOSING) return;
    
    readyState = WS_CLOSING;
    
    std::vector<uint8_t> payload;
    payload.push_back((code >> 8) & 0xFF);
    payload.push_back(code & 0xFF);
    
    if (reason) {
        std::string r = ts_to_std_string(reason);
        payload.insert(payload.end(), r.begin(), r.end());
    }
    
    SendFrame(WS_OPCODE_CLOSE, payload.data(), payload.size());
}

void TsWebSocket::Ping(TsValue* data) {
    if (readyState != WS_OPEN) return;
    
    if (data) {
        TsString* str = (TsString*)ts_value_get_object(data);
        if (!str) str = (TsString*)(void*)data;  // Fallback if not boxed
        if (str) {
            std::string s = ts_to_std_string(str);
            SendFrame(WS_OPCODE_PING, (const uint8_t*)s.data(), s.length());
            return;
        }
    }
    SendFrame(WS_OPCODE_PING, nullptr, 0);
}

void TsWebSocket::Pong(TsValue* data) {
    if (readyState != WS_OPEN) return;
    
    if (data) {
        TsString* str = (TsString*)ts_value_get_object(data);
        if (!str) str = (TsString*)(void*)data;  // Fallback if not boxed
        if (str) {
            std::string s = ts_to_std_string(str);
            SendFrame(WS_OPCODE_PONG, (const uint8_t*)s.data(), s.length());
            return;
        }
    }
    SendFrame(WS_OPCODE_PONG, nullptr, 0);
}

void TsWebSocket::HandleClose() {
    if (readyState == WS_CLOSED) return;
    
    readyState = WS_CLOSED;
    
    TsCloseEvent* event = TsCloseEvent::Create(1006, TsString::Create("Connection closed abnormally"));
    event->wasClean = false;
    
    void* argv[1] = { event };
    Emit("close", 1, argv);
    if (onclose) {
        TsValue* args[1] = { (TsValue*)event };
        ts_function_call((TsValue*)onclose, 1, args);
    }
}

void TsWebSocket::HandleError(TsString* error) {
    readyState = WS_CLOSED;
    
    void* argv[1] = { error };
    Emit("error", 1, argv);
    if (onerror) {
        TsValue* args[1] = { (TsValue*)error };
        ts_function_call((TsValue*)onerror, 1, args);
    }
}

// ============================================================================
// WebSocket C API
// ============================================================================

void* ts_websocket_create(void* url, void* protocols) {
    TsString* urlStr = (TsString*)ts_value_get_object((TsValue*)url);
    if (!urlStr) urlStr = (TsString*)url;
    
    TsValue* protoVal = (TsValue*)protocols;
    return TsWebSocket::Create(urlStr, protoVal);
}

void ts_websocket_send(void* ws, void* data) {
    if (!ws) return;
    TsWebSocket* socket = (TsWebSocket*)ws;
    socket->Send((TsValue*)data);
}

void ts_websocket_close(void* ws, int64_t code, void* reason) {
    if (!ws) return;
    TsWebSocket* socket = (TsWebSocket*)ws;
    TsString* reasonStr = reason ? (TsString*)ts_value_get_object((TsValue*)reason) : nullptr;
    if (!reasonStr && reason) reasonStr = (TsString*)reason;
    socket->Close(code, reasonStr);
}

void ts_websocket_ping(void* ws, void* data) {
    if (!ws) return;
    TsWebSocket* socket = (TsWebSocket*)ws;
    socket->Ping((TsValue*)data);
}

void ts_websocket_pong(void* ws, void* data) {
    if (!ws) return;
    TsWebSocket* socket = (TsWebSocket*)ws;
    socket->Pong((TsValue*)data);
}

int64_t ts_websocket_get_ready_state(void* ws) {
    if (!ws) return WS_CLOSED;
    TsWebSocket* socket = (TsWebSocket*)ws;
    return socket->readyState;
}

void* ts_websocket_get_url(void* ws) {
    if (!ws) return TsString::Create("");
    TsWebSocket* socket = (TsWebSocket*)ws;
    return socket->url;
}

void* ts_websocket_get_protocol(void* ws) {
    if (!ws) return TsString::Create("");
    TsWebSocket* socket = (TsWebSocket*)ws;
    return socket->protocol;
}

void* ts_websocket_get_extensions(void* ws) {
    if (!ws) return TsString::Create("");
    TsWebSocket* socket = (TsWebSocket*)ws;
    return socket->extensions;
}

int64_t ts_websocket_get_buffered_amount(void* ws) {
    if (!ws) return 0;
    TsWebSocket* socket = (TsWebSocket*)ws;
    return socket->bufferedAmount;
}

void* ts_websocket_get_binary_type(void* ws) {
    if (!ws) return TsString::Create("blob");
    TsWebSocket* socket = (TsWebSocket*)ws;
    return socket->binaryType;
}

void ts_websocket_set_binary_type(void* ws, void* type) {
    if (!ws) return;
    TsWebSocket* socket = (TsWebSocket*)ws;
    // Try to unbox as a string first
    TsString* typeStr = (TsString*)ts_value_get_string((TsValue*)type);
    if (!typeStr) typeStr = (TsString*)type;  // Fallback if not boxed
    if (typeStr) socket->binaryType = typeStr;
}

void ts_websocket_set_onopen(void* ws, void* callback) {
    if (!ws) return;
    TsWebSocket* socket = (TsWebSocket*)ws;
    socket->onopen = callback;
}

void ts_websocket_set_onmessage(void* ws, void* callback) {
    if (!ws) return;
    TsWebSocket* socket = (TsWebSocket*)ws;
    socket->onmessage = callback;
}

void ts_websocket_set_onclose(void* ws, void* callback) {
    if (!ws) return;
    TsWebSocket* socket = (TsWebSocket*)ws;
    socket->onclose = callback;
}

void ts_websocket_set_onerror(void* ws, void* callback) {
    if (!ws) return;
    TsWebSocket* socket = (TsWebSocket*)ws;
    socket->onerror = callback;
}

void* ts_websocket_get_onopen(void* ws) {
    if (!ws) return nullptr;
    TsWebSocket* socket = (TsWebSocket*)ws;
    return socket->onopen;
}

void* ts_websocket_get_onmessage(void* ws) {
    if (!ws) return nullptr;
    TsWebSocket* socket = (TsWebSocket*)ws;
    return socket->onmessage;
}

void* ts_websocket_get_onclose(void* ws) {
    if (!ws) return nullptr;
    TsWebSocket* socket = (TsWebSocket*)ws;
    return socket->onclose;
}

void* ts_websocket_get_onerror(void* ws) {
    if (!ws) return nullptr;
    TsWebSocket* socket = (TsWebSocket*)ws;
    return socket->onerror;
}

int64_t ts_websocket_connecting() { return WS_CONNECTING; }
int64_t ts_websocket_open() { return WS_OPEN; }
int64_t ts_websocket_closing() { return WS_CLOSING; }
int64_t ts_websocket_closed() { return WS_CLOSED; }

}

