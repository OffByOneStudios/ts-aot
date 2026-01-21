#include "TsSecureSocket.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "GC.h"
#include <string.h>
#include <iostream>

TsSecureSocket::TsSecureSocket(bool is_server) : TsSocket(), ssl_ctx(nullptr), ssl(nullptr), rbio(nullptr), wbio(nullptr), handshake_done(false), is_server(is_server), sni_hostname(nullptr) {
    this->magic = 0x53534F43; // "SSOC"
    InitSSL();
}

TsSecureSocket::TsSecureSocket(uv_tcp_t* h, bool is_server) : TsSocket(h), ssl_ctx(nullptr), ssl(nullptr), rbio(nullptr), wbio(nullptr), handshake_done(false), is_server(is_server), sni_hostname(nullptr) {
    this->magic = 0x53534F43; // "SSOC"
    InitSSL();
}

TsSecureSocket::~TsSecureSocket() {
    if (ssl) SSL_free(ssl);
    if (ssl_ctx) SSL_CTX_free(ssl_ctx);
}

void TsSecureSocket::InitSSL() {
    if (is_server) {
        ssl_ctx = SSL_CTX_new(TLS_server_method());
    } else {
        ssl_ctx = SSL_CTX_new(TLS_client_method());
    }
    
    ssl = SSL_new(ssl_ctx);
    rbio = BIO_new(BIO_s_mem());
    wbio = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl, rbio, wbio);
    
    if (is_server) {
        SSL_set_accept_state(ssl);
    } else {
        SSL_set_connect_state(ssl);
    }
}

void TsSecureSocket::SetCertificate(const char* cert_path, const char* key_path) {
    if (SSL_CTX_use_certificate_file(ssl_ctx, cert_path, SSL_FILETYPE_PEM) <= 0) {
        // Handle error
    }
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        // Handle error
    }
}

void TsSecureSocket::SetCertificate(TsBuffer* cert, TsBuffer* key) {
    BIO* cert_bio = BIO_new_mem_buf(cert->GetData(), (int)cert->GetLength());
    X509* x509 = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
    if (!x509) {
        std::cerr << "Failed to parse certificate PEM" << std::endl;
        unsigned long err = ERR_get_error();
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        std::cerr << "OpenSSL error: " << buf << std::endl;
        BIO_free(cert_bio);
        return;
    }
    
    if (SSL_CTX_use_certificate(ssl_ctx, x509) <= 0) {
        std::cerr << "Failed to use certificate" << std::endl;
    }
    X509_free(x509);
    BIO_free(cert_bio);

    BIO* key_bio = BIO_new_mem_buf(key->GetData(), (int)key->GetLength());
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
    if (!pkey) {
        std::cerr << "Failed to parse private key PEM" << std::endl;
        unsigned long err = ERR_get_error();
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        std::cerr << "OpenSSL error: " << buf << std::endl;
        BIO_free(key_bio);
        return;
    }
    
    if (SSL_CTX_use_PrivateKey(ssl_ctx, pkey) <= 0) {
        std::cerr << "Failed to use private key" << std::endl;
    }
    EVP_PKEY_free(pkey);
    BIO_free(key_bio);
    
    // Recreate the SSL object from the updated context to pick up the certificates
    if (ssl) {
        SSL_free(ssl);
    }
    ssl = SSL_new(ssl_ctx);
    rbio = BIO_new(BIO_s_mem());
    wbio = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl, rbio, wbio);
    if (is_server) {
        SSL_set_accept_state(ssl);
    } else {
        SSL_set_connect_state(ssl);
    }
}

void TsSecureSocket::SetVerify(bool rejectUnauthorized, TsValue* ca) {
    if (rejectUnauthorized) {
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, nullptr);
    } else {
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, nullptr);
    }
    
    if (ca) {
        if (ca->type == ValueType::STRING_PTR) {
            TsString* sCa = (TsString*)ca->ptr_val;
            BIO* cbio = BIO_new_mem_buf(sCa->ToUtf8(), -1);
            X509_STORE* store = SSL_CTX_get_cert_store(ssl_ctx);
            X509* x509 = nullptr;
            while ((x509 = PEM_read_bio_X509(cbio, nullptr, nullptr, nullptr))) {
                X509_STORE_add_cert(store, x509);
                X509_free(x509);
            }
            BIO_free(cbio);
        } else if (ca->type == ValueType::OBJECT_PTR) {
            TsBuffer* bCa = (TsBuffer*)ca->ptr_val;
            BIO* cbio = BIO_new_mem_buf(bCa->GetData(), (int)bCa->GetLength());
            X509_STORE* store = SSL_CTX_get_cert_store(ssl_ctx);
            X509* x509 = nullptr;
            while ((x509 = PEM_read_bio_X509(cbio, nullptr, nullptr, nullptr))) {
                X509_STORE_add_cert(store, x509);
                X509_free(x509);
            }
            BIO_free(cbio);
        }
    }
}

void TsSecureSocket::Connect(const char* host, int port, void* callback) {
    TsSocket::Connect(host, port, callback);
}

void TsSecureSocket::OnConnected() {
    TsSocket::OnConnected();
    if (!is_server) {
        TryHandshake();
    }
}

void TsSecureSocket::HandleRead(ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) {
        BIO_write(rbio, buf->base, (int)nread);
        if (!handshake_done) {
            TryHandshake();
        } else {
            DecryptAndEmit();
        }
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            Emit("end", 0, nullptr);
        } else {
            Emit("error", 0, nullptr);
        }
        End();
    }
}

void TsSecureSocket::TryHandshake() {
    int ret = SSL_do_handshake(ssl);
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            char err_buf[256];
            ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
            std::cerr << "SSL Handshake Error: " << err_buf << " (ret=" << ret << ", err=" << err << ")" << std::endl;
            Emit("error", 0, nullptr);
            return;
        }
    }
    
    FlushWbio();

    if (SSL_is_init_finished(ssl)) {
        handshake_done = true;
        Emit("secureConnect", 0, nullptr);
        DecryptAndEmit(); // Process any data that might have arrived with the handshake
    }
}

void TsSecureSocket::DecryptAndEmit() {
    char buf[16384];
    int ret;
    while ((ret = SSL_read(ssl, buf, sizeof(buf))) > 0) {
        TsBuffer* chunk = TsBuffer::Create(ret);
        memcpy(chunk->GetData(), buf, ret);
        
        TsValue* arg0 = ts_value_make_object(chunk);
        void* args[] = { arg0 };
        Emit("data", 1, args);
    }
    
    int err = SSL_get_error(ssl, ret);
    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE && err != SSL_ERROR_ZERO_RETURN) {
        // Error or EOF
    }
    
    FlushWbio();
}

bool TsSecureSocket::Write(void* data, size_t length) {
    if (!handshake_done) {
        return false;
    }

    int ret = SSL_write(ssl, data, (int)length);
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        // Handle error
        return false;
    }

    FlushWbio();
    return true;
}

void TsSecureSocket::FlushWbio() {
    char buf[8192];
    int bytes;
    while ((bytes = BIO_read(wbio, buf, sizeof(buf))) > 0) {
        // We use the base TsSocket::Write to send the encrypted data over the wire
        // But TsSocket::Write expects raw data and handles buffering.
        // We should probably call uv_write directly to avoid double buffering or recursion if we were to override Write differently.
        // Actually, TsSocket::Write is virtual, so calling TsSocket::Write here would call the base implementation.
        
        // However, TsSocket::Write takes void* data and length.
        // We need to make sure we don't call our own Write.
        
        // Let's use a helper in TsSocket or just call uv_write.
        
        uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
        
        // We need a way to track this write for bufferedAmount if we want to be accurate,
        // but for now let's just send it.
        
        char* write_buf = (char*)ts_alloc(bytes);
        memcpy(write_buf, buf, bytes);
        uv_buf_t uv_buf = uv_buf_init(write_buf, bytes);
        
        uv_write(req, (uv_stream_t*)handle, &uv_buf, 1, [](uv_write_t* req, int status) {
            if (status < 0) {
                // std::cerr << "uv_write failed in FlushWbio: " << uv_strerror(status) << std::endl;
            }
        });
    }
}

void TsSecureSocket::HandleWrite(int status, size_t length) {
    // This is called when the underlying TCP write completes.
    // We don't necessarily need to do anything here for SSL.
    TsSocket::HandleWrite(status, length);
}

void TsSecureSocket::End() {
    if (ssl && handshake_done) {
        SSL_shutdown(ssl);
        FlushWbio();
    }
    TsSocket::End();
}

// ============================================================================
// TLSSocket property implementations
// ============================================================================

bool TsSecureSocket::IsAuthorized() const {
    if (!ssl) return false;
    long result = SSL_get_verify_result(ssl);
    return result == X509_V_OK;
}

TsString* TsSecureSocket::GetAuthorizationError() const {
    if (!ssl) return TsString::Create("");
    long result = SSL_get_verify_result(ssl);
    if (result == X509_V_OK) {
        return TsString::Create("");
    }
    const char* errStr = X509_verify_cert_error_string(result);
    return TsString::Create(errStr ? errStr : "Unknown error");
}

TsObject* TsSecureSocket::GetCertificate() const {
    if (!ssl) return nullptr;
    X509* cert = SSL_get_certificate(ssl);
    if (!cert) return nullptr;

    // Create a simple object with certificate info
    TsMap* obj = TsMap::Create();

    // Get subject
    X509_NAME* subjectName = X509_get_subject_name(cert);
    if (subjectName) {
        char buf[256];
        X509_NAME_oneline(subjectName, buf, sizeof(buf));
        obj->Set(*ts_value_make_string(TsString::Create("subject")), *ts_value_make_string(TsString::Create(buf)));
    }

    // Get issuer
    X509_NAME* issuerName = X509_get_issuer_name(cert);
    if (issuerName) {
        char buf[256];
        X509_NAME_oneline(issuerName, buf, sizeof(buf));
        obj->Set(*ts_value_make_string(TsString::Create("issuer")), *ts_value_make_string(TsString::Create(buf)));
    }

    return obj;
}

TsObject* TsSecureSocket::GetPeerCertificate(bool detailed) const {
    if (!ssl) return nullptr;
    X509* cert = SSL_get_peer_certificate(ssl);
    if (!cert) return nullptr;

    // Create a simple object with certificate info
    TsMap* obj = TsMap::Create();

    // Get subject
    X509_NAME* subjectName = X509_get_subject_name(cert);
    if (subjectName) {
        char buf[256];
        X509_NAME_oneline(subjectName, buf, sizeof(buf));
        obj->Set(*ts_value_make_string(TsString::Create("subject")), *ts_value_make_string(TsString::Create(buf)));
    }

    // Get issuer
    X509_NAME* issuerName = X509_get_issuer_name(cert);
    if (issuerName) {
        char buf[256];
        X509_NAME_oneline(issuerName, buf, sizeof(buf));
        obj->Set(*ts_value_make_string(TsString::Create("issuer")), *ts_value_make_string(TsString::Create(buf)));
    }

    // Get validity dates
    ASN1_TIME* notBefore = X509_get_notBefore(cert);
    ASN1_TIME* notAfter = X509_get_notAfter(cert);
    if (notBefore) {
        BIO* bio = BIO_new(BIO_s_mem());
        ASN1_TIME_print(bio, notBefore);
        char dateBuf[64];
        int len = BIO_read(bio, dateBuf, sizeof(dateBuf) - 1);
        dateBuf[len > 0 ? len : 0] = '\0';
        BIO_free(bio);
        obj->Set(*ts_value_make_string(TsString::Create("valid_from")), *ts_value_make_string(TsString::Create(dateBuf)));
    }
    if (notAfter) {
        BIO* bio = BIO_new(BIO_s_mem());
        ASN1_TIME_print(bio, notAfter);
        char dateBuf[64];
        int len = BIO_read(bio, dateBuf, sizeof(dateBuf) - 1);
        dateBuf[len > 0 ? len : 0] = '\0';
        BIO_free(bio);
        obj->Set(*ts_value_make_string(TsString::Create("valid_to")), *ts_value_make_string(TsString::Create(dateBuf)));
    }

    X509_free(cert);  // We own this cert, must free
    return obj;
}

TsString* TsSecureSocket::GetProtocol() const {
    if (!ssl) return TsString::Create("");
    const char* proto = SSL_get_version(ssl);
    return TsString::Create(proto ? proto : "");
}

TsBuffer* TsSecureSocket::GetSession() const {
    if (!ssl) return nullptr;
    SSL_SESSION* session = SSL_get_session(ssl);
    if (!session) return nullptr;

    // Serialize session to buffer
    int len = i2d_SSL_SESSION(session, nullptr);
    if (len <= 0) return nullptr;

    TsBuffer* buf = TsBuffer::Create(len);
    unsigned char* data = buf->GetData();
    i2d_SSL_SESSION(session, &data);
    return buf;
}

bool TsSecureSocket::Renegotiate(void* options, void* callback) {
    if (!ssl || !handshake_done) return false;

    // Note: TLS 1.3 doesn't support renegotiation
    int ret = SSL_renegotiate(ssl);
    if (ret <= 0) return false;

    // Do the handshake
    ret = SSL_do_handshake(ssl);
    FlushWbio();

    return ret > 0;
}

bool TsSecureSocket::SetMaxSendFragment(size_t size) {
    if (!ssl) return false;
    // Valid range is 512 to 16384
    if (size < 512 || size > 16384) return false;
    return SSL_set_max_send_fragment(ssl, size) == 1;
}

// ============================================================================
// SNI (Server Name Indication) support
// ============================================================================

void TsSecureSocket::SetServername(const char* hostname) {
    if (!ssl || !hostname) return;

    // Store hostname for later retrieval
    if (sni_hostname) {
        // Free old hostname if set
        // Note: Using ts_alloc, no explicit free needed with GC
    }
    size_t len = strlen(hostname) + 1;
    sni_hostname = (char*)ts_alloc(len);
    memcpy(sni_hostname, hostname, len);

    // Set SNI on the SSL connection
    SSL_set_tlsext_host_name(ssl, hostname);
}

TsString* TsSecureSocket::GetServername() const {
    if (!ssl) return nullptr;

    // For client, return the hostname we set
    if (!is_server && sni_hostname) {
        return TsString::Create(sni_hostname);
    }

    // For server, get the SNI sent by client
    const char* servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (servername) {
        return TsString::Create(servername);
    }
    return nullptr;
}

// ============================================================================
// ALPN (Application-Layer Protocol Negotiation) support
// ============================================================================

void TsSecureSocket::SetALPNProtocols(TsArray* protocols) {
    if (!ssl_ctx || !protocols) return;

    // Build ALPN wire format: [len][proto][len][proto]...
    // First, calculate total length
    size_t totalLen = 0;
    int64_t count = protocols->Length();
    for (int64_t i = 0; i < count; i++) {
        TsValue* val = ts_array_get_as_value(protocols, i);
        if (val && val->type == ValueType::STRING_PTR) {
            TsString* str = (TsString*)val->ptr_val;
            const char* utf8 = str->ToUtf8();
            size_t len = strlen(utf8);
            if (len <= 255) {  // ALPN protocol name max length
                totalLen += 1 + len;
            }
        }
    }

    if (totalLen == 0) return;

    // Build the buffer
    unsigned char* alpn = (unsigned char*)ts_alloc(totalLen);
    size_t offset = 0;
    for (int64_t i = 0; i < count; i++) {
        TsValue* val = ts_array_get_as_value(protocols, i);
        if (val && val->type == ValueType::STRING_PTR) {
            TsString* str = (TsString*)val->ptr_val;
            const char* utf8 = str->ToUtf8();
            size_t len = strlen(utf8);
            if (len <= 255) {
                alpn[offset++] = (unsigned char)len;
                memcpy(alpn + offset, utf8, len);
                offset += len;
            }
        }
    }

    // Set ALPN protocols on context (for client)
    SSL_CTX_set_alpn_protos(ssl_ctx, alpn, (unsigned int)totalLen);
}

TsString* TsSecureSocket::GetALPNProtocol() const {
    if (!ssl) return nullptr;

    const unsigned char* alpn = nullptr;
    unsigned int alpnLen = 0;
    SSL_get0_alpn_selected(ssl, &alpn, &alpnLen);

    if (alpn && alpnLen > 0) {
        char* str = (char*)ts_alloc(alpnLen + 1);
        memcpy(str, alpn, alpnLen);
        str[alpnLen] = '\0';
        return TsString::Create(str);
    }
    return nullptr;
}

// ============================================================================
// Session resumption support
// ============================================================================

void TsSecureSocket::SetSession(TsBuffer* session) {
    if (!ssl || !session) return;

    const unsigned char* data = session->GetData();
    size_t len = session->GetLength();

    // Deserialize session
    SSL_SESSION* sess = d2i_SSL_SESSION(nullptr, &data, (long)len);
    if (sess) {
        SSL_set_session(ssl, sess);
        SSL_SESSION_free(sess);
    }
}

bool TsSecureSocket::IsSessionReused() const {
    if (!ssl) return false;
    return SSL_session_reused(ssl) == 1;
}

// ============================================================================
// Client certificate support
// ============================================================================

void TsSecureSocket::SetClientCertificate(TsBuffer* cert, TsBuffer* key) {
    if (!ssl_ctx || !cert || !key) return;

    // Parse certificate
    BIO* cert_bio = BIO_new_mem_buf(cert->GetData(), (int)cert->GetLength());
    X509* x509 = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
    BIO_free(cert_bio);

    if (!x509) {
        std::cerr << "Failed to parse client certificate PEM" << std::endl;
        return;
    }

    // Parse private key
    BIO* key_bio = BIO_new_mem_buf(key->GetData(), (int)key->GetLength());
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
    BIO_free(key_bio);

    if (!pkey) {
        std::cerr << "Failed to parse client private key PEM" << std::endl;
        X509_free(x509);
        return;
    }

    // Set client certificate and key
    if (SSL_CTX_use_certificate(ssl_ctx, x509) <= 0) {
        std::cerr << "Failed to use client certificate" << std::endl;
    }
    if (SSL_CTX_use_PrivateKey(ssl_ctx, pkey) <= 0) {
        std::cerr << "Failed to use client private key" << std::endl;
    }

    X509_free(x509);
    EVP_PKEY_free(pkey);

    // Recreate the SSL object to pick up the client cert
    if (ssl) {
        SSL_free(ssl);
    }
    ssl = SSL_new(ssl_ctx);
    rbio = BIO_new(BIO_s_mem());
    wbio = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl, rbio, wbio);
    if (is_server) {
        SSL_set_accept_state(ssl);
    } else {
        SSL_set_connect_state(ssl);
    }
}

// ============================================================================
// Extern "C" API implementations
// ============================================================================

extern "C" {

void* ts_tls_create_secure_context(void* options) {
    // Create a new SSL_CTX and wrap it
    // For now, return a simple wrapper object
    SSL_CTX* ctx = SSL_CTX_new(TLS_method());
    if (!ctx) return nullptr;

    // Process options if provided
    if (options) {
        TsValue* opts = (TsValue*)options;
        void* rawOpts = ts_value_get_object(opts);
        if (!rawOpts) rawOpts = options;

        // TODO: Extract key, cert, ca from options and configure ctx
    }

    // Return boxed pointer to SSL_CTX
    return ts_value_make_object(ctx);
}

void* ts_tls_connect(void* options, void* callback) {
    // Extract options
    TsValue* opts = (TsValue*)options;
    void* rawOpts = ts_value_get_object(opts);
    if (!rawOpts) rawOpts = options;

    // Create a new secure socket
    void* mem = ts_alloc(sizeof(TsSecureSocket));
    TsSecureSocket* socket = new (mem) TsSecureSocket(false);

    // Extract host and port from options
    TsObject* optsObj = dynamic_cast<TsObject*>((TsObject*)rawOpts);
    if (optsObj) {
        TsValue* hostVal = (TsValue*)ts_object_get_property(optsObj, "host");
        TsValue* portVal = (TsValue*)ts_object_get_property(optsObj, "port");

        const char* host = "localhost";
        int port = 443;

        if (hostVal && hostVal->type == ValueType::STRING_PTR) {
            host = ((TsString*)hostVal->ptr_val)->ToUtf8();
        }
        if (portVal && portVal->type == ValueType::NUMBER_INT) {
            port = (int)portVal->i_val;
        }

        // Set verification options
        TsValue* rejectVal = (TsValue*)ts_object_get_property(optsObj, "rejectUnauthorized");
        bool reject = true;
        if (rejectVal && rejectVal->type == ValueType::BOOLEAN) {
            reject = rejectVal->b_val;
        }
        socket->SetVerify(reject, nullptr);

        // Connect
        socket->Connect(host, port, callback);
    }

    return ts_value_make_object(socket);
}

void* ts_tls_create_server(void* options, void* callback) {
    // For now, delegate to HTTPS server creation mechanism
    // This would need proper TLS server implementation
    return nullptr;
}

void* ts_tls_get_ciphers() {
    // Get list of available ciphers
    SSL_CTX* ctx = SSL_CTX_new(TLS_method());
    if (!ctx) return ts_value_make_object(TsArray::Create());

    TsArray* arr = TsArray::Create();

    STACK_OF(SSL_CIPHER)* ciphers = SSL_CTX_get_ciphers(ctx);
    if (ciphers) {
        int count = sk_SSL_CIPHER_num(ciphers);
        for (int i = 0; i < count; i++) {
            const SSL_CIPHER* cipher = sk_SSL_CIPHER_value(ciphers, i);
            const char* name = SSL_CIPHER_get_name(cipher);
            if (name) {
                ts_array_push(arr, ts_value_make_string(TsString::Create(name)));
            }
        }
    }

    SSL_CTX_free(ctx);
    return ts_value_make_object(arr);
}

bool ts_secure_socket_get_authorized(void* socket) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return false;
    return s->IsAuthorized();
}

void* ts_secure_socket_get_authorization_error(void* socket) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return ts_value_make_string(TsString::Create(""));
    return ts_value_make_string(s->GetAuthorizationError());
}

bool ts_secure_socket_get_encrypted(void* socket) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return false;
    return s->IsEncrypted();
}

void* ts_secure_socket_get_certificate(void* socket) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return ts_value_make_object(nullptr);
    TsObject* cert = s->GetCertificate();
    return cert ? ts_value_make_object(cert) : ts_value_make_object(nullptr);
}

void* ts_secure_socket_get_peer_certificate(void* socket, bool detailed) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return ts_value_make_object(nullptr);
    TsObject* cert = s->GetPeerCertificate(detailed);
    return cert ? ts_value_make_object(cert) : ts_value_make_object(nullptr);
}

void* ts_secure_socket_get_protocol(void* socket) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return ts_value_make_string(TsString::Create(""));
    return ts_value_make_string(s->GetProtocol());
}

void* ts_secure_socket_get_session(void* socket) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return ts_value_make_object(nullptr);
    TsBuffer* session = s->GetSession();
    return session ? ts_value_make_object(session) : ts_value_make_object(nullptr);
}

bool ts_secure_socket_renegotiate(void* socket, void* options, void* callback) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return false;
    return s->Renegotiate(options, callback);
}

bool ts_secure_socket_set_max_send_fragment(void* socket, int64_t size) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return false;
    return s->SetMaxSendFragment((size_t)size);
}

// ============================================================================
// SNI API
// ============================================================================

void ts_secure_socket_set_servername(void* socket, void* hostname) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return;

    // Get hostname string
    TsValue* hostnameVal = (TsValue*)hostname;
    void* rawHostname = ts_value_get_object(hostnameVal);
    if (!rawHostname) rawHostname = hostname;

    TsString* str = dynamic_cast<TsString*>((TsObject*)rawHostname);
    if (str) {
        s->SetServername(str->ToUtf8());
    }
}

void* ts_secure_socket_get_servername(void* socket) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return ts_value_make_object(nullptr);

    TsString* name = s->GetServername();
    return name ? ts_value_make_string(name) : ts_value_make_object(nullptr);
}

// ============================================================================
// ALPN API
// ============================================================================

void ts_secure_socket_set_alpn_protocols(void* socket, void* protocols) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return;

    // Get protocols array
    TsValue* protocolsVal = (TsValue*)protocols;
    void* rawProtocols = ts_value_get_object(protocolsVal);
    if (!rawProtocols) rawProtocols = protocols;

    TsArray* arr = dynamic_cast<TsArray*>((TsObject*)rawProtocols);
    if (arr) {
        s->SetALPNProtocols(arr);
    }
}

void* ts_secure_socket_get_alpn_protocol(void* socket) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return ts_value_make_object(nullptr);

    TsString* proto = s->GetALPNProtocol();
    return proto ? ts_value_make_string(proto) : ts_value_make_object(nullptr);
}

// ============================================================================
// Session resumption API
// ============================================================================

void ts_secure_socket_set_session(void* socket, void* session) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return;

    // Get session buffer
    TsValue* sessionVal = (TsValue*)session;
    void* rawSession = ts_value_get_object(sessionVal);
    if (!rawSession) rawSession = session;

    TsBuffer* buf = dynamic_cast<TsBuffer*>((TsObject*)rawSession);
    if (buf) {
        s->SetSession(buf);
    }
}

bool ts_secure_socket_is_session_reused(void* socket) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return false;
    return s->IsSessionReused();
}

// ============================================================================
// Client certificate API
// ============================================================================

void ts_secure_socket_set_client_certificate(void* socket, void* cert, void* key) {
    TsValue* val = (TsValue*)socket;
    void* rawPtr = ts_value_get_object(val);
    if (!rawPtr) rawPtr = socket;

    TsSecureSocket* s = dynamic_cast<TsSecureSocket*>((TsObject*)rawPtr);
    if (!s) return;

    // Get certificate buffer
    TsValue* certVal = (TsValue*)cert;
    void* rawCert = ts_value_get_object(certVal);
    if (!rawCert) rawCert = cert;
    TsBuffer* certBuf = dynamic_cast<TsBuffer*>((TsObject*)rawCert);

    // Get key buffer
    TsValue* keyVal = (TsValue*)key;
    void* rawKey = ts_value_get_object(keyVal);
    if (!rawKey) rawKey = key;
    TsBuffer* keyBuf = dynamic_cast<TsBuffer*>((TsObject*)rawKey);

    if (certBuf && keyBuf) {
        s->SetClientCertificate(certBuf, keyBuf);
    }
}

} // extern "C"
