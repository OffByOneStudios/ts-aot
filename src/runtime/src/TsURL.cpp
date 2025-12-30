#include "TsURL.h"
#include "TsArray.h"
#include "GC.h"
#include "TsRuntime.h"
#include <new>
#include <string>
#include <regex>
#include <algorithm>
#include <sstream>

// ============ TsURL Implementation ============

TsURL* TsURL::Create(TsString* url, TsString* base) {
    void* mem = ts_alloc(sizeof(TsURL));
    return new(mem) TsURL(url, base);
}

TsURL::TsURL(TsString* url, TsString* base) {
    this->href = url;
    this->username = TsString::Create("");
    this->password = TsString::Create("");
    this->searchParams = nullptr;
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
}
