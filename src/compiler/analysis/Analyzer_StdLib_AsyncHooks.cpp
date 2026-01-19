#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerAsyncHooks() {
    auto asyncHooksType = std::make_shared<ObjectType>();

    // =========================================================================
    // AsyncLocalStorage<T> class
    // Provides async context propagation through async operations
    // =========================================================================
    auto asyncLocalStorageClass = std::make_shared<ClassType>("AsyncLocalStorage");

    // Constructor: new AsyncLocalStorage<T>()
    auto alsCtorType = std::make_shared<FunctionType>();
    alsCtorType->returnType = asyncLocalStorageClass;
    asyncLocalStorageClass->constructorOverloads.push_back(alsCtorType);

    // getStore(): T | undefined
    // Returns the current store value
    auto getStoreType = std::make_shared<FunctionType>();
    getStoreType->returnType = std::make_shared<Type>(TypeKind::Any);
    asyncLocalStorageClass->methods["getStore"] = getStoreType;

    // run(store: T, callback: () => R, ...args: any[]): R
    // Runs a function with the specified store value
    auto runType = std::make_shared<FunctionType>();
    runType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // store
    runType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));  // callback
    runType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // ...args
    runType->hasRest = true;
    runType->returnType = std::make_shared<Type>(TypeKind::Any);
    asyncLocalStorageClass->methods["run"] = runType;

    // exit(callback: () => R, ...args: any[]): R
    // Runs a function with no store value
    auto exitType = std::make_shared<FunctionType>();
    exitType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));  // callback
    exitType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // ...args
    exitType->hasRest = true;
    exitType->returnType = std::make_shared<Type>(TypeKind::Any);
    asyncLocalStorageClass->methods["exit"] = exitType;

    // enterWith(store: T): void
    // Sets the store value for the current execution context
    auto enterWithType = std::make_shared<FunctionType>();
    enterWithType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // store
    enterWithType->returnType = std::make_shared<Type>(TypeKind::Void);
    asyncLocalStorageClass->methods["enterWith"] = enterWithType;

    // disable(): void
    // Disables the AsyncLocalStorage instance
    auto disableType = std::make_shared<FunctionType>();
    disableType->returnType = std::make_shared<Type>(TypeKind::Void);
    asyncLocalStorageClass->methods["disable"] = disableType;

    // Register the class
    symbols.defineType("AsyncLocalStorage", asyncLocalStorageClass);
    asyncHooksType->fields["AsyncLocalStorage"] = asyncLocalStorageClass;

    // =========================================================================
    // AsyncResource class
    // Low-level resource tracking for async operations
    // =========================================================================
    auto asyncResourceClass = std::make_shared<ClassType>("AsyncResource");

    // Constructor: new AsyncResource(type: string, triggerAsyncId?: number)
    auto arCtorType = std::make_shared<FunctionType>();
    arCtorType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // type
    arCtorType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // triggerAsyncId (optional)
    arCtorType->isOptional = { false, true };
    arCtorType->returnType = asyncResourceClass;
    asyncResourceClass->constructorOverloads.push_back(arCtorType);

    // asyncId(): number
    auto asyncIdMethod = std::make_shared<FunctionType>();
    asyncIdMethod->returnType = std::make_shared<Type>(TypeKind::Int);
    asyncResourceClass->methods["asyncId"] = asyncIdMethod;

    // triggerAsyncId(): number
    auto triggerAsyncIdMethod = std::make_shared<FunctionType>();
    triggerAsyncIdMethod->returnType = std::make_shared<Type>(TypeKind::Int);
    asyncResourceClass->methods["triggerAsyncId"] = triggerAsyncIdMethod;

    // runInAsyncScope(fn: Function, thisArg?: any, ...args: any[]): any
    auto runInAsyncScopeType = std::make_shared<FunctionType>();
    runInAsyncScopeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));  // fn
    runInAsyncScopeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // thisArg
    runInAsyncScopeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // ...args
    runInAsyncScopeType->hasRest = true;
    runInAsyncScopeType->returnType = std::make_shared<Type>(TypeKind::Any);
    asyncResourceClass->methods["runInAsyncScope"] = runInAsyncScopeType;

    // bind(fn: Function): Function
    auto bindMethod = std::make_shared<FunctionType>();
    bindMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    bindMethod->returnType = std::make_shared<Type>(TypeKind::Function);
    asyncResourceClass->methods["bind"] = bindMethod;

    // emitDestroy(): AsyncResource
    auto emitDestroyType = std::make_shared<FunctionType>();
    emitDestroyType->returnType = asyncResourceClass;
    asyncResourceClass->methods["emitDestroy"] = emitDestroyType;

    // Register the class
    symbols.defineType("AsyncResource", asyncResourceClass);
    asyncHooksType->fields["AsyncResource"] = asyncResourceClass;

    // =========================================================================
    // Module-level functions
    // =========================================================================

    // executionAsyncId(): number
    // Returns the async ID of the current execution context
    auto executionAsyncIdType = std::make_shared<FunctionType>();
    executionAsyncIdType->returnType = std::make_shared<Type>(TypeKind::Int);
    asyncHooksType->fields["executionAsyncId"] = executionAsyncIdType;

    // triggerAsyncId(): number
    // Returns the trigger async ID of the current execution context
    auto triggerAsyncIdFnType = std::make_shared<FunctionType>();
    triggerAsyncIdFnType->returnType = std::make_shared<Type>(TypeKind::Int);
    asyncHooksType->fields["triggerAsyncId"] = triggerAsyncIdFnType;

    // executionAsyncResource(): object | undefined
    // Returns the async resource for the current execution context
    auto executionAsyncResourceType = std::make_shared<FunctionType>();
    executionAsyncResourceType->returnType = std::make_shared<Type>(TypeKind::Any);
    asyncHooksType->fields["executionAsyncResource"] = executionAsyncResourceType;

    // =========================================================================
    // AsyncHook interface (for createHook callbacks)
    // =========================================================================
    auto asyncHookClass = std::make_shared<ClassType>("AsyncHook");

    // enable(): AsyncHook
    auto enableType = std::make_shared<FunctionType>();
    enableType->returnType = asyncHookClass;
    asyncHookClass->methods["enable"] = enableType;

    // disable(): AsyncHook
    auto disableHookType = std::make_shared<FunctionType>();
    disableHookType->returnType = asyncHookClass;
    asyncHookClass->methods["disable"] = disableHookType;

    symbols.defineType("AsyncHook", asyncHookClass);
    asyncHooksType->fields["AsyncHook"] = asyncHookClass;

    // createHook(callbacks: AsyncHookCallbacks): AsyncHook
    // Creates an async hook for tracking async operations
    auto createHookType = std::make_shared<FunctionType>();
    createHookType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // callbacks object
    createHookType->returnType = asyncHookClass;
    asyncHooksType->fields["createHook"] = createHookType;

    // Register the async_hooks module
    symbols.define("async_hooks", asyncHooksType);
}

} // namespace ts
