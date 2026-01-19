#include "TsDNS.h"
#include "TsRuntime.h"
#include "TsPromise.h"
#include "TsArray.h"
#include "TsObject.h"
#include "GC.h"
#include <cstring>
#include <cstdio>  // For debug fprintf

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

using namespace ts;

// Helper to create error from string
static void* make_error(const char* msg) {
    return ts_error_create(ts_value_make_string(TsString::Create(msg)));
}

// Helper to convert addrinfo to IP string
static std::string addrinfo_to_string(struct addrinfo* ai, int* outFamily) {
    char buf[INET6_ADDRSTRLEN];

    if (ai->ai_family == AF_INET) {
        struct sockaddr_in* addr = (struct sockaddr_in*)ai->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
        if (outFamily) *outFamily = 4;
    } else if (ai->ai_family == AF_INET6) {
        struct sockaddr_in6* addr = (struct sockaddr_in6*)ai->ai_addr;
        inet_ntop(AF_INET6, &addr->sin6_addr, buf, sizeof(buf));
        if (outFamily) *outFamily = 6;
    } else {
        return "";
    }

    return std::string(buf);
}

// Callback for uv_getaddrinfo (lookup)
static void on_dns_lookup(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    DnsLookupContext* ctx = (DnsLookupContext*)req->data;

    if (ctx->promise) {
        TsPromise* promise = (TsPromise*)ctx->promise;

        if (status < 0) {
            // Create error
            std::string errMsg = "getaddrinfo ";
            errMsg += uv_err_name(status);
            errMsg += ": ";
            errMsg += uv_strerror(status);
            TsValue* err = (TsValue*)make_error(errMsg.c_str());
            ts_promise_reject_internal(promise, err);
        } else if (res) {
            if (ctx->options.all) {
                // Return array of all addresses
                TsArray* results = TsArray::Create();
                struct addrinfo* curr = res;
                while (curr) {
                    int family = 4;
                    std::string addr = addrinfo_to_string(curr, &family);
                    if (!addr.empty()) {
                        // Create result object {address, family}
                        TsMap* resultObj = TsMap::Create();

                        TsValue addrKey; addrKey.type = ValueType::STRING_PTR;
                        addrKey.ptr_val = TsString::Create("address");
                        TsValue addrVal; addrVal.type = ValueType::STRING_PTR;
                        addrVal.ptr_val = TsString::Create(addr.c_str());
                        resultObj->Set(addrKey, addrVal);

                        TsValue famKey; famKey.type = ValueType::STRING_PTR;
                        famKey.ptr_val = TsString::Create("family");
                        TsValue famVal; famVal.type = ValueType::NUMBER_INT;
                        famVal.i_val = family;
                        resultObj->Set(famKey, famVal);

                        ts_array_push(results, ts_value_make_object(resultObj));
                    }
                    curr = curr->ai_next;
                }
                TsValue resultVal; resultVal.type = ValueType::OBJECT_PTR;
                resultVal.ptr_val = results;
                ts_promise_resolve_internal(promise, &resultVal);
            } else {
                // Return first result as object {address, family}
                int family = 4;
                std::string addr = addrinfo_to_string(res, &family);

                TsMap* result = TsMap::Create();

                TsValue addrKey; addrKey.type = ValueType::STRING_PTR;
                addrKey.ptr_val = TsString::Create("address");
                TsValue addrVal; addrVal.type = ValueType::STRING_PTR;
                addrVal.ptr_val = TsString::Create(addr.c_str());
                result->Set(addrKey, addrVal);

                TsValue famKey; famKey.type = ValueType::STRING_PTR;
                famKey.ptr_val = TsString::Create("family");
                TsValue famVal; famVal.type = ValueType::NUMBER_INT;
                famVal.i_val = family;
                result->Set(famKey, famVal);

                TsValue resultVal; resultVal.type = ValueType::OBJECT_PTR;
                resultVal.ptr_val = result;
                ts_promise_resolve_internal(promise, &resultVal);
            }
        } else {
            TsValue* err = (TsValue*)make_error("getaddrinfo: no addresses found");
            ts_promise_reject_internal(promise, err);
        }
    } else if (ctx->callback) {
        // Callback-based API: callback(err, address, family)
        void* args[3];

        if (status < 0) {
            std::string errMsg = "getaddrinfo ";
            errMsg += uv_err_name(status);
            errMsg += ": ";
            errMsg += uv_strerror(status);
            args[0] = make_error(errMsg.c_str());
            args[1] = ts_value_make_undefined();
            args[2] = ts_value_make_undefined();
        } else if (res) {
            int family = 4;
            std::string addr = addrinfo_to_string(res, &family);

            args[0] = ts_value_make_null();
            args[1] = ts_value_make_string(TsString::Create(addr.c_str()));
            args[2] = ts_value_make_int(family);
        } else {
            args[0] = make_error("getaddrinfo: no addresses found");
            args[1] = ts_value_make_undefined();
            args[2] = ts_value_make_undefined();
        }

        ts_function_call((TsValue*)ctx->callback, 3, (TsValue**)args);
    }

    if (res) {
        uv_freeaddrinfo(res);
    }
}

// Callback for uv_getnameinfo (reverse lookup)
static void on_dns_reverse(uv_getnameinfo_t* req, int status, const char* hostname, const char* service) {
    DnsReverseContext* ctx = (DnsReverseContext*)req->data;

    if (ctx->promise) {
        TsPromise* promise = (TsPromise*)ctx->promise;

        if (status < 0) {
            std::string errMsg = "getnameinfo ";
            errMsg += uv_err_name(status);
            errMsg += ": ";
            errMsg += uv_strerror(status);
            TsValue* err = (TsValue*)make_error(errMsg.c_str());
            ts_promise_reject_internal(promise, err);
        } else if (hostname) {
            // Return array with single hostname (Node.js returns array)
            TsArray* results = TsArray::Create();
            ts_array_push(results, ts_value_make_string(TsString::Create(hostname)));
            TsValue resultVal; resultVal.type = ValueType::OBJECT_PTR;
            resultVal.ptr_val = results;
            ts_promise_resolve_internal(promise, &resultVal);
        } else {
            TsValue* err = (TsValue*)make_error("getnameinfo: no hostname found");
            ts_promise_reject_internal(promise, err);
        }
    } else if (ctx->callback) {
        void* args[2];

        if (status < 0) {
            std::string errMsg = "getnameinfo ";
            errMsg += uv_err_name(status);
            errMsg += ": ";
            errMsg += uv_strerror(status);
            args[0] = make_error(errMsg.c_str());
            args[1] = ts_value_make_undefined();
        } else if (hostname) {
            TsArray* results = TsArray::Create();
            ts_array_push(results, ts_value_make_string(TsString::Create(hostname)));

            args[0] = ts_value_make_null();
            args[1] = ts_value_make_array(results);
        } else {
            args[0] = make_error("getnameinfo: no hostname found");
            args[1] = ts_value_make_undefined();
        }

        ts_function_call((TsValue*)ctx->callback, 2, (TsValue**)args);
    }
}

// Parse lookup options from JS object
static DnsLookupOptions parse_lookup_options(void* optionsPtr) {
    DnsLookupOptions opts;

    if (!optionsPtr) return opts;

    void* rawPtr = ts_value_get_object((TsValue*)optionsPtr);
    if (!rawPtr) rawPtr = optionsPtr;

    TsMap* options = dynamic_cast<TsMap*>((TsObject*)rawPtr);
    if (!options) return opts;

    // Check 'family' option
    TsValue familyKey; familyKey.type = ValueType::STRING_PTR;
    familyKey.ptr_val = TsString::Create("family");
    TsValue familyVal = options->Get(familyKey);
    if (familyVal.type == ValueType::NUMBER_INT) {
        opts.family = (int)familyVal.i_val;
    }

    // Check 'all' option
    TsValue allKey; allKey.type = ValueType::STRING_PTR;
    allKey.ptr_val = TsString::Create("all");
    TsValue allVal = options->Get(allKey);
    if (allVal.type == ValueType::BOOLEAN) {
        opts.all = allVal.b_val;
    }

    // Check 'verbatim' option
    TsValue verbatimKey; verbatimKey.type = ValueType::STRING_PTR;
    verbatimKey.ptr_val = TsString::Create("verbatim");
    TsValue verbatimVal = options->Get(verbatimKey);
    if (verbatimVal.type == ValueType::BOOLEAN) {
        opts.verbatim = verbatimVal.b_val;
    }

    return opts;
}

extern "C" {

// dns.lookup(hostname, [options], callback)
void ts_dns_lookup(void* hostname, void* optionsOrCallback, void* callbackOrNull) {
    // DEBUG("ts_dns_lookup called");

    // Get hostname string - use ts_value_get_string for proper string unboxing
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        // ERROR("ts_dns_lookup: hostname is not a string");
        return;
    }

    // Determine if second arg is options or callback
    DnsLookupOptions opts;
    void* callback = nullptr;

    if (callbackOrNull) {
        // Three args: hostname, options, callback
        opts = parse_lookup_options(optionsOrCallback);
        callback = callbackOrNull;
    } else {
        // Two args: hostname, callback
        callback = optionsOrCallback;
    }

    // Create context
    DnsLookupContext* ctx = (DnsLookupContext*)ts_alloc(sizeof(DnsLookupContext));
    new (ctx) DnsLookupContext();
    ctx->callback = callback;
    ctx->options = opts;
    ctx->req.data = ctx;

    // Setup hints based on options
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    if (opts.family == 4) {
        hints.ai_family = AF_INET;
    } else if (opts.family == 6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai_family = AF_UNSPEC;
    }

    // Start async lookup
    int r = uv_getaddrinfo(
        uv_default_loop(),
        &ctx->req,
        on_dns_lookup,
        hostStr->ToUtf8(),
        nullptr,
        &hints
    );

    if (r < 0) {
        // Immediate error
        void* args[3];
        std::string errMsg = "getaddrinfo ";
        errMsg += uv_err_name(r);
        errMsg += ": ";
        errMsg += uv_strerror(r);
        args[0] = make_error(errMsg.c_str());
        args[1] = ts_value_make_undefined();
        args[2] = ts_value_make_undefined();
        ts_function_call((TsValue*)callback, 3, (TsValue**)args);
    }
}

// dns.resolve(hostname, callback) - resolve A records
void ts_dns_resolve(void* hostname, void* callback) {
    ts_dns_resolve4(hostname, callback);
}

// dns.resolve4(hostname, callback)
void ts_dns_resolve4(void* hostname, void* callback) {
    // DEBUG("ts_dns_resolve4 called");

    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        // ERROR("ts_dns_resolve4: hostname is not a string");
        return;
    }

    DnsLookupContext* ctx = (DnsLookupContext*)ts_alloc(sizeof(DnsLookupContext));
    new (ctx) DnsLookupContext();
    ctx->callback = callback;
    ctx->options.family = 4;
    ctx->options.all = true;  // resolve returns all addresses
    ctx->req.data = ctx;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int r = uv_getaddrinfo(
        uv_default_loop(),
        &ctx->req,
        on_dns_lookup,
        hostStr->ToUtf8(),
        nullptr,
        &hints
    );

    if (r < 0) {
        void* args[2];
        std::string errMsg = "getaddrinfo ";
        errMsg += uv_err_name(r);
        args[0] = make_error(errMsg.c_str());
        args[1] = ts_value_make_undefined();
        ts_function_call((TsValue*)callback, 2, (TsValue**)args);
    }
}

// dns.resolve6(hostname, callback)
void ts_dns_resolve6(void* hostname, void* callback) {
    // DEBUG("ts_dns_resolve6 called");

    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        // ERROR("ts_dns_resolve6: hostname is not a string");
        return;
    }

    DnsLookupContext* ctx = (DnsLookupContext*)ts_alloc(sizeof(DnsLookupContext));
    new (ctx) DnsLookupContext();
    ctx->callback = callback;
    ctx->options.family = 6;
    ctx->options.all = true;
    ctx->req.data = ctx;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    int r = uv_getaddrinfo(
        uv_default_loop(),
        &ctx->req,
        on_dns_lookup,
        hostStr->ToUtf8(),
        nullptr,
        &hints
    );

    if (r < 0) {
        void* args[2];
        std::string errMsg = "getaddrinfo ";
        errMsg += uv_err_name(r);
        args[0] = make_error(errMsg.c_str());
        args[1] = ts_value_make_undefined();
        ts_function_call((TsValue*)callback, 2, (TsValue**)args);
    }
}

// dns.reverse(ip, callback)
void ts_dns_reverse(void* ip, void* callback) {
    // DEBUG("ts_dns_reverse called");

    TsString* ipStr = (TsString*)ts_value_get_string((TsValue*)ip);
    if (!ipStr) {
        // ERROR("ts_dns_reverse: ip is not a string");
        return;
    }

    const char* ipCStr = ipStr->ToUtf8();

    // Parse IP address
    struct sockaddr_storage addr;
    memset(&addr, 0, sizeof(addr));

    // Try IPv4 first
    struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
    if (inet_pton(AF_INET, ipCStr, &addr4->sin_addr) == 1) {
        addr4->sin_family = AF_INET;
    } else {
        // Try IPv6
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
        if (inet_pton(AF_INET6, ipCStr, &addr6->sin6_addr) == 1) {
            addr6->sin6_family = AF_INET6;
        } else {
            // Invalid IP address
            void* args[2];
            args[0] = make_error("Invalid IP address");
            args[1] = ts_value_make_undefined();
            ts_function_call((TsValue*)callback, 2, (TsValue**)args);
            return;
        }
    }

    DnsReverseContext* ctx = (DnsReverseContext*)ts_alloc(sizeof(DnsReverseContext));
    new (ctx) DnsReverseContext();
    ctx->callback = callback;
    ctx->req.data = ctx;

    int r = uv_getnameinfo(
        uv_default_loop(),
        &ctx->req,
        on_dns_reverse,
        (struct sockaddr*)&addr,
        NI_NAMEREQD
    );

    if (r < 0) {
        void* args[2];
        std::string errMsg = "getnameinfo ";
        errMsg += uv_err_name(r);
        args[0] = make_error(errMsg.c_str());
        args[1] = ts_value_make_undefined();
        ts_function_call((TsValue*)callback, 2, (TsValue**)args);
    }
}

// dns.getServers() - returns array of DNS server addresses
void* ts_dns_get_servers() {
    // libuv doesn't expose DNS server list directly
    // Return empty array for now
    TsArray* servers = TsArray::Create();
    return ts_value_make_array(servers);
}

// dns.setServers(servers) - not fully supported by libuv
void ts_dns_set_servers(void* servers) {
    // libuv uses system resolver, can't easily change DNS servers
    // WARN("dns.setServers() is not supported - using system resolver");
}

// Promise-based APIs

void* ts_dns_promises_lookup(void* hostname, void* options) {
    // DEBUG("ts_dns_promises_lookup called");

    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        // ERROR("ts_dns_promises_lookup: hostname is not a string");
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();

    DnsLookupContext* ctx = (DnsLookupContext*)ts_alloc(sizeof(DnsLookupContext));
    new (ctx) DnsLookupContext();
    ctx->promise = promise;
    ctx->options = parse_lookup_options(options);
    ctx->req.data = ctx;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    if (ctx->options.family == 4) {
        hints.ai_family = AF_INET;
    } else if (ctx->options.family == 6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai_family = AF_UNSPEC;
    }

    int r = uv_getaddrinfo(
        uv_default_loop(),
        &ctx->req,
        on_dns_lookup,
        hostStr->ToUtf8(),
        nullptr,
        &hints
    );

    if (r < 0) {
        std::string errMsg = "getaddrinfo ";
        errMsg += uv_err_name(r);
        errMsg += ": ";
        errMsg += uv_strerror(r);
        TsValue* err = (TsValue*)make_error(errMsg.c_str());
        ts_promise_reject_internal(promise, err);
    }

    return ts_value_make_object(promise);
}

void* ts_dns_promises_resolve(void* hostname) {
    return ts_dns_promises_resolve4(hostname);
}

void* ts_dns_promises_resolve4(void* hostname) {
    // DEBUG("ts_dns_promises_resolve4 called");

    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();

    DnsLookupContext* ctx = (DnsLookupContext*)ts_alloc(sizeof(DnsLookupContext));
    new (ctx) DnsLookupContext();
    ctx->promise = promise;
    ctx->options.family = 4;
    ctx->options.all = true;
    ctx->req.data = ctx;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int r = uv_getaddrinfo(
        uv_default_loop(),
        &ctx->req,
        on_dns_lookup,
        hostStr->ToUtf8(),
        nullptr,
        &hints
    );

    if (r < 0) {
        std::string errMsg = "getaddrinfo ";
        errMsg += uv_err_name(r);
        TsValue* err = (TsValue*)make_error(errMsg.c_str());
        ts_promise_reject_internal(promise, err);
    }

    return ts_value_make_object(promise);
}

void* ts_dns_promises_resolve6(void* hostname) {
    // DEBUG("ts_dns_promises_resolve6 called");

    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();

    DnsLookupContext* ctx = (DnsLookupContext*)ts_alloc(sizeof(DnsLookupContext));
    new (ctx) DnsLookupContext();
    ctx->promise = promise;
    ctx->options.family = 6;
    ctx->options.all = true;
    ctx->req.data = ctx;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    int r = uv_getaddrinfo(
        uv_default_loop(),
        &ctx->req,
        on_dns_lookup,
        hostStr->ToUtf8(),
        nullptr,
        &hints
    );

    if (r < 0) {
        std::string errMsg = "getaddrinfo ";
        errMsg += uv_err_name(r);
        TsValue* err = (TsValue*)make_error(errMsg.c_str());
        ts_promise_reject_internal(promise, err);
    }

    return ts_value_make_object(promise);
}

void* ts_dns_promises_reverse(void* ip) {
    // DEBUG("ts_dns_promises_reverse called");

    TsString* ipStr = (TsString*)ts_value_get_string((TsValue*)ip);
    if (!ipStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("ip must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    const char* ipCStr = ipStr->ToUtf8();

    struct sockaddr_storage addr;
    memset(&addr, 0, sizeof(addr));

    struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
    if (inet_pton(AF_INET, ipCStr, &addr4->sin_addr) == 1) {
        addr4->sin_family = AF_INET;
    } else {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&addr;
        if (inet_pton(AF_INET6, ipCStr, &addr6->sin6_addr) == 1) {
            addr6->sin6_family = AF_INET6;
        } else {
            TsPromise* promise = ts_promise_create();
            TsValue* err = (TsValue*)make_error("Invalid IP address");
            ts_promise_reject_internal(promise, err);
            return ts_value_make_object(promise);
        }
    }

    TsPromise* promise = ts_promise_create();

    DnsReverseContext* ctx = (DnsReverseContext*)ts_alloc(sizeof(DnsReverseContext));
    new (ctx) DnsReverseContext();
    ctx->promise = promise;
    ctx->req.data = ctx;

    int r = uv_getnameinfo(
        uv_default_loop(),
        &ctx->req,
        on_dns_reverse,
        (struct sockaddr*)&addr,
        NI_NAMEREQD
    );

    if (r < 0) {
        std::string errMsg = "getnameinfo ";
        errMsg += uv_err_name(r);
        TsValue* err = (TsValue*)make_error(errMsg.c_str());
        ts_promise_reject_internal(promise, err);
    }

    return ts_value_make_object(promise);
}

// DNS error constants (using custom codes similar to c-ares)
int64_t ts_dns_NODATA() { return 1; }
int64_t ts_dns_FORMERR() { return 2; }
int64_t ts_dns_SERVFAIL() { return 3; }
int64_t ts_dns_NOTFOUND() { return 4; }
int64_t ts_dns_NOTIMP() { return 5; }
int64_t ts_dns_REFUSED() { return 6; }
int64_t ts_dns_BADQUERY() { return 7; }
int64_t ts_dns_BADNAME() { return 8; }
int64_t ts_dns_BADFAMILY() { return 9; }
int64_t ts_dns_BADRESP() { return 10; }
int64_t ts_dns_CONNREFUSED() { return 11; }
int64_t ts_dns_TIMEOUT() { return 12; }
int64_t ts_dns_EOF() { return 13; }
int64_t ts_dns_FILE() { return 14; }
int64_t ts_dns_NOMEM() { return 15; }
int64_t ts_dns_DESTRUCTION() { return 16; }
int64_t ts_dns_BADSTR() { return 17; }
int64_t ts_dns_BADFLAGS() { return 18; }
int64_t ts_dns_NONAME() { return 19; }
int64_t ts_dns_BADHINTS() { return 20; }
int64_t ts_dns_NOTINITIALIZED() { return 21; }
int64_t ts_dns_LOADIPHLPAPI() { return 22; }
int64_t ts_dns_ADDRGETNETWORKPARAMS() { return 23; }
int64_t ts_dns_CANCELLED() { return 24; }

} // extern "C"
