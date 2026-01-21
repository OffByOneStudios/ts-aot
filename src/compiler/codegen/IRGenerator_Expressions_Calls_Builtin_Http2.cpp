#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register HTTP/2 module's runtime functions
static bool http2FunctionsRegistered = false;
static void ensureHTTP2FunctionsRegistered(BoxingPolicy& bp) {
    if (http2FunctionsRegistered) return;
    http2FunctionsRegistered = true;

    // Module functions
    bp.registerRuntimeApi("ts_http2_create_server", {true, true}, true);  // options, callback -> Server
    bp.registerRuntimeApi("ts_http2_create_secure_server", {true, true}, true);  // options, callback -> SecureServer
    bp.registerRuntimeApi("ts_http2_connect", {true, true, true}, true);  // authority, options, callback -> ClientSession
    bp.registerRuntimeApi("ts_http2_get_default_settings", {}, true);  // -> settings object
    bp.registerRuntimeApi("ts_http2_get_packed_settings", {true}, true);  // settings -> Buffer
    bp.registerRuntimeApi("ts_http2_get_unpacked_settings", {true}, true);  // buffer -> settings
    bp.registerRuntimeApi("ts_http2_get_constants", {}, true);  // -> constants object

    // Session property getters
    bp.registerRuntimeApi("ts_http2_session_get_alpn_protocol", {true}, false);  // session -> string
    bp.registerRuntimeApi("ts_http2_session_get_closed", {true}, false);  // session -> bool
    bp.registerRuntimeApi("ts_http2_session_get_connecting", {true}, false);  // session -> bool
    bp.registerRuntimeApi("ts_http2_session_get_destroyed", {true}, false);  // session -> bool
    bp.registerRuntimeApi("ts_http2_session_get_encrypted", {true}, false);  // session -> bool
    bp.registerRuntimeApi("ts_http2_session_get_local_settings", {true}, true);  // session -> settings
    bp.registerRuntimeApi("ts_http2_session_get_remote_settings", {true}, true);  // session -> settings
    bp.registerRuntimeApi("ts_http2_session_get_socket", {true}, false);  // session -> socket
    bp.registerRuntimeApi("ts_http2_session_get_type", {true}, false);  // session -> int
    bp.registerRuntimeApi("ts_http2_session_get_state", {true}, true);  // session -> state object

    // Session methods
    bp.registerRuntimeApi("ts_http2_session_close", {true, true}, false);  // session, callback -> void
    bp.registerRuntimeApi("ts_http2_session_destroy", {true, true, false}, false);  // session, error, code -> void
    bp.registerRuntimeApi("ts_http2_session_goaway", {true, false, false, true}, false);  // session, code, lastStreamId, data
    bp.registerRuntimeApi("ts_http2_session_ping", {true, true, true}, false);  // session, payload, callback -> bool
    bp.registerRuntimeApi("ts_http2_session_ref", {true}, false);  // session -> void
    bp.registerRuntimeApi("ts_http2_session_unref", {true}, false);  // session -> void
    bp.registerRuntimeApi("ts_http2_session_set_timeout", {true, false, true}, false);  // session, msecs, callback
    bp.registerRuntimeApi("ts_http2_session_settings", {true, true}, false);  // session, settings

    // ServerHttp2Session specific
    bp.registerRuntimeApi("ts_http2_server_session_altsvc", {true, true, true}, false);  // session, alt, origin
    bp.registerRuntimeApi("ts_http2_server_session_origin", {true, true}, false);  // session, origins

    // ClientHttp2Session specific
    bp.registerRuntimeApi("ts_http2_client_session_request", {true, true, true}, true);  // session, headers, options -> stream

    // Stream property getters
    bp.registerRuntimeApi("ts_http2_stream_get_aborted", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_http2_stream_get_buffer_size", {true}, false);  // stream -> int
    bp.registerRuntimeApi("ts_http2_stream_get_closed", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_http2_stream_get_destroyed", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_http2_stream_get_end_after_headers", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_http2_stream_get_id", {true}, false);  // stream -> int
    bp.registerRuntimeApi("ts_http2_stream_get_pending", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_http2_stream_get_rst_code", {true}, false);  // stream -> int
    bp.registerRuntimeApi("ts_http2_stream_get_sent_headers", {true}, true);  // stream -> headers
    bp.registerRuntimeApi("ts_http2_stream_get_sent_info_headers", {true}, true);  // stream -> array
    bp.registerRuntimeApi("ts_http2_stream_get_sent_trailers", {true}, true);  // stream -> trailers
    bp.registerRuntimeApi("ts_http2_stream_get_session", {true}, false);  // stream -> session
    bp.registerRuntimeApi("ts_http2_stream_get_state", {true}, true);  // stream -> state object

    // Stream methods
    bp.registerRuntimeApi("ts_http2_stream_close", {true, false, true}, false);  // stream, code, callback
    bp.registerRuntimeApi("ts_http2_stream_priority", {true, true}, false);  // stream, options
    bp.registerRuntimeApi("ts_http2_stream_set_timeout", {true, false, true}, false);  // stream, msecs, callback

    // ServerHttp2Stream specific
    bp.registerRuntimeApi("ts_http2_server_stream_get_headers_sent", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_http2_server_stream_get_push_allowed", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_http2_server_stream_additional_headers", {true, true}, false);  // stream, headers
    bp.registerRuntimeApi("ts_http2_server_stream_push_stream", {true, true, true, true}, false);  // stream, headers, options, callback
    bp.registerRuntimeApi("ts_http2_server_stream_respond", {true, true, true}, false);  // stream, headers, options
    bp.registerRuntimeApi("ts_http2_server_stream_respond_with_fd", {true, false, true, true}, false);  // stream, fd, headers, options
    bp.registerRuntimeApi("ts_http2_server_stream_respond_with_file", {true, true, true, true}, false);  // stream, path, headers, options

    // Http2Server methods (inherits from TsServer)
    bp.registerRuntimeApi("ts_http2_server_listen", {true, false, true, true}, true);  // server, port, host, callback -> server
    bp.registerRuntimeApi("ts_http2_server_close", {true, true}, false);  // server, callback -> void
    bp.registerRuntimeApi("ts_http2_server_address", {true}, true);  // server -> address object
    bp.registerRuntimeApi("ts_http2_server_set_timeout", {true, false, true}, false);  // server, msecs, callback
}

bool IRGenerator::tryGenerateHTTP2Call(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureHTTP2FunctionsRegistered(boxingPolicy);

    SPDLOG_INFO("tryGenerateHTTP2Call: prop->name={}", prop->name);

    bool isHttp2Module = false;
    bool isHttp2Session = false;
    bool isServerHttp2Session = false;
    bool isClientHttp2Session = false;
    bool isHttp2Stream = false;
    bool isServerHttp2Stream = false;
    bool isClientHttp2Stream = false;
    bool isHttp2Server = false;

    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "http2") isHttp2Module = true;
    }

    if (!isHttp2Module && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
        SPDLOG_INFO("tryGenerateHTTP2Call: classType->name={}", classType->name);

        if (classType->name == "Http2Session") isHttp2Session = true;
        else if (classType->name == "ServerHttp2Session") { isServerHttp2Session = true; isHttp2Session = true; }
        else if (classType->name == "ClientHttp2Session") { isClientHttp2Session = true; isHttp2Session = true; }
        else if (classType->name == "Http2Stream") isHttp2Stream = true;
        else if (classType->name == "ServerHttp2Stream") { isServerHttp2Stream = true; isHttp2Stream = true; }
        else if (classType->name == "ClientHttp2Stream") { isClientHttp2Stream = true; isHttp2Stream = true; }
        else if (classType->name == "Http2Server" || classType->name == "Http2SecureServer") isHttp2Server = true;
    }

    if (!isHttp2Module && !isHttp2Session && !isHttp2Stream && !isHttp2Server) return false;

    // =========================================================================
    // http2 module functions
    // =========================================================================
    if (isHttp2Module) {
        if (prop->name == "createServer") {
            llvm::Value* options = getUndefinedValue();
            llvm::Value* callback = getUndefinedValue();

            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                auto argType = node->arguments[0]->inferredType;
                if (argType && argType->kind == TypeKind::Function) {
                    callback = boxValue(lastValue, argType);
                } else {
                    options = boxValue(lastValue, argType);
                }
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_create_server", ft);
            lastValue = createCall(ft, fn.getCallee(), { options, callback });
            return true;
        }
        else if (prop->name == "createSecureServer") {
            llvm::Value* options = getUndefinedValue();
            llvm::Value* callback = getUndefinedValue();

            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                options = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_create_secure_server", ft);
            lastValue = createCall(ft, fn.getCallee(), { options, callback });
            return true;
        }
        else if (prop->name == "connect") {
            if (node->arguments.empty()) return true;

            visit(node->arguments[0].get());
            llvm::Value* authority = boxValue(lastValue, node->arguments[0]->inferredType);

            llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                auto argType = node->arguments[1]->inferredType;
                if (argType && argType->kind == TypeKind::Function) {
                    callback = boxValue(lastValue, argType);
                } else {
                    options = boxValue(lastValue, argType);
                }
            }
            if (node->arguments.size() >= 3) {
                visit(node->arguments[2].get());
                callback = boxValue(lastValue, node->arguments[2]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_connect", ft);
            lastValue = createCall(ft, fn.getCallee(), { authority, options, callback });
            return true;
        }
        else if (prop->name == "getDefaultSettings") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_get_default_settings", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return true;
        }
        else if (prop->name == "getPackedSettings") {
            if (node->arguments.empty()) return true;

            visit(node->arguments[0].get());
            llvm::Value* settings = boxValue(lastValue, node->arguments[0]->inferredType);

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_get_packed_settings", ft);
            lastValue = createCall(ft, fn.getCallee(), { settings });
            return true;
        }
        else if (prop->name == "getUnpackedSettings") {
            if (node->arguments.empty()) return true;

            visit(node->arguments[0].get());
            llvm::Value* buffer = boxValue(lastValue, node->arguments[0]->inferredType);

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_get_unpacked_settings", ft);
            lastValue = createCall(ft, fn.getCallee(), { buffer });
            return true;
        }
    }

    // =========================================================================
    // Http2Session methods
    // =========================================================================
    if (isHttp2Session) {
        if (prop->name == "close") {
            visit(prop->expression.get());
            llvm::Value* session = lastValue;

            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                callback = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_session_close", ft);
            createCall(ft, fn.getCallee(), { session, callback });
            lastValue = session;
            return true;
        }
        else if (prop->name == "destroy") {
            visit(prop->expression.get());
            llvm::Value* session = lastValue;

            llvm::Value* error = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* code = llvm::ConstantInt::get(builder->getInt64Ty(), 0);

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                error = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                code = castValue(lastValue, builder->getInt64Ty());
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getInt64Ty() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_session_destroy", ft);
            createCall(ft, fn.getCallee(), { session, error, code });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        else if (prop->name == "goaway") {
            visit(prop->expression.get());
            llvm::Value* session = lastValue;

            llvm::Value* code = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            llvm::Value* lastStreamId = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            llvm::Value* data = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                code = castValue(lastValue, builder->getInt64Ty());
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                lastStreamId = castValue(lastValue, builder->getInt64Ty());
            }
            if (node->arguments.size() >= 3) {
                visit(node->arguments[2].get());
                data = boxValue(lastValue, node->arguments[2]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_session_goaway", ft);
            createCall(ft, fn.getCallee(), { session, code, lastStreamId, data });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        else if (prop->name == "ping") {
            visit(prop->expression.get());
            llvm::Value* session = lastValue;

            llvm::Value* payload = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                auto argType = node->arguments[0]->inferredType;
                if (argType && argType->kind == TypeKind::Function) {
                    callback = boxValue(lastValue, argType);
                } else {
                    payload = boxValue(lastValue, argType);
                }
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_session_ping", ft);
            lastValue = createCall(ft, fn.getCallee(), { session, payload, callback });
            return true;
        }
        else if (prop->name == "ref") {
            visit(prop->expression.get());
            llvm::Value* session = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_session_ref", ft);
            createCall(ft, fn.getCallee(), { session });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        else if (prop->name == "unref") {
            visit(prop->expression.get());
            llvm::Value* session = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_session_unref", ft);
            createCall(ft, fn.getCallee(), { session });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        else if (prop->name == "setTimeout") {
            visit(prop->expression.get());
            llvm::Value* session = lastValue;

            llvm::Value* msecs = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                msecs = castValue(lastValue, builder->getInt64Ty());
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_session_set_timeout", ft);
            createCall(ft, fn.getCallee(), { session, msecs, callback });
            lastValue = session;
            return true;
        }
        else if (prop->name == "settings") {
            visit(prop->expression.get());
            llvm::Value* session = lastValue;

            llvm::Value* settings = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                settings = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_session_settings", ft);
            createCall(ft, fn.getCallee(), { session, settings });
            lastValue = session;
            return true;
        }

        // ServerHttp2Session-specific methods
        if (isServerHttp2Session) {
            if (prop->name == "altsvc") {
                visit(prop->expression.get());
                llvm::Value* session = lastValue;

                if (node->arguments.size() < 2) return true;

                visit(node->arguments[0].get());
                llvm::Value* alt = boxValue(lastValue, node->arguments[0]->inferredType);
                visit(node->arguments[1].get());
                llvm::Value* origin = boxValue(lastValue, node->arguments[1]->inferredType);

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_session_altsvc", ft);
                createCall(ft, fn.getCallee(), { session, alt, origin });
                lastValue = session;
                return true;
            }
            else if (prop->name == "origin") {
                visit(prop->expression.get());
                llvm::Value* session = lastValue;

                llvm::Value* origins = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    origins = boxValue(lastValue, node->arguments[0]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_session_origin", ft);
                createCall(ft, fn.getCallee(), { session, origins });
                lastValue = session;
                return true;
            }
        }

        // ClientHttp2Session-specific methods
        if (isClientHttp2Session) {
            if (prop->name == "request") {
                visit(prop->expression.get());
                llvm::Value* session = lastValue;

                llvm::Value* headers = llvm::ConstantPointerNull::get(builder->getPtrTy());
                llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());

                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    headers = boxValue(lastValue, node->arguments[0]->inferredType);
                }
                if (node->arguments.size() >= 2) {
                    visit(node->arguments[1].get());
                    options = boxValue(lastValue, node->arguments[1]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_client_session_request", ft);
                lastValue = createCall(ft, fn.getCallee(), { session, headers, options });
                return true;
            }
        }
    }

    // =========================================================================
    // Http2Stream methods
    // =========================================================================
    if (isHttp2Stream) {
        if (prop->name == "close") {
            visit(prop->expression.get());
            llvm::Value* stream = lastValue;

            llvm::Value* code = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                code = castValue(lastValue, builder->getInt64Ty());
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_stream_close", ft);
            createCall(ft, fn.getCallee(), { stream, code, callback });
            lastValue = stream;
            return true;
        }
        else if (prop->name == "priority") {
            visit(prop->expression.get());
            llvm::Value* stream = lastValue;

            llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                options = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_stream_priority", ft);
            createCall(ft, fn.getCallee(), { stream, options });
            lastValue = stream;
            return true;
        }
        else if (prop->name == "setTimeout") {
            visit(prop->expression.get());
            llvm::Value* stream = lastValue;

            llvm::Value* msecs = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                msecs = castValue(lastValue, builder->getInt64Ty());
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_stream_set_timeout", ft);
            createCall(ft, fn.getCallee(), { stream, msecs, callback });
            lastValue = stream;
            return true;
        }

        // ServerHttp2Stream-specific methods
        if (isServerHttp2Stream) {
            if (prop->name == "additionalHeaders") {
                visit(prop->expression.get());
                llvm::Value* stream = lastValue;

                llvm::Value* headers = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    headers = boxValue(lastValue, node->arguments[0]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_stream_additional_headers", ft);
                createCall(ft, fn.getCallee(), { stream, headers });
                lastValue = stream;
                return true;
            }
            else if (prop->name == "pushStream") {
                visit(prop->expression.get());
                llvm::Value* stream = lastValue;

                llvm::Value* headers = llvm::ConstantPointerNull::get(builder->getPtrTy());
                llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
                llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    headers = boxValue(lastValue, node->arguments[0]->inferredType);
                }
                if (node->arguments.size() >= 2) {
                    visit(node->arguments[1].get());
                    options = boxValue(lastValue, node->arguments[1]->inferredType);
                }
                if (node->arguments.size() >= 3) {
                    visit(node->arguments[2].get());
                    callback = boxValue(lastValue, node->arguments[2]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_stream_push_stream", ft);
                createCall(ft, fn.getCallee(), { stream, headers, options, callback });
                lastValue = stream;
                return true;
            }
            else if (prop->name == "respond") {
                visit(prop->expression.get());
                llvm::Value* stream = lastValue;

                llvm::Value* headers = llvm::ConstantPointerNull::get(builder->getPtrTy());
                llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());

                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    headers = boxValue(lastValue, node->arguments[0]->inferredType);
                }
                if (node->arguments.size() >= 2) {
                    visit(node->arguments[1].get());
                    options = boxValue(lastValue, node->arguments[1]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_stream_respond", ft);
                createCall(ft, fn.getCallee(), { stream, headers, options });
                lastValue = stream;
                return true;
            }
            else if (prop->name == "respondWithFD") {
                visit(prop->expression.get());
                llvm::Value* stream = lastValue;

                if (node->arguments.empty()) return true;

                visit(node->arguments[0].get());
                llvm::Value* fd = castValue(lastValue, builder->getInt64Ty());

                llvm::Value* headers = llvm::ConstantPointerNull::get(builder->getPtrTy());
                llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());

                if (node->arguments.size() >= 2) {
                    visit(node->arguments[1].get());
                    headers = boxValue(lastValue, node->arguments[1]->inferredType);
                }
                if (node->arguments.size() >= 3) {
                    visit(node->arguments[2].get());
                    options = boxValue(lastValue, node->arguments[2]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_stream_respond_with_fd", ft);
                createCall(ft, fn.getCallee(), { stream, fd, headers, options });
                lastValue = stream;
                return true;
            }
            else if (prop->name == "respondWithFile") {
                visit(prop->expression.get());
                llvm::Value* stream = lastValue;

                if (node->arguments.empty()) return true;

                visit(node->arguments[0].get());
                llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);

                llvm::Value* headers = llvm::ConstantPointerNull::get(builder->getPtrTy());
                llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());

                if (node->arguments.size() >= 2) {
                    visit(node->arguments[1].get());
                    headers = boxValue(lastValue, node->arguments[1]->inferredType);
                }
                if (node->arguments.size() >= 3) {
                    visit(node->arguments[2].get());
                    options = boxValue(lastValue, node->arguments[2]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_stream_respond_with_file", ft);
                createCall(ft, fn.getCallee(), { stream, path, headers, options });
                lastValue = stream;
                return true;
            }
        }
    }

    // =========================================================================
    // Http2Server methods (listen, close, address, setTimeout)
    // =========================================================================
    if (isHttp2Server) {
        if (prop->name == "listen") {
            visit(prop->expression.get());
            llvm::Value* server = lastValue;

            llvm::Value* port = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            llvm::Value* host = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            for (size_t i = 0; i < node->arguments.size(); i++) {
                visit(node->arguments[i].get());
                auto argType = node->arguments[i]->inferredType;
                if (argType && argType->kind == TypeKind::Function) {
                    callback = boxValue(lastValue, argType);
                } else if (argType && (argType->kind == TypeKind::Int || argType->kind == TypeKind::Double)) {
                    port = castValue(lastValue, builder->getInt64Ty());
                } else if (argType && argType->kind == TypeKind::String) {
                    host = boxValue(lastValue, argType);
                }
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_listen", ft);
            lastValue = createCall(ft, fn.getCallee(), { server, port, host, callback });
            return true;
        }
        else if (prop->name == "close") {
            visit(prop->expression.get());
            llvm::Value* server = lastValue;

            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                callback = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_close", ft);
            createCall(ft, fn.getCallee(), { server, callback });
            lastValue = server;
            return true;
        }
        else if (prop->name == "address") {
            visit(prop->expression.get());
            llvm::Value* server = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_address", ft);
            lastValue = createCall(ft, fn.getCallee(), { server });
            return true;
        }
        else if (prop->name == "setTimeout") {
            visit(prop->expression.get());
            llvm::Value* server = lastValue;

            llvm::Value* msecs = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                msecs = castValue(lastValue, builder->getInt64Ty());
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http2_server_set_timeout", ft);
            createCall(ft, fn.getCallee(), { server, msecs, callback });
            lastValue = server;
            return true;
        }
        // EventEmitter methods (on, once, emit, etc.) are handled by tryGenerateEventEmitterCall
    }

    return false;
}

bool IRGenerator::tryGenerateHTTP2PropertyAccess(ast::PropertyAccessExpression* node) {
    // Check if this is http2.constants or http2.* access
    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());

    // First check for class instance property access
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(node->expression->inferredType);

        // Http2Session properties
        if (classType->name == "Http2Session" || classType->name == "ServerHttp2Session" || classType->name == "ClientHttp2Session") {
            visit(node->expression.get());
            llvm::Value* session = lastValue;

            if (node->name == "alpnProtocol") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_session_get_alpn_protocol", ft);
                lastValue = createCall(ft, fn.getCallee(), { session });
                return true;
            } else if (node->name == "closed") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_session_get_closed", ft);
                lastValue = createCall(ft, fn.getCallee(), { session });
                return true;
            } else if (node->name == "connecting") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_session_get_connecting", ft);
                lastValue = createCall(ft, fn.getCallee(), { session });
                return true;
            } else if (node->name == "destroyed") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_session_get_destroyed", ft);
                lastValue = createCall(ft, fn.getCallee(), { session });
                return true;
            } else if (node->name == "encrypted") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_session_get_encrypted", ft);
                lastValue = createCall(ft, fn.getCallee(), { session });
                return true;
            } else if (node->name == "localSettings") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_session_get_local_settings", ft);
                lastValue = createCall(ft, fn.getCallee(), { session });
                return true;
            } else if (node->name == "remoteSettings") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_session_get_remote_settings", ft);
                lastValue = createCall(ft, fn.getCallee(), { session });
                return true;
            } else if (node->name == "socket") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_session_get_socket", ft);
                lastValue = createCall(ft, fn.getCallee(), { session });
                return true;
            } else if (node->name == "type") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_session_get_type", ft);
                lastValue = createCall(ft, fn.getCallee(), { session });
                return true;
            }
        }

        // Http2Stream properties
        if (classType->name == "Http2Stream" || classType->name == "ServerHttp2Stream" || classType->name == "ClientHttp2Stream") {
            visit(node->expression.get());
            llvm::Value* stream = lastValue;

            if (node->name == "aborted") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_stream_get_aborted", ft);
                lastValue = createCall(ft, fn.getCallee(), { stream });
                return true;
            } else if (node->name == "bufferSize") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_stream_get_buffer_size", ft);
                lastValue = createCall(ft, fn.getCallee(), { stream });
                return true;
            } else if (node->name == "closed") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_stream_get_closed", ft);
                lastValue = createCall(ft, fn.getCallee(), { stream });
                return true;
            } else if (node->name == "destroyed") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_stream_get_destroyed", ft);
                lastValue = createCall(ft, fn.getCallee(), { stream });
                return true;
            } else if (node->name == "endAfterHeaders") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_stream_get_end_after_headers", ft);
                lastValue = createCall(ft, fn.getCallee(), { stream });
                return true;
            } else if (node->name == "id") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_stream_get_id", ft);
                lastValue = createCall(ft, fn.getCallee(), { stream });
                return true;
            } else if (node->name == "pending") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_stream_get_pending", ft);
                lastValue = createCall(ft, fn.getCallee(), { stream });
                return true;
            } else if (node->name == "rstCode") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_stream_get_rst_code", ft);
                lastValue = createCall(ft, fn.getCallee(), { stream });
                return true;
            } else if (node->name == "sentHeaders") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_stream_get_sent_headers", ft);
                lastValue = createCall(ft, fn.getCallee(), { stream });
                return true;
            } else if (node->name == "session") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_stream_get_session", ft);
                lastValue = createCall(ft, fn.getCallee(), { stream });
                return true;
            }

            // ServerHttp2Stream-specific properties
            if (classType->name == "ServerHttp2Stream") {
                if (node->name == "headersSent") {
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_server_stream_get_headers_sent", ft);
                    lastValue = createCall(ft, fn.getCallee(), { stream });
                    return true;
                } else if (node->name == "pushAllowed") {
                    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_server_stream_get_push_allowed", ft);
                    lastValue = createCall(ft, fn.getCallee(), { stream });
                    return true;
                }
            }
        }
    }

    if (!id) return false;
    if (id->name != "http2") return false;

    if (node->name == "constants") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_get_constants", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    } else if (node->name == "sensitiveHeaders") {
        // Return the sensitiveHeaders symbol
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http2_get_sensitive_headers", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    return false;
}

} // namespace ts
