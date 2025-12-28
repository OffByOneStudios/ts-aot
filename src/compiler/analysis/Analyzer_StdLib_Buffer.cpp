#include "Analyzer.h"

namespace ts {

void Analyzer::registerBuffer() {
    auto bufferClass = std::make_shared<ClassType>("Buffer");
    bufferClass->fields["length"] = std::make_shared<Type>(TypeKind::Int);
    
    auto bufferToString = std::make_shared<FunctionType>();
    bufferToString->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding
    bufferToString->returnType = std::make_shared<Type>(TypeKind::String);
    bufferClass->methods["toString"] = bufferToString;

    auto bufferFill = std::make_shared<FunctionType>();
    bufferFill->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    bufferFill->returnType = bufferClass;
    bufferClass->methods["fill"] = bufferFill;

    auto bufferSlice = std::make_shared<FunctionType>();
    bufferSlice->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferSlice->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferSlice->returnType = bufferClass;
    bufferClass->methods["slice"] = bufferSlice;
    
    symbols.defineType("Buffer", bufferClass);

    auto bufferStatic = std::make_shared<ObjectType>();
    
    auto bufferAlloc = std::make_shared<FunctionType>();
    bufferAlloc->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    bufferAlloc->returnType = bufferClass;
    bufferStatic->fields["alloc"] = bufferAlloc;
    
    auto bufferFrom = std::make_shared<FunctionType>();
    bufferFrom->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    bufferFrom->paramTypes.push_back(std::make_shared<Type>(TypeKind::String)); // encoding
    bufferFrom->returnType = bufferClass;
    bufferStatic->fields["from"] = bufferFrom;

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
