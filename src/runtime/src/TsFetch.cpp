#include "TsFetch.h"
#include "GC.h"
#include "TsRuntime.h"
#include "TsPromise.h"
#include "TsJSON.h"
#include <new>
#include <uv.h>
#include <llhttp.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "TsURL.h"
#include "TsBuffer.h"
#include "TsArray.h"
#include <vector>
#include <string>
#include <new>

struct FetchContext {
    ts::TsPromise* promise;
    TsURL* url;
    uv_tcp_t tcp;
    uv_getaddrinfo_t dns_req;
    llhttp_t parser;
    llhttp_settings_t settings;
    
    SSL_CTX* ssl_ctx;
    SSL* ssl;
    bool is_https;

    TsHeaders* response_headers;
    int response_status;
    TsString* response_status_text;
    std::vector<uint8_t> response_body;
    
    TsString* current_header_field;
    void* response_vtable;
    std::string request_data;

    FetchContext(ts::TsPromise* p, TsURL* u, void* vtable) : promise(p), url(u), ssl_ctx(nullptr), ssl(nullptr), is_https(false), response_headers(nullptr), response_status(0), response_status_text(nullptr), response_vtable(vtable) {
        tcp.data = this;
        dns_req.data = this;
    }
};

// llhttp callbacks
static int on_status(llhttp_t* parser, const char* at, size_t length) {
    FetchContext* ctx = (FetchContext*)parser->data;
    ctx->response_status = parser->status_code;
    ctx->response_status_text = TsString::Create(std::string(at, length).c_str());
    return 0;
}

static int on_header_field(llhttp_t* parser, const char* at, size_t length) {
    FetchContext* ctx = (FetchContext*)parser->data;
    ctx->current_header_field = TsString::Create(std::string(at, length).c_str());
    return 0;
}

static int on_header_value(llhttp_t* parser, const char* at, size_t length) {
    FetchContext* ctx = (FetchContext*)parser->data;
    if (ctx->current_header_field) {
        ctx->response_headers->Append(ctx->current_header_field, TsString::Create(std::string(at, length).c_str()));
    }
    return 0;
}

static int on_body(llhttp_t* parser, const char* at, size_t length) {
    FetchContext* ctx = (FetchContext*)parser->data;
    ctx->response_body.insert(ctx->response_body.end(), at, at + length);
    return 0;
}

static int on_message_complete(llhttp_t* parser) {
    FetchContext* ctx = (FetchContext*)parser->data;
    TsBuffer* body_buf = TsBuffer::Create(ctx->response_body.size());
    std::memcpy(body_buf->GetData(), ctx->response_body.data(), ctx->response_body.size());
    
    TsResponse* resp = TsResponse::Create(ctx->response_status, ctx->response_status_text, ctx->response_headers, body_buf);
    resp->vtable = ctx->response_vtable;
    TsValue val = TsValue(resp);
    ts::ts_promise_resolve_internal(ctx->promise, &val);
    
    // Cleanup
    if (ctx->is_https) {
        SSL_free(ctx->ssl);
        SSL_CTX_free(ctx->ssl_ctx);
    }
    uv_close((uv_handle_t*)&ctx->tcp, [](uv_handle_t* handle) {
        // FetchContext is managed by GC
    });
    
    return 0;
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    FetchContext* ctx = (FetchContext*)stream->data;
    if (nread > 0) {
        if (ctx->is_https) {
            // Handle TLS read
            char out[4096];
            int bytes = SSL_read(ctx->ssl, out, sizeof(out));
            if (bytes > 0) {
                llhttp_execute(&ctx->parser, out, bytes);
            }
        } else {
            llhttp_errno_t err = llhttp_execute(&ctx->parser, buf->base, nread);
            if (err != HPE_OK) {
            }
        }
    } else if (nread < 0) {
        if (nread != UV_EOF) {
            TsValue val = TsValue(TsString::Create("Read error"));
            ts::ts_promise_reject_internal(ctx->promise, &val);
        } else {
        }
        uv_close((uv_handle_t*)stream, nullptr);
    }
    if (buf->base) free(buf->base);
}

static void on_connect(uv_connect_t* req, int status) {
    FetchContext* ctx = (FetchContext*)req->handle->data;
    if (status < 0) {
        TsValue val = TsValue(TsString::Create("Connect error"));
        ts::ts_promise_reject_internal(ctx->promise, &val);
        return;
    }

    if (ctx->is_https) {
        ctx->ssl_ctx = SSL_CTX_new(TLS_client_method());
        ctx->ssl = SSL_new(ctx->ssl_ctx);
        // SSL_set_fd(ctx->ssl, (int)ctx->tcp.socket); // This is platform dependent
        SSL_connect(ctx->ssl);
    }

    uv_write_t* write_req = (uv_write_t*)malloc(sizeof(uv_write_t));
    // We need to keep the request string alive until write is done
    std::string* req_str = new std::string(ctx->request_data);
    uv_buf_t buf = uv_buf_init((char*)req_str->c_str(), (unsigned int)req_str->length());
    write_req->data = req_str;
    
    if (ctx->is_https) {
        SSL_write(ctx->ssl, req_str->c_str(), (int)req_str->length());
    } else {
        uv_write(write_req, (uv_stream_t*)&ctx->tcp, &buf, 1, [](uv_write_t* req, int status) {
            delete (std::string*)req->data;
            free(req);
        });
    }

    uv_read_start((uv_stream_t*)&ctx->tcp, [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
        buf->base = (char*)malloc(suggested_size);
        buf->len = (unsigned int)suggested_size;
    }, on_read);
}

static void on_resolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    FetchContext* ctx = (FetchContext*)req->data;
    if (status < 0) {
        TsValue val = TsValue(TsString::Create("DNS error"));
        ts::ts_promise_reject_internal(ctx->promise, &val);
        return;
    }

    uv_tcp_init(uv_default_loop(), &ctx->tcp);
    uv_connect_t* connect_req = (uv_connect_t*)malloc(sizeof(uv_connect_t));
    uv_tcp_connect(connect_req, &ctx->tcp, res->ai_addr, on_connect);
    uv_freeaddrinfo(res);
}

// --- TsHeaders ---

TsHeaders* TsHeaders::Create() {
    void* mem = ts_alloc(sizeof(TsHeaders));
    return new(mem) TsHeaders();
}

TsHeaders::TsHeaders() {
    map = TsMap::Create();
}

void TsHeaders::Append(TsString* name, TsString* value) {
    // Simple implementation: just set for now. Real Headers append values.
    map->Set(name, TsValue(value));
}

TsString* TsHeaders::Get(TsString* name) {
    TsValue val = map->Get(name);
    if (val.type == ValueType::STRING_PTR) {
        return (TsString*)val.ptr_val;
    }
    return nullptr;
}

void TsHeaders::Set(TsString* name, TsString* value) {
    map->Set(name, TsValue(value));
}

bool TsHeaders::Has(TsString* name) {
    return map->Has(name);
}

void TsHeaders::Delete(TsString* name) {
    map->Delete(name);
}

// --- TsResponse ---

TsResponse* TsResponse::Create(int status, TsString* statusText, TsHeaders* headers, TsBuffer* body) {
    void* mem = ts_alloc(sizeof(TsResponse));
    return new(mem) TsResponse(status, statusText, headers, body);
}

TsResponse::TsResponse(int status, TsString* statusText, TsHeaders* headers, TsBuffer* body) {
    this->status = status;
    this->statusText = statusText;
    this->headers = headers;
    this->body = body;
}

void* TsResponse::Text() {
    ts::TsPromise* promise = ts::ts_promise_create();
    TsString* str = body ? body->ToString() : TsString::Create("");
    ts::ts_promise_resolve_internal(promise, ts_value_make_string(str));
    return promise;
}

void* TsResponse::Json() {
    ts::TsPromise* promise = ts::ts_promise_create();
    void* obj = body ? ts_json_parse(body->ToString()) : nullptr;
    ts::ts_promise_resolve_internal(promise, ts_value_make_object(obj));
    return promise;
}

void* TsResponse::ArrayBuffer() {
    return body;
}

// --- C Exports ---

extern "C" {
    void* ts_headers_create() { return TsHeaders::Create(); }
    void ts_headers_append(void* headers, void* name, void* value) { ((TsHeaders*)headers)->Append((TsString*)name, (TsString*)value); }
    void* ts_headers_get(void* headers, void* name) { return ((TsHeaders*)headers)->Get((TsString*)name); }
    void ts_headers_set(void* headers, void* name, void* value) { ((TsHeaders*)headers)->Set((TsString*)name, (TsString*)value); }
    bool ts_headers_has(void* headers, void* name) { return ((TsHeaders*)headers)->Has((TsString*)name); }
    void ts_headers_delete(void* headers, void* name) { ((TsHeaders*)headers)->Delete((TsString*)name); }

    void* Response_text(void* ctx, void* self) { 
        return ts_value_make_promise((ts::TsPromise*)((TsResponse*)self)->Text()); 
    }
    void* Response_json(void* ctx, void* self) { 
        return ts_value_make_promise((ts::TsPromise*)((TsResponse*)self)->Json()); 
    }

    void* Response_get_status(void* resp) { return ts_value_make_double(((TsResponse*)resp)->GetStatus()); }
    void* Response_get_statusText(void* resp) { return ((TsResponse*)resp)->GetStatusText(); }
    void* Response_get_headers(void* resp) { return ((TsResponse*)resp)->GetHeaders(); }

    void* ts_response_create(void* vtable, int32_t status, void* statusText, void* headers, void* body) {
        TsResponse* resp = TsResponse::Create(status, (TsString*)statusText, (TsHeaders*)headers, (TsBuffer*)body);
        resp->vtable = vtable;
        return resp;
    }

    int32_t ts_response_status(void* resp) { return ((TsResponse*)resp)->GetStatus(); }
    void* ts_response_statusText(void* resp) { return ((TsResponse*)resp)->GetStatusText(); }
    void* ts_response_headers(void* resp) { return ((TsResponse*)resp)->GetHeaders(); }
    void* ts_response_text(void* resp) { return ((TsResponse*)resp)->Text(); }
    void* ts_response_json(void* resp) { return ((TsResponse*)resp)->Json(); }
    void* ts_response_arrayBuffer(void* resp) { return ((TsResponse*)resp)->ArrayBuffer(); }

    void* ts_fetch(void* vtable, void* url_val, void* options_val) {
        TsString* url_str = (TsString*)url_val;
        TsURL* url = TsURL::Create(url_str);
        ts::TsPromise* promise = ts::ts_promise_create();
        
        FetchContext* ctx = new(ts_alloc(sizeof(FetchContext))) FetchContext(promise, url, vtable);
        ctx->is_https = std::string(url->GetProtocol()->ToUtf8()) == "https:";
        ctx->response_headers = TsHeaders::Create();

        // Parse options
        std::string method = "GET";
        std::string body = "";
        std::vector<std::pair<std::string, std::string>> headers;

        if (options_val) {
            TsValue* opt = (TsValue*)options_val;
            if (opt->type == ValueType::OBJECT_PTR) {
                TsMap* opt_map = (TsMap*)opt->ptr_val;
                
                // Method
                TsValue m_val = opt_map->Get(TsString::Create("method"));
                if (m_val.type == ValueType::STRING_PTR) {
                    method = ((TsString*)m_val.ptr_val)->ToUtf8();
                }

                // Body
                TsValue b_val = opt_map->Get(TsString::Create("body"));
                if (b_val.type == ValueType::STRING_PTR) {
                    body = ((TsString*)b_val.ptr_val)->ToUtf8();
                }

                // Headers
                TsValue h_val = opt_map->Get(TsString::Create("headers"));
                if (h_val.type == ValueType::OBJECT_PTR) {
                    // Could be TsMap or TsHeaders
                    uint32_t magic = *(uint32_t*)h_val.ptr_val;
                    TsMap* h_map = nullptr;
                    if (magic == TsHeaders::MAGIC) {
                        h_map = ((TsHeaders*)h_val.ptr_val)->GetMap();
                    } else if (magic == TsMap::MAGIC) {
                        h_map = (TsMap*)h_val.ptr_val;
                    }

                    if (h_map) {
                        TsArray* keys = (TsArray*)h_map->GetKeys();
                        for (int i = 0; i < keys->Length(); i++) {
                            TsString* key = (TsString*)keys->Get(i);
                            TsValue val = h_map->Get(key);
                            if (val.type == ValueType::STRING_PTR) {
                                headers.push_back({key->ToUtf8(), ((TsString*)val.ptr_val)->ToUtf8()});
                            }
                        }
                    }
                }
            }
        }

        // Build request
        ctx->request_data = method + " " + std::string(url->GetPathname()->ToUtf8()) + std::string(url->GetSearch()->ToUtf8()) + " HTTP/1.1\r\n";
        ctx->request_data += "Host: " + std::string(url->GetHost()->ToUtf8()) + "\r\n";
        ctx->request_data += "User-Agent: ts-aoc/1.0\r\n";
        ctx->request_data += "Accept: */*\r\n";
        
        bool has_content_length = false;
        for (const auto& h : headers) {
            ctx->request_data += h.first + ": " + h.second + "\r\n";
            if (h.first == "Content-Length") has_content_length = true;
        }

        if (!body.empty() && !has_content_length) {
            ctx->request_data += "Content-Length: " + std::to_string(body.length()) + "\r\n";
        }

        ctx->request_data += "Connection: close\r\n\r\n";
        if (!body.empty()) {
            ctx->request_data += body;
        }

        llhttp_settings_init(&ctx->settings);
        ctx->settings.on_status = on_status;
        ctx->settings.on_header_field = on_header_field;
        ctx->settings.on_header_value = on_header_value;
        ctx->settings.on_body = on_body;
        ctx->settings.on_message_complete = on_message_complete;

        llhttp_init(&ctx->parser, HTTP_RESPONSE, &ctx->settings);
        ctx->parser.data = ctx;

        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        std::string host = url->GetHostname()->ToUtf8();
        std::string port = url->GetPort()->ToUtf8();
        if (port.empty()) port = ctx->is_https ? "443" : "80";

        uv_getaddrinfo(uv_default_loop(), &ctx->dns_req, on_resolved, host.c_str(), port.c_str(), &hints);

        return ts_value_make_promise(promise);
    }
}
