#pragma once

#include "TsString.h"
#include "TsObject.h"
#include "TsMap.h"
#include "TsBuffer.h"
#include "TsFetch.h"
#include "TsReadable.h"
#include "TsWritable.h"
#include "TsServer.h"
#include <uv.h>
#include <llhttp.h>
#include <vector>

class TsIncomingMessage : public TsReadable {
public:
    static constexpr uint32_t MAGIC = 0x494E434D; // "INCM"
    static TsIncomingMessage* Create();

    TsString* method;
    TsString* url;
    TsHeaders* headers;
    int statusCode = 0;  // For HTTP client responses

    // TsReadable implementation
    virtual void Pause() override;
    virtual void Resume() override;

private:
    TsIncomingMessage();
    uint32_t magic = MAGIC;
};

class TsServerResponse : public TsWritable {
public:
    static constexpr uint32_t MAGIC = 0x53524553; // "SRES"
    static TsServerResponse* Create(uv_stream_t* client);

    void WriteHead(int status, TsObject* headers);
    
    // TsWritable implementation
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;
    void End(TsValue data);

private:
    TsServerResponse(uv_stream_t* client);
    uint32_t magic = MAGIC;
    uv_stream_t* client;
    bool headersSent = false;
    int status = 200;
};

class TsHttpServer : public TsServer {
public:
    static constexpr uint32_t MAGIC = 0x48535256; // "HSRV"
    static TsHttpServer* Create(void* callback);

private:
    TsHttpServer();
    uint32_t magic = MAGIC;
};

class TsClientRequest : public TsWritable {
public:
    static constexpr uint32_t MAGIC = 0x43524551; // "CREQ"
    static TsClientRequest* Create(TsValue* options, void* callback);

    // TsWritable implementation
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;
    void End(TsValue data);

private:
    TsClientRequest(TsValue* options, void* callback);
    uv_tcp_t* socket;
    llhttp_t parser;
    llhttp_settings_t settings;
    TsIncomingMessage* response;
    void* callback;
    bool connected = false;
    bool endCalled = false;
    std::string method = "GET";
    std::string host = "localhost";
    int port = 80;
    std::string path = "/";
    TsMap* headers = nullptr;
    bool headersSent = false;
    TsString* currentHeaderField = nullptr;

    void SendHeaders();
    void Connect();
};

extern "C" {
    void* ts_http_create_server(void* callback);
    void ts_http_server_listen(void* server, int64_t port, void* callback);
    void ts_http_server_response_write_head(void* res, int64_t status, TsValue* headers);
    bool ts_http_server_response_write(void* res, void* data);
    void ts_http_server_response_end(void* res, void* data);

    void* ts_http_request(TsValue* options, void* callback);
    void* ts_http_get(TsValue* options, void* callback);
}
