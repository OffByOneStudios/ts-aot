#include "TsDNS.h"
#include "TsRuntime.h"
#include "TsPromise.h"
#include "TsArray.h"
#include "TsObject.h"
#include "GC.h"
#include <cstring>
#include <cstdio>  // For debug fprintf
#include <ares.h>
#include <ares_dns.h>
#include <ares_nameser.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

using namespace ts;

// Global c-ares channel (initialized once)
static ares_channel g_ares_channel = nullptr;
static bool g_ares_initialized = false;

// Initialize c-ares
static void ensure_ares_init() {
    if (g_ares_initialized) return;

    int status = ares_library_init(ARES_LIB_INIT_ALL);
    if (status != ARES_SUCCESS) {
        fprintf(stderr, "c-ares: ares_library_init failed: %s\n", ares_strerror(status));
        return;
    }

    struct ares_options options;
    memset(&options, 0, sizeof(options));
    options.timeout = 5000;  // 5 second timeout
    options.tries = 3;

    status = ares_init_options(&g_ares_channel, &options, ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES);
    if (status != ARES_SUCCESS) {
        fprintf(stderr, "c-ares: ares_init_options failed: %s\n", ares_strerror(status));
        ares_library_cleanup();
        return;
    }

    g_ares_initialized = true;
}

// Process c-ares socket events (integrate with libuv)
static void ares_process_wrapper() {
    if (!g_ares_channel) return;

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    int nfds = ares_fds(g_ares_channel, &read_fds, &write_fds);
    if (nfds == 0) return;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;  // 10ms
    select(nfds, &read_fds, &write_fds, nullptr, &tv);
    ares_process(g_ares_channel, &read_fds, &write_fds);
}

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

// C-ares callback for CNAME queries
static void on_cname_query(void* arg, int status, int timeouts, unsigned char* abuf, int alen) {
    DnsAresContext* ctx = (DnsAresContext*)arg;

    TsArray* results = TsArray::Create();

    if (status == ARES_SUCCESS) {
        struct hostent* host = nullptr;
        int parse_status = ares_parse_a_reply(abuf, alen, &host, nullptr, nullptr);

        // For CNAME, we need to check if there's an alias
        if (host && host->h_name) {
            ts_array_push(results, ts_value_make_string(TsString::Create(host->h_name)));
        }
        if (host) ares_free_hostent(host);
    }

    if (ctx->promise) {
        TsPromise* promise = (TsPromise*)ctx->promise;
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            TsValue* err = (TsValue*)make_error(ares_strerror(status));
            ts_promise_reject_internal(promise, err);
        } else {
            TsValue resultVal; resultVal.type = ValueType::ARRAY_PTR;
            resultVal.ptr_val = results;
            ts_promise_resolve_internal(promise, &resultVal);
        }
    } else if (ctx->callback) {
        void* args[2];
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            args[0] = make_error(ares_strerror(status));
            args[1] = ts_value_make_undefined();
        } else {
            args[0] = ts_value_make_null();
            args[1] = ts_value_make_array(results);
        }
        ts_function_call((TsValue*)ctx->callback, 2, (TsValue**)args);
    }
}

// C-ares callback for MX queries
static void on_mx_query(void* arg, int status, int timeouts, unsigned char* abuf, int alen) {
    DnsAresContext* ctx = (DnsAresContext*)arg;

    TsArray* results = TsArray::Create();

    if (status == ARES_SUCCESS) {
        struct ares_mx_reply* mx_reply = nullptr;
        int parse_status = ares_parse_mx_reply(abuf, alen, &mx_reply);

        if (parse_status == ARES_SUCCESS) {
            struct ares_mx_reply* mx = mx_reply;
            while (mx) {
                // Create {exchange, priority} object
                TsMap* entry = TsMap::Create();

                TsValue exchangeKey; exchangeKey.type = ValueType::STRING_PTR;
                exchangeKey.ptr_val = TsString::Create("exchange");
                TsValue exchangeVal; exchangeVal.type = ValueType::STRING_PTR;
                exchangeVal.ptr_val = TsString::Create(mx->host);
                entry->Set(exchangeKey, exchangeVal);

                TsValue priorityKey; priorityKey.type = ValueType::STRING_PTR;
                priorityKey.ptr_val = TsString::Create("priority");
                TsValue priorityVal; priorityVal.type = ValueType::NUMBER_INT;
                priorityVal.i_val = mx->priority;
                entry->Set(priorityKey, priorityVal);

                ts_array_push(results, ts_value_make_object(entry));
                mx = mx->next;
            }
            ares_free_data(mx_reply);
        }
    }

    if (ctx->promise) {
        TsPromise* promise = (TsPromise*)ctx->promise;
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            TsValue* err = (TsValue*)make_error(ares_strerror(status));
            ts_promise_reject_internal(promise, err);
        } else {
            TsValue resultVal; resultVal.type = ValueType::ARRAY_PTR;
            resultVal.ptr_val = results;
            ts_promise_resolve_internal(promise, &resultVal);
        }
    } else if (ctx->callback) {
        void* args[2];
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            args[0] = make_error(ares_strerror(status));
            args[1] = ts_value_make_undefined();
        } else {
            args[0] = ts_value_make_null();
            args[1] = ts_value_make_array(results);
        }
        ts_function_call((TsValue*)ctx->callback, 2, (TsValue**)args);
    }
}

// C-ares callback for NS queries
static void on_ns_query(void* arg, int status, int timeouts, unsigned char* abuf, int alen) {
    DnsAresContext* ctx = (DnsAresContext*)arg;

    TsArray* results = TsArray::Create();

    if (status == ARES_SUCCESS) {
        struct hostent* host = nullptr;
        int parse_status = ares_parse_ns_reply(abuf, alen, &host);

        if (parse_status == ARES_SUCCESS && host) {
            for (int i = 0; host->h_aliases && host->h_aliases[i]; i++) {
                ts_array_push(results, ts_value_make_string(TsString::Create(host->h_aliases[i])));
            }
            ares_free_hostent(host);
        }
    }

    if (ctx->promise) {
        TsPromise* promise = (TsPromise*)ctx->promise;
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            TsValue* err = (TsValue*)make_error(ares_strerror(status));
            ts_promise_reject_internal(promise, err);
        } else {
            TsValue resultVal; resultVal.type = ValueType::ARRAY_PTR;
            resultVal.ptr_val = results;
            ts_promise_resolve_internal(promise, &resultVal);
        }
    } else if (ctx->callback) {
        void* args[2];
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            args[0] = make_error(ares_strerror(status));
            args[1] = ts_value_make_undefined();
        } else {
            args[0] = ts_value_make_null();
            args[1] = ts_value_make_array(results);
        }
        ts_function_call((TsValue*)ctx->callback, 2, (TsValue**)args);
    }
}

// C-ares callback for TXT queries
static void on_txt_query(void* arg, int status, int timeouts, unsigned char* abuf, int alen) {
    DnsAresContext* ctx = (DnsAresContext*)arg;

    TsArray* results = TsArray::Create();

    if (status == ARES_SUCCESS) {
        struct ares_txt_reply* txt_reply = nullptr;
        int parse_status = ares_parse_txt_reply(abuf, alen, &txt_reply);

        if (parse_status == ARES_SUCCESS) {
            struct ares_txt_reply* txt = txt_reply;
            while (txt) {
                // Each TXT record is an array of strings (one entry)
                TsArray* txtArray = TsArray::Create();
                ts_array_push(txtArray, ts_value_make_string(TsString::Create((char*)txt->txt)));
                ts_array_push(results, ts_value_make_array(txtArray));
                txt = txt->next;
            }
            ares_free_data(txt_reply);
        }
    }

    if (ctx->promise) {
        TsPromise* promise = (TsPromise*)ctx->promise;
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            TsValue* err = (TsValue*)make_error(ares_strerror(status));
            ts_promise_reject_internal(promise, err);
        } else {
            TsValue resultVal; resultVal.type = ValueType::ARRAY_PTR;
            resultVal.ptr_val = results;
            ts_promise_resolve_internal(promise, &resultVal);
        }
    } else if (ctx->callback) {
        void* args[2];
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            args[0] = make_error(ares_strerror(status));
            args[1] = ts_value_make_undefined();
        } else {
            args[0] = ts_value_make_null();
            args[1] = ts_value_make_array(results);
        }
        ts_function_call((TsValue*)ctx->callback, 2, (TsValue**)args);
    }
}

// C-ares callback for SRV queries
static void on_srv_query(void* arg, int status, int timeouts, unsigned char* abuf, int alen) {
    DnsAresContext* ctx = (DnsAresContext*)arg;

    TsArray* results = TsArray::Create();

    if (status == ARES_SUCCESS) {
        struct ares_srv_reply* srv_reply = nullptr;
        int parse_status = ares_parse_srv_reply(abuf, alen, &srv_reply);

        if (parse_status == ARES_SUCCESS) {
            struct ares_srv_reply* srv = srv_reply;
            while (srv) {
                // Create {name, port, priority, weight} object
                TsMap* entry = TsMap::Create();

                TsValue nameKey; nameKey.type = ValueType::STRING_PTR;
                nameKey.ptr_val = TsString::Create("name");
                TsValue nameVal; nameVal.type = ValueType::STRING_PTR;
                nameVal.ptr_val = TsString::Create(srv->host);
                entry->Set(nameKey, nameVal);

                TsValue portKey; portKey.type = ValueType::STRING_PTR;
                portKey.ptr_val = TsString::Create("port");
                TsValue portVal; portVal.type = ValueType::NUMBER_INT;
                portVal.i_val = srv->port;
                entry->Set(portKey, portVal);

                TsValue priorityKey; priorityKey.type = ValueType::STRING_PTR;
                priorityKey.ptr_val = TsString::Create("priority");
                TsValue priorityVal; priorityVal.type = ValueType::NUMBER_INT;
                priorityVal.i_val = srv->priority;
                entry->Set(priorityKey, priorityVal);

                TsValue weightKey; weightKey.type = ValueType::STRING_PTR;
                weightKey.ptr_val = TsString::Create("weight");
                TsValue weightVal; weightVal.type = ValueType::NUMBER_INT;
                weightVal.i_val = srv->weight;
                entry->Set(weightKey, weightVal);

                ts_array_push(results, ts_value_make_object(entry));
                srv = srv->next;
            }
            ares_free_data(srv_reply);
        }
    }

    if (ctx->promise) {
        TsPromise* promise = (TsPromise*)ctx->promise;
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            TsValue* err = (TsValue*)make_error(ares_strerror(status));
            ts_promise_reject_internal(promise, err);
        } else {
            TsValue resultVal; resultVal.type = ValueType::ARRAY_PTR;
            resultVal.ptr_val = results;
            ts_promise_resolve_internal(promise, &resultVal);
        }
    } else if (ctx->callback) {
        void* args[2];
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            args[0] = make_error(ares_strerror(status));
            args[1] = ts_value_make_undefined();
        } else {
            args[0] = ts_value_make_null();
            args[1] = ts_value_make_array(results);
        }
        ts_function_call((TsValue*)ctx->callback, 2, (TsValue**)args);
    }
}

// C-ares callback for PTR queries
static void on_ptr_query(void* arg, int status, int timeouts, unsigned char* abuf, int alen) {
    DnsAresContext* ctx = (DnsAresContext*)arg;

    TsArray* results = TsArray::Create();

    if (status == ARES_SUCCESS) {
        struct hostent* host = nullptr;
        int parse_status = ares_parse_ptr_reply(abuf, alen, nullptr, 0, AF_INET, &host);

        if (parse_status == ARES_SUCCESS && host) {
            if (host->h_name) {
                ts_array_push(results, ts_value_make_string(TsString::Create(host->h_name)));
            }
            for (int i = 0; host->h_aliases && host->h_aliases[i]; i++) {
                ts_array_push(results, ts_value_make_string(TsString::Create(host->h_aliases[i])));
            }
            ares_free_hostent(host);
        }
    }

    if (ctx->promise) {
        TsPromise* promise = (TsPromise*)ctx->promise;
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            TsValue* err = (TsValue*)make_error(ares_strerror(status));
            ts_promise_reject_internal(promise, err);
        } else {
            TsValue resultVal; resultVal.type = ValueType::ARRAY_PTR;
            resultVal.ptr_val = results;
            ts_promise_resolve_internal(promise, &resultVal);
        }
    } else if (ctx->callback) {
        void* args[2];
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            args[0] = make_error(ares_strerror(status));
            args[1] = ts_value_make_undefined();
        } else {
            args[0] = ts_value_make_null();
            args[1] = ts_value_make_array(results);
        }
        ts_function_call((TsValue*)ctx->callback, 2, (TsValue**)args);
    }
}

// C-ares callback for NAPTR queries
static void on_naptr_query(void* arg, int status, int timeouts, unsigned char* abuf, int alen) {
    DnsAresContext* ctx = (DnsAresContext*)arg;

    TsArray* results = TsArray::Create();

    if (status == ARES_SUCCESS) {
        struct ares_naptr_reply* naptr_reply = nullptr;
        int parse_status = ares_parse_naptr_reply(abuf, alen, &naptr_reply);

        if (parse_status == ARES_SUCCESS) {
            struct ares_naptr_reply* naptr = naptr_reply;
            while (naptr) {
                TsMap* entry = TsMap::Create();

                TsValue flagsKey; flagsKey.type = ValueType::STRING_PTR;
                flagsKey.ptr_val = TsString::Create("flags");
                TsValue flagsVal; flagsVal.type = ValueType::STRING_PTR;
                flagsVal.ptr_val = TsString::Create((char*)naptr->flags);
                entry->Set(flagsKey, flagsVal);

                TsValue serviceKey; serviceKey.type = ValueType::STRING_PTR;
                serviceKey.ptr_val = TsString::Create("service");
                TsValue serviceVal; serviceVal.type = ValueType::STRING_PTR;
                serviceVal.ptr_val = TsString::Create((char*)naptr->service);
                entry->Set(serviceKey, serviceVal);

                TsValue regexpKey; regexpKey.type = ValueType::STRING_PTR;
                regexpKey.ptr_val = TsString::Create("regexp");
                TsValue regexpVal; regexpVal.type = ValueType::STRING_PTR;
                regexpVal.ptr_val = TsString::Create((char*)naptr->regexp);
                entry->Set(regexpKey, regexpVal);

                TsValue replacementKey; replacementKey.type = ValueType::STRING_PTR;
                replacementKey.ptr_val = TsString::Create("replacement");
                TsValue replacementVal; replacementVal.type = ValueType::STRING_PTR;
                replacementVal.ptr_val = TsString::Create(naptr->replacement);
                entry->Set(replacementKey, replacementVal);

                TsValue orderKey; orderKey.type = ValueType::STRING_PTR;
                orderKey.ptr_val = TsString::Create("order");
                TsValue orderVal; orderVal.type = ValueType::NUMBER_INT;
                orderVal.i_val = naptr->order;
                entry->Set(orderKey, orderVal);

                TsValue preferenceKey; preferenceKey.type = ValueType::STRING_PTR;
                preferenceKey.ptr_val = TsString::Create("preference");
                TsValue preferenceVal; preferenceVal.type = ValueType::NUMBER_INT;
                preferenceVal.i_val = naptr->preference;
                entry->Set(preferenceKey, preferenceVal);

                ts_array_push(results, ts_value_make_object(entry));
                naptr = naptr->next;
            }
            ares_free_data(naptr_reply);
        }
    }

    if (ctx->promise) {
        TsPromise* promise = (TsPromise*)ctx->promise;
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            TsValue* err = (TsValue*)make_error(ares_strerror(status));
            ts_promise_reject_internal(promise, err);
        } else {
            TsValue resultVal; resultVal.type = ValueType::ARRAY_PTR;
            resultVal.ptr_val = results;
            ts_promise_resolve_internal(promise, &resultVal);
        }
    } else if (ctx->callback) {
        void* args[2];
        if (status != ARES_SUCCESS && ts_array_length(results) == 0) {
            args[0] = make_error(ares_strerror(status));
            args[1] = ts_value_make_undefined();
        } else {
            args[0] = ts_value_make_null();
            args[1] = ts_value_make_array(results);
        }
        ts_function_call((TsValue*)ctx->callback, 2, (TsValue**)args);
    }
}

// C-ares callback for SOA queries
static void on_soa_query(void* arg, int status, int timeouts, unsigned char* abuf, int alen) {
    DnsAresContext* ctx = (DnsAresContext*)arg;

    TsMap* result = nullptr;

    if (status == ARES_SUCCESS) {
        struct ares_soa_reply* soa_reply = nullptr;
        int parse_status = ares_parse_soa_reply(abuf, alen, &soa_reply);

        if (parse_status == ARES_SUCCESS && soa_reply) {
            result = TsMap::Create();

            TsValue nsKey; nsKey.type = ValueType::STRING_PTR;
            nsKey.ptr_val = TsString::Create("nsname");
            TsValue nsVal; nsVal.type = ValueType::STRING_PTR;
            nsVal.ptr_val = TsString::Create(soa_reply->nsname);
            result->Set(nsKey, nsVal);

            TsValue hostKey; hostKey.type = ValueType::STRING_PTR;
            hostKey.ptr_val = TsString::Create("hostmaster");
            TsValue hostVal; hostVal.type = ValueType::STRING_PTR;
            hostVal.ptr_val = TsString::Create(soa_reply->hostmaster);
            result->Set(hostKey, hostVal);

            TsValue serialKey; serialKey.type = ValueType::STRING_PTR;
            serialKey.ptr_val = TsString::Create("serial");
            TsValue serialVal; serialVal.type = ValueType::NUMBER_INT;
            serialVal.i_val = soa_reply->serial;
            result->Set(serialKey, serialVal);

            TsValue refreshKey; refreshKey.type = ValueType::STRING_PTR;
            refreshKey.ptr_val = TsString::Create("refresh");
            TsValue refreshVal; refreshVal.type = ValueType::NUMBER_INT;
            refreshVal.i_val = soa_reply->refresh;
            result->Set(refreshKey, refreshVal);

            TsValue retryKey; retryKey.type = ValueType::STRING_PTR;
            retryKey.ptr_val = TsString::Create("retry");
            TsValue retryVal; retryVal.type = ValueType::NUMBER_INT;
            retryVal.i_val = soa_reply->retry;
            result->Set(retryKey, retryVal);

            TsValue expireKey; expireKey.type = ValueType::STRING_PTR;
            expireKey.ptr_val = TsString::Create("expire");
            TsValue expireVal; expireVal.type = ValueType::NUMBER_INT;
            expireVal.i_val = soa_reply->expire;
            result->Set(expireKey, expireVal);

            TsValue minttlKey; minttlKey.type = ValueType::STRING_PTR;
            minttlKey.ptr_val = TsString::Create("minttl");
            TsValue minttlVal; minttlVal.type = ValueType::NUMBER_INT;
            minttlVal.i_val = soa_reply->minttl;
            result->Set(minttlKey, minttlVal);

            ares_free_data(soa_reply);
        }
    }

    if (ctx->promise) {
        TsPromise* promise = (TsPromise*)ctx->promise;
        if (status != ARES_SUCCESS || !result) {
            TsValue* err = (TsValue*)make_error(ares_strerror(status));
            ts_promise_reject_internal(promise, err);
        } else {
            TsValue resultVal; resultVal.type = ValueType::OBJECT_PTR;
            resultVal.ptr_val = result;
            ts_promise_resolve_internal(promise, &resultVal);
        }
    } else if (ctx->callback) {
        void* args[2];
        if (status != ARES_SUCCESS || !result) {
            args[0] = make_error(ares_strerror(status));
            args[1] = ts_value_make_undefined();
        } else {
            args[0] = ts_value_make_null();
            args[1] = ts_value_make_object(result);
        }
        ts_function_call((TsValue*)ctx->callback, 2, (TsValue**)args);
    }
}

// Helper to run c-ares query with libuv integration
static void run_ares_query(DnsAresContext* ctx, const char* hostname, int dnsclass, int type, ares_callback callback) {
    ensure_ares_init();
    if (!g_ares_channel) {
        void* args[2];
        args[0] = make_error("c-ares not initialized");
        args[1] = ts_value_make_undefined();
        if (ctx->callback) {
            ts_function_call((TsValue*)ctx->callback, 2, (TsValue**)args);
        } else if (ctx->promise) {
            TsValue* err = (TsValue*)args[0];
            ts_promise_reject_internal((TsPromise*)ctx->promise, err);
        }
        return;
    }

    ares_query(g_ares_channel, hostname, dnsclass, type, callback, ctx);

    // Process immediately and set up timer for follow-up
    ares_process_wrapper();
}

extern "C" {

// Initialize c-ares
void ts_dns_init() {
    ensure_ares_init();
}

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

// dns.resolveCname(hostname, callback)
void ts_dns_resolve_cname(void* hostname, void* callback) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) return;

    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->callback = callback;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_CNAME, on_cname_query);
}

// dns.resolveMx(hostname, callback)
void ts_dns_resolve_mx(void* hostname, void* callback) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) return;

    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->callback = callback;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_MX, on_mx_query);
}

// dns.resolveNs(hostname, callback)
void ts_dns_resolve_ns(void* hostname, void* callback) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) return;

    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->callback = callback;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_NS, on_ns_query);
}

// dns.resolveTxt(hostname, callback)
void ts_dns_resolve_txt(void* hostname, void* callback) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) return;

    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->callback = callback;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_TXT, on_txt_query);
}

// dns.resolveSrv(hostname, callback)
void ts_dns_resolve_srv(void* hostname, void* callback) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) return;

    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->callback = callback;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_SRV, on_srv_query);
}

// dns.resolvePtr(hostname, callback)
void ts_dns_resolve_ptr(void* hostname, void* callback) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) return;

    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->callback = callback;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_PTR, on_ptr_query);
}

// dns.resolveNaptr(hostname, callback)
void ts_dns_resolve_naptr(void* hostname, void* callback) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) return;

    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->callback = callback;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_NAPTR, on_naptr_query);
}

// dns.resolveSoa(hostname, callback)
void ts_dns_resolve_soa(void* hostname, void* callback) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) return;

    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->callback = callback;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_SOA, on_soa_query);
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

// Promise-based versions of the new resolve methods

void* ts_dns_promises_resolve_cname(void* hostname) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();
    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->promise = promise;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_CNAME, on_cname_query);
    return ts_value_make_object(promise);
}

void* ts_dns_promises_resolve_mx(void* hostname) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();
    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->promise = promise;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_MX, on_mx_query);
    return ts_value_make_object(promise);
}

void* ts_dns_promises_resolve_ns(void* hostname) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();
    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->promise = promise;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_NS, on_ns_query);
    return ts_value_make_object(promise);
}

void* ts_dns_promises_resolve_txt(void* hostname) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();
    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->promise = promise;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_TXT, on_txt_query);
    return ts_value_make_object(promise);
}

void* ts_dns_promises_resolve_srv(void* hostname) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();
    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->promise = promise;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_SRV, on_srv_query);
    return ts_value_make_object(promise);
}

void* ts_dns_promises_resolve_ptr(void* hostname) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();
    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->promise = promise;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_PTR, on_ptr_query);
    return ts_value_make_object(promise);
}

void* ts_dns_promises_resolve_naptr(void* hostname) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();
    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->promise = promise;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_NAPTR, on_naptr_query);
    return ts_value_make_object(promise);
}

void* ts_dns_promises_resolve_soa(void* hostname) {
    TsString* hostStr = (TsString*)ts_value_get_string((TsValue*)hostname);
    if (!hostStr) {
        TsPromise* promise = ts_promise_create();
        TsValue* err = (TsValue*)make_error("hostname must be a string");
        ts_promise_reject_internal(promise, err);
        return ts_value_make_object(promise);
    }

    TsPromise* promise = ts_promise_create();
    DnsAresContext* ctx = (DnsAresContext*)ts_alloc(sizeof(DnsAresContext));
    new (ctx) DnsAresContext();
    ctx->promise = promise;
    ctx->hostname = hostStr->ToUtf8();

    run_ares_query(ctx, ctx->hostname.c_str(), C_IN, T_SOA, on_soa_query);
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
