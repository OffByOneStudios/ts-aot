#pragma once
#include "TsSocket.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

class TsBuffer;

class TsSecureSocket : public TsSocket {
public:
    TsSecureSocket(bool is_server = false);
    TsSecureSocket(uv_tcp_t* handle, bool is_server = false);
    virtual ~TsSecureSocket();

    void Connect(const char* host, int port, void* callback);
    
    void SetCertificate(const char* cert_path, const char* key_path);
    void SetCertificate(TsBuffer* cert, TsBuffer* key);
    void SetVerify(bool rejectUnauthorized, TsValue* ca = nullptr);

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    // TLSSocket property getters
    bool IsAuthorized() const;
    TsString* GetAuthorizationError() const;
    bool IsEncrypted() const { return true; }
    TsObject* GetCertificate() const;
    TsObject* GetPeerCertificate(bool detailed = false) const;
    TsString* GetProtocol() const;

    // TLSSocket methods
    TsBuffer* GetSession() const;
    bool Renegotiate(void* options, void* callback);
    bool SetMaxSendFragment(size_t size);

    // SNI (Server Name Indication)
    void SetServername(const char* hostname);
    TsString* GetServername() const;

    // ALPN (Application-Layer Protocol Negotiation)
    void SetALPNProtocols(TsArray* protocols);
    TsString* GetALPNProtocol() const;

    // Session resumption
    void SetSession(TsBuffer* session);
    bool IsSessionReused() const;

    // Client certificate
    void SetClientCertificate(TsBuffer* cert, TsBuffer* key);

protected:
    virtual void OnConnected() override;
    virtual void HandleRead(ssize_t nread, const uv_buf_t* buf) override;
    virtual void HandleWrite(int status, size_t length) override;

private:
    SSL_CTX* ssl_ctx;
    SSL* ssl;
    BIO* rbio;
    BIO* wbio;
    bool handshake_done;
    bool is_server;
    char* sni_hostname;  // SNI hostname for client connections

    void InitSSL();
    void FlushWbio();
    void TryHandshake();
    void DecryptAndEmit();
};
