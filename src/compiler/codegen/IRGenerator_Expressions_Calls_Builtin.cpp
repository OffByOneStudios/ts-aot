#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;

bool IRGenerator::tryGenerateBuiltinCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    llvm::errs() << "Trying to generate builtin call for " << prop->name << "\n";

    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
        if (classType->name == "Date") {
            visit(prop->expression.get());
            llvm::Value* dateObj = lastValue;
            
            std::string methodName = prop->name;
            std::string funcName = "Date_" + methodName;
            
            llvm::Type* retType = llvm::Type::getInt64Ty(*context);
            std::vector<llvm::Type*> paramTypes = { builder->getPtrTy() };
            
            if (methodName == "toISOString" || methodName == "toString" || methodName == "toDateString") {
                retType = builder->getPtrTy();
            } else if (methodName.substr(0, 3) == "set") {
                retType = llvm::Type::getVoidTy(*context);
                paramTypes.push_back(llvm::Type::getInt64Ty(*context));
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(retType, paramTypes, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
            
            std::vector<llvm::Value*> args = { dateObj };
            for (auto& arg : node->arguments) {
                visit(arg.get());
                llvm::Value* val = lastValue;
                if (val->getType()->isDoubleTy()) {
                    val = builder->CreateFPToSI(val, llvm::Type::getInt64Ty(*context));
                }
                args.push_back(val);
            }
            
            lastValue = builder->CreateCall(fn, args);
            return true;
        }
    }

    if (prop->name == "log") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "console") {
                llvm::errs() << "Found console.log\n";
                if (node->arguments.empty()) return true;
                
                for (size_t i = 0; i < node->arguments.size(); ++i) {
                    visit(node->arguments[i].get());
                    llvm::Value* arg = lastValue;
                    if (!arg) {
                        llvm::errs() << "Error: console.log argument evaluated to null\n";
                        continue;
                    }

                    llvm::Type* argType = arg->getType();

                    if (argType->isVoidTy()) {
                        llvm::errs() << "Error: Argument to console.log is void\n";
                        continue;
                    }

                    std::string funcName = "ts_console_log";
                    llvm::Type* paramType = llvm::PointerType::getUnqual(*context);

                    if (argType->isIntegerTy(64)) {
                        funcName = "ts_console_log_int";
                        paramType = llvm::Type::getInt64Ty(*context);
                    } else if (argType->isDoubleTy()) {
                        funcName = "ts_console_log_double";
                        paramType = llvm::Type::getDoubleTy(*context);
                    } else if (argType->isIntegerTy(1)) {
                        funcName = "ts_console_log_bool";
                        paramType = llvm::Type::getInt1Ty(*context);
                    } else if (argType->isPointerTy()) {
                        // If it's a pointer, it could be a TsString* or a TsValue*
                        // For now, assume it's a TsValue* and use ts_console_log_value
                        // unless we know for sure it's a string.
                        if (node->arguments[i]->inferredType && node->arguments[i]->inferredType->kind == TypeKind::String) {
                            funcName = "ts_console_log";
                        } else {
                            funcName = "ts_console_log_value";
                        }
                        paramType = llvm::PointerType::getUnqual(*context);
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        llvm::Type::getVoidTy(*context), { paramType }, false);
                    llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

                    builder->CreateCall(logFn, { arg });
                }
                lastValue = nullptr;
                return true;
            }
        }
    } else if (prop->name == "readFileSync") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "fs") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    arg = builder->CreateIntToPtr(arg, llvm::PointerType::getUnqual(*context));
                }
                
                llvm::FunctionCallee readFn = module->getOrInsertFunction("ts_fs_readFileSync",
                    llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                        { llvm::PointerType::getUnqual(*context) }, false));
                
                lastValue = builder->CreateCall(readFn, { arg });
                return true;
            }
        }
    } else if (prop->name == "readFile") {
        if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
            if (auto obj = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                if (obj->name == "fs" && innerProp->name == "promises") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    llvm::FunctionCallee readFn = module->getOrInsertFunction("ts_fs_readFile_async",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                    
                    lastValue = builder->CreateCall(readFn, { arg });
                    return true;
                }
            }
        }
    } else if (prop->name == "writeFileSync") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "fs") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                if (path->getType()->isIntegerTy(64)) {
                    path = builder->CreateIntToPtr(path, llvm::PointerType::getUnqual(*context));
                }
                visit(node->arguments[1].get());
                llvm::Value* content = lastValue;
                if (content->getType()->isIntegerTy(64)) {
                    content = builder->CreateIntToPtr(content, llvm::PointerType::getUnqual(*context));
                }
                
                llvm::FunctionCallee writeFn = module->getOrInsertFunction("ts_fs_writeFileSync",
                    llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                        { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
                
                builder->CreateCall(writeFn, { path, content });
                return true;
            }
        }
    } else if (prop->name == "existsSync") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "fs") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                
                llvm::FunctionCallee existsFn = module->getOrInsertFunction("ts_fs_existsSync",
                    llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), 
                        { builder->getPtrTy() }, false));
                
                lastValue = builder->CreateCall(existsFn, { arg });
                return true;
            }
        }
    } else if (prop->name == "writeFile") {
        if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
            if (auto obj = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                if (obj->name == "fs" && innerProp->name == "promises") {
                    if (node->arguments.size() < 2) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* path = lastValue;
                    visit(node->arguments[1].get());
                    llvm::Value* content = lastValue;
                    
                    llvm::FunctionCallee writeFn = module->getOrInsertFunction("ts_fs_writeFile_async",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    
                    lastValue = builder->CreateCall(writeFn, { path, content });
                    return true;
                }
            }
        }
    } else if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (obj->name == "Math") {
            if (prop->name == "min" || prop->name == "max") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg1 = lastValue;
                visit(node->arguments[1].get());
                llvm::Value* arg2 = lastValue;
                
                std::string funcName = (prop->name == "min") ? "ts_math_min" : "ts_math_max";
                llvm::FunctionCallee fn = module->getOrInsertFunction(funcName,
                    llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg1, arg2 });
                return true;
            } else if (prop->name == "abs") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_abs",
                    llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { llvm::Type::getInt64Ty(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg });
                return true;
            } else if (prop->name == "floor") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                
                if (arg->getType()->isIntegerTy(64)) {
                    // floor(int) is just int
                    lastValue = arg;
                    return true;
                }
                
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_floor",
                    llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { llvm::Type::getDoubleTy(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg });
                return true;
            } else if (prop->name == "ceil") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    lastValue = arg;
                    return true;
                }
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_ceil",
                    llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { llvm::Type::getDoubleTy(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg });
                return true;
            } else if (prop->name == "round") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    lastValue = arg;
                    return true;
                }
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_round",
                    llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { llvm::Type::getDoubleTy(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg });
                return true;
            } else if (prop->name == "sqrt") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    arg = builder->CreateSIToFP(arg, llvm::Type::getDoubleTy(*context));
                }
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_sqrt",
                    llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                        { llvm::Type::getDoubleTy(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg });
                return true;
            } else if (prop->name == "pow") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg1 = lastValue;
                visit(node->arguments[1].get());
                llvm::Value* arg2 = lastValue;
                if (arg1->getType()->isIntegerTy(64)) arg1 = builder->CreateSIToFP(arg1, llvm::Type::getDoubleTy(*context));
                if (arg2->getType()->isIntegerTy(64)) arg2 = builder->CreateSIToFP(arg2, llvm::Type::getDoubleTy(*context));
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_pow",
                    llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                        { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg1, arg2 });
                return true;
            } else if (prop->name == "random") {
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_random",
                    llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), {}, false));
                lastValue = builder->CreateCall(fn);
                return true;
            } else if (prop->name == "log10" || prop->name == "log2" || prop->name == "log1p" ||
                       prop->name == "expm1" || prop->name == "cosh" || prop->name == "sinh" ||
                       prop->name == "tanh" || prop->name == "acosh" || prop->name == "asinh" ||
                       prop->name == "atanh" || prop->name == "cbrt" || prop->name == "trunc" ||
                       prop->name == "sign") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    arg = builder->CreateSIToFP(arg, llvm::Type::getDoubleTy(*context));
                }
                std::string fnName = "ts_math_" + prop->name;
                llvm::FunctionCallee fn = module->getOrInsertFunction(fnName,
                    llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                        { llvm::Type::getDoubleTy(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg });
                return true;
            } else if (prop->name == "hypot") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg1 = lastValue;
                visit(node->arguments[1].get());
                llvm::Value* arg2 = lastValue;
                if (arg1->getType()->isIntegerTy(64)) arg1 = builder->CreateSIToFP(arg1, llvm::Type::getDoubleTy(*context));
                if (arg2->getType()->isIntegerTy(64)) arg2 = builder->CreateSIToFP(arg2, llvm::Type::getDoubleTy(*context));
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_hypot",
                    llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                        { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg1, arg2 });
                return true;
            } else if (prop->name == "fround") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    arg = builder->CreateSIToFP(arg, llvm::Type::getDoubleTy(*context));
                }
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_fround",
                    llvm::FunctionType::get(llvm::Type::getFloatTy(*context),
                        { llvm::Type::getDoubleTy(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg });
                lastValue = builder->CreateFPExt(lastValue, llvm::Type::getDoubleTy(*context));
                return true;
            } else if (prop->name == "clz32") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isDoubleTy()) {
                    arg = builder->CreateFPToSI(arg, llvm::Type::getInt32Ty(*context));
                } else if (arg->getType()->isIntegerTy(64)) {
                    arg = builder->CreateTrunc(arg, llvm::Type::getInt32Ty(*context));
                }
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_clz32",
                    llvm::FunctionType::get(llvm::Type::getInt32Ty(*context),
                        { llvm::Type::getInt32Ty(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg });
                lastValue = builder->CreateSExt(lastValue, llvm::Type::getInt64Ty(*context));
                return true;
            }
        } else if (obj->name == "Date") {
            if (prop->name == "now") {
                llvm::FunctionCallee fn = module->getOrInsertFunction("Date_static_now",
                    llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false));
                lastValue = builder->CreateCall(fn);
                return true;
            }
        } else if (obj->name == "Promise") {
            if (prop->name == "resolve") {
                llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve",
                    builder->getPtrTy(), builder->getPtrTy());
                
                llvm::Value* val;
                std::shared_ptr<Type> valType;
                if (node->arguments.empty()) {
                    val = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    valType = std::make_shared<Type>(TypeKind::Void);
                } else {
                    visit(node->arguments[0].get());
                    val = lastValue;
                    valType = node->arguments[0]->inferredType;
                }
                
                llvm::Value* boxedVal = boxValue(val, valType);
                lastValue = builder->CreateCall(resolveFn, { boxedVal });
                return true;
            } else if (prop->name == "reject") {
                llvm::FunctionCallee rejectFn = module->getOrInsertFunction("ts_promise_reject",
                    builder->getPtrTy(), builder->getPtrTy());
                
                llvm::Value* val;
                std::shared_ptr<Type> valType;
                if (node->arguments.empty()) {
                    val = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    valType = std::make_shared<Type>(TypeKind::Void);
                } else {
                    visit(node->arguments[0].get());
                    val = lastValue;
                    valType = node->arguments[0]->inferredType;
                }
                
                llvm::Value* boxedVal = boxValue(val, valType);
                lastValue = builder->CreateCall(rejectFn, { boxedVal });
                return true;
            } else if (prop->name == "all") {
                if (node->arguments.empty()) return true;
                llvm::FunctionCallee allFn = module->getOrInsertFunction("ts_promise_all",
                    builder->getPtrTy(), builder->getPtrTy());
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = builder->CreateCall(allFn, { iterable });
                return true;
            } else if (prop->name == "race") {
                if (node->arguments.empty()) return true;
                llvm::FunctionCallee raceFn = module->getOrInsertFunction("ts_promise_race",
                    builder->getPtrTy(), builder->getPtrTy());
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = builder->CreateCall(raceFn, { iterable });
                return true;
            } else if (prop->name == "allSettled") {
                if (node->arguments.empty()) return true;
                llvm::FunctionCallee allSettledFn = module->getOrInsertFunction("ts_promise_allSettled",
                    builder->getPtrTy(), builder->getPtrTy());
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = builder->CreateCall(allSettledFn, { iterable });
                return true;
            } else if (prop->name == "any") {
                if (node->arguments.empty()) return true;
                llvm::FunctionCallee anyFn = module->getOrInsertFunction("ts_promise_any",
                    builder->getPtrTy(), builder->getPtrTy());
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = builder->CreateCall(anyFn, { iterable });
                return true;
            }
        }
    } else if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
        if (classType->name == "Date") {
            visit(prop->expression.get());
            llvm::Value* dateObj = lastValue;
            
            std::string methodName = prop->name;
            std::string funcName = "Date_" + methodName;
            
            llvm::Type* retType = llvm::Type::getInt64Ty(*context);
            std::vector<llvm::Type*> paramTypes = { builder->getPtrTy() };
            
            if (methodName == "toISOString" || methodName == "toString" || methodName == "toDateString") {
                retType = builder->getPtrTy();
            } else if (methodName.substr(0, 3) == "set") {
                retType = llvm::Type::getVoidTy(*context);
                paramTypes.push_back(llvm::Type::getInt64Ty(*context));
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(retType, paramTypes, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
            
            std::vector<llvm::Value*> args = { dateObj };
            for (auto& arg : node->arguments) {
                visit(arg.get());
                llvm::Value* val = lastValue;
                if (val->getType()->isDoubleTy()) {
                    val = builder->CreateFPToSI(val, llvm::Type::getInt64Ty(*context));
                }
                args.push_back(val);
            }
            
            lastValue = builder->CreateCall(fn, args);
            return true;
        }
    }

    if (prop->name == "charCodeAt") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* index = lastValue;
         
         if (index->getType()->isDoubleTy()) {
             index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
         }

         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_charCodeAt",
             llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
         
         lastValue = builder->CreateCall(fn, { obj, index });
         return true;
    } else if (prop->name == "split") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* sep = lastValue;
         
         std::string fnName = "ts_string_split";
         if (node->arguments[0]->inferredType && node->arguments[0]->inferredType->kind == TypeKind::Class) {
             auto classType = std::static_pointer_cast<ClassType>(node->arguments[0]->inferredType);
             if (classType->name == "RegExp") {
                 fnName = "ts_string_split_regexp";
             }
         }

         if (sep->getType()->isIntegerTy(64)) {
             sep = builder->CreateIntToPtr(sep, llvm::PointerType::getUnqual(*context));
         }

         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName,
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, sep });
         return true;
    } else if (prop->name == "trim") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_trim",
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj });
         return true;
    } else if (prop->name == "startsWith") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* prefix = lastValue;
         
         if (prefix->getType()->isIntegerTy(64)) {
             prefix = builder->CreateIntToPtr(prefix, llvm::PointerType::getUnqual(*context));
         }

         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_startsWith",
             llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, prefix });
         return true;
    } else if (prop->name == "includes") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* search = lastValue;
         
         if (search->getType()->isIntegerTy(64)) {
             search = builder->CreateIntToPtr(search, llvm::PointerType::getUnqual(*context));
         }

         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_includes",
             llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, search });
         return true;
    } else if (prop->name == "indexOf") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* search = lastValue;
         
         if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
             if (search->getType()->isPointerTy()) {
                 // If it's a pointer (string), we need to cast it to i64 for the generic array storage
                 search = builder->CreatePtrToInt(search, llvm::Type::getInt64Ty(*context));
             } else if (search->getType()->isDoubleTy()) {
                 search = builder->CreateFPToSI(search, llvm::Type::getInt64Ty(*context));
             }
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_indexOf",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, search });
         } else {
             if (search->getType()->isIntegerTy(64)) {
                 search = builder->CreateIntToPtr(search, llvm::PointerType::getUnqual(*context));
             }
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_indexOf",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, search });
         }
         return true;
    } else if (prop->name == "slice") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
         llvm::Value* end = nullptr;
         
         if (node->arguments.size() >= 1) {
             visit(node->arguments[0].get());
             start = lastValue;
             if (start->getType()->isDoubleTy()) {
                 start = builder->CreateFPToSI(start, llvm::Type::getInt64Ty(*context));
             }
         }
         if (node->arguments.size() >= 2) {
             visit(node->arguments[1].get());
             end = lastValue;
             if (end->getType()->isDoubleTy()) {
                 end = builder->CreateFPToSI(end, llvm::Type::getInt64Ty(*context));
             }
         } else {
             // Default end to length
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             end = builder->CreateCall(lenFn, { obj });
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_slice",
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, start, end });
         return true;
    } else if (prop->name == "join") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         llvm::Value* sep = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
         if (!node->arguments.empty()) {
             visit(node->arguments[0].get());
             sep = lastValue;
             if (sep->getType()->isIntegerTy(64)) {
                 sep = builder->CreateIntToPtr(sep, llvm::PointerType::getUnqual(*context));
             }
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_join",
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, sep });
         return true;
    } else if (prop->name == "toLowerCase") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_toLowerCase",
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj });
         return true;
    } else if (prop->name == "toUpperCase") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_toUpperCase",
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj });
         return true;
    } else if (prop->name == "replace" || prop->name == "replaceAll") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.size() < 2) return true;
         visit(node->arguments[0].get());
         llvm::Value* pattern = lastValue;
         visit(node->arguments[1].get());
         llvm::Value* replacement = lastValue;
         
         std::string fnName = (prop->name == "replace") ? "ts_string_replace" : "ts_string_replaceAll";
         if (node->arguments[0]->inferredType && node->arguments[0]->inferredType->kind == TypeKind::Class) {
             auto classType = std::static_pointer_cast<ClassType>(node->arguments[0]->inferredType);
             if (classType->name == "RegExp") {
                 fnName = (prop->name == "replace") ? "ts_string_replace_regexp" : "ts_string_replaceAll_regexp";
             }
         }

         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName,
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, pattern, replacement });
         return true;
    } else if (prop->name == "match") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* arg = lastValue;
         
         std::string fnName = "ts_string_match"; // Fallback
         if (node->arguments[0]->inferredType && node->arguments[0]->inferredType->kind == TypeKind::Class) {
             auto classType = std::static_pointer_cast<ClassType>(node->arguments[0]->inferredType);
             if (classType->name == "RegExp") {
                 fnName = "ts_string_match_regexp";
             }
         }

         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName,
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, arg });
         return true;
    } else if (prop->name == "search") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* arg = lastValue;
         
         std::string fnName = "ts_string_indexOf"; // Fallback for string
         if (node->arguments[0]->inferredType && node->arguments[0]->inferredType->kind == TypeKind::Class) {
             auto classType = std::static_pointer_cast<ClassType>(node->arguments[0]->inferredType);
             if (classType->name == "RegExp") {
                 fnName = "ts_string_search_regexp";
             }
         }

         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName,
             llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, arg });
         return true;
    } else if (prop->name == "repeat") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* count = lastValue;
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_repeat",
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, count });
         return true;
    } else if (prop->name == "padStart" || prop->name == "padEnd") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* targetLen = lastValue;
         
         llvm::Value* padStr = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
         if (node->arguments.size() > 1) {
             visit(node->arguments[1].get());
             padStr = lastValue;
         }
         
         std::string fnName = (prop->name == "padStart") ? "ts_string_padStart" : "ts_string_padEnd";
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName,
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, targetLen, padStr });
         return true;
    } else if (prop->name == "substring") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         
         visit(node->arguments[0].get());
         llvm::Value* start = lastValue;
         if (start->getType()->isDoubleTy()) {
             start = builder->CreateFPToSI(start, llvm::Type::getInt64Ty(*context));
         }

         llvm::Value* end = nullptr;
         if (node->arguments.size() > 1) {
             visit(node->arguments[1].get());
             end = lastValue;
             if (end->getType()->isDoubleTy()) {
                 end = builder->CreateFPToSI(end, llvm::Type::getInt64Ty(*context));
             }
         } else {
             // Default end to length
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             end = builder->CreateCall(lenFn, { obj });
         }

         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_substring",
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, start, end });
         return true;
    } else if (prop->name == "sort") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_sort",
             llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { llvm::PointerType::getUnqual(*context) }, false));
         builder->CreateCall(fn, { obj });
         lastValue = nullptr;
         return true;
    } else if (prop->name == "forEach" || prop->name == "map" || prop->name == "filter" || prop->name == "some" || prop->name == "every" || prop->name == "find" || prop->name == "findIndex" || prop->name == "flatMap") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::Value* thisArg = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
         if (node->arguments.size() > 1) {
             visit(node->arguments[1].get());
             thisArg = boxValue(lastValue, node->arguments[1]->inferredType);
         }
         
         std::string fnName = "ts_array_" + prop->name;
         llvm::Type* retTy = llvm::PointerType::getUnqual(*context);
         if (prop->name == "forEach") retTy = llvm::Type::getVoidTy(*context);
         else if (prop->name == "some" || prop->name == "every") retTy = llvm::Type::getInt1Ty(*context);
         else if (prop->name == "findIndex") retTy = llvm::Type::getInt64Ty(*context);

         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName,
             llvm::FunctionType::get(retTy,
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, callback, thisArg });
         
         if (prop->name == "find") {
             std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
             if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
                 elemType = std::static_pointer_cast<ArrayType>(prop->expression->inferredType)->elementType;
             }
             lastValue = unboxValue(lastValue, elemType);
         }
         return true;
    } else if (prop->name == "flat") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         llvm::Value* depth = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1);
         if (!node->arguments.empty()) {
             visit(node->arguments[0].get());
             depth = lastValue;
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_flat",
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, depth });
         return true;
    } else if (prop->name == "reduce") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::Value* initialValue = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
         if (node->arguments.size() > 1) {
             visit(node->arguments[1].get());
             initialValue = boxValue(lastValue, node->arguments[1]->inferredType);
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_reduce",
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { obj, callback, initialValue });
         return true;
    } else if (prop->name == "set") {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.size() < 2) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = lastValue;
         if (key->getType()->isIntegerTy(64)) {
             key = builder->CreateIntToPtr(key, llvm::PointerType::getUnqual(*context));
         }
         visit(node->arguments[1].get());
         llvm::Value* value = lastValue;
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_set",
             llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
         builder->CreateCall(fn, { map, key, value });
         lastValue = nullptr;
         return true;
    } else if (prop->name == "get") {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = lastValue;
         if (key->getType()->isIntegerTy(64)) {
             key = builder->CreateIntToPtr(key, llvm::PointerType::getUnqual(*context));
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_get",
             llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { map, key });
         return true;
    } else if (prop->name == "has") {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = lastValue;
         if (key->getType()->isIntegerTy(64)) {
             key = builder->CreateIntToPtr(key, llvm::PointerType::getUnqual(*context));
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_has",
             llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         llvm::Value* res = builder->CreateCall(fn, { map, key });
         lastValue = builder->CreateICmpNE(res, llvm::ConstantInt::get(res->getType(), 0), "tobool");
         return true;
    } else if (prop->name == "delete") {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_delete",
             llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { map, key });
         return true;
    } else if (prop->name == "clear") {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, llvm::PointerType::getUnqual(*context));
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_clear",
             llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { llvm::PointerType::getUnqual(*context) }, false));
         builder->CreateCall(fn, { map });
         lastValue = nullptr;
         return true;
    } else if (prop->name == "values" || prop->name == "entries") {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, llvm::PointerType::getUnqual(*context));
         }
         
         std::string fnName = "ts_map_" + prop->name;
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName,
             llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                 { llvm::PointerType::getUnqual(*context) }, false));
         lastValue = builder->CreateCall(fn, { map });
         return true;
    } else if (prop->name == "forEach") {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, llvm::PointerType::getUnqual(*context));
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::Value* thisArg = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
         if (node->arguments.size() > 1) {
             visit(node->arguments[1].get());
             thisArg = boxValue(lastValue, node->arguments[1]->inferredType);
         }
         
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_forEach",
             llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
         builder->CreateCall(fn, { map, callback, thisArg });
         lastValue = nullptr;
         return true;
    } else if (prop->name == "md5") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "crypto") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    arg = builder->CreateIntToPtr(arg, llvm::PointerType::getUnqual(*context));
                }
                
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_crypto_md5",
                    llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                        { llvm::PointerType::getUnqual(*context) }, false));
                lastValue = builder->CreateCall(fn, { arg });
                return true;
            }
        }
    } else if (prop->name == "test" || prop->name == "exec") {
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            if (cls->name == "RegExp") {
                visit(prop->expression.get());
                llvm::Value* re = lastValue;
                
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* str = lastValue;
                if (str->getType()->isIntegerTy(64)) {
                    str = builder->CreateIntToPtr(str, builder->getPtrTy());
                }
                
                if (prop->name == "test") {
                    llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_test",
                        llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    llvm::Value* res = builder->CreateCall(fn, { re, str });
                    lastValue = builder->CreateICmpNE(res, llvm::ConstantInt::get(res->getType(), 0));
                } else {
                    llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_exec",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    lastValue = builder->CreateCall(fn, { re, str });
                }
                return true;
            }
        }
    } else if (prop->name == "parse" || prop->name == "stringify") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "JSON") {
                if (node->arguments.empty()) return true;
                
                if (prop->name == "parse") {
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    if (arg->getType()->isIntegerTy(64)) {
                        arg = builder->CreateIntToPtr(arg, llvm::PointerType::getUnqual(*context));
                    }
                    
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_json_parse",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return true;
                } else {
                    // stringify
                    visit(node->arguments[0].get());
                    llvm::Value* objArg = boxValue(lastValue, node->arguments[0]->inferredType);
                    
                    llvm::Value* replacer = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    if (node->arguments.size() > 1) {
                        visit(node->arguments[1].get());
                        replacer = boxValue(lastValue, node->arguments[1]->inferredType);
                    }
                    
                    llvm::Value* space = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    if (node->arguments.size() > 2) {
                        visit(node->arguments[2].get());
                        space = boxValue(lastValue, node->arguments[2]->inferredType);
                    }
                    
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_json_stringify",
                        llvm::FunctionType::get(builder->getPtrTy(), 
                            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false));
                    lastValue = builder->CreateCall(fn, { objArg, replacer, space });
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace ts
