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
    
    symbols.defineType("Socket", socketClass);
    netModule->fields["Socket"] = socketClass;
    
    auto serverClass = std::make_shared<ClassType>("Server");
    auto listenMethod = std::make_shared<FunctionType>();
    listenMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // port
    listenMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback
    serverClass->methods["listen"] = listenMethod;
    
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
    
    symbols.define("net", netModule);
}

} // namespace ts
