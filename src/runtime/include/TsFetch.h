#pragma once

#include "TsString.h"
#include "TsObject.h"
#include "TsMap.h"
#include "TsBuffer.h"

class TsHeaders : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x48454144; // "HEAD"
    static TsHeaders* Create();

    void Append(TsString* name, TsString* value);
    TsString* Get(TsString* name);
    void Set(TsString* name, TsString* value);
    bool Has(TsString* name);
    void Delete(TsString* name);

    TsMap* GetMap() { return map; }

private:
    TsHeaders();
    uint32_t magic = MAGIC;
    TsMap* map;
};

class TsResponse : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x52455350; // "RESP"
    static TsResponse* Create(int status, TsString* statusText, TsHeaders* headers, TsBuffer* body);

    int GetStatus() { return status; }
    bool GetOk() { return status >= 200 && status < 300; }
    TsString* GetStatusText() { return statusText; }
    TsHeaders* GetHeaders() { return headers; }
    TsBuffer* GetBody() { return body; }

    void* Text();
    void* Json();
    void* ArrayBuffer();

private:
    TsResponse(int status, TsString* statusText, TsHeaders* headers, TsBuffer* body);
    uint32_t magic = MAGIC;
    int status;
    TsString* statusText;
    TsHeaders* headers;
    TsBuffer* body;
};

extern "C" {
    void* ts_headers_create();
    void ts_headers_append(void* headers, void* name, void* value);
    void* ts_headers_get(void* headers, void* name);
    void ts_headers_set(void* headers, void* name, void* value);
    bool ts_headers_has(void* headers, void* name);
    void ts_headers_delete(void* headers, void* name);

    void* ts_response_create(void* vtable, int32_t status, void* statusText, void* headers, void* body);
    int32_t ts_response_status(void* resp);
    bool ts_response_ok(void* resp);
    void* ts_response_statusText(void* resp);
    void* ts_response_headers(void* resp);
    void* ts_response_text(void* resp);
    void* ts_response_json(void* resp);
    void* ts_response_arrayBuffer(void* resp);

    void* ts_fetch(void* vtable, void* url, void* options);
}
