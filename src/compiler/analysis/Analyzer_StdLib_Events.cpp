#include "Analyzer.h"

namespace ts {

void Analyzer::registerEvents() {
    auto eventEmitterClass = std::make_shared<ClassType>("EventEmitter");
    
    // on(event: string, listener: Function): this
    auto onFn = std::make_shared<FunctionType>();
    onFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    onFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    onFn->returnType = eventEmitterClass;
    eventEmitterClass->methods["on"] = onFn;
    eventEmitterClass->methods["addListener"] = onFn;

    // once(event: string, listener: Function): this
    auto onceFn = std::make_shared<FunctionType>();
    onceFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    onceFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    onceFn->returnType = eventEmitterClass;
    eventEmitterClass->methods["once"] = onceFn;

    // prependListener(event: string, listener: Function): this
    eventEmitterClass->methods["prependListener"] = onFn;

    // prependOnceListener(event: string, listener: Function): this
    eventEmitterClass->methods["prependOnceListener"] = onceFn;

    // emit(event: string, ...args: any[]): boolean
    auto emitFn = std::make_shared<FunctionType>();
    emitFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    emitFn->returnType = std::make_shared<Type>(TypeKind::Boolean);
    eventEmitterClass->methods["emit"] = emitFn;

    // removeListener(event: string, listener: Function): this
    auto removeListenerFn = std::make_shared<FunctionType>();
    removeListenerFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    removeListenerFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    removeListenerFn->returnType = eventEmitterClass;
    eventEmitterClass->methods["removeListener"] = removeListenerFn;
    eventEmitterClass->methods["off"] = removeListenerFn;

    // removeAllListeners(event?: string): this
    auto removeAllListenersFn = std::make_shared<FunctionType>();
    removeAllListenersFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    removeAllListenersFn->returnType = eventEmitterClass;
    eventEmitterClass->methods["removeAllListeners"] = removeAllListenersFn;

    // setMaxListeners(n: number): this
    auto setMaxListenersFn = std::make_shared<FunctionType>();
    setMaxListenersFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setMaxListenersFn->returnType = eventEmitterClass;
    eventEmitterClass->methods["setMaxListeners"] = setMaxListenersFn;

    // listenerCount(event: string): number
    auto listenerCountFn = std::make_shared<FunctionType>();
    listenerCountFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    listenerCountFn->returnType = std::make_shared<Type>(TypeKind::Int);
    eventEmitterClass->methods["listenerCount"] = listenerCountFn;

    // listeners(event: string): Function[]
    auto listenersFn = std::make_shared<FunctionType>();
    listenersFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    auto funcArrayType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Function));
    listenersFn->returnType = funcArrayType;
    eventEmitterClass->methods["listeners"] = listenersFn;

    // rawListeners(event: string): Function[]
    auto rawListenersFn = std::make_shared<FunctionType>();
    rawListenersFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    rawListenersFn->returnType = funcArrayType;
    eventEmitterClass->methods["rawListeners"] = rawListenersFn;

    // getMaxListeners(): number
    auto getMaxListenersFn = std::make_shared<FunctionType>();
    getMaxListenersFn->returnType = std::make_shared<Type>(TypeKind::Int);
    eventEmitterClass->methods["getMaxListeners"] = getMaxListenersFn;

    // eventNames(): string[]
    auto eventNamesFn = std::make_shared<FunctionType>();
    auto stringArrayType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    eventNamesFn->returnType = stringArrayType;
    eventEmitterClass->methods["eventNames"] = eventNamesFn;

    symbols.defineType("EventEmitter", eventEmitterClass);

    auto eventsModule = std::make_shared<ObjectType>();
    eventsModule->fields["EventEmitter"] = eventEmitterClass;

    // events.once(emitter: EventEmitter, event: string): Promise<any[]>
    auto staticOnceFn = std::make_shared<FunctionType>();
    staticOnceFn->paramTypes.push_back(eventEmitterClass);
    staticOnceFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    
    auto promiseType = symbols.lookupType("Promise");
    if (promiseType) {
        staticOnceFn->returnType = promiseType;
    } else {
        staticOnceFn->returnType = std::make_shared<Type>(TypeKind::Any);
    }
    eventsModule->fields["once"] = staticOnceFn;

    symbols.define("events", eventsModule);
}

} // namespace ts
