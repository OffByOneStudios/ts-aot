#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include "../analysis/Monomorphizer.h"
#include "../analysis/Module.h"
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
    bp.registerRuntimeApi("ts_array_lastIndexOf", {true, true, false}, false);  // arr, value, fromIndex -> int
    bp.registerRuntimeApi("ts_array_flat", {true, false}, true);  // arr, depth
    bp.registerRuntimeApi("ts_array_sort", {true}, true);  // arr
    bp.registerRuntimeApi("ts_array_sort_with_comparator", {true, true}, true);  // arr, comparator
    bp.registerRuntimeApi("ts_array_reduce", {true, true, true}, true);  // arr, callback, initialValue
    bp.registerRuntimeApi("ts_array_reduceRight", {true, true, true}, true);  // arr, callback, initialValue
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
    bp.registerRuntimeApi("ts_string_at", {false, false}, false);  // str, index -> string (supports negative indices)
    bp.registerRuntimeApi("ts_string_charCodeAt", {false, false}, false);  // str, index -> int
    bp.registerRuntimeApi("ts_string_codePointAt", {false, false}, false);  // str, index -> int
    bp.registerRuntimeApi("ts_string_fromCodePoint", {false}, false);  // codePointsArray -> str
    bp.registerRuntimeApi("ts_string_raw", {false, false}, false);  // templateObj, substitutionsArray -> str
    bp.registerRuntimeApi("ts_string_includes", {false, false, false}, false);  // str, search, position -> bool
    bp.registerRuntimeApi("ts_string_indexOf", {false, false, false}, false);  // str, search, start -> int
    bp.registerRuntimeApi("ts_string_lastIndexOf", {false, false, false}, false);  // str, search, start -> int
    bp.registerRuntimeApi("ts_string_substring", {false, false, false}, false);  // str, start, end
    bp.registerRuntimeApi("ts_string_toLowerCase", {false}, false);
    bp.registerRuntimeApi("ts_string_toUpperCase", {false}, false);
    bp.registerRuntimeApi("ts_string_normalize", {false, false}, false);  // str, form -> str
    bp.registerRuntimeApi("ts_string_trim", {false}, false);
    bp.registerRuntimeApi("ts_string_trimStart", {false}, false);
    bp.registerRuntimeApi("ts_string_trimEnd", {false}, false);
    bp.registerRuntimeApi("ts_string_split", {false, false}, true);  // str, separator -> array
    bp.registerRuntimeApi("ts_string_split_regexp", {false, true}, true);  // str, regexp
    bp.registerRuntimeApi("ts_string_replace", {false, false, false}, false);  // str, search, replacement
    bp.registerRuntimeApi("ts_string_replace_regexp", {false, true, false}, false);
    bp.registerRuntimeApi("ts_string_replaceAll", {false, false, false}, false);
    bp.registerRuntimeApi("ts_string_replaceAll_regexp", {false, true, false}, false);
    bp.registerRuntimeApi("ts_string_isWellFormed", {false}, false);  // str -> bool (ES2024)
    bp.registerRuntimeApi("ts_string_toWellFormed", {false}, false);  // str -> str (ES2024)
    bp.registerRuntimeApi("ts_string_match", {false, false}, true);  // str, pattern -> array or null
    bp.registerRuntimeApi("ts_string_match_regexp", {false, true}, true);
    bp.registerRuntimeApi("ts_string_matchAll_regexp", {false, true}, true);  // ES2020: str, regexp -> array of match arrays
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
    bp.registerRuntimeApi("ts_console_time_log", {false}, false);
    bp.registerRuntimeApi("ts_console_trace", {}, false);
    bp.registerRuntimeApi("ts_console_dir", {true}, false);  // boxed value
    bp.registerRuntimeApi("ts_console_count", {false}, false);  // label
    bp.registerRuntimeApi("ts_console_count_reset", {false}, false);
    bp.registerRuntimeApi("ts_console_group", {false}, false);  // label
    bp.registerRuntimeApi("ts_console_group_collapsed", {false}, false);
    bp.registerRuntimeApi("ts_console_group_end", {}, false);  // no args
    bp.registerRuntimeApi("ts_console_clear", {}, false);  // no args
    bp.registerRuntimeApi("ts_console_table", {true, true}, false);  // data, properties (both boxed)

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
    bp.registerRuntimeApi("ts_math_imul", {false, false}, false);  // (a, b) -> i32
    
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
    bp.registerRuntimeApi("ts_promise_withResolvers", {}, true);  // no args, returns object
    
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
    bp.registerRuntimeApi("ts_object_preventExtensions", {true}, true);  // obj -> obj
    bp.registerRuntimeApi("ts_object_isFrozen", {true}, true);  // obj -> bool (boxed)
    bp.registerRuntimeApi("ts_object_isSealed", {true}, true);  // obj -> bool (boxed)
    bp.registerRuntimeApi("ts_object_isExtensible", {true}, true);  // obj -> bool (boxed)
    bp.registerRuntimeApi("ts_object_has_own", {true, false}, false);  // obj, key -> bool
    bp.registerRuntimeApi("ts_object_from_entries", {true}, true);  // entries -> obj
    bp.registerRuntimeApi("ts_object_getOwnPropertyNames", {true}, true);  // obj -> array
    bp.registerRuntimeApi("ts_object_getPrototypeOf", {true}, true);  // obj -> any (null)
    bp.registerRuntimeApi("ts_object_create", {true}, true);  // proto -> obj
    bp.registerRuntimeApi("ts_object_setPrototypeOf", {true, true}, true);  // obj, proto -> obj (stub, just returns obj)
    bp.registerRuntimeApi("ts_object_defineProperty", {true, true, true}, true);  // obj, prop, desc -> obj
    bp.registerRuntimeApi("ts_object_defineProperties", {true, true}, true);  // obj, descs -> obj
    bp.registerRuntimeApi("ts_object_getOwnPropertyDescriptor", {true, true}, true);  // obj, prop -> desc
    bp.registerRuntimeApi("ts_object_getOwnPropertyDescriptors", {true}, true);  // obj -> descs
    bp.registerRuntimeApi("ts_object_groupBy", {true, true}, true);  // ES2024: iterable, callback -> obj
    bp.registerRuntimeApi("ts_map_groupBy", {true, true}, true);  // ES2024: iterable, callback -> Map

    // ========== Value boxing helpers ==========
    bp.registerRuntimeApi("ts_value_make_int", {false}, true);
    bp.registerRuntimeApi("ts_value_make_double", {false}, true);
    bp.registerRuntimeApi("ts_value_make_object", {true}, true);
    bp.registerRuntimeApi("ts_value_get_int", {true}, false);
    bp.registerRuntimeApi("ts_value_get_object", {true}, false);  // Returns raw pointer, NOT boxed

    // ========== Proxy ==========
    bp.registerRuntimeApi("ts_proxy_create", {true, true}, true);  // target, handler -> Proxy
    bp.registerRuntimeApi("ts_proxy_revocable", {true, true}, true);  // target, handler -> {proxy, revoke}
    bp.registerRuntimeApi("ts_proxy_get", {true, true, true}, true);
    bp.registerRuntimeApi("ts_proxy_set", {true, true, true, true}, false);
    bp.registerRuntimeApi("ts_proxy_has", {true, true}, false);
    bp.registerRuntimeApi("ts_proxy_delete", {true, true}, false);
    bp.registerRuntimeApi("ts_proxy_apply", {true, true, true, false}, true);
    bp.registerRuntimeApi("ts_proxy_construct", {true, true, false, true}, true);
    bp.registerRuntimeApi("ts_proxy_ownKeys", {true}, true);
    bp.registerRuntimeApi("ts_is_proxy", {true}, false);

    // ========== Reflect ==========
    bp.registerRuntimeApi("ts_reflect_get", {true, true, true}, true);  // target, prop, receiver -> value
    bp.registerRuntimeApi("ts_reflect_set", {true, true, true, true}, false);  // target, prop, value, receiver -> bool
    bp.registerRuntimeApi("ts_reflect_has", {true, true}, false);  // target, prop -> bool
    bp.registerRuntimeApi("ts_reflect_deleteProperty", {true, true}, false);  // target, prop -> bool
    bp.registerRuntimeApi("ts_reflect_apply", {true, true, true}, true);  // target, thisArg, args -> result
    bp.registerRuntimeApi("ts_reflect_construct", {true, true, true}, true);  // target, args, newTarget -> object
    bp.registerRuntimeApi("ts_reflect_getPrototypeOf", {true}, true);  // target -> prototype
    bp.registerRuntimeApi("ts_reflect_setPrototypeOf", {true, true}, false);  // target, proto -> bool
    bp.registerRuntimeApi("ts_reflect_isExtensible", {true}, false);  // target -> bool
    bp.registerRuntimeApi("ts_reflect_preventExtensions", {true}, false);  // target -> bool
    bp.registerRuntimeApi("ts_reflect_getOwnPropertyDescriptor", {true, true}, true);  // target, prop -> descriptor
    bp.registerRuntimeApi("ts_reflect_defineProperty", {true, true, true}, false);  // target, prop, desc -> bool
    bp.registerRuntimeApi("ts_reflect_ownKeys", {true}, true);  // target -> array
}

bool IRGenerator::tryGenerateBuiltinCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureBuiltinFunctionsRegistered(boxingPolicy);
    
    SPDLOG_DEBUG("tryGenerateBuiltinCall: {}", prop->name);

    if (tryGenerateFSCall(node, prop)) return true;
    if (tryGeneratePathCall(node, prop)) return true;
    if (tryGenerateEventsCall(node, prop)) return true;
    if (tryGeneratePromiseCall(node, prop)) return true;
    // StringDecoder before Stream - it has write/end methods that override generic stream handling
    if (tryGenerateStringDecoderCall(node, prop)) return true;
    if (tryGenerateStreamCall(node, prop)) return true;
    if (tryGenerateBufferCall(node, prop)) return true;
    if (tryGenerateProcessCall(node, prop)) return true;
    if (tryGenerateHTTPCall(node, prop)) return true;  // HTTP before Net - HTTP Server.listen() uses ts_http_server_listen
    if (tryGenerateNetCall(node, prop)) return true;
    if (tryGenerateTextEncodingCall(node, prop)) return true;
    if (tryGenerateURLCall(node, prop)) return true;
    if (tryGenerateURLModuleCall(node, prop)) return true;
    if (tryGenerateUtilCall(node, prop)) return true;
    if (tryGenerateQueryStringCall(node, prop)) return true;
    if (tryGenerateOSCall(node, prop)) return true;
    if (tryGenerateTimersCall(node, prop)) return true;
    if (tryGenerateTimersPromisesCall(node, prop)) return true;
    if (tryGenerateTimersSchedulerCall(node, prop)) return true;
    if (tryGenerateVMCall(node, prop)) return true;
    if (tryGenerateV8Call(node, prop)) return true;
    if (tryGenerateAssertCall(node, prop)) return true;
    if (tryGeneratePerfHooksCall(node, prop)) return true;
    if (tryGenerateAsyncHooksCall(node, prop)) return true;
    if (tryGenerateAsyncLocalStorageCall(node, prop)) return true;
    if (tryGenerateAsyncResourceCall(node, prop)) return true;
    if (tryGenerateAsyncHookMethodCall(node, prop)) return true;
    if (tryGenerateChildProcessCall(node, prop)) return true;
    if (tryGenerateChildProcessMethodCall(node, prop)) return true;
    if (tryGenerateClusterCall(node, prop)) return true;
    if (tryGenerateWorkerCall(node, prop)) return true;
    if (tryGenerateDNSCall(node, prop)) return true;
    if (tryGenerateDgramCall(node, prop)) return true;
    if (tryGenerateInspectorCall(node, prop)) return true;
    if (tryGenerateInspectorSessionCall(node, prop)) return true;

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
            } else if (prop->name == "is") {
                // Object.is(value1, value2) - ES6 SameValue comparison
                // Differs from === in that:
                // - Object.is(NaN, NaN) returns true
                // - Object.is(0, -0) returns false
                if (node->arguments.size() < 2) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* val1 = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* val2 = boxValue(lastValue, node->arguments[1]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_is", ft);
                lastValue = createCall(ft, fn.getCallee(), { val1, val2 });
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
        if (id->name == "Reflect") {
            // Reflect.get(target, propertyKey [, receiver])
            if (prop->name == "get") {
                if (node->arguments.size() < 2) {
                    lastValue = getUndefinedValue();
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* propKey = boxValue(lastValue, node->arguments[1]->inferredType);
                llvm::Value* receiver = target;  // Default receiver is target
                if (node->arguments.size() > 2) {
                    visit(node->arguments[2].get());
                    receiver = boxValue(lastValue, node->arguments[2]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_get", ft);
                lastValue = createCall(ft, fn.getCallee(), { target, propKey, receiver });
                boxedValues.insert(lastValue);
                return true;
            }
            // Reflect.set(target, propertyKey, value [, receiver])
            if (prop->name == "set") {
                if (node->arguments.size() < 3) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* propKey = boxValue(lastValue, node->arguments[1]->inferredType);
                visit(node->arguments[2].get());
                llvm::Value* value = boxValue(lastValue, node->arguments[2]->inferredType);
                llvm::Value* receiver = target;
                if (node->arguments.size() > 3) {
                    visit(node->arguments[3].get());
                    receiver = boxValue(lastValue, node->arguments[3]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_set", ft);
                llvm::Value* result = createCall(ft, fn.getCallee(), { target, propKey, value, receiver });
                lastValue = builder->CreateICmpNE(result, llvm::ConstantInt::get(builder->getInt64Ty(), 0));
                return true;
            }
            // Reflect.has(target, propertyKey)
            if (prop->name == "has") {
                if (node->arguments.size() < 2) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* propKey = boxValue(lastValue, node->arguments[1]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_has", ft);
                llvm::Value* result = createCall(ft, fn.getCallee(), { target, propKey });
                lastValue = builder->CreateICmpNE(result, llvm::ConstantInt::get(builder->getInt64Ty(), 0));
                return true;
            }
            // Reflect.deleteProperty(target, propertyKey)
            if (prop->name == "deleteProperty") {
                if (node->arguments.size() < 2) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* propKey = boxValue(lastValue, node->arguments[1]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_deleteProperty", ft);
                llvm::Value* result = createCall(ft, fn.getCallee(), { target, propKey });
                lastValue = builder->CreateICmpNE(result, llvm::ConstantInt::get(builder->getInt64Ty(), 0));
                return true;
            }
            // Reflect.apply(target, thisArgument, argumentsList)
            if (prop->name == "apply") {
                if (node->arguments.size() < 3) {
                    lastValue = getUndefinedValue();
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* thisArg = boxValue(lastValue, node->arguments[1]->inferredType);
                visit(node->arguments[2].get());
                llvm::Value* args = boxValue(lastValue, node->arguments[2]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_apply", ft);
                lastValue = createCall(ft, fn.getCallee(), { target, thisArg, args });
                boxedValues.insert(lastValue);
                return true;
            }
            // Reflect.construct(target, argumentsList [, newTarget])
            if (prop->name == "construct") {
                if (node->arguments.size() < 2) {
                    lastValue = getUndefinedValue();
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* args = boxValue(lastValue, node->arguments[1]->inferredType);
                llvm::Value* newTarget = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (node->arguments.size() > 2) {
                    visit(node->arguments[2].get());
                    newTarget = boxValue(lastValue, node->arguments[2]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_construct", ft);
                lastValue = createCall(ft, fn.getCallee(), { target, args, newTarget });
                boxedValues.insert(lastValue);
                return true;
            }
            // Reflect.getPrototypeOf(target)
            if (prop->name == "getPrototypeOf") {
                if (node->arguments.empty()) {
                    lastValue = getUndefinedValue();
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_getPrototypeOf", ft);
                lastValue = createCall(ft, fn.getCallee(), { target });
                boxedValues.insert(lastValue);
                return true;
            }
            // Reflect.setPrototypeOf(target, prototype)
            if (prop->name == "setPrototypeOf") {
                if (node->arguments.size() < 2) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* proto = boxValue(lastValue, node->arguments[1]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_setPrototypeOf", ft);
                llvm::Value* result = createCall(ft, fn.getCallee(), { target, proto });
                lastValue = builder->CreateICmpNE(result, llvm::ConstantInt::get(builder->getInt64Ty(), 0));
                return true;
            }
            // Reflect.isExtensible(target)
            if (prop->name == "isExtensible") {
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_isExtensible", ft);
                llvm::Value* result = createCall(ft, fn.getCallee(), { target });
                lastValue = builder->CreateICmpNE(result, llvm::ConstantInt::get(builder->getInt64Ty(), 0));
                return true;
            }
            // Reflect.preventExtensions(target)
            if (prop->name == "preventExtensions") {
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_preventExtensions", ft);
                llvm::Value* result = createCall(ft, fn.getCallee(), { target });
                lastValue = builder->CreateICmpNE(result, llvm::ConstantInt::get(builder->getInt64Ty(), 0));
                return true;
            }
            // Reflect.getOwnPropertyDescriptor(target, propertyKey)
            if (prop->name == "getOwnPropertyDescriptor") {
                if (node->arguments.size() < 2) {
                    lastValue = getUndefinedValue();
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* propKey = boxValue(lastValue, node->arguments[1]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_getOwnPropertyDescriptor", ft);
                lastValue = createCall(ft, fn.getCallee(), { target, propKey });
                boxedValues.insert(lastValue);
                return true;
            }
            // Reflect.defineProperty(target, propertyKey, attributes)
            if (prop->name == "defineProperty") {
                if (node->arguments.size() < 3) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* propKey = boxValue(lastValue, node->arguments[1]->inferredType);
                visit(node->arguments[2].get());
                llvm::Value* attrs = boxValue(lastValue, node->arguments[2]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_defineProperty", ft);
                llvm::Value* result = createCall(ft, fn.getCallee(), { target, propKey, attrs });
                lastValue = builder->CreateICmpNE(result, llvm::ConstantInt::get(builder->getInt64Ty(), 0));
                return true;
            }
            // Reflect.ownKeys(target)
            if (prop->name == "ownKeys") {
                if (node->arguments.empty()) {
                    llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create", createFt);
                    lastValue = createCall(createFt, createFn.getCallee(), {});
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_reflect_ownKeys", ft);
                lastValue = createCall(ft, fn.getCallee(), { target });
                boxedValues.insert(lastValue);
                return true;
            }
        }
        if (id->name == "Proxy") {
            // Proxy.revocable(target, handler)
            if (prop->name == "revocable") {
                if (node->arguments.size() < 2) {
                    lastValue = getUndefinedValue();
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* handler = boxValue(lastValue, node->arguments[1]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_proxy_revocable", ft);
                lastValue = createCall(ft, fn.getCallee(), { target, handler });
                boxedValues.insert(lastValue);
                return true;
            }
        }
        if (id->name == "Number") {
            if (prop->name == "isFinite") {
                // Number.isFinite(value) - returns true if value is finite (not NaN, not Infinity)
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* val = castValue(lastValue, builder->getDoubleTy());

                // A number is finite if it's not NaN and not infinite
                // Check: val == val (false for NaN) && val != Infinity && val != -Infinity
                llvm::Value* isNotNaN = builder->CreateFCmpOEQ(val, val);
                llvm::Value* posInf = llvm::ConstantFP::getInfinity(builder->getDoubleTy(), false);
                llvm::Value* negInf = llvm::ConstantFP::getInfinity(builder->getDoubleTy(), true);
                llvm::Value* notPosInf = builder->CreateFCmpONE(val, posInf);
                llvm::Value* notNegInf = builder->CreateFCmpONE(val, negInf);

                lastValue = builder->CreateAnd(isNotNaN, builder->CreateAnd(notPosInf, notNegInf));
                return true;
            } else if (prop->name == "isNaN") {
                // Number.isNaN(value) - returns true only if value is NaN
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* val = castValue(lastValue, builder->getDoubleTy());

                // NaN is the only value that is not equal to itself
                lastValue = builder->CreateFCmpUNO(val, val);
                return true;
            } else if (prop->name == "isInteger") {
                // Number.isInteger(value) - returns true if value is an integer
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* val = castValue(lastValue, builder->getDoubleTy());

                // isInteger: value is finite and floor(value) == value
                llvm::Value* isNotNaN = builder->CreateFCmpOEQ(val, val);
                llvm::Value* posInf = llvm::ConstantFP::getInfinity(builder->getDoubleTy(), false);
                llvm::Value* negInf = llvm::ConstantFP::getInfinity(builder->getDoubleTy(), true);
                llvm::Value* notPosInf = builder->CreateFCmpONE(val, posInf);
                llvm::Value* notNegInf = builder->CreateFCmpONE(val, negInf);
                llvm::Value* isFinite = builder->CreateAnd(isNotNaN, builder->CreateAnd(notPosInf, notNegInf));

                // Check floor(val) == val using intrinsic
                llvm::Function* floorFn = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::floor, {builder->getDoubleTy()});
                llvm::Value* floorVal = builder->CreateCall(floorFn, {val});
                llvm::Value* isWholeNumber = builder->CreateFCmpOEQ(val, floorVal);

                lastValue = builder->CreateAnd(isFinite, isWholeNumber);
                return true;
            } else if (prop->name == "isSafeInteger") {
                // Number.isSafeInteger(value) - returns true if value is a safe integer (-2^53+1 to 2^53-1)
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantInt::getFalse(*context);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* val = castValue(lastValue, builder->getDoubleTy());

                // First check if it's an integer
                llvm::Value* isNotNaN = builder->CreateFCmpOEQ(val, val);
                llvm::Value* posInf = llvm::ConstantFP::getInfinity(builder->getDoubleTy(), false);
                llvm::Value* negInf = llvm::ConstantFP::getInfinity(builder->getDoubleTy(), true);
                llvm::Value* notPosInf = builder->CreateFCmpONE(val, posInf);
                llvm::Value* notNegInf = builder->CreateFCmpONE(val, negInf);
                llvm::Value* isFinite = builder->CreateAnd(isNotNaN, builder->CreateAnd(notPosInf, notNegInf));

                llvm::Function* floorFn = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::floor, {builder->getDoubleTy()});
                llvm::Value* floorVal = builder->CreateCall(floorFn, {val});
                llvm::Value* isWholeNumber = builder->CreateFCmpOEQ(val, floorVal);
                llvm::Value* isInteger = builder->CreateAnd(isFinite, isWholeNumber);

                // Check range: -9007199254740991 <= val <= 9007199254740991 (2^53 - 1)
                llvm::Value* maxSafe = llvm::ConstantFP::get(builder->getDoubleTy(), 9007199254740991.0);
                llvm::Value* minSafe = llvm::ConstantFP::get(builder->getDoubleTy(), -9007199254740991.0);
                llvm::Value* leMax = builder->CreateFCmpOLE(val, maxSafe);
                llvm::Value* geMin = builder->CreateFCmpOGE(val, minSafe);
                llvm::Value* inRange = builder->CreateAnd(leMax, geMin);

                lastValue = builder->CreateAnd(isInteger, inRange);
                return true;
            } else if (prop->name == "MAX_SAFE_INTEGER") {
                lastValue = llvm::ConstantInt::get(builder->getInt64Ty(), 9007199254740991LL);
                return true;
            } else if (prop->name == "MIN_SAFE_INTEGER") {
                lastValue = llvm::ConstantInt::get(builder->getInt64Ty(), -9007199254740991LL);
                return true;
            } else if (prop->name == "POSITIVE_INFINITY") {
                lastValue = llvm::ConstantFP::getInfinity(builder->getDoubleTy(), false);
                return true;
            } else if (prop->name == "NEGATIVE_INFINITY") {
                lastValue = llvm::ConstantFP::getInfinity(builder->getDoubleTy(), true);
                return true;
            } else if (prop->name == "NaN") {
                lastValue = llvm::ConstantFP::getNaN(builder->getDoubleTy());
                return true;
            } else if (prop->name == "EPSILON") {
                lastValue = llvm::ConstantFP::get(builder->getDoubleTy(), 2.220446049250313e-16);
                return true;
            } else if (prop->name == "MAX_VALUE") {
                lastValue = llvm::ConstantFP::get(builder->getDoubleTy(), 1.7976931348623157e+308);
                return true;
            } else if (prop->name == "MIN_VALUE") {
                lastValue = llvm::ConstantFP::get(builder->getDoubleTy(), 5e-324);
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

            // Handle integer getters (getInt8, getUint8, getInt16, getUint16, getInt32, getUint32)
            if (prop->name == "getInt8" || prop->name == "getUint8" ||
                prop->name == "getInt16" || prop->name == "getUint16" ||
                prop->name == "getInt32" || prop->name == "getUint32") {

                visit(node->arguments[0].get());
                llvm::Value* offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));

                llvm::Value* littleEndian = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
                if (node->arguments.size() > 1) {
                    visit(node->arguments[1].get());
                    littleEndian = castValue(lastValue, llvm::Type::getInt1Ty(*context));
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                    { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt1Ty(*context) }, false);
                std::string funcName = "DataView_" + prop->name;
                llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);

                llvm::Value* contextVal = currentAsyncContext;
                if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

                lastValue = createCall(ft, fn.getCallee(), { contextVal, dv, offset, littleEndian });
                if (!node->inferredType || node->inferredType->kind != TypeKind::Int) {
                    lastValue = castValue(lastValue, llvm::Type::getDoubleTy(*context));
                }
                return true;
            }

            // Handle float getters (getFloat32, getFloat64)
            if (prop->name == "getFloat32" || prop->name == "getFloat64") {
                visit(node->arguments[0].get());
                llvm::Value* offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));

                llvm::Value* littleEndian = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
                if (node->arguments.size() > 1) {
                    visit(node->arguments[1].get());
                    littleEndian = castValue(lastValue, llvm::Type::getInt1Ty(*context));
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                    { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt1Ty(*context) }, false);
                std::string funcName = "DataView_" + prop->name;
                llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);

                llvm::Value* contextVal = currentAsyncContext;
                if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

                lastValue = createCall(ft, fn.getCallee(), { contextVal, dv, offset, littleEndian });
                return true;
            }

            // Handle integer setters (setInt8, setUint8, setInt16, setUint16, setInt32, setUint32)
            if (prop->name == "setInt8" || prop->name == "setUint8" ||
                prop->name == "setInt16" || prop->name == "setUint16" ||
                prop->name == "setInt32" || prop->name == "setUint32") {

                visit(node->arguments[0].get());
                llvm::Value* offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));

                visit(node->arguments[1].get());
                llvm::Value* val64 = castValue(lastValue, llvm::Type::getInt64Ty(*context));

                llvm::Value* littleEndian = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
                if (node->arguments.size() > 2) {
                    visit(node->arguments[2].get());
                    littleEndian = castValue(lastValue, llvm::Type::getInt1Ty(*context));
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt1Ty(*context) }, false);
                std::string funcName = "DataView_" + prop->name;
                llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);

                llvm::Value* contextVal = currentAsyncContext;
                if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

                createCall(ft, fn.getCallee(), { contextVal, dv, offset, val64, littleEndian });
                lastValue = nullptr;
                return true;
            }

            // Handle float setters (setFloat32, setFloat64)
            if (prop->name == "setFloat32" || prop->name == "setFloat64") {
                visit(node->arguments[0].get());
                llvm::Value* offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));

                visit(node->arguments[1].get());
                llvm::Value* val = castValue(lastValue, llvm::Type::getDoubleTy(*context));

                llvm::Value* littleEndian = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
                if (node->arguments.size() > 2) {
                    visit(node->arguments[2].get());
                    littleEndian = castValue(lastValue, llvm::Type::getInt1Ty(*context));
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getDoubleTy(*context), llvm::Type::getInt1Ty(*context) }, false);
                std::string funcName = "DataView_" + prop->name;
                llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);

                llvm::Value* contextVal = currentAsyncContext;
                if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

                createCall(ft, fn.getCallee(), { contextVal, dv, offset, val, littleEndian });
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
            
            if (methodName == "toISOString" || methodName == "toJSON" || methodName == "toString" || methodName == "toDateString") {
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
        } else if (classType->name == "Hash") {
            // crypto.createHash() returns Hash object
            visit(prop->expression.get());
            llvm::Value* hashObj = lastValue;

            if (prop->name == "update") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_hash_update", ft);
                lastValue = createCall(ft, fn.getCallee(), { hashObj, data });
                return true;
            } else if (prop->name == "digest") {
                llvm::Value* encoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    encoding = lastValue;
                    if (encoding->getType()->isIntegerTy(64)) {
                        encoding = builder->CreateIntToPtr(encoding, builder->getPtrTy());
                    }
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_hash_digest", ft);
                lastValue = createCall(ft, fn.getCallee(), { hashObj, encoding });
                return true;
            } else if (prop->name == "copy") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_hash_copy", ft);
                lastValue = createCall(ft, fn.getCallee(), { hashObj });
                return true;
            }
        } else if (classType->name == "Hmac") {
            // crypto.createHmac() returns Hmac object
            visit(prop->expression.get());
            llvm::Value* hmacObj = lastValue;

            if (prop->name == "update") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_hmac_update", ft);
                lastValue = createCall(ft, fn.getCallee(), { hmacObj, data });
                return true;
            } else if (prop->name == "digest") {
                llvm::Value* encoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    encoding = lastValue;
                    if (encoding->getType()->isIntegerTy(64)) {
                        encoding = builder->CreateIntToPtr(encoding, builder->getPtrTy());
                    }
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_hmac_digest", ft);
                lastValue = createCall(ft, fn.getCallee(), { hmacObj, encoding });
                return true;
            }
        } else if (classType->name == "Cipher") {
            // crypto.createCipheriv() returns Cipher object
            visit(prop->expression.get());
            llvm::Value* cipherObj = lastValue;

            if (prop->name == "update") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_cipher_update", ft);
                lastValue = createCall(ft, fn.getCallee(), { cipherObj, data });
                return true;
            } else if (prop->name == "final") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_cipher_final", ft);
                lastValue = createCall(ft, fn.getCallee(), { cipherObj });
                return true;
            } else if (prop->name == "getAuthTag") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_cipher_getAuthTag", ft);
                lastValue = createCall(ft, fn.getCallee(), { cipherObj });
                return true;
            } else if (prop->name == "setAAD") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_cipher_setAAD", ft);
                lastValue = createCall(ft, fn.getCallee(), { cipherObj, data });
                return true;
            }
        } else if (classType->name == "Decipher") {
            // crypto.createDecipheriv() returns Decipher object
            visit(prop->expression.get());
            llvm::Value* decipherObj = lastValue;

            if (prop->name == "update") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_decipher_update", ft);
                lastValue = createCall(ft, fn.getCallee(), { decipherObj, data });
                return true;
            } else if (prop->name == "final") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_decipher_final", ft);
                lastValue = createCall(ft, fn.getCallee(), { decipherObj });
                return true;
            } else if (prop->name == "setAuthTag") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* tag = boxValue(lastValue, node->arguments[0]->inferredType);
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_decipher_setAuthTag", ft);
                lastValue = createCall(ft, fn.getCallee(), { decipherObj, tag });
                return true;
            } else if (prop->name == "setAAD") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_decipher_setAAD", ft);
                lastValue = createCall(ft, fn.getCallee(), { decipherObj, data });
                return true;
            }
        } else if (classType->name == "Sign") {
            // crypto.createSign() returns Sign object
            visit(prop->expression.get());
            llvm::Value* signObj = lastValue;

            if (prop->name == "update") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_sign_update", ft);
                lastValue = createCall(ft, fn.getCallee(), { signObj, data });
                return true;
            } else if (prop->name == "sign") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* privateKey = boxValue(lastValue, node->arguments[0]->inferredType);
                llvm::Value* outputEncoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (node->arguments.size() > 1) {
                    visit(node->arguments[1].get());
                    outputEncoding = lastValue;
                    if (outputEncoding->getType()->isIntegerTy(64)) {
                        outputEncoding = builder->CreateIntToPtr(outputEncoding, builder->getPtrTy());
                    }
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_sign_sign", ft);
                lastValue = createCall(ft, fn.getCallee(), { signObj, privateKey, outputEncoding });
                return true;
            }
        } else if (classType->name == "Verify") {
            // crypto.createVerify() returns Verify object
            visit(prop->expression.get());
            llvm::Value* verifyObj = lastValue;

            if (prop->name == "update") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_verify_update", ft);
                lastValue = createCall(ft, fn.getCallee(), { verifyObj, data });
                return true;
            } else if (prop->name == "verify") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* publicKey = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* signature = boxValue(lastValue, node->arguments[1]->inferredType);
                llvm::Value* signatureEncoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (node->arguments.size() > 2) {
                    visit(node->arguments[2].get());
                    signatureEncoding = lastValue;
                    if (signatureEncoding->getType()->isIntegerTy(64)) {
                        signatureEncoding = builder->CreateIntToPtr(signatureEncoding, builder->getPtrTy());
                    }
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_verify_verify", ft);
                lastValue = createCall(ft, fn.getCallee(), { verifyObj, publicKey, signature, signatureEncoding });
                return true;
            }
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

    if (prop->name == "log" || prop->name == "error" || prop->name == "warn" || prop->name == "info" || prop->name == "debug" || prop->name == "time" || prop->name == "timeEnd" || prop->name == "timeLog" || prop->name == "trace" || prop->name == "assert" || prop->name == "dir" || prop->name == "dirxml" || prop->name == "table" || prop->name == "count" || prop->name == "countReset" || prop->name == "group" || prop->name == "groupCollapsed" || prop->name == "groupEnd" || prop->name == "clear") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "console") {
                // console.assert(condition, ...messages) - only logs if condition is falsy
                if (prop->name == "assert") {
                    if (node->arguments.empty()) return true;

                    // Evaluate condition
                    visit(node->arguments[0].get());
                    llvm::Value* condition = lastValue;

                    // Convert to i1 if needed
                    if (!condition->getType()->isIntegerTy(1)) {
                        if (condition->getType()->isIntegerTy(64)) {
                            condition = builder->CreateICmpNE(condition, llvm::ConstantInt::get(builder->getInt64Ty(), 0));
                        } else if (condition->getType()->isDoubleTy()) {
                            condition = builder->CreateFCmpONE(condition, llvm::ConstantFP::get(builder->getDoubleTy(), 0.0));
                        } else if (condition->getType()->isPointerTy()) {
                            condition = builder->CreateICmpNE(condition, llvm::ConstantPointerNull::get(builder->getPtrTy()));
                        }
                    }

                    // Create basic blocks for assert failure path
                    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
                    llvm::BasicBlock* failBB = llvm::BasicBlock::Create(*context, "assert.fail", currentFunc);
                    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "assert.end", currentFunc);

                    // Branch: if condition is true, skip; if false, print assertion failure
                    builder->CreateCondBr(condition, endBB, failBB);

                    // Fail block - print "Assertion failed: " and any messages
                    builder->SetInsertPoint(failBB);

                    // Print "Assertion failed: "
                    llvm::FunctionType* logFt = llvm::FunctionType::get(
                        llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee logFn = module->getOrInsertFunction("ts_console_error", logFt);
                    llvm::Value* assertMsgPtr = builder->CreateGlobalStringPtr("Assertion failed:");
                    llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
                    llvm::Value* assertMsg = createCall(createStrFt, createStrFn.getCallee(), { assertMsgPtr });
                    createCall(logFt, logFn.getCallee(), { assertMsg });

                    // Print remaining arguments as messages
                    for (size_t i = 1; i < node->arguments.size(); ++i) {
                        visit(node->arguments[i].get());
                        llvm::Value* arg = lastValue;
                        if (!arg) continue;

                        std::string funcName = "ts_console_error";
                        llvm::Type* paramType = builder->getPtrTy();

                        if (arg->getType()->isIntegerTy(64)) {
                            funcName = "ts_console_error_int";
                            paramType = builder->getInt64Ty();
                        } else if (arg->getType()->isDoubleTy()) {
                            funcName = "ts_console_error_double";
                            paramType = builder->getDoubleTy();
                        } else if (arg->getType()->isIntegerTy(1)) {
                            funcName = "ts_console_error_bool";
                            paramType = builder->getInt1Ty();
                        } else if (arg->getType()->isPointerTy()) {
                            if (boxedValues.count(arg)) {
                                funcName = "ts_console_error_value";
                            } else if (node->arguments[i]->inferredType && node->arguments[i]->inferredType->kind == TypeKind::String) {
                                funcName = "ts_console_error";
                            } else {
                                funcName = "ts_console_error_value";
                                arg = boxValue(arg, node->arguments[i]->inferredType);
                            }
                        }

                        llvm::FunctionType* ft = llvm::FunctionType::get(
                            llvm::Type::getVoidTy(*context), { paramType }, false);
                        llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
                        createCall(ft, fn.getCallee(), { arg });
                    }

                    builder->CreateBr(endBB);

                    // End block
                    builder->SetInsertPoint(endBB);
                    lastValue = nullptr;
                    return true;
                }

                if (prop->name == "time" || prop->name == "timeEnd" || prop->name == "timeLog") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* label = unboxValue(lastValue, node->arguments[0]->inferredType);

                    std::string funcName;
                    if (prop->name == "time") funcName = "ts_console_time";
                    else if (prop->name == "timeEnd") funcName = "ts_console_time_end";
                    else funcName = "ts_console_time_log";

                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
                    createCall(ft, fn.getCallee(), { label });
                    lastValue = nullptr;
                    return true;
                }

                if (prop->name == "count" || prop->name == "countReset") {
                    llvm::Value* label;
                    if (node->arguments.empty()) {
                        // Default label
                        llvm::Value* defaultPtr = builder->CreateGlobalStringPtr("default");
                        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                        llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
                        label = createCall(createStrFt, createStrFn.getCallee(), { defaultPtr });
                    } else {
                        visit(node->arguments[0].get());
                        label = unboxValue(lastValue, node->arguments[0]->inferredType);
                    }

                    std::string funcName = (prop->name == "count") ? "ts_console_count" : "ts_console_count_reset";
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
                    createCall(ft, fn.getCallee(), { label });
                    lastValue = nullptr;
                    return true;
                }

                if (prop->name == "dir" || prop->name == "dirxml") {
                    // dirxml is an alias for dir in Node.js
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* val = boxValue(lastValue, node->arguments[0]->inferredType);

                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_console_dir", ft);
                    createCall(ft, fn.getCallee(), { val });
                    lastValue = nullptr;
                    return true;
                }

                if (prop->name == "table") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);

                    llvm::Value* properties = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    if (node->arguments.size() > 1) {
                        visit(node->arguments[1].get());
                        properties = boxValue(lastValue, node->arguments[1]->inferredType);
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                        { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_console_table", ft);
                    createCall(ft, fn.getCallee(), { data, properties });
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

                if (prop->name == "group" || prop->name == "groupCollapsed") {
                    llvm::Value* label;
                    if (node->arguments.empty()) {
                        // No label - pass nullptr
                        label = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    } else {
                        visit(node->arguments[0].get());
                        // String arguments are already TsString*, don't unbox
                        label = lastValue;
                    }

                    std::string funcName = (prop->name == "group") ? "ts_console_group" : "ts_console_group_collapsed";
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
                    createCall(ft, fn.getCallee(), { label });
                    lastValue = nullptr;
                    return true;
                }

                if (prop->name == "groupEnd") {
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {}, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_console_group_end", ft);
                    createCall(ft, fn.getCallee(), {});
                    lastValue = nullptr;
                    return true;
                }

                if (prop->name == "clear") {
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {}, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_console_clear", ft);
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
            } else if (prop->name == "imul") {
                if (node->arguments.size() < 2) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg1 = lastValue;
                visit(node->arguments[1].get());
                llvm::Value* arg2 = lastValue;
                // Convert to i32
                if (arg1->getType()->isDoubleTy()) {
                    arg1 = builder->CreateFPToSI(arg1, llvm::Type::getInt32Ty(*context));
                } else if (arg1->getType()->isIntegerTy(64)) {
                    arg1 = builder->CreateTrunc(arg1, llvm::Type::getInt32Ty(*context));
                }
                if (arg2->getType()->isDoubleTy()) {
                    arg2 = builder->CreateFPToSI(arg2, llvm::Type::getInt32Ty(*context));
                } else if (arg2->getType()->isIntegerTy(64)) {
                    arg2 = builder->CreateTrunc(arg2, llvm::Type::getInt32Ty(*context));
                }
                llvm::FunctionType* imulFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context),
                        { llvm::Type::getInt32Ty(*context), llvm::Type::getInt32Ty(*context) }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_math_imul", imulFt);
                lastValue = createCall(imulFt, fn.getCallee(), { arg1, arg2 });
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
            } else if (prop->name == "withResolvers") {
                // ES2024 Promise.withResolvers() - returns { promise, resolve, reject }
                llvm::FunctionType* withResolversFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee withResolversFn = getRuntimeFunction("ts_promise_withResolvers", withResolversFt);
                lastValue = createCall(withResolversFt, withResolversFn.getCallee(), {});
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
            } else if (prop->name == "getOwnPropertyNames") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }

                llvm::FunctionType* gopnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee gopnFn = getRuntimeFunction("ts_object_getOwnPropertyNames", gopnFt);
                lastValue = createCall(gopnFt, gopnFn.getCallee(), { arg });
                return true;
            } else if (prop->name == "getPrototypeOf") {
                if (node->arguments.empty()) return true;
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }

                llvm::FunctionType* gpoFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee gpoFn = getRuntimeFunction("ts_object_getPrototypeOf", gpoFt);
                lastValue = createCall(gpoFt, gpoFn.getCallee(), { arg });
                return true;
            } else if (prop->name == "create") {
                // Handle Object.create(proto)
                llvm::Value* arg = nullptr;
                if (node->arguments.empty()) {
                    // Object.create() with no args - use null
                    arg = llvm::ConstantPointerNull::get(builder->getPtrTy());
                } else {
                    visit(node->arguments[0].get());
                    arg = lastValue;
                    if (!arg->getType()->isPointerTy()) {
                        arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                    }
                }

                llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee createFn = getRuntimeFunction("ts_object_create", createFt);
                lastValue = createCall(createFt, createFn.getCallee(), { arg });
                return true;
            } else if (prop->name == "setPrototypeOf") {
                // Handle Object.setPrototypeOf(obj, proto)
                // Note: ts-aot doesn't have full prototype chain, so this just returns the object
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* obj = lastValue;
                if (!obj->getType()->isPointerTy()) {
                    obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
                }
                // We ignore the proto argument since we don't have prototype chains
                // Just return the object for compatibility
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_setPrototypeOf", ft);

                llvm::Value* proto = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (node->arguments.size() > 1) {
                    visit(node->arguments[1].get());
                    proto = lastValue;
                    if (!proto->getType()->isPointerTy()) {
                        proto = builder->CreateIntToPtr(proto, builder->getPtrTy());
                    }
                }
                lastValue = createCall(ft, fn.getCallee(), { obj, proto });
                return true;
            } else if (prop->name == "freeze") {
                // Handle Object.freeze(obj)
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_freeze", ft);
                lastValue = createCall(ft, fn.getCallee(), { arg });
                return true;
            } else if (prop->name == "seal") {
                // Handle Object.seal(obj)
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_seal", ft);
                lastValue = createCall(ft, fn.getCallee(), { arg });
                return true;
            } else if (prop->name == "preventExtensions") {
                // Handle Object.preventExtensions(obj)
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_preventExtensions", ft);
                lastValue = createCall(ft, fn.getCallee(), { arg });
                return true;
            } else if (prop->name == "isFrozen") {
                // Handle Object.isFrozen(obj)
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1);  // true for undefined
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_isFrozen", ft);
                lastValue = createCall(ft, fn.getCallee(), { arg });
                return true;
            } else if (prop->name == "isSealed") {
                // Handle Object.isSealed(obj)
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1);  // true for undefined
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_isSealed", ft);
                lastValue = createCall(ft, fn.getCallee(), { arg });
                return true;
            } else if (prop->name == "isExtensible") {
                // Handle Object.isExtensible(obj)
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);  // false for undefined
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (!arg->getType()->isPointerTy()) {
                    arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_isExtensible", ft);
                lastValue = createCall(ft, fn.getCallee(), { arg });
                return true;
            } else if (prop->name == "defineProperty") {
                // Handle Object.defineProperty(obj, prop, descriptor)
                if (node->arguments.size() < 3) {
                    lastValue = node->arguments.empty()
                        ? llvm::ConstantPointerNull::get(builder->getPtrTy())
                        : (visit(node->arguments[0].get()), lastValue);
                    return true;
                }
                // obj argument - box it
                visit(node->arguments[0].get());
                llvm::Value* objArg = lastValue;
                if (!boxedValues.count(objArg) && node->arguments[0]->inferredType) {
                    objArg = boxValue(objArg, node->arguments[0]->inferredType);
                }
                if (!objArg->getType()->isPointerTy()) {
                    objArg = builder->CreateIntToPtr(objArg, builder->getPtrTy());
                }
                // prop argument - box it (string)
                visit(node->arguments[1].get());
                llvm::Value* propArg = lastValue;
                if (!boxedValues.count(propArg) && node->arguments[1]->inferredType) {
                    propArg = boxValue(propArg, node->arguments[1]->inferredType);
                }
                if (!propArg->getType()->isPointerTy()) {
                    propArg = builder->CreateIntToPtr(propArg, builder->getPtrTy());
                }
                // descriptor argument - box it
                visit(node->arguments[2].get());
                llvm::Value* descArg = lastValue;
                if (!boxedValues.count(descArg) && node->arguments[2]->inferredType) {
                    descArg = boxValue(descArg, node->arguments[2]->inferredType);
                }
                if (!descArg->getType()->isPointerTy()) {
                    descArg = builder->CreateIntToPtr(descArg, builder->getPtrTy());
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_defineProperty", ft);
                lastValue = createCall(ft, fn.getCallee(), { objArg, propArg, descArg });
                return true;
            } else if (prop->name == "defineProperties") {
                // Handle Object.defineProperties(obj, descriptors)
                if (node->arguments.size() < 2) {
                    lastValue = node->arguments.empty()
                        ? llvm::ConstantPointerNull::get(builder->getPtrTy())
                        : (visit(node->arguments[0].get()), lastValue);
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* objArg = lastValue;
                if (!objArg->getType()->isPointerTy()) {
                    objArg = builder->CreateIntToPtr(objArg, builder->getPtrTy());
                }
                visit(node->arguments[1].get());
                llvm::Value* descsArg = lastValue;
                if (!descsArg->getType()->isPointerTy()) {
                    descsArg = builder->CreateIntToPtr(descsArg, builder->getPtrTy());
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_defineProperties", ft);
                lastValue = createCall(ft, fn.getCallee(), { objArg, descsArg });
                return true;
            } else if (prop->name == "getOwnPropertyDescriptor") {
                // Handle Object.getOwnPropertyDescriptor(obj, prop)
                if (node->arguments.size() < 2) {
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                }
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
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_getOwnPropertyDescriptor", ft);
                lastValue = createCall(ft, fn.getCallee(), { objArg, propArg });
                return true;
            } else if (prop->name == "getOwnPropertyDescriptors") {
                // Handle Object.getOwnPropertyDescriptors(obj)
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                }
                visit(node->arguments[0].get());
                llvm::Value* objArg = lastValue;
                if (!objArg->getType()->isPointerTy()) {
                    objArg = builder->CreateIntToPtr(objArg, builder->getPtrTy());
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_getOwnPropertyDescriptors", ft);
                lastValue = createCall(ft, fn.getCallee(), { objArg });
                return true;
            } else if (prop->name == "groupBy") {
                // ES2024 Object.groupBy(iterable, callbackFn)
                if (node->arguments.size() < 2) {
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                }
                // Get iterable argument
                visit(node->arguments[0].get());
                llvm::Value* iterableArg = lastValue;
                if (!iterableArg->getType()->isPointerTy()) {
                    iterableArg = builder->CreateIntToPtr(iterableArg, builder->getPtrTy());
                }
                if (node->arguments[0]->inferredType) {
                    iterableArg = boxValue(iterableArg, node->arguments[0]->inferredType);
                }
                // Get callback argument
                visit(node->arguments[1].get());
                llvm::Value* callbackArg = lastValue;
                if (!callbackArg->getType()->isPointerTy()) {
                    callbackArg = builder->CreateIntToPtr(callbackArg, builder->getPtrTy());
                }
                if (node->arguments[1]->inferredType) {
                    callbackArg = boxValue(callbackArg, node->arguments[1]->inferredType);
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_object_groupBy", ft);
                lastValue = createCall(ft, fn.getCallee(), { iterableArg, callbackArg });
                return true;
            }
        } else if (obj->name == "Map") {
            if (prop->name == "groupBy") {
                // ES2024 Map.groupBy(iterable, callbackFn)
                if (node->arguments.size() < 2) {
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                }
                // Get iterable argument
                visit(node->arguments[0].get());
                llvm::Value* iterableArg = lastValue;
                if (!iterableArg->getType()->isPointerTy()) {
                    iterableArg = builder->CreateIntToPtr(iterableArg, builder->getPtrTy());
                }
                if (node->arguments[0]->inferredType) {
                    iterableArg = boxValue(iterableArg, node->arguments[0]->inferredType);
                }
                // Get callback argument
                visit(node->arguments[1].get());
                llvm::Value* callbackArg = lastValue;
                if (!callbackArg->getType()->isPointerTy()) {
                    callbackArg = builder->CreateIntToPtr(callbackArg, builder->getPtrTy());
                }
                if (node->arguments[1]->inferredType) {
                    callbackArg = boxValue(callbackArg, node->arguments[1]->inferredType);
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_map_groupBy", ft);
                lastValue = createCall(ft, fn.getCallee(), { iterableArg, callbackArg });
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
            } else if (prop->name == "from") {
                // Array.from(arrayLike, mapFn?, thisArg?)
                llvm::Value* arrayLike = llvm::ConstantPointerNull::get(builder->getPtrTy());
                llvm::Value* mapFn = llvm::ConstantPointerNull::get(builder->getPtrTy());
                llvm::Value* thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());

                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    arrayLike = lastValue;
                    if (!arrayLike->getType()->isPointerTy()) {
                        arrayLike = builder->CreateIntToPtr(arrayLike, builder->getPtrTy());
                    }
                    // Box the value if needed
                    if (node->arguments[0]->inferredType) {
                        arrayLike = boxValue(arrayLike, node->arguments[0]->inferredType);
                    }
                }

                if (node->arguments.size() > 1) {
                    visit(node->arguments[1].get());
                    mapFn = lastValue;
                    if (!mapFn->getType()->isPointerTy()) {
                        mapFn = builder->CreateIntToPtr(mapFn, builder->getPtrTy());
                    }
                }

                if (node->arguments.size() > 2) {
                    visit(node->arguments[2].get());
                    thisArg = lastValue;
                    if (!thisArg->getType()->isPointerTy()) {
                        thisArg = builder->CreateIntToPtr(thisArg, builder->getPtrTy());
                    }
                }

                llvm::FunctionType* fromFt = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fromFn = getRuntimeFunction("ts_array_from", fromFt);
                lastValue = createCall(fromFt, fromFn.getCallee(), { arrayLike, mapFn, thisArg });
                return true;
            } else if (prop->name == "of") {
                // Array.of(...items) - create array from arguments
                // First create an empty array
                llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee createFn = getRuntimeFunction("ts_array_create", createFt);
                llvm::Value* arr = createCall(createFt, createFn.getCallee(), {});

                // Push each argument onto the array
                llvm::FunctionType* pushFt = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);

                for (size_t i = 0; i < node->arguments.size(); i++) {
                    visit(node->arguments[i].get());
                    llvm::Value* val = lastValue;

                    // Box the value
                    if (node->arguments[i]->inferredType) {
                        val = boxValue(val, node->arguments[i]->inferredType);
                    } else if (!val->getType()->isPointerTy()) {
                        val = builder->CreateIntToPtr(val, builder->getPtrTy());
                    }

                    createCall(pushFt, pushFn.getCallee(), { arr, val });
                }

                lastValue = arr;
                return true;
            }
        } else if (obj->name == "String") {
            if (prop->name == "fromCodePoint") {
                // String.fromCodePoint(...codePoints) - create string from Unicode code points
                // First create an array to hold the code points
                llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee createFn = getRuntimeFunction("ts_array_create", createFt);
                llvm::Value* arr = createCall(createFt, createFn.getCallee(), {});

                // Push each argument (code point) onto the array
                llvm::FunctionType* pushFt = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);

                for (size_t i = 0; i < node->arguments.size(); i++) {
                    visit(node->arguments[i].get());
                    llvm::Value* val = lastValue;

                    // Convert to integer if needed
                    if (val->getType()->isDoubleTy()) {
                        val = builder->CreateFPToSI(val, llvm::Type::getInt64Ty(*context));
                    }

                    // Box the value
                    if (node->arguments[i]->inferredType) {
                        val = boxValue(val, node->arguments[i]->inferredType);
                    } else if (!val->getType()->isPointerTy()) {
                        val = builder->CreateIntToPtr(val, builder->getPtrTy());
                    }

                    createCall(pushFt, pushFn.getCallee(), { arr, val });
                }

                // Call ts_string_fromCodePoint
                llvm::FunctionType* fromCodePointFt = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy() }, false);
                llvm::FunctionCallee fromCodePointFn = getRuntimeFunction("ts_string_fromCodePoint", fromCodePointFt);
                lastValue = createCall(fromCodePointFt, fromCodePointFn.getCallee(), { arr });
                return true;
            }
            if (prop->name == "raw") {
                // String.raw(template, ...substitutions) - returns raw string without escape processing
                // First argument is an object with a 'raw' property (array of raw string pieces)
                // Remaining arguments are substitution values
                if (node->arguments.empty()) {
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                }

                // Get the template object
                visit(node->arguments[0].get());
                llvm::Value* templateObj = lastValue;

                // Unbox if needed (Any type)
                if (node->arguments[0]->inferredType && node->arguments[0]->inferredType->kind == TypeKind::Any) {
                    llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(),
                        { builder->getPtrTy() }, false);
                    llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_object", unboxFt);
                    templateObj = createCall(unboxFt, unboxFn.getCallee(), { templateObj });
                }

                // Create array for substitutions
                llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee createFn = getRuntimeFunction("ts_array_create", createFt);
                llvm::Value* subsArr = createCall(createFt, createFn.getCallee(), {});

                // Push all substitution arguments
                llvm::FunctionType* pushFt = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);

                for (size_t i = 1; i < node->arguments.size(); i++) {
                    visit(node->arguments[i].get());
                    llvm::Value* val = lastValue;

                    // Box the value if not already boxed
                    if (!boxedValues.count(val)) {
                        val = boxValue(val, node->arguments[i]->inferredType);
                    }

                    createCall(pushFt, pushFn.getCallee(), { subsArr, val });
                }

                // Call ts_string_raw(templateObj, substitutionsArray)
                llvm::FunctionType* rawFt = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee rawFn = getRuntimeFunction("ts_string_raw", rawFt);
                lastValue = createCall(rawFt, rawFn.getCallee(), { templateObj, subsArr });
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
    } else if (prop->name == "codePointAt" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::String) {
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

         llvm::FunctionType* codePointAtFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_codePointAt", codePointAtFt);

         lastValue = createCall(codePointAtFt, fn.getCallee(), { obj, index });
         return true;
    } else if (prop->name == "charAt" || prop->name == "at") {
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

         std::string fnName = (prop->name == "at") ? "ts_string_at" : "ts_string_charAt";
         llvm::FunctionType* charAtFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fn = getRuntimeFunction(fnName, charAtFt);

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
    } else if (prop->name == "trimStart" || prop->name == "trimLeft") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_trimStart", ft);
         lastValue = createCall(ft, fn.getCallee(), { obj });
         return true;
    } else if (prop->name == "trimEnd" || prop->name == "trimRight") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_trimEnd", ft);
         lastValue = createCall(ft, fn.getCallee(), { obj });
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
    } else if (prop->name == "indexOf" || prop->name == "lastIndexOf") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* search = lastValue;

         bool isLastIndexOf = (prop->name == "lastIndexOf");

         if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
             if (search->getType()->isPointerTy()) {
                 // If it's a pointer (string), we need to cast it to i64 for the generic array storage
                 search = builder->CreatePtrToInt(search, llvm::Type::getInt64Ty(*context));
             } else if (search->getType()->isDoubleTy()) {
                 // For specialized double arrays, use bitcast to preserve the bit pattern
                 search = builder->CreateBitCast(search, llvm::Type::getInt64Ty(*context));
             }
             llvm::FunctionType* indexOfFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
             std::string fnName = isLastIndexOf ? "ts_array_lastIndexOf" : "ts_array_indexOf";
             llvm::FunctionCallee fn = getRuntimeFunction(fnName, indexOfFt);
             lastValue = createCall(indexOfFt, fn.getCallee(), { obj, search });
         } else {
             if (search->getType()->isIntegerTy(64)) {
                 search = builder->CreateIntToPtr(search, builder->getPtrTy());
             }
             llvm::FunctionType* indexOfFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { builder->getPtrTy(), builder->getPtrTy() }, false);
             // String lastIndexOf not implemented yet, fall back to indexOf
             std::string fnName = isLastIndexOf ? "ts_string_lastIndexOf" : "ts_string_indexOf";
             llvm::FunctionCallee fn = getRuntimeFunction(fnName, indexOfFt);
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
    } else if (prop->name == "fill" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         // arr.fill(value, start?, end?)
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         // Get the array's element type to determine storage format
         auto arrType = std::static_pointer_cast<ArrayType>(prop->expression->inferredType);
         std::shared_ptr<Type> elemType = arrType->elementType;

         // Value argument (required)
         llvm::Value* value = llvm::ConstantPointerNull::get(builder->getPtrTy());
         if (node->arguments.size() >= 1) {
             visit(node->arguments[0].get());
             llvm::Value* val = lastValue;
             std::shared_ptr<Type> argType = node->arguments[0]->inferredType;

             // For specialized arrays (int[], double[]), store raw values without boxing
             // This matches how push() handles specialized storage
             if (elemType->kind == TypeKind::Int) {
                 // For int arrays, cast the value to i64 then to ptr (raw storage)
                 if (val->getType()->isPointerTy()) {
                     val = unboxValue(val, elemType);
                 }
                 if (!val->getType()->isIntegerTy(64)) {
                     val = builder->CreateIntCast(val, builder->getInt64Ty(), true);
                 }
                 value = builder->CreateIntToPtr(val, builder->getPtrTy());
             } else if (elemType->kind == TypeKind::Double) {
                 // For double arrays, bitcast double to i64 then to ptr
                 if (val->getType()->isPointerTy()) {
                     val = unboxValue(val, elemType);
                 }
                 llvm::Value* asInt = builder->CreateBitCast(val, builder->getInt64Ty());
                 value = builder->CreateIntToPtr(asInt, builder->getPtrTy());
             } else {
                 // For all other types (Any, Class, String, etc.), box the value
                 value = boxValue(val, argType);
             }
         }

         // Start argument (default 0)
         llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
         if (node->arguments.size() >= 2) {
             visit(node->arguments[1].get());
             start = lastValue;
             if (start->getType()->isDoubleTy()) {
                 start = builder->CreateFPToSI(start, llvm::Type::getInt64Ty(*context));
             }
         }

         // End argument (default length)
         llvm::Value* end = nullptr;
         if (node->arguments.size() >= 3) {
             visit(node->arguments[2].get());
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

         llvm::FunctionType* fillFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee fillFn = getRuntimeFunction("ts_array_fill", fillFt);
         lastValue = createCall(fillFt, fillFn.getCallee(), { obj, value, start, end });
         return true;
    } else if (prop->name == "copyWithin" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         // arr.copyWithin(target, start?, end?)
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         // Target argument (required)
         if (node->arguments.empty()) {
             lastValue = obj; // No target, return array unchanged
             return true;
         }
         visit(node->arguments[0].get());
         llvm::Value* target = lastValue;
         if (target->getType()->isDoubleTy()) {
             target = builder->CreateFPToSI(target, llvm::Type::getInt64Ty(*context));
         }

         // Start argument (default 0)
         llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
         if (node->arguments.size() >= 2) {
             visit(node->arguments[1].get());
             start = lastValue;
             if (start->getType()->isDoubleTy()) {
                 start = builder->CreateFPToSI(start, llvm::Type::getInt64Ty(*context));
             }
         }

         // End argument (default length)
         llvm::Value* end = nullptr;
         if (node->arguments.size() >= 3) {
             visit(node->arguments[2].get());
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

         llvm::FunctionType* copyWithinFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
         llvm::FunctionCallee copyWithinFn = getRuntimeFunction("ts_array_copyWithin", copyWithinFt);
         lastValue = createCall(copyWithinFt, copyWithinFn.getCallee(), { obj, target, start, end });
         return true;
    } else if (prop->name == "entries" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         // arr.entries() - returns an array of [index, value] pairs
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         llvm::FunctionType* entriesFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee entriesFn = getRuntimeFunction("ts_array_entries", entriesFt);
         lastValue = createCall(entriesFt, entriesFn.getCallee(), { obj });
         return true;
    } else if (prop->name == "keys" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         // arr.keys() - returns an array of indices
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         llvm::FunctionType* keysFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee keysFn = getRuntimeFunction("ts_array_keys", keysFt);
         lastValue = createCall(keysFt, keysFn.getCallee(), { obj });
         return true;
    } else if (prop->name == "values" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         // arr.values() - returns an array of values
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         llvm::FunctionType* valuesFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee valuesFn = getRuntimeFunction("ts_array_values", valuesFt);
         lastValue = createCall(valuesFt, valuesFn.getCallee(), { obj });
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
    } else if (prop->name == "normalize") {
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         // Get optional form argument, or null if not provided
         llvm::Value* form = llvm::ConstantPointerNull::get(builder->getPtrTy());
         if (!node->arguments.empty()) {
             visit(node->arguments[0].get());
             form = lastValue;
             if (form->getType()->isIntegerTy(64)) {
                 form = builder->CreateIntToPtr(form, builder->getPtrTy());
             }
         }

         llvm::FunctionType* normalizeFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_string_normalize", normalizeFt);
         lastValue = createCall(normalizeFt, fn.getCallee(), { obj, form });
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
    } else if (prop->name == "isWellFormed") {
         // ES2024: String.prototype.isWellFormed() -> boolean
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_isWellFormed", ft);
         lastValue = createCall(ft, fn.getCallee(), { obj });
         return true;
    } else if (prop->name == "toWellFormed") {
         // ES2024: String.prototype.toWellFormed() -> string
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_toWellFormed", ft);
         lastValue = createCall(ft, fn.getCallee(), { obj });
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
    } else if (prop->name == "matchAll") {
         // ES2020: String.prototype.matchAll(regexp) -> array of match arrays
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         if (node->arguments.empty()) return true;
         visit(node->arguments[0].get());
         llvm::Value* arg = lastValue;

         llvm::FunctionType* matchAllFt = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_matchAll_regexp", matchAllFt);
         lastValue = createCall(matchAllFt, fn.getCallee(), { obj, arg });
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
         // ts_array_includes expects int64_t as bit pattern (not converted)
         if (val->getType()->isDoubleTy()) {
             // For specialized double arrays, use bitcast to preserve the bit pattern
             val = builder->CreateBitCast(val, llvm::Type::getInt64Ty(*context));
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
    } else if (prop->name == "toReversed" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         // ES2023 toReversed() - returns new reversed array
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_toReversed", ft);
         lastValue = createCall(ft, fn.getCallee(), { obj });
         return true;
    } else if (prop->name == "toSorted" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         // ES2023 toSorted() - returns new sorted array
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_toSorted", ft);
         lastValue = createCall(ft, fn.getCallee(), { obj });
         return true;
    } else if (prop->name == "toSpliced" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         // ES2023 toSpliced(start, deleteCount, ...items) - returns new array with splice applied
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         // Get start (required)
         llvm::Value* start = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
         if (node->arguments.size() > 0) {
             visit(node->arguments[0].get());
             start = lastValue;
             if (start->getType()->isDoubleTy()) {
                 start = builder->CreateFPToSI(start, builder->getInt64Ty());
             }
         }

         // Get deleteCount (optional, defaults to array length - start)
         llvm::Value* deleteCount = llvm::ConstantInt::get(builder->getInt64Ty(), INT64_MAX);
         if (node->arguments.size() > 1) {
             visit(node->arguments[1].get());
             deleteCount = lastValue;
             if (deleteCount->getType()->isDoubleTy()) {
                 deleteCount = builder->CreateFPToSI(deleteCount, builder->getInt64Ty());
             }
         }

         // Collect remaining arguments as items to insert
         llvm::Value* itemsPtr = llvm::ConstantPointerNull::get(builder->getPtrTy());
         llvm::Value* itemCount = llvm::ConstantInt::get(builder->getInt64Ty(), 0);

         if (node->arguments.size() > 2) {
             size_t numItems = node->arguments.size() - 2;

             // Determine if array is a number[] (specialized double array)
             auto arrType = std::dynamic_pointer_cast<ArrayType>(prop->expression->inferredType);
             bool isDoubleArray = arrType && arrType->elementType &&
                                  arrType->elementType->kind == TypeKind::Double;

             // Create a temporary array for items (use ts_array_create with length=0 so Push works)
             llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(),
                     {}, false);  // No arguments for ts_array_create
             llvm::FunctionCallee createFn = getRuntimeFunction("ts_array_create", createFt);
             itemsPtr = createCall(createFt, createFn.getCallee(),
                     {});  // No arguments needed

             for (size_t i = 2; i < node->arguments.size(); ++i) {
                 visit(node->arguments[i].get());
                 llvm::Value* item = lastValue;

                 // Convert value to int64 for storage
                 llvm::Value* itemAsInt64;
                 if (item->getType()->isPointerTy()) {
                     itemAsInt64 = builder->CreatePtrToInt(item, builder->getInt64Ty());
                 } else if (item->getType()->isDoubleTy()) {
                     // Bitcast double to int64 to preserve bit pattern
                     itemAsInt64 = builder->CreateBitCast(item, builder->getInt64Ty());
                 } else if (item->getType()->isIntegerTy(64) && isDoubleArray) {
                     // For number[] arrays, convert int to double first, then bitcast
                     llvm::Value* asDouble = builder->CreateSIToFP(item, builder->getDoubleTy());
                     itemAsInt64 = builder->CreateBitCast(asDouble, builder->getInt64Ty());
                 } else if (item->getType()->isIntegerTy()) {
                     itemAsInt64 = builder->CreateZExtOrBitCast(item, builder->getInt64Ty());
                 } else {
                     itemAsInt64 = item;
                 }

                 llvm::Value* itemAsPtr = builder->CreateIntToPtr(itemAsInt64, builder->getPtrTy());
                 llvm::FunctionType* pushFt = llvm::FunctionType::get(builder->getVoidTy(),
                         { builder->getPtrTy(), builder->getPtrTy() }, false);
                 llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);
                 createCall(pushFt, pushFn.getCallee(), { itemsPtr, itemAsPtr });
             }
             itemCount = llvm::ConstantInt::get(builder->getInt64Ty(), numItems);
         }

         llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty(),
                   builder->getPtrTy(), builder->getInt64Ty() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_toSpliced", ft);
         lastValue = createCall(ft, fn.getCallee(), { obj, start, deleteCount, itemsPtr, itemCount });
         return true;
    } else if (prop->name == "with" && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
         // ES2023 with(index, value) - returns new array with element at index replaced
         visit(prop->expression.get());
         llvm::Value* obj = lastValue;
         if (obj->getType()->isIntegerTy(64)) {
             obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
         }

         if (node->arguments.size() < 2) return true;

         // Get index
         visit(node->arguments[0].get());
         llvm::Value* index = lastValue;
         if (index->getType()->isDoubleTy()) {
             index = builder->CreateFPToSI(index, builder->getInt64Ty());
         }

         // Get value - runtime expects it as int64 (cast from void*)
         visit(node->arguments[1].get());
         llvm::Value* value = lastValue;

         // Determine if array is a number[] (specialized double array)
         auto arrType = std::dynamic_pointer_cast<ArrayType>(prop->expression->inferredType);
         bool isDoubleArray = arrType && arrType->elementType &&
                              arrType->elementType->kind == TypeKind::Double;

         // Convert value to int64 for passing to runtime
         if (value->getType()->isPointerTy()) {
             value = builder->CreatePtrToInt(value, builder->getInt64Ty());
         } else if (value->getType()->isIntegerTy(1)) {
             value = builder->CreateZExt(value, builder->getInt64Ty());
         } else if (value->getType()->isDoubleTy()) {
             value = builder->CreateBitCast(value, builder->getInt64Ty());
         } else if (value->getType()->isIntegerTy(64) && isDoubleArray) {
             // For number[] arrays, convert int to double first, then bitcast
             // This ensures we pass the double bit pattern, not the raw integer
             llvm::Value* asDouble = builder->CreateSIToFP(value, builder->getDoubleTy());
             value = builder->CreateBitCast(asDouble, builder->getInt64Ty());
         }
         // Convert to pointer for the call (will be cast back to int64 in runtime)
         llvm::Value* valueAsPtr = builder->CreateIntToPtr(value, builder->getPtrTy());

         llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                 { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = getRuntimeFunction("ts_array_with", ft);
         lastValue = createCall(ft, fn.getCallee(), { obj, index, valueAsPtr });
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
    } else if ((prop->name == "forEach" || prop->name == "map" || prop->name == "filter" || prop->name == "some" || prop->name == "every" || prop->name == "find" || prop->name == "findIndex" || prop->name == "findLast" || prop->name == "findLastIndex" || prop->name == "flatMap") && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
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
         else if (prop->name == "findIndex" || prop->name == "findLastIndex") retTy = llvm::Type::getInt64Ty(*context);

         llvm::FunctionType* arrayFt = llvm::FunctionType::get(retTy,
                 { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
         llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, arrayFt);
         lastValue = createCall(arrayFt, fn.getCallee(), { obj, callback, thisArg });

         if (prop->name == "find" || prop->name == "findLast") {
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
    } else if ((prop->name == "reduce" || prop->name == "reduceRight") && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
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
         std::string fnName = prop->name == "reduceRight" ? "ts_array_reduceRight" : "ts_array_reduce";
         llvm::FunctionCallee fn = getRuntimeFunction(fnName, reduceFt);
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
    } else if (prop->name == "md5" || prop->name == "createHash" || prop->name == "createHmac" ||
               prop->name == "getHashes" || prop->name == "getCiphers" || prop->name == "randomBytes" ||
               prop->name == "randomFill" || prop->name == "randomFillSync" ||
               prop->name == "randomInt" || prop->name == "randomUUID" || prop->name == "pbkdf2Sync" ||
               prop->name == "pbkdf2" || prop->name == "scryptSync" || prop->name == "scrypt" ||
               prop->name == "timingSafeEqual" || prop->name == "createCipheriv" || prop->name == "createDecipheriv" ||
               prop->name == "createSign" || prop->name == "createVerify" || prop->name == "sign" || prop->name == "verify" ||
               prop->name == "generateKeyPairSync" || prop->name == "generateKeyPair" ||
               prop->name == "generateKeySync" || prop->name == "generateKey" ||
               prop->name == "createPrivateKey" || prop->name == "createPublicKey" || prop->name == "createSecretKey" ||
               prop->name == "hkdfSync") {
        if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "crypto") {
                if (prop->name == "md5") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    if (arg->getType()->isIntegerTy(64)) {
                        arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_md5", ft);
                    lastValue = createCall(ft, fn.getCallee(), { arg });
                    return true;
                } else if (prop->name == "createHash") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* algorithm = lastValue;
                    if (algorithm->getType()->isIntegerTy(64)) {
                        algorithm = builder->CreateIntToPtr(algorithm, builder->getPtrTy());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_createHash", ft);
                    lastValue = createCall(ft, fn.getCallee(), { algorithm });
                    return true;
                } else if (prop->name == "createHmac") {
                    if (node->arguments.size() < 2) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* algorithm = lastValue;
                    if (algorithm->getType()->isIntegerTy(64)) {
                        algorithm = builder->CreateIntToPtr(algorithm, builder->getPtrTy());
                    }
                    visit(node->arguments[1].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[1]->inferredType);
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_createHmac", ft);
                    lastValue = createCall(ft, fn.getCallee(), { algorithm, key });
                    return true;
                } else if (prop->name == "getHashes") {
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_getHashes", ft);
                    lastValue = createCall(ft, fn.getCallee(), {});
                    return true;
                } else if (prop->name == "getCiphers") {
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_getCiphers", ft);
                    lastValue = createCall(ft, fn.getCallee(), {});
                    return true;
                } else if (prop->name == "randomBytes") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* size = lastValue;
                    if (!size->getType()->isIntegerTy(64)) {
                        size = builder->CreatePtrToInt(size, builder->getInt64Ty());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_randomBytes", ft);
                    lastValue = createCall(ft, fn.getCallee(), { size });
                    return true;
                } else if (prop->name == "randomFillSync") {
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* buffer = lastValue;
                    if (buffer->getType()->isIntegerTy(64)) {
                        buffer = builder->CreateIntToPtr(buffer, builder->getPtrTy());
                    }
                    llvm::Value* offset = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
                    llvm::Value* size = llvm::ConstantInt::get(builder->getInt64Ty(), -1);
                    if (node->arguments.size() > 1) {
                        visit(node->arguments[1].get());
                        offset = lastValue;
                        if (!offset->getType()->isIntegerTy(64)) {
                            offset = builder->CreatePtrToInt(offset, builder->getInt64Ty());
                        }
                    }
                    if (node->arguments.size() > 2) {
                        visit(node->arguments[2].get());
                        size = lastValue;
                        if (!size->getType()->isIntegerTy(64)) {
                            size = builder->CreatePtrToInt(size, builder->getInt64Ty());
                        }
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_randomFillSync", ft);
                    lastValue = createCall(ft, fn.getCallee(), { buffer, offset, size });
                    return true;
                } else if (prop->name == "randomFill") {
                    // crypto.randomFill(buffer, offset?, size?, callback)
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* buffer = lastValue;
                    if (buffer->getType()->isIntegerTy(64)) {
                        buffer = builder->CreateIntToPtr(buffer, builder->getPtrTy());
                    }
                    llvm::Value* offset = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
                    llvm::Value* size = llvm::ConstantInt::get(builder->getInt64Ty(), -1);
                    llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

                    size_t argIdx = 1;
                    // Check if we have offset/size arguments (they're numbers, not functions)
                    if (node->arguments.size() > argIdx) {
                        auto& arg = node->arguments[argIdx];
                        if (arg->inferredType && arg->inferredType->kind != TypeKind::Function) {
                            visit(arg.get());
                            offset = lastValue;
                            if (!offset->getType()->isIntegerTy(64)) {
                                offset = builder->CreatePtrToInt(offset, builder->getInt64Ty());
                            }
                            argIdx++;
                        }
                    }
                    if (node->arguments.size() > argIdx) {
                        auto& arg = node->arguments[argIdx];
                        if (arg->inferredType && arg->inferredType->kind != TypeKind::Function) {
                            visit(arg.get());
                            size = lastValue;
                            if (!size->getType()->isIntegerTy(64)) {
                                size = builder->CreatePtrToInt(size, builder->getInt64Ty());
                            }
                            argIdx++;
                        }
                    }
                    // The last argument should be the callback
                    if (node->arguments.size() > argIdx) {
                        visit(node->arguments[argIdx].get());
                        callback = lastValue;
                        if (callback->getType()->isIntegerTy(64)) {
                            callback = builder->CreateIntToPtr(callback, builder->getPtrTy());
                        }
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                        { builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_randomFill", ft);
                    createCall(ft, fn.getCallee(), { buffer, offset, size, callback });
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                } else if (prop->name == "randomInt") {
                    llvm::Value* min = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
                    llvm::Value* max;
                    if (node->arguments.size() == 1) {
                        visit(node->arguments[0].get());
                        max = lastValue;
                    } else if (node->arguments.size() >= 2) {
                        visit(node->arguments[0].get());
                        min = lastValue;
                        visit(node->arguments[1].get());
                        max = lastValue;
                    } else {
                        max = llvm::ConstantInt::get(builder->getInt64Ty(), 100);
                    }
                    if (!min->getType()->isIntegerTy(64)) {
                        min = builder->CreatePtrToInt(min, builder->getInt64Ty());
                    }
                    if (!max->getType()->isIntegerTy(64)) {
                        max = builder->CreatePtrToInt(max, builder->getInt64Ty());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getInt64Ty(), builder->getInt64Ty() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_randomInt", ft);
                    lastValue = createCall(ft, fn.getCallee(), { min, max });
                    return true;
                } else if (prop->name == "randomUUID") {
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_randomUUID", ft);
                    lastValue = createCall(ft, fn.getCallee(), {});
                    return true;
                } else if (prop->name == "pbkdf2Sync") {
                    if (node->arguments.size() < 5) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* password = boxValue(lastValue, node->arguments[0]->inferredType);
                    visit(node->arguments[1].get());
                    llvm::Value* salt = boxValue(lastValue, node->arguments[1]->inferredType);
                    visit(node->arguments[2].get());
                    llvm::Value* iterations = lastValue;
                    if (!iterations->getType()->isIntegerTy(64)) {
                        iterations = builder->CreatePtrToInt(iterations, builder->getInt64Ty());
                    }
                    visit(node->arguments[3].get());
                    llvm::Value* keylen = lastValue;
                    if (!keylen->getType()->isIntegerTy(64)) {
                        keylen = builder->CreatePtrToInt(keylen, builder->getInt64Ty());
                    }
                    visit(node->arguments[4].get());
                    llvm::Value* digest = lastValue;
                    if (digest->getType()->isIntegerTy(64)) {
                        digest = builder->CreateIntToPtr(digest, builder->getPtrTy());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                        { builder->getPtrTy(), builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_pbkdf2Sync", ft);
                    lastValue = createCall(ft, fn.getCallee(), { password, salt, iterations, keylen, digest });
                    return true;
                } else if (prop->name == "scryptSync") {
                    if (node->arguments.size() < 3) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* password = boxValue(lastValue, node->arguments[0]->inferredType);
                    visit(node->arguments[1].get());
                    llvm::Value* salt = boxValue(lastValue, node->arguments[1]->inferredType);
                    visit(node->arguments[2].get());
                    llvm::Value* keylen = lastValue;
                    if (!keylen->getType()->isIntegerTy(64)) {
                        keylen = builder->CreatePtrToInt(keylen, builder->getInt64Ty());
                    }
                    // Default scrypt parameters
                    llvm::Value* N = llvm::ConstantInt::get(builder->getInt64Ty(), 16384);
                    llvm::Value* r = llvm::ConstantInt::get(builder->getInt64Ty(), 8);
                    llvm::Value* p = llvm::ConstantInt::get(builder->getInt64Ty(), 1);
                    // TODO: Parse options object if provided
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                        { builder->getPtrTy(), builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty(), builder->getInt64Ty(), builder->getInt64Ty() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_scryptSync", ft);
                    lastValue = createCall(ft, fn.getCallee(), { password, salt, keylen, N, r, p });
                    return true;
                } else if (prop->name == "timingSafeEqual") {
                    if (node->arguments.size() < 2) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* a = lastValue;
                    if (a->getType()->isIntegerTy(64)) {
                        a = builder->CreateIntToPtr(a, builder->getPtrTy());
                    }
                    visit(node->arguments[1].get());
                    llvm::Value* b = lastValue;
                    if (b->getType()->isIntegerTy(64)) {
                        b = builder->CreateIntToPtr(b, builder->getPtrTy());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_timingSafeEqual", ft);
                    lastValue = createCall(ft, fn.getCallee(), { a, b });
                    return true;
                } else if (prop->name == "pbkdf2") {
                    // crypto.pbkdf2(password, salt, iterations, keylen, digest, callback)
                    if (node->arguments.size() < 6) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* password = boxValue(lastValue, node->arguments[0]->inferredType);
                    visit(node->arguments[1].get());
                    llvm::Value* salt = boxValue(lastValue, node->arguments[1]->inferredType);
                    visit(node->arguments[2].get());
                    llvm::Value* iterations = lastValue;
                    if (!iterations->getType()->isIntegerTy(64)) {
                        iterations = builder->CreatePtrToInt(iterations, builder->getInt64Ty());
                    }
                    visit(node->arguments[3].get());
                    llvm::Value* keylen = lastValue;
                    if (!keylen->getType()->isIntegerTy(64)) {
                        keylen = builder->CreatePtrToInt(keylen, builder->getInt64Ty());
                    }
                    visit(node->arguments[4].get());
                    llvm::Value* digest = lastValue;
                    if (digest->getType()->isIntegerTy(64)) {
                        digest = builder->CreateIntToPtr(digest, builder->getPtrTy());
                    }
                    visit(node->arguments[5].get());
                    llvm::Value* callback = lastValue;
                    if (callback->getType()->isIntegerTy(64)) {
                        callback = builder->CreateIntToPtr(callback, builder->getPtrTy());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                        { builder->getPtrTy(), builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_pbkdf2", ft);
                    createCall(ft, fn.getCallee(), { password, salt, iterations, keylen, digest, callback });
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                } else if (prop->name == "scrypt") {
                    // crypto.scrypt(password, salt, keylen, options, callback)
                    // We simplify: crypto.scrypt(password, salt, keylen, callback) with default options
                    // Or: crypto.scrypt(password, salt, keylen, options, callback) where options has N, r, p
                    if (node->arguments.size() < 4) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* password = boxValue(lastValue, node->arguments[0]->inferredType);
                    visit(node->arguments[1].get());
                    llvm::Value* salt = boxValue(lastValue, node->arguments[1]->inferredType);
                    visit(node->arguments[2].get());
                    llvm::Value* keylen = lastValue;
                    if (!keylen->getType()->isIntegerTy(64)) {
                        keylen = builder->CreatePtrToInt(keylen, builder->getInt64Ty());
                    }
                    // Default scrypt parameters
                    llvm::Value* N = llvm::ConstantInt::get(builder->getInt64Ty(), 16384);
                    llvm::Value* r = llvm::ConstantInt::get(builder->getInt64Ty(), 8);
                    llvm::Value* p = llvm::ConstantInt::get(builder->getInt64Ty(), 1);
                    llvm::Value* callback = nullptr;
                    // Check if argument 3 is a function (callback) or options object
                    if (node->arguments.size() == 4) {
                        // scrypt(password, salt, keylen, callback)
                        visit(node->arguments[3].get());
                        callback = lastValue;
                    } else if (node->arguments.size() >= 5) {
                        // scrypt(password, salt, keylen, options, callback)
                        // TODO: Parse options object for N, r, p
                        visit(node->arguments[4].get());
                        callback = lastValue;
                    }
                    if (callback && callback->getType()->isIntegerTy(64)) {
                        callback = builder->CreateIntToPtr(callback, builder->getPtrTy());
                    }
                    if (!callback) {
                        callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                        { builder->getPtrTy(), builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty(), builder->getInt64Ty(), builder->getInt64Ty(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_scrypt", ft);
                    createCall(ft, fn.getCallee(), { password, salt, keylen, N, r, p, callback });
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                } else if (prop->name == "createCipheriv") {
                    // crypto.createCipheriv(algorithm, key, iv) -> Cipher
                    if (node->arguments.size() < 3) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* algorithm = lastValue;
                    if (algorithm->getType()->isIntegerTy(64)) {
                        algorithm = builder->CreateIntToPtr(algorithm, builder->getPtrTy());
                    }
                    visit(node->arguments[1].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[1]->inferredType);
                    visit(node->arguments[2].get());
                    llvm::Value* iv = boxValue(lastValue, node->arguments[2]->inferredType);
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                        { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_createCipheriv", ft);
                    lastValue = createCall(ft, fn.getCallee(), { algorithm, key, iv });
                    return true;
                } else if (prop->name == "createDecipheriv") {
                    // crypto.createDecipheriv(algorithm, key, iv) -> Decipher
                    if (node->arguments.size() < 3) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* algorithm = lastValue;
                    if (algorithm->getType()->isIntegerTy(64)) {
                        algorithm = builder->CreateIntToPtr(algorithm, builder->getPtrTy());
                    }
                    visit(node->arguments[1].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[1]->inferredType);
                    visit(node->arguments[2].get());
                    llvm::Value* iv = boxValue(lastValue, node->arguments[2]->inferredType);
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                        { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_createDecipheriv", ft);
                    lastValue = createCall(ft, fn.getCallee(), { algorithm, key, iv });
                    return true;
                } else if (prop->name == "createSign") {
                    // crypto.createSign(algorithm) -> Sign
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* algorithm = lastValue;
                    if (algorithm->getType()->isIntegerTy(64)) {
                        algorithm = builder->CreateIntToPtr(algorithm, builder->getPtrTy());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_createSign", ft);
                    lastValue = createCall(ft, fn.getCallee(), { algorithm });
                    return true;
                } else if (prop->name == "createVerify") {
                    // crypto.createVerify(algorithm) -> Verify
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* algorithm = lastValue;
                    if (algorithm->getType()->isIntegerTy(64)) {
                        algorithm = builder->CreateIntToPtr(algorithm, builder->getPtrTy());
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_createVerify", ft);
                    lastValue = createCall(ft, fn.getCallee(), { algorithm });
                    return true;
                } else if (prop->name == "sign") {
                    // crypto.sign(algorithm, data, key, options?) -> Buffer
                    if (node->arguments.size() < 3) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* algorithm = lastValue;
                    if (algorithm->getType()->isIntegerTy(64)) {
                        algorithm = builder->CreateIntToPtr(algorithm, builder->getPtrTy());
                    }
                    visit(node->arguments[1].get());
                    llvm::Value* data = boxValue(lastValue, node->arguments[1]->inferredType);
                    visit(node->arguments[2].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[2]->inferredType);
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                        { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_sign_oneshot", ft);
                    lastValue = createCall(ft, fn.getCallee(), { algorithm, data, key });
                    return true;
                } else if (prop->name == "verify") {
                    // crypto.verify(algorithm, data, key, signature) -> boolean
                    if (node->arguments.size() < 4) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* algorithm = lastValue;
                    if (algorithm->getType()->isIntegerTy(64)) {
                        algorithm = builder->CreateIntToPtr(algorithm, builder->getPtrTy());
                    }
                    visit(node->arguments[1].get());
                    llvm::Value* data = boxValue(lastValue, node->arguments[1]->inferredType);
                    visit(node->arguments[2].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[2]->inferredType);
                    visit(node->arguments[3].get());
                    llvm::Value* signature = boxValue(lastValue, node->arguments[3]->inferredType);
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                        { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_verify_oneshot", ft);
                    lastValue = createCall(ft, fn.getCallee(), { algorithm, data, key, signature });
                    return true;
                } else if (prop->name == "generateKeyPairSync") {
                    // crypto.generateKeyPairSync(type, options?) -> { publicKey, privateKey }
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* type = lastValue;
                    if (type->getType()->isIntegerTy(64)) {
                        type = builder->CreateIntToPtr(type, builder->getPtrTy());
                    }

                    // Extract options: modulusLength (for RSA), namedCurve (for EC)
                    llvm::Value* modulusLength = llvm::ConstantInt::get(builder->getInt64Ty(), 2048);
                    llvm::Value* namedCurve = llvm::ConstantPointerNull::get(builder->getPtrTy());

                    if (node->arguments.size() > 1 && node->arguments[1]) {
                        // Options object - try to extract modulusLength and namedCurve
                        if (auto objLit = dynamic_cast<ast::ObjectLiteralExpression*>(node->arguments[1].get())) {
                            for (auto& prop : objLit->properties) {
                                if (auto propAssign = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
                                    if (propAssign->name == "modulusLength") {
                                        visit(propAssign->initializer.get());
                                        modulusLength = lastValue;
                                        if (modulusLength->getType()->isPointerTy()) {
                                            modulusLength = builder->CreatePtrToInt(modulusLength, builder->getInt64Ty());
                                        }
                                    } else if (propAssign->name == "namedCurve") {
                                        visit(propAssign->initializer.get());
                                        namedCurve = lastValue;
                                        if (namedCurve->getType()->isIntegerTy(64)) {
                                            namedCurve = builder->CreateIntToPtr(namedCurve, builder->getPtrTy());
                                        }
                                    }
                                }
                            }
                        }
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                        { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_generateKeyPairSync", ft);
                    lastValue = createCall(ft, fn.getCallee(), { type, modulusLength, namedCurve });
                    return true;
                } else if (prop->name == "generateKeyPair") {
                    // crypto.generateKeyPair(type, options, callback)
                    if (node->arguments.size() < 3) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* type = lastValue;
                    if (type->getType()->isIntegerTy(64)) {
                        type = builder->CreateIntToPtr(type, builder->getPtrTy());
                    }

                    // Extract options
                    llvm::Value* modulusLength = llvm::ConstantInt::get(builder->getInt64Ty(), 2048);
                    llvm::Value* namedCurve = llvm::ConstantPointerNull::get(builder->getPtrTy());

                    if (node->arguments[1]) {
                        if (auto objLit = dynamic_cast<ast::ObjectLiteralExpression*>(node->arguments[1].get())) {
                            for (auto& prop : objLit->properties) {
                                if (auto propAssign = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
                                    if (propAssign->name == "modulusLength") {
                                        visit(propAssign->initializer.get());
                                        modulusLength = lastValue;
                                        if (modulusLength->getType()->isPointerTy()) {
                                            modulusLength = builder->CreatePtrToInt(modulusLength, builder->getInt64Ty());
                                        }
                                    } else if (propAssign->name == "namedCurve") {
                                        visit(propAssign->initializer.get());
                                        namedCurve = lastValue;
                                        if (namedCurve->getType()->isIntegerTy(64)) {
                                            namedCurve = builder->CreateIntToPtr(namedCurve, builder->getPtrTy());
                                        }
                                    }
                                }
                            }
                        }
                    }

                    visit(node->arguments[2].get());
                    llvm::Value* callback = lastValue;
                    if (callback->getType()->isIntegerTy(64)) {
                        callback = builder->CreateIntToPtr(callback, builder->getPtrTy());
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                        { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_generateKeyPair", ft);
                    createCall(ft, fn.getCallee(), { type, modulusLength, namedCurve, callback });
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                } else if (prop->name == "generateKeySync") {
                    // crypto.generateKeySync(type, options?) -> Buffer
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* type = lastValue;
                    if (type->getType()->isIntegerTy(64)) {
                        type = builder->CreateIntToPtr(type, builder->getPtrTy());
                    }

                    llvm::Value* length = llvm::ConstantInt::get(builder->getInt64Ty(), 256); // default 256 bits

                    if (node->arguments.size() > 1 && node->arguments[1]) {
                        if (auto objLit = dynamic_cast<ast::ObjectLiteralExpression*>(node->arguments[1].get())) {
                            for (auto& prop : objLit->properties) {
                                if (auto propAssign = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
                                    if (propAssign->name == "length") {
                                        visit(propAssign->initializer.get());
                                        length = lastValue;
                                        if (length->getType()->isPointerTy()) {
                                            length = builder->CreatePtrToInt(length, builder->getInt64Ty());
                                        }
                                    }
                                }
                            }
                        }
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                        { builder->getPtrTy(), builder->getInt64Ty() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_generateKeySync", ft);
                    lastValue = createCall(ft, fn.getCallee(), { type, length });
                    return true;
                } else if (prop->name == "generateKey") {
                    // crypto.generateKey(type, options, callback)
                    if (node->arguments.size() < 3) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* type = lastValue;
                    if (type->getType()->isIntegerTy(64)) {
                        type = builder->CreateIntToPtr(type, builder->getPtrTy());
                    }

                    llvm::Value* length = llvm::ConstantInt::get(builder->getInt64Ty(), 256);

                    if (node->arguments[1]) {
                        if (auto objLit = dynamic_cast<ast::ObjectLiteralExpression*>(node->arguments[1].get())) {
                            for (auto& prop : objLit->properties) {
                                if (auto propAssign = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
                                    if (propAssign->name == "length") {
                                        visit(propAssign->initializer.get());
                                        length = lastValue;
                                        if (length->getType()->isPointerTy()) {
                                            length = builder->CreatePtrToInt(length, builder->getInt64Ty());
                                        }
                                    }
                                }
                            }
                        }
                    }

                    visit(node->arguments[2].get());
                    llvm::Value* callback = lastValue;
                    if (callback->getType()->isIntegerTy(64)) {
                        callback = builder->CreateIntToPtr(callback, builder->getPtrTy());
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                        { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_generateKey", ft);
                    createCall(ft, fn.getCallee(), { type, length, callback });
                    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    return true;
                } else if (prop->name == "createPrivateKey") {
                    // crypto.createPrivateKey(key) -> string
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_createPrivateKey", ft);
                    lastValue = createCall(ft, fn.getCallee(), { key });
                    return true;
                } else if (prop->name == "createPublicKey") {
                    // crypto.createPublicKey(key) -> string
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_createPublicKey", ft);
                    lastValue = createCall(ft, fn.getCallee(), { key });
                    return true;
                } else if (prop->name == "createSecretKey") {
                    // crypto.createSecretKey(key, encoding?) -> Buffer
                    if (node->arguments.empty()) return true;
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    llvm::Value* encoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    if (node->arguments.size() > 1) {
                        visit(node->arguments[1].get());
                        encoding = lastValue;
                        if (encoding->getType()->isIntegerTy(64)) {
                            encoding = builder->CreateIntToPtr(encoding, builder->getPtrTy());
                        }
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_createSecretKey", ft);
                    lastValue = createCall(ft, fn.getCallee(), { key, encoding });
                    return true;
                } else if (prop->name == "hkdfSync") {
                    // crypto.hkdfSync(digest, ikm, salt, info, keylen) -> Buffer
                    if (node->arguments.size() < 5) return true;

                    // Get digest algorithm
                    visit(node->arguments[0].get());
                    llvm::Value* digest = lastValue;
                    if (digest->getType()->isIntegerTy(64)) {
                        digest = builder->CreateIntToPtr(digest, builder->getPtrTy());
                    }

                    // Get ikm (input keying material) - box it
                    visit(node->arguments[1].get());
                    llvm::Value* ikm = lastValue;
                    if (node->arguments[1]->inferredType) {
                        ikm = boxValue(ikm, node->arguments[1]->inferredType);
                    }

                    // Get salt - box it
                    visit(node->arguments[2].get());
                    llvm::Value* salt = lastValue;
                    if (node->arguments[2]->inferredType) {
                        salt = boxValue(salt, node->arguments[2]->inferredType);
                    }

                    // Get info - box it
                    visit(node->arguments[3].get());
                    llvm::Value* info = lastValue;
                    if (node->arguments[3]->inferredType) {
                        info = boxValue(info, node->arguments[3]->inferredType);
                    }

                    // Get keylen
                    visit(node->arguments[4].get());
                    llvm::Value* keylen = lastValue;
                    if (keylen->getType()->isPointerTy()) {
                        keylen = builder->CreatePtrToInt(keylen, builder->getInt64Ty());
                    } else if (!keylen->getType()->isIntegerTy(64)) {
                        keylen = builder->CreateZExtOrTrunc(keylen, builder->getInt64Ty());
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        builder->getPtrTy(),
                        { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getInt64Ty() },
                        false
                    );
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_crypto_hkdfSync", ft);
                    lastValue = createCall(ft, fn.getCallee(), { digest, ikm, salt, info, keylen });
                    return true;
                }
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

bool IRGenerator::tryGenerateTimersCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "timers") return false;

    const std::string& methodName = prop->name;

    // timers.setTimeout(callback, delay) / timers.setInterval(callback, delay)
    if (methodName == "setTimeout" || methodName == "setInterval") {
        if (node->arguments.size() < 2) return true;

        visit(node->arguments[0].get());
        llvm::Value* callback = lastValue;

        // Ensure callback is a pointer to the function
        if (callback->getType()->isIntegerTy()) {
            callback = builder->CreateIntToPtr(callback, builder->getPtrTy());
        }

        llvm::Value* boxedCallback = boxValue(callback, node->arguments[0]->inferredType);

        visit(node->arguments[1].get());
        llvm::Value* delay = lastValue;

        std::string funcName = (methodName == "setTimeout") ? "ts_set_timeout" : "ts_set_interval";
        llvm::FunctionType* timerFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee timerFn = module->getOrInsertFunction(funcName, timerFt);

        llvm::Value* boxedRes = createCall(timerFt, timerFn.getCallee(), { boxedCallback, delay });
        lastValue = unboxValue(boxedRes, std::make_shared<Type>(TypeKind::Int));
        return true;
    }

    // timers.clearTimeout(id) / timers.clearInterval(id) / timers.clearImmediate(id)
    if (methodName == "clearTimeout" || methodName == "clearInterval" || methodName == "clearImmediate") {
        if (node->arguments.empty()) return true;

        visit(node->arguments[0].get());
        llvm::Value* timerId = lastValue;
        llvm::Value* boxedId = boxValue(timerId, node->arguments[0]->inferredType);

        llvm::FunctionType* clearFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee clearFn = getRuntimeFunction("ts_clear_timer", clearFt);

        createCall(clearFt, clearFn.getCallee(), { boxedId });
        lastValue = nullptr;
        return true;
    }

    // timers.setImmediate(callback)
    if (methodName == "setImmediate") {
        if (node->arguments.empty()) return true;

        visit(node->arguments[0].get());
        llvm::Value* callback = lastValue;
        llvm::Value* boxedCallback = boxValue(callback, node->arguments[0]->inferredType);

        llvm::FunctionType* timerFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee timerFn = getRuntimeFunction("ts_set_immediate", timerFt);

        llvm::Value* boxedRes = createCall(timerFt, timerFn.getCallee(), { boxedCallback });
        lastValue = unboxValue(boxedRes, std::make_shared<Type>(TypeKind::Int));
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateTimersPromisesCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id) return false;

    // Check if this is the timers/promises module by examining the inferred type
    auto exprType = prop->expression->inferredType;
    if (!exprType) return false;

    // Handle namespace imports: import * as timersPromises from 'timers/promises'
    if (exprType->kind == TypeKind::Namespace) {
        auto nsType = std::static_pointer_cast<NamespaceType>(exprType);
        if (!nsType->module) return false;
        // Check if this is the timers/promises builtin module
        if (nsType->module->path != "builtin:timers/promises") return false;
    } else if (exprType->kind == TypeKind::Object) {
        // Handle direct symbol lookup (less common)
        auto objType = std::static_pointer_cast<ObjectType>(exprType);
        auto setTimeoutIt = objType->fields.find("setTimeout");
        if (setTimeoutIt == objType->fields.end()) return false;

        // Check if setTimeout returns Promise (timers/promises) vs int (timers)
        auto setTimeoutType = setTimeoutIt->second;
        if (!setTimeoutType || setTimeoutType->kind != TypeKind::Function) return false;

        auto funcType = std::static_pointer_cast<FunctionType>(setTimeoutType);
        if (!funcType->returnType || funcType->returnType->kind != TypeKind::Class) return false;

        auto returnClass = std::static_pointer_cast<ClassType>(funcType->returnType);
        if (returnClass->name != "Promise") return false;
    } else {
        return false;
    }

    // This is the timers/promises module
    const std::string& methodName = prop->name;

    // timers/promises.setTimeout(delay, value?) -> Promise
    if (methodName == "setTimeout") {
        // First arg is delay (required)
        if (node->arguments.empty()) {
            // No delay provided, use 0
            llvm::Value* delay = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            llvm::Value* value = llvm::ConstantPointerNull::get(builder->getPtrTy());

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getInt64Ty(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_timers_promises_setTimeout", ft);
            lastValue = createCall(ft, fn.getCallee(), { delay, value });
            boxedValues.insert(lastValue);
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* delay = lastValue;
        if (delay->getType()->isPointerTy()) {
            delay = unboxValue(delay, std::make_shared<Type>(TypeKind::Int));
        }

        llvm::Value* value = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            value = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getInt64Ty(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_timers_promises_setTimeout", ft);
        lastValue = createCall(ft, fn.getCallee(), { delay, value });
        boxedValues.insert(lastValue);
        return true;
    }

    // timers/promises.setImmediate(value?) -> Promise
    if (methodName == "setImmediate") {
        llvm::Value* value = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            value = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_timers_promises_setImmediate", ft);
        lastValue = createCall(ft, fn.getCallee(), { value });
        boxedValues.insert(lastValue);
        return true;
    }

    // timers/promises.setInterval(delay, value?) -> AsyncIterable
    if (methodName == "setInterval") {
        // First arg is delay (required)
        llvm::Value* delay = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
        llvm::Value* value = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            delay = lastValue;
            if (delay->getType()->isPointerTy()) {
                delay = unboxValue(delay, std::make_shared<Type>(TypeKind::Int));
            }
        }

        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            value = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getInt64Ty(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_timers_promises_setInterval", ft);
        lastValue = createCall(ft, fn.getCallee(), { delay, value });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

// Handle timers/promises.scheduler.wait() and scheduler.yield()
bool IRGenerator::tryGenerateTimersSchedulerCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check if this is scheduler.wait or scheduler.yield
    // The access pattern is: timersPromises.scheduler.wait() or timersPromises.scheduler.yield()
    auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get());
    if (!innerProp) return false;

    // innerProp should be something.scheduler
    if (innerProp->name != "scheduler") return false;

    // Check if the expression of innerProp is the timers/promises module
    auto id = dynamic_cast<ast::Identifier*>(innerProp->expression.get());
    if (!id) return false;

    auto exprType = innerProp->expression->inferredType;
    if (!exprType) return false;

    // Check if this is the timers/promises module
    bool isTimersPromises = false;
    if (exprType->kind == TypeKind::Namespace) {
        auto nsType = std::static_pointer_cast<NamespaceType>(exprType);
        if (nsType->module && nsType->module->path == "builtin:timers/promises") {
            isTimersPromises = true;
        }
    } else if (exprType->kind == TypeKind::Object) {
        auto objType = std::static_pointer_cast<ObjectType>(exprType);
        if (objType->fields.find("scheduler") != objType->fields.end()) {
            isTimersPromises = true;
        }
    }

    if (!isTimersPromises) return false;

    const std::string& methodName = prop->name;

    // scheduler.wait(delay, options?) -> Promise<void>
    if (methodName == "wait") {
        llvm::Value* delay = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            delay = lastValue;
            if (delay->getType()->isPointerTy()) {
                delay = unboxValue(delay, std::make_shared<Type>(TypeKind::Int));
            }
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_timers_scheduler_wait", ft);
        lastValue = createCall(ft, fn.getCallee(), { delay });
        boxedValues.insert(lastValue);
        return true;
    }

    // scheduler.yield() -> Promise<void>
    if (methodName == "yield") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_timers_scheduler_yield", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

} // namespace ts
