#include "TsURL.h"
#include "GC.h"
#include "TsRuntime.h"
#include <new>
#include <string>
#include <regex>

TsURL* TsURL::Create(TsString* url, TsString* base) {
    void* mem = ts_alloc(sizeof(TsURL));
    return new(mem) TsURL(url, base);
}

TsURL::TsURL(TsString* url, TsString* base) {
    this->href = url;
    // Basic parsing logic
    Parse();
}

void TsURL::Parse() {
    if (!href) return;
    std::string urlStr = href->ToUtf8();
    
    // Very basic URL regex: protocol://host[:port]/path?search#hash
    std::regex urlRegex(R"(^([^:/?#]+):(?://([^/?#]*))?([^?#]*)(?:\?([^#]*))?(?:#(.*))?)");
    std::smatch matches;
    
    if (std::regex_match(urlStr, matches, urlRegex)) {
        protocol = TsString::Create((matches[1].str() + ":").c_str());
        host = TsString::Create(matches[2].str().c_str());
        pathname = TsString::Create(matches[3].str().empty() ? "/" : matches[3].str().c_str());
        search = TsString::Create(matches[4].str().empty() ? "" : ("?" + matches[4].str()).c_str());
        hash = TsString::Create(matches[5].str().empty() ? "" : ("#" + matches[5].str()).c_str());
        
        std::string hostStr = matches[2].str();
        size_t colonPos = hostStr.find_last_of(':');
        if (colonPos != std::string::npos) {
            hostname = TsString::Create(hostStr.substr(0, colonPos).c_str());
            port = TsString::Create(hostStr.substr(colonPos + 1).c_str());
        } else {
            hostname = host;
            port = TsString::Create("");
        }
    }
}

TsString* TsURL::GetHref() { return href; }
TsString* TsURL::GetProtocol() { return protocol; }
TsString* TsURL::GetHost() { return host; }
TsString* TsURL::GetHostname() { return hostname; }
TsString* TsURL::GetPort() { return port; }
TsString* TsURL::GetPathname() { return pathname; }
TsString* TsURL::GetSearch() { return search; }
TsString* TsURL::GetHash() { return hash; }

extern "C" {
    void* ts_url_create(void* vtable, void* url, void* base) {
        TsURL* u = TsURL::Create((TsString*)url, (TsString*)base);
        u->vtable = vtable;
        return u;
    }
    void* ts_url_get_href(void* url) { return ((TsURL*)url)->GetHref(); }
    void* ts_url_get_protocol(void* url) { return ((TsURL*)url)->GetProtocol(); }
    void* ts_url_get_host(void* url) { return ((TsURL*)url)->GetHost(); }
    void* ts_url_get_hostname(void* url) { return ((TsURL*)url)->GetHostname(); }
    void* ts_url_get_port(void* url) { return ((TsURL*)url)->GetPort(); }
    void* ts_url_get_pathname(void* url) { return ((TsURL*)url)->GetPathname(); }
    void* ts_url_get_search(void* url) { return ((TsURL*)url)->GetSearch(); }
    void* ts_url_get_hash(void* url) { return ((TsURL*)url)->GetHash(); }

    void* URL_get_href(void* url) { return ((TsURL*)url)->GetHref(); }
    void* URL_get_protocol(void* url) { return ((TsURL*)url)->GetProtocol(); }
    void* URL_get_host(void* url) { return ((TsURL*)url)->GetHost(); }
    void* URL_get_hostname(void* url) { return ((TsURL*)url)->GetHostname(); }
    void* URL_get_port(void* url) { return ((TsURL*)url)->GetPort(); }
    void* URL_get_pathname(void* url) { return ((TsURL*)url)->GetPathname(); }
    void* URL_get_search(void* url) { return ((TsURL*)url)->GetSearch(); }
    void* URL_get_hash(void* url) { return ((TsURL*)url)->GetHash(); }
}
