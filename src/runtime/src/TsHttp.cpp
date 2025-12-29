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

// TsServerResponse
TsServerResponse::TsServerResponse(TsSocket* socket) : TsEventEmitter(), TsWritable(), socket(socket) {
    this->magic = MAGIC;
}

TsServerResponse* TsServerResponse::Create(TsSocket* socket) {
    void* mem = ts_alloc(sizeof(TsServerResponse));
    return new (mem) TsServerResponse(socket);
}

void TsServerResponse::WriteHead(int status, TsObject* headers) {
    this->status = status;
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

TsClientRequest::TsClientRequest(TsValue* optionsPtr, void* callback, bool is_https) : TsEventEmitter(), TsWritable(), callback(callback), is_https(is_https) {
    if (optionsPtr) {
        options = *optionsPtr;
    } else {
        options.type = ValueType::UNDEFINED;
    }

    if (is_https) {
        socket = new (ts_alloc(sizeof(TsSecureSocket))) TsSecureSocket();
        port = 443;
    } else {
        socket = new (ts_alloc(sizeof(TsSocket))) TsSocket();
        port = 80;
    }

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

        TsValue v_path = obj->Get(TsValue(TsString::Create("path")));
        if (v_path.type == ValueType::STRING_PTR) path = ((TsString*)v_path.ptr_val)->ToUtf8();

        TsValue v_method = obj->Get(TsValue(TsString::Create("method")));
        if (v_method.type == ValueType::STRING_PTR) method = ((TsString*)v_method.ptr_val)->ToUtf8();
        
        TsValue v_headers = obj->Get(TsValue(TsString::Create("headers")));
        if (v_headers.type == ValueType::OBJECT_PTR && v_headers.ptr_val) {
            headers = (TsMap*)v_headers.ptr_val;
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
        req += "Connection: close\r\n";
    } else {
        req += "Transfer-Encoding: chunked\r\n";
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
}

