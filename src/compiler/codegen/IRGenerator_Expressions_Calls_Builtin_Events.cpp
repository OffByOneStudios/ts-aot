#include "IRGenerator.h"
#include "BoxingPolicy.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register Events module's runtime functions once (12 functions)
static bool eventsFunctionsRegistered = false;
static void ensureEventsFunctionsRegistered(BoxingPolicy& bp) {
    if (eventsFunctionsRegistered) return;
    eventsFunctionsRegistered = true;

    bp.registerRuntimeApi("ts_event_emitter_on", {false, false, true}, true);  // (emitter, event, listener) -> void*
    bp.registerRuntimeApi("ts_event_emitter_once", {false, false, true}, true);  // (emitter, event, listener) -> void*
    bp.registerRuntimeApi("ts_event_emitter_emit", {false, false, false, true}, false);  // (emitter, event, argc, argv) -> bool
    bp.registerRuntimeApi("ts_event_emitter_prepend_listener", {false, false, true}, true);  // (emitter, event, listener) -> void*
    bp.registerRuntimeApi("ts_event_emitter_prepend_once_listener", {false, false, true}, true);  // (emitter, event, listener) -> void*
    bp.registerRuntimeApi("ts_event_emitter_remove_listener", {false, false, true}, true);  // (emitter, event, listener) -> void*
    bp.registerRuntimeApi("ts_event_emitter_remove_all_listeners", {false, false}, true);  // (emitter, event?) -> void*
    bp.registerRuntimeApi("ts_event_emitter_set_max_listeners", {false, false}, true);  // (emitter, n) -> void*
    bp.registerRuntimeApi("ts_event_emitter_get_max_listeners", {false}, false);  // (emitter) -> int
    bp.registerRuntimeApi("ts_event_emitter_listener_count", {false, false}, false);  // (emitter, event) -> int
    bp.registerRuntimeApi("ts_event_emitter_event_names", {false}, true);  // (emitter) -> array
    bp.registerRuntimeApi("ts_event_emitter_listeners", {false, false}, true);  // (emitter, event) -> array
    bp.registerRuntimeApi("ts_event_emitter_raw_listeners", {false, false}, true);  // (emitter, event) -> array
    bp.registerRuntimeApi("ts_event_emitter_static_once", {false, false}, true);  // (emitter, event) -> promise
    bp.registerRuntimeApi("ts_event_emitter_static_on", {false, false}, true);    // (emitter, event) -> async iterator
    bp.registerRuntimeApi("ts_event_emitter_static_listener_count", {false, false}, false);  // (emitter, event) -> int (deprecated)
}

bool IRGenerator::tryGenerateEventsCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureEventsFunctionsRegistered(boxingPolicy);

    SPDLOG_DEBUG("tryGenerateEventsCall: checking method '{}'", prop->name);

    // Check for events.EventEmitter.listenerCount(emitter, event) - static method (deprecated)
    if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
        if (auto id = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
            if (id->name == "events" && innerProp->name == "EventEmitter" && prop->name == "listenerCount") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* emitter = lastValue;
                visit(node->arguments[1].get());
                llvm::Value* event = lastValue;

                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_static_listener_count", ft);
                lastValue = createCall(ft, fn.getCallee(), { emitter, event });
                return true;
            }
        }
    }

    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "events" && prop->name == "once") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* emitter = lastValue;
            visit(node->arguments[1].get());
            llvm::Value* event = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_static_once", ft);
            lastValue = createCall(ft, fn.getCallee(), { emitter, event });
            return true;
        }

        if (id->name == "events" && prop->name == "on") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* emitter = lastValue;
            visit(node->arguments[1].get());
            llvm::Value* event = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_static_on", ft);
            lastValue = createCall(ft, fn.getCallee(), { emitter, event });
            return true;
        }
    }

    if (prop->name == "on" || prop->name == "addListener" || prop->name == "once" ||
        prop->name == "prependListener" || prop->name == "prependOnceListener" ||
        prop->name == "removeListener" || prop->name == "off") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Box the emitter if not already boxed
        if (!boxedValues.count(obj)) {
            obj = boxValue(obj, prop->expression->inferredType);
        }

        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* event = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;

        // Box the callback if it's a function
        if (node->arguments[1]->getKind() == "ArrowFunction" || node->arguments[1]->getKind() == "FunctionExpression") {
            callback = boxValue(callback, std::make_shared<Type>(TypeKind::Function));
        } else {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);

        const char* fnName = "ts_event_emitter_on";
        if (prop->name == "once") fnName = "ts_event_emitter_once";
        else if (prop->name == "prependListener") fnName = "ts_event_emitter_prepend_listener";
        else if (prop->name == "prependOnceListener") fnName = "ts_event_emitter_prepend_once_listener";
        else if (prop->name == "removeListener" || prop->name == "off") fnName = "ts_event_emitter_remove_listener";

        llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
        createCall(ft, fn.getCallee(), { obj, event, callback });

        // Return 'this' for chaining
        lastValue = obj;
        return true;
    } else if (prop->name == "removeAllListeners") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Box the emitter if not already boxed
        if (!boxedValues.count(obj)) {
            obj = boxValue(obj, prop->expression->inferredType);
        }

        llvm::Value* event = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            event = lastValue;
        } else {
            event = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_remove_all_listeners", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, event });

        // Return 'this' for chaining
        lastValue = obj;
        return true;
    } else if (prop->name == "setMaxListeners") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Box the emitter if not already boxed
        if (!boxedValues.count(obj)) {
            obj = boxValue(obj, prop->expression->inferredType);
        }

        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* n = lastValue;

        // Ensure n is an i32
        n = builder->CreateIntCast(n, llvm::Type::getInt32Ty(*context), true);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_set_max_listeners", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, n });

        // Return 'this' for chaining
        lastValue = obj;
        return true;
    } else if (prop->name == "emit") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Box the emitter if not already boxed
        if (!boxedValues.count(obj)) {
            obj = boxValue(obj, prop->expression->inferredType);
        }

        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* event = lastValue;
        
        // Collect remaining arguments into an array
        std::vector<llvm::Value*> args;
        for (size_t i = 1; i < node->arguments.size(); ++i) {
            visit(node->arguments[i].get());
            args.push_back(boxValue(lastValue, node->arguments[i]->inferredType));
        }
        
        // Create a raw array of TsValue*
        llvm::Type* ptrTy = builder->getPtrTy();
        llvm::Value* argv = builder->CreateAlloca(ptrTy, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), args.size()));
        for (size_t i = 0; i < args.size(); ++i) {
            llvm::Value* ptr = builder->CreateGEP(ptrTy, argv, { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), i) });
            builder->CreateStore(args[i], ptr);
        }
        
        llvm::FunctionType* emitFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt32Ty(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee emitFn = getRuntimeFunction("ts_event_emitter_emit", emitFt);
        lastValue = createCall(emitFt, emitFn.getCallee(), { obj, event, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), args.size()), argv });
        
        // Box the result (boolean)
        lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
        return true;
    } else if (prop->name == "listenerCount") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Box the emitter if not already boxed
        if (!boxedValues.count(obj)) {
            obj = boxValue(obj, prop->expression->inferredType);
        }

        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* event = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_listener_count", ft);
        llvm::Value* count = createCall(ft, fn.getCallee(), { obj, event });

        // Box the result (int)
        lastValue = boxValue(count, std::make_shared<Type>(TypeKind::Int));
        return true;
    } else if (prop->name == "getMaxListeners") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Box the emitter if not already boxed
        if (!boxedValues.count(obj)) {
            obj = boxValue(obj, prop->expression->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_get_max_listeners", ft);
        llvm::Value* maxListeners = createCall(ft, fn.getCallee(), { obj });

        // Box the result (int)
        lastValue = boxValue(maxListeners, std::make_shared<Type>(TypeKind::Int));
        return true;
    } else if (prop->name == "eventNames") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Box the emitter if not already boxed
        if (!boxedValues.count(obj)) {
            obj = boxValue(obj, prop->expression->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_event_names", ft);
        llvm::Value* names = createCall(ft, fn.getCallee(), { obj });

        // Box the result (array)
        lastValue = boxValue(names, std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));
        return true;
    } else if (prop->name == "listeners") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Box the emitter if not already boxed
        if (!boxedValues.count(obj)) {
            obj = boxValue(obj, prop->expression->inferredType);
        }

        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* event = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_listeners", ft);
        llvm::Value* listeners = createCall(ft, fn.getCallee(), { obj, event });

        // Box the result (array of functions)
        lastValue = boxValue(listeners, std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Function)));
        return true;
    } else if (prop->name == "rawListeners") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Box the emitter if not already boxed
        if (!boxedValues.count(obj)) {
            obj = boxValue(obj, prop->expression->inferredType);
        }

        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* event = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_raw_listeners", ft);
        llvm::Value* rawListeners = createCall(ft, fn.getCallee(), { obj, event });

        // Box the result (array of functions)
        lastValue = boxValue(rawListeners, std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Function)));
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateEventsPropertyAccess(ast::PropertyAccessExpression* node) {
    return false;
}

} // namespace ts
