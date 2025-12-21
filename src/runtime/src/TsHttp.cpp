#include "TsHttp.h"
#include "GC.h"
#include "TsRuntime.h"
#include <uv.h>
#include <llhttp.h>
#include <iostream>
#include <string>
#include <cstring>

#define GC_THREADS
#include <gc/gc.h>

struct ServerContext {
    TsHttpServer* server;
    uv_tcp_t client;
    llhttp_t parser;
    llhttp_settings_t settings;
    TsIncomingMessage* currentRequest;
    TsServerResponse* currentResponse;
    TsString* currentHeaderField;

    ServerContext(TsHttpServer* s) : server(s), currentRequest(nullptr), currentResponse(nullptr), currentHeaderField(nullptr) {
        client.data = this;
        parser.data = this;
    }
};

// TsIncomingMessage
TsIncomingMessage::TsIncomingMessage() : method(nullptr), url(nullptr) {
    headers = TsHeaders::Create();
}

TsIncomingMessage* TsIncomingMessage::Create() {
    void* mem = ts_alloc(sizeof(TsIncomingMessage));
    return new (mem) TsIncomingMessage();
}

// TsServerResponse
TsServerResponse::TsServerResponse(uv_stream_t* client) : client(client) {}

TsServerResponse* TsServerResponse::Create(uv_stream_t* client) {
    void* mem = ts_alloc(sizeof(TsServerResponse));
    return new (mem) TsServerResponse(client);
}

void TsServerResponse::WriteHead(int status, TsObject* headers) {
    this->status = status;
    this->headersSent = true;
    
    std::string head = "HTTP/1.1 " + std::to_string(status) + " OK\r\n";
    if (headers) {
        // TODO: Iterate headers and append
    }
    // For now, just a basic header
    head += "Content-Type: text/plain\r\n";
    head += "Transfer-Encoding: chunked\r\n";
    head += "\r\n";

    uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
    char* buf = (char*)malloc(head.length());
    memcpy(buf, head.c_str(), head.length());
    uv_buf_t uv_buf = uv_buf_init(buf, (unsigned int)head.length());
    req->data = buf;
    uv_write(req, client, &uv_buf, 1, [](uv_write_t* req, int status) {
        free(req->data);
        free(req);
    });
}

void TsServerResponse::Write(TsValue data) {
    if (!headersSent) WriteHead(200, nullptr);
    
    std::string body;
    if (data.type == ValueType::STRING_PTR) {
        body = ((TsString*)data.ptr_val)->ToUtf8();
    } else {
        // TODO: Handle other types
        body = "TODO: Handle non-string body";
    }

    if (body.empty()) return;

    char chunk_header[32];
    sprintf(chunk_header, "%x\r\n", (unsigned int)body.length());
    std::string chunk = std::string(chunk_header) + body + "\r\n";

    uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
    char* buf = (char*)malloc(chunk.length());
    memcpy(buf, chunk.c_str(), chunk.length());
    uv_buf_t uv_buf = uv_buf_init(buf, (unsigned int)chunk.length());
    req->data = buf;
    uv_write(req, client, &uv_buf, 1, [](uv_write_t* req, int status) {
        free(req->data);
        free(req);
    });
}

void TsServerResponse::End(TsValue data) {
    if (data.type != ValueType::UNDEFINED) {
        Write(data);
    } else if (!headersSent) {
        WriteHead(200, nullptr);
    }

    std::string end_chunk = "0\r\n\r\n";
    uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
    char* buf = (char*)malloc(end_chunk.length());
    memcpy(buf, end_chunk.c_str(), end_chunk.length());
    uv_buf_t uv_buf = uv_buf_init(buf, (unsigned int)end_chunk.length());
    req->data = buf;
    uv_write(req, client, &uv_buf, 1, [](uv_write_t* req, int status) {
        free(req->data);
        free(req);
        // Close connection after end
        // uv_close((uv_handle_t*)req->handle, nullptr); // This is wrong, req->handle is the stream
    });
    
    // We need to close the stream after the write is finished.
    // For now, let's just close it.
    // uv_close((uv_handle_t*)client, nullptr);
}

// TsHttpServer
TsHttpServer::TsHttpServer(void* callbackVTable, void* callbackFunc) 
    : requestCallbackVTable(callbackVTable), requestCallbackFunc(callbackFunc) {
    tcp.data = this;
}

TsHttpServer* TsHttpServer::Create(void* callbackVTable, void* callbackFunc) {
    void* mem = ts_alloc(sizeof(TsHttpServer));
    return new (mem) TsHttpServer(callbackVTable, callbackFunc);
}

static void on_close(uv_handle_t* handle) {
    // ServerContext* ctx = (ServerContext*)handle->data;
    // No-op for now, GC will handle it
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    ServerContext* ctx = (ServerContext*)stream->data;
    if (nread > 0) {
        llhttp_errno_t err = llhttp_execute(&ctx->parser, buf->base, nread);
        if (err != HPE_OK) {
            uv_close((uv_handle_t*)stream, on_close);
        }
    } else if (nread < 0) {
        uv_close((uv_handle_t*)stream, on_close);
    }
    if (buf->base) free(buf->base);
}

static int on_url(llhttp_t* parser, const char* at, size_t length) {
    ServerContext* ctx = (ServerContext*)parser->data;
    ctx->currentRequest->url = TsString::Create(std::string(at, length).c_str());
    ctx->currentRequest->method = TsString::Create(llhttp_method_name((llhttp_method_t)parser->method));
    return 0;
}

static int on_header_field(llhttp_t* parser, const char* at, size_t length) {
    ServerContext* ctx = (ServerContext*)parser->data;
    std::string field(at, length);
    for (auto& c : field) c = std::tolower(c);
    ctx->currentHeaderField = TsString::Create(field.c_str());
    return 0;
}

static int on_header_value(llhttp_t* parser, const char* at, size_t length) {
    ServerContext* ctx = (ServerContext*)parser->data;
    ctx->currentRequest->headers->Append(ctx->currentHeaderField, TsString::Create(std::string(at, length).c_str()));
    return 0;
}

static int on_message_complete(llhttp_t* parser) {
    ServerContext* ctx = (ServerContext*)parser->data;
    
    typedef void (*RequestCallback)(void*, TsValue*, TsValue*);
    RequestCallback cb = (RequestCallback)ctx->server->requestCallbackFunc;
    
    TsValue reqVal = ctx->currentRequest;
    TsValue resVal = ctx->currentResponse;
    
    cb(ctx->server->requestCallbackVTable, &reqVal, &resVal);
    
    return 0;
}

static void on_new_connection(uv_stream_t* server_stream, int status) {
    if (status < 0) return;

    TsHttpServer* server = (TsHttpServer*)server_stream->data;
    
    // Allocate ServerContext on GC heap
    ServerContext* ctx = (ServerContext*)ts_alloc(sizeof(ServerContext));
    new (ctx) ServerContext(server);
    
    uv_tcp_init(uv_default_loop(), &ctx->client);
    ctx->client.data = ctx;
    if (uv_accept(server_stream, (uv_stream_t*)&ctx->client) == 0) {
        llhttp_settings_init(&ctx->settings);
        ctx->settings.on_url = on_url;
        ctx->settings.on_header_field = on_header_field;
        ctx->settings.on_header_value = on_header_value;
        ctx->settings.on_message_complete = on_message_complete;
        
        llhttp_init(&ctx->parser, HTTP_REQUEST, &ctx->settings);
        ctx->parser.data = ctx;
        
        ctx->currentRequest = TsIncomingMessage::Create();
        ctx->currentResponse = TsServerResponse::Create((uv_stream_t*)&ctx->client);
        
        uv_read_start((uv_stream_t*)&ctx->client, [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
            buf->base = (char*)malloc(suggested_size);
            buf->len = (unsigned int)suggested_size;
        }, on_read);
    } else {
        uv_close((uv_handle_t*)&ctx->client, on_close);
    }
}

void TsHttpServer::Listen(int port, void* callbackVTable, void* callbackFunc) {
    uv_tcp_init(uv_default_loop(), &tcp);
    tcp.data = this; // Ensure data is set
    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", port, &addr);
    
    int r = uv_tcp_bind(&tcp, (const struct sockaddr*)&addr, 0);
    if (r) {
        return;
    }
    
    r = uv_listen((uv_stream_t*)&tcp, 128, on_new_connection);
}

extern "C" {
    void* ts_http_create_server(void* context, void* vtable, void* callback) {
        return TsHttpServer::Create(vtable, callback);
    }

    void ts_http_server_listen(void* context, void* server, int32_t port, void* vtable, void* callback) {
        ((TsHttpServer*)server)->Listen(port, vtable, callback);
    }

    void* ts_incoming_message_method(void* context, void* msg) {
        return ((TsIncomingMessage*)msg)->method;
    }

    void* ts_incoming_message_url(void* context, void* msg) {
        return ((TsIncomingMessage*)msg)->url;
    }

    void* ts_incoming_message_headers(void* context, void* msg) {
        TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
        res->type = ValueType::OBJECT_PTR;
        res->ptr_val = ((TsIncomingMessage*)msg)->headers;
        return res;
    }

    void ts_server_response_write_head(void* context, void* res, int32_t status, void* headers) {
        ((TsServerResponse*)res)->WriteHead(status, (TsObject*)headers);
    }

    void ts_server_response_write(void* context, void* res, TsValue* data) {
        ((TsServerResponse*)res)->Write(*data);
    }

    void ts_server_response_end(void* context, void* res, TsValue* data) {
        ((TsServerResponse*)res)->End(*data);
    }

    // VTable wrappers
    void Server_listen(void* context, void* server, int64_t port, void* vtable, void* callback) {
        ts_http_server_listen(context, server, (int32_t)port, vtable, callback);
    }

    void ServerResponse_writeHead(void* context, void* res, int64_t status, void* headers) {
        ts_server_response_write_head(context, res, (int32_t)status, headers);
    }

    void ServerResponse_write(void* context, void* res, TsValue* data) {
        ts_server_response_write(context, res, data);
    }

    void ServerResponse_end(void* context, void* res, TsValue* data) {
        ts_server_response_end(context, res, data);
    }
}
