#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register HTTP module's runtime functions once (44 functions)
static bool httpFunctionsRegistered = false;
static void ensureHTTPFunctionsRegistered(BoxingPolicy& bp) {
    if (httpFunctionsRegistered) return;
    httpFunctionsRegistered = true;
    
    // HTTP server/client
    bp.registerRuntimeApi("ts_http_create_server", {true}, true);  // options -> Server
    bp.registerRuntimeApi("ts_http_get", {true, true}, true);  // url/options, callback
    bp.registerRuntimeApi("ts_http_request", {true, true}, true);
    bp.registerRuntimeApi("ts_http_server_listen", {true, false, false, true}, true);  // server, port, host, callback
    bp.registerRuntimeApi("ts_http_server_response_write_head", {true, false, false, true}, false);  // res, statusCode, statusMsg, headers
    bp.registerRuntimeApi("ts_http_get_global_agent", {}, true);
    bp.registerRuntimeApi("ts_http_agent_destroy", {true}, false);
    bp.registerRuntimeApi("ts_http_get_max_header_size", {}, false);  // -> number
    bp.registerRuntimeApi("ts_http_get_max_idle_http_parsers", {}, false);
    bp.registerRuntimeApi("ts_http_set_max_idle_http_parsers", {false}, false);
    bp.registerRuntimeApi("ts_http_get_methods", {}, true);  // -> array
    bp.registerRuntimeApi("ts_http_get_status_codes", {}, true);  // -> object
    bp.registerRuntimeApi("ts_http_validate_header_name", {false}, false);
    bp.registerRuntimeApi("ts_http_validate_header_value", {false, false}, false);
    
    // HTTPS
    bp.registerRuntimeApi("ts_https_create_server", {true}, true);
    bp.registerRuntimeApi("ts_https_get", {true, true}, true);
    bp.registerRuntimeApi("ts_https_request", {true, true}, true);
    bp.registerRuntimeApi("ts_https_get_global_agent", {}, true);
    
    // WebSocket
    bp.registerRuntimeApi("ts_websocket_send", {true, true}, false);  // socket, data
    bp.registerRuntimeApi("ts_websocket_close", {true, false, false}, false);  // socket, code, reason
    bp.registerRuntimeApi("ts_websocket_ping", {true, true}, false);
    bp.registerRuntimeApi("ts_websocket_pong", {true, true}, false);
    bp.registerRuntimeApi("ts_websocket_get_ready_state", {true}, false);
    bp.registerRuntimeApi("ts_websocket_get_buffered_amount", {true}, false);
    bp.registerRuntimeApi("ts_websocket_get_url", {true}, false);
    bp.registerRuntimeApi("ts_websocket_get_protocol", {true}, false);
    bp.registerRuntimeApi("ts_websocket_get_extensions", {true}, false);
    bp.registerRuntimeApi("ts_websocket_get_binary_type", {true}, false);
    bp.registerRuntimeApi("ts_websocket_get_onopen", {true}, true);
    bp.registerRuntimeApi("ts_websocket_get_onclose", {true}, true);
    bp.registerRuntimeApi("ts_websocket_get_onerror", {true}, true);
    bp.registerRuntimeApi("ts_websocket_get_onmessage", {true}, true);
    // WebSocket state constants
    bp.registerRuntimeApi("ts_websocket_connecting", {}, false);
    bp.registerRuntimeApi("ts_websocket_open", {}, false);
    bp.registerRuntimeApi("ts_websocket_closing", {}, false);
    bp.registerRuntimeApi("ts_websocket_closed", {}, false);
    
    // Net helpers used in HTTP
    bp.registerRuntimeApi("ts_net_socket_address_get_address", {true}, false);
    bp.registerRuntimeApi("ts_net_socket_address_get_port", {true}, false);
    bp.registerRuntimeApi("ts_net_socket_address_get_family", {true}, false);
    bp.registerRuntimeApi("ts_net_socket_address_get_flowlabel", {true}, false);
    bp.registerRuntimeApi("ts_net_block_list_get_rules", {true}, true);  // -> array
    
    // Stream helpers used in HTTP
    bp.registerRuntimeApi("ts_writable_write", {true, true}, true);
    bp.registerRuntimeApi("ts_writable_end", {true}, true);
    bp.registerRuntimeApi("ts_value_get_object", {true}, false);  // Returns raw pointer
}

bool IRGenerator::tryGenerateHTTPCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureHTTPFunctionsRegistered(boxingPolicy);
    
    SPDLOG_INFO("tryGenerateHTTPCall: prop->name={}", prop->name);
    bool isHttp = false;
    bool isHttps = false;
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        SPDLOG_INFO("tryGenerateHTTPCall: id->name={}", id->name);
        if (id->name == "http") isHttp = true;
        else if (id->name == "https") isHttps = true;
    }

    bool isServer = false;
    bool isServerResponse = false;
    bool isIncomingMessage = false;
    bool isClientRequest = false;
    bool isAgent = false;
    bool isHttpsAgent = false;
    bool isWebSocket = false;

    if (!isHttp && !isHttps) {
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            SPDLOG_INFO("tryGenerateHTTPCall: classType->name={}", classType->name);
            if (classType->name == "Server") isServer = true;
            else if (classType->name == "ServerResponse") isServerResponse = true;
            else if (classType->name == "IncomingMessage") isIncomingMessage = true;
            else if (classType->name == "ClientRequest") isClientRequest = true;
            else if (classType->name == "Agent") isAgent = true;
            else if (classType->name == "HttpsAgent") isHttpsAgent = true;
            else if (classType->name == "WebSocket") isWebSocket = true;
        } else if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
            // For untyped variables (e.g., callback parameters without type annotations),
            // try to infer the type from the method name being called
            SPDLOG_INFO("tryGenerateHTTPCall: expression has Any type, checking method name={}", prop->name);
            if (prop->name == "writeHead" || prop->name == "write" || prop->name == "end" || 
                prop->name == "statusCode" || prop->name == "setHeader") {
                // Could be ServerResponse or ClientRequest
                isServerResponse = true;  // Default to ServerResponse for these methods
                SPDLOG_INFO("tryGenerateHTTPCall: inferred isServerResponse=true from method name");
            } else if (prop->name == "headers" || prop->name == "method" || prop->name == "url") {
                isIncomingMessage = true;
                SPDLOG_INFO("tryGenerateHTTPCall: inferred isIncomingMessage=true from method name");
            } else if (prop->name == "destroy" && !prop->expression->inferredType) {
                // Could be Agent.destroy()
                isAgent = true;
            } else if (prop->name == "send" || prop->name == "close" || prop->name == "ping" || prop->name == "pong") {
                // Could be WebSocket
                isWebSocket = true;
            }
        } else if (prop->expression->inferredType) {
            SPDLOG_INFO("tryGenerateHTTPCall: expression type kind={}", (int)prop->expression->inferredType->kind);
        } else {
            SPDLOG_INFO("tryGenerateHTTPCall: expression has no inferred type");
        }
    }

    if (!isHttp && !isHttps && !isServer && !isServerResponse && !isIncomingMessage && !isClientRequest && !isAgent && !isHttpsAgent && !isWebSocket) return false;

    if (isHttp || isHttps) {
        if (prop->name == "createServer") {
            llvm::Value* options = getUndefinedValue();
            llvm::Value* callback = getUndefinedValue();

            if (node->arguments.size() == 1) {
                visit(node->arguments[0].get());
                auto argType = node->arguments[0]->inferredType;
                if (argType && argType->kind == TypeKind::Function) {
                    callback = boxValue(lastValue, argType);
                } else {
                    options = boxValue(lastValue, argType);
                }
            } else if (node->arguments.size() >= 2) {
                visit(node->arguments[0].get());
                options = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            const char* fnName = isHttps ? "ts_https_create_server" : "ts_http_create_server";
            llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
            lastValue = createCall(ft, fn.getCallee(), { options, callback });
            return true;
        } else if (prop->name == "request" || prop->name == "get") {
            if (node->arguments.empty()) return true;
            
            visit(node->arguments[0].get());
            llvm::Value* options = lastValue ? boxValue(lastValue, node->arguments[0]->inferredType) : getUndefinedValue();
            
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                callback = lastValue ? boxValue(lastValue, node->arguments[1]->inferredType) : getUndefinedValue();
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            const char* fnName;
            if (isHttps) {
                fnName = (prop->name == "request") ? "ts_https_request" : "ts_https_get";
            } else {
                fnName = (prop->name == "request") ? "ts_http_request" : "ts_http_get";
            }
            llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
            lastValue = createCall(ft, fn.getCallee(), { options, callback });
            return true;
        } else if (prop->name == "validateHeaderName") {
            // http.validateHeaderName(name: string) -> void
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http_validate_header_name", ft);
            createCall(ft, fn.getCallee(), { name });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        } else if (prop->name == "validateHeaderValue") {
            // http.validateHeaderValue(name: string, value: string) -> void
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            visit(node->arguments[1].get());
            llvm::Value* value = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http_validate_header_value", ft);
            createCall(ft, fn.getCallee(), { name, value });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        } else if (prop->name == "setMaxIdleHTTPParsers") {
            // http.setMaxIdleHTTPParsers(max: number) -> void
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* maxVal = lastValue;
            
            // Convert to i64 if needed
            if (!maxVal->getType()->isIntegerTy(64)) {
                if (maxVal->getType()->isIntegerTy()) {
                    maxVal = builder->CreateSExt(maxVal, builder->getInt64Ty());
                } else if (maxVal->getType()->isDoubleTy()) {
                    maxVal = builder->CreateFPToSI(maxVal, builder->getInt64Ty());
                }
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getInt64Ty() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http_set_max_idle_http_parsers", ft);
            createCall(ft, fn.getCallee(), { maxVal });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        } else if (prop->name == "getMaxIdleHTTPParsers") {
            // http.getMaxIdleHTTPParsers() -> number
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http_get_max_idle_http_parsers", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return true;
        }
    }

    if (isServer) {
        if (prop->name == "listen") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* server = lastValue;
            
            visit(node->arguments[0].get());
            llvm::Value* port = boxValue(lastValue, node->arguments[0]->inferredType);
            
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                callback = lastValue ? boxValue(lastValue, node->arguments[1]->inferredType) : getUndefinedValue();
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http_server_listen", ft);
            createCall(ft, fn.getCallee(), { server, port, callback });
            lastValue = server;
            return true;
        }
    }
    if (isServerResponse || isClientRequest) {
        if (prop->name == "writeHead" && isServerResponse) {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* boxedRes = lastValue;
            
            // For Any-typed expressions, we need to unbox as Object
            llvm::Value* res;
            if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
                llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_object", unboxFt);
                res = createCall(unboxFt, unboxFn.getCallee(), { boxedRes });
            } else {
                res = unboxValue(boxedRes, prop->expression->inferredType);
            }
            
            visit(node->arguments[0].get());
            llvm::Value* status = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            
            llvm::Value* headers = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                headers = lastValue ? boxValue(lastValue, node->arguments[1]->inferredType) : getUndefinedValue();
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http_server_response_write_head", ft);
            createCall(ft, fn.getCallee(), { res, status, headers });
            lastValue = boxedRes;
            return true;
        } else if (prop->name == "write") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* boxedRes = lastValue;
            
            // For Any-typed expressions, we need to unbox as Object
            llvm::Value* res;
            if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
                llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_object", unboxFt);
                res = createCall(unboxFt, unboxFn.getCallee(), { boxedRes });
            } else {
                res = unboxValue(boxedRes, prop->expression->inferredType);
            }
            if (!res) {
                SPDLOG_ERROR("tryGenerateHTTPCall: write: res is NULL!");
                return true;
            }
            visit(node->arguments[0].get());
            llvm::Value* data = lastValue ? boxValue(lastValue, node->arguments[0]->inferredType) : getUndefinedValue();
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_write", ft);
            createCall(ft, fn.getCallee(), { res, data });
            lastValue = boxedRes; // Return 'this' for chaining
            return true;
        } else if (prop->name == "end") {
            visit(prop->expression.get());
            llvm::Value* boxedRes = lastValue;
            
            // For Any-typed expressions, we need to unbox as Object
            llvm::Value* res;
            if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
                llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_object", unboxFt);
                res = createCall(unboxFt, unboxFn.getCallee(), { boxedRes });
            } else {
                res = unboxValue(boxedRes, prop->expression->inferredType);
            }
            if (!res) {
                SPDLOG_ERROR("tryGenerateHTTPCall: end: res is NULL!");
                return true;
            }
            
            llvm::Value* data = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                data = lastValue ? boxValue(lastValue, node->arguments[0]->inferredType) : getUndefinedValue();
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_end", ft);
            createCall(ft, fn.getCallee(), { res, data });
            lastValue = boxedRes; // Return 'this' for chaining
            return true;
        }
    }

    if (isAgent || isHttpsAgent) {
        if (prop->name == "destroy") {
            visit(prop->expression.get());
            llvm::Value* agent = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http_agent_destroy", ft);
            createCall(ft, fn.getCallee(), { agent });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
    }

    if (isWebSocket) {
        if (prop->name == "send") {
            visit(prop->expression.get());
            llvm::Value* ws = lastValue;
            
            llvm::Value* data = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                data = lastValue ? boxValue(lastValue, node->arguments[0]->inferredType) : getUndefinedValue();
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_send", ft);
            createCall(ft, fn.getCallee(), { ws, data });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        } else if (prop->name == "close") {
            visit(prop->expression.get());
            llvm::Value* ws = lastValue;
            
            llvm::Value* code = llvm::ConstantInt::get(builder->getInt64Ty(), 1000);
            llvm::Value* reason = llvm::ConstantPointerNull::get(builder->getPtrTy());
            
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                if (lastValue->getType()->isIntegerTy()) {
                    code = builder->CreateIntCast(lastValue, builder->getInt64Ty(), true);
                }
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                reason = lastValue;
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_close", ft);
            createCall(ft, fn.getCallee(), { ws, code, reason });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        } else if (prop->name == "ping") {
            visit(prop->expression.get());
            llvm::Value* ws = lastValue;
            
            llvm::Value* data = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                data = lastValue ? boxValue(lastValue, node->arguments[0]->inferredType) : getUndefinedValue();
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_ping", ft);
            createCall(ft, fn.getCallee(), { ws, data });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        } else if (prop->name == "pong") {
            visit(prop->expression.get());
            llvm::Value* ws = lastValue;
            
            llvm::Value* data = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                data = lastValue ? boxValue(lastValue, node->arguments[0]->inferredType) : getUndefinedValue();
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_pong", ft);
            createCall(ft, fn.getCallee(), { ws, data });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
    }

    return false;
}

bool IRGenerator::tryGenerateHTTPPropertyAccess(ast::PropertyAccessExpression* node) {
    // Check if this is http.METHODS, http.STATUS_CODES, or http.maxHeaderSize
    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());
    
    // First, check for WebSocket instance property access
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(node->expression->inferredType);
        if (classType->name == "WebSocket") {
            visit(node->expression.get());
            llvm::Value* ws = lastValue;
            
            if (node->name == "readyState") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_get_ready_state", ft);
                lastValue = createCall(ft, fn.getCallee(), { ws });
                return true;
            } else if (node->name == "url") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_get_url", ft);
                lastValue = createCall(ft, fn.getCallee(), { ws });
                return true;
            } else if (node->name == "protocol") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_get_protocol", ft);
                lastValue = createCall(ft, fn.getCallee(), { ws });
                return true;
            } else if (node->name == "extensions") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_get_extensions", ft);
                lastValue = createCall(ft, fn.getCallee(), { ws });
                return true;
            } else if (node->name == "bufferedAmount") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_get_buffered_amount", ft);
                lastValue = createCall(ft, fn.getCallee(), { ws });
                return true;
            } else if (node->name == "binaryType") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_get_binary_type", ft);
                lastValue = createCall(ft, fn.getCallee(), { ws });
                return true;
            } else if (node->name == "onopen") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_get_onopen", ft);
                lastValue = createCall(ft, fn.getCallee(), { ws });
                return true;
            } else if (node->name == "onmessage") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_get_onmessage", ft);
                lastValue = createCall(ft, fn.getCallee(), { ws });
                return true;
            } else if (node->name == "onclose") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_get_onclose", ft);
                lastValue = createCall(ft, fn.getCallee(), { ws });
                return true;
            } else if (node->name == "onerror") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_get_onerror", ft);
                lastValue = createCall(ft, fn.getCallee(), { ws });
                return true;
            } else if (node->name == "CONNECTING") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), {}, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_connecting", ft);
                lastValue = createCall(ft, fn.getCallee(), {});
                return true;
            } else if (node->name == "OPEN") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), {}, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_open", ft);
                lastValue = createCall(ft, fn.getCallee(), {});
                return true;
            } else if (node->name == "CLOSING") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), {}, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_closing", ft);
                lastValue = createCall(ft, fn.getCallee(), {});
                return true;
            } else if (node->name == "CLOSED") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), {}, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_closed", ft);
                lastValue = createCall(ft, fn.getCallee(), {});
                return true;
            }
        }
    }
    
    if (!id) return false;
    
    if (id->name != "http" && id->name != "https") return false;
    
    if (node->name == "METHODS") {
        // http.METHODS - returns array of HTTP method strings
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_http_get_methods", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    } else if (node->name == "STATUS_CODES") {
        // http.STATUS_CODES - returns object mapping status codes to descriptions
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_http_get_status_codes", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    } else if (node->name == "maxHeaderSize") {
        // http.maxHeaderSize - returns the max header size constant
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_http_get_max_header_size", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    } else if (node->name == "globalAgent") {
        // http.globalAgent or https.globalAgent
        const char* fnName = (id->name == "https") ? "ts_https_get_global_agent" : "ts_http_get_global_agent";
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    } else if (node->name == "Agent") {
        // http.Agent or https.Agent class reference - we'll handle constructor in NewExpression
        // Return a marker that represents the class
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    } else if (node->name == "WebSocket") {
        // http.WebSocket class reference - we'll handle constructor in NewExpression
        // Return a marker that represents the class
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }
    
    return false;
}

bool IRGenerator::tryGenerateNetPropertyAccess(ast::PropertyAccessExpression* node) {
    SPDLOG_INFO("tryGenerateNetPropertyAccess: node->name={}, exprType kind={}", 
        node->name, 
        node->expression->inferredType ? (int)node->expression->inferredType->kind : -1);
    
    // Check if this is net.* property access (for static methods/classes)
    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (id && id->name == "net") {
        // net.SocketAddress or net.BlockList static access is handled via call expressions
        // No static properties to handle here
        return false;
    }
    
    // Check if this is SocketAddress or BlockList instance property access
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(node->expression->inferredType);
        SPDLOG_INFO("tryGenerateNetPropertyAccess: classType->name={}", classType->name);
        
        if (classType->name == "SocketAddress") {
            visit(node->expression.get());
            llvm::Value* addr = lastValue;
            
            if (node->name == "address") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_address_get_address", ft);
                lastValue = createCall(ft, fn.getCallee(), { addr });
                return true;
            } else if (node->name == "family") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_address_get_family", ft);
                lastValue = createCall(ft, fn.getCallee(), { addr });
                return true;
            } else if (node->name == "flowlabel") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_address_get_flowlabel", ft);
                lastValue = createCall(ft, fn.getCallee(), { addr });
                return true;
            } else if (node->name == "port") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_address_get_port", ft);
                lastValue = createCall(ft, fn.getCallee(), { addr });
                return true;
            }
        }
        
        if (classType->name == "BlockList") {
            if (node->name == "rules") {
                visit(node->expression.get());
                llvm::Value* blockList = lastValue;
                
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_block_list_get_rules", ft);
                lastValue = createCall(ft, fn.getCallee(), { blockList });
                return true;
            }
        }
    }
    
    return false;
}

} // namespace ts
