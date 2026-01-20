#include "IRGenerator.h"
#include "BoxingPolicy.h"

namespace ts {

// Static helper to register dgram module's runtime functions once
static bool dgramFunctionsRegistered = false;
static void ensureDgramFunctionsRegistered(BoxingPolicy& bp) {
    if (dgramFunctionsRegistered) return;
    dgramFunctionsRegistered = true;

    // dgram module functions
    bp.registerRuntimeApi("ts_dgram_create_socket", {false}, true);  // type -> Socket
    bp.registerRuntimeApi("ts_dgram_create_socket_options", {false, false, false, false, false}, true);  // type, reuseAddr, ipv6Only, recvBufferSize, sendBufferSize -> Socket

    // Socket methods
    bp.registerRuntimeApi("ts_udp_socket_bind", {true, true, false, true}, false);  // socket, port, address, callback
    bp.registerRuntimeApi("ts_udp_socket_send", {true, true, true, true, true, false, true}, false);  // socket, msg, offset, length, port, address, callback
    bp.registerRuntimeApi("ts_udp_socket_close", {true, true}, false);  // socket, callback
    bp.registerRuntimeApi("ts_udp_socket_address", {true}, false);  // socket -> raw TsMap*

    // Multicast/Broadcast configuration
    bp.registerRuntimeApi("ts_udp_socket_set_broadcast", {true, true}, false);  // socket, flag
    bp.registerRuntimeApi("ts_udp_socket_set_multicast_ttl", {true, true}, false);  // socket, ttl
    bp.registerRuntimeApi("ts_udp_socket_set_multicast_loopback", {true, true}, false);  // socket, flag
    bp.registerRuntimeApi("ts_udp_socket_set_multicast_interface", {true, false}, false);  // socket, interface
    bp.registerRuntimeApi("ts_udp_socket_add_membership", {true, false, false}, false);  // socket, multicastAddr, interfaceAddr
    bp.registerRuntimeApi("ts_udp_socket_drop_membership", {true, false, false}, false);  // socket, multicastAddr, interfaceAddr

    // Connected mode
    bp.registerRuntimeApi("ts_udp_socket_connect", {true, true, false, true}, false);  // socket, port, address, callback
    bp.registerRuntimeApi("ts_udp_socket_disconnect", {true}, false);  // socket

    // Handle reference control
    bp.registerRuntimeApi("ts_udp_socket_ref", {true}, false);  // socket
    bp.registerRuntimeApi("ts_udp_socket_unref", {true}, false);  // socket

    // Buffer size and TTL
    bp.registerRuntimeApi("ts_udp_socket_set_recv_buffer_size", {true, true}, false);  // socket, size
    bp.registerRuntimeApi("ts_udp_socket_set_send_buffer_size", {true, true}, false);  // socket, size
    bp.registerRuntimeApi("ts_udp_socket_get_recv_buffer_size", {true}, false);  // socket -> int
    bp.registerRuntimeApi("ts_udp_socket_get_send_buffer_size", {true}, false);  // socket -> int
    bp.registerRuntimeApi("ts_udp_socket_set_ttl", {true, true}, false);  // socket, ttl
    bp.registerRuntimeApi("ts_udp_socket_remote_address", {true}, false);  // socket -> TsMap*

    // Property accessors
    bp.registerRuntimeApi("ts_udp_socket_get_local_address", {true}, false);
    bp.registerRuntimeApi("ts_udp_socket_get_local_port", {true}, false);
    bp.registerRuntimeApi("ts_udp_socket_get_local_family", {true}, false);
    bp.registerRuntimeApi("ts_udp_socket_get_remote_address", {true}, false);
    bp.registerRuntimeApi("ts_udp_socket_get_remote_port", {true}, false);
    bp.registerRuntimeApi("ts_udp_socket_get_bytes_read", {true}, false);
    bp.registerRuntimeApi("ts_udp_socket_get_bytes_written", {true}, false);
    bp.registerRuntimeApi("ts_udp_socket_is_bound", {true}, false);
    bp.registerRuntimeApi("ts_udp_socket_is_connected", {true}, false);
    bp.registerRuntimeApi("ts_udp_socket_is_destroyed", {true}, false);
}

bool IRGenerator::tryGenerateDgramCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureDgramFunctionsRegistered(boxingPolicy);

    bool isDgram = false;
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "dgram") isDgram = true;
    }

    bool isUDPSocket = false;
    if (!isDgram) {
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            if (classType->name == "Socket" || classType->name == "UDPSocket") {
                isUDPSocket = true;
            }
        }
    }

    if (!isDgram && !isUDPSocket) return false;

    // Handle dgram module methods
    if (isDgram) {
        if (prop->name == "createSocket") {
            // dgram.createSocket(type, callback?) OR dgram.createSocket(options)
            // Check if first argument is an object literal (options form)
            if (!node->arguments.empty()) {
                if (auto objLit = dynamic_cast<ast::ObjectLiteralExpression*>(node->arguments[0].get())) {
                    // Options object form: { type, reuseAddr?, ipv6Only?, recvBufferSize?, sendBufferSize? }
                    llvm::Value* typeArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    llvm::Value* reuseAddr = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
                    llvm::Value* ipv6Only = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
                    llvm::Value* recvBufferSize = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
                    llvm::Value* sendBufferSize = llvm::ConstantInt::get(builder->getInt64Ty(), 0);

                    for (auto& prop : objLit->properties) {
                        if (auto propAssign = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
                            if (propAssign->name == "type") {
                                visit(propAssign->initializer.get());
                                typeArg = lastValue;
                            } else if (propAssign->name == "reuseAddr") {
                                visit(propAssign->initializer.get());
                                // Convert bool to i64 (0 or 1)
                                if (lastValue->getType()->isIntegerTy(1)) {
                                    reuseAddr = builder->CreateZExt(lastValue, builder->getInt64Ty());
                                } else if (lastValue->getType()->isPointerTy()) {
                                    reuseAddr = builder->CreatePtrToInt(lastValue, builder->getInt64Ty());
                                } else {
                                    reuseAddr = lastValue;
                                }
                            } else if (propAssign->name == "ipv6Only") {
                                visit(propAssign->initializer.get());
                                if (lastValue->getType()->isIntegerTy(1)) {
                                    ipv6Only = builder->CreateZExt(lastValue, builder->getInt64Ty());
                                } else if (lastValue->getType()->isPointerTy()) {
                                    ipv6Only = builder->CreatePtrToInt(lastValue, builder->getInt64Ty());
                                } else {
                                    ipv6Only = lastValue;
                                }
                            } else if (propAssign->name == "recvBufferSize") {
                                visit(propAssign->initializer.get());
                                if (lastValue->getType()->isPointerTy()) {
                                    recvBufferSize = builder->CreatePtrToInt(lastValue, builder->getInt64Ty());
                                } else {
                                    recvBufferSize = lastValue;
                                }
                            } else if (propAssign->name == "sendBufferSize") {
                                visit(propAssign->initializer.get());
                                if (lastValue->getType()->isPointerTy()) {
                                    sendBufferSize = builder->CreatePtrToInt(lastValue, builder->getInt64Ty());
                                } else {
                                    sendBufferSize = lastValue;
                                }
                            }
                        }
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        builder->getPtrTy(),
                        { builder->getPtrTy(), builder->getInt64Ty(), builder->getInt64Ty(),
                          builder->getInt64Ty(), builder->getInt64Ty() },
                        false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_dgram_create_socket_options", ft);
                    lastValue = createCall(ft, fn.getCallee(), { typeArg, reuseAddr, ipv6Only, recvBufferSize, sendBufferSize });
                    boxedValues.insert(lastValue);
                    return true;
                }
            }

            // String form: dgram.createSocket(type, callback?)
            llvm::Value* typeArg = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                typeArg = lastValue;
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_dgram_create_socket", ft);
            lastValue = createCall(ft, fn.getCallee(), { typeArg });
            boxedValues.insert(lastValue);

            // If callback provided (second arg), register it for 'message' event
            // (This is handled in runtime by passing to socket.on('message', callback))
            return true;
        }
        return false;
    }

    // Handle Socket instance methods
    if (isUDPSocket) {
        visit(prop->expression.get());
        llvm::Value* socket = lastValue;

        // DON'T re-box the socket - it's already boxed from createSocket()
        // The runtime's GetUDPSocket() will unbox it correctly.
        // Re-boxing would create a double-boxed value that crashes on unbox.

        if (prop->name == "bind") {
            // socket.bind(port, address?, callback?)
            llvm::Value* port = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* address = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                port = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                // Could be address (string) or callback (function)
                if (node->arguments[1]->inferredType &&
                    node->arguments[1]->inferredType->kind == TypeKind::Function) {
                    callback = lastValue;
                } else {
                    address = lastValue;
                }
            }
            if (node->arguments.size() >= 3) {
                visit(node->arguments[2].get());
                callback = lastValue;
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_bind", ft);
            createCall(ft, fn.getCallee(), { socket, port, address, callback });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "send") {
            // socket.send(msg, offset?, length?, port, address, callback?)
            // Node.js also supports: socket.send(msg, port, address, callback?)
            llvm::Value* msg = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* offset = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* length = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* port = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* address = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            size_t argCount = node->arguments.size();

            if (argCount >= 1) {
                visit(node->arguments[0].get());
                msg = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            // Detect which form is being used based on argument types
            // Form 1: send(msg, port, address, callback?)
            // Form 2: send(msg, offset, length, port, address, callback?)
            bool isShortForm = false;
            if (argCount >= 3) {
                // If arg[1] is a number that looks like a port (> 0) and arg[2] is a string (address), use short form
                auto arg1Type = node->arguments[1]->inferredType;
                auto arg2Type = node->arguments[2]->inferredType;
                if (arg2Type && arg2Type->kind == TypeKind::String) {
                    isShortForm = true;
                }
            }

            if (isShortForm) {
                // send(msg, port, address, callback?)
                if (argCount >= 2) {
                    visit(node->arguments[1].get());
                    port = boxValue(lastValue, node->arguments[1]->inferredType);
                }
                if (argCount >= 3) {
                    visit(node->arguments[2].get());
                    address = lastValue;
                }
                if (argCount >= 4) {
                    visit(node->arguments[3].get());
                    callback = lastValue;
                }
            } else {
                // send(msg, offset, length, port, address, callback?)
                if (argCount >= 2) {
                    visit(node->arguments[1].get());
                    offset = boxValue(lastValue, node->arguments[1]->inferredType);
                }
                if (argCount >= 3) {
                    visit(node->arguments[2].get());
                    length = boxValue(lastValue, node->arguments[2]->inferredType);
                }
                if (argCount >= 4) {
                    visit(node->arguments[3].get());
                    port = boxValue(lastValue, node->arguments[3]->inferredType);
                }
                if (argCount >= 5) {
                    visit(node->arguments[4].get());
                    address = lastValue;
                }
                if (argCount >= 6) {
                    visit(node->arguments[5].get());
                    callback = lastValue;
                }
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(),
                  builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_send", ft);
            createCall(ft, fn.getCallee(), { socket, msg, offset, length, port, address, callback });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "close") {
            // socket.close(callback?)
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                callback = lastValue;
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_close", ft);
            createCall(ft, fn.getCallee(), { socket, callback });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "address") {
            // socket.address()
            // Returns raw TsMap*, not boxed - property access will use ts_map_get
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_address", ft);
            lastValue = createCall(ft, fn.getCallee(), { socket });
            // Don't insert into boxedValues - returns raw TsMap*
            return true;
        }

        if (prop->name == "setBroadcast") {
            // socket.setBroadcast(flag)
            llvm::Value* flag = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                flag = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_set_broadcast", ft);
            createCall(ft, fn.getCallee(), { socket, flag });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "setMulticastTTL") {
            // socket.setMulticastTTL(ttl)
            llvm::Value* ttl = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                ttl = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_set_multicast_ttl", ft);
            createCall(ft, fn.getCallee(), { socket, ttl });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "setMulticastLoopback") {
            // socket.setMulticastLoopback(flag)
            llvm::Value* flag = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                flag = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_set_multicast_loopback", ft);
            createCall(ft, fn.getCallee(), { socket, flag });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "setMulticastInterface") {
            // socket.setMulticastInterface(interfaceAddress)
            llvm::Value* interfaceAddr = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                interfaceAddr = lastValue;
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_set_multicast_interface", ft);
            createCall(ft, fn.getCallee(), { socket, interfaceAddr });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "addMembership") {
            // socket.addMembership(multicastAddress, multicastInterface?)
            llvm::Value* multicastAddr = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* interfaceAddr = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                multicastAddr = lastValue;
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                interfaceAddr = lastValue;
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_add_membership", ft);
            createCall(ft, fn.getCallee(), { socket, multicastAddr, interfaceAddr });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "dropMembership") {
            // socket.dropMembership(multicastAddress, multicastInterface?)
            llvm::Value* multicastAddr = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* interfaceAddr = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                multicastAddr = lastValue;
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                interfaceAddr = lastValue;
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_drop_membership", ft);
            createCall(ft, fn.getCallee(), { socket, multicastAddr, interfaceAddr });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "connect") {
            // socket.connect(port, address?, callback?)
            llvm::Value* port = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* address = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                port = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                // Could be address (string) or callback (function)
                if (node->arguments[1]->inferredType &&
                    node->arguments[1]->inferredType->kind == TypeKind::Function) {
                    callback = lastValue;
                } else {
                    address = lastValue;
                }
            }
            if (node->arguments.size() >= 3) {
                visit(node->arguments[2].get());
                callback = lastValue;
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_connect", ft);
            createCall(ft, fn.getCallee(), { socket, port, address, callback });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "disconnect") {
            // socket.disconnect()
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_disconnect", ft);
            createCall(ft, fn.getCallee(), { socket });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "ref") {
            // socket.ref()
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_ref", ft);
            createCall(ft, fn.getCallee(), { socket });
            lastValue = socket;  // Returns the socket for chaining
            return true;
        }

        if (prop->name == "unref") {
            // socket.unref()
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_unref", ft);
            createCall(ft, fn.getCallee(), { socket });
            lastValue = socket;  // Returns the socket for chaining
            return true;
        }

        if (prop->name == "setRecvBufferSize") {
            // socket.setRecvBufferSize(size)
            llvm::Value* size = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                size = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_set_recv_buffer_size", ft);
            createCall(ft, fn.getCallee(), { socket, size });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "setSendBufferSize") {
            // socket.setSendBufferSize(size)
            llvm::Value* size = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                size = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_set_send_buffer_size", ft);
            createCall(ft, fn.getCallee(), { socket, size });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "getRecvBufferSize") {
            // socket.getRecvBufferSize()
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getInt64Ty(),
                { builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_get_recv_buffer_size", ft);
            lastValue = createCall(ft, fn.getCallee(), { socket });
            return true;
        }

        if (prop->name == "getSendBufferSize") {
            // socket.getSendBufferSize()
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getInt64Ty(),
                { builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_get_send_buffer_size", ft);
            lastValue = createCall(ft, fn.getCallee(), { socket });
            return true;
        }

        if (prop->name == "setTTL") {
            // socket.setTTL(ttl)
            llvm::Value* ttl = llvm::ConstantPointerNull::get(builder->getPtrTy());

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                ttl = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_set_ttl", ft);
            createCall(ft, fn.getCallee(), { socket, ttl });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        }

        if (prop->name == "remoteAddress") {
            // socket.remoteAddress()
            // Returns object { address, family, port } similar to address()
            // For now, return the connected remote address info
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_udp_socket_remote_address", ft);
            lastValue = createCall(ft, fn.getCallee(), { socket });
            return true;
        }
    }

    return false;
}

} // namespace ts
