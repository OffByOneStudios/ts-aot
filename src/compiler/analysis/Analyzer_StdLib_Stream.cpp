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

    symbols.defineType("Readable", readableClass);

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
    symbols.defineType("ReadStream", readStreamClass);

    auto writeStreamClass = std::make_shared<ClassType>("WriteStream");
    writeStreamClass->baseClass = writableClass;
    symbols.defineType("WriteStream", writeStreamClass);

    auto streamModule = std::make_shared<ObjectType>();
    streamModule->fields["Stream"] = streamClass;
    streamModule->fields["Readable"] = readableClass;
    streamModule->fields["Writable"] = writableClass;
    streamModule->fields["Duplex"] = duplexClass;
    streamModule->fields["Transform"] = transformClass;
    symbols.define("stream", streamModule);
}

} // namespace ts
