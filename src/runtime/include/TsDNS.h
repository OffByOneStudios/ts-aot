#pragma once
#include "TsObject.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include <uv.h>
#include <string>
#include <vector>

/*
 * TsDNS - Node.js dns module implementation
 *
 * Provides DNS resolution using libuv's uv_getaddrinfo and uv_getnameinfo.
 *
 * APIs implemented:
 *   - dns.lookup(hostname, callback) - Resolve hostname to IP address
 *   - dns.lookup(hostname, options, callback) - With options (family, hints, all)
 *   - dns.resolve(hostname, callback) - DNS A record lookup
 *   - dns.resolve4(hostname, callback) - DNS A record lookup (IPv4)
 *   - dns.resolve6(hostname, callback) - DNS AAAA record lookup (IPv6)
 *   - dns.reverse(ip, callback) - Reverse DNS lookup
 *   - dns.promises.lookup() - Promise-based lookup
 *   - dns.promises.resolve() - Promise-based resolve
 *   - dns.promises.resolve4() - Promise-based resolve4
 *   - dns.promises.resolve6() - Promise-based resolve6
 *   - dns.promises.reverse() - Promise-based reverse
 */

// DNS lookup options
struct DnsLookupOptions {
    int family = 0;        // 0 = any, 4 = IPv4, 6 = IPv6
    int hints = 0;         // AI_* flags
    bool all = false;      // Return all addresses (not just first)
    bool verbatim = false; // Return addresses in order from resolver
};

// DNS lookup result
struct DnsLookupResult {
    std::string address;
    int family = 4;        // 4 = IPv4, 6 = IPv6
};

// Context for async DNS operations
struct DnsLookupContext {
    void* callback = nullptr;     // JavaScript callback
    void* promise = nullptr;      // Promise for promise-based API
    DnsLookupOptions options;
    uv_getaddrinfo_t req;
};

struct DnsReverseContext {
    void* callback = nullptr;
    void* promise = nullptr;
    uv_getnameinfo_t req;
};

// C API for dns module
extern "C" {
    // dns.lookup(hostname, callback)
    // dns.lookup(hostname, options, callback)
    void ts_dns_lookup(void* hostname, void* optionsOrCallback, void* callbackOrNull);

    // dns.resolve(hostname, callback) - same as resolve4
    void ts_dns_resolve(void* hostname, void* callback);

    // dns.resolve4(hostname, callback)
    void ts_dns_resolve4(void* hostname, void* callback);

    // dns.resolve6(hostname, callback)
    void ts_dns_resolve6(void* hostname, void* callback);

    // dns.reverse(ip, callback)
    void ts_dns_reverse(void* ip, void* callback);

    // dns.getServers() - returns array of DNS server addresses
    void* ts_dns_get_servers();

    // dns.setServers(servers) - sets DNS servers (not fully implemented in libuv)
    void ts_dns_set_servers(void* servers);

    // Promise-based APIs
    void* ts_dns_promises_lookup(void* hostname, void* options);
    void* ts_dns_promises_resolve(void* hostname);
    void* ts_dns_promises_resolve4(void* hostname);
    void* ts_dns_promises_resolve6(void* hostname);
    void* ts_dns_promises_reverse(void* ip);

    // Error code constants
    int64_t ts_dns_NODATA();
    int64_t ts_dns_FORMERR();
    int64_t ts_dns_SERVFAIL();
    int64_t ts_dns_NOTFOUND();
    int64_t ts_dns_NOTIMP();
    int64_t ts_dns_REFUSED();
    int64_t ts_dns_BADQUERY();
    int64_t ts_dns_BADNAME();
    int64_t ts_dns_BADFAMILY();
    int64_t ts_dns_BADRESP();
    int64_t ts_dns_CONNREFUSED();
    int64_t ts_dns_TIMEOUT();
    int64_t ts_dns_EOF();
    int64_t ts_dns_FILE();
    int64_t ts_dns_NOMEM();
    int64_t ts_dns_DESTRUCTION();
    int64_t ts_dns_BADSTR();
    int64_t ts_dns_BADFLAGS();
    int64_t ts_dns_NONAME();
    int64_t ts_dns_BADHINTS();
    int64_t ts_dns_NOTINITIALIZED();
    int64_t ts_dns_LOADIPHLPAPI();
    int64_t ts_dns_ADDRGETNETWORKPARAMS();
    int64_t ts_dns_CANCELLED();
}

