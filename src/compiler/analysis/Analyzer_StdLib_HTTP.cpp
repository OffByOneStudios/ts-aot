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
    
    // Request properties (server-side)
    incomingMessageClass->fields["method"] = std::make_shared<Type>(TypeKind::String);
    incomingMessageClass->fields["url"] = std::make_shared<Type>(TypeKind::String);
    incomingMessageClass->fields["headers"] = std::make_shared<Type>(TypeKind::Any);
    
    // Response properties (client-side) - IncomingMessage is used for http.ClientResponse too
    incomingMessageClass->fields["statusCode"] = std::make_shared<Type>(TypeKind::Int);
    incomingMessageClass->fields["statusMessage"] = std::make_shared<Type>(TypeKind::String);
    
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

    // Use existing Server class from net module, or create one
    auto serverClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Server"));
    if (!serverClass) {
        serverClass = std::make_shared<ClassType>("Server");
        symbols.defineType("Server", serverClass);
    }
    auto eventEmitterClass = std::static_pointer_cast<ClassType>(symbols.lookupType("EventEmitter"));
    serverClass->baseClass = eventEmitterClass;
    
    auto createServerType = std::make_shared<FunctionType>();
    auto requestCallback = std::make_shared<FunctionType>();
    requestCallback->paramTypes.push_back(incomingMessageClass);
    requestCallback->paramTypes.push_back(serverResponseClass);
    
    // Support both (callback) and (options, callback)
    createServerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    createServerType->paramTypes.push_back(requestCallback);
    
    createServerType->returnType = serverClass;
    httpType->fields["createServer"] = createServerType;

    auto clientRequestClass = std::make_shared<ClassType>("ClientRequest");
    clientRequestClass->baseClass = writableClass;
    symbols.defineType("ClientRequest", clientRequestClass);

    auto responseCallback = std::make_shared<FunctionType>();
    responseCallback->paramTypes.push_back(incomingMessageClass);

    auto requestType = std::make_shared<FunctionType>();
    requestType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    requestType->paramTypes.push_back(responseCallback);
    requestType->returnType = clientRequestClass;
    httpType->fields["request"] = requestType;

    auto getType = std::make_shared<FunctionType>();
    getType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    getType->paramTypes.push_back(responseCallback);
    getType->returnType = clientRequestClass;
    httpType->fields["get"] = getType;
    
    symbols.define("http", httpType);
}

void Analyzer::registerHTTPS() {
    auto httpsType = std::make_shared<ObjectType>();
    
    auto incomingMessageClass = std::static_pointer_cast<ClassType>(symbols.lookupType("IncomingMessage"));
    auto serverResponseClass = std::static_pointer_cast<ClassType>(symbols.lookupType("ServerResponse"));
    auto clientRequestClass = std::static_pointer_cast<ClassType>(symbols.lookupType("ClientRequest"));
    auto serverClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Server"));

    auto responseCallback = std::make_shared<FunctionType>();
    responseCallback->paramTypes.push_back(incomingMessageClass);

    auto requestCallback = std::make_shared<FunctionType>();
    requestCallback->paramTypes.push_back(incomingMessageClass);
    requestCallback->paramTypes.push_back(serverResponseClass);

    auto createServerType = std::make_shared<FunctionType>();
    createServerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options or callback
    createServerType->paramTypes.push_back(requestCallback); // callback (optional if first is callback)
    createServerType->returnType = serverClass;
    httpsType->fields["createServer"] = createServerType;

    auto requestType = std::make_shared<FunctionType>();
    requestType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    requestType->paramTypes.push_back(responseCallback);
    requestType->returnType = clientRequestClass;
    httpsType->fields["request"] = requestType;

    auto getType = std::make_shared<FunctionType>();
    getType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    getType->paramTypes.push_back(responseCallback);
    getType->returnType = clientRequestClass;
    httpsType->fields["get"] = getType;

    symbols.define("https", httpsType);
}

} // namespace ts
