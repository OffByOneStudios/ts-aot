#include "Analyzer.h"

namespace ts {

void Analyzer::registerHTTP() {
    // Register URLSearchParams class first (URL.searchParams returns it)
    auto urlSearchParamsClass = std::make_shared<ClassType>("URLSearchParams");
    urlSearchParamsClass->fields["size"] = std::make_shared<Type>(TypeKind::Int);
    
    // URLSearchParams methods
    auto appendMethod = std::make_shared<FunctionType>();
    appendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    appendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    appendMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    urlSearchParamsClass->methods["append"] = appendMethod;
    
    auto deleteMethod = std::make_shared<FunctionType>();
    deleteMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    deleteMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    urlSearchParamsClass->methods["delete"] = deleteMethod;
    
    auto getMethod = std::make_shared<FunctionType>();
    getMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    getMethod->returnType = std::make_shared<Type>(TypeKind::String);
    urlSearchParamsClass->methods["get"] = getMethod;
    
    auto getAllMethod = std::make_shared<FunctionType>();
    getAllMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    getAllMethod->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    urlSearchParamsClass->methods["getAll"] = getAllMethod;
    
    auto hasMethod = std::make_shared<FunctionType>();
    hasMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    hasMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    urlSearchParamsClass->methods["has"] = hasMethod;
    
    auto setMethod = std::make_shared<FunctionType>();
    setMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    setMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    setMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    urlSearchParamsClass->methods["set"] = setMethod;
    
    auto sortMethod = std::make_shared<FunctionType>();
    sortMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    urlSearchParamsClass->methods["sort"] = sortMethod;
    
    auto toStringMethod = std::make_shared<FunctionType>();
    toStringMethod->returnType = std::make_shared<Type>(TypeKind::String);
    urlSearchParamsClass->methods["toString"] = toStringMethod;

    // Iterator methods
    auto entriesMethod = std::make_shared<FunctionType>();
    entriesMethod->returnType = std::make_shared<ArrayType>(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));
    urlSearchParamsClass->methods["entries"] = entriesMethod;

    auto keysMethod = std::make_shared<FunctionType>();
    keysMethod->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    urlSearchParamsClass->methods["keys"] = keysMethod;

    auto valuesMethod = std::make_shared<FunctionType>();
    valuesMethod->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    urlSearchParamsClass->methods["values"] = valuesMethod;

    // forEach(callback: (value, key, searchParams) => void, thisArg?: any)
    auto forEachCallbackType = std::make_shared<FunctionType>();
    forEachCallbackType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // value
    forEachCallbackType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // key
    forEachCallbackType->paramTypes.push_back(urlSearchParamsClass);                       // searchParams
    forEachCallbackType->returnType = std::make_shared<Type>(TypeKind::Void);

    auto forEachMethod = std::make_shared<FunctionType>();
    forEachMethod->paramTypes.push_back(forEachCallbackType);
    forEachMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // thisArg (optional)
    forEachMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    urlSearchParamsClass->methods["forEach"] = forEachMethod;

    symbols.defineType("URLSearchParams", urlSearchParamsClass);

    // Register URL class
    auto urlClass = std::make_shared<ClassType>("URL");
    urlClass->fields["href"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["origin"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["protocol"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["host"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["hostname"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["port"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["pathname"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["search"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["hash"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["username"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["password"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["searchParams"] = urlSearchParamsClass;
    
    // URL methods
    auto urlToStringMethod = std::make_shared<FunctionType>();
    urlToStringMethod->returnType = std::make_shared<Type>(TypeKind::String);
    urlClass->methods["toString"] = urlToStringMethod;
    
    auto toJSONMethod = std::make_shared<FunctionType>();
    toJSONMethod->returnType = std::make_shared<Type>(TypeKind::String);
    urlClass->methods["toJSON"] = toJSONMethod;
    
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

    // Common properties
    incomingMessageClass->fields["httpVersion"] = std::make_shared<Type>(TypeKind::String);
    incomingMessageClass->fields["complete"] = std::make_shared<Type>(TypeKind::Boolean);
    incomingMessageClass->fields["rawHeaders"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    
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

    // ServerResponse-specific properties
    serverResponseClass->fields["statusCode"] = std::make_shared<Type>(TypeKind::Int);
    serverResponseClass->fields["statusMessage"] = std::make_shared<Type>(TypeKind::String);

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

    // ClientRequest properties
    clientRequestClass->fields["path"] = std::make_shared<Type>(TypeKind::String);
    clientRequestClass->fields["method"] = std::make_shared<Type>(TypeKind::String);
    clientRequestClass->fields["host"] = std::make_shared<Type>(TypeKind::String);
    clientRequestClass->fields["protocol"] = std::make_shared<Type>(TypeKind::String);

    // ClientRequest methods
    auto getHeaderMethod = std::make_shared<FunctionType>();
    getHeaderMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    getHeaderMethod->returnType = std::make_shared<Type>(TypeKind::Any);
    clientRequestClass->methods["getHeader"] = getHeaderMethod;

    auto setHeaderMethod = std::make_shared<FunctionType>();
    setHeaderMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    setHeaderMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    setHeaderMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    clientRequestClass->methods["setHeader"] = setHeaderMethod;

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

    // http.METHODS - array of HTTP method strings
    auto methodsType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    httpType->fields["METHODS"] = methodsType;

    // http.STATUS_CODES - object mapping status codes to descriptions
    auto statusCodesType = std::make_shared<ObjectType>();
    httpType->fields["STATUS_CODES"] = statusCodesType;

    // http.maxHeaderSize - number
    httpType->fields["maxHeaderSize"] = std::make_shared<Type>(TypeKind::Int);

    // http.validateHeaderName(name: string) -> void (throws on invalid)
    auto validateHeaderNameType = std::make_shared<FunctionType>();
    validateHeaderNameType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    validateHeaderNameType->returnType = std::make_shared<Type>(TypeKind::Void);
    httpType->fields["validateHeaderName"] = validateHeaderNameType;

    // http.validateHeaderValue(name: string, value: string) -> void (throws on invalid)
    auto validateHeaderValueType = std::make_shared<FunctionType>();
    validateHeaderValueType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    validateHeaderValueType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    validateHeaderValueType->returnType = std::make_shared<Type>(TypeKind::Void);
    httpType->fields["validateHeaderValue"] = validateHeaderValueType;

    // http.Agent class
    auto agentClass = std::make_shared<ClassType>("Agent");
    symbols.defineType("Agent", agentClass);
    
    // Agent constructor: new Agent(options?)
    auto agentConstructor = std::make_shared<FunctionType>();
    agentConstructor->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    agentConstructor->returnType = agentClass;
    agentClass->fields["constructor"] = agentConstructor;
    
    // Agent methods
    auto agentDestroyType = std::make_shared<FunctionType>();
    agentDestroyType->returnType = std::make_shared<Type>(TypeKind::Void);
    agentClass->fields["destroy"] = agentDestroyType;
    
    // Agent properties
    agentClass->fields["keepAlive"] = std::make_shared<Type>(TypeKind::Boolean);
    agentClass->fields["maxSockets"] = std::make_shared<Type>(TypeKind::Int);
    agentClass->fields["maxTotalSockets"] = std::make_shared<Type>(TypeKind::Int);
    agentClass->fields["maxFreeSockets"] = std::make_shared<Type>(TypeKind::Int);

    // http.Agent - the class
    httpType->fields["Agent"] = agentClass;
    
    // http.globalAgent - default agent instance
    httpType->fields["globalAgent"] = agentClass;

    // http.setMaxIdleHTTPParsers(max: number) -> void
    auto setMaxIdleType = std::make_shared<FunctionType>();
    setMaxIdleType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setMaxIdleType->returnType = std::make_shared<Type>(TypeKind::Void);
    httpType->fields["setMaxIdleHTTPParsers"] = setMaxIdleType;

    // http.getMaxIdleHTTPParsers() -> number
    auto getMaxIdleType = std::make_shared<FunctionType>();
    getMaxIdleType->returnType = std::make_shared<Type>(TypeKind::Int);
    httpType->fields["getMaxIdleHTTPParsers"] = getMaxIdleType;

    // http.OutgoingMessage class - base class for ServerResponse and ClientRequest
    auto outgoingMessageClass = std::make_shared<ClassType>("OutgoingMessage");
    outgoingMessageClass->baseClass = writableClass;
    
    outgoingMessageClass->fields["headersSent"] = std::make_shared<Type>(TypeKind::Boolean);
    outgoingMessageClass->fields["finished"] = std::make_shared<Type>(TypeKind::Boolean);
    outgoingMessageClass->fields["writableEnded"] = std::make_shared<Type>(TypeKind::Boolean);
    outgoingMessageClass->fields["socket"] = std::make_shared<Type>(TypeKind::Any);
    
    // OutgoingMessage methods
    auto setHeaderType = std::make_shared<FunctionType>();
    setHeaderType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // name
    setHeaderType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // value
    setHeaderType->returnType = std::make_shared<Type>(TypeKind::Void);
    outgoingMessageClass->methods["setHeader"] = setHeaderType;
    
    auto getHeaderType = std::make_shared<FunctionType>();
    getHeaderType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    getHeaderType->returnType = std::make_shared<Type>(TypeKind::Any);
    outgoingMessageClass->methods["getHeader"] = getHeaderType;
    
    auto getHeadersType = std::make_shared<FunctionType>();
    getHeadersType->returnType = std::make_shared<Type>(TypeKind::Any);
    outgoingMessageClass->methods["getHeaders"] = getHeadersType;
    
    auto hasHeaderType = std::make_shared<FunctionType>();
    hasHeaderType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    hasHeaderType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    outgoingMessageClass->methods["hasHeader"] = hasHeaderType;
    
    auto removeHeaderType = std::make_shared<FunctionType>();
    removeHeaderType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    removeHeaderType->returnType = std::make_shared<Type>(TypeKind::Void);
    outgoingMessageClass->methods["removeHeader"] = removeHeaderType;
    
    auto getHeaderNamesType = std::make_shared<FunctionType>();
    getHeaderNamesType->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    outgoingMessageClass->methods["getHeaderNames"] = getHeaderNamesType;
    
    auto flushHeadersType = std::make_shared<FunctionType>();
    flushHeadersType->returnType = std::make_shared<Type>(TypeKind::Void);
    outgoingMessageClass->methods["flushHeaders"] = flushHeadersType;
    
    symbols.defineType("OutgoingMessage", outgoingMessageClass);
    // Note: OutgoingMessage is a base class type, not exported on http module
    // to avoid vtable generation issues

    // http.CloseEvent class - for WebSocket and other close events
    auto closeEventClass = std::make_shared<ClassType>("CloseEvent");
    closeEventClass->fields["code"] = std::make_shared<Type>(TypeKind::Int);
    closeEventClass->fields["reason"] = std::make_shared<Type>(TypeKind::String);
    closeEventClass->fields["wasClean"] = std::make_shared<Type>(TypeKind::Boolean);
    symbols.defineType("CloseEvent", closeEventClass);
    // Note: CloseEvent is a type, not exported on http module

    // http.MessageEvent class - for WebSocket and SSE messages
    auto messageEventClass = std::make_shared<ClassType>("MessageEvent");
    messageEventClass->fields["data"] = std::make_shared<Type>(TypeKind::Any);
    messageEventClass->fields["origin"] = std::make_shared<Type>(TypeKind::String);
    messageEventClass->fields["lastEventId"] = std::make_shared<Type>(TypeKind::String);
    messageEventClass->fields["source"] = std::make_shared<Type>(TypeKind::Any);
    messageEventClass->fields["ports"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
    symbols.defineType("MessageEvent", messageEventClass);
    // Note: MessageEvent is a type, not exported on http module

    // WebSocket class (RFC 6455, browser-compatible API)
    // Also available as http.WebSocket per Node.js v22.5.0+
    auto webSocketClass = std::make_shared<ClassType>("WebSocket");
    webSocketClass->baseClass = eventEmitterClass;
    
    // WebSocket properties (read-only)
    webSocketClass->fields["url"] = std::make_shared<Type>(TypeKind::String);
    webSocketClass->fields["protocol"] = std::make_shared<Type>(TypeKind::String);
    webSocketClass->fields["extensions"] = std::make_shared<Type>(TypeKind::String);
    webSocketClass->fields["readyState"] = std::make_shared<Type>(TypeKind::Int);
    webSocketClass->fields["bufferedAmount"] = std::make_shared<Type>(TypeKind::Int);
    webSocketClass->fields["binaryType"] = std::make_shared<Type>(TypeKind::String);
    
    // WebSocket ready state constants
    webSocketClass->fields["CONNECTING"] = std::make_shared<Type>(TypeKind::Int);
    webSocketClass->fields["OPEN"] = std::make_shared<Type>(TypeKind::Int);
    webSocketClass->fields["CLOSING"] = std::make_shared<Type>(TypeKind::Int);
    webSocketClass->fields["CLOSED"] = std::make_shared<Type>(TypeKind::Int);
    
    // WebSocket event handler properties (optional callbacks)
    webSocketClass->fields["onopen"] = std::make_shared<Type>(TypeKind::Any);
    webSocketClass->fields["onmessage"] = std::make_shared<Type>(TypeKind::Any);
    webSocketClass->fields["onclose"] = std::make_shared<Type>(TypeKind::Any);
    webSocketClass->fields["onerror"] = std::make_shared<Type>(TypeKind::Any);
    
    // WebSocket methods
    auto wsSendType = std::make_shared<FunctionType>();
    wsSendType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // data: string | Buffer
    wsSendType->returnType = std::make_shared<Type>(TypeKind::Void);
    webSocketClass->methods["send"] = wsSendType;
    
    auto wsCloseType = std::make_shared<FunctionType>();
    wsCloseType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // code (optional)
    wsCloseType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // reason (optional)
    wsCloseType->returnType = std::make_shared<Type>(TypeKind::Void);
    webSocketClass->methods["close"] = wsCloseType;
    
    auto wsPingType = std::make_shared<FunctionType>();
    wsPingType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // data (optional)
    wsPingType->returnType = std::make_shared<Type>(TypeKind::Void);
    webSocketClass->methods["ping"] = wsPingType;
    
    auto wsPongType = std::make_shared<FunctionType>();
    wsPongType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // data (optional)
    wsPongType->returnType = std::make_shared<Type>(TypeKind::Void);
    webSocketClass->methods["pong"] = wsPongType;
    
    symbols.defineType("WebSocket", webSocketClass);
    
    // Also expose WebSocket on http module (Node.js v22.5.0+ style)
    httpType->fields["WebSocket"] = webSocketClass;
    
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

    // https.Agent class (extends http.Agent conceptually)
    auto httpAgentClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Agent"));
    auto httpsAgentClass = std::make_shared<ClassType>("HttpsAgent");
    httpsAgentClass->baseClass = httpAgentClass;
    symbols.defineType("HttpsAgent", httpsAgentClass);
    
    // Agent constructor
    auto agentConstructor = std::make_shared<FunctionType>();
    agentConstructor->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    agentConstructor->returnType = httpsAgentClass;
    httpsAgentClass->fields["constructor"] = agentConstructor;
    
    // https.Agent - the class
    httpsType->fields["Agent"] = httpsAgentClass;
    
    // https.globalAgent - default agent instance
    httpsType->fields["globalAgent"] = httpsAgentClass;

    symbols.define("https", httpsType);
}

void Analyzer::registerURLModule() {
    // Register the 'url' module with static utility functions
    auto urlModule = std::make_shared<ObjectType>();

    auto urlClass = std::static_pointer_cast<ClassType>(symbols.lookupType("URL"));
    auto urlSearchParamsClass = std::static_pointer_cast<ClassType>(symbols.lookupType("URLSearchParams"));

    // Re-export URL and URLSearchParams classes
    urlModule->fields["URL"] = urlClass;
    urlModule->fields["URLSearchParams"] = urlSearchParamsClass;

    // url.fileURLToPath(url: string | URL): string
    auto fileURLToPathFn = std::make_shared<FunctionType>();
    fileURLToPathFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // string or URL
    fileURLToPathFn->returnType = std::make_shared<Type>(TypeKind::String);
    urlModule->fields["fileURLToPath"] = fileURLToPathFn;

    // url.pathToFileURL(path: string): URL
    auto pathToFileURLFn = std::make_shared<FunctionType>();
    pathToFileURLFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    pathToFileURLFn->returnType = urlClass;
    urlModule->fields["pathToFileURL"] = pathToFileURLFn;

    // url.format(urlObject: URL | object, options?: object): string
    auto formatFn = std::make_shared<FunctionType>();
    formatFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // URL or legacy object
    formatFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options (optional)
    formatFn->returnType = std::make_shared<Type>(TypeKind::String);
    urlModule->fields["format"] = formatFn;

    // url.resolve(from: string, to: string): string
    auto resolveFn = std::make_shared<FunctionType>();
    resolveFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // from
    resolveFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // to
    resolveFn->returnType = std::make_shared<Type>(TypeKind::String);
    urlModule->fields["resolve"] = resolveFn;

    // url.urlToHttpOptions(url: URL): object
    auto urlToHttpOptionsFn = std::make_shared<FunctionType>();
    urlToHttpOptionsFn->paramTypes.push_back(urlClass);  // URL object
    urlToHttpOptionsFn->returnType = std::make_shared<ObjectType>();  // Returns options object
    urlModule->fields["urlToHttpOptions"] = urlToHttpOptionsFn;

    // url.domainToASCII(domain: string): string
    auto domainToASCIIFn = std::make_shared<FunctionType>();
    domainToASCIIFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    domainToASCIIFn->returnType = std::make_shared<Type>(TypeKind::String);
    urlModule->fields["domainToASCII"] = domainToASCIIFn;

    // url.domainToUnicode(domain: string): string
    auto domainToUnicodeFn = std::make_shared<FunctionType>();
    domainToUnicodeFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    domainToUnicodeFn->returnType = std::make_shared<Type>(TypeKind::String);
    urlModule->fields["domainToUnicode"] = domainToUnicodeFn;

    // url.parse(urlString: string, parseQueryString?: boolean, slashesDenoteHost?: boolean): UrlWithParsedQuery
    // Returns an object with: protocol, slashes, auth, host, port, hostname, hash, search, query, pathname, path, href
    auto parseFn = std::make_shared<FunctionType>();
    parseFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // urlString
    parseFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // parseQueryString (optional)
    parseFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // slashesDenoteHost (optional)
    parseFn->returnType = std::make_shared<ObjectType>();  // Legacy URL object
    urlModule->fields["parse"] = parseFn;

    symbols.define("url", urlModule);
}

} // namespace ts
