#pragma once

#include "TsString.h"
#include "TsObject.h"

extern "C" {
    // Parse a query string into an object
    void* ts_querystring_parse(void* str, void* sep, void* eq);

    // Stringify an object to a query string
    void* ts_querystring_stringify(void* obj, void* sep, void* eq);

    // Percent-encode a string
    void* ts_querystring_escape(void* str);

    // Decode percent-encoded characters
    void* ts_querystring_unescape(void* str);
}
