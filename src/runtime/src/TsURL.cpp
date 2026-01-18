#include "TsURL.h"
#include "TsArray.h"
#include "TsMap.h"
#include "GC.h"
#include "TsRuntime.h"
#include <new>
#include <string>
#include <regex>
#include <algorithm>
#include <sstream>
#include <vector>
#include <unicode/uidna.h>
#include <unicode/unistr.h>

// ============ TsURL Implementation ============

TsURL* TsURL::Create(TsString* url, TsString* base) {
    void* mem = ts_alloc(sizeof(TsURL));
    return new(mem) TsURL(url, base);
}

TsURL::TsURL(TsString* url, TsString* base) {
    this->username = TsString::Create("");
    this->password = TsString::Create("");
    this->searchParams = nullptr;

    // If base is provided and url is relative, resolve against base
    if (base && url) {
        std::string urlStr = url->ToUtf8();
        std::string baseStr = base->ToUtf8();

        // Check if url is relative (doesn't have a protocol)
        bool isRelative = urlStr.find("://") == std::string::npos &&
                          (urlStr.empty() || urlStr[0] == '/' || urlStr[0] == '.' ||
                           urlStr[0] == '?' || urlStr[0] == '#' ||
                           (urlStr.find(':') == std::string::npos));

        if (isRelative && !baseStr.empty()) {
            // Parse the base URL first
            std::regex baseRegex(R"(^([^:/?#]+):(?://(?:([^:@/?#]*)(?::([^@/?#]*))?@)?([^/?#:]*)(?::([0-9]*))?)?([^?#]*)(?:\?([^#]*))?(?:#(.*))?)");
            std::smatch baseMatches;

            if (std::regex_match(baseStr, baseMatches, baseRegex)) {
                std::string baseProtocol = baseMatches[1].str();
                std::string baseHost = baseMatches[4].str();
                std::string basePort = baseMatches[5].str();
                std::string basePath = baseMatches[6].str();
                std::string baseUsername = baseMatches[2].str();
                std::string basePassword = baseMatches[3].str();

                std::string resolved;

                if (urlStr.empty()) {
                    // Empty string resolves to base
                    resolved = baseStr;
                } else if (urlStr[0] == '#') {
                    // Hash-only: use base with new hash
                    size_t hashPos = baseStr.find('#');
                    if (hashPos != std::string::npos) {
                        resolved = baseStr.substr(0, hashPos) + urlStr;
                    } else {
                        resolved = baseStr + urlStr;
                    }
                } else if (urlStr[0] == '?') {
                    // Query-only: use base protocol/host/path with new query
                    resolved = baseProtocol + "://";
                    if (!baseUsername.empty()) {
                        resolved += baseUsername;
                        if (!basePassword.empty()) resolved += ":" + basePassword;
                        resolved += "@";
                    }
                    resolved += baseHost;
                    if (!basePort.empty()) resolved += ":" + basePort;
                    resolved += basePath + urlStr;
                } else if (urlStr.substr(0, 2) == "//") {
                    // Protocol-relative: use base protocol
                    resolved = baseProtocol + ":" + urlStr;
                } else if (urlStr[0] == '/') {
                    // Absolute path: use base protocol/host
                    resolved = baseProtocol + "://";
                    if (!baseUsername.empty()) {
                        resolved += baseUsername;
                        if (!basePassword.empty()) resolved += ":" + basePassword;
                        resolved += "@";
                    }
                    resolved += baseHost;
                    if (!basePort.empty()) resolved += ":" + basePort;
                    resolved += urlStr;
                } else {
                    // Relative path: resolve against base path
                    resolved = baseProtocol + "://";
                    if (!baseUsername.empty()) {
                        resolved += baseUsername;
                        if (!basePassword.empty()) resolved += ":" + basePassword;
                        resolved += "@";
                    }
                    resolved += baseHost;
                    if (!basePort.empty()) resolved += ":" + basePort;

                    // Get directory part of base path
                    size_t lastSlash = basePath.rfind('/');
                    std::string baseDir = (lastSlash != std::string::npos) ?
                                          basePath.substr(0, lastSlash + 1) : "/";

                    // Combine and normalize
                    std::string combinedPath = baseDir + urlStr;

                    // Normalize path (remove . and ..)
                    std::vector<std::string> segments;
                    std::stringstream ss(combinedPath);
                    std::string segment;
                    while (std::getline(ss, segment, '/')) {
                        if (segment == "..") {
                            if (!segments.empty()) {
                                segments.pop_back();
                            }
                        } else if (segment != "." && !segment.empty()) {
                            // Skip empty segments (from leading/trailing/double slashes)
                            segments.push_back(segment);
                        }
                    }

                    // Rebuild path - always start with / for absolute paths
                    std::string normalizedPath = "/";
                    for (size_t i = 0; i < segments.size(); i++) {
                        if (i > 0) normalizedPath += "/";
                        normalizedPath += segments[i];
                    }

                    resolved += normalizedPath;
                }

                this->href = TsString::Create(resolved.c_str());
                Parse();
                return;
            }
        }
    }

    // No base or not relative - parse url directly
    this->href = url;
    Parse();
}

void TsURL::Parse() {
    if (!href) return;
    std::string urlStr = href->ToUtf8();
    
    // WHATWG URL regex: protocol://[username[:password]@]host[:port]/path?search#hash
    std::regex urlRegex(R"(^([^:/?#]+):(?://(?:([^:@/?#]*)(?::([^@/?#]*))?@)?([^/?#:]*)(?::([0-9]*))?)?([^?#]*)(?:\?([^#]*))?(?:#(.*))?)");
    std::smatch matches;
    
    if (std::regex_match(urlStr, matches, urlRegex)) {
        protocol = TsString::Create((matches[1].str() + ":").c_str());
        username = TsString::Create(matches[2].str().c_str());
        password = TsString::Create(matches[3].str().c_str());
        hostname = TsString::Create(matches[4].str().c_str());
        port = TsString::Create(matches[5].str().c_str());
        pathname = TsString::Create(matches[6].str().empty() ? "/" : matches[6].str().c_str());
        search = TsString::Create(matches[7].str().empty() ? "" : ("?" + matches[7].str()).c_str());
        hash = TsString::Create(matches[8].str().empty() ? "" : ("#" + matches[8].str()).c_str());
        
        // Build host from hostname:port
        std::string hostStr = matches[4].str();
        if (!matches[5].str().empty()) {
            hostStr += ":" + matches[5].str();
        }
        host = TsString::Create(hostStr.c_str());
        
        // Create searchParams from search string
        std::string searchStr = matches[7].str();
        searchParams = TsURLSearchParams::Create(TsString::Create(searchStr.c_str()));
    } else {
        // Fallback for simple paths
        protocol = TsString::Create("");
        host = TsString::Create("");
        hostname = TsString::Create("");
        port = TsString::Create("");
        pathname = href;
        search = TsString::Create("");
        hash = TsString::Create("");
        searchParams = TsURLSearchParams::Create(nullptr);
    }
}

void TsURL::UpdateHref() {
    std::string result;
    if (protocol && strlen(protocol->ToUtf8()) > 0) {
        result += protocol->ToUtf8();
        result += "//";
    }
    if (username && strlen(username->ToUtf8()) > 0) {
        result += username->ToUtf8();
        if (password && strlen(password->ToUtf8()) > 0) {
            result += ":";
            result += password->ToUtf8();
        }
        result += "@";
    }
    if (hostname) result += hostname->ToUtf8();
    if (port && strlen(port->ToUtf8()) > 0) {
        result += ":";
        result += port->ToUtf8();
    }
    if (pathname) result += pathname->ToUtf8();
    if (search) result += search->ToUtf8();
    if (hash) result += hash->ToUtf8();
    href = TsString::Create(result.c_str());
}

TsString* TsURL::GetHref() { return href; }
TsString* TsURL::GetProtocol() { return protocol; }
TsString* TsURL::GetHost() { return host; }
TsString* TsURL::GetHostname() { return hostname; }
TsString* TsURL::GetPort() { return port; }
TsString* TsURL::GetPathname() { return pathname; }
TsString* TsURL::GetSearch() { return search; }
TsString* TsURL::GetHash() { return hash; }
TsString* TsURL::GetUsername() { return username; }
TsString* TsURL::GetPassword() { return password; }
TsURLSearchParams* TsURL::GetSearchParams() { return searchParams; }
TsString* TsURL::ToString() { return href; }
TsString* TsURL::ToJSON() { return href; }

TsString* TsURL::GetOrigin() {
    std::string result;
    if (protocol) result += protocol->ToUtf8();
    result += "//";
    if (hostname) result += hostname->ToUtf8();
    if (port && strlen(port->ToUtf8()) > 0) {
        result += ":";
        result += port->ToUtf8();
    }
    return TsString::Create(result.c_str());
}

void TsURL::SetHref(TsString* value) { href = value; Parse(); }
void TsURL::SetProtocol(TsString* value) { protocol = value; UpdateHref(); }
void TsURL::SetHost(TsString* value) { 
    host = value;
    // Parse host into hostname:port
    std::string hostStr = value->ToUtf8();
    size_t colonPos = hostStr.find_last_of(':');
    if (colonPos != std::string::npos) {
        hostname = TsString::Create(hostStr.substr(0, colonPos).c_str());
        port = TsString::Create(hostStr.substr(colonPos + 1).c_str());
    } else {
        hostname = value;
        port = TsString::Create("");
    }
    UpdateHref();
}
void TsURL::SetHostname(TsString* value) { hostname = value; UpdateHref(); }
void TsURL::SetPort(TsString* value) { port = value; UpdateHref(); }
void TsURL::SetPathname(TsString* value) { pathname = value; UpdateHref(); }
void TsURL::SetSearch(TsString* value) { 
    search = value; 
    // Update searchParams
    std::string s = value->ToUtf8();
    if (!s.empty() && s[0] == '?') s = s.substr(1);
    searchParams = TsURLSearchParams::Create(TsString::Create(s.c_str()));
    UpdateHref(); 
}
void TsURL::SetHash(TsString* value) { hash = value; UpdateHref(); }
void TsURL::SetUsername(TsString* value) { username = value; UpdateHref(); }
void TsURL::SetPassword(TsString* value) { password = value; UpdateHref(); }

// ============ TsURLSearchParams Implementation ============

TsURLSearchParams* TsURLSearchParams::Create(TsString* query) {
    void* mem = ts_alloc(sizeof(TsURLSearchParams));
    return new(mem) TsURLSearchParams(query);
}

TsURLSearchParams::TsURLSearchParams(TsString* query) {
    entries = ts_array_create();
    if (query) Parse(query);
}

void TsURLSearchParams::Parse(TsString* query) {
    if (!query) return;
    std::string q = query->ToUtf8();
    if (q.empty()) return;
    if (q[0] == '?') q = q.substr(1);
    
    std::istringstream stream(q);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        if (pair.empty()) continue;
        size_t eqPos = pair.find('=');
        std::string name, value;
        if (eqPos != std::string::npos) {
            name = pair.substr(0, eqPos);
            value = pair.substr(eqPos + 1);
        } else {
            name = pair;
            value = "";
        }
        // URL decode (basic: + to space, %XX)
        // For simplicity, just handle + to space
        std::replace(name.begin(), name.end(), '+', ' ');
        std::replace(value.begin(), value.end(), '+', ' ');
        
        Append(TsString::Create(name.c_str()), TsString::Create(value.c_str()));
    }
}

void TsURLSearchParams::Append(TsString* name, TsString* value) {
    TsArray* arr = (TsArray*)entries;
    // Store as a simple object with name and value
    TsArray* pair = TsArray::Create();
    pair->Push(reinterpret_cast<int64_t>(name));
    pair->Push(reinterpret_cast<int64_t>(value));
    arr->Push(reinterpret_cast<int64_t>(pair));
}

void TsURLSearchParams::Delete(TsString* name) {
    TsArray* arr = (TsArray*)entries;
    std::string nameStr = name->ToUtf8();
    TsArray* newArr = TsArray::Create();
    for (size_t i = 0; i < (size_t)arr->Length(); i++) {
        TsArray* pair = (TsArray*)arr->Get(i);
        TsString* pairName = (TsString*)pair->Get(0);
        if (strcmp(pairName->ToUtf8(), nameStr.c_str()) != 0) {
            newArr->Push(reinterpret_cast<int64_t>(pair));
        }
    }
    entries = newArr;
}

TsString* TsURLSearchParams::Get(TsString* name) {
    TsArray* arr = (TsArray*)entries;
    std::string nameStr = name->ToUtf8();
    for (size_t i = 0; i < (size_t)arr->Length(); i++) {
        TsArray* pair = (TsArray*)arr->Get(i);
        TsString* pairName = (TsString*)pair->Get(0);
        if (strcmp(pairName->ToUtf8(), nameStr.c_str()) == 0) {
            return (TsString*)pair->Get(1);
        }
    }
    return nullptr;
}

void* TsURLSearchParams::GetAll(TsString* name) {
    TsArray* arr = (TsArray*)entries;
    TsArray* result = TsArray::Create();
    std::string nameStr = name->ToUtf8();
    for (size_t i = 0; i < (size_t)arr->Length(); i++) {
        TsArray* pair = (TsArray*)arr->Get(i);
        TsString* pairName = (TsString*)pair->Get(0);
        if (strcmp(pairName->ToUtf8(), nameStr.c_str()) == 0) {
            result->Push(pair->Get(1));
        }
    }
    return result;
}

bool TsURLSearchParams::Has(TsString* name) {
    return Get(name) != nullptr;
}

void TsURLSearchParams::Set(TsString* name, TsString* value) {
    Delete(name);
    Append(name, value);
}

void TsURLSearchParams::Sort() {
    // Sort entries by name
    TsArray* arr = (TsArray*)entries;
    // Simple bubble sort for now
    for (size_t i = 0; i < (size_t)arr->Length(); i++) {
        for (size_t j = i + 1; j < (size_t)arr->Length(); j++) {
            TsArray* pairI = (TsArray*)arr->Get(i);
            TsArray* pairJ = (TsArray*)arr->Get(j);
            TsString* nameI = (TsString*)pairI->Get(0);
            TsString* nameJ = (TsString*)pairJ->Get(0);
            if (strcmp(nameI->ToUtf8(), nameJ->ToUtf8()) > 0) {
                // Swap
                arr->Set(i, reinterpret_cast<int64_t>(pairJ));
                arr->Set(j, reinterpret_cast<int64_t>(pairI));
            }
        }
    }
}

TsString* TsURLSearchParams::ToString() {
    TsArray* arr = (TsArray*)entries;
    std::string result;
    for (size_t i = 0; i < (size_t)arr->Length(); i++) {
        TsArray* pair = (TsArray*)arr->Get(i);
        TsString* name = (TsString*)pair->Get(0);
        TsString* value = (TsString*)pair->Get(1);
        if (i > 0) result += "&";
        result += name->ToUtf8();
        result += "=";
        result += value->ToUtf8();
    }
    return TsString::Create(result.c_str());
}

size_t TsURLSearchParams::GetSize() {
    return (size_t)((TsArray*)entries)->Length();
}

void* TsURLSearchParams::Entries() {
    // entries is already an array of [key, value] pairs
    // Return a copy of it
    TsArray* arr = (TsArray*)entries;
    TsArray* result = TsArray::Create();
    for (size_t i = 0; i < (size_t)arr->Length(); i++) {
        result->Push(arr->Get(i));
    }
    return result;
}

void* TsURLSearchParams::Keys() {
    TsArray* arr = (TsArray*)entries;
    TsArray* result = TsArray::Create();
    for (size_t i = 0; i < (size_t)arr->Length(); i++) {
        TsArray* pair = (TsArray*)arr->Get(i);
        result->Push(pair->Get(0));  // Get the key (first element)
    }
    return result;
}

void* TsURLSearchParams::Values() {
    TsArray* arr = (TsArray*)entries;
    TsArray* result = TsArray::Create();
    for (size_t i = 0; i < (size_t)arr->Length(); i++) {
        TsArray* pair = (TsArray*)arr->Get(i);
        result->Push(pair->Get(1));  // Get the value (second element)
    }
    return result;
}

void TsURLSearchParams::ForEach(void* callback, void* thisArg) {
    TsArray* arr = (TsArray*)entries;
    // ForEach callback signature: (value, key, params)
    typedef void (*ForEachCallback)(void*, void*, void*, void*);
    ForEachCallback fn = (ForEachCallback)callback;
    for (size_t i = 0; i < (size_t)arr->Length(); i++) {
        TsArray* pair = (TsArray*)arr->Get(i);
        TsString* key = (TsString*)pair->Get(0);
        TsString* value = (TsString*)pair->Get(1);
        fn(thisArg, value, key, this);
    }
}

// ============ C API ============

extern "C" {
    void* ts_url_create(void* vtable, void* url, void* base) {
        TsURL* u = TsURL::Create((TsString*)url, (TsString*)base);
        u->vtable = vtable;
        return u;
    }
    void* ts_url_get_href(void* url) { return ((TsURL*)url)->GetHref(); }
    void* ts_url_get_origin(void* url) { return ((TsURL*)url)->GetOrigin(); }
    void* ts_url_get_protocol(void* url) { return ((TsURL*)url)->GetProtocol(); }
    void* ts_url_get_host(void* url) { return ((TsURL*)url)->GetHost(); }
    void* ts_url_get_hostname(void* url) { return ((TsURL*)url)->GetHostname(); }
    void* ts_url_get_port(void* url) { return ((TsURL*)url)->GetPort(); }
    void* ts_url_get_pathname(void* url) { return ((TsURL*)url)->GetPathname(); }
    void* ts_url_get_search(void* url) { return ((TsURL*)url)->GetSearch(); }
    void* ts_url_get_hash(void* url) { return ((TsURL*)url)->GetHash(); }
    void* ts_url_get_username(void* url) { return ((TsURL*)url)->GetUsername(); }
    void* ts_url_get_password(void* url) { return ((TsURL*)url)->GetPassword(); }
    void* ts_url_get_search_params(void* url) { return ((TsURL*)url)->GetSearchParams(); }
    void* ts_url_to_string(void* url) { return ((TsURL*)url)->ToString(); }
    void* ts_url_to_json(void* url) { return ((TsURL*)url)->ToJSON(); }

    void ts_url_set_href(void* url, void* value) { ((TsURL*)url)->SetHref((TsString*)value); }
    void ts_url_set_protocol(void* url, void* value) { ((TsURL*)url)->SetProtocol((TsString*)value); }
    void ts_url_set_host(void* url, void* value) { ((TsURL*)url)->SetHost((TsString*)value); }
    void ts_url_set_hostname(void* url, void* value) { ((TsURL*)url)->SetHostname((TsString*)value); }
    void ts_url_set_port(void* url, void* value) { ((TsURL*)url)->SetPort((TsString*)value); }
    void ts_url_set_pathname(void* url, void* value) { ((TsURL*)url)->SetPathname((TsString*)value); }
    void ts_url_set_search(void* url, void* value) { ((TsURL*)url)->SetSearch((TsString*)value); }
    void ts_url_set_hash(void* url, void* value) { ((TsURL*)url)->SetHash((TsString*)value); }

    void* URL_get_href(void* url) { return ((TsURL*)url)->GetHref(); }
    void* URL_get_origin(void* url) { return ((TsURL*)url)->GetOrigin(); }
    void* URL_get_protocol(void* url) { return ((TsURL*)url)->GetProtocol(); }
    void* URL_get_host(void* url) { return ((TsURL*)url)->GetHost(); }
    void* URL_get_hostname(void* url) { return ((TsURL*)url)->GetHostname(); }
    void* URL_get_port(void* url) { return ((TsURL*)url)->GetPort(); }
    void* URL_get_pathname(void* url) { return ((TsURL*)url)->GetPathname(); }
    void* URL_get_search(void* url) { return ((TsURL*)url)->GetSearch(); }
    void* URL_get_hash(void* url) { return ((TsURL*)url)->GetHash(); }
    void* URL_get_username(void* url) { return ((TsURL*)url)->GetUsername(); }
    void* URL_get_password(void* url) { return ((TsURL*)url)->GetPassword(); }
    void* URL_get_searchParams(void* url) { return ((TsURL*)url)->GetSearchParams(); }

    // URL VTable methods
    void* URL_toString(void* url) { return ((TsURL*)url)->ToString(); }
    void* URL_toJSON(void* url) { return ((TsURL*)url)->ToJSON(); }

    // URLSearchParams C API
    void* ts_url_search_params_create(void* query) {
        return TsURLSearchParams::Create((TsString*)query);
    }
    void ts_url_search_params_append(void* params, void* name, void* value) {
        ((TsURLSearchParams*)params)->Append((TsString*)name, (TsString*)value);
    }
    void ts_url_search_params_delete(void* params, void* name) {
        ((TsURLSearchParams*)params)->Delete((TsString*)name);
    }
    void* ts_url_search_params_get(void* params, void* name) {
        return ((TsURLSearchParams*)params)->Get((TsString*)name);
    }
    void* ts_url_search_params_get_all(void* params, void* name) {
        return ((TsURLSearchParams*)params)->GetAll((TsString*)name);
    }
    bool ts_url_search_params_has(void* params, void* name) {
        return ((TsURLSearchParams*)params)->Has((TsString*)name);
    }
    void ts_url_search_params_set(void* params, void* name, void* value) {
        ((TsURLSearchParams*)params)->Set((TsString*)name, (TsString*)value);
    }
    void ts_url_search_params_sort(void* params) {
        ((TsURLSearchParams*)params)->Sort();
    }
    void* ts_url_search_params_to_string(void* params) {
        return ((TsURLSearchParams*)params)->ToString();
    }
    int64_t ts_url_search_params_size(void* params) {
        return (int64_t)((TsURLSearchParams*)params)->GetSize();
    }

    // URLSearchParams VTable methods
    void URLSearchParams_append(void* params, void* name, void* value) {
        ((TsURLSearchParams*)params)->Append((TsString*)name, (TsString*)value);
    }
    void URLSearchParams_delete(void* params, void* name) {
        ((TsURLSearchParams*)params)->Delete((TsString*)name);
    }
    void* URLSearchParams_get(void* params, void* name) {
        return ((TsURLSearchParams*)params)->Get((TsString*)name);
    }
    void* URLSearchParams_getAll(void* params, void* name) {
        return ((TsURLSearchParams*)params)->GetAll((TsString*)name);
    }
    bool URLSearchParams_has(void* params, void* name) {
        return ((TsURLSearchParams*)params)->Has((TsString*)name);
    }
    void URLSearchParams_set(void* params, void* name, void* value) {
        ((TsURLSearchParams*)params)->Set((TsString*)name, (TsString*)value);
    }
    void URLSearchParams_sort(void* params) {
        ((TsURLSearchParams*)params)->Sort();
    }
    void* URLSearchParams_toString(void* params) {
        return ((TsURLSearchParams*)params)->ToString();
    }

    // URLSearchParams iterator methods
    void* ts_url_search_params_entries(void* params) {
        return ((TsURLSearchParams*)params)->Entries();
    }
    void* ts_url_search_params_keys(void* params) {
        return ((TsURLSearchParams*)params)->Keys();
    }
    void* ts_url_search_params_values(void* params) {
        return ((TsURLSearchParams*)params)->Values();
    }
    void ts_url_search_params_for_each(void* params, void* callback, void* thisArg) {
        ((TsURLSearchParams*)params)->ForEach(callback, thisArg);
    }

    // URLSearchParams VTable iterator methods
    void* URLSearchParams_entries(void* params) {
        return ((TsURLSearchParams*)params)->Entries();
    }
    void* URLSearchParams_keys(void* params) {
        return ((TsURLSearchParams*)params)->Keys();
    }
    void* URLSearchParams_values(void* params) {
        return ((TsURLSearchParams*)params)->Values();
    }
    void URLSearchParams_forEach(void* params, void* callback, void* thisArg) {
        ((TsURLSearchParams*)params)->ForEach(callback, thisArg);
    }

    // URL module static functions

    // Helper to decode percent-encoded characters
    static std::string percentDecode(const std::string& str) {
        std::string result;
        for (size_t i = 0; i < str.length(); i++) {
            if (str[i] == '%' && i + 2 < str.length()) {
                int hex;
                if (sscanf(str.substr(i + 1, 2).c_str(), "%x", &hex) == 1) {
                    result += static_cast<char>(hex);
                    i += 2;
                } else {
                    result += str[i];
                }
            } else {
                result += str[i];
            }
        }
        return result;
    }

    // Helper to percent-encode a path for file URLs
    static std::string percentEncodePath(const std::string& path) {
        std::string result;
        for (unsigned char c : path) {
            // Don't encode alphanumeric, -, _, ., ~, or /
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
                result += c;
            } else if (c == '\\') {
                // Convert backslash to forward slash on Windows
                result += '/';
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", c);
                result += buf;
            }
        }
        return result;
    }

    void* ts_url_file_url_to_path(void* urlArg) {
        // fileURLToPath(url) - converts file:// URL to file system path
        if (!urlArg) return TsString::Create("");

        TsString* urlStr = nullptr;
        TsURL* urlObj = nullptr;

        // First, try to unbox as object (for URL object)
        void* objPtr = ts_value_get_object((TsValue*)urlArg);
        if (objPtr) {
            urlObj = dynamic_cast<TsURL*>((TsObject*)objPtr);
            if (urlObj) {
                urlStr = urlObj->GetHref();
            }
        }

        // If not a URL object, try to get as string
        if (!urlStr) {
            void* strPtr = ts_value_get_string((TsValue*)urlArg);
            if (strPtr) {
                urlStr = (TsString*)strPtr;
            } else {
                // Last resort - treat urlArg as raw TsString*
                urlStr = (TsString*)urlArg;
            }
        }

        if (!urlStr) return TsString::Create("");

        std::string url = urlStr->ToUtf8();

        // Must start with file://
        if (url.substr(0, 7) != "file://") {
            // In Node.js this throws, but we'll return empty string
            return TsString::Create("");
        }

        // Remove file:// prefix
        std::string path = url.substr(7);

        // Handle file:///C:/... (Windows absolute path)
        // or file:///path/to/file (Unix absolute path)
#ifdef _WIN32
        // On Windows: file:///C:/path -> C:/path
        if (path.length() >= 3 && path[0] == '/' && isalpha(path[1]) && path[2] == ':') {
            path = path.substr(1);  // Remove leading /
        }
        // Convert forward slashes to backslashes on Windows
        for (char& c : path) {
            if (c == '/') c = '\\';
        }
#else
        // On Unix: file:///path -> /path (keep the leading /)
        // path already has leading /
#endif

        // Decode percent-encoded characters
        path = percentDecode(path);

        return TsString::Create(path.c_str());
    }

    void* ts_url_path_to_file_url(void* pathArg) {
        // pathToFileURL(path) - converts file system path to file:// URL
        if (!pathArg) return nullptr;

        // Try to unbox as string
        void* strPtr = ts_value_get_string((TsValue*)pathArg);
        TsString* pathStr = nullptr;
        if (strPtr) {
            pathStr = (TsString*)strPtr;
        } else {
            // Fallback to raw pointer
            pathStr = (TsString*)pathArg;
        }
        std::string path = pathStr->ToUtf8();

        if (path.empty()) {
            return nullptr;
        }

        std::string urlStr = "file://";

#ifdef _WIN32
        // On Windows: C:\path -> file:///C:/path
        if (path.length() >= 2 && isalpha(path[0]) && path[1] == ':') {
            urlStr += "/";
        }
#endif

        // Percent-encode the path (also converts \ to /)
        urlStr += percentEncodePath(path);

        // Create and return a URL object
        return TsURL::Create(TsString::Create(urlStr.c_str()));
    }

    // Helper to get string from TsMap property
    static TsString* getMapString(TsMap* map, const char* key) {
        TsValue val = map->Get(TsString::Create(key));
        if (val.type == ValueType::STRING_PTR) {
            return (TsString*)val.ptr_val;
        }
        return nullptr;
    }

    // Helper to get int from TsMap property
    static int64_t getMapInt(TsMap* map, const char* key) {
        TsValue val = map->Get(TsString::Create(key));
        if (val.type == ValueType::NUMBER_INT) {
            return val.i_val;
        }
        if (val.type == ValueType::NUMBER_DBL) {
            return (int64_t)val.d_val;
        }
        return 0;
    }

    // Helper to get bool from TsMap property
    static bool getMapBool(TsMap* map, const char* key) {
        TsValue val = map->Get(TsString::Create(key));
        if (val.type == ValueType::BOOLEAN) {
            return val.b_val;
        }
        return false;
    }

    // url.format(urlObject, options?) - format URL to string
    // Supports both legacy URL objects { protocol, hostname, port, pathname, ... }
    // and WHATWG URL instances
    void* ts_url_format(void* urlArg, void* optionsArg) {
        if (!urlArg) return TsString::Create("");

        // Try to get as TsURL first (WHATWG URL instance)
        void* objPtr = ts_value_get_object((TsValue*)urlArg);
        if (!objPtr) objPtr = urlArg;

        TsURL* url = dynamic_cast<TsURL*>((TsObject*)objPtr);
        if (url) {
            // WHATWG URL - just return its href
            return url->GetHref();
        }

        // Try to get as TsMap (legacy URL object with properties)
        TsMap* map = dynamic_cast<TsMap*>((TsObject*)objPtr);
        if (!map) {
            // Check if it's boxed differently
            if (((TsObject*)objPtr)->magic == TsMap::MAGIC) {
                map = (TsMap*)objPtr;
            }
        }

        if (!map) {
            // Can't parse, return empty
            return TsString::Create("");
        }

        // Build URL from object properties
        std::string result;

        // Get protocol
        TsString* protocol = getMapString(map, "protocol");

        if (protocol && strlen(protocol->ToUtf8()) > 0) {
            result += protocol->ToUtf8();
            // Add colon if not present
            if (result.back() != ':') result += ':';
        }

        // Check for slashes property
        bool hasSlashes = getMapBool(map, "slashes");
        // Also add slashes if protocol requires it (http, https, ftp)
        if (protocol) {
            std::string p = protocol->ToUtf8();
            if (p == "http:" || p == "https:" || p == "ftp:" || p == "file:") {
                hasSlashes = true;
            }
        }

        if (hasSlashes) {
            result += "//";
        }

        // Get auth (username:password)
        TsString* auth = getMapString(map, "auth");
        if (auth && strlen(auth->ToUtf8()) > 0) {
            result += auth->ToUtf8();
            result += "@";
        }

        // Get host (includes port)
        TsString* host = getMapString(map, "host");
        TsString* hostname = getMapString(map, "hostname");

        if (host && strlen(host->ToUtf8()) > 0) {
            result += host->ToUtf8();
        } else {
            // Build from hostname and port
            if (hostname) result += hostname->ToUtf8();

            // Get port - can be string or number
            TsString* portStr = getMapString(map, "port");
            int64_t portNum = getMapInt(map, "port");

            if (portStr && strlen(portStr->ToUtf8()) > 0) {
                result += ":";
                result += portStr->ToUtf8();
            } else if (portNum > 0) {
                result += ":";
                result += std::to_string(portNum);
            }
        }

        // Get pathname
        TsString* pathname = getMapString(map, "pathname");
        if (pathname) result += pathname->ToUtf8();

        // Get search/query
        TsString* search = getMapString(map, "search");
        TsString* query = getMapString(map, "query");

        if (search && strlen(search->ToUtf8()) > 0) {
            std::string s = search->ToUtf8();
            if (s[0] != '?') result += '?';
            result += s;
        } else if (query && strlen(query->ToUtf8()) > 0) {
            result += "?";
            result += query->ToUtf8();
        }

        // Get hash
        TsString* hash = getMapString(map, "hash");
        if (hash && strlen(hash->ToUtf8()) > 0) {
            std::string h = hash->ToUtf8();
            if (h[0] != '#') result += '#';
            result += h;
        }

        return TsString::Create(result.c_str());
    }

    // url.resolve(from, to) - resolves 'to' relative to 'from'
    // This is the legacy API for resolving relative URLs
    void* ts_url_resolve(void* fromArg, void* toArg) {
        if (!fromArg || !toArg) return TsString::Create("");

        // Get 'from' as string
        TsString* fromStr = nullptr;
        void* fromPtr = ts_value_get_string((TsValue*)fromArg);
        if (fromPtr) {
            fromStr = (TsString*)fromPtr;
        } else {
            fromStr = (TsString*)fromArg;
        }

        // Get 'to' as string
        TsString* toStr = nullptr;
        void* toPtr = ts_value_get_string((TsValue*)toArg);
        if (toPtr) {
            toStr = (TsString*)toPtr;
        } else {
            toStr = (TsString*)toArg;
        }

        if (!fromStr || !toStr) return TsString::Create("");

        std::string from = fromStr->ToUtf8();
        std::string to = toStr->ToUtf8();

        // If 'to' is an absolute URL, return it directly
        if (to.find("://") != std::string::npos) {
            return TsString::Create(to.c_str());
        }

        // Parse 'from' to get base
        std::regex urlRegex(R"(^([^:/?#]+):(?://(?:([^:@/?#]*)(?::([^@/?#]*))?@)?([^/?#:]*)(?::([0-9]*))?)?([^?#]*)(?:\?([^#]*))?(?:#(.*))?)");
        std::smatch fromMatches;

        if (!std::regex_match(from, fromMatches, urlRegex)) {
            // Can't parse 'from', just return 'to'
            return TsString::Create(to.c_str());
        }

        std::string protocol = fromMatches[1].str();
        std::string username = fromMatches[2].str();
        std::string password = fromMatches[3].str();
        std::string hostname = fromMatches[4].str();
        std::string port = fromMatches[5].str();
        std::string pathname = fromMatches[6].str();

        // Handle different 'to' patterns

        // Handle protocol-relative URLs (//host/path)
        if (to.length() >= 2 && to[0] == '/' && to[1] == '/') {
            return TsString::Create((protocol + ":" + to).c_str());
        }

        std::string result = protocol + ":";

        if (!hostname.empty()) {
            result += "//";
            if (!username.empty()) {
                result += username;
                if (!password.empty()) {
                    result += ":" + password;
                }
                result += "@";
            }
            result += hostname;
            if (!port.empty()) {
                result += ":" + port;
            }
        }

        if (to[0] == '/') {
            // Absolute path - replace from's path
            result += to;
        } else if (to[0] == '?') {
            // Query string - keep from's path
            result += pathname + to;
        } else if (to[0] == '#') {
            // Hash - keep from's path and search
            if (fromMatches[7].str().empty()) {
                result += pathname + to;
            } else {
                result += pathname + "?" + fromMatches[7].str() + to;
            }
        } else {
            // Relative path - resolve against from's directory
            size_t lastSlash = pathname.rfind('/');
            if (lastSlash != std::string::npos) {
                std::string dir = pathname.substr(0, lastSlash + 1);
                result += dir + to;
            } else {
                result += "/" + to;
            }
        }

        return TsString::Create(result.c_str());
    }

    // url.urlToHttpOptions(url) - extracts options from URL for http.request()
    void* ts_url_to_http_options(void* urlArg) {
        if (!urlArg) return nullptr;

        // Try to get as TsURL first
        void* objPtr = ts_value_get_object((TsValue*)urlArg);
        if (!objPtr) objPtr = urlArg;

        TsURL* url = dynamic_cast<TsURL*>((TsObject*)objPtr);
        if (!url) {
            // Try to parse string as URL
            TsString* urlStr = nullptr;
            void* strPtr = ts_value_get_string((TsValue*)urlArg);
            if (strPtr) {
                urlStr = (TsString*)strPtr;
            } else {
                urlStr = (TsString*)urlArg;
            }
            if (urlStr) {
                url = TsURL::Create(urlStr);
            }
        }

        if (!url) return nullptr;

        // Create options object
        TsMap* options = TsMap::Create();

        // protocol
        TsString* protocol = url->GetProtocol();
        if (protocol && strlen(protocol->ToUtf8()) > 0) {
            options->Set(TsString::Create("protocol"), ts_value_make_string(protocol));
        }

        // hostname
        TsString* hostname = url->GetHostname();
        if (hostname && strlen(hostname->ToUtf8()) > 0) {
            options->Set(TsString::Create("hostname"), ts_value_make_string(hostname));
        }

        // port
        TsString* port = url->GetPort();
        if (port && strlen(port->ToUtf8()) > 0) {
            // Convert to number
            int portNum = atoi(port->ToUtf8());
            if (portNum > 0) {
                options->Set(TsString::Create("port"), ts_value_make_int(portNum));
            }
        }

        // path (pathname + search)
        TsString* pathname = url->GetPathname();
        TsString* search = url->GetSearch();
        std::string path;
        if (pathname) path += pathname->ToUtf8();
        if (search) path += search->ToUtf8();
        if (!path.empty()) {
            options->Set(TsString::Create("path"), ts_value_make_string(TsString::Create(path.c_str())));
        }

        // hash
        TsString* hash = url->GetHash();
        if (hash && strlen(hash->ToUtf8()) > 0) {
            options->Set(TsString::Create("hash"), ts_value_make_string(hash));
        }

        // auth (username:password)
        TsString* username = url->GetUsername();
        TsString* password = url->GetPassword();
        if (username && strlen(username->ToUtf8()) > 0) {
            std::string auth = username->ToUtf8();
            if (password && strlen(password->ToUtf8()) > 0) {
                auth += ":";
                auth += password->ToUtf8();
            }
            options->Set(TsString::Create("auth"), ts_value_make_string(TsString::Create(auth.c_str())));
        }

        // href
        TsString* href = url->GetHref();
        if (href) {
            options->Set(TsString::Create("href"), ts_value_make_string(href));
        }

        return options;
    }

    // url.domainToASCII(domain) - convert internationalized domain to ASCII (Punycode)
    void* ts_url_domain_to_ascii(void* domainArg) {
        TsString* domain = (TsString*)domainArg;
        if (!domain) {
            return TsString::Create("");
        }

        UErrorCode status = U_ZERO_ERROR;
        UIDNA* idna = uidna_openUTS46(UIDNA_DEFAULT, &status);
        if (U_FAILURE(status)) {
            return TsString::Create("");
        }

        const char* input = domain->ToUtf8();
        icu::UnicodeString uInput = icu::UnicodeString::fromUTF8(input);

        UIDNAInfo info = UIDNA_INFO_INITIALIZER;
        char output[256];
        int32_t len = uidna_nameToASCII_UTF8(idna, input, -1, output, sizeof(output), &info, &status);

        uidna_close(idna);

        if (U_FAILURE(status) || info.errors != 0) {
            // Return empty string on error (matches Node.js behavior)
            return TsString::Create("");
        }

        output[len] = '\0';
        return TsString::Create(output);
    }

    // url.domainToUnicode(domain) - convert ASCII (Punycode) domain to Unicode
    void* ts_url_domain_to_unicode(void* domainArg) {
        TsString* domain = (TsString*)domainArg;
        if (!domain) {
            return TsString::Create("");
        }

        UErrorCode status = U_ZERO_ERROR;
        UIDNA* idna = uidna_openUTS46(UIDNA_DEFAULT, &status);
        if (U_FAILURE(status)) {
            return TsString::Create("");
        }

        const char* input = domain->ToUtf8();

        UIDNAInfo info = UIDNA_INFO_INITIALIZER;
        char output[256];
        int32_t len = uidna_nameToUnicodeUTF8(idna, input, -1, output, sizeof(output), &info, &status);

        uidna_close(idna);

        if (U_FAILURE(status)) {
            // Return the input unchanged on error
            return domain;
        }

        output[len] = '\0';
        return TsString::Create(output);
    }

    // url.parse(urlString, parseQueryString?, slashesDenoteHost?) - legacy URL parsing
    // Returns an object with: protocol, slashes, auth, host, port, hostname, hash, search, query, pathname, path, href
    void* ts_url_parse(void* urlArg, bool parseQueryString, bool slashesDenoteHost) {
        if (!urlArg) return nullptr;

        // Get URL string
        TsString* urlStr = (TsString*)urlArg;
        if (!urlStr) return nullptr;

        std::string url = urlStr->ToUtf8();
        if (url.empty()) return nullptr;

        // Create result object (TsMap for property access)
        TsMap* result = TsMap::Create();

        // Parse the URL using regex
        // Format: [protocol:]//[user[:password]@]host[:port]/path[?search][#hash]
        std::regex urlRegex(R"(^([^:/?#]+):(?://(?:([^:@/?#]*)(?::([^@/?#]*))?@)?([^/?#:]*)(?::([0-9]*))?)?([^?#]*)(?:\?([^#]*))?(?:#(.*))?)");
        std::smatch matches;

        bool hasProtocol = false;
        bool hasSlashes = false;
        std::string protocol, auth, host, hostname, port, pathname, search, query, hash;

        if (std::regex_match(url, matches, urlRegex)) {
            hasProtocol = true;
            protocol = matches[1].str() + ":";
            std::string username = matches[2].str();
            std::string password = matches[3].str();
            hostname = matches[4].str();
            port = matches[5].str();
            pathname = matches[6].str();
            query = matches[7].str();
            hash = matches[8].str();

            // Build auth from username:password
            if (!username.empty()) {
                auth = username;
                if (!password.empty()) {
                    auth += ":" + password;
                }
            }

            // Build host from hostname:port
            host = hostname;
            if (!port.empty()) {
                host += ":" + port;
            }

            // Build search from query
            if (!query.empty()) {
                search = "?" + query;
            }

            // Prefix hash with #
            if (!hash.empty()) {
                hash = "#" + hash;
            }

            // Default pathname to "/" when there's a host but no explicit path
            if (pathname.empty() && !hostname.empty()) {
                pathname = "/";
            }

            // Check if URL has slashes (// after protocol)
            size_t colonPos = url.find(':');
            if (colonPos != std::string::npos && colonPos + 2 < url.length()) {
                hasSlashes = (url[colonPos + 1] == '/' && url[colonPos + 2] == '/');
            }
        } else if (slashesDenoteHost && url.length() >= 2 && url[0] == '/' && url[1] == '/') {
            // Handle //host/path case when slashesDenoteHost is true
            hasSlashes = true;
            std::string rest = url.substr(2);
            size_t pathStart = rest.find('/');
            if (pathStart != std::string::npos) {
                host = rest.substr(0, pathStart);
                hostname = host;
                // Check for port
                size_t portPos = host.find(':');
                if (portPos != std::string::npos) {
                    hostname = host.substr(0, portPos);
                    port = host.substr(portPos + 1);
                }
                pathname = rest.substr(pathStart);
            } else {
                host = rest;
                hostname = rest;
                pathname = "/";
            }
        } else {
            // Simple path, possibly with query and hash
            size_t hashPos = url.find('#');
            size_t queryPos = url.find('?');

            if (hashPos != std::string::npos) {
                hash = url.substr(hashPos);
                url = url.substr(0, hashPos);
            }
            if (queryPos != std::string::npos) {
                search = url.substr(queryPos);
                query = url.substr(queryPos + 1);
                pathname = url.substr(0, queryPos);
            } else {
                pathname = url;
            }
        }

        // Set all properties on the result object
        // Note: TsMap::Set takes TsValue by value, so we dereference the TsValue* returned by ts_value_make_*
        result->Set(TsString::Create("protocol"), *ts_value_make_string(TsString::Create(protocol.c_str())));
        result->Set(TsString::Create("slashes"), *ts_value_make_bool(hasSlashes));
        result->Set(TsString::Create("auth"), auth.empty() ? *ts_value_make_null() : *ts_value_make_string(TsString::Create(auth.c_str())));
        result->Set(TsString::Create("host"), host.empty() ? *ts_value_make_null() : *ts_value_make_string(TsString::Create(host.c_str())));
        result->Set(TsString::Create("port"), port.empty() ? *ts_value_make_null() : *ts_value_make_string(TsString::Create(port.c_str())));
        result->Set(TsString::Create("hostname"), hostname.empty() ? *ts_value_make_null() : *ts_value_make_string(TsString::Create(hostname.c_str())));
        result->Set(TsString::Create("hash"), hash.empty() ? *ts_value_make_null() : *ts_value_make_string(TsString::Create(hash.c_str())));
        result->Set(TsString::Create("search"), search.empty() ? *ts_value_make_null() : *ts_value_make_string(TsString::Create(search.c_str())));
        result->Set(TsString::Create("pathname"), pathname.empty() ? *ts_value_make_null() : *ts_value_make_string(TsString::Create(pathname.c_str())));

        // path = pathname + search
        std::string path = pathname + search;
        result->Set(TsString::Create("path"), path.empty() ? *ts_value_make_null() : *ts_value_make_string(TsString::Create(path.c_str())));

        // query - either parsed object or string depending on parseQueryString
        if (parseQueryString && !query.empty()) {
            // Parse query string into object
            TsMap* queryObj = TsMap::Create();
            std::istringstream queryStream(query);
            std::string pair;
            while (std::getline(queryStream, pair, '&')) {
                size_t eqPos = pair.find('=');
                if (eqPos != std::string::npos) {
                    std::string key = percentDecode(pair.substr(0, eqPos));
                    std::string value = percentDecode(pair.substr(eqPos + 1));
                    queryObj->Set(TsString::Create(key.c_str()), *ts_value_make_string(TsString::Create(value.c_str())));
                } else {
                    std::string key = percentDecode(pair);
                    queryObj->Set(TsString::Create(key.c_str()), *ts_value_make_string(TsString::Create("")));
                }
            }
            result->Set(TsString::Create("query"), *ts_value_make_object(queryObj));
        } else {
            result->Set(TsString::Create("query"), query.empty() ? *ts_value_make_null() : *ts_value_make_string(TsString::Create(query.c_str())));
        }

        // Rebuild href
        std::string href;
        if (!protocol.empty()) {
            href += protocol;
            if (hasSlashes) {
                href += "//";
            }
        } else if (hasSlashes) {
            href += "//";
        }
        if (!auth.empty()) {
            href += auth + "@";
        }
        href += host;
        href += pathname;
        href += search;
        href += hash;
        result->Set(TsString::Create("href"), *ts_value_make_string(TsString::Create(href.c_str())));

        return result;
    }
}
