#pragma once

/*
 * ⚠️ HTTP RUNTIME DEVELOPMENT GUIDE ⚠️
 * 
 * Before editing this file, read: .github/instructions/runtime-extensions.instructions.md
 * 
 * CRITICAL RULES:
 * 1. Memory: Use ts_alloc() for GC objects, NOT new/malloc
 * 2. Strings: Use TsString::Create(), NOT std::string
 * 3. Casting: Use AsXxx() or dynamic_cast, NOT C-style casts
 * 4. Boxing: Use ts_value_get_object() to unbox void* params
 * 5. Errors: ts_error_create() returns ALREADY-BOXED TsValue*
 * 
 * ADDING NEW PROPERTIES:
 * 1. Add field to class (e.g., int statusCode = 0;)
 * 2. Set in callback (e.g., on_headers_complete)
 * 3. Handle in ts_object_get_property() using dynamic_cast
 * 4. Add extern "C" getter if needed for codegen
 */

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
#include <map>
#include <string>

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
    class TsHttpAgent* agent = nullptr;  // Agent for connection pooling (forward declare)
    bool socketFromAgent = false;        // True if socket was reused from agent
    std::string agentKey;                // Key used for agent socket pool

    void SendHeaders();
    virtual void Connect();
    void ReturnSocketToAgent();          // Return socket to agent pool after request

protected:
    TsClientRequest(TsValue* options, void* callback, bool is_https = false);
};

// HTTP Agent for connection pooling
class TsHttpAgent : public TsEventEmitter {
public:
    static constexpr uint32_t MAGIC = 0x41475448; // "AGTH"
    static TsHttpAgent* Create(TsValue* options);

    // Agent options
    bool keepAlive = false;         // Keep sockets around for reuse
    int keepAliveMsecs = 1000;      // Initial delay for TCP keepalive
    int maxSockets = 256;           // Max concurrent sockets per origin (Infinity in Node.js)
    int maxTotalSockets = 256;      // Max total sockets
    int maxFreeSockets = 256;       // Max sockets to leave open in free state
    int timeout = 0;                // Socket timeout in ms (0 = no timeout)
    std::string scheduling = "lifo"; // "lifo" or "fifo"

    // Socket pools - keyed by "host:port"
    std::map<std::string, std::vector<TsSocket*>> freeSockets;
    std::map<std::string, std::vector<TsSocket*>> sockets;
    std::map<std::string, std::vector<TsClientRequest*>> requests; // pending requests

    // Methods
    TsSocket* GetFreeSocket(const std::string& key);
    void AddSocket(const std::string& key, TsSocket* socket);
    void RemoveSocket(const std::string& key, TsSocket* socket);
    void ReuseSocket(const std::string& key, TsSocket* socket);
    void Destroy();
    std::string GetName(const std::string& host, int port, const std::string& localAddress = "");

protected:
    TsHttpAgent(TsValue* options);
};

// HTTPS Agent (extends HTTP Agent)
class TsHttpsAgent : public TsHttpAgent {
public:
    static constexpr uint32_t MAGIC = 0x41475453; // "AGTS"
    static TsHttpsAgent* Create(TsValue* options);

protected:
    TsHttpsAgent(TsValue* options);
};

// Global agent instances
extern TsHttpAgent* globalHttpAgent;
extern TsHttpsAgent* globalHttpsAgent;

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

    // http module constants and utilities
    void* ts_http_get_methods();
    void* ts_http_get_status_codes();
    int64_t ts_http_get_max_header_size();
    void ts_http_validate_header_name(void* name);
    void ts_http_validate_header_value(void* name, void* value);
    
    // Agent API
    void* ts_http_agent_create(TsValue* options);
    void* ts_https_agent_create(TsValue* options);
    void* ts_http_get_global_agent();
    void* ts_https_get_global_agent();
    void ts_http_agent_destroy(void* agent);
    int64_t ts_http_get_max_idle_http_parsers();
    void ts_http_set_max_idle_http_parsers(int64_t max);
}
