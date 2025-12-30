#pragma once

#include "TsString.h"
#include "TsObject.h"

class TsURLSearchParams;  // Forward declaration

class TsURL : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x55524C4C; // "URLL"
    static TsURL* Create(TsString* url, TsString* base = nullptr);

    // Getters
    TsString* GetHref();
    TsString* GetOrigin();
    TsString* GetProtocol();
    TsString* GetHost();
    TsString* GetHostname();
    TsString* GetPort();
    TsString* GetPathname();
    TsString* GetSearch();
    TsString* GetHash();
    TsString* GetUsername();
    TsString* GetPassword();
    TsURLSearchParams* GetSearchParams();
    TsString* ToString();
    TsString* ToJSON();

    // Setters
    void SetHref(TsString* value);
    void SetProtocol(TsString* value);
    void SetHost(TsString* value);
    void SetHostname(TsString* value);
    void SetPort(TsString* value);
    void SetPathname(TsString* value);
    void SetSearch(TsString* value);
    void SetHash(TsString* value);
    void SetUsername(TsString* value);
    void SetPassword(TsString* value);

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
    TsString* username;
    TsString* password;
    TsURLSearchParams* searchParams;

    void Parse();
    void UpdateHref();
};

class TsURLSearchParams : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x55524C50; // "URLP"
    static TsURLSearchParams* Create(TsString* query = nullptr);
    
    void Append(TsString* name, TsString* value);
    void Delete(TsString* name);
    TsString* Get(TsString* name);
    void* GetAll(TsString* name);  // Returns TsArray*
    bool Has(TsString* name);
    void Set(TsString* name, TsString* value);
    void Sort();
    TsString* ToString();
    size_t GetSize();

private:
    TsURLSearchParams(TsString* query);
    uint32_t magic = MAGIC;
    void* entries;  // TsArray of {name, value} pairs
    void Parse(TsString* query);
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
    void* ts_url_get_username(void* url);
    void* ts_url_get_password(void* url);
    void* ts_url_get_search_params(void* url);
    void* ts_url_to_string(void* url);
    void* ts_url_to_json(void* url);
    
    void ts_url_set_href(void* url, void* value);
    void ts_url_set_protocol(void* url, void* value);
    void ts_url_set_host(void* url, void* value);
    void ts_url_set_hostname(void* url, void* value);
    void ts_url_set_port(void* url, void* value);
    void ts_url_set_pathname(void* url, void* value);
    void ts_url_set_search(void* url, void* value);
    void ts_url_set_hash(void* url, void* value);
    
    // URLSearchParams
    void* ts_url_search_params_create(void* query);
    void ts_url_search_params_append(void* params, void* name, void* value);
    void ts_url_search_params_delete(void* params, void* name);
    void* ts_url_search_params_get(void* params, void* name);
    void* ts_url_search_params_get_all(void* params, void* name);
    bool ts_url_search_params_has(void* params, void* name);
    void ts_url_search_params_set(void* params, void* name, void* value);
    void ts_url_search_params_sort(void* params);
    void* ts_url_search_params_to_string(void* params);
    int64_t ts_url_search_params_size(void* params);
}
