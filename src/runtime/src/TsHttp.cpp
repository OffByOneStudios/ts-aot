#include "TsHttp.h"
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
    uv_stream_t* client;
    llhttp_t parser;
    llhttp_settings_t settings;
    TsIncomingMessage* currentRequest;
    TsServerResponse* currentResponse;
    TsString* currentHeaderField;

    HttpContext(TsHttpServer* s, uv_stream_t* c) 
        : server(s), client(c), currentRequest(nullptr), currentResponse(nullptr), currentHeaderField(nullptr) {
        parser.data = this;
    }
};

// TsIncomingMessage
TsIncomingMessage::TsIncomingMessage() : TsEventEmitter(), TsReadable(), method(nullptr), url(nullptr) {
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
TsServerResponse::TsServerResponse(uv_stream_t* client) : TsEventEmitter(), TsWritable(), client(client) {}

TsServerResponse* TsServerResponse::Create(uv_stream_t* client) {
    void* mem = ts_alloc(sizeof(TsServerResponse));
    return new (mem) TsServerResponse(client);
}

void TsServerResponse::WriteHead(int status, TsObject* headers) {
    this->status = status;
    this->headersSent = true;
    
    std::string head = "HTTP/1.1 " + std::to_string(status) + " OK\r\n";
    head += "Transfer-Encoding: chunked\r\n";
    head += "\r\n";

    uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    
    // We need to copy the string because head is on stack
    char* persistent_head = (char*)ts_alloc(head.length());
    memcpy(persistent_head, head.c_str(), head.length());
    uv_buf_t uv_buf = uv_buf_init(persistent_head, (unsigned int)head.length());

    uv_write(req, client, &uv_buf, 1, [](uv_write_t* req, int status) {
        // GC handles cleanup
    });
}

bool TsServerResponse::Write(void* data, size_t length) {
    if (closed) return false;
    if (!headersSent) WriteHead(200, nullptr);

    char chunk_header[32];
    int header_len = sprintf(chunk_header, "%x\r\n", (unsigned int)length);
    
    uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    
    uv_buf_t bufs[3];
    
    char* h = (char*)ts_alloc(header_len);
    memcpy(h, chunk_header, header_len);
    bufs[0] = uv_buf_init(h, (unsigned int)header_len);
    
    bufs[1] = uv_buf_init((char*)data, (unsigned int)length);
    bufs[2] = uv_buf_init((char*)"\r\n", 2);

    uv_write(req, client, bufs, 3, [](uv_write_t* req, int status) {
    });

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
            Write((void*)str->ToUtf8(), str->Length());
        } else if (data.type == ValueType::OBJECT_PTR) {
            TsBuffer* buf = (TsBuffer*)data.ptr_val;
            Write(buf->GetData(), buf->GetLength());
        }
    }

    if (!headersSent) WriteHead(200, nullptr);

    const char* footer = "0\r\n\r\n";
    uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    uv_buf_t uv_buf = uv_buf_init((char*)footer, 5);
    
    uv_write(req, client, &uv_buf, 1, [](uv_write_t* req, int status) {
        uv_close((uv_handle_t*)req->handle, nullptr);
    });
    
    closed = true;
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
TsHttpServer::TsHttpServer() : TsServer() {}

TsHttpServer* TsHttpServer::Create(void* callback) {
    void* mem = ts_alloc(sizeof(TsHttpServer));
    TsHttpServer* server = new (mem) TsHttpServer();
    
    if (callback) {
        server->On("request", callback);
    }

    // Listen for "connection" event from base TsServer
    server->On("connection", ts_value_make_function((void*)[](void* ctx, TsValue* arg0) {
        TsHttpServer* self = (TsHttpServer*)ctx;
        TsSocket* socket = (TsSocket*)arg0->ptr_val;
        uv_stream_t* client = socket->GetStream();

        HttpContext* httpCtx = (HttpContext*)ts_alloc(sizeof(HttpContext));
        new (httpCtx) HttpContext(self, client);
        
        llhttp_settings_init(&httpCtx->settings);
        httpCtx->settings.on_url = on_url;
        httpCtx->settings.on_header_field = on_header_field;
        httpCtx->settings.on_header_value = on_header_value;
        httpCtx->settings.on_headers_complete = on_headers_complete;
        httpCtx->settings.on_message_complete = on_message_complete;
        
        llhttp_init(&httpCtx->parser, HTTP_REQUEST, &httpCtx->settings);
        httpCtx->parser.data = httpCtx;
        
        httpCtx->currentRequest = TsIncomingMessage::Create();
        httpCtx->currentResponse = TsServerResponse::Create(client);
        
        client->data = httpCtx;
        uv_read_start(client, on_alloc, on_read);

        return (TsValue*)nullptr;
    }, server));

    return server;
}

// TsClientRequest
static void client_on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)ts_alloc(suggested_size);
    buf->len = (unsigned int)suggested_size;
}

TsClientRequest::TsClientRequest(TsValue* optionsPtr, void* callback) : TsEventEmitter(), TsWritable(), callback(callback) {
    TsValue options;
    if (optionsPtr) {
        options = *optionsPtr;
    } else {
        options.type = ValueType::UNDEFINED;
    }

    socket = (uv_tcp_t*)ts_alloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), socket);
    socket->data = this;

    if (options.type == ValueType::STRING_PTR) {
        std::string url = ((TsString*)options.ptr_val)->ToUtf8();
        if (url.find("http://") == 0) {
            url = url.substr(7);
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

TsClientRequest* TsClientRequest::Create(TsValue* options, void* callback) {
    void* mem = ts_alloc(sizeof(TsClientRequest));
    return new (mem) TsClientRequest(options, callback);
}

void TsClientRequest::Connect() {
    struct sockaddr_in dest;
    if (host == "localhost") {
        uv_ip4_addr("127.0.0.1", port, &dest);
    } else {
        uv_ip4_addr(host.c_str(), port, &dest);
    }

    uv_connect_t* connect_req = (uv_connect_t*)ts_alloc(sizeof(uv_connect_t));
    connect_req->data = this;
    int r = uv_tcp_connect(connect_req, socket, (const struct sockaddr*)&dest, [](uv_connect_t* req, int status) {
        TsClientRequest* self = (TsClientRequest*)req->data;
        if (status == 0) {
            self->connected = true;
            self->SendHeaders();
            uv_read_start((uv_stream_t*)self->socket, client_on_alloc, [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
                TsClientRequest* req = (TsClientRequest*)stream->data;
                if (nread > 0) {
                    llhttp_execute(&req->parser, buf->base, nread);
                } else if (nread < 0) {
                    uv_close((uv_handle_t*)stream, nullptr);
                }
            });
        } else {
            // ts_error_create returns an already-boxed TsValue*
            TsValue* errVal = (TsValue*)ts_error_create(TsString::Create(uv_strerror(status)));
            TsValue* args[1] = { errVal };
            self->Emit("error", 1, (void**)args);
        }
    });

    if (r != 0) {
        // ts_error_create returns an already-boxed TsValue*
        TsValue* errVal = (TsValue*)ts_error_create(TsString::Create(uv_strerror(r)));
        TsValue* args[1] = { errVal };
        Emit("error", 1, (void**)args);
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
        uv_write_t* write_req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
        write_req->data = this;
        char* persistent_req = (char*)ts_alloc(req.length());
        memcpy(persistent_req, req.c_str(), req.length());
        uv_buf_t uv_buf = uv_buf_init(persistent_req, (unsigned int)req.length());

        uv_write(write_req, (uv_stream_t*)socket, &uv_buf, 1, [](uv_write_t* req, int status) {
        });
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

    uv_write_t* write_req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    char* persistent_data = (char*)ts_alloc(length);
    memcpy(persistent_data, data, length);
    uv_buf_t uv_buf = uv_buf_init(persistent_data, (unsigned int)length);
    uv_write(write_req, (uv_stream_t*)socket, &uv_buf, 1, [](uv_write_t* req, int status) {
    });
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

extern "C" {
void* ts_http_create_server(void* callback) {
    return TsHttpServer::Create(callback);
}

void ts_http_server_listen(void* server, int64_t port, void* callback) {
    ((TsHttpServer*)server)->Listen((int)port, callback);
}

void ts_http_server_response_write_head(void* res, int64_t status, TsValue* headers) {
    TsServerResponse* r = (TsServerResponse*)res;
    TsObject* h = nullptr;
    if (headers && headers->type == ValueType::OBJECT_PTR) {
        h = (TsObject*)headers->ptr_val;
    }
    r->WriteHead((int)status, h);
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

void* ts_http_request(TsValue* options, void* callback) {
    TsClientRequest* req = TsClientRequest::Create(options, callback);
    // Return TsEventEmitter* - the compiler knows the full type hierarchy and will
    // correctly adjust the pointer for virtual inheritance
    return static_cast<TsEventEmitter*>(req);
}

void* ts_http_get(TsValue* options, void* callback) {
    TsClientRequest* req = TsClientRequest::Create(options, callback);
    req->End();
    // Return TsEventEmitter* - the compiler knows the full type hierarchy and will
    // correctly adjust the pointer for virtual inheritance
    return static_cast<TsEventEmitter*>(req);
}
}

