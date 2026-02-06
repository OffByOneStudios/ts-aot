#include "Analyzer.h"

namespace ts {

void Analyzer::registerHTTP2() {
    // Get base classes we may reference
    auto socketClass = symbols.lookupType("Socket");
    auto bufferClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Buffer"));

    // =========================================================================
    // Http2Settings type (object with settings properties)
    // =========================================================================
    auto http2SettingsType = std::make_shared<ObjectType>();
    http2SettingsType->fields["headerTableSize"] = std::make_shared<Type>(TypeKind::Int);
    http2SettingsType->fields["enablePush"] = std::make_shared<Type>(TypeKind::Boolean);
    http2SettingsType->fields["maxConcurrentStreams"] = std::make_shared<Type>(TypeKind::Int);
    http2SettingsType->fields["initialWindowSize"] = std::make_shared<Type>(TypeKind::Int);
    http2SettingsType->fields["maxFrameSize"] = std::make_shared<Type>(TypeKind::Int);
    http2SettingsType->fields["maxHeaderListSize"] = std::make_shared<Type>(TypeKind::Int);

    // =========================================================================
    // Http2Stream class
    // Note: We don't set baseClass or methods to avoid VTable generation.
    // Methods are handled via tryGenerateHTTP2Call in codegen.
    // =========================================================================
    auto http2StreamClass = std::make_shared<ClassType>("Http2Stream");
    // Properties only - no methods to avoid VTable generation
    http2StreamClass->fields["aborted"] = std::make_shared<Type>(TypeKind::Boolean);
    http2StreamClass->fields["bufferSize"] = std::make_shared<Type>(TypeKind::Int);
    http2StreamClass->fields["closed"] = std::make_shared<Type>(TypeKind::Boolean);
    http2StreamClass->fields["destroyed"] = std::make_shared<Type>(TypeKind::Boolean);
    http2StreamClass->fields["endAfterHeaders"] = std::make_shared<Type>(TypeKind::Boolean);
    http2StreamClass->fields["id"] = std::make_shared<Type>(TypeKind::Int);
    http2StreamClass->fields["pending"] = std::make_shared<Type>(TypeKind::Boolean);
    http2StreamClass->fields["rstCode"] = std::make_shared<Type>(TypeKind::Int);
    http2StreamClass->fields["sentHeaders"] = std::make_shared<Type>(TypeKind::Any);
    http2StreamClass->fields["sentInfoHeaders"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
    http2StreamClass->fields["sentTrailers"] = std::make_shared<Type>(TypeKind::Any);

    // =========================================================================
    // ServerHttp2Stream class
    // =========================================================================
    auto serverHttp2StreamClass = std::make_shared<ClassType>("ServerHttp2Stream");
    // Properties only
    serverHttp2StreamClass->fields["headersSent"] = std::make_shared<Type>(TypeKind::Boolean);
    serverHttp2StreamClass->fields["pushAllowed"] = std::make_shared<Type>(TypeKind::Boolean);

    symbols.defineType("ServerHttp2Stream", serverHttp2StreamClass);

    // =========================================================================
    // ClientHttp2Stream class
    // =========================================================================
    auto clientHttp2StreamClass = std::make_shared<ClassType>("ClientHttp2Stream");
    // No additional properties - events only

    symbols.defineType("ClientHttp2Stream", clientHttp2StreamClass);
    symbols.defineType("Http2Stream", http2StreamClass);

    // =========================================================================
    // Http2Session class
    // Note: We don't set baseClass or methods to avoid VTable generation.
    // Methods are handled via tryGenerateHTTP2Call in codegen.
    // =========================================================================
    auto http2SessionClass = std::make_shared<ClassType>("Http2Session");
    // Properties
    http2SessionClass->fields["alpnProtocol"] = std::make_shared<Type>(TypeKind::String);
    http2SessionClass->fields["closed"] = std::make_shared<Type>(TypeKind::Boolean);
    http2SessionClass->fields["connecting"] = std::make_shared<Type>(TypeKind::Boolean);
    http2SessionClass->fields["destroyed"] = std::make_shared<Type>(TypeKind::Boolean);
    http2SessionClass->fields["encrypted"] = std::make_shared<Type>(TypeKind::Boolean);
    http2SessionClass->fields["localSettings"] = http2SettingsType;
    http2SessionClass->fields["remoteSettings"] = http2SettingsType;
    http2SessionClass->fields["socket"] = socketClass ? socketClass : std::make_shared<Type>(TypeKind::Any);
    http2SessionClass->fields["type"] = std::make_shared<Type>(TypeKind::Int);

    // Methods (types for codegen dispatch, actual implementations in runtime)
    auto closeMethod = std::make_shared<FunctionType>();
    closeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // callback
    closeMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    http2SessionClass->fields["close"] = closeMethod;

    auto destroyMethod = std::make_shared<FunctionType>();
    destroyMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // error
    destroyMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // code
    destroyMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    http2SessionClass->fields["destroy"] = destroyMethod;

    auto goawayMethod = std::make_shared<FunctionType>();
    goawayMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // code
    goawayMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // lastStreamId
    goawayMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // data
    goawayMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    http2SessionClass->fields["goaway"] = goawayMethod;

    auto pingMethod = std::make_shared<FunctionType>();
    pingMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // payload
    pingMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // callback
    pingMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    http2SessionClass->fields["ping"] = pingMethod;

    auto refMethod = std::make_shared<FunctionType>();
    refMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    http2SessionClass->fields["ref"] = refMethod;

    auto unrefMethod = std::make_shared<FunctionType>();
    unrefMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    http2SessionClass->fields["unref"] = unrefMethod;

    auto setTimeoutMethod = std::make_shared<FunctionType>();
    setTimeoutMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // msecs
    setTimeoutMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // callback
    setTimeoutMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    http2SessionClass->fields["setTimeout"] = setTimeoutMethod;

    auto settingsMethod = std::make_shared<FunctionType>();
    settingsMethod->paramTypes.push_back(http2SettingsType); // settings
    settingsMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    http2SessionClass->fields["settings"] = settingsMethod;

    symbols.defineType("Http2Session", http2SessionClass);

    // Update Http2Stream to reference Http2Session
    http2StreamClass->fields["session"] = http2SessionClass;

    // =========================================================================
    // ServerHttp2Session class (inherits Http2Session properties and methods)
    // =========================================================================
    auto serverHttp2SessionClass = std::make_shared<ClassType>("ServerHttp2Session");
    // Copy Http2Session properties and methods
    for (const auto& [name, type] : http2SessionClass->fields) {
        serverHttp2SessionClass->fields[name] = type;
    }
    // Server-specific methods
    auto altsvcMethod = std::make_shared<FunctionType>();
    altsvcMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // alt
    altsvcMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // origin
    altsvcMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    serverHttp2SessionClass->fields["altsvc"] = altsvcMethod;

    auto originMethod = std::make_shared<FunctionType>();
    originMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // origins array
    originMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    serverHttp2SessionClass->fields["origin"] = originMethod;

    symbols.defineType("ServerHttp2Session", serverHttp2SessionClass);

    // =========================================================================
    // ClientHttp2Session class (inherits Http2Session properties and methods)
    // =========================================================================
    auto clientHttp2SessionClass = std::make_shared<ClassType>("ClientHttp2Session");
    // Copy Http2Session properties and methods
    for (const auto& [name, type] : http2SessionClass->fields) {
        clientHttp2SessionClass->fields[name] = type;
    }
    // Client-specific methods
    auto requestMethod = std::make_shared<FunctionType>();
    requestMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // headers
    requestMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    requestMethod->returnType = std::make_shared<ClassType>("ClientHttp2Stream");
    clientHttp2SessionClass->fields["request"] = requestMethod;

    symbols.defineType("ClientHttp2Session", clientHttp2SessionClass);

    // =========================================================================
    // Http2Server class
    // =========================================================================
    auto http2ServerClass = std::make_shared<ClassType>("Http2Server");
    symbols.defineType("Http2Server", http2ServerClass);

    // =========================================================================
    // Http2SecureServer class
    // =========================================================================
    auto http2SecureServerClass = std::make_shared<ClassType>("Http2SecureServer");
    symbols.defineType("Http2SecureServer", http2SecureServerClass);

    // =========================================================================
    // http2 module
    // =========================================================================
    auto http2Module = std::make_shared<ObjectType>();

    // createServer(options?, onRequestHandler?) -> Http2Server
    auto createServerType = std::make_shared<FunctionType>();
    createServerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    auto streamCallback = std::make_shared<FunctionType>();
    streamCallback->paramTypes.push_back(serverHttp2StreamClass);
    streamCallback->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // headers
    streamCallback->returnType = std::make_shared<Type>(TypeKind::Void);
    createServerType->paramTypes.push_back(streamCallback);
    createServerType->returnType = http2ServerClass;
    http2Module->fields["createServer"] = createServerType;

    // createSecureServer(options, onRequestHandler?) -> Http2SecureServer
    auto createSecureServerType = std::make_shared<FunctionType>();
    createSecureServerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    createSecureServerType->paramTypes.push_back(streamCallback);
    createSecureServerType->returnType = http2SecureServerClass;
    http2Module->fields["createSecureServer"] = createSecureServerType;

    // connect(authority, options?, listener?) -> ClientHttp2Session
    auto connectType = std::make_shared<FunctionType>();
    connectType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // authority
    connectType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    connectType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // listener
    connectType->returnType = clientHttp2SessionClass;
    http2Module->fields["connect"] = connectType;

    // getDefaultSettings() -> Http2Settings
    auto getDefaultSettingsType = std::make_shared<FunctionType>();
    getDefaultSettingsType->returnType = http2SettingsType;
    http2Module->fields["getDefaultSettings"] = getDefaultSettingsType;

    // getPackedSettings(settings) -> Buffer
    auto getPackedSettingsType = std::make_shared<FunctionType>();
    getPackedSettingsType->paramTypes.push_back(http2SettingsType);
    getPackedSettingsType->returnType = bufferClass ? std::static_pointer_cast<Type>(bufferClass) : std::make_shared<Type>(TypeKind::Any);
    http2Module->fields["getPackedSettings"] = getPackedSettingsType;

    // getUnpackedSettings(buffer) -> Http2Settings
    auto getUnpackedSettingsType = std::make_shared<FunctionType>();
    getUnpackedSettingsType->paramTypes.push_back(bufferClass ? std::static_pointer_cast<Type>(bufferClass) : std::make_shared<Type>(TypeKind::Any));
    getUnpackedSettingsType->returnType = http2SettingsType;
    http2Module->fields["getUnpackedSettings"] = getUnpackedSettingsType;

    // sensitiveHeaders symbol
    http2Module->fields["sensitiveHeaders"] = std::make_shared<Type>(TypeKind::Any);

    // constants object
    auto constantsType = std::make_shared<ObjectType>();
    // Error codes
    constantsType->fields["NGHTTP2_NO_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_PROTOCOL_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_INTERNAL_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_FLOW_CONTROL_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_SETTINGS_TIMEOUT"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_STREAM_CLOSED"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_FRAME_SIZE_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_REFUSED_STREAM"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_CANCEL"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_COMPRESSION_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_CONNECT_ERROR"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_ENHANCE_YOUR_CALM"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_INADEQUATE_SECURITY"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_HTTP_1_1_REQUIRED"] = std::make_shared<Type>(TypeKind::Int);
    // Settings identifiers
    constantsType->fields["NGHTTP2_SETTINGS_HEADER_TABLE_SIZE"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_SETTINGS_ENABLE_PUSH"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_SETTINGS_MAX_FRAME_SIZE"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE"] = std::make_shared<Type>(TypeKind::Int);
    // Session types
    constantsType->fields["NGHTTP2_SESSION_SERVER"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["NGHTTP2_SESSION_CLIENT"] = std::make_shared<Type>(TypeKind::Int);
    // Default settings values
    constantsType->fields["DEFAULT_SETTINGS_HEADER_TABLE_SIZE"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["DEFAULT_SETTINGS_ENABLE_PUSH"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["DEFAULT_SETTINGS_MAX_CONCURRENT_STREAMS"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["DEFAULT_SETTINGS_INITIAL_WINDOW_SIZE"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["DEFAULT_SETTINGS_MAX_FRAME_SIZE"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["DEFAULT_SETTINGS_MAX_HEADER_LIST_SIZE"] = std::make_shared<Type>(TypeKind::Int);
    http2Module->fields["constants"] = constantsType;

    symbols.define("http2", http2Module);
}

} // namespace ts
