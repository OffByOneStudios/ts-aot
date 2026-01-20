#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerInspector() {
    // =========================================================================
    // inspector module - Stubbed (AOT incompatible)
    //
    // The inspector module provides an API for interacting with the V8 inspector
    // (Chrome DevTools Protocol). Since ts-aot compiles to native code via LLVM
    // and doesn't use V8, real inspector functionality is not available.
    //
    // These stubs allow code to compile and run without errors, but inspector
    // features will be no-ops at runtime.
    // =========================================================================
    auto inspectorType = std::make_shared<ObjectType>();

    // inspector.Session class
    auto sessionClass = std::make_shared<ClassType>("InspectorSession");

    // Session.connect(): void
    auto connectType = std::make_shared<FunctionType>();
    connectType->returnType = std::make_shared<Type>(TypeKind::Void);
    sessionClass->methods["connect"] = connectType;

    // Session.connectToMainThread(): void
    auto connectToMainThreadType = std::make_shared<FunctionType>();
    connectToMainThreadType->returnType = std::make_shared<Type>(TypeKind::Void);
    sessionClass->methods["connectToMainThread"] = connectToMainThreadType;

    // Session.disconnect(): void
    auto disconnectType = std::make_shared<FunctionType>();
    disconnectType->returnType = std::make_shared<Type>(TypeKind::Void);
    sessionClass->methods["disconnect"] = disconnectType;

    // Session.post(method: string, params?: object, callback?: Function): void
    auto postType = std::make_shared<FunctionType>();
    postType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    postType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // params (optional)
    postType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // callback (optional)
    postType->returnType = std::make_shared<Type>(TypeKind::Void);
    sessionClass->methods["post"] = postType;

    // Session extends EventEmitter, so it has on(), once(), emit(), etc.
    // The EventEmitter methods are inherited

    // Register Session class
    symbols.defineType("InspectorSession", sessionClass);
    inspectorType->fields["Session"] = sessionClass;

    // inspector.open(port?: number, host?: string, wait?: boolean): void
    auto openType = std::make_shared<FunctionType>();
    openType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));     // port (optional)
    openType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // host (optional)
    openType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // wait (optional)
    openType->returnType = std::make_shared<Type>(TypeKind::Void);
    inspectorType->fields["open"] = openType;

    // inspector.close(): void
    auto closeType = std::make_shared<FunctionType>();
    closeType->returnType = std::make_shared<Type>(TypeKind::Void);
    inspectorType->fields["close"] = closeType;

    // inspector.url(): string | undefined
    auto urlType = std::make_shared<FunctionType>();
    urlType->returnType = std::make_shared<Type>(TypeKind::String);  // returns undefined if not active
    inspectorType->fields["url"] = urlType;

    // inspector.waitForDebugger(): void
    auto waitForDebuggerType = std::make_shared<FunctionType>();
    waitForDebuggerType->returnType = std::make_shared<Type>(TypeKind::Void);
    inspectorType->fields["waitForDebugger"] = waitForDebuggerType;

    // inspector.console: object (for sending messages to DevTools console)
    auto consoleType = std::make_shared<ObjectType>();
    inspectorType->fields["console"] = consoleType;

    symbols.define("inspector", inspectorType);
}

} // namespace ts
