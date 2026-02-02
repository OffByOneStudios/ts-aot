#ifndef TS_DNS_EXT_H
#define TS_DNS_EXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize c-ares (called once at startup)
void ts_dns_init();

// dns.lookup(hostname, callback)
// dns.lookup(hostname, options, callback)
void ts_dns_lookup(void* hostname, void* optionsOrCallback, void* callbackOrNull);

// dns.resolve(hostname, callback) - same as resolve4
void ts_dns_resolve(void* hostname, void* callback);

// dns.resolve4(hostname, callback)
void ts_dns_resolve4(void* hostname, void* callback);

// dns.resolve6(hostname, callback)
void ts_dns_resolve6(void* hostname, void* callback);

// dns.resolveCname(hostname, callback)
void ts_dns_resolve_cname(void* hostname, void* callback);

// dns.resolveMx(hostname, callback)
void ts_dns_resolve_mx(void* hostname, void* callback);

// dns.resolveNs(hostname, callback)
void ts_dns_resolve_ns(void* hostname, void* callback);

// dns.resolveTxt(hostname, callback)
void ts_dns_resolve_txt(void* hostname, void* callback);

// dns.resolveSrv(hostname, callback)
void ts_dns_resolve_srv(void* hostname, void* callback);

// dns.resolvePtr(hostname, callback)
void ts_dns_resolve_ptr(void* hostname, void* callback);

// dns.resolveNaptr(hostname, callback)
void ts_dns_resolve_naptr(void* hostname, void* callback);

// dns.resolveSoa(hostname, callback)
void ts_dns_resolve_soa(void* hostname, void* callback);

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
void* ts_dns_promises_resolve_cname(void* hostname);
void* ts_dns_promises_resolve_mx(void* hostname);
void* ts_dns_promises_resolve_ns(void* hostname);
void* ts_dns_promises_resolve_txt(void* hostname);
void* ts_dns_promises_resolve_srv(void* hostname);
void* ts_dns_promises_resolve_ptr(void* hostname);
void* ts_dns_promises_resolve_naptr(void* hostname);
void* ts_dns_promises_resolve_soa(void* hostname);
void* ts_dns_promises_reverse(void* ip);
void* ts_dns_promises_get_servers();
void* ts_dns_promises_set_servers(void* servers);

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

#ifdef __cplusplus
}
#endif

#endif // TS_DNS_EXT_H
