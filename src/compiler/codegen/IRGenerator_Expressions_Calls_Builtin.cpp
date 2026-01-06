#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

// Static helper to register core Builtin runtime functions once (~115 functions)
static bool builtinFunctionsRegistered = false;
static void ensureBuiltinFunctionsRegistered(BoxingPolicy& bp) {
    if (builtinFunctionsRegistered) return;
    builtinFunctionsRegistered = true;
    
    // ========== Array methods ==========
    bp.registerRuntimeApi("ts_array_length", {true}, false);  // arr -> int
    bp.registerRuntimeApi("ts_array_push", {true, true}, false);  // arr, value
    bp.registerRuntimeApi("ts_array_pop", {true}, true);  // arr -> boxed
    bp.registerRuntimeApi("ts_array_shift", {true}, true);
    bp.registerRuntimeApi("ts_array_unshift", {true, true}, false);  // arr, value -> length
    bp.registerRuntimeApi("ts_array_slice", {true, false, false}, false);  // arr, start, end -> raw TsArray*
    bp.registerRuntimeApi("ts_array_join", {true, false}, false);  // arr, separator -> string
    bp.registerRuntimeApi("ts_array_indexOf", {true, true, false}, false);  // arr, value, fromIndex -> int
    bp.registerRuntimeApi("ts_array_flat", {true, false}, true);  // arr, depth
    bp.registerRuntimeApi("ts_array_sort", {true}, true);  // arr
    bp.registerRuntimeApi("ts_array_sort_with_comparator", {true, true}, true);  // arr, comparator
    bp.registerRuntimeApi("ts_array_reduce", {true, true, true}, true);  // arr, callback, initialValue
    bp.registerRuntimeApi("ts_array_is_specialized", {true}, false);  // arr -> bool
    
    // ========== Map methods ==========
    bp.registerRuntimeApi("ts_map_set", {true, true, true}, true);  // map, key, value
    bp.registerRuntimeApi("ts_map_get", {true, true}, true);  // map, key
    bp.registerRuntimeApi("ts_map_has", {true, true}, false);  // map, key -> bool
    bp.registerRuntimeApi("ts_map_delete", {true, true}, false);  // map, key -> bool
    bp.registerRuntimeApi("ts_map_clear", {true}, false);
    bp.registerRuntimeApi("ts_map_forEach", {true, true}, false);  // map, callback
    
    // ========== Set methods ==========
    bp.registerRuntimeApi("ts_set_add", {true, true}, true);  // set, value
    bp.registerRuntimeApi("ts_set_has", {true, true}, false);
    bp.registerRuntimeApi("ts_set_delete", {true, true}, false);
    bp.registerRuntimeApi("ts_set_clear", {true}, false);
    bp.registerRuntimeApi("ts_set_forEach", {true, true}, false);
    
    // ========== String methods ==========
    bp.registerRuntimeApi("ts_string_length", {false}, false);  // str -> int
    bp.registerRuntimeApi("ts_string_charAt", {false, false}, false);  // str, index -> string
    bp.registerRuntimeApi("ts_string_charCodeAt", {false, false}, false);  // str, index -> int
    bp.registerRuntimeApi("ts_string_includes", {false, false, false}, false);  // str, search, position -> bool
    bp.registerRuntimeApi("ts_string_indexOf", {false, false, false}, false);  // str, search, start -> int
    bp.registerRuntimeApi("ts_string_substring", {false, false, false}, false);  // str, start, end
    bp.registerRuntimeApi("ts_string_toLowerCase", {false}, false);
    bp.registerRuntimeApi("ts_string_toUpperCase", {false}, false);
    bp.registerRuntimeApi("ts_string_trim", {false}, false);
    bp.registerRuntimeApi("ts_string_split", {false, false}, true);  // str, separator -> array
    bp.registerRuntimeApi("ts_string_split_regexp", {false, true}, true);  // str, regexp
    bp.registerRuntimeApi("ts_string_replace", {false, false, false}, false);  // str, search, replacement
    bp.registerRuntimeApi("ts_string_replace_regexp", {false, true, false}, false);
    bp.registerRuntimeApi("ts_string_replaceAll", {false, false, false}, false);
    bp.registerRuntimeApi("ts_string_replaceAll_regexp", {false, true, false}, false);
    bp.registerRuntimeApi("ts_string_match", {false, false}, true);  // str, pattern -> array or null
    bp.registerRuntimeApi("ts_string_match_regexp", {false, true}, true);
    bp.registerRuntimeApi("ts_string_search_regexp", {false, true}, false);  // -> int
    bp.registerRuntimeApi("ts_string_startsWith", {false, false, false}, false);  // str, search, position -> bool
    bp.registerRuntimeApi("ts_string_padStart", {false, false, false}, false);  // str, targetLength, padString
    bp.registerRuntimeApi("ts_string_padEnd", {false, false, false}, false);
    bp.registerRuntimeApi("ts_string_repeat", {false, false}, false);  // str, count
    
    // ========== Number/conversion ==========
    bp.registerRuntimeApi("ts_int_to_string", {false}, false);  // int -> string
    bp.registerRuntimeApi("ts_double_to_string", {false}, false);
    bp.registerRuntimeApi("ts_double_to_fixed", {false, false}, false);  // double, digits -> string
    
    // ========== Console ==========
    bp.registerRuntimeApi("ts_console_log", {false}, false);  // string
    bp.registerRuntimeApi("ts_console_log_int", {false}, false);
    bp.registerRuntimeApi("ts_console_log_double", {false}, false);
    bp.registerRuntimeApi("ts_console_log_bool", {false}, false);
    bp.registerRuntimeApi("ts_console_log_value", {true}, false);  // boxed value
    bp.registerRuntimeApi("ts_console_error", {false}, false);
    bp.registerRuntimeApi("ts_console_error_int", {false}, false);
    bp.registerRuntimeApi("ts_console_error_double", {false}, false);
    bp.registerRuntimeApi("ts_console_error_bool", {false}, false);
    bp.registerRuntimeApi("ts_console_error_value", {true}, false);
    bp.registerRuntimeApi("ts_console_time", {false}, false);  // label
    bp.registerRuntimeApi("ts_console_time_end", {false}, false);
    bp.registerRuntimeApi("ts_console_trace", {false}, false);
    
    // ========== Math ==========
    bp.registerRuntimeApi("ts_math_abs", {false}, false);  // double -> double
    bp.registerRuntimeApi("ts_math_ceil", {false}, false);
    bp.registerRuntimeApi("ts_math_floor", {false}, false);
    bp.registerRuntimeApi("ts_math_round", {false}, false);
    bp.registerRuntimeApi("ts_math_sqrt", {false}, false);
    bp.registerRuntimeApi("ts_math_pow", {false, false}, false);
    bp.registerRuntimeApi("ts_math_min", {false, false}, false);
    bp.registerRuntimeApi("ts_math_max", {false, false}, false);
    bp.registerRuntimeApi("ts_math_random", {}, false);
    bp.registerRuntimeApi("ts_math_clz32", {false}, false);
    bp.registerRuntimeApi("ts_math_fround", {false}, false);
    bp.registerRuntimeApi("ts_math_hypot", {true}, false);  // array of numbers
    
    // ========== JSON ==========
    bp.registerRuntimeApi("ts_json_parse", {false}, true);  // string -> boxed
    bp.registerRuntimeApi("ts_json_stringify", {true}, false);  // boxed -> string
    
    // ========== Promise ==========
    bp.registerRuntimeApi("ts_promise_resolve", {false, true}, true);  // (context, value) -> Promise
    bp.registerRuntimeApi("ts_promise_reject", {false, true}, true);  // (context, reason) -> Promise
    bp.registerRuntimeApi("ts_promise_all", {true}, true);  // array -> Promise
    bp.registerRuntimeApi("ts_promise_race", {true}, true);
    bp.registerRuntimeApi("ts_promise_any", {true}, true);
    bp.registerRuntimeApi("ts_promise_allSettled", {true}, true);
    
    // ========== Symbol ==========
    bp.registerRuntimeApi("ts_symbol_for", {false}, true);  // key -> Symbol
    bp.registerRuntimeApi("ts_symbol_key_for", {true}, false);  // symbol -> string or undefined
    
    // ========== Crypto ==========
    bp.registerRuntimeApi("ts_crypto_md5", {false}, false);  // string -> string
    
    // ========== HTTP Server response (used in Builtin) ==========
    bp.registerRuntimeApi("ts_http_create_server", {true}, true);
    bp.registerRuntimeApi("ts_http_server_listen", {true, false, false, true}, true);
    bp.registerRuntimeApi("ts_server_response_write", {true, true}, false);  // res, data
    bp.registerRuntimeApi("ts_server_response_end", {true, true}, false);  // res, data
    bp.registerRuntimeApi("ts_server_response_write_head", {true, false, true}, false);  // res, status, headers
    
    // ========== FS functions referenced in main Builtin ==========
    bp.registerRuntimeApi("ts_fs_readFileSync", {false}, false);
    bp.registerRuntimeApi("ts_fs_writeFileSync", {false, true}, false);
    bp.registerRuntimeApi("ts_fs_existsSync", {false}, false);
    bp.registerRuntimeApi("ts_fs_readdirSync", {false}, true);
    bp.registerRuntimeApi("ts_fs_statSync", {false}, true);
    bp.registerRuntimeApi("ts_fs_openSync", {false, false, false}, false);
    bp.registerRuntimeApi("ts_fs_closeSync", {false}, false);
    bp.registerRuntimeApi("ts_fs_createReadStream", {false, true}, true);
    bp.registerRuntimeApi("ts_fs_createWriteStream", {false, true}, true);
    bp.registerRuntimeApi("ts_fs_readFile_async", {false}, true);
    bp.registerRuntimeApi("ts_fs_writeFile_async", {false, true}, true);
    bp.registerRuntimeApi("ts_fs_mkdir_async", {false, true}, true);
    bp.registerRuntimeApi("ts_fs_stat_async", {false}, true);
    bp.registerRuntimeApi("ts_fs_open_async", {false, false, false}, true);
    bp.registerRuntimeApi("ts_fs_close_async", {false}, true);
    bp.registerRuntimeApi("ts_fs_readdir_async", {false}, true);
    
    // ========== Object static methods ==========
    bp.registerRuntimeApi("ts_object_keys", {true}, true);  // obj -> array
    bp.registerRuntimeApi("ts_object_values", {true}, true);  // obj -> array
    bp.registerRuntimeApi("ts_object_entries", {true}, true);  // obj -> array
    bp.registerRuntimeApi("ts_object_assign", {true, true}, true);  // target, source
    bp.registerRuntimeApi("ts_object_freeze", {true}, true);  // obj -> obj
    bp.registerRuntimeApi("ts_object_seal", {true}, true);  // obj -> obj
    bp.registerRuntimeApi("ts_object_is_frozen", {true}, false);  // obj -> bool
    bp.registerRuntimeApi("ts_object_is_sealed", {true}, false);  // obj -> bool
    bp.registerRuntimeApi("ts_object_has_own", {true, false}, false);  // obj, key -> bool
    bp.registerRuntimeApi("ts_object_from_entries", {true}, true);  // entries -> obj
    
    // ========== Value boxing helpers ==========
    bp.registerRuntimeApi("ts_value_make_int", {false}, true);
    bp.registerRuntimeApi("ts_value_make_double", {false}, true);
    bp.registerRuntimeApi("ts_value_make_object", {true}, true);
    bp.registerRuntimeApi("ts_value_get_int", {true}, false);
    bp.registerRuntimeApi("ts_value_get_object", {true}, true);
}

bool IRGenerator::tryGenerateBuiltinCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureBuiltinFunctionsRegistered(boxingPolicy);
    
    SPDLOG_DEBUG("tryGenerateBuiltinCall: {}", prop->name);

    if (tryGenerateFSCall(node, prop)) return true;
    if (tryGeneratePathCall(node, prop)) return true;
    if (tryGenerateEventsCall(node, prop)) return true;
    if (tryGeneratePromiseCall(node, prop)) return true;
    if (tryGenerateStreamCall(node, prop)) return true;
    if (tryGenerateBufferCall(node, prop)) return true;
    if (tryGenerateProcessCall(node, prop)) return true;
    if (tryGenerateHTTPCall(node, prop)) return true;  // HTTP before Net - HTTP Server.listen() uses ts_http_server_listen
    if (tryGenerateNetCall(node, prop)) return true;
    if (tryGenerateTextEncodingCall(node, prop)) return true;
    if (tryGenerateURLCall(node, prop)) return true;
    if (tryGenerateUtilCall(node, prop)) return true;

    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "Object") {
            if (prop->name == "keys") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* obj = boxValue(lastValue, node->arguments[0]->inferredType);
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_keys", ft);
                lastValue = createCall(ft, fn.getCallee(), { obj });
                return true;
            }
        }
        if (id->name == "Symbol") {
            if (prop->name == "for") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* key = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_symbol_for", ft);
                lastValue = createCall(ft, fn.getCallee(), { key });
                return true;
            } else if (prop->name == "keyFor") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* sym = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_symbol_key_for", ft);
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
                    llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_int", unboxFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_http_server_listen", ft);
                
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
                    llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_int", unboxFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_server_response_write_head", ft);
                
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

    if (prop->name == "log" || prop->name == "error" || prop->name == "warn" || prop->name == "info" || prop->name == "time" || prop->name == "timeEnd" || prop->name == "trace") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "console") {
                if (prop->name == "time" || prop->name == "timeEnd") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* label = unboxValue(lastValue, node->arguments[0]->inferredType);
                    
                    std::string funcName = (prop->name == "time") ? "ts_console_time" : "ts_console_time_end";
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
                    createCall(ft, fn.getCallee(), { label });
                    lastValue = nullptr;
                    return true;
                }

                if (prop->name == "trace") {
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {}, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_console_trace", ft);
                    createCall(ft, fn.getCallee(), {});
                    lastValue = nullptr;
                    return true;
                }

                if (node->arguments.empty()) return true;
                
                bool isError = (prop->name == "error" || prop->name == "warn");
                
                for (size_t i = 0; i < node->arguments.size(); ++i) {
                    visit(node->arguments[i].get());
                    llvm::Value* arg = lastValue;
                    if (!arg) continue;

                    llvm::Type* argType = arg->getType();
                    
                    std::string funcName = isError ? "ts_console_error" : "ts_console_log";
                    llvm::Type* paramType = llvm::PointerType::getUnqual(*context);

                    if (argType->isIntegerTy(64)) {
                        funcName = isError ? "ts_console_error_int" : "ts_console_log_int";
                        paramType = llvm::Type::getInt64Ty(*context);
                    } else if (argType->isDoubleTy()) {
                        funcName = isError ? "ts_console_error_double" : "ts_console_log_double";
                        paramType = llvm::Type::getDoubleTy(*context);
                    } else if (argType->isIntegerTy(1)) {
                        funcName = isError ? "ts_console_error_bool" : "ts_console_log_bool";
                        paramType = llvm::Type::getInt1Ty(*context);
                    } else if (argType->isPointerTy()) {
                        // If it's in boxedValues, it's a TsValue* - use ts_console_log_value
                        // Otherwise check the inferred type
                        if (boxedValues.count(arg)) {
                            funcName = isError ? "ts_console_error_value" : "ts_console_log_value";
                        } else if (node->arguments[i]->inferredType && node->arguments[i]->inferredType->kind == TypeKind::String) {
                            funcName = isError ? "ts_console_error" : "ts_console_log";
                        } else {
                            // Unknown pointer type - box it and use ts_console_log_value
                            funcName = isError ? "ts_console_error_value" : "ts_console_log_value";
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_http_create_server", ft);
                
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
                llvm::FunctionCallee readFn = getRuntimeFunction("ts_fs_readFileSync", readFt);
                
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
                llvm::FunctionCallee writeFn = getRuntimeFunction("ts_fs_writeFileSync", writeFt);
                
                createCall(writeFt, writeFn.getCallee(), { path, data });
                lastValue = nullptr;
                return true;
            } else if (prop->name == "existsSync") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                
                llvm::FunctionType* existsFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), 
                        { builder->getPtrTy() }, false);
                llvm::FunctionCallee existsFn = getRuntimeFunction("ts_fs_existsSync", existsFt);
                
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_readdirSync", ft);
                
                lastValue = unboxValue(createCall(ft, fn.getCallee(), { path, options }), node->inferredType);
                return true;
            } else if (prop->name == "statSync") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                        { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_statSync", ft);
                
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_openSync", ft);
                
                llvm::Value* fd = createCall(ft, fn.getCallee(), { path, flags, mode });
                
                llvm::FunctionType* makeDoubleFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee makeDoubleFn = getRuntimeFunction("ts_value_make_double", makeDoubleFt);
                lastValue = createCall(makeDoubleFt, makeDoubleFn.getCallee(), { fd });
                return true;
            } else if (prop->name == "closeSync") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                        { llvm::Type::getDoubleTy(*context) }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_closeSync", ft);
                
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
                llvm::FunctionCallee makeIntFn = getRuntimeFunction("ts_value_make_int", makeIntFt);
                lastValue = createCall(makeIntFt, makeIntFn.getCallee(), { result });
                return true;
            } else if (prop->name == "createReadStream") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_createReadStream", ft);
                lastValue = createCall(ft, fn.getCallee(), { path });
                return true;
            } else if (prop->name == "createWriteStream") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* path = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_createWriteStream", ft);
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
                    llvm::FunctionCallee readFn = getRuntimeFunction("ts_fs_readFile_async", readFt);
                    
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
                llvm::FunctionCallee writeFn = getRuntimeFunction("ts_fs_writeFileSync", writeFt);
                
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
                    llvm::FunctionCallee writeFn = getRuntimeFunction("ts_fs_writeFile_async", writeFt);
                    
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
                    llvm::FunctionCallee mkdirFn = getRuntimeFunction("ts_fs_mkdir_async", mkdirFt);
                    
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
                    llvm::FunctionCallee statFn = getRuntimeFunction("ts_fs_stat_async", statFt);
                    
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
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_open_async", ft);
                    
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
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_close_async", ft);
                    
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
                    llvm::FunctionCallee readdirFn = getRuntimeFunction("ts_fs_readdir_async", readdirFt);
                    
                    lastValue = createCall(readdirFt, readdirFn.getCallee(), { path });
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_abs", absFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_floor", floorFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_ceil", ceilFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_round", roundFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_sqrt", sqrtFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_pow", powFt);
                lastValue = createCall(powFt, fn.getCallee(), { arg1, arg2 });
                return true;
            } else if (prop->name == "random") {
                llvm::FunctionType* randomFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), {}, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_random", randomFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_hypot", hypotFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_fround", froundFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_clz32", clz32Ft);
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
                // ts_promise_resolve(void* context, TsValue* value)
                llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee resolveFn = getRuntimeFunction("ts_promise_resolve", resolveFt);

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
                llvm::Value* ctx = currentAsyncContext ? currentAsyncContext : llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(resolveFt, resolveFn.getCallee(), { ctx, boxedVal });
                return true;
            } else if (prop->name == "reject") {
                // ts_promise_reject(void* context, TsValue* reason)
                llvm::FunctionType* rejectFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee rejectFn = getRuntimeFunction("ts_promise_reject", rejectFt);
                
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
                llvm::Value* ctx = currentAsyncContext ? currentAsyncContext : llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(rejectFt, rejectFn.getCallee(), { ctx, boxedVal });
                return true;
            } else if (prop->name == "all") {
                if (node->arguments.empty()) return true;
                llvm::FunctionType* allFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee allFn = getRuntimeFunction("ts_promise_all", allFt);
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = createCall(allFt, allFn.getCallee(), { iterable });
                return true;
            } else if (prop->name == "race") {
                if (node->arguments.empty()) return true;
                llvm::FunctionType* raceFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee raceFn = getRuntimeFunction("ts_promise_race", raceFt);
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = createCall(raceFt, raceFn.getCallee(), { iterable });
                return true;
            } else if (prop->name == "allSettled") {
                if (node->arguments.empty()) return true;
                llvm::FunctionType* allSettledFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee allSettledFn = getRuntimeFunction("ts_promise_allSettled", allSettledFt);
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = createCall(allSettledFt, allSettledFn.getCallee(), { iterable });
                return true;
            } else if (prop->name == "any") {
                if (node->arguments.empty()) return true;
                llvm::FunctionType* anyFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee anyFn = getRuntimeFunction("ts_promise_any", anyFt);
                
                visit(node->arguments[0].get());
                llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);
                lastValue = createCall(anyFt, anyFn.getCallee(), { iterable });
                return true;
            }
        } else if (obj->name == "Object") {
            if (prop->name == "keys") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                // Ensure we have a pointer
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                
                llvm::FunctionType* keysFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee keysFn = getRuntimeFunction("ts_object_keys", keysFt);
                lastValue = createCall(keysFt, keysFn.getCallee(), { arg });
                return true;
            } else if (prop->name == "values") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                
                llvm::FunctionType* valuesFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee valuesFn = getRuntimeFunction("ts_object_values", valuesFt);
                lastValue = createCall(valuesFt, valuesFn.getCallee(), { arg });
                return true;
            } else if (prop->name == "entries") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = boxValue(lastValue, node->arguments[0]->inferredType);
                
                llvm::FunctionType* entriesFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee entriesFn = getRuntimeFunction("ts_object_entries", entriesFt);
                lastValue = createCall(entriesFt, entriesFn.getCallee(), { arg });
                return true;
            } else if (prop->name == "assign") {
                if (node->arguments.size() < 2) return true;
                
                // Visit target (first argument)
                visit(node->arguments[0].get());
                llvm::Value* target = lastValue;
                if (!target->getType()->isPointerTy()) {
                    target = builder->CreateIntToPtr(target, builder->getPtrTy());
                }
                
                // Process all source objects (arguments[1] onward)
                llvm::FunctionType* assignFt = llvm::FunctionType::get(builder->getPtrTy(), 
                        { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee assignFn = getRuntimeFunction("ts_object_assign", assignFt);
                
                for (size_t i = 1; i < node->arguments.size(); ++i) {
                    visit(node->arguments[i].get());
                    llvm::Value* source = lastValue;
                    if (!source->getType()->isPointerTy()) {
                        source = builder->CreateIntToPtr(source, builder->getPtrTy());
                    }
                    // Chain: target = ts_object_assign(target, source)
                    target = createCall(assignFt, assignFn.getCallee(), { target, source });
                }
                
                lastValue = target;
                return true;
            } else if (prop->name == "freeze") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                
                llvm::FunctionType* freezeFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee freezeFn = getRuntimeFunction("ts_object_freeze", freezeFt);
                lastValue = createCall(freezeFt, freezeFn.getCallee(), { arg });
                return true;
            } else if (prop->name == "seal") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                
                llvm::FunctionType* sealFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee sealFn = getRuntimeFunction("ts_object_seal", sealFt);
                lastValue = createCall(sealFt, sealFn.getCallee(), { arg });
                return true;
            } else if (prop->name == "isFrozen") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                
                llvm::FunctionType* isFrozenFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee isFrozenFn = getRuntimeFunction("ts_object_is_frozen", isFrozenFt);
                lastValue = createCall(isFrozenFt, isFrozenFn.getCallee(), { arg });
                lastValue = builder->CreateZExt(lastValue, llvm::Type::getInt64Ty(*context));
                return true;
            } else if (prop->name == "isSealed") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                
                llvm::FunctionType* isSealedFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee isSealedFn = getRuntimeFunction("ts_object_is_sealed", isSealedFt);
                lastValue = createCall(isSealedFt, isSealedFn.getCallee(), { arg });
                lastValue = builder->CreateZExt(lastValue, llvm::Type::getInt64Ty(*context));
                return true;
            } else if (prop->name == "hasOwn") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* objArg = lastValue;
                if (!objArg->getType()->isPointerTy()) {
                    objArg = builder->CreateIntToPtr(objArg, builder->getPtrTy());
                }
                visit(node->arguments[1].get());
                llvm::Value* propArg = lastValue;
                if (!propArg->getType()->isPointerTy()) {
                    propArg = builder->CreateIntToPtr(propArg, builder->getPtrTy());
                }
                
                llvm::FunctionType* hasOwnFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), 
                        { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee hasOwnFn = getRuntimeFunction("ts_object_has_own", hasOwnFt);
                lastValue = createCall(hasOwnFt, hasOwnFn.getCallee(), { objArg, propArg });
                lastValue = builder->CreateZExt(lastValue, llvm::Type::getInt64Ty(*context));
                return true;
            } else if (prop->name == "fromEntries") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                
                llvm::FunctionType* fromEntriesFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fromEntriesFn = getRuntimeFunction("ts_object_from_entries", fromEntriesFt);
                lastValue = createCall(fromEntriesFt, fromEntriesFn.getCallee(), { arg });
                return true;
            }
        } else if (obj->name == "Array") {
            if (prop->name == "isArray") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                
                // For primitives, always return false
                if (!arg->getType()->isPointerTy()) {
                    lastValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
                    return true;
                }
                
                llvm::FunctionType* isArrayFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee isArrayFn = getRuntimeFunction("ts_array_is_array", isArrayFt);
                lastValue = createCall(isArrayFt, isArrayFn.getCallee(), { arg });
                lastValue = builder->CreateZExt(lastValue, llvm::Type::getInt64Ty(*context));
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
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_charCodeAt", charCodeAtFt);
         
         lastValue = createCall(charCodeAtFt, fn.getCallee(), { obj, index });
         return true;
    } else if (prop->name == "charAt") {
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

         llvm::FunctionType* charAtFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_charAt", charAtFt);
         
         lastValue = createCall(charAtFt, fn.getCallee(), { obj, index });
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
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_trim", trimFt);
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
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_startsWith", startsWithFt);
         lastValue = createCall(startsWithFt, fn.getCallee(), { obj, prefix });
         return true;
    } else if (prop->name == "includes" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::String) {
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
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_includes", includesFt);
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
             llvm::FunctionCallee fn = getRuntimeFunction("ts_array_indexOf", indexOfFt);
             lastValue = createCall(indexOfFt, fn.getCallee(), { obj, search });
         } else {
             if (search->getType()->isIntegerTy(64)) {
                 search = builder->CreateIntToPtr(search, builder->getPtrTy());
             }
             llvm::FunctionType* indexOfFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { builder->getPtrTy(), builder->getPtrTy() }, false);
             llvm::FunctionCallee fn = getRuntimeFunction("ts_string_indexOf", indexOfFt);
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
             llvm::FunctionCallee lenFn = getRuntimeFunction("ts_array_length", lenFt);
             end = createCall(lenFt, lenFn.getCallee(), { obj });
         }
         
         llvm::FunctionType* sliceFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_slice", sliceFt);
         lastValue = createCall(sliceFt, fn.getCallee(), { obj, start, end });
         return true;
    } else if (prop->name == "at" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) {
             lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
             return true;
         }
         
         visit(node->arguments[0].get());
         llvm::Value* idx = lastValue;
         if (idx->getType()->isDoubleTy()) {
             idx = builder->CreateFPToSI(idx, llvm::Type::getInt64Ty(*context));
         }
         
         llvm::FunctionType* atFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_at", atFt);
         llvm::Value* ret = createCall(atFt, fn.getCallee(), { obj, idx });
         
         // Unbox the result to the element type
         std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
         if (prop->expression->inferredType->kind == TypeKind::Array) {
             elemType = std::static_pointer_cast<ArrayType>(prop->expression->inferredType)->elementType;
         }
         boxedValues.insert(ret);
         lastValue = unboxValue(ret, elemType);
         return true;
    } else if ((prop->name == "slice" || prop->name == "substring") && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::String) {
         // String slice/substring: str.slice(start, end) or str.substring(start, end)
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         
         llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
         llvm::Value* end = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), -1);  // -1 means end of string
         
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
         }
         
         llvm::FunctionType* sliceFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_slice", sliceFt);
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
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_join", joinFt);
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
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_toLowerCase", toLowerFt);
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
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_toUpperCase", toUpperFt);
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
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_repeat", repeatFt);
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
             llvm::FunctionCallee lenFn = getRuntimeFunction("ts_string_length", lenFt);
             end = createCall(lenFt, lenFn.getCallee(), { obj });
         }

         llvm::FunctionType* substringFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_substring", substringFt);
         lastValue = createCall(substringFt, fn.getCallee(), { obj, start, end });
         return true;
    } else if (prop->name == "push" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* arrObj = lastValue;
         if (arrObj->getType()->isIntegerTy(64)) {
             arrObj = builder->CreateIntToPtr(arrObj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) {
             lastValue = nullptr;
             return true;
         }
         
         // Get the array's element type to determine if we should use specialized storage
         auto arrType = std::static_pointer_cast<ArrayType>(prop->expression->inferredType);
         std::shared_ptr<Type> elemType = arrType->elementType;
         
         visit(node->arguments[0].get());
         llvm::Value* val = lastValue;
         std::shared_ptr<Type> argType = node->arguments[0]->inferredType;
         
         // For specialized arrays (int[], double[]), store raw values without boxing
         // This matches how array literals handle specialized storage
         llvm::Value* pushVal;
         if (elemType->kind == TypeKind::Int) {
             // For int arrays, cast the value to i64 then to ptr (raw storage)
             if (val->getType()->isPointerTy()) {
                 // Value is boxed, unbox it first
                 val = unboxValue(val, elemType);
             }
             if (!val->getType()->isIntegerTy(64)) {
                 val = builder->CreateIntCast(val, builder->getInt64Ty(), true);
             }
             pushVal = builder->CreateIntToPtr(val, builder->getPtrTy());
         } else if (elemType->kind == TypeKind::Double) {
             // For double arrays, bitcast double to i64 then to ptr
             if (val->getType()->isPointerTy()) {
                 val = unboxValue(val, elemType);
             }
             llvm::Value* asInt = builder->CreateBitCast(val, builder->getInt64Ty());
             pushVal = builder->CreateIntToPtr(asInt, builder->getPtrTy());
         } else {
             // For all other types (Any, Class, String, etc.), box the value
             pushVal = boxValue(val, argType);
         }
         
         llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);
         createCall(pushFt, pushFn.getCallee(), { arrObj, pushVal });
         lastValue = nullptr;
         return true;
    } else if (prop->name == "pop" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* arrObj = lastValue;
         if (arrObj->getType()->isIntegerTy(64)) {
             arrObj = builder->CreateIntToPtr(arrObj, builder->getPtrTy());
         }
         
         llvm::FunctionType* popFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee popFn = getRuntimeFunction("ts_array_pop", popFt);
         llvm::Value* ret = createCall(popFt, popFn.getCallee(), { arrObj });
         
         std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
         if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
             elemType = std::static_pointer_cast<ArrayType>(prop->expression->inferredType)->elementType;
         }
         lastValue = unboxValue(ret, elemType);
         return true;
    } else if (prop->name == "shift" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* arrObj = lastValue;
         if (arrObj->getType()->isIntegerTy(64)) {
             arrObj = builder->CreateIntToPtr(arrObj, builder->getPtrTy());
         }
         
         llvm::FunctionType* shiftFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee shiftFn = getRuntimeFunction("ts_array_shift", shiftFt);
         llvm::Value* ret = createCall(shiftFt, shiftFn.getCallee(), { arrObj });
         
         std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
         if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
             elemType = std::static_pointer_cast<ArrayType>(prop->expression->inferredType)->elementType;
         }
         lastValue = unboxValue(ret, elemType);
         return true;
    } else if (prop->name == "includes" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* arrObj = lastValue;
         if (arrObj->getType()->isIntegerTy(64)) {
             arrObj = builder->CreateIntToPtr(arrObj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) {
             lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
             return true;
         }
         
         visit(node->arguments[0].get());
         llvm::Value* val = lastValue;
         // ts_array_includes expects int64_t, not boxed value
         if (val->getType()->isDoubleTy()) {
             val = builder->CreateFPToSI(val, llvm::Type::getInt64Ty(*context));
         } else if (val->getType()->isIntegerTy(1)) {
             val = builder->CreateZExt(val, llvm::Type::getInt64Ty(*context));
         } else if (!val->getType()->isIntegerTy(64)) {
             val = builder->CreatePtrToInt(val, llvm::Type::getInt64Ty(*context));
         }
         
         llvm::FunctionType* includesFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_includes", includesFt);
         lastValue = createCall(includesFt, fn.getCallee(), { arrObj, val });
         return true;
    } else if (prop->name == "concat" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         // JavaScript concat creates a NEW array - doesn't modify original
         // 1. Create new empty array
         // 2. Concat source array into it
         // 3. Concat each argument array into it
         // 4. Return new array
         
         visit(prop->expression.get());
         llvm::Value* srcArr = lastValue;
         if (srcArr->getType()->isIntegerTy(64)) {
             srcArr = builder->CreateIntToPtr(srcArr, builder->getPtrTy());
         }
         
         // Create new empty array
         llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
         llvm::FunctionCallee createFn = getRuntimeFunction("ts_array_create", createFt);
         llvm::Value* newArr = createCall(createFt, createFn.getCallee(), {});
         
         // Concat source array into new array
         llvm::FunctionType* concatFt = llvm::FunctionType::get(builder->getVoidTy(),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee concatFn = getRuntimeFunction("ts_array_concat", concatFt);
         createCall(concatFt, concatFn.getCallee(), { newArr, srcArr });
         
         // Concat each argument array
         for (auto& arg : node->arguments) {
             visit(arg.get());
             llvm::Value* argArr = lastValue;
             if (argArr->getType()->isIntegerTy(64)) {
                 argArr = builder->CreateIntToPtr(argArr, builder->getPtrTy());
             }
             createCall(concatFt, concatFn.getCallee(), { newArr, argArr });
         }
         
         lastValue = newArr;
         return true;
    } else if (prop->name == "unshift" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* arrObj = lastValue;
         if (arrObj->getType()->isIntegerTy(64)) {
             arrObj = builder->CreateIntToPtr(arrObj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) {
             lastValue = nullptr;
             return true;
         }
         
         visit(node->arguments[0].get());
         llvm::Value* val = lastValue;
         std::shared_ptr<Type> argType = node->arguments[0]->inferredType;
         llvm::Value* boxedVal = boxValue(val, argType);
         
         llvm::FunctionType* unshiftFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee unshiftFn = getRuntimeFunction("ts_array_unshift", unshiftFt);
         createCall(unshiftFt, unshiftFn.getCallee(), { arrObj, boxedVal });
         lastValue = nullptr;
         return true;
    } else if (prop->name == "reverse" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         llvm::FunctionType* reverseFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_reverse", reverseFt);
         createCall(reverseFt, fn.getCallee(), { obj });
         lastValue = obj;  // reverse() returns the array for chaining
         return true;
    } else if (prop->name == "sort" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) {
             // No comparator - use default sort
             llvm::FunctionType* sortFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                     { builder->getPtrTy() }, false);
             llvm::FunctionCallee fn = getRuntimeFunction("ts_array_sort", sortFt);
             createCall(sortFt, fn.getCallee(), { obj });
         } else {
             // Has comparator - use sort with comparator
             visit(node->arguments[0].get());
             llvm::Value* comparator = boxValue(lastValue, node->arguments[0]->inferredType);
             
             llvm::FunctionType* sortFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                     { builder->getPtrTy(), builder->getPtrTy() }, false);
             llvm::FunctionCallee fn = getRuntimeFunction("ts_array_sort_with_comparator", sortFt);
             createCall(sortFt, fn.getCallee(), { obj, comparator });
         }
         lastValue = obj;  // sort() returns the array for chaining
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
             // Mark as boxed so unboxValue will process it
             boxedValues.insert(lastValue);
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
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_flat", flatFt);
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
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_reduce", reduceFt);
         lastValue = createCall(reduceFt, fn.getCallee(), { obj, callback, initialValue });
         return true;
    } else if (prop->name == "set" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Map) {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, builder->getPtrTy());
         }
         // Unbox map if it's a boxed TsValue* (e.g., from a cell)
         map = unboxValue(map, prop->expression->inferredType);
         
         if (node->arguments.size() < 2) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
         visit(node->arguments[1].get());
         llvm::Value* value = boxValue(lastValue, node->arguments[1]->inferredType);
         
         // Use inline map set operation
         emitInlineMapSet(map, key, value);
         lastValue = map;
         return true;
    } else if (prop->name == "get" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Map) {
         SPDLOG_INFO("Map.get: node->inferredType = {}", node->inferredType ? std::to_string(static_cast<int>(node->inferredType->kind)) : "null");
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, builder->getPtrTy());
         }
         // Unbox map if it's a boxed TsValue* (e.g., from a cell)
         map = unboxValue(map, prop->expression->inferredType);
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
         
         // Use inline map get operation
         llvm::Value* boxedResult = emitInlineMapGet(map, key);
         
         // emitInlineMapGet returns boxed TsValue* on stack, unbox based on call expression's inferred type
         lastValue = unboxValue(boxedResult, node->inferredType);
         return true;
    } else if (prop->name == "has" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Map) {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, builder->getPtrTy());
         }
         // Unbox map if it's a boxed TsValue* (e.g., from a cell)
         map = unboxValue(map, prop->expression->inferredType);
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* hasFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_map_has", hasFt);
         lastValue = createCall(hasFt, fn.getCallee(), { map, key });
         return true;
    } else if (prop->name == "delete" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Map) {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, builder->getPtrTy());
         }
         // Unbox map if it's a boxed TsValue* (e.g., from a cell)
         map = unboxValue(map, prop->expression->inferredType);
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* deleteFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_map_delete", deleteFt);
         lastValue = createCall(deleteFt, fn.getCallee(), { map, key });
         return true;
    } else if (prop->name == "clear" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Map) {
         visit(prop->expression.get());
         llvm::Value* map = lastValue;
         if (map->getType()->isIntegerTy(64)) {
             map = builder->CreateIntToPtr(map, builder->getPtrTy());
         }
         // Unbox map if it's a boxed TsValue* (e.g., from a cell)
         map = unboxValue(map, prop->expression->inferredType);
         
         llvm::FunctionType* clearFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_map_clear", clearFt);
         createCall(clearFt, fn.getCallee(), { map });
         lastValue = nullptr;
         return true;
    } else if (prop->name == "add" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::SetType) {
         visit(prop->expression.get());
         llvm::Value* set = lastValue;
         // Unbox if the value came from array element access
         set = unboxValue(set, prop->expression->inferredType);
         if (set->getType()->isIntegerTy(64)) {
             set = builder->CreateIntToPtr(set, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* value = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* addFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_set_add", addFt);
         createCall(addFt, fn.getCallee(), { set, value });
         lastValue = set;
         return true;
    } else if (prop->name == "has" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::SetType) {
         visit(prop->expression.get());
         llvm::Value* set = lastValue;
         // Unbox if the value came from array element access
         set = unboxValue(set, prop->expression->inferredType);
         if (set->getType()->isIntegerTy(64)) {
             set = builder->CreateIntToPtr(set, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* value = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* hasFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_set_has", hasFt);
         lastValue = createCall(hasFt, fn.getCallee(), { set, value });
         return true;
    } else if (prop->name == "delete" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::SetType) {
         visit(prop->expression.get());
         llvm::Value* set = lastValue;
         // Unbox if the value came from array element access
         set = unboxValue(set, prop->expression->inferredType);
         if (set->getType()->isIntegerTy(64)) {
             set = builder->CreateIntToPtr(set, builder->getPtrTy());
         }
         
         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* value = boxValue(lastValue, node->arguments[0]->inferredType);
         
         llvm::FunctionType* deleteFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_set_delete", deleteFt);
         lastValue = createCall(deleteFt, fn.getCallee(), { set, value });
         return true;
    } else if (prop->name == "clear" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::SetType) {
         visit(prop->expression.get());
         llvm::Value* set = lastValue;
         // Unbox if the value came from array element access
         set = unboxValue(set, prop->expression->inferredType);
         if (set->getType()->isIntegerTy(64)) {
             set = builder->CreateIntToPtr(set, builder->getPtrTy());
         }
         
         llvm::FunctionType* clearFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_set_clear", clearFt);
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
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_md5", md5Ft);
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
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_json_parse", parseFt);
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
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_json_stringify", stringifyFt);
                    lastValue = createCall(stringifyFt, fn.getCallee(), { objArg, replacer, space });
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace ts
