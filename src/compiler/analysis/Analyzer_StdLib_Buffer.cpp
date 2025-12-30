#include "Analyzer.h"

namespace ts {

void Analyzer::registerBuffer() {
    auto bufferClass = std::make_shared<ClassType>("Buffer");
    bufferClass->fields["length"] = std::make_shared<Type>(TypeKind::Int);
    bufferClass->fields["byteLength"] = std::make_shared<Type>(TypeKind::Int);
    bufferClass->fields["byteOffset"] = std::make_shared<Type>(TypeKind::Int);
    bufferClass->fields["buffer"] = bufferClass; // buffer.buffer returns the ArrayBuffer (same as buffer for Node.js)
    
    auto bufferToString = std::make_shared<FunctionType>();
    bufferToString->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding
    bufferToString->returnType = std::make_shared<Type>(TypeKind::String);
    bufferClass->methods["toString"] = bufferToString;

    auto bufferFill = std::make_shared<FunctionType>();
    bufferFill->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // value
    bufferFill->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // start
    bufferFill->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // end
    bufferFill->returnType = bufferClass;
    bufferClass->methods["fill"] = bufferFill;

    auto bufferSlice = std::make_shared<FunctionType>();
    bufferSlice->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferSlice->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferSlice->returnType = bufferClass;
    bufferClass->methods["slice"] = bufferSlice;
    
    auto bufferSubarray = std::make_shared<FunctionType>();
    bufferSubarray->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // start
    bufferSubarray->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // end
    bufferSubarray->returnType = bufferClass;
    bufferClass->methods["subarray"] = bufferSubarray;
    
    auto bufferCopy = std::make_shared<FunctionType>();
    bufferCopy->paramTypes.push_back(bufferClass);  // target
    bufferCopy->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // targetStart
    bufferCopy->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // sourceStart
    bufferCopy->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // sourceEnd
    bufferCopy->returnType = std::make_shared<Type>(TypeKind::Int); // returns bytes copied
    bufferClass->methods["copy"] = bufferCopy;
    
    symbols.defineType("Buffer", bufferClass);

    auto bufferStatic = std::make_shared<ObjectType>();
    
    auto bufferAlloc = std::make_shared<FunctionType>();
    bufferAlloc->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferAlloc->returnType = bufferClass;
    bufferStatic->fields["alloc"] = bufferAlloc;
    
    auto bufferAllocUnsafe = std::make_shared<FunctionType>();
    bufferAllocUnsafe->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferAllocUnsafe->returnType = bufferClass;
    bufferStatic->fields["allocUnsafe"] = bufferAllocUnsafe;
    
    auto bufferFrom = std::make_shared<FunctionType>();
    bufferFrom->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    bufferFrom->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding
    bufferFrom->returnType = bufferClass;
    bufferStatic->fields["from"] = bufferFrom;
    
    auto bufferConcat = std::make_shared<FunctionType>();
    bufferConcat->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // list of buffers
    bufferConcat->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // totalLength (optional)
    bufferConcat->returnType = bufferClass;
    bufferStatic->fields["concat"] = bufferConcat;

    auto bufferIsBuffer = std::make_shared<FunctionType>();
    bufferIsBuffer->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    bufferIsBuffer->returnType = std::make_shared<Type>(TypeKind::Boolean);
    bufferStatic->fields["isBuffer"] = bufferIsBuffer;
    
    symbols.define("Buffer", bufferStatic);

    auto bufferModule = std::make_shared<ObjectType>();
    bufferModule->fields["Buffer"] = bufferStatic;
    symbols.define("buffer", bufferModule);
}

} // namespace ts
