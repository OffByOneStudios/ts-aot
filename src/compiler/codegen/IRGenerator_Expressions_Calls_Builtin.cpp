#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

bool IRGenerator::tryGenerateBuiltinCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    SPDLOG_DEBUG("tryGenerateBuiltinCall: {}", prop->name);

    if (tryGenerateFSCall(node, prop)) return true;
    if (tryGeneratePathCall(node, prop)) return true;
    if (tryGenerateEventsCall(node, prop)) return true;
    if (tryGenerateStreamCall(node, prop)) return true;
    if (tryGenerateBufferCall(node, prop)) return true;

    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "Symbol") {
            if (prop->name == "for") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* key = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_symbol_for", ft);
                lastValue = createCall(ft, fn.getCallee(), { key });
                return true;
            } else if (prop->name == "keyFor") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* sym = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_symbol_key_for", ft);
                lastValue = createCall(ft, fn.getCallee(), { sym });
                return true;
            }
        }
    }

    if (prop->expression->inferredType && (prop->expression->inferredType->kind == TypeKind::Int || prop->expression->inferredType->kind == TypeKind::Double)) {
        if (prop->name == "toString" || prop->name == "toFixed") {
            visit(prop->expression.get());
            llvm::Value* val = lastValue;
            
            std::vector<llvm::Value*> args;
            std::vector<llvm::Type*> paramTypes;
            
            if (prop->expression->inferredType->kind == TypeKind::Int) {
                args.push_back(val);
                paramTypes.push_back(llvm::Type::getInt64Ty(*context));
            } else {
                args.push_back(val);
                paramTypes.push_back(llvm::Type::getDoubleTy(*context));
            }
            
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                llvm::Value* arg0 = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                args.push_back(arg0);
                paramTypes.push_back(llvm::Type::getInt64Ty(*context));
            } else {
                if (prop->name == "toString") {
                    args.push_back(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 10));
                    paramTypes.push_back(llvm::Type::getInt64Ty(*context));
                } else {
                    args.push_back(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0));
                    paramTypes.push_back(llvm::Type::getInt64Ty(*context));
                }
            }
            
            std::string fnName = (prop->expression->inferredType->kind == TypeKind::Int) ? "ts_int_to_string" : "ts_double_to_string";
            if (prop->name == "toFixed") {
                fnName = "ts_double_to_fixed"; // Both int and double can use this if we cast int to double
                if (prop->expression->inferredType->kind == TypeKind::Int) {
                    args[0] = builder->CreateSIToFP(args[0], llvm::Type::getDoubleTy(*context));
                    paramTypes[0] = llvm::Type::getDoubleTy(*context);
                }
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), paramTypes, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
            lastValue = createCall(ft, fn.getCallee(), args);
            return true;
        }
    }

    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
        if (classType->name == "DataView") {
            visit(prop->expression.get());
            llvm::Value* dv = lastValue;
            
            if (prop->name == "getUint32") {
                visit(node->arguments[0].get());
                llvm::Value* offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                
                llvm::Value* littleEndian = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
                if (node->arguments.size() > 1) {
                    visit(node->arguments[1].get());
                    littleEndian = castValue(lastValue, llvm::Type::getInt1Ty(*context));
                }
                
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt1Ty(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("DataView_getUint32", ft);
                
                llvm::Value* contextVal = currentAsyncContext;
                if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

                lastValue = createCall(ft, fn.getCallee(), { contextVal, dv, offset, littleEndian });
                if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                    lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                }
                return true;
            } else if (prop->name == "setUint32") {
                visit(node->arguments[0].get());
                llvm::Value* offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                
                visit(node->arguments[1].get());
                llvm::Value* val = castValue(lastValue, llvm::Type::getInt32Ty(*context));
                // The runtime expects i64 for the value parameter to be generic, but we cast to i32 first for JS semantics
                llvm::Value* val64 = castValue(val, llvm::Type::getInt64Ty(*context));
                
                llvm::Value* littleEndian = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
                if (node->arguments.size() > 2) {
                    visit(node->arguments[2].get());
                    littleEndian = castValue(lastValue, llvm::Type::getInt1Ty(*context));
                }
                
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt1Ty(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("DataView_setUint32", ft);

                llvm::Value* contextVal = currentAsyncContext;
                if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

                createCall(ft, fn.getCallee(), { contextVal, dv, offset, val64, littleEndian });
                lastValue = nullptr;
                return true;
            }
        }
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
            
            lastValue = createCall(ft, fn.getCallee(), args);
            return true;
        } else if (classType->name == "Server") {
            if (prop->name == "listen") {
                visit(prop->expression.get());
                llvm::Value* server = lastValue;
                
                visit(node->arguments[0].get());
                llvm::Value* port = lastValue;
                if (port->getType()->isPointerTy()) {
                    llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int", unboxFt);
                    port = createCall(unboxFt, unboxFn.getCallee(), { port });
                }
                if (port->getType()->isDoubleTy()) {
                    port = builder->CreateFPToSI(port, llvm::Type::getInt32Ty(*context));
                } else {
                    port = builder->CreateIntCast(port, llvm::Type::getInt32Ty(*context), true);
                }

                llvm::Value* vtable = llvm::ConstantPointerNull::get(builder->getPtrTy());
                llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (node->arguments.size() > 1) {
                    visit(node->arguments[1].get());
                    callback = lastValue;
                }

                llvm::Value* contextVal = currentAsyncContext;
                if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt32Ty(*context), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http_server_listen", ft);
                
                createCall(ft, fn.getCallee(), { contextVal, server, port, vtable, callback });
                lastValue = nullptr;
                return true;
            }
        } else if (classType->name == "ServerResponse") {
            if (prop->name == "writeHead") {
                visit(prop->expression.get());
                llvm::Value* res = lastValue;
                
                visit(node->arguments[0].get());
                llvm::Value* status = lastValue;
                if (status->getType()->isPointerTy()) {
                    llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int", unboxFt);
                    status = createCall(unboxFt, unboxFn.getCallee(), { status });
                }
                if (status->getType()->isDoubleTy()) {
                    status = builder->CreateFPToSI(status, llvm::Type::getInt32Ty(*context));
                } else {
                    status = builder->CreateIntCast(status, llvm::Type::getInt32Ty(*context), true);
                }

                llvm::Value* headers = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (node->arguments.size() > 1) {
                    visit(node->arguments[1].get());
                    headers = lastValue;
                }

                llvm::Value* contextVal = currentAsyncContext;
                if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt32Ty(*context), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_server_response_write_head", ft);
                
                createCall(ft, fn.getCallee(), { contextVal, res, status, headers });
                lastValue = nullptr;
                return true;
            } else if (prop->name == "write" || prop->name == "end") {
                visit(prop->expression.get());
                llvm::Value* res = lastValue;
                
                llvm::Value* data = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    data = boxValue(lastValue, node->arguments[0]->inferredType);
                }

                llvm::Value* contextVal = currentAsyncContext;
                if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

                std::string funcName = (prop->name == "write") ? "ts_server_response_write" : "ts_server_response_end";
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
                
                createCall(ft, fn.getCallee(), { contextVal, res, data });
                lastValue = nullptr;
                return true;
            }
        }
    }

    if (prop->name == "log") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "console") {
                if (node->arguments.empty()) return true;
                
                for (size_t i = 0; i < node->arguments.size(); ++i) {
                    visit(node->arguments[i].get());
                    llvm::Value* arg = lastValue;
                    if (!arg) continue;

                    llvm::Type* argType = arg->getType();
                    
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
                            arg = boxValue(arg, node->arguments[i]->inferredType);
                        }
                        paramType = llvm::PointerType::getUnqual(*context);
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        llvm::Type::getVoidTy(*context), { paramType }, false);
                    llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

                    createCall(ft, logFn.getCallee(), { arg });
                }
                lastValue = nullptr;
                return true;
            }
        }
    } else if (prop->name == "createServer") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "http") {
                visit(node->arguments[0].get());
                llvm::Value* callback = lastValue;
                llvm::Value* vtable = llvm::ConstantPointerNull::get(builder->getPtrTy());
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http_create_server", ft);
                
                llvm::Value* contextVal = currentAsyncContext ? currentAsyncContext : llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(ft, fn.getCallee(), { contextVal, vtable, callback });
                return true;
            }
        }
    }

    if (tryGenerateFSCall(node, prop)) return true;
    if (tryGeneratePathCall(node, prop)) return true;

    if (false) {
        // Handle both fs.readFileSync and const fs = require('fs'); fs.readFileSync
        bool isFs = false;
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "fs") isFs = true;
        }
        
        // Also allow if the expression is 'any' (e.g. from require)
        if (!isFs && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
            isFs = true; 
        }

        if (isFs) {
            if (prop->name == "readFileSync") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                
                llvm::FunctionType* readFt = llvm::FunctionType::get(builder->getPtrTy(), 
                        { builder->getPtrTy() }, false);
                llvm::FunctionCallee readFn = module->getOrInsertFunction("ts_fs_readFileSync", readFt);
                
                lastValue = createCall(readFt, readFn.getCallee(), { arg });
                return true;
            } else if (prop->name == "writeFileSync") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                visit(node->arguments[1].get());
                llvm::Value* data = boxValue(lastValue, node->arguments[1]->inferredType);
                
                llvm::FunctionType* writeFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                        { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee writeFn = module->getOrInsertFunction("ts_fs_writeFileSync", writeFt);
                
                createCall(writeFt, writeFn.getCallee(), { path, data });
                lastValue = nullptr;
                return true;
            } else if (prop->name == "existsSync") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                
                llvm::FunctionType* existsFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), 
                        { builder->getPtrTy() }, false);
                llvm::FunctionCallee existsFn = module->getOrInsertFunction("ts_fs_existsSync", existsFt);
                
                lastValue = createCall(existsFt, existsFn.getCallee(), { path });
                return true;
            } else if (prop->name == "unlinkSync" || prop->name == "mkdirSync" || prop->name == "rmdirSync") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                
                std::string runtimeName = "ts_fs_" + prop->name;
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                        { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
                
                createCall(ft, fn.getCallee(), { path });
                lastValue = nullptr;
                return true;
            } else if (prop->name == "readdirSync") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                
                llvm::Value* options = nullptr;
                if (node->arguments.size() > 1) {
                    visit(node->arguments[1].get());
                    options = boxValue(lastValue, node->arguments[1]->inferredType);
                } else {
                    options = llvm::ConstantPointerNull::get(builder->getPtrTy());
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                        { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_readdirSync", ft);
                
                lastValue = unboxValue(createCall(ft, fn.getCallee(), { path, options }), node->inferredType);
                return true;
            } else if (prop->name == "statSync") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                        { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_statSync", ft);
                
                lastValue = unboxValue(createCall(ft, fn.getCallee(), { path }), node->inferredType);
                return true;
            } else if (prop->name == "openSync") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                visit(node->arguments[1].get());
                llvm::Value* flags = lastValue;
                llvm::Value* mode = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
                
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), 
                        { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_openSync", ft);
                
                llvm::Value* fd = createCall(ft, fn.getCallee(), { path, flags, mode });
                
                llvm::FunctionType* makeDoubleFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee makeDoubleFn = module->getOrInsertFunction("ts_value_make_double", makeDoubleFt);
                lastValue = createCall(makeDoubleFt, makeDoubleFn.getCallee(), { fd });
                return true;
            } else if (prop->name == "closeSync") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                        { llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_closeSync", ft);
                
                createCall(ft, fn.getCallee(), { fd });
                lastValue = nullptr;
                return true;
            } else if (prop->name == "readSync" || prop->name == "writeSync") {
                if (node->arguments.size() < 5) return true;
                visit(node->arguments[0].get());
                llvm::Value* fd = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                visit(node->arguments[1].get());
                llvm::Value* buffer = lastValue;
                visit(node->arguments[2].get());
                llvm::Value* offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                visit(node->arguments[3].get());
                llvm::Value* length = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                visit(node->arguments[4].get());
                llvm::Value* position = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                
                std::string runtimeName = "ts_fs_" + prop->name;
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), 
                        { llvm::Type::getInt64Ty(*context), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
                
                llvm::Value* result = createCall(ft, fn.getCallee(), { fd, buffer, offset, length, position });
                
                llvm::FunctionType* makeIntFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee makeIntFn = module->getOrInsertFunction("ts_value_make_int", makeIntFt);
                lastValue = createCall(makeIntFt, makeIntFn.getCallee(), { result });
                return true;
            } else if (prop->name == "createReadStream") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_createReadStream", ft);
                lastValue = createCall(ft, fn.getCallee(), { path });
                return true;
            } else if (prop->name == "createWriteStream") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_createWriteStream", ft);
                lastValue = createCall(ft, fn.getCallee(), { path });
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
                    
                    llvm::FunctionType* readFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee readFn = module->getOrInsertFunction("ts_fs_readFile_async", readFt);
                    
                    lastValue = createCall(readFt, readFn.getCallee(), { arg });
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
                
                llvm::FunctionType* writeFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                        { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false);
                llvm::FunctionCallee writeFn = module->getOrInsertFunction("ts_fs_writeFileSync", writeFt);
                
                createCall(writeFt, writeFn.getCallee(), { path, content });
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
                    
                    llvm::FunctionType* writeFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee writeFn = module->getOrInsertFunction("ts_fs_writeFile_async", writeFt);
                    
                    lastValue = createCall(writeFt, writeFn.getCallee(), { path, content });
                    return true;
                }
            }
        }
    } else if (prop->name == "mkdir") {
        if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
            if (auto obj = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                if (obj->name == "fs" && innerProp->name == "promises") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* path = lastValue;
                    
                    llvm::FunctionType* mkdirFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee mkdirFn = module->getOrInsertFunction("ts_fs_mkdir_async", mkdirFt);
                    
                    lastValue = createCall(mkdirFt, mkdirFn.getCallee(), { path });
                    return true;
                }
            }
        }
    } else if (prop->name == "stat") {
        if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
            if (auto obj = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                if (obj->name == "fs" && innerProp->name == "promises") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* path = lastValue;
                    
                    llvm::FunctionType* statFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee statFn = module->getOrInsertFunction("ts_fs_stat_async", statFt);
                    
                    lastValue = createCall(statFt, statFn.getCallee(), { path });
                    return true;
                }
            }
        }
    } else if (prop->name == "open") {
        if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
            if (auto obj = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                if (obj->name == "fs" && innerProp->name == "promises") {
                    if (node->arguments.size() < 2) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* path = lastValue;
                    visit(node->arguments[1].get());
                    llvm::Value* flags = lastValue;
                    llvm::Value* mode = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
                    
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_open_async", ft);
                    
                    lastValue = createCall(ft, fn.getCallee(), { path, flags, mode });
                    return true;
                }
            }
        }
    } else if (prop->name == "close") {
        if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
            if (auto obj = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                if (obj->name == "fs" && innerProp->name == "promises") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                    
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getDoubleTy(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_close_async", ft);
                    
                    lastValue = createCall(ft, fn.getCallee(), { fd });
                    return true;
                }
            }
        }
    } else if (prop->name == "read" || prop->name == "write") {
        if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
            if (auto obj = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                if (obj->name == "fs" && innerProp->name == "promises") {
                    if (node->arguments.size() < 5) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* fd = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                    visit(node->arguments[1].get());
                    llvm::Value* buffer = lastValue;
                    visit(node->arguments[2].get());
                    llvm::Value* offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                    visit(node->arguments[3].get());
                    llvm::Value* length = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                    visit(node->arguments[4].get());
                    llvm::Value* position = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                    
                    std::string runtimeName = "ts_fs_" + prop->name + "_async";
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                            { llvm::Type::getInt64Ty(*context), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
                    
                    lastValue = createCall(ft, fn.getCallee(), { fd, buffer, offset, length, position });
                    return true;
                }
            }
        }
    } else if (prop->name == "readdir") {
        if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
            if (auto obj = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                if (obj->name == "fs" && innerProp->name == "promises") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* path = lastValue;
                    
                    llvm::FunctionType* readdirFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee readdirFn = module->getOrInsertFunction("ts_fs_readdir_async", readdirFt);
                    
                    lastValue = createCall(readdirFt, readdirFn.getCallee(), { path });
                    return true;
                }
            }
        }
    } else if (prop->name == "exit") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "process") {
                llvm::Value* code = nullptr;
                if (node->arguments.empty()) {
                    code = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
                } else {
                    visit(node->arguments[0].get());
                    code = lastValue;
                }
                
                llvm::FunctionType* exitFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee exitFn = module->getOrInsertFunction("ts_process_exit", exitFt);
                
                createCall(exitFt, exitFn.getCallee(), { code });
                lastValue = nullptr;
                return true;
            }
        }
    } else if (prop->name == "cwd") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "process") {
                llvm::FunctionType* cwdFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee cwdFn = module->getOrInsertFunction("ts_process_cwd", cwdFt);
                
                lastValue = unboxValue(createCall(cwdFt, cwdFn.getCallee(), {}), node->inferredType);
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
                llvm::FunctionType* mathFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, mathFt);
                lastValue = createCall(mathFt, fn.getCallee(), { arg1, arg2 });
                if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                    lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                }
                return true;
            } else if (prop->name == "abs") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                
                llvm::FunctionType* absFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_abs", absFt);
                lastValue = createCall(absFt, fn.getCallee(), { arg });
                if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                    lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                }
                return true;
            } else if (prop->name == "floor") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                
                if (arg->getType()->isIntegerTy(64)) {
                    // floor(int) is just int
                    lastValue = arg;
                    if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                        lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                    }
                    return true;
                }
                
                llvm::FunctionType* floorFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_floor", floorFt);
                lastValue = createCall(floorFt, fn.getCallee(), { arg });
                if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                    lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                }
                return true;
            } else if (prop->name == "ceil") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    lastValue = arg;
                    if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                        lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                    }
                    return true;
                }
                llvm::FunctionType* ceilFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_ceil", ceilFt);
                lastValue = createCall(ceilFt, fn.getCallee(), { arg });
                if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                    lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                }
                return true;
            } else if (prop->name == "round") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    lastValue = arg;
                    if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                        lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                    }
                    return true;
                }
                llvm::FunctionType* roundFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                        { llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_round", roundFt);
                lastValue = createCall(roundFt, fn.getCallee(), { arg });
                if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                    lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                }
                return true;
            } else if (prop->name == "sqrt") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    arg = builder->CreateSIToFP(arg, llvm::Type::getDoubleTy(*context));
                }
                llvm::FunctionType* sqrtFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                        { llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_sqrt", sqrtFt);
                lastValue = createCall(sqrtFt, fn.getCallee(), { arg });
                return true;
            } else if (prop->name == "pow") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg1 = lastValue;
                visit(node->arguments[1].get());
                llvm::Value* arg2 = lastValue;
                if (arg1->getType()->isIntegerTy(64)) arg1 = builder->CreateSIToFP(arg1, llvm::Type::getDoubleTy(*context));
                if (arg2->getType()->isIntegerTy(64)) arg2 = builder->CreateSIToFP(arg2, llvm::Type::getDoubleTy(*context));
                llvm::FunctionType* powFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                        { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_pow", powFt);
                lastValue = createCall(powFt, fn.getCallee(), { arg1, arg2 });
                return true;
            } else if (prop->name == "random") {
                llvm::FunctionType* randomFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), {}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_random", randomFt);
                lastValue = createCall(randomFt, fn.getCallee(), {});
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
                llvm::FunctionType* mathFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                        { llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, mathFt);
                lastValue = createCall(mathFt, fn.getCallee(), { arg });
                return true;
            } else if (prop->name == "hypot") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg1 = lastValue;
                visit(node->arguments[1].get());
                llvm::Value* arg2 = lastValue;
                if (arg1->getType()->isIntegerTy(64)) arg1 = builder->CreateSIToFP(arg1, llvm::Type::getDoubleTy(*context));
                if (arg2->getType()->isIntegerTy(64)) arg2 = builder->CreateSIToFP(arg2, llvm::Type::getDoubleTy(*context));
                llvm::FunctionType* hypotFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                        { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_hypot", hypotFt);
                lastValue = createCall(hypotFt, fn.getCallee(), { arg1, arg2 });
                return true;
            } else if (prop->name == "fround") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy(64)) {
                    arg = builder->CreateSIToFP(arg, llvm::Type::getDoubleTy(*context));
                }
                llvm::FunctionType* froundFt = llvm::FunctionType::get(llvm::Type::getFloatTy(*context),
                        { llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_fround", froundFt);
                lastValue = createCall(froundFt, fn.getCallee(), { arg });
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
                llvm::FunctionType* clz32Ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context),
                        { llvm::Type::getInt32Ty(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_clz32", clz32Ft);
                lastValue = createCall(clz32Ft, fn.getCallee(), { arg });
                lastValue = builder->CreateSExt(lastValue, llvm::Type::getInt64Ty(*context));
                if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                    lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                }
                return true;
            }
        } else if (obj->name == "Date") {
            if (prop->name == "now") {
                llvm::FunctionType* nowFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("Date_static_now", nowFt);
                lastValue = createCall(nowFt, fn.getCallee(), {});
                return true;
            }
        } else if (obj->name == "Promise") {
            if (prop->name == "resolve") {
                llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve", resolveFt);
                
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
                lastValue = createCall(resolveFt, resolveFn.getCallee(), { boxedVal });
                return true;
            } else if (prop->name == "reject") {
                llvm::FunctionType* rejectFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee rejectFn = module->getOrInsertFunction("ts_promise_reject", rejectFt);
                
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
                lastValue = createCall(rejectFt, rejectFn.getCallee(), { boxedVal });
                return true;
            } else if (prop->name == "all") {
                if (node->arguments.empty()) return true;
                llvm::FunctionType* allFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee allFn = module->getOrInsertFunction("ts_promise_all", allFt);
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = createCall(allFt, allFn.getCallee(), { iterable });
                return true;
            } else if (prop->name == "race") {
                if (node->arguments.empty()) return true;
                llvm::FunctionType* raceFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee raceFn = module->getOrInsertFunction("ts_promise_race", raceFt);
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = createCall(raceFt, raceFn.getCallee(), { iterable });
                return true;
            } else if (prop->name == "allSettled") {
                if (node->arguments.empty()) return true;
                llvm::FunctionType* allSettledFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee allSettledFn = module->getOrInsertFunction("ts_promise_allSettled", allSettledFt);
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = createCall(allSettledFt, allSettledFn.getCallee(), { iterable });
                return true;
            } else if (prop->name == "any") {
                if (node->arguments.empty()) return true;
                llvm::FunctionType* anyFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee anyFn = module->getOrInsertFunction("ts_promise_any", anyFt);
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = createCall(anyFt, anyFn.getCallee(), { iterable });
                return true;
            }
        }
    }

    if (prop->name == "charCodeAt") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* index = lastValue;
         
         if (index->getType()->isDoubleTy()) {
             index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
         }

         llvm::FunctionType* charCodeAtFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_charCodeAt", charCodeAtFt);
         
         lastValue = createCall(charCodeAtFt, fn.getCallee(), { obj, index });
         return true;
    } else if (prop->name == "split") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
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
             sep = builder->CreateIntToPtr(sep, builder->getPtrTy());
         }

         llvm::FunctionType* splitFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, splitFt);
         lastValue = createCall(splitFt, fn.getCallee(), { obj, sep });
         return true;
    } else if (prop->name == "trim") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         llvm::FunctionType* trimFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_trim", trimFt);
         lastValue = createCall(trimFt, fn.getCallee(), { obj });
         return true;
    } else if (prop->name == "startsWith") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* prefix = lastValue;
         
         if (prefix->getType()->isIntegerTy(64)) {
             prefix = builder->CreateIntToPtr(prefix, builder->getPtrTy());
         }

         llvm::FunctionType* startsWithFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_startsWith", startsWithFt);
         lastValue = createCall(startsWithFt, fn.getCallee(), { obj, prefix });
         return true;
    } else if (prop->name == "includes") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* search = lastValue;
         
         if (search->getType()->isIntegerTy(64)) {
             search = builder->CreateIntToPtr(search, builder->getPtrTy());
         }

         llvm::FunctionType* includesFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_includes", includesFt);
         lastValue = createCall(includesFt, fn.getCallee(), { obj, search });
         return true;
    } else if (prop->name == "indexOf") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
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
             llvm::FunctionType* indexOfFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_indexOf", indexOfFt);
             lastValue = createCall(indexOfFt, fn.getCallee(), { obj, search });
         } else {
             if (search->getType()->isIntegerTy(64)) {
                 search = builder->CreateIntToPtr(search, builder->getPtrTy());
             }
             llvm::FunctionType* indexOfFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { builder->getPtrTy(), builder->getPtrTy() }, false);
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_indexOf", indexOfFt);
             lastValue = createCall(indexOfFt, fn.getCallee(), { obj, search });
         }
         return true;
    } else if (prop->name == "slice" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
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
             llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { builder->getPtrTy() }, false);
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length", lenFt);
             end = createCall(lenFt, lenFn.getCallee(), { obj });
         }
         
         llvm::FunctionType* sliceFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_slice", sliceFt);
         lastValue = createCall(sliceFt, fn.getCallee(), { obj, start, end });
         return true;
    } else if (prop->name == "join" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         llvm::Value* sep = llvm::ConstantPointerNull::get(builder->getPtrTy());
         if (!node->arguments.empty()) {
             visit(node->arguments[0].get());
             sep = lastValue;
             if (sep->getType()->isIntegerTy(64)) {
                 sep = builder->CreateIntToPtr(sep, builder->getPtrTy());
             }
         }
         
         llvm::FunctionType* joinFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_join", joinFt);
         lastValue = createCall(joinFt, fn.getCallee(), { obj, sep });
         return true;
    } else if (prop->name == "toLowerCase") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         llvm::FunctionType* toLowerFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_toLowerCase", toLowerFt);
         lastValue = createCall(toLowerFt, fn.getCallee(), { obj });
         return true;
    } else if (prop->name == "toUpperCase") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         llvm::FunctionType* toUpperFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_toUpperCase", toUpperFt);
         lastValue = createCall(toUpperFt, fn.getCallee(), { obj });
         return true;
    } else if (prop->name == "replace" || prop->name == "replaceAll") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
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

         llvm::FunctionType* replaceFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, replaceFt);
         lastValue = createCall(replaceFt, fn.getCallee(), { obj, pattern, replacement });
         return true;
    } else if (prop->name == "match") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
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

         llvm::FunctionType* matchFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, matchFt);
         lastValue = createCall(matchFt, fn.getCallee(), { obj, arg });
         return true;
    } else if (prop->name == "search") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
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

         llvm::FunctionType* searchFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, searchFt);
         lastValue = createCall(searchFt, fn.getCallee(), { obj, arg });
         return true;
    } else if (prop->name == "repeat") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* count = lastValue;
         
         llvm::FunctionType* repeatFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_repeat", repeatFt);
         lastValue = createCall(repeatFt, fn.getCallee(), { obj, count });
         return true;
    } else if (prop->name == "padStart" || prop->name == "padEnd") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* targetLen = lastValue;
         
         llvm::Value* padStr = llvm::ConstantPointerNull::get(builder->getPtrTy());
         if (node->arguments.size() > 1) {
             visit(node->arguments[1].get());
             padStr = lastValue;
         }
         
         std::string fnName = (prop->name == "padStart") ? "ts_string_padStart" : "ts_string_padEnd";
         llvm::FunctionType* padFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, padFt);
         lastValue = createCall(padFt, fn.getCallee(), { obj, targetLen, padStr });
         return true;
    } else if (prop->name == "substring") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
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
             llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { builder->getPtrTy() }, false);
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length", lenFt);
             end = createCall(lenFt, lenFn.getCallee(), { obj });
         }

         llvm::FunctionType* substringFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_substring", substringFt);
         lastValue = createCall(substringFt, fn.getCallee(), { obj, start, end });
         return true;
    } else if (prop->name == "sort" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         llvm::FunctionType* sortFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_sort", sortFt);
         createCall(sortFt, fn.getCallee(), { obj });
         lastValue = nullptr;
         return true;
    } else if ((prop->name == "forEach" || prop->name == "map" || prop->name == "filter" || prop->name == "some" || prop->name == "every" || prop->name == "find" || prop->name == "findIndex" || prop->name == "flatMap") && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::Value* thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
         if (node->arguments.size() > 1) {
             visit(node->arguments[1].get());
             thisArg = boxValue(lastValue, node->arguments[1]->inferredType);
         }
         
         std::string fnName = "ts_array_" + prop->name;
         llvm::Type* retTy = builder->getPtrTy();
         if (prop->name == "forEach") retTy = llvm::Type::getVoidTy(*context);
         else if (prop->name == "some" || prop->name == "every") retTy = llvm::Type::getInt1Ty(*context);
         else if (prop->name == "findIndex") retTy = llvm::Type::getInt64Ty(*context);

         llvm::FunctionType* arrayFt = llvm::FunctionType::get(retTy,
                 { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, arrayFt);
         lastValue = createCall(arrayFt, fn.getCallee(), { obj, callback, thisArg });
         
         if (prop->name == "find") {
             std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
             if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
                 elemType = std::static_pointer_cast<ArrayType>(prop->expression->inferredType)->elementType;
             }
             lastValue = unboxValue(lastValue, elemType);
         }
         return true;
    } else if (prop->name == "flat" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         llvm::Value* depth = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1);
         if (!node->arguments.empty()) {
             visit(node->arguments[0].get());
             depth = lastValue;
         }
         
         llvm::FunctionType* flatFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_flat", flatFt);
         lastValue = createCall(flatFt, fn.getCallee(), { obj, depth });
         return true;
    } else if (prop->name == "reduce" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::Value* initialValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
         if (node->arguments.size() > 1) {
             visit(node->arguments[1].get());
             initialValue = boxValue(lastValue, node->arguments[1]->inferredType);
         }
         
         llvm::FunctionType* reduceFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_reduce", reduceFt);
         lastValue = createCall(reduceFt, fn.getCallee(), { obj, callback, initialValue });
         return true;
    } else if (prop->name == "set" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Map) {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, builder->getPtrTy());
         }
         
         if (node->arguments.size() < 2) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
         visit(node->arguments[1].get());
         llvm::Value* value = boxValue(lastValue, node->arguments[1]->inferredType);
         
         llvm::FunctionType* setFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_set", setFt);
         createCall(setFt, fn.getCallee(), { map, key, value });
         lastValue = map;
         return true;
    } else if (prop->name == "get" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Map) {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_get", getFt);
         lastValue = createCall(getFt, fn.getCallee(), { map, key });
         return true;
    } else if (prop->name == "has" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Map) {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* hasFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_has", hasFt);
         lastValue = createCall(hasFt, fn.getCallee(), { map, key });
         return true;
    } else if (prop->name == "delete" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Map) {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* deleteFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_delete", deleteFt);
         lastValue = createCall(deleteFt, fn.getCallee(), { map, key });
         return true;
    } else if (prop->name == "clear" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Map) {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, builder->getPtrTy());
         }
         
         llvm::FunctionType* clearFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_clear", clearFt);
         createCall(clearFt, fn.getCallee(), { map });
         lastValue = nullptr;
         return true;
    } else if (prop->name == "add" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::SetType) {
         visit(prop->expression.get());
         llvm::Value* set = lastValue;
         if (set->getType()->isIntegerTy(64)) {
             set = builder->CreateIntToPtr(set, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* value = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* addFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_set_add", addFt);
         createCall(addFt, fn.getCallee(), { set, value });
         lastValue = set;
         return true;
    } else if (prop->name == "has" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::SetType) {
         visit(prop->expression.get());
         llvm::Value* set = lastValue;
         if (set->getType()->isIntegerTy(64)) {
             set = builder->CreateIntToPtr(set, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* value = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* hasFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_set_has", hasFt);
         lastValue = createCall(hasFt, fn.getCallee(), { set, value });
         return true;
    } else if (prop->name == "delete" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::SetType) {
         visit(prop->expression.get());
         llvm::Value* set = lastValue;
         if (set->getType()->isIntegerTy(64)) {
             set = builder->CreateIntToPtr(set, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* value = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* deleteFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_set_delete", deleteFt);
         lastValue = createCall(deleteFt, fn.getCallee(), { set, value });
         return true;
    } else if (prop->name == "clear" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::SetType) {
         visit(prop->expression.get());
         llvm::Value* set = lastValue;
         if (set->getType()->isIntegerTy(64)) {
             set = builder->CreateIntToPtr(set, builder->getPtrTy());
         }
         
         llvm::FunctionType* clearFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_set_clear", clearFt);
         createCall(clearFt, fn.getCallee(), { set });
         lastValue = nullptr;
         return true;
    } else if ((prop->name == "values" || prop->name == "entries") && prop->expression->inferredType && (prop->expression->inferredType->kind == TypeKind::Map || prop->expression->inferredType->kind == TypeKind::SetType)) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         std::string prefix = (prop->expression->inferredType->kind == TypeKind::Map) ? "ts_map_" : "ts_set_";
         std::string fnName = prefix + prop->name;
         llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
         lastValue = createCall(ft, fn.getCallee(), { obj });
         return true;
    } else if (prop->name == "forEach" && prop->expression->inferredType && (prop->expression->inferredType->kind == TypeKind::Map || prop->expression->inferredType->kind == TypeKind::SetType)) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::Value* thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
         if (node->arguments.size() > 1) {
             visit(node->arguments[1].get());
             thisArg = boxValue(lastValue, node->arguments[1]->inferredType);
         }
         
         std::string fnName = (prop->expression->inferredType->kind == TypeKind::Map) ? "ts_map_forEach" : "ts_set_forEach";
         llvm::FunctionType* forEachFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, forEachFt);
         createCall(forEachFt, fn.getCallee(), { obj, callback, thisArg });
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
                
                llvm::FunctionType* md5Ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                        { llvm::PointerType::getUnqual(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_crypto_md5", md5Ft);
                lastValue = createCall(md5Ft, fn.getCallee(), { arg });
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
                    llvm::FunctionType* testFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_test", testFt);
                    llvm::Value* res = createCall(testFt, fn.getCallee(), { re, str });
                    lastValue = builder->CreateICmpNE(res, llvm::ConstantInt::get(res->getType(), 0));
                } else {
                    llvm::FunctionType* execFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_exec", execFt);
                    lastValue = createCall(execFt, fn.getCallee(), { re, str });
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
                    
                    llvm::FunctionType* parseFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_json_parse", parseFt);
                    lastValue = createCall(parseFt, fn.getCallee(), { arg });
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
                    
                    llvm::FunctionType* stringifyFt = llvm::FunctionType::get(builder->getPtrTy(), 
                            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_json_stringify", stringifyFt);
                    lastValue = createCall(stringifyFt, fn.getCallee(), { objArg, replacer, space });
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace ts
