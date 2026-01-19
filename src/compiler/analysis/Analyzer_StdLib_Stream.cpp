#include "Analyzer.h"

namespace ts {

void Analyzer::registerStreams() {
    auto eventEmitterClass = std::static_pointer_cast<ClassType>(symbols.lookupType("EventEmitter"));
    if (!eventEmitterClass) {
        registerEvents();
        eventEmitterClass = std::static_pointer_cast<ClassType>(symbols.lookupType("EventEmitter"));
    }

    auto streamClass = std::make_shared<ClassType>("Stream");
    streamClass->baseClass = eventEmitterClass;
    symbols.defineType("Stream", streamClass);

    // Readable
    auto readableClass = std::make_shared<ClassType>("Readable");
    readableClass->baseClass = streamClass;

    auto readFn = std::make_shared<FunctionType>();
    readFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    readFn->returnType = std::make_shared<Type>(TypeKind::Any);
    readableClass->methods["read"] = readFn;

    auto resumeFn = std::make_shared<FunctionType>();
    resumeFn->returnType = readableClass;
    readableClass->methods["resume"] = resumeFn;

    auto pauseFn = std::make_shared<FunctionType>();
    pauseFn->returnType = readableClass;
    readableClass->methods["pause"] = pauseFn;

    auto pipeFn = std::make_shared<FunctionType>();
    pipeFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // Writable
    pipeFn->returnType = std::make_shared<Type>(TypeKind::Any);
    readableClass->methods["pipe"] = pipeFn;

    // Readable state properties
    readableClass->fields["destroyed"] = std::make_shared<Type>(TypeKind::Boolean);
    readableClass->fields["readable"] = std::make_shared<Type>(TypeKind::Boolean);
    readableClass->fields["readableEnded"] = std::make_shared<Type>(TypeKind::Boolean);
    readableClass->fields["readableFlowing"] = std::make_shared<Type>(TypeKind::Boolean);
    readableClass->fields["readableHighWaterMark"] = std::make_shared<Type>(TypeKind::Int);
    readableClass->fields["readableLength"] = std::make_shared<Type>(TypeKind::Int);
    readableClass->fields["readableObjectMode"] = std::make_shared<Type>(TypeKind::Boolean);
    readableClass->fields["readableAborted"] = std::make_shared<Type>(TypeKind::Boolean);
    readableClass->fields["readableDidRead"] = std::make_shared<Type>(TypeKind::Boolean);

    // Readable methods
    auto isPausedFn = std::make_shared<FunctionType>();
    isPausedFn->returnType = std::make_shared<Type>(TypeKind::Boolean);
    readableClass->methods["isPaused"] = isPausedFn;

    auto unpipeFn = std::make_shared<FunctionType>();
    unpipeFn->returnType = readableClass;
    readableClass->methods["unpipe"] = unpipeFn;

    auto destroyFn = std::make_shared<FunctionType>();
    destroyFn->returnType = readableClass;
    readableClass->methods["destroy"] = destroyFn;

    // readable.unshift(chunk) - push data back to the front of the internal buffer
    auto unshiftFn = std::make_shared<FunctionType>();
    unshiftFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // chunk
    unshiftFn->returnType = std::make_shared<Type>(TypeKind::Void);
    readableClass->methods["unshift"] = unshiftFn;

    // readable.setEncoding(encoding) - set the character encoding
    auto setEncodingFn = std::make_shared<FunctionType>();
    setEncodingFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // encoding
    setEncodingFn->returnType = readableClass;  // returns this for chaining
    readableClass->methods["setEncoding"] = setEncodingFn;

    // readable.readableEncoding property
    readableClass->fields["readableEncoding"] = std::make_shared<Type>(TypeKind::String);

    // readable.wrap(stream) - wrap an old-style stream in a new Readable
    auto wrapFn = std::make_shared<FunctionType>();
    wrapFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // oldStream
    wrapFn->returnType = readableClass;  // returns this for chaining
    readableClass->methods["wrap"] = wrapFn;

    symbols.defineType("Readable", readableClass);

    // Create static Readable object with from() method
    auto readableStatic = std::make_shared<ObjectType>();
    auto fromFn = std::make_shared<FunctionType>();
    fromFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // iterable (array)
    fromFn->returnType = readableClass;
    readableStatic->fields["from"] = fromFn;
    symbols.define("Readable", readableStatic);

    // Writable
    auto writableClass = std::make_shared<ClassType>("Writable");
    writableClass->baseClass = streamClass;

    auto writeFn = std::make_shared<FunctionType>();
    writeFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    writeFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding
    writeFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function)); // callback
    writeFn->returnType = std::make_shared<Type>(TypeKind::Boolean);
    writableClass->methods["write"] = writeFn;

    auto endFn = std::make_shared<FunctionType>();
    endFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    endFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    endFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    endFn->returnType = writableClass;
    writableClass->methods["end"] = endFn;

    // Writable state properties
    writableClass->fields["destroyed"] = std::make_shared<Type>(TypeKind::Boolean);
    writableClass->fields["writable"] = std::make_shared<Type>(TypeKind::Boolean);
    writableClass->fields["writableEnded"] = std::make_shared<Type>(TypeKind::Boolean);
    writableClass->fields["writableFinished"] = std::make_shared<Type>(TypeKind::Boolean);
    writableClass->fields["writableNeedDrain"] = std::make_shared<Type>(TypeKind::Boolean);
    writableClass->fields["writableHighWaterMark"] = std::make_shared<Type>(TypeKind::Int);
    writableClass->fields["writableLength"] = std::make_shared<Type>(TypeKind::Int);
    writableClass->fields["writableObjectMode"] = std::make_shared<Type>(TypeKind::Boolean);
    writableClass->fields["writableAborted"] = std::make_shared<Type>(TypeKind::Boolean);

    // Writable methods
    auto writableDestroyFn = std::make_shared<FunctionType>();
    writableDestroyFn->returnType = writableClass;
    writableClass->methods["destroy"] = writableDestroyFn;

    auto corkFn = std::make_shared<FunctionType>();
    corkFn->returnType = writableClass;
    writableClass->methods["cork"] = corkFn;

    auto uncorkFn = std::make_shared<FunctionType>();
    uncorkFn->returnType = writableClass;
    writableClass->methods["uncork"] = uncorkFn;

    // writable.setDefaultEncoding(encoding) - set the default encoding for write()
    auto setDefaultEncodingFn = std::make_shared<FunctionType>();
    setDefaultEncodingFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // encoding
    setDefaultEncodingFn->returnType = writableClass;  // returns this for chaining
    writableClass->methods["setDefaultEncoding"] = setDefaultEncodingFn;

    // writable.writableCorked property
    writableClass->fields["writableCorked"] = std::make_shared<Type>(TypeKind::Int);

    symbols.defineType("Writable", writableClass);

    // Duplex & Transform
    auto duplexClass = std::make_shared<ClassType>("Duplex");
    duplexClass->baseClass = readableClass; // Should inherit from both, but we only support single inheritance for now
    symbols.defineType("Duplex", duplexClass);

    auto transformClass = std::make_shared<ClassType>("Transform");
    transformClass->baseClass = duplexClass;
    symbols.defineType("Transform", transformClass);

    // fs.ReadStream and fs.WriteStream
    auto readStreamClass = std::make_shared<ClassType>("ReadStream");
    readStreamClass->baseClass = readableClass;
    // ReadStream-specific properties
    readStreamClass->fields["bytesRead"] = std::make_shared<Type>(TypeKind::Int);
    readStreamClass->fields["path"] = std::make_shared<Type>(TypeKind::String);
    readStreamClass->fields["pending"] = std::make_shared<Type>(TypeKind::Boolean);
    symbols.defineType("ReadStream", readStreamClass);

    auto writeStreamClass = std::make_shared<ClassType>("WriteStream");
    writeStreamClass->baseClass = writableClass;
    // WriteStream-specific properties
    writeStreamClass->fields["bytesWritten"] = std::make_shared<Type>(TypeKind::Int);
    writeStreamClass->fields["path"] = std::make_shared<Type>(TypeKind::String);
    writeStreamClass->fields["pending"] = std::make_shared<Type>(TypeKind::Boolean);
    symbols.defineType("WriteStream", writeStreamClass);

    auto streamModule = std::make_shared<ObjectType>();
    streamModule->fields["Stream"] = streamClass;

    // Create static Readable object for stream.Readable with from() method
    auto streamReadableStatic = std::make_shared<ObjectType>();
    auto streamFromFn = std::make_shared<FunctionType>();
    streamFromFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // iterable (array)
    streamFromFn->returnType = readableClass;
    streamReadableStatic->fields["from"] = streamFromFn;
    streamModule->fields["Readable"] = streamReadableStatic;

    streamModule->fields["Writable"] = writableClass;
    streamModule->fields["Duplex"] = duplexClass;
    streamModule->fields["Transform"] = transformClass;

    // stream.pipeline(...streams, callback) -> returns last stream
    // Simplified signature: takes array of streams and optional callback
    auto pipelineFn = std::make_shared<FunctionType>();
    pipelineFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // ...streams (variadic, simplified as any)
    pipelineFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // callback (optional)
    pipelineFn->returnType = std::make_shared<Type>(TypeKind::Any);  // returns last stream
    streamModule->fields["pipeline"] = pipelineFn;

    // stream.finished(stream, options?, callback) -> cleanup function
    auto finishedFn = std::make_shared<FunctionType>();
    finishedFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // stream
    finishedFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options or callback
    finishedFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // callback (optional)
    finishedFn->returnType = std::make_shared<FunctionType>();  // returns cleanup function
    streamModule->fields["finished"] = finishedFn;

    symbols.define("stream", streamModule);
}

} // namespace ts
