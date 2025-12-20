#pragma once

#include "TsString.h"
#include "TsObject.h"

class TsURL : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x55524C4C; // "URLL"
    static TsURL* Create(TsString* url, TsString* base = nullptr);

    TsString* GetHref();
    TsString* GetOrigin();
    TsString* GetProtocol();
    TsString* GetHost();
    TsString* GetHostname();
    TsString* GetPort();
    TsString* GetPathname();
    TsString* GetSearch();
    TsString* GetHash();

private:
    TsURL(TsString* url, TsString* base);
    uint32_t magic = MAGIC;
    TsString* href;
    TsString* protocol;
    TsString* host;
    TsString* hostname;
    TsString* port;
    TsString* pathname;
    TsString* search;
    TsString* hash;

    void Parse();
};

extern "C" {
    void* ts_url_create(void* vtable, void* url, void* base);
    void* ts_url_get_href(void* url);
    void* ts_url_get_origin(void* url);
    void* ts_url_get_protocol(void* url);
    void* ts_url_get_host(void* url);
    void* ts_url_get_hostname(void* url);
    void* ts_url_get_port(void* url);
    void* ts_url_get_pathname(void* url);
    void* ts_url_get_search(void* url);
    void* ts_url_get_hash(void* url);
}
