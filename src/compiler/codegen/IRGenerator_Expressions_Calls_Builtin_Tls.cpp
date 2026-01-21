#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register TLS module's runtime functions once
static bool tlsFunctionsRegistered = false;
static void ensureTlsFunctionsRegistered(BoxingPolicy& bp) {
    if (tlsFunctionsRegistered) return;
    tlsFunctionsRegistered = true;

    // TLS module functions
    bp.registerRuntimeApi("ts_tls_create_secure_context", {true}, true);  // options -> boxed SecureContext
    bp.registerRuntimeApi("ts_tls_connect", {true, true}, true);          // options, callback -> boxed TLSSocket
    bp.registerRuntimeApi("ts_tls_create_server", {true, true}, true);    // options, callback -> boxed Server
    bp.registerRuntimeApi("ts_tls_get_ciphers", {}, true);                // -> boxed string array

    // TLSSocket property getters
    bp.registerRuntimeApi("ts_secure_socket_get_authorized", {true}, false);           // socket -> bool
    bp.registerRuntimeApi("ts_secure_socket_get_authorization_error", {true}, true);   // socket -> boxed string
    bp.registerRuntimeApi("ts_secure_socket_get_encrypted", {true}, false);            // socket -> bool
    bp.registerRuntimeApi("ts_secure_socket_get_certificate", {true}, true);           // socket -> boxed object
    bp.registerRuntimeApi("ts_secure_socket_get_peer_certificate", {true, false}, true); // socket, detailed -> boxed object
    bp.registerRuntimeApi("ts_secure_socket_get_protocol", {true}, true);              // socket -> boxed string

    // TLSSocket methods
    bp.registerRuntimeApi("ts_secure_socket_get_session", {true}, true);               // socket -> boxed Buffer
    bp.registerRuntimeApi("ts_secure_socket_renegotiate", {true, true, true}, false);  // socket, options, callback -> bool
    bp.registerRuntimeApi("ts_secure_socket_set_max_send_fragment", {true, false}, false); // socket, size -> bool

    // SNI (Server Name Indication)
    bp.registerRuntimeApi("ts_secure_socket_set_servername", {true, true}, false);     // socket, hostname -> void
    bp.registerRuntimeApi("ts_secure_socket_get_servername", {true}, true);            // socket -> boxed string

    // ALPN (Application-Layer Protocol Negotiation)
    bp.registerRuntimeApi("ts_secure_socket_set_alpn_protocols", {true, true}, false); // socket, protocols -> void
    bp.registerRuntimeApi("ts_secure_socket_get_alpn_protocol", {true}, true);         // socket -> boxed string

    // Session resumption
    bp.registerRuntimeApi("ts_secure_socket_set_session", {true, true}, false);        // socket, session -> void
    bp.registerRuntimeApi("ts_secure_socket_is_session_reused", {true}, false);        // socket -> bool

    // Client certificate
    bp.registerRuntimeApi("ts_secure_socket_set_client_certificate", {true, true, true}, false); // socket, cert, key -> void
}

bool IRGenerator::tryGenerateTlsCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureTlsFunctionsRegistered(boxingPolicy);

    // Check if this is a tls.* call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "tls") return false;

    SPDLOG_DEBUG("tryGenerateTlsCall: tls.{}", prop->name);

    if (prop->name == "createSecureContext") {
        // tls.createSecureContext(options): SecureContext
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tls_create_secure_context", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        boxedValues.insert(lastValue);
        return true;
    }

    if (prop->name == "connect") {
        // tls.connect(options, callback?): TLSSocket
        // tls.connect(port, host?, options?, callback?): TLSSocket
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            callback = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tls_connect", ft);
        lastValue = createCall(ft, fn.getCallee(), { options, callback });
        boxedValues.insert(lastValue);
        return true;
    }

    if (prop->name == "createServer") {
        // tls.createServer(options, secureConnectionListener?): Server
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            callback = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tls_create_server", ft);
        lastValue = createCall(ft, fn.getCallee(), { options, callback });
        boxedValues.insert(lastValue);
        return true;
    }

    if (prop->name == "getCiphers") {
        // tls.getCiphers(): string[]
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_tls_get_ciphers", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateTlsSocketCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureTlsFunctionsRegistered(boxingPolicy);

    // Get the object being called on
    auto objectType = prop->expression->inferredType;
    if (!objectType) return false;

    auto classType = std::dynamic_pointer_cast<ClassType>(objectType);
    if (!classType || (classType->name != "TLSSocket" && classType->name != "SecureSocket")) return false;

    SPDLOG_DEBUG("tryGenerateTlsSocketCall: socket.{}", prop->name);

    // Get the socket object
    visit(prop->expression.get());
    llvm::Value* socket = lastValue;

    // Ensure socket is boxed for runtime calls
    if (!boxedValues.count(socket)) {
        socket = boxValue(socket, objectType);
    }

    if (prop->name == "getCertificate") {
        // socket.getCertificate(): object | null
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_get_certificate", ft);
        lastValue = createCall(ft, fn.getCallee(), { socket });
        boxedValues.insert(lastValue);
        return true;
    }

    if (prop->name == "getPeerCertificate") {
        // socket.getPeerCertificate(detailed?: boolean): object | null
        llvm::Value* detailed = builder->getFalse();

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            detailed = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getInt1Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_get_peer_certificate", ft);
        lastValue = createCall(ft, fn.getCallee(), { socket, detailed });
        boxedValues.insert(lastValue);
        return true;
    }

    if (prop->name == "getProtocol") {
        // socket.getProtocol(): string | null
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_get_protocol", ft);
        lastValue = createCall(ft, fn.getCallee(), { socket });
        boxedValues.insert(lastValue);
        return true;
    }

    if (prop->name == "getSession") {
        // socket.getSession(): Buffer | undefined
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_get_session", ft);
        lastValue = createCall(ft, fn.getCallee(), { socket });
        boxedValues.insert(lastValue);
        return true;
    }

    if (prop->name == "renegotiate") {
        // socket.renegotiate(options, callback): boolean
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            callback = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_renegotiate", ft);
        lastValue = createCall(ft, fn.getCallee(), { socket, options, callback });
        return true;
    }

    if (prop->name == "setMaxSendFragment") {
        // socket.setMaxSendFragment(size: number): boolean
        llvm::Value* size = llvm::ConstantInt::get(builder->getInt64Ty(), 16384);  // default max

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            size = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_set_max_send_fragment", ft);
        lastValue = createCall(ft, fn.getCallee(), { socket, size });
        return true;
    }

    // =========================================================================
    // SNI (Server Name Indication) support
    // =========================================================================

    if (prop->name == "setServername") {
        // socket.setServername(hostname: string): void
        if (node->arguments.size() == 0) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = boxValue(lastValue, node->arguments[0]->inferredType);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_set_servername", ft);
        createCall(ft, fn.getCallee(), { socket, hostname });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (prop->name == "getServername") {
        // socket.getServername(): string | undefined
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_get_servername", ft);
        lastValue = createCall(ft, fn.getCallee(), { socket });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // ALPN (Application-Layer Protocol Negotiation) support
    // =========================================================================

    if (prop->name == "setALPNProtocols") {
        // socket.setALPNProtocols(protocols: string[]): void
        if (node->arguments.size() == 0) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* protocols = boxValue(lastValue, node->arguments[0]->inferredType);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_set_alpn_protocols", ft);
        createCall(ft, fn.getCallee(), { socket, protocols });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    // =========================================================================
    // Session resumption support
    // =========================================================================

    if (prop->name == "setSession") {
        // socket.setSession(session: Buffer): void
        if (node->arguments.size() == 0) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* session = boxValue(lastValue, node->arguments[0]->inferredType);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_set_session", ft);
        createCall(ft, fn.getCallee(), { socket, session });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (prop->name == "isSessionReused") {
        // socket.isSessionReused(): boolean
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_is_session_reused", ft);
        lastValue = createCall(ft, fn.getCallee(), { socket });
        return true;
    }

    // =========================================================================
    // Client certificate support
    // =========================================================================

    if (prop->name == "setClientCertificate") {
        // socket.setClientCertificate(cert: Buffer, key: Buffer): void
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* cert = boxValue(lastValue, node->arguments[0]->inferredType);

        visit(node->arguments[1].get());
        llvm::Value* key = boxValue(lastValue, node->arguments[1]->inferredType);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_secure_socket_set_client_certificate", ft);
        createCall(ft, fn.getCallee(), { socket, cert, key });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    return false;
}

} // namespace ts
