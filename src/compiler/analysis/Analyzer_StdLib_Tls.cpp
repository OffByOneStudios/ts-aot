#include "Analyzer.h"

namespace ts {

void Analyzer::registerTls() {
    auto tlsType = std::make_shared<ObjectType>();

    // =========================================================================
    // TLSSocket class
    // Note: We don't extend Socket to avoid VTable generation issues.
    // TLSSocket instances are created by tls.connect() and are runtime objects.
    // =========================================================================
    auto tlsSocketClass = std::make_shared<ClassType>("TLSSocket");

    // Properties
    tlsSocketClass->fields["authorized"] = std::make_shared<Type>(TypeKind::Boolean);
    tlsSocketClass->fields["authorizationError"] = std::make_shared<Type>(TypeKind::String);
    tlsSocketClass->fields["encrypted"] = std::make_shared<Type>(TypeKind::Boolean);

    // getCertificate(): object | null
    auto getCertificateMethod = std::make_shared<FunctionType>();
    getCertificateMethod->returnType = std::make_shared<Type>(TypeKind::Any);
    tlsSocketClass->fields["getCertificate"] = getCertificateMethod;

    // getPeerCertificate(detailed?: boolean): object | null
    auto getPeerCertificateMethod = std::make_shared<FunctionType>();
    getPeerCertificateMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean));  // optional detailed
    getPeerCertificateMethod->returnType = std::make_shared<Type>(TypeKind::Any);
    tlsSocketClass->fields["getPeerCertificate"] = getPeerCertificateMethod;

    // getProtocol(): string | null
    auto getProtocolMethod = std::make_shared<FunctionType>();
    getProtocolMethod->returnType = std::make_shared<Type>(TypeKind::String);
    tlsSocketClass->fields["getProtocol"] = getProtocolMethod;

    // getSession(): Buffer | undefined
    auto getSessionMethod = std::make_shared<FunctionType>();
    auto bufferClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Buffer"));
    getSessionMethod->returnType = bufferClass;
    tlsSocketClass->fields["getSession"] = getSessionMethod;

    // renegotiate(options: object, callback: Function): boolean
    auto renegotiateMethod = std::make_shared<FunctionType>();
    renegotiateMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));      // options
    renegotiateMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback
    renegotiateMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    tlsSocketClass->fields["renegotiate"] = renegotiateMethod;

    // setMaxSendFragment(size: number): boolean
    auto setMaxSendFragmentMethod = std::make_shared<FunctionType>();
    setMaxSendFragmentMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setMaxSendFragmentMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    tlsSocketClass->fields["setMaxSendFragment"] = setMaxSendFragmentMethod;

    // =========================================================================
    // SNI (Server Name Indication) support
    // =========================================================================

    // setServername(hostname: string): void
    auto setServernameMethod = std::make_shared<FunctionType>();
    setServernameMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    setServernameMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    tlsSocketClass->fields["setServername"] = setServernameMethod;

    // getServername(): string | undefined
    auto getServernameMethod = std::make_shared<FunctionType>();
    getServernameMethod->returnType = std::make_shared<Type>(TypeKind::String);
    tlsSocketClass->fields["getServername"] = getServernameMethod;

    // servername property (alias for getServername result)
    tlsSocketClass->fields["servername"] = std::make_shared<Type>(TypeKind::String);

    // =========================================================================
    // ALPN (Application-Layer Protocol Negotiation) support
    // =========================================================================

    // setALPNProtocols(protocols: string[]): void
    auto setALPNProtocolsMethod = std::make_shared<FunctionType>();
    setALPNProtocolsMethod->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));
    setALPNProtocolsMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    tlsSocketClass->fields["setALPNProtocols"] = setALPNProtocolsMethod;

    // alpnProtocol property (getter): string | false
    tlsSocketClass->fields["alpnProtocol"] = std::make_shared<Type>(TypeKind::String);

    // =========================================================================
    // Session resumption support
    // =========================================================================

    // setSession(session: Buffer): void
    auto setSessionMethod = std::make_shared<FunctionType>();
    setSessionMethod->paramTypes.push_back(bufferClass);
    setSessionMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    tlsSocketClass->fields["setSession"] = setSessionMethod;

    // isSessionReused(): boolean
    auto isSessionReusedMethod = std::make_shared<FunctionType>();
    isSessionReusedMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    tlsSocketClass->fields["isSessionReused"] = isSessionReusedMethod;

    // =========================================================================
    // Client certificate support
    // =========================================================================

    // setClientCertificate(cert: Buffer, key: Buffer): void
    auto setClientCertificateMethod = std::make_shared<FunctionType>();
    setClientCertificateMethod->paramTypes.push_back(bufferClass);  // cert
    setClientCertificateMethod->paramTypes.push_back(bufferClass);  // key
    setClientCertificateMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    tlsSocketClass->fields["setClientCertificate"] = setClientCertificateMethod;

    symbols.defineType("TLSSocket", tlsSocketClass);

    // =========================================================================
    // SecureContext class (opaque wrapper for SSL_CTX)
    // =========================================================================
    auto secureContextClass = std::make_shared<ClassType>("SecureContext");
    symbols.defineType("SecureContext", secureContextClass);

    // =========================================================================
    // tls.createSecureContext(options): SecureContext
    // =========================================================================
    auto createSecureContextType = std::make_shared<FunctionType>();
    createSecureContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    createSecureContextType->returnType = secureContextClass;
    tlsType->fields["createSecureContext"] = createSecureContextType;

    // =========================================================================
    // tls.connect(options, callback?): TLSSocket
    // tls.connect(port, host?, options?, callback?): TLSSocket
    // =========================================================================
    auto connectType = std::make_shared<FunctionType>();
    connectType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));      // options or port
    connectType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));      // host or options
    connectType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));      // options
    connectType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback
    connectType->returnType = tlsSocketClass;
    tlsType->fields["connect"] = connectType;

    // =========================================================================
    // tls.createServer(options, secureConnectionListener?): Server
    // =========================================================================
    auto serverClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Server"));

    auto tlsConnectionCallback = std::make_shared<FunctionType>();
    tlsConnectionCallback->paramTypes.push_back(tlsSocketClass);

    auto createServerType = std::make_shared<FunctionType>();
    createServerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    createServerType->paramTypes.push_back(tlsConnectionCallback);                   // callback
    createServerType->returnType = serverClass;
    tlsType->fields["createServer"] = createServerType;

    // =========================================================================
    // tls.getCiphers(): string[]
    // =========================================================================
    auto getCiphersType = std::make_shared<FunctionType>();
    getCiphersType->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    tlsType->fields["getCiphers"] = getCiphersType;

    // =========================================================================
    // tls.DEFAULT_ECDH_CURVE: string
    // tls.DEFAULT_MAX_VERSION: string
    // tls.DEFAULT_MIN_VERSION: string
    // =========================================================================
    tlsType->fields["DEFAULT_ECDH_CURVE"] = std::make_shared<Type>(TypeKind::String);
    tlsType->fields["DEFAULT_MAX_VERSION"] = std::make_shared<Type>(TypeKind::String);
    tlsType->fields["DEFAULT_MIN_VERSION"] = std::make_shared<Type>(TypeKind::String);

    // =========================================================================
    // TLSSocket class export (for new TLSSocket())
    // =========================================================================
    auto tlsSocketStaticObj = std::make_shared<ObjectType>();
    tlsSocketStaticObj->fields["prototype"] = tlsSocketClass;
    tlsType->fields["TLSSocket"] = tlsSocketStaticObj;

    // =========================================================================
    // SecureContext class export
    // =========================================================================
    auto secureContextStaticObj = std::make_shared<ObjectType>();
    secureContextStaticObj->fields["prototype"] = secureContextClass;
    tlsType->fields["SecureContext"] = secureContextStaticObj;

    symbols.define("tls", tlsType);
}

} // namespace ts
