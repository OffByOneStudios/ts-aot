#include "IRGenerator.h"
#include "BoxingPolicy.h"

namespace ts {

// Static helper to register Events module's runtime functions once (8 functions)
static bool eventsFunctionsRegistered = false;
static void ensureEventsFunctionsRegistered(BoxingPolicy& bp) {
    if (eventsFunctionsRegistered) return;
    eventsFunctionsRegistered = true;
    
    bp.registerRuntimeApi("ts_event_emitter_on", {true, false, true}, true);  // emitter, event, listener
    bp.registerRuntimeApi("ts_event_emitter_once", {true, false, true}, true);
    bp.registerRuntimeApi("ts_event_emitter_emit", {true, false, true}, false);  // returns bool
    bp.registerRuntimeApi("ts_event_emitter_prepend_listener", {true, false, true}, true);
    bp.registerRuntimeApi("ts_event_emitter_prepend_once_listener", {true, false, true}, true);
    bp.registerRuntimeApi("ts_event_emitter_remove_all_listeners", {true, false}, true);
    bp.registerRuntimeApi("ts_event_emitter_set_max_listeners", {true, false}, true);  // emitter, n
    bp.registerRuntimeApi("ts_event_emitter_static_once", {true, false}, true);  // events.once()
}

bool IRGenerator::tryGenerateEventsCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureEventsFunctionsRegistered(boxingPolicy);
    
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "events" && prop->name == "once") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* emitter = lastValue;
            visit(node->arguments[1].get());
            llvm::Value* event = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_event_emitter_static_once", ft);
            lastValue = createCall(ft, fn.getCallee(), { emitter, event });
            return true;
        }
    }

    if (prop->name == "on" || prop->name == "addListener" || prop->name == "once" || 
        prop->name == "prependListener" || prop->name == "prependOnceListener") {
        visit(prop->expression.get());
        llvm::Value* boxedObj = lastValue;
        llvm::Value* obj = unboxValue(boxedObj, prop->expression->inferredType);
        
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
        else if (prop->name == "prependOnceListener") {
            // We don't have a specific ts_event_emitter_prepend_once_listener yet,
            // but we can implement it or just use once for now (though it won't prepend).
            // Actually, let's implement it in the runtime.
            fnName = "ts_event_emitter_prepend_once_listener";
        }

        llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
        createCall(ft, fn.getCallee(), { obj, event, callback });
        
        // Return 'this' for chaining
        lastValue = boxedObj;
        return true;
    } else if (prop->name == "removeAllListeners") {
        visit(prop->expression.get());
        llvm::Value* boxedObj = lastValue;
        llvm::Value* obj = unboxValue(boxedObj, prop->expression->inferredType);
        
        llvm::Value* event = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            event = lastValue;
        } else {
            event = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_event_emitter_remove_all_listeners", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, event });
        
        // Return 'this' for chaining
        lastValue = boxedObj;
        return true;
    } else if (prop->name == "setMaxListeners") {
        visit(prop->expression.get());
        llvm::Value* boxedObj = lastValue;
        llvm::Value* obj = unboxValue(boxedObj, prop->expression->inferredType);
        
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* n = lastValue;
        
        // Ensure n is an i32
        n = builder->CreateIntCast(n, llvm::Type::getInt32Ty(*context), true);
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_event_emitter_set_max_listeners", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, n });
        
        // Return 'this' for chaining
        lastValue = boxedObj;
        return true;
    } else if (prop->name == "emit") {
        visit(prop->expression.get());
        llvm::Value* boxedObj = lastValue;
        llvm::Value* obj = unboxValue(boxedObj, prop->expression->inferredType);
        
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
        llvm::FunctionCallee emitFn = module->getOrInsertFunction("ts_event_emitter_emit", emitFt);
        lastValue = createCall(emitFt, emitFn.getCallee(), { obj, event, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), args.size()), argv });
        
        // Box the result (boolean)
        lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
        return true;
    }
    
    return false;
}

bool IRGenerator::tryGenerateEventsPropertyAccess(ast::PropertyAccessExpression* node) {
    return false;
}

} // namespace ts
