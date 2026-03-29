#include "TsFetch.h"
#include "GC.h"
#include "TsNanBox.h"
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
#include "TsFlatObject.h"
#include <vector>
#include <string>
#include <set>
#include <new>

struct FetchContext;
static std::set<FetchContext*> g_active_fetches;

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
                ts::ts_promise_reject_internal(ctx->promise, (TsValue*)TsString::Create("SSL handshake failed"));
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

static void on_fetch_close(uv_handle_t* handle) {
    FetchContext* ctx = (FetchContext*)handle->data;
    g_active_fetches.erase(ctx);
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
    ts::ts_promise_resolve_internal(ctx->promise, (TsValue*)resp);

    if (!uv_is_closing((uv_handle_t*)&ctx->tcp)) {
        uv_close((uv_handle_t*)&ctx->tcp, on_fetch_close);
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
        if (!ctx->done) {
            ctx->done = true;
            if (nread != UV_EOF) {
                ts::ts_promise_reject_internal(ctx->promise, (TsValue*)TsString::Create("Read error"));
            }
        }
        if (!uv_is_closing((uv_handle_t*)stream)) {
            uv_close((uv_handle_t*)stream, on_fetch_close);
        }
    }

    if (buf->base) free(buf->base);
}

static void on_connect(uv_connect_t* req, int status) {
    FetchContext* ctx = (FetchContext*)req->handle->data;
    free(req);
    if (status < 0) {
        ts::ts_promise_reject_internal(ctx->promise, (TsValue*)TsString::Create("Connect error"));
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
        ts::ts_promise_reject_internal(ctx->promise, (TsValue*)TsString::Create("DNS error"));
        return;
    }

    uv_tcp_init(uv_default_loop(), &ctx->tcp);
    ctx->tcp.data = ctx;
    uv_connect_t* connect_req = (uv_connect_t*)malloc(sizeof(uv_connect_t));

    uv_tcp_connect(connect_req, &ctx->tcp, res->ai_addr, on_connect);
    uv_freeaddrinfo(res);
}

extern "C" {

// --- Headers C wrappers ---
void* ts_headers_create() {
    return TsHeaders::Create();
}

void ts_headers_append(void* headers, void* name, void* value) {
    TsHeaders* h = (TsHeaders*)headers;
    h->Append((TsString*)name, (TsString*)value);
}

void* ts_headers_get(void* headers, void* name) {
    TsHeaders* h = (TsHeaders*)headers;
    return h->Get((TsString*)name);
}

void ts_headers_set(void* headers, void* name, void* value) {
    TsHeaders* h = (TsHeaders*)headers;
    h->Set((TsString*)name, (TsString*)value);
}

bool ts_headers_has(void* headers, void* name) {
    TsHeaders* h = (TsHeaders*)headers;
    return h->Has((TsString*)name);
}

void ts_headers_delete(void* headers, void* name) {
    TsHeaders* h = (TsHeaders*)headers;
    h->Delete((TsString*)name);
}

void* ts_fetch(void* url_val, void* options_val) {
    OPENSSL_init_ssl(0, NULL);
    TsString* url_str = (TsString*)url_val;
    TsURL* url = TsURL::Create(url_str);
    ts::TsPromise* promise = ts::ts_promise_create();

    FetchContext* ctx = new(ts_alloc(sizeof(FetchContext))) FetchContext(promise, url, nullptr);
    g_active_fetches.insert(ctx);
    ctx->is_https = std::string(url->GetProtocol()->ToUtf8()) == "https:";
    ctx->response_headers = TsHeaders::Create();

    // Parse options
    std::string method = "GET";
    std::string body = "";
    std::vector<std::pair<std::string, std::string>> headers;

    if (options_val) {
        TsValue optDec = nanbox_to_tagged((TsValue*)options_val);
        if (optDec.type == ValueType::OBJECT_PTR) {
            void* opt_ptr = optDec.ptr_val;
            // Handle flat objects (object literals compiled with inline slots)
            if (is_flat_object(opt_ptr)) {
                opt_ptr = ts_flat_object_to_map(opt_ptr);
            }
            TsMap* opt_map = (TsMap*)opt_ptr;

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
                void* h_ptr = h_val.ptr_val;
                // Handle flat objects for headers
                if (is_flat_object(h_ptr)) {
                    h_ptr = ts_flat_object_to_map(h_ptr);
                }
                uint32_t magic = *(uint32_t*)h_ptr;
                TsMap* h_map = nullptr;
                if (magic == TsHeaders::MAGIC) {
                    h_map = ((TsHeaders*)h_ptr)->GetMap();
                } else if (magic == TsMap::MAGIC) {
                    h_map = (TsMap*)h_ptr;
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

// --- Helper: extract TsMap* from a NaN-boxed object (flat object or TsMap) ---
static TsMap* unbox_to_map(void* val) {
    if (!val) return nullptr;
    TsValue decoded = nanbox_to_tagged((TsValue*)val);
    if (decoded.type != ValueType::OBJECT_PTR || !decoded.ptr_val) return nullptr;
    void* ptr = decoded.ptr_val;
    if (is_flat_object(ptr)) ptr = ts_flat_object_to_map(ptr);
    uint32_t m = *(uint32_t*)((char*)ptr + 16);
    if (m == TsMap::MAGIC) return (TsMap*)ptr;
    return nullptr;
}

// --- Helper: build TsHeaders from init (null, TsHeaders, flat obj, or TsMap) ---
static TsHeaders* headers_from_init(void* init) {
    TsHeaders* h = TsHeaders::Create();
    if (!init) return h;

    TsValue decoded = nanbox_to_tagged((TsValue*)init);
    if (decoded.type != ValueType::OBJECT_PTR || !decoded.ptr_val) return h;

    void* ptr = decoded.ptr_val;
    if (is_flat_object(ptr)) ptr = ts_flat_object_to_map(ptr);

    uint32_t magic = *(uint32_t*)ptr;
    TsMap* src = nullptr;
    if (magic == TsHeaders::MAGIC) {
        src = ((TsHeaders*)ptr)->GetMap();
    } else {
        // Check magic at offset 16 for TsMap
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 == TsMap::MAGIC) src = (TsMap*)ptr;
    }

    if (src) {
        TsArray* keys = (TsArray*)src->GetKeys();
        for (int i = 0; i < keys->Length(); i++) {
            TsString* key = (TsString*)keys->Get(i);
            TsValue val = src->Get(key);
            if (val.type == ValueType::STRING_PTR) {
                h->Append(key, (TsString*)val.ptr_val);
            }
        }
    }
    return h;
}

// --- Helper: extract TsString* from a NaN-boxed value ---
static TsString* unbox_string(void* val) {
    if (!val) return nullptr;
    TsValue decoded = nanbox_to_tagged((TsValue*)val);
    if (decoded.type == ValueType::STRING_PTR && decoded.ptr_val) {
        return (TsString*)decoded.ptr_val;
    }
    return nullptr;
}

// --- Helper: create TsBuffer from a NaN-boxed body value (string, Buffer, or null) ---
static TsBuffer* body_from_value(void* val) {
    if (!val) return TsBuffer::Create(0);
    uint64_t nb = (uint64_t)(uintptr_t)val;
    if (nanbox_is_null(nb) || nanbox_is_undefined(nb)) return TsBuffer::Create(0);

    TsValue decoded = nanbox_to_tagged((TsValue*)val);
    if (decoded.type == ValueType::STRING_PTR && decoded.ptr_val) {
        TsString* str = (TsString*)decoded.ptr_val;
        const char* utf8 = str->ToUtf8();
        size_t len = strlen(utf8);
        TsBuffer* buf = TsBuffer::Create(len);
        memcpy(buf->GetData(), utf8, len);
        return buf;
    }
    if (decoded.type == ValueType::OBJECT_PTR && decoded.ptr_val) {
        uint32_t magic = *(uint32_t*)decoded.ptr_val;
        if (magic == TsBuffer::MAGIC) return (TsBuffer*)decoded.ptr_val;
    }
    return TsBuffer::Create(0);
}

// --- Web Standards constructors ---

void* ts_request_create(void* url_val, void* options_val) {
    TsString* url = unbox_string(url_val);
    if (!url) url = TsString::Create("");

    TsString* method = TsString::Create("GET");
    TsHeaders* headers = TsHeaders::Create();
    TsBuffer* body = TsBuffer::Create(0);

    if (options_val) {
        TsMap* opts = unbox_to_map(options_val);
        if (opts) {
            TsValue m = opts->Get(TsString::Create("method"));
            if (m.type == ValueType::STRING_PTR) method = (TsString*)m.ptr_val;

            TsValue h = opts->Get(TsString::Create("headers"));
            if (h.type == ValueType::OBJECT_PTR) {
                headers = headers_from_init((void*)(uintptr_t)nanbox_ptr(h.ptr_val));
            }

            TsValue b = opts->Get(TsString::Create("body"));
            if (b.type == ValueType::STRING_PTR) {
                const char* utf8 = ((TsString*)b.ptr_val)->ToUtf8();
                size_t len = strlen(utf8);
                body = TsBuffer::Create(len);
                memcpy(body->GetData(), utf8, len);
            }
        }
    }

    return TsRequest::Create(url, method, headers, body);
}

void* ts_response_create(void* body_val, void* options_val) {
    TsBuffer* body = body_from_value(body_val);
    int status = 200;
    TsString* statusText = TsString::Create("");
    TsHeaders* headers = TsHeaders::Create();

    if (options_val) {
        TsMap* opts = unbox_to_map(options_val);
        if (opts) {
            TsValue s = opts->Get(TsString::Create("status"));
            if (s.type == ValueType::NUMBER_INT) status = (int)s.i_val;
            else if (s.type == ValueType::NUMBER_DBL) status = (int)s.d_val;

            TsValue st = opts->Get(TsString::Create("statusText"));
            if (st.type == ValueType::STRING_PTR) statusText = (TsString*)st.ptr_val;

            TsValue h = opts->Get(TsString::Create("headers"));
            if (h.type == ValueType::OBJECT_PTR) {
                headers = headers_from_init((void*)(uintptr_t)nanbox_ptr(h.ptr_val));
            }
        }
    }

    return TsResponse::Create(status, statusText, headers, body);
}

void* ts_headers_create_from_init(void* init) {
    return headers_from_init(init);
}

void* ts_response_json_static(void* data, void* init) {
    // data comes as NaN-boxed from ext.json lowering - box it for ts_json_stringify
    void* boxed_data = data;
    if (data && is_flat_object(data)) {
        boxed_data = ts_value_make_object(data);
    }
    TsString* json = (TsString*)ts_json_stringify(boxed_data, nullptr, nullptr);
    if (!json) json = TsString::Create("null");

    const char* utf8 = json->ToUtf8();
    size_t len = strlen(utf8);
    TsBuffer* body = TsBuffer::Create(len);
    memcpy(body->GetData(), utf8, len);

    int status = 200;
    TsHeaders* headers = TsHeaders::Create();
    headers->Set(TsString::Create("content-type"), TsString::Create("application/json"));

    if (init) {
        TsMap* opts = unbox_to_map(init);
        if (opts) {
            TsValue s = opts->Get(TsString::Create("status"));
            if (s.type == ValueType::NUMBER_INT) status = (int)s.i_val;
            else if (s.type == ValueType::NUMBER_DBL) status = (int)s.d_val;
        }
    }

    return TsResponse::Create(status, TsString::Create(""), headers, body);
}

void* ts_response_redirect_static(void* url_val, void* status_val) {
    TsString* url = unbox_string(url_val);
    if (!url) url = TsString::Create("/");

    int status = 302;
    if (status_val) {
        uint64_t nb = (uint64_t)(uintptr_t)status_val;
        if (nanbox_is_int32(nb)) status = (int)nanbox_to_int32(nb);
        else if (nanbox_is_double(nb)) status = (int)nanbox_to_double(nb);
    }

    TsHeaders* headers = TsHeaders::Create();
    headers->Set(TsString::Create("location"), url);

    return TsResponse::Create(status, TsString::Create(""), headers, TsBuffer::Create(0));
}

// Request property getters for ext.json
void* ts_request_get_method(void* self) {
    TsRequest* req = (TsRequest*)self;
    return req->GetMethod();
}

void* ts_request_get_url(void* self) {
    TsRequest* req = (TsRequest*)self;
    return req->GetUrl();
}

void* ts_request_get_headers(void* self) {
    TsRequest* req = (TsRequest*)self;
    return req->GetHeaders();
}

void* ts_request_text(void* self) {
    TsRequest* req = (TsRequest*)self;
    TsBuffer* body = req->GetBody();
    TsString* text;
    if (body && body->GetLength() > 0) {
        size_t len = body->GetLength();
        char* buf = (char*)ts_alloc(len + 1);
        memcpy(buf, body->GetData(), len);
        buf[len] = '\0';
        text = TsString::Create(buf);
    } else {
        text = TsString::Create("");
    }
    return ts::ts_promise_resolve(nullptr, (TsValue*)ts_value_make_string(text));
}

void* ts_request_json(void* self) {
    TsRequest* req = (TsRequest*)self;
    TsBuffer* body = req->GetBody();
    TsString* text;
    if (body && body->GetLength() > 0) {
        size_t len = body->GetLength();
        char* buf = (char*)ts_alloc(len + 1);
        memcpy(buf, body->GetData(), len);
        buf[len] = '\0';
        text = TsString::Create(buf);
    } else {
        text = TsString::Create("null");
    }
    void* parsed = ts_json_parse(text);
    return ts::ts_promise_resolve(nullptr, (TsValue*)parsed);
}

void* ts_request_clone(void* self) {
    TsRequest* req = (TsRequest*)self;
    return req->Clone();
}

} // extern "C"

// ============================================================================
// TsHeaders
// ============================================================================

TsHeaders* TsHeaders::Create() {
    void* mem = ts_alloc(sizeof(TsHeaders));
    return new(mem) TsHeaders();
}

TsHeaders::TsHeaders() {
    this->TsObject::magic = MAGIC;
    map = TsMap::Create();
    ts_gc_write_barrier(&this->map, map);
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

// Native function wrappers for Headers methods (ctx = TsHeaders*)
static TsValue* Headers_get_native(void* ctx, int argc, TsValue** argv) {
    TsHeaders* h = (TsHeaders*)ctx;
    if (!h || argc < 1) return ts_value_make_null();
    TsString* name = unbox_string(argv[0]);
    if (!name) return ts_value_make_null();
    TsString* val = h->Get(name);
    return val ? ts_value_make_string(val) : ts_value_make_null();
}

static TsValue* Headers_set_native(void* ctx, int argc, TsValue** argv) {
    TsHeaders* h = (TsHeaders*)ctx;
    if (!h || argc < 2) return ts_value_make_undefined();
    TsString* name = unbox_string(argv[0]);
    TsString* value = unbox_string(argv[1]);
    if (name && value) h->Set(name, value);
    return ts_value_make_undefined();
}

static TsValue* Headers_has_native(void* ctx, int argc, TsValue** argv) {
    TsHeaders* h = (TsHeaders*)ctx;
    if (!h || argc < 1) return ts_value_make_bool(false);
    TsString* name = unbox_string(argv[0]);
    if (!name) return ts_value_make_bool(false);
    return ts_value_make_bool(h->Has(name));
}

static TsValue* Headers_append_native(void* ctx, int argc, TsValue** argv) {
    TsHeaders* h = (TsHeaders*)ctx;
    if (!h || argc < 2) return ts_value_make_undefined();
    TsString* name = unbox_string(argv[0]);
    TsString* value = unbox_string(argv[1]);
    if (name && value) h->Append(name, value);
    return ts_value_make_undefined();
}

static TsValue* Headers_delete_native(void* ctx, int argc, TsValue** argv) {
    TsHeaders* h = (TsHeaders*)ctx;
    if (!h || argc < 1) return ts_value_make_undefined();
    TsString* name = unbox_string(argv[0]);
    if (name) h->Delete(name);
    return ts_value_make_undefined();
}

static TsValue* Headers_forEach_native(void* ctx, int argc, TsValue** argv) {
    TsHeaders* h = (TsHeaders*)ctx;
    if (!h || argc < 1) return ts_value_make_undefined();
    TsValue* callback = argv[0];
    if (!callback) return ts_value_make_undefined();

    TsMap* map = h->GetMap();
    TsArray* keys = (TsArray*)map->GetKeys();
    for (int i = 0; i < keys->Length(); i++) {
        TsString* key = (TsString*)keys->Get(i);
        TsValue val = map->Get(key);
        if (val.type == ValueType::STRING_PTR) {
            ts_call_2(callback, ts_value_make_string((TsString*)val.ptr_val), ts_value_make_string(key));
        }
    }
    return ts_value_make_undefined();
}

TsValue TsHeaders::GetPropertyVirtual(const char* key) {
    if (strcmp(key, "get") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Headers_get_native, this, FunctionType::NATIVE, 1);
        return v;
    }
    if (strcmp(key, "set") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Headers_set_native, this, FunctionType::NATIVE, 2);
        return v;
    }
    if (strcmp(key, "has") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Headers_has_native, this, FunctionType::NATIVE, 1);
        return v;
    }
    if (strcmp(key, "append") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Headers_append_native, this, FunctionType::NATIVE, 2);
        return v;
    }
    if (strcmp(key, "delete") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Headers_delete_native, this, FunctionType::NATIVE, 1);
        return v;
    }
    if (strcmp(key, "forEach") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Headers_forEach_native, this, FunctionType::NATIVE, 1);
        return v;
    }
    // Fallback: look up arbitrary header names in the internal map
    // This supports dot/bracket access like headers.referrer or headers["content-type"]
    if (map) {
        TsString* nameStr = TsString::GetInterned(key);
        TsValue k;
        k.type = ValueType::STRING_PTR;
        k.ptr_val = nameStr;
        TsValue val = map->Get(k);
        if (val.type != ValueType::UNDEFINED) {
            return val;
        }
    }
    return TsObject::GetPropertyVirtual(key);
}

// ============================================================================
// TsResponse
// ============================================================================

TsResponse* TsResponse::Create(int status, TsString* statusText, TsHeaders* headers, TsBuffer* body) {
    void* mem = ts_alloc(sizeof(TsResponse));
    return new(mem) TsResponse(status, statusText, headers, body);
}

TsResponse::TsResponse(int status, TsString* statusText, TsHeaders* headers, TsBuffer* body)
    : status(status), statusText(statusText), headers(headers), body(body) {
    this->TsObject::magic = MAGIC;
}

static TsValue* Response_text_native(void* ctx, int argc, TsValue** argv) {
    TsResponse* resp = (TsResponse*)ctx;
    if (!resp) return ts::ts_promise_resolve(nullptr, ts_value_make_string(TsString::Create("")));
    TsString* str = resp->GetBody()->ToString();
    return ts::ts_promise_resolve(nullptr, ts_value_make_string(str));
}

static TsValue* Response_json_native(void* ctx, int argc, TsValue** argv) {
    TsResponse* resp = (TsResponse*)ctx;
    if (!resp) return ts::ts_promise_resolve(nullptr, ts_value_make_undefined());
    TsString* str = resp->GetBody()->ToString();
    void* parsed = ts_json_parse(str);
    return ts::ts_promise_resolve(nullptr, (TsValue*)parsed);
}

TsValue TsResponse::GetPropertyVirtual(const char* key) {
    if (strcmp(key, "status") == 0) {
        TsValue v; v.type = ValueType::NUMBER_INT; v.i_val = status; return v;
    }
    if (strcmp(key, "ok") == 0) {
        TsValue v; v.type = ValueType::BOOLEAN; v.b_val = (status >= 200 && status < 300); return v;
    }
    if (strcmp(key, "statusText") == 0) {
        TsValue v; v.type = ValueType::STRING_PTR; v.ptr_val = statusText; return v;
    }
    if (strcmp(key, "headers") == 0) {
        TsValue v; v.type = ValueType::OBJECT_PTR; v.ptr_val = headers; return v;
    }
    if (strcmp(key, "body") == 0) {
        // Return null for body (ReadableStream not implemented)
        TsValue v; v.type = ValueType::UNDEFINED; v.i_val = 0;
        if (body && body->GetLength() > 0) {
            v.type = ValueType::OBJECT_PTR; v.ptr_val = body;
        }
        return v;
    }
    if (strcmp(key, "text") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Response_text_native, this, FunctionType::NATIVE, 0);
        return v;
    }
    if (strcmp(key, "json") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Response_json_native, this, FunctionType::NATIVE, 0);
        return v;
    }
    return TsObject::GetPropertyVirtual(key);
}

// ============================================================================
// TsRequest
// ============================================================================

TsRequest* TsRequest::Create(TsString* url, TsString* method, TsHeaders* headers, TsBuffer* body) {
    void* mem = ts_alloc(sizeof(TsRequest));
    return new(mem) TsRequest(url, method, headers, body);
}

TsRequest::TsRequest(TsString* url, TsString* method, TsHeaders* headers, TsBuffer* body)
    : method(method), url(url), headers(headers), body(body) {
    this->TsObject::magic = MAGIC;
}

TsRequest* TsRequest::Clone() {
    // Clone body buffer
    TsBuffer* newBody = TsBuffer::Create(body ? body->GetLength() : 0);
    if (body && body->GetLength() > 0) {
        memcpy(newBody->GetData(), body->GetData(), body->GetLength());
    }
    // Clone headers
    TsHeaders* newHeaders = TsHeaders::Create();
    TsMap* srcMap = headers->GetMap();
    TsArray* keys = (TsArray*)srcMap->GetKeys();
    for (int i = 0; i < keys->Length(); i++) {
        TsString* key = (TsString*)keys->Get(i);
        TsValue val = srcMap->Get(key);
        if (val.type == ValueType::STRING_PTR) {
            newHeaders->Append(key, (TsString*)val.ptr_val);
        }
    }
    return TsRequest::Create(url, method, newHeaders, newBody);
}

static TsValue* Request_text_native(void* ctx, int argc, TsValue** argv) {
    TsRequest* req = (TsRequest*)ctx;
    if (!req || !req->GetBody()) return ts::ts_promise_resolve(nullptr, ts_value_make_string(TsString::Create("")));
    TsString* str = req->GetBody()->ToString();
    return ts::ts_promise_resolve(nullptr, ts_value_make_string(str));
}

static TsValue* Request_json_native(void* ctx, int argc, TsValue** argv) {
    TsRequest* req = (TsRequest*)ctx;
    if (!req || !req->GetBody()) return ts::ts_promise_resolve(nullptr, ts_value_make_undefined());
    TsString* str = req->GetBody()->ToString();
    void* parsed = ts_json_parse(str);
    return ts::ts_promise_resolve(nullptr, (TsValue*)parsed);
}

static TsValue* Request_clone_native(void* ctx, int argc, TsValue** argv) {
    TsRequest* req = (TsRequest*)ctx;
    if (!req) return ts_value_make_undefined();
    return ts_value_make_object(req->Clone());
}

TsValue TsRequest::GetPropertyVirtual(const char* key) {
    if (strcmp(key, "method") == 0) {
        TsValue v; v.type = ValueType::STRING_PTR; v.ptr_val = method; return v;
    }
    if (strcmp(key, "url") == 0) {
        TsValue v; v.type = ValueType::STRING_PTR; v.ptr_val = url; return v;
    }
    if (strcmp(key, "headers") == 0) {
        TsValue v; v.type = ValueType::OBJECT_PTR; v.ptr_val = headers; return v;
    }
    if (strcmp(key, "body") == 0) {
        TsValue v; v.type = ValueType::UNDEFINED; v.i_val = 0;
        if (body && body->GetLength() > 0) {
            v.type = ValueType::OBJECT_PTR; v.ptr_val = body;
        }
        return v;
    }
    if (strcmp(key, "text") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Request_text_native, this, FunctionType::NATIVE, 0);
        return v;
    }
    if (strcmp(key, "json") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Request_json_native, this, FunctionType::NATIVE, 0);
        return v;
    }
    if (strcmp(key, "clone") == 0) {
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)Request_clone_native, this, FunctionType::NATIVE, 0);
        return v;
    }
    return TsObject::GetPropertyVirtual(key);
}
