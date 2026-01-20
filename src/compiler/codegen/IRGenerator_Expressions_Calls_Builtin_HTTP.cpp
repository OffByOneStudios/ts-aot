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

    // OutgoingMessage header methods
    bp.registerRuntimeApi("ts_outgoing_message_set_header", {true, false, true}, false);  // (msg, name, value) -> void
    bp.registerRuntimeApi("ts_outgoing_message_get_header", {true, false}, true);  // (msg, name) -> any
    bp.registerRuntimeApi("ts_outgoing_message_get_headers", {true}, true);  // (msg) -> object
    bp.registerRuntimeApi("ts_outgoing_message_has_header", {true, false}, false);  // (msg, name) -> bool
    bp.registerRuntimeApi("ts_outgoing_message_remove_header", {true, false}, false);  // (msg, name) -> void
    bp.registerRuntimeApi("ts_outgoing_message_get_header_names", {true}, true);  // (msg) -> string[]
    bp.registerRuntimeApi("ts_outgoing_message_flush_headers", {true}, false);  // (msg) -> void

    // OutgoingMessage property getters (headersSent, writableEnded, writableFinished)
    bp.registerRuntimeApi("ts_outgoing_message_get_headers_sent", {true}, false);  // (msg) -> bool
    bp.registerRuntimeApi("ts_outgoing_message_get_writable_ended", {true}, false);  // (msg) -> bool
    bp.registerRuntimeApi("ts_outgoing_message_get_writable_finished", {true}, false);  // (msg) -> bool

    // ServerResponse property getters/setters
    bp.registerRuntimeApi("ts_server_response_get_status_code", {true}, false);  // (res) -> int
    bp.registerRuntimeApi("ts_server_response_get_status_message", {true}, false);  // (res) -> string
    bp.registerRuntimeApi("ts_server_response_set_status_code", {true, false}, false);  // (res, code) -> void
    bp.registerRuntimeApi("ts_server_response_set_status_message", {true, false}, false);  // (res, msg) -> void

    // IncomingMessage property getters
    bp.registerRuntimeApi("ts_incoming_message_statusMessage", {true, true}, false);  // (ctx, msg) -> string
    bp.registerRuntimeApi("ts_incoming_message_httpVersion", {true, true}, false);  // (ctx, msg) -> string
    bp.registerRuntimeApi("ts_incoming_message_complete", {true, true}, false);  // (ctx, msg) -> bool
    bp.registerRuntimeApi("ts_incoming_message_rawHeaders", {true, true}, true);  // (ctx, msg) -> array
    bp.registerRuntimeApi("ts_incoming_message_rawTrailers", {true, true}, true);  // (ctx, msg) -> array
    bp.registerRuntimeApi("ts_incoming_message_trailers", {true, true}, true);  // (ctx, msg) -> object
    bp.registerRuntimeApi("ts_incoming_message_socket", {true, true}, false);  // (ctx, msg) -> socket
    bp.registerRuntimeApi("ts_incoming_message_aborted", {true, true}, false);  // (ctx, msg) -> bool

    // ServerResponse addTrailers
    bp.registerRuntimeApi("ts_server_response_add_trailers", {true, true}, false);  // (res, trailers) -> void

    // ClientRequest property getters and methods
    bp.registerRuntimeApi("ts_client_request_get_path", {true}, false);  // (req) -> string
    bp.registerRuntimeApi("ts_client_request_get_method", {true}, false);  // (req) -> string
    bp.registerRuntimeApi("ts_client_request_get_host", {true}, false);  // (req) -> string
    bp.registerRuntimeApi("ts_client_request_get_protocol", {true}, false);  // (req) -> string
    bp.registerRuntimeApi("ts_client_request_get_header", {true, false}, true);  // (req, name) -> any
    bp.registerRuntimeApi("ts_client_request_set_header", {true, false, true}, false);  // (req, name, value) -> void
    bp.registerRuntimeApi("ts_client_request_get_socket", {true}, false);  // (req) -> socket

    // Server timeout configuration
    bp.registerRuntimeApi("ts_http_server_set_timeout", {true, false, true}, true);  // (server, msecs, callback) -> server
    bp.registerRuntimeApi("ts_http_server_get_timeout", {true}, false);  // (server) -> int
    bp.registerRuntimeApi("ts_http_server_get_keep_alive_timeout", {true}, false);  // (server) -> int
    bp.registerRuntimeApi("ts_http_server_set_keep_alive_timeout", {true, false}, false);  // (server, msecs) -> void
    bp.registerRuntimeApi("ts_http_server_get_headers_timeout", {true}, false);  // (server) -> int
    bp.registerRuntimeApi("ts_http_server_set_headers_timeout", {true, false}, false);  // (server, msecs) -> void
    bp.registerRuntimeApi("ts_http_server_get_request_timeout", {true}, false);  // (server) -> int
    bp.registerRuntimeApi("ts_http_server_set_request_timeout", {true, false}, false);  // (server, msecs) -> void
    bp.registerRuntimeApi("ts_http_server_get_max_headers_count", {true}, false);  // (server) -> int
    bp.registerRuntimeApi("ts_http_server_set_max_headers_count", {true, false}, false);  // (server, count) -> void

    // ServerResponse timeout
    bp.registerRuntimeApi("ts_server_response_set_timeout", {true, false, true}, true);  // (res, msecs, callback) -> res

    // ClientRequest timeout and socket configuration
    bp.registerRuntimeApi("ts_client_request_set_timeout", {true, false, true}, true);  // (req, msecs, callback) -> req
    bp.registerRuntimeApi("ts_client_request_set_no_delay", {true, false}, false);  // (req, noDelay) -> void
    bp.registerRuntimeApi("ts_client_request_set_socket_keep_alive", {true, false, false}, false);  // (req, enable, initialDelay) -> void
    bp.registerRuntimeApi("ts_client_request_get_reused_socket", {true}, false);  // (req) -> bool
    bp.registerRuntimeApi("ts_client_request_get_max_headers_count", {true}, false);  // (req) -> int
    bp.registerRuntimeApi("ts_client_request_set_max_headers_count", {true, false}, false);  // (req, count) -> void
    bp.registerRuntimeApi("ts_client_request_get_raw_header_names", {true}, true);  // (req) -> string[]
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
                prop->name == "statusCode" || prop->name == "setHeader" ||
                prop->name == "getHeader" || prop->name == "getHeaders" || prop->name == "hasHeader" ||
                prop->name == "removeHeader" || prop->name == "getHeaderNames" || prop->name == "flushHeaders") {
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
        } else if (prop->name == "setTimeout") {
            // server.setTimeout(msecs, callback?) -> server
            visit(prop->expression.get());
            llvm::Value* server = lastValue;

            llvm::Value* msecs = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                msecs = castValue(lastValue, builder->getInt64Ty());
            }

            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                callback = lastValue ? boxValue(lastValue, node->arguments[1]->inferredType) : getUndefinedValue();
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http_server_set_timeout", ft);
            lastValue = createCall(ft, fn.getCallee(), { server, msecs, callback });
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
        } else if (prop->name == "setHeader") {
            if (node->arguments.size() < 2) return true;
            visit(prop->expression.get());
            llvm::Value* msg = lastValue;

            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            visit(node->arguments[1].get());
            llvm::Value* value = lastValue ? boxValue(lastValue, node->arguments[1]->inferredType) : getUndefinedValue();

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_outgoing_message_set_header", ft);
            createCall(ft, fn.getCallee(), { msg, name, value });
            lastValue = msg; // Return 'this' for chaining
            return true;
        } else if (prop->name == "getHeader") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* msg = lastValue;

            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_outgoing_message_get_header", ft);
            lastValue = createCall(ft, fn.getCallee(), { msg, name });
            return true;
        } else if (prop->name == "getHeaders") {
            visit(prop->expression.get());
            llvm::Value* msg = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_outgoing_message_get_headers", ft);
            lastValue = createCall(ft, fn.getCallee(), { msg });
            return true;
        } else if (prop->name == "hasHeader") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* msg = lastValue;

            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_outgoing_message_has_header", ft);
            lastValue = createCall(ft, fn.getCallee(), { msg, name });
            return true;
        } else if (prop->name == "removeHeader") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* msg = lastValue;

            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_outgoing_message_remove_header", ft);
            createCall(ft, fn.getCallee(), { msg, name });
            lastValue = msg; // Return 'this' for chaining
            return true;
        } else if (prop->name == "getHeaderNames") {
            visit(prop->expression.get());
            llvm::Value* msg = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_outgoing_message_get_header_names", ft);
            lastValue = createCall(ft, fn.getCallee(), { msg });
            return true;
        } else if (prop->name == "flushHeaders") {
            visit(prop->expression.get());
            llvm::Value* msg = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_outgoing_message_flush_headers", ft);
            createCall(ft, fn.getCallee(), { msg });
            lastValue = msg; // Return 'this' for chaining
            return true;
        } else if (prop->name == "setTimeout") {
            // res.setTimeout(msecs, callback?) or req.setTimeout(msecs, callback?)
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;

            llvm::Value* msecs = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                msecs = castValue(lastValue, builder->getInt64Ty());
            }

            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                callback = lastValue ? boxValue(lastValue, node->arguments[1]->inferredType) : getUndefinedValue();
            }

            const char* fnName = isServerResponse ? "ts_server_response_set_timeout" : "ts_client_request_set_timeout";
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction(fnName, ft);
            lastValue = createCall(ft, fn.getCallee(), { obj, msecs, callback });
            return true;
        } else if (prop->name == "setNoDelay" && isClientRequest) {
            // req.setNoDelay(noDelay)
            visit(prop->expression.get());
            llvm::Value* req = lastValue;

            llvm::Value* noDelay = llvm::ConstantInt::getTrue(*context);
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                noDelay = castValue(lastValue, builder->getInt1Ty());
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getInt1Ty() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_client_request_set_no_delay", ft);
            createCall(ft, fn.getCallee(), { req, noDelay });
            lastValue = req;
            return true;
        } else if (prop->name == "setSocketKeepAlive" && isClientRequest) {
            // req.setSocketKeepAlive(enable, initialDelay?)
            visit(prop->expression.get());
            llvm::Value* req = lastValue;

            llvm::Value* enable = llvm::ConstantInt::getFalse(*context);
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                enable = castValue(lastValue, builder->getInt1Ty());
            }

            llvm::Value* initialDelay = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                initialDelay = castValue(lastValue, builder->getInt64Ty());
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getInt1Ty(), builder->getInt64Ty() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_client_request_set_socket_keep_alive", ft);
            createCall(ft, fn.getCallee(), { req, enable, initialDelay });
            lastValue = req;
            return true;
        } else if (prop->name == "getRawHeaderNames" && isClientRequest) {
            // req.getRawHeaderNames() -> string[]
            visit(prop->expression.get());
            llvm::Value* req = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_client_request_get_raw_header_names", ft);
            lastValue = createCall(ft, fn.getCallee(), { req });
            return true;
        } else if (prop->name == "addTrailers" && isServerResponse) {
            // res.addTrailers(trailers)
            visit(prop->expression.get());
            llvm::Value* res = lastValue;

            llvm::Value* trailers = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                trailers = lastValue ? boxValue(lastValue, node->arguments[0]->inferredType) : getUndefinedValue();
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_server_response_add_trailers", ft);
            createCall(ft, fn.getCallee(), { res, trailers });
            lastValue = res;  // Return 'this' for chaining
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

    // First, check for ServerResponse, IncomingMessage, ClientRequest, or WebSocket instance property access
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(node->expression->inferredType);

        // ServerResponse properties (headersSent, writableEnded, writableFinished, statusCode)
        if (classType->name == "ServerResponse" || classType->name == "OutgoingMessage" || classType->name == "ClientRequest") {
            visit(node->expression.get());
            llvm::Value* res = lastValue;

            if (node->name == "headersSent") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_outgoing_message_get_headers_sent", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "writableEnded") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_outgoing_message_get_writable_ended", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "writableFinished") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_outgoing_message_get_writable_finished", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "statusCode" && classType->name == "ServerResponse") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_server_response_get_status_code", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "statusMessage" && classType->name == "ServerResponse") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_server_response_get_status_message", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "path" && classType->name == "ClientRequest") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_client_request_get_path", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "method" && classType->name == "ClientRequest") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_client_request_get_method", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "host" && classType->name == "ClientRequest") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_client_request_get_host", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "protocol" && classType->name == "ClientRequest") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_client_request_get_protocol", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "socket" && classType->name == "ClientRequest") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_client_request_get_socket", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "reusedSocket" && classType->name == "ClientRequest") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_client_request_get_reused_socket", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            } else if (node->name == "maxHeadersCount" && classType->name == "ClientRequest") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_client_request_get_max_headers_count", ft);
                lastValue = createCall(ft, fn.getCallee(), { res });
                return true;
            }
            // Let other properties fall through to generic object property access
        }

        // Server timeout properties
        if (classType->name == "Server") {
            visit(node->expression.get());
            llvm::Value* server = lastValue;

            if (node->name == "timeout") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http_server_get_timeout", ft);
                lastValue = createCall(ft, fn.getCallee(), { server });
                return true;
            } else if (node->name == "keepAliveTimeout") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http_server_get_keep_alive_timeout", ft);
                lastValue = createCall(ft, fn.getCallee(), { server });
                return true;
            } else if (node->name == "headersTimeout") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http_server_get_headers_timeout", ft);
                lastValue = createCall(ft, fn.getCallee(), { server });
                return true;
            } else if (node->name == "requestTimeout") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http_server_get_request_timeout", ft);
                lastValue = createCall(ft, fn.getCallee(), { server });
                return true;
            } else if (node->name == "maxHeadersCount") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http_server_get_max_headers_count", ft);
                lastValue = createCall(ft, fn.getCallee(), { server });
                return true;
            }
        }

        // IncomingMessage properties (statusMessage, httpVersion, complete, rawHeaders)
        if (classType->name == "IncomingMessage") {
            visit(node->expression.get());
            llvm::Value* msg = lastValue;

            if (node->name == "statusMessage") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_incoming_message_statusMessage", ft);
                llvm::Value* ctx = llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(ft, fn.getCallee(), { ctx, msg });
                return true;
            } else if (node->name == "httpVersion") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_incoming_message_httpVersion", ft);
                llvm::Value* ctx = llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(ft, fn.getCallee(), { ctx, msg });
                return true;
            } else if (node->name == "complete") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_incoming_message_complete", ft);
                llvm::Value* ctx = llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(ft, fn.getCallee(), { ctx, msg });
                return true;
            } else if (node->name == "rawHeaders") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_incoming_message_rawHeaders", ft);
                llvm::Value* ctx = llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(ft, fn.getCallee(), { ctx, msg });
                return true;
            } else if (node->name == "rawTrailers") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_incoming_message_rawTrailers", ft);
                llvm::Value* ctx = llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(ft, fn.getCallee(), { ctx, msg });
                return true;
            } else if (node->name == "trailers") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_incoming_message_trailers", ft);
                llvm::Value* ctx = llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(ft, fn.getCallee(), { ctx, msg });
                return true;
            } else if (node->name == "socket") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_incoming_message_socket", ft);
                llvm::Value* ctx = llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(ft, fn.getCallee(), { ctx, msg });
                return true;
            } else if (node->name == "aborted") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_incoming_message_aborted", ft);
                llvm::Value* ctx = llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(ft, fn.getCallee(), { ctx, msg });
                return true;
            }
            // Let other properties fall through to generic object property access
        }

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

        // Socket address properties (remoteAddress, remotePort, remoteFamily, localAddress, localPort, localFamily)
        if (classType->name == "Socket") {
            visit(node->expression.get());
            llvm::Value* socket = lastValue;

            if (node->name == "remoteAddress") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_remote_address", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "remotePort") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_remote_port", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "remoteFamily") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_remote_family", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "localAddress") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_local_address", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "localPort") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_local_port", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "localFamily") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_local_family", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "bytesRead") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_bytes_read", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "bytesWritten") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_bytes_written", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "connecting") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_connecting", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "destroyed") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_destroyed", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "pending") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_pending", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            } else if (node->name == "readyState") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_get_ready_state", ft);
                lastValue = createCall(ft, fn.getCallee(), { socket });
                return true;
            }
        }

        // Server properties
        if (classType->name == "Server") {
            visit(node->expression.get());
            llvm::Value* server = lastValue;

            if (node->name == "maxConnections") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_net_server_get_maxConnections", ft);
                lastValue = createCall(ft, fn.getCallee(), { server });
                return true;
            }
        }
    }

    return false;
}

} // namespace ts
