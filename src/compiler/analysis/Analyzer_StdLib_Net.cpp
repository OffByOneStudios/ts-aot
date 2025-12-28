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
    
    symbols.define("net", netModule);
}

} // namespace ts
