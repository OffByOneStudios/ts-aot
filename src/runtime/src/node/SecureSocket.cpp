#include "TsSecureSocket.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "GC.h"
#include <string.h>
#include <iostream>

TsSecureSocket::TsSecureSocket(bool is_server) : TsSocket(), ssl_ctx(nullptr), ssl(nullptr), rbio(nullptr), wbio(nullptr), handshake_done(false), is_server(is_server) {
    this->magic = 0x53534F43; // "SSOC"
    InitSSL();
}

TsSecureSocket::TsSecureSocket(uv_tcp_t* h, bool is_server) : TsSocket(h), ssl_ctx(nullptr), ssl(nullptr), rbio(nullptr), wbio(nullptr), handshake_done(false), is_server(is_server) {
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
