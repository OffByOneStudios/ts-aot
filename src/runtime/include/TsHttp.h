#pragma once

#include "TsString.h"
#include "TsObject.h"
#include "TsMap.h"
#include "TsBuffer.h"
#include "TsFetch.h"
#include <uv.h>
#include <llhttp.h>
#include <vector>

class TsIncomingMessage : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x494E434D; // "INCM"
    static TsIncomingMessage* Create();

    TsString* method;
    TsString* url;
    TsHeaders* headers;

private:
    TsIncomingMessage();
    uint32_t magic = MAGIC;
};

class TsServerResponse : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x53524553; // "SRES"
    static TsServerResponse* Create(uv_stream_t* client);

    void WriteHead(int status, TsObject* headers);
    void Write(TsValue data);
    void End(TsValue data);

private:
    TsServerResponse(uv_stream_t* client);
    uint32_t magic = MAGIC;
    uv_stream_t* client;
    bool headersSent = false;
    int status = 200;
};

class TsHttpServer : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x48535256; // "HSRV"
    static TsHttpServer* Create(void* callbackVTable, void* callbackFunc);

    void Listen(int port, void* callbackVTable, void* callbackFunc);

    void* requestCallbackVTable;
    void* requestCallbackFunc;
    uv_tcp_t tcp;

private:
    TsHttpServer(void* callbackVTable, void* callbackFunc);
    uint32_t magic = MAGIC;
};

extern "C" {
    void* ts_http_create_server(void* context, void* vtable, void* callback);
    void ts_http_server_listen(void* context, void* server, int32_t port, void* vtable, void* callback);

    void* ts_incoming_message_method(void* context, void* msg);
    void* ts_incoming_message_url(void* context, void* msg);
    void* ts_incoming_message_headers(void* context, void* msg);

    void ts_server_response_write_head(void* context, void* res, int32_t status, void* headers);
    void ts_server_response_write(void* context, void* res, TsValue* data);
    void ts_server_response_end(void* context, void* res, TsValue* data);
}
