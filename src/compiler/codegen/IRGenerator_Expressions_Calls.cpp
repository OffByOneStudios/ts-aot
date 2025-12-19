#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;
void IRGenerator::visitCallExpression(ast::CallExpression* node) { 
    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        if (id->name == "fetch") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* arg = lastValue;
            
            llvm::FunctionCallee fetchFn = module->getOrInsertFunction("ts_fetch",
                llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
            
            lastValue = builder->CreateCall(fetchFn, { arg });
            return;
        }
    }

    if (auto superExpr = dynamic_cast<ast::SuperExpression*>(node->callee.get())) {
        // super() call in constructor
        if (!currentClass || currentClass->kind != TypeKind::Class) return;
        auto classType = std::static_pointer_cast<ClassType>(currentClass);
        if (!classType->baseClass) return;
        
        std::string baseClassName = classType->baseClass->name;
        std::string ctorName = baseClassName + "_constructor";
        llvm::Function* ctor = module->getFunction(ctorName);
        if (ctor) {
            std::vector<llvm::Value*> args;
            // 'this' is the first argument of the current function
            llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
            args.push_back(currentFunc->getArg(0));
            
            for (auto& arg : node->arguments) {
                visit(arg.get());
                args.push_back(lastValue);
            }
            builder->CreateCall(ctor, args);
        }
        return;
    }

    if (tryGenerateMemberCall(node)) {
        return;
    }

    // User function call
    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        if (namedValues.count(id->name)) {
            llvm::Value* val = namedValues[id->name];
            llvm::Type* varType = nullptr;
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
                varType = alloca->getAllocatedType();
            } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(val)) {
                varType = gep->getResultElementType();
            }

            if (varType && varType->isPointerTy()) {
                llvm::Value* funcPtr = builder->CreateLoad(varType, val, id->name.c_str());
                
                std::vector<llvm::Value*> args;
                std::vector<llvm::Type*> argTypes;
                for (auto& arg : node->arguments) {
                    visit(arg.get());
                    llvm::Value* v = lastValue;
                    
                    // Arrow functions always take TsValue* (ptr)
                    v = boxValue(v, arg->inferredType);
                    
                    args.push_back(v);
                    argTypes.push_back(v->getType());
                }
                
                llvm::Type* retType = builder->getPtrTy(); // Arrow functions always return TsValue* (ptr)
                llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
                lastValue = builder->CreateCall(ft, funcPtr, args);
                return;
            }
        }

        if (id->name == "setTimeout" || id->name == "setInterval") {
            if (node->arguments.size() < 2) return;
            
            visit(node->arguments[0].get());
            llvm::Value* callback = lastValue;
            
            // Ensure callback is a pointer to the function
            if (callback->getType()->isIntegerTy()) {
                callback = builder->CreateIntToPtr(callback, builder->getPtrTy());
            }
            
            llvm::Value* boxedCallback = boxValue(callback, node->arguments[0]->inferredType);
            
            visit(node->arguments[1].get());
            llvm::Value* delay = lastValue;
            if (delay->getType()->isDoubleTy()) {
                delay = builder->CreateFPToSI(delay, llvm::Type::getInt64Ty(*context));
            }
            
            std::string funcName = (id->name == "setTimeout") ? "ts_set_timeout" : "ts_set_interval";
            llvm::FunctionCallee timerFn = module->getOrInsertFunction(funcName,
                builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context));
            
            llvm::Value* boxedRes = builder->CreateCall(timerFn, { boxedCallback, delay });
            lastValue = unboxValue(boxedRes, std::make_shared<Type>(TypeKind::Int));
            return;
        }

        if (id->name == "clearTimeout" || id->name == "clearInterval") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* timerId = lastValue;
            llvm::Value* boxedId = boxValue(timerId, node->arguments[0]->inferredType);
            
            llvm::FunctionCallee clearFn = module->getOrInsertFunction("ts_clear_timer",
                llvm::Type::getVoidTy(*context), builder->getPtrTy());
            
            builder->CreateCall(clearFn, { boxedId });
            lastValue = nullptr;
            return;
        }

        if (id->name == "parseInt") {
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* arg = lastValue;
             if (arg->getType()->isIntegerTy(64)) {
                 arg = builder->CreateIntToPtr(arg, llvm::PointerType::getUnqual(*context));
             }

             llvm::FunctionCallee parseFn = module->getOrInsertFunction("ts_parseInt",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             
             lastValue = builder->CreateCall(parseFn, { arg });
             return;
        }

        if (id->name == "ts_console_log") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* arg = lastValue;
            
            std::string funcName = "ts_console_log";
            llvm::Type* paramType = llvm::PointerType::getUnqual(*context);

            if (arg->getType()->isIntegerTy(64)) {
                funcName = "ts_console_log_int";
                paramType = llvm::Type::getInt64Ty(*context);
            } else if (arg->getType()->isDoubleTy()) {
                funcName = "ts_console_log_double";
                paramType = llvm::Type::getDoubleTy(*context);
            } else if (arg->getType()->isIntegerTy(1)) {
                funcName = "ts_console_log_bool";
                paramType = llvm::Type::getInt1Ty(*context);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*context), { paramType }, false);
            llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

            builder->CreateCall(logFn, { arg });
            lastValue = nullptr;
            return;
        }

        std::string funcName = id->name;
        
        std::vector<llvm::Value*> args;
        std::vector<std::shared_ptr<Type>> argTypes;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            if (lastValue) {
                args.push_back(lastValue);
                if (arg->inferredType) {
                    argTypes.push_back(arg->inferredType);
                } else {
                    // Fallback if type inference failed
                    argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                }
            }
        }

        std::string mangledName = Monomorphizer::generateMangledName(funcName, argTypes, node->resolvedTypeArguments);

        llvm::Function* func = module->getFunction(mangledName);
        if (func) {
            lastValue = builder->CreateCall(func, args);
        } else {
            llvm::errs() << "Function not found: " << mangledName << "\n";
            // TODO: Error handling
            lastValue = nullptr;
        }
    }
}

} // namespace ts


