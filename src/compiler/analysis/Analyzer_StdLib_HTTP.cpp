#include "Analyzer.h"

namespace ts {

void Analyzer::registerHTTP() {
    // Register URL class
    auto urlClass = std::make_shared<ClassType>("URL");
    urlClass->fields["href"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["protocol"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["host"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["hostname"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["port"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["pathname"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["search"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["hash"] = std::make_shared<Type>(TypeKind::String);
    symbols.defineType("URL", urlClass);

    // fetch
    auto responseClass = std::make_shared<ClassType>("Response");
    
    auto promiseResponse = std::make_shared<ClassType>("Promise");
    promiseResponse->typeArguments.push_back(responseClass);

    auto fetchType = std::make_shared<FunctionType>();
    fetchType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    fetchType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    fetchType->returnType = promiseResponse;
    symbols.define("fetch", fetchType);

    // Response
    responseClass->fields["status"] = std::make_shared<Type>(TypeKind::Int);
    responseClass->fields["statusText"] = std::make_shared<Type>(TypeKind::String);
    responseClass->fields["ok"] = std::make_shared<Type>(TypeKind::Boolean);
    
    auto promiseString = std::make_shared<ClassType>("Promise");
    promiseString->typeArguments.push_back(std::make_shared<Type>(TypeKind::String));

    auto textMethod = std::make_shared<FunctionType>();
    textMethod->returnType = promiseString;
    responseClass->methods["text"] = textMethod;
    
    auto promiseAny = std::make_shared<ClassType>("Promise");
    promiseAny->typeArguments.push_back(std::make_shared<Type>(TypeKind::Any));

    auto jsonMethod = std::make_shared<FunctionType>();
    jsonMethod->returnType = promiseAny;
    responseClass->methods["json"] = jsonMethod;
    
    symbols.defineType("Response", responseClass);

    // Register http module
    auto httpType = std::make_shared<ObjectType>();
    
    auto incomingMessageClass = std::make_shared<ClassType>("IncomingMessage");
    // IncomingMessage inherits from Readable
    auto readableClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Readable"));
    incomingMessageClass->baseClass = readableClass;
    
    incomingMessageClass->fields["method"] = std::make_shared<Type>(TypeKind::String);
    incomingMessageClass->fields["url"] = std::make_shared<Type>(TypeKind::String);
    incomingMessageClass->fields["headers"] = std::make_shared<Type>(TypeKind::Any);
    symbols.defineType("IncomingMessage", incomingMessageClass);

    auto serverResponseClass = std::make_shared<ClassType>("ServerResponse");
    // ServerResponse inherits from Writable
    auto writableClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Writable"));
    serverResponseClass->baseClass = writableClass;
    
    auto writeHeadMethod = std::make_shared<FunctionType>();
    writeHeadMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    writeHeadMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // headers
    serverResponseClass->methods["writeHead"] = writeHeadMethod;
    
    auto writeMethod = std::make_shared<FunctionType>();
    writeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    serverResponseClass->methods["write"] = writeMethod;
    
    auto endMethod = std::make_shared<FunctionType>();
    endMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    serverResponseClass->methods["end"] = endMethod;
    symbols.defineType("ServerResponse", serverResponseClass);

    auto serverClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Server"));
    if (!serverClass) {
        serverClass = std::make_shared<ClassType>("Server");
        symbols.defineType("Server", serverClass);
    }
    
    auto createServerType = std::make_shared<FunctionType>();
    auto requestCallback = std::make_shared<FunctionType>();
    requestCallback->paramTypes.push_back(incomingMessageClass);
    requestCallback->paramTypes.push_back(serverResponseClass);
    createServerType->paramTypes.push_back(requestCallback);
    createServerType->returnType = serverClass;
    httpType->fields["createServer"] = createServerType;
    
    symbols.define("http", httpType);
}

} // namespace ts
