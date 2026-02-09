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
#define GC_THREADS
#include <gc/gc.h>
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
    BIO* rbio;
    BIO* wbio;
    bool is_https;

    TsHeaders* response_headers;
    int response_status;
    TsString* response_status_text;
    std::vector<uint8_t> response_body;

    TsString* current_header_field;
    void* response_vtable;
    std::string request_data;
    bool request_sent;
    bool done;

    FetchContext(ts::TsPromise* p, TsURL* u, void* vtable) : promise(p), url(u), ssl_ctx(nullptr), ssl(nullptr), rbio(nullptr), wbio(nullptr), is_https(false), response_headers(nullptr), response_status(0), response_status_text(nullptr), response_vtable(vtable), request_sent(false), done(false) {
        tcp.data = this;
        dns_req.data = this;
    }
};

static void flush_wbio(FetchContext* ctx) {
    if (!ctx->is_https || !ctx->wbio) return;
    char buf[8192];
    int bytes;
    while ((bytes = BIO_read(ctx->wbio, buf, sizeof(buf))) > 0) {
        uv_write_t* write_req = (uv_write_t*)malloc(sizeof(uv_write_t));
        char* write_buf = (char*)malloc(bytes);
        memcpy(write_buf, buf, bytes);
        uv_buf_t uv_buf = uv_buf_init(write_buf, bytes);
        write_req->data = write_buf;
        uv_write(write_req, (uv_stream_t*)&ctx->tcp, &uv_buf, 1, [](uv_write_t* req, int status) {
            free(req->data);
            free(req);
        });
    }
}

static void try_ssl_step(FetchContext* ctx) {
    if (!ctx->ssl) return;

    if (!SSL_is_init_finished(ctx->ssl)) {
        int ret = SSL_do_handshake(ctx->ssl);
        if (ret <= 0) {
            int err = SSL_get_error(ctx->ssl, ret);
            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                TsValue val = TsValue(TsString::Create("SSL handshake failed"));
                ts::ts_promise_reject_internal(ctx->promise, &val);
                return;
            }
        }
        flush_wbio(ctx);
    }

    if (SSL_is_init_finished(ctx->ssl)) {
        if (!ctx->request_sent) {
            SSL_write(ctx->ssl, ctx->request_data.c_str(), (int)ctx->request_data.length());
            ctx->request_sent = true;
            flush_wbio(ctx);
        }

        static char ssl_read_buf[16384];
        int ret = SSL_read(ctx->ssl, ssl_read_buf, sizeof(ssl_read_buf));
        if (ret > 0) {
            llhttp_errno_t err = llhttp_execute(&ctx->parser, ssl_read_buf, ret);
            (void)err;
        } else {
            int err = SSL_get_error(ctx->ssl, ret);
            (void)err;
        }

        flush_wbio(ctx);
    }
}

// llhttp callbacks
static int on_status(llhttp_t* parser, const char* at, size_t length) {
    FetchContext* ctx = (FetchContext*)parser->data;
    ctx->response_status = parser->status_code;
    ctx->response_status_text = TsString::Create(std::string(at, length).c_str());
    return 0;
}

static int on_header_field(llhttp_t* parser, const char* at, size_t length) {
    FetchContext* ctx = (FetchContext*)parser->data;
    std::string field(at, length);
    ctx->current_header_field = TsString::Create(field.c_str());
    return 0;
}

static int on_header_value(llhttp_t* parser, const char* at, size_t length) {
    FetchContext* ctx = (FetchContext*)parser->data;
    std::string value(at, length);
    ctx->response_headers->Append(ctx->current_header_field, TsString::Create(value.c_str()));
    return 0;
}

static int on_body(llhttp_t* parser, const char* at, size_t length) {
    FetchContext* ctx = (FetchContext*)parser->data;
    ctx->response_body.insert(ctx->response_body.end(), at, at + length);
    return 0;
}

static int on_message_complete(llhttp_t* parser) {
    FetchContext* ctx = (FetchContext*)parser->data;
    ctx->done = true;

    TsBuffer* body_buf = TsBuffer::Create(ctx->response_body.size());
    std::memcpy(body_buf->GetData(), ctx->response_body.data(), ctx->response_body.size());

    TsResponse* resp = TsResponse::Create(ctx->response_status, ctx->response_status_text, ctx->response_headers, body_buf);
    resp->vtable = ctx->response_vtable;
    TsValue val = TsValue(resp);
    ts::ts_promise_resolve_internal(ctx->promise, &val);

    if (!uv_is_closing((uv_handle_t*)&ctx->tcp)) {
        uv_close((uv_handle_t*)&ctx->tcp, nullptr);
    }

    return 0;
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    FetchContext* ctx = (FetchContext*)stream->data;
    if (ctx->done) {
        if (buf->base) free(buf->base);
        return;
    }

    if (nread > 0) {
        if (ctx->ssl) {
            BIO_write(ctx->rbio, buf->base, (int)nread);
            try_ssl_step(ctx);
        } else {
            llhttp_errno_t err = llhttp_execute(&ctx->parser, buf->base, nread);
            (void)err;
        }
    } else if (nread < 0) {
        if (nread != UV_EOF) {
            TsValue val = TsValue(TsString::Create("Read error"));
            ts::ts_promise_reject_internal(ctx->promise, &val);
        }
        if (!uv_is_closing((uv_handle_t*)stream)) {
            uv_close((uv_handle_t*)stream, nullptr);
        }
    }

    if (buf->base) free(buf->base);
}

static void on_connect(uv_connect_t* req, int status) {
    FetchContext* ctx = (FetchContext*)req->handle->data;
    free(req);
    if (status < 0) {
        TsValue val = TsValue(TsString::Create("Connect error"));
        ts::ts_promise_reject_internal(ctx->promise, &val);
        return;
    }

    if (ctx->is_https) {
        ctx->ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx->ssl_ctx) {
            return;
        }
        ctx->ssl = SSL_new(ctx->ssl_ctx);
        ctx->rbio = BIO_new(BIO_s_mem());
        ctx->wbio = BIO_new(BIO_s_mem());
        SSL_set_bio(ctx->ssl, ctx->rbio, ctx->wbio);
        SSL_set_connect_state(ctx->ssl);

        // Set SNI
        TsString* hostname = ctx->url->GetHostname();
        if (hostname) {
            SSL_set_tlsext_host_name(ctx->ssl, hostname->ToUtf8());
        }
    }

    uv_read_start((uv_stream_t*)&ctx->tcp, [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
        buf->base = (char*)malloc(suggested_size);
        buf->len = (unsigned int)suggested_size;
    }, on_read);

    if (ctx->is_https) {
        try_ssl_step(ctx);
    } else {
        uv_write_t* write_req = (uv_write_t*)malloc(sizeof(uv_write_t));
        char* req_buf = (char*)malloc(ctx->request_data.length());
        memcpy(req_buf, ctx->request_data.c_str(), ctx->request_data.length());
        uv_buf_t buf = uv_buf_init(req_buf, (unsigned int)ctx->request_data.length());
        write_req->data = req_buf;
        uv_write(write_req, (uv_stream_t*)&ctx->tcp, &buf, 1, [](uv_write_t* req, int status) {
            free(req->data);
            free(req);
        });
    }
}

static void on_resolved(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    FetchContext* ctx = (FetchContext*)req->data;
    if (status < 0) {
        TsValue val = TsValue(TsString::Create("DNS error"));
        ts::ts_promise_reject_internal(ctx->promise, &val);
        return;
    }

    uv_tcp_init(uv_default_loop(), &ctx->tcp);
    ctx->tcp.data = ctx;
    uv_connect_t* connect_req = (uv_connect_t*)malloc(sizeof(uv_connect_t));

    uv_tcp_connect(connect_req, &ctx->tcp, res->ai_addr, on_connect);
    uv_freeaddrinfo(res);
}

extern "C" {

void* ts_fetch(void* url_val, void* options_val) {
    OPENSSL_init_ssl(0, NULL);
    TsString* url_str = (TsString*)url_val;
    TsURL* url = TsURL::Create(url_str);
    ts::TsPromise* promise = ts::ts_promise_create();

    FetchContext* ctx = new(GC_malloc_uncollectable(sizeof(FetchContext))) FetchContext(promise, url, nullptr);
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

// --- Response property getters (unboxed return types for ext.json) ---

int64_t ts_response_status(void* resp) {
    return ((TsResponse*)resp)->GetStatus();
}

bool ts_response_ok(void* resp) {
    return ((TsResponse*)resp)->GetOk();
}

void* ts_response_statusText(void* resp) {
    return ((TsResponse*)resp)->GetStatusText();
}

void* ts_response_headers(void* resp) {
    return ((TsResponse*)resp)->GetHeaders();
}

// --- Response methods ---

void* ts_response_text(void* ctx, void* resp) {
    TsResponse* r = (TsResponse*)resp;
    TsString* str = r->GetBody()->ToString();
    return ts::ts_promise_resolve(nullptr, ts_value_make_string(str));
}

void* ts_response_json(void* ctx, void* resp) {
    TsResponse* r = (TsResponse*)resp;
    TsString* str = r->GetBody()->ToString();
    void* parsed = ts_json_parse(str);
    return ts::ts_promise_resolve(nullptr, (TsValue*)parsed);
}

} // extern "C"

// --- TsHeaders ---

TsHeaders* TsHeaders::Create() {
    void* mem = ts_alloc(sizeof(TsHeaders));
    return new(mem) TsHeaders();
}

TsHeaders::TsHeaders() {
    map = TsMap::Create();
}

void TsHeaders::Append(TsString* name, TsString* value) {
    TsValue val;
    val.type = ValueType::STRING_PTR;
    val.ptr_val = value;
    map->Set(name, val);
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

TsResponse::TsResponse(int status, TsString* statusText, TsHeaders* headers, TsBuffer* body)
    : status(status), statusText(statusText), headers(headers), body(body) {}

// Wrapper for .text() method called via dynamic dispatch
static TsValue* Response_text_wrapper(void* context) {
    TsResponse* resp = (TsResponse*)context;
    TsString* str = resp->GetBody()->ToString();
    return ts::ts_promise_resolve(nullptr, ts_value_make_string(str));
}

// Wrapper for .json() method called via dynamic dispatch
static TsValue* Response_json_wrapper(void* context) {
    TsResponse* resp = (TsResponse*)context;
    TsString* str = resp->GetBody()->ToString();
    void* parsed = ts_json_parse(str);
    return ts::ts_promise_resolve(nullptr, (TsValue*)parsed);
}

TsValue TsResponse::GetPropertyVirtual(const char* key) {
    if (strcmp(key, "status") == 0) {
        TsValue v;
        v.type = ValueType::NUMBER_INT;
        v.i_val = status;
        return v;
    }
    if (strcmp(key, "ok") == 0) {
        TsValue v;
        v.type = ValueType::BOOLEAN;
        v.b_val = (status >= 200 && status < 300);
        return v;
    }
    if (strcmp(key, "statusText") == 0) {
        TsValue v;
        v.type = ValueType::STRING_PTR;
        v.ptr_val = statusText;
        return v;
    }
    if (strcmp(key, "headers") == 0) {
        TsValue v;
        v.type = ValueType::OBJECT_PTR;
        v.ptr_val = headers;
        return v;
    }
    if (strcmp(key, "text") == 0) {
        TsValue v;
        v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Response_text_wrapper, this, FunctionType::COMPILED, 0);
        return v;
    }
    if (strcmp(key, "json") == 0) {
        TsValue v;
        v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Response_json_wrapper, this, FunctionType::COMPILED, 0);
        return v;
    }
    return TsObject::GetPropertyVirtual(key);
}
