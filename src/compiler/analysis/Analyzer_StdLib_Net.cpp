#include "Analyzer.h"

namespace ts {

void Analyzer::registerNet() {
    auto netModule = std::make_shared<ObjectType>();
    
    auto socketClass = std::make_shared<ClassType>("Socket");
    // Socket inherits from Duplex
    auto duplexClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Duplex"));
    socketClass->baseClass = duplexClass;

    auto connectMethod = std::make_shared<FunctionType>();
    connectMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // port
    connectMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // host
    connectMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback
    socketClass->methods["connect"] = connectMethod;

    // Socket address properties
    socketClass->fields["remoteAddress"] = std::make_shared<Type>(TypeKind::String);
    socketClass->fields["remotePort"] = std::make_shared<Type>(TypeKind::Int);
    socketClass->fields["remoteFamily"] = std::make_shared<Type>(TypeKind::String);
    socketClass->fields["localAddress"] = std::make_shared<Type>(TypeKind::String);
    socketClass->fields["localPort"] = std::make_shared<Type>(TypeKind::Int);
    socketClass->fields["localFamily"] = std::make_shared<Type>(TypeKind::String);

    // Socket state properties
    socketClass->fields["bytesRead"] = std::make_shared<Type>(TypeKind::Int);
    socketClass->fields["bytesWritten"] = std::make_shared<Type>(TypeKind::Int);
    socketClass->fields["connecting"] = std::make_shared<Type>(TypeKind::Boolean);
    socketClass->fields["destroyed"] = std::make_shared<Type>(TypeKind::Boolean);
    socketClass->fields["pending"] = std::make_shared<Type>(TypeKind::Boolean);
    socketClass->fields["readyState"] = std::make_shared<Type>(TypeKind::String);

    // Socket configuration methods
    // socket.setTimeout(msecs, callback?) -> socket
    auto setTimeoutMethod = std::make_shared<FunctionType>();
    setTimeoutMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // msecs
    setTimeoutMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback (optional)
    setTimeoutMethod->returnType = socketClass;
    socketClass->methods["setTimeout"] = setTimeoutMethod;

    // socket.setNoDelay(noDelay?) -> socket
    auto setNoDelayMethod = std::make_shared<FunctionType>();
    setNoDelayMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // noDelay (optional, defaults to true)
    setNoDelayMethod->returnType = socketClass;
    socketClass->methods["setNoDelay"] = setNoDelayMethod;

    // socket.setKeepAlive(enable?, initialDelay?) -> socket
    auto setKeepAliveMethod = std::make_shared<FunctionType>();
    setKeepAliveMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // enable (optional)
    setKeepAliveMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // initialDelay (optional)
    setKeepAliveMethod->returnType = socketClass;
    socketClass->methods["setKeepAlive"] = setKeepAliveMethod;

    // socket.address() -> { address, family, port } | null
    auto socketAddressMethod = std::make_shared<FunctionType>();
    socketAddressMethod->returnType = std::make_shared<Type>(TypeKind::Any);
    socketClass->methods["address"] = socketAddressMethod;

    // socket.ref() -> socket
    auto socketRefMethod = std::make_shared<FunctionType>();
    socketRefMethod->returnType = socketClass;
    socketClass->methods["ref"] = socketRefMethod;

    // socket.unref() -> socket
    auto socketUnrefMethod = std::make_shared<FunctionType>();
    socketUnrefMethod->returnType = socketClass;
    socketClass->methods["unref"] = socketUnrefMethod;

    symbols.defineType("Socket", socketClass);
    netModule->fields["Socket"] = socketClass;
    
    auto serverClass = std::make_shared<ClassType>("Server");
    auto listenMethod = std::make_shared<FunctionType>();
    listenMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // port
    listenMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback
    serverClass->methods["listen"] = listenMethod;

    // server.close() -> void
    auto serverCloseMethod = std::make_shared<FunctionType>();
    serverCloseMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    serverClass->methods["close"] = serverCloseMethod;

    // server.address() -> { address, family, port } | null
    auto serverAddressMethod = std::make_shared<FunctionType>();
    serverAddressMethod->returnType = std::make_shared<Type>(TypeKind::Any);
    serverClass->methods["address"] = serverAddressMethod;

    // server.ref() -> server
    auto serverRefMethod = std::make_shared<FunctionType>();
    serverRefMethod->returnType = serverClass;
    serverClass->methods["ref"] = serverRefMethod;

    // server.unref() -> server
    auto serverUnrefMethod = std::make_shared<FunctionType>();
    serverUnrefMethod->returnType = serverClass;
    serverClass->methods["unref"] = serverUnrefMethod;

    // server.getConnections(callback) -> void
    auto serverGetConnectionsMethod = std::make_shared<FunctionType>();
    serverGetConnectionsMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    serverGetConnectionsMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    serverClass->methods["getConnections"] = serverGetConnectionsMethod;

    // server.maxConnections (number, -1 = unlimited)
    serverClass->fields["maxConnections"] = std::make_shared<Type>(TypeKind::Int);

    symbols.defineType("Server", serverClass);
    netModule->fields["Server"] = serverClass;
    
    auto createServerType = std::make_shared<FunctionType>();
    createServerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // connectionListener
    createServerType->returnType = serverClass;
    netModule->fields["createServer"] = createServerType;

    // net.connect(port, host?, callback?) -> Socket
    auto connectFn = std::make_shared<FunctionType>();
    connectFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // port
    connectFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // host (optional)
    connectFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback (optional)
    connectFn->returnType = socketClass;
    netModule->fields["connect"] = connectFn;

    // net.createConnection - alias for net.connect
    netModule->fields["createConnection"] = connectFn;

    // net.isIP(input: string) -> number (0, 4, or 6)
    auto isIPType = std::make_shared<FunctionType>();
    isIPType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    isIPType->returnType = std::make_shared<Type>(TypeKind::Int);
    netModule->fields["isIP"] = isIPType;

    // net.isIPv4(input: string) -> boolean
    auto isIPv4Type = std::make_shared<FunctionType>();
    isIPv4Type->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    isIPv4Type->returnType = std::make_shared<Type>(TypeKind::Boolean);
    netModule->fields["isIPv4"] = isIPv4Type;

    // net.isIPv6(input: string) -> boolean
    auto isIPv6Type = std::make_shared<FunctionType>();
    isIPv6Type->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    isIPv6Type->returnType = std::make_shared<Type>(TypeKind::Boolean);
    netModule->fields["isIPv6"] = isIPv6Type;

    // net.getDefaultAutoSelectFamily() -> boolean
    auto getDefaultAutoSelectFamilyType = std::make_shared<FunctionType>();
    getDefaultAutoSelectFamilyType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    netModule->fields["getDefaultAutoSelectFamily"] = getDefaultAutoSelectFamilyType;

    // net.setDefaultAutoSelectFamily(value: boolean) -> void
    auto setDefaultAutoSelectFamilyType = std::make_shared<FunctionType>();
    setDefaultAutoSelectFamilyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean));
    setDefaultAutoSelectFamilyType->returnType = std::make_shared<Type>(TypeKind::Void);
    netModule->fields["setDefaultAutoSelectFamily"] = setDefaultAutoSelectFamilyType;

    // net.getDefaultAutoSelectFamilyAttemptTimeout() -> number
    auto getDefaultAutoSelectFamilyAttemptTimeoutType = std::make_shared<FunctionType>();
    getDefaultAutoSelectFamilyAttemptTimeoutType->returnType = std::make_shared<Type>(TypeKind::Int);
    netModule->fields["getDefaultAutoSelectFamilyAttemptTimeout"] = getDefaultAutoSelectFamilyAttemptTimeoutType;

    // net.setDefaultAutoSelectFamilyAttemptTimeout(value: number) -> void
    auto setDefaultAutoSelectFamilyAttemptTimeoutType = std::make_shared<FunctionType>();
    setDefaultAutoSelectFamilyAttemptTimeoutType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setDefaultAutoSelectFamilyAttemptTimeoutType->returnType = std::make_shared<Type>(TypeKind::Void);
    netModule->fields["setDefaultAutoSelectFamilyAttemptTimeout"] = setDefaultAutoSelectFamilyAttemptTimeoutType;

    // SocketAddress class
    auto socketAddressClass = std::make_shared<ClassType>("SocketAddress");
    socketAddressClass->fields["address"] = std::make_shared<Type>(TypeKind::String);
    socketAddressClass->fields["family"] = std::make_shared<Type>(TypeKind::String);
    socketAddressClass->fields["flowlabel"] = std::make_shared<Type>(TypeKind::Int);
    socketAddressClass->fields["port"] = std::make_shared<Type>(TypeKind::Int);
    symbols.defineType("SocketAddress", socketAddressClass);
    
    // SocketAddress constructor takes optional options object
    auto socketAddressStatic = std::make_shared<ObjectType>();
    auto socketAddressConstructor = std::make_shared<FunctionType>();
    socketAddressConstructor->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options (optional)
    socketAddressConstructor->returnType = socketAddressClass;
    socketAddressStatic->fields["new"] = socketAddressConstructor;
    
    // SocketAddress.parse(input: string) -> SocketAddress | undefined
    auto socketAddressParseType = std::make_shared<FunctionType>();
    socketAddressParseType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    socketAddressParseType->returnType = socketAddressClass;
    socketAddressStatic->fields["parse"] = socketAddressParseType;
    
    netModule->fields["SocketAddress"] = socketAddressStatic;

    // BlockList class - use ClassType for constructor return type matching
    auto blockListClass = std::make_shared<ClassType>();
    blockListClass->name = "BlockList";
    
    // BlockList instance methods
    // addAddress(address: string, type?: 'ipv4' | 'ipv6') -> void
    auto addAddressType = std::make_shared<FunctionType>();
    addAddressType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    addAddressType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // optional type
    addAddressType->returnType = std::make_shared<Type>(TypeKind::Void);
    blockListClass->methods["addAddress"] = addAddressType;
    
    // addRange(start: string, end: string, type?: 'ipv4' | 'ipv6') -> void
    auto addRangeType = std::make_shared<FunctionType>();
    addRangeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    addRangeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    addRangeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // optional type
    addRangeType->returnType = std::make_shared<Type>(TypeKind::Void);
    blockListClass->methods["addRange"] = addRangeType;
    
    // addSubnet(network: string, prefix: number, type?: 'ipv4' | 'ipv6') -> void
    auto addSubnetType = std::make_shared<FunctionType>();
    addSubnetType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    addSubnetType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // prefix is an integer
    addSubnetType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // optional type
    addSubnetType->returnType = std::make_shared<Type>(TypeKind::Void);
    blockListClass->methods["addSubnet"] = addSubnetType;
    
    // check(address: string, type?: 'ipv4' | 'ipv6') -> boolean
    auto checkType = std::make_shared<FunctionType>();
    checkType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    checkType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // optional type
    checkType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    blockListClass->methods["check"] = checkType;
    
    // rules -> string[]
    blockListClass->fields["rules"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    
    // BlockList static methods
    auto blockListStatic = std::make_shared<ObjectType>();
    auto blockListConstructor = std::make_shared<FunctionType>();
    blockListConstructor->returnType = blockListClass;
    blockListStatic->fields["new"] = blockListConstructor;
    
    // BlockList.isBlockList(value: any) -> boolean
    auto isBlockListType = std::make_shared<FunctionType>();
    isBlockListType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    isBlockListType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    blockListStatic->fields["isBlockList"] = isBlockListType;
    
    netModule->fields["BlockList"] = blockListStatic;
    
    symbols.define("net", netModule);
}

} // namespace ts
