#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;

bool IRGenerator::tryGenerateBuiltinCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    if (prop->name == "log") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "console") {
                if (node->arguments.empty()) return true;
                
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg) {
                    llvm::errs() << "Error: console.log argument evaluated to null\n";
                    return true;
                }

                llvm::Type* argType = arg->getType();

                if (argType->isVoidTy()) {
                    llvm::errs() << "Error: Argument to console.log is void\n";
                    return true;
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
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(
                    llvm::Type::getVoidTy(*context), { paramType }, false);
                llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

                builder->CreateCall(logFn, { arg });
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
            }
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
         
         if (sep->getType()->isIntegerTy(64)) {
             sep = builder->CreateIntToPtr(sep, llvm::PointerType::getUnqual(*context));
         }

         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_split",
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
    }

    return false;
}

} // namespace ts
