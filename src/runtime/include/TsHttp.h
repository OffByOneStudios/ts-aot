#pragma once

#include "TsString.h"
#include "TsObject.h"
#include "TsMap.h"
#include "TsBuffer.h"
#include "TsFetch.h"
#include "TsReadable.h"
#include "TsWritable.h"
#include "TsServer.h"
#include "TsSocket.h"
#include <uv.h>
#include <llhttp.h>
#include <vector>

class TsSocket;

class TsIncomingMessage : public TsReadable {
public:
    static constexpr uint32_t MAGIC = 0x494E434D; // "INCM"
    static TsIncomingMessage* Create();

    TsString* method;
    TsString* url;
    TsHeaders* headers;
    int statusCode = 0;  // For HTTP client responses
    TsString* statusMessage = nullptr;  // For HTTP client responses

    // TsReadable implementation
    virtual void Pause() override;
    virtual void Resume() override;

private:
    TsIncomingMessage();
};

class TsServerResponse : public TsWritable {
public:
    static constexpr uint32_t MAGIC = 0x53524553; // "SRES"
    static TsServerResponse* Create(TsSocket* socket);

    void WriteHead(int status, TsObject* headers);
    
    // TsWritable implementation
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;
    void End(TsValue data);

private:
    TsServerResponse(TsSocket* socket);
    TsSocket* socket;
    bool headersSent = false;
    int status = 200;
};

class TsHttpServer : public TsServer {
public:
    static constexpr uint32_t MAGIC = 0x48535256; // "HSRV"
    static TsHttpServer* Create(TsValue* options, void* callback);

protected:
    TsHttpServer();
};

class TsHttpsServer : public TsHttpServer {
public:
    static TsHttpsServer* Create(TsValue* options, void* callback);

protected:
    virtual void HandleConnection(int status) override;

private:
    TsHttpsServer(TsValue* options);
    TsValue* options;
};

class TsClientRequest : public TsWritable {
public:
    static constexpr uint32_t MAGIC = 0x43524551; // "CREQ"
    static TsClientRequest* Create(TsValue* options, void* callback, bool is_https = false);

    // TsWritable implementation
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;
    void End(TsValue data);

    TsSocket* socket;
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
    bool is_https = false;
    TsValue options;

    void SendHeaders();
    virtual void Connect();

protected:
    TsClientRequest(TsValue* options, void* callback, bool is_https = false);
};

extern "C" {
    void* ts_http_create_server(TsValue* options, void* callback);
    void* ts_https_create_server(TsValue* options, void* callback);
    void ts_http_server_listen(void* server, void* port_val, void* callback);
    void ts_http_server_response_write_head(void* res, int64_t status, TsValue* headers);
    bool ts_http_server_response_write(void* res, void* data);
    void ts_http_server_response_end(void* res, void* data);

    void* ts_http_request(TsValue* options, void* callback);
    void* ts_http_get(TsValue* options, void* callback);
    void* ts_https_request(TsValue* options, void* callback);
    void* ts_https_get(TsValue* options, void* callback);
}
