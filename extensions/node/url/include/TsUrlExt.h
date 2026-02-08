#ifndef TS_URL_EXT_H
#define TS_URL_EXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// URL class
void* ts_url_create(void* url, void* base);
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

// URL VTable methods
void* URL_get_href(void* url);
void* URL_get_origin(void* url);
void* URL_get_protocol(void* url);
void* URL_get_host(void* url);
void* URL_get_hostname(void* url);
void* URL_get_port(void* url);
void* URL_get_pathname(void* url);
void* URL_get_search(void* url);
void* URL_get_hash(void* url);
void* URL_get_username(void* url);
void* URL_get_password(void* url);
void* URL_get_searchParams(void* url);
void* URL_toString(void* url);
void* URL_toJSON(void* url);

// URLSearchParams class
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

// URLSearchParams iterator methods
void* ts_url_search_params_entries(void* params);
void* ts_url_search_params_keys(void* params);
void* ts_url_search_params_values(void* params);
void ts_url_search_params_for_each(void* params, void* callback, void* thisArg);

// URLSearchParams VTable methods
void URLSearchParams_append(void* params, void* name, void* value);
void URLSearchParams_delete(void* params, void* name);
void* URLSearchParams_get(void* params, void* name);
void* URLSearchParams_getAll(void* params, void* name);
bool URLSearchParams_has(void* params, void* name);
void URLSearchParams_set(void* params, void* name, void* value);
void URLSearchParams_sort(void* params);
void* URLSearchParams_toString(void* params);
void* URLSearchParams_entries(void* params);
void* URLSearchParams_keys(void* params);
void* URLSearchParams_values(void* params);
void URLSearchParams_forEach(void* params, void* callback, void* thisArg);

// URL module static functions
void* ts_url_file_url_to_path(void* url);
void* ts_url_path_to_file_url(void* path);
void* ts_url_format(void* urlObj, void* options);
void* ts_url_resolve(void* from, void* to);
void* ts_url_to_http_options(void* url);
void* ts_url_domain_to_ascii(void* domain);
void* ts_url_domain_to_unicode(void* domain);
void* ts_url_parse(void* urlStr, bool parseQueryString, bool slashesDenoteHost);

// QueryString functions
void* ts_querystring_parse(void* str, void* sep, void* eq);
void* ts_querystring_stringify(void* obj, void* sep, void* eq);
void* ts_querystring_escape(void* str);
void* ts_querystring_unescape(void* str);

#ifdef __cplusplus
}
#endif

#endif // TS_URL_EXT_H
