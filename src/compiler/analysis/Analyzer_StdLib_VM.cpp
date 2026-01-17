#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerVM() {
    // =========================================================================
    // vm module - Stubbed (AOT incompatible)
    //
    // The vm module provides APIs for compiling and running code within V8
    // Virtual Machine contexts. This is fundamentally incompatible with AOT
    // compilation as it requires runtime code evaluation.
    //
    // These stubs allow code to compile but will throw errors at runtime.
    // =========================================================================
    auto vmType = std::make_shared<ObjectType>();

    // vm.createContext(contextObject?: object, options?: object): object
    auto createContextType = std::make_shared<FunctionType>();
    createContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    createContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    createContextType->returnType = std::make_shared<Type>(TypeKind::Object);
    vmType->fields["createContext"] = createContextType;

    // vm.isContext(object: any): boolean
    auto isContextType = std::make_shared<FunctionType>();
    isContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    isContextType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    vmType->fields["isContext"] = isContextType;

    // vm.runInContext(code: string, contextifiedObject: object, options?: object): any
    auto runInContextType = std::make_shared<FunctionType>();
    runInContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    runInContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    runInContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    runInContextType->returnType = std::make_shared<Type>(TypeKind::Any);
    vmType->fields["runInContext"] = runInContextType;

    // vm.runInNewContext(code: string, contextObject?: object, options?: object): any
    auto runInNewContextType = std::make_shared<FunctionType>();
    runInNewContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    runInNewContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    runInNewContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    runInNewContextType->returnType = std::make_shared<Type>(TypeKind::Any);
    vmType->fields["runInNewContext"] = runInNewContextType;

    // vm.runInThisContext(code: string, options?: object): any
    auto runInThisContextType = std::make_shared<FunctionType>();
    runInThisContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    runInThisContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    runInThisContextType->returnType = std::make_shared<Type>(TypeKind::Any);
    vmType->fields["runInThisContext"] = runInThisContextType;

    // vm.compileFunction(code: string, params?: string[], options?: object): Function
    auto compileFunctionType = std::make_shared<FunctionType>();
    compileFunctionType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    compileFunctionType->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));
    compileFunctionType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    compileFunctionType->returnType = std::make_shared<Type>(TypeKind::Function);
    vmType->fields["compileFunction"] = compileFunctionType;

    // vm.Script class - constructor takes code string and options
    auto scriptClass = std::make_shared<ClassType>("Script");

    // Script.runInContext(contextifiedObject: object, options?: object): any
    auto scriptRunInContextType = std::make_shared<FunctionType>();
    scriptRunInContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    scriptRunInContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    scriptRunInContextType->returnType = std::make_shared<Type>(TypeKind::Any);
    scriptClass->methods["runInContext"] = scriptRunInContextType;

    // Script.runInNewContext(contextObject?: object, options?: object): any
    auto scriptRunInNewContextType = std::make_shared<FunctionType>();
    scriptRunInNewContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    scriptRunInNewContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    scriptRunInNewContextType->returnType = std::make_shared<Type>(TypeKind::Any);
    scriptClass->methods["runInNewContext"] = scriptRunInNewContextType;

    // Script.runInThisContext(options?: object): any
    auto scriptRunInThisContextType = std::make_shared<FunctionType>();
    scriptRunInThisContextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    scriptRunInThisContextType->returnType = std::make_shared<Type>(TypeKind::Any);
    scriptClass->methods["runInThisContext"] = scriptRunInThisContextType;

    // Script.createCachedData(): Buffer
    auto createCachedDataType = std::make_shared<FunctionType>();
    createCachedDataType->returnType = std::make_shared<ClassType>("Buffer");
    scriptClass->methods["createCachedData"] = createCachedDataType;

    symbols.defineType("Script", scriptClass);
    vmType->fields["Script"] = scriptClass;

    symbols.define("vm", vmType);
}

} // namespace ts
