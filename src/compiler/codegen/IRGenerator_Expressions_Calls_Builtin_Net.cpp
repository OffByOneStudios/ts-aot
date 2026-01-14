#include "IRGenerator.h"
#include "BoxingPolicy.h"

namespace ts {

// Static helper to register Net module's runtime functions once (19 functions)
static bool netFunctionsRegistered = false;
static void ensureNetFunctionsRegistered(BoxingPolicy& bp) {
    if (netFunctionsRegistered) return;
    netFunctionsRegistered = true;
    
    // net module functions
    bp.registerRuntimeApi("ts_net_create_server", {true}, true);  // options -> Server
    bp.registerRuntimeApi("ts_net_create_socket", {true}, true);  // options -> Socket
    bp.registerRuntimeApi("ts_net_is_ip", {false}, false);  // string -> number (0, 4, or 6)
    bp.registerRuntimeApi("ts_net_is_ipv4", {false}, false);  // string -> bool
    bp.registerRuntimeApi("ts_net_is_ipv6", {false}, false);
    bp.registerRuntimeApi("ts_net_get_default_auto_select_family", {}, false);  // -> bool
    bp.registerRuntimeApi("ts_net_set_default_auto_select_family", {false}, false);
    bp.registerRuntimeApi("ts_net_get_default_auto_select_family_attempt_timeout", {}, false);  // -> number
    bp.registerRuntimeApi("ts_net_set_default_auto_select_family_attempt_timeout", {false}, false);
    
    // Server methods
    bp.registerRuntimeApi("ts_net_server_listen", {true, false, false, false, true}, true);  // server, port, host, backlog, callback
    
    // Socket methods
    bp.registerRuntimeApi("ts_net_socket_connect", {true, true}, true);  // socket, options
    bp.registerRuntimeApi("ts_writable_write", {true, true}, true);  // socket, data
    bp.registerRuntimeApi("ts_writable_end", {true}, true);
    
    // SocketAddress static
    bp.registerRuntimeApi("ts_net_socket_address_parse", {false}, true);  // address string -> SocketAddress
    
    // BlockList
    bp.registerRuntimeApi("ts_net_block_list_is_block_list", {true}, false);  // obj -> bool
    bp.registerRuntimeApi("ts_net_block_list_add_address", {true, false, false}, false);  // list, address, family
    bp.registerRuntimeApi("ts_net_block_list_add_range", {true, false, false, false}, false);  // list, start, end, family
    bp.registerRuntimeApi("ts_net_block_list_add_subnet", {true, false, false, false}, false);  // list, address, prefix, family
    bp.registerRuntimeApi("ts_net_block_list_check", {true, false, false}, false);  // list, address, family -> bool

    // Socket address properties
    bp.registerRuntimeApi("ts_net_socket_get_remote_address", {true}, false);  // socket -> string
    bp.registerRuntimeApi("ts_net_socket_get_remote_port", {true}, false);  // socket -> number
    bp.registerRuntimeApi("ts_net_socket_get_remote_family", {true}, false);  // socket -> string
    bp.registerRuntimeApi("ts_net_socket_get_local_address", {true}, false);  // socket -> string
    bp.registerRuntimeApi("ts_net_socket_get_local_port", {true}, false);  // socket -> number
    bp.registerRuntimeApi("ts_net_socket_get_local_family", {true}, false);  // socket -> string

    // Socket state properties
    bp.registerRuntimeApi("ts_net_socket_get_bytes_read", {true}, false);  // socket -> number
    bp.registerRuntimeApi("ts_net_socket_get_bytes_written", {true}, false);  // socket -> number
    bp.registerRuntimeApi("ts_net_socket_get_connecting", {true}, false);  // socket -> bool
    bp.registerRuntimeApi("ts_net_socket_get_destroyed", {true}, false);  // socket -> bool
    bp.registerRuntimeApi("ts_net_socket_get_pending", {true}, false);  // socket -> bool
    bp.registerRuntimeApi("ts_net_socket_get_ready_state", {true}, false);  // socket -> string
}

bool IRGenerator::tryGenerateNetCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureNetFunctionsRegistered(boxingPolicy);
    
    bool isNet = false;
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "net") isNet = true;
    }
    
    // Check for net.SocketAddress.parse() or net.BlockList.isBlockList()
    // Pattern: prop->expression is PropertyAccessExpression with expression "net"
    bool isSocketAddressStatic = false;
    bool isBlockListStatic = false;
    if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
        if (auto id = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
            if (id->name == "net") {
                if (innerProp->name == "SocketAddress") {
                    isSocketAddressStatic = true;
                } else if (innerProp->name == "BlockList") {
                    isBlockListStatic = true;
                }
            }
        }
    }
    
    // Handle net.SocketAddress.parse()
    if (isSocketAddressStatic && prop->name == "parse") {
        llvm::Value* input = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            input = lastValue;
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_address_parse", ft);
        lastValue = createCall(ft, fn.getCallee(), { input });
        return true;
    }
    
    // Handle net.BlockList.isBlockList()
    if (isBlockListStatic && prop->name == "isBlockList") {
        llvm::Value* value = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            value = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_net_block_list_is_block_list", ft);
        lastValue = createCall(ft, fn.getCallee(), { value });
        lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
        return true;
    }

    bool isSocket = false;
    bool isServer = false;
    bool isBlockList = false;

    if (!isNet) {
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            if (classType->name == "Socket") isSocket = true;
            else if (classType->name == "Server") isServer = true;
            else if (classType->name == "BlockList") isBlockList = true;
        }
    }

    if (!isNet && !isSocket && !isServer && !isBlockList) return false;
    
    // Handle BlockList instance methods
    if (isBlockList) {
        visit(prop->expression.get());
        llvm::Value* blockList = lastValue;
        
        if (prop->name == "addAddress") {
            // addAddress(address: string, type?: 'ipv4' | 'ipv6')
            llvm::Value* address = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* family = llvm::ConstantPointerNull::get(builder->getPtrTy());
            
            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                address = lastValue;
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                family = lastValue;
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(), 
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, 
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_block_list_add_address", ft);
            createCall(ft, fn.getCallee(), { blockList, address, family });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        } else if (prop->name == "addRange") {
            // addRange(start: string, end: string, type?: 'ipv4' | 'ipv6')
            llvm::Value* start = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* end = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* family = llvm::ConstantPointerNull::get(builder->getPtrTy());
            
            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                start = lastValue;
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                end = lastValue;
            }
            if (node->arguments.size() >= 3) {
                visit(node->arguments[2].get());
                family = lastValue;
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(), 
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, 
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_block_list_add_range", ft);
            createCall(ft, fn.getCallee(), { blockList, start, end, family });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        } else if (prop->name == "addSubnet") {
            // addSubnet(network: string, prefix: number, type?: 'ipv4' | 'ipv6')
            llvm::Value* network = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* prefix = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            llvm::Value* family = llvm::ConstantPointerNull::get(builder->getPtrTy());
            
            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                network = lastValue;
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                prefix = lastValue;
                if (!prefix->getType()->isIntegerTy(64)) {
                    if (prefix->getType()->isIntegerTy()) {
                        prefix = builder->CreateSExt(prefix, builder->getInt64Ty());
                    } else if (prefix->getType()->isDoubleTy()) {
                        prefix = builder->CreateFPToSI(prefix, builder->getInt64Ty());
                    }
                }
            }
            if (node->arguments.size() >= 3) {
                visit(node->arguments[2].get());
                family = lastValue;
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getVoidTy(), 
                { builder->getPtrTy(), builder->getPtrTy(), builder->getInt64Ty(), builder->getPtrTy() }, 
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_block_list_add_subnet", ft);
            createCall(ft, fn.getCallee(), { blockList, network, prefix, family });
            lastValue = llvm::Constant::getNullValue(builder->getPtrTy());
            return true;
        } else if (prop->name == "check") {
            // check(address: string, type?: 'ipv4' | 'ipv6') -> boolean
            llvm::Value* address = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* family = llvm::ConstantPointerNull::get(builder->getPtrTy());
            
            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                address = lastValue;
            }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                family = lastValue;
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getInt1Ty(), 
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, 
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_block_list_check", ft);
            lastValue = createCall(ft, fn.getCallee(), { blockList, address, family });
            return true;
        }
    }

    if (isNet) {
        if (prop->name == "createServer") {
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                callback = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_create_server", ft);
            lastValue = createCall(ft, fn.getCallee(), { callback });
            return true;
        } else if (prop->name == "connect" || prop->name == "createConnection") {
            // net.connect(port, host?, callback?)
            if (node->arguments.empty()) return true;
            
            // Create socket first
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee createFn = getRuntimeFunction("ts_net_create_socket", createFt);
            llvm::Value* socket = createCall(createFt, createFn.getCallee(), {});
            
            visit(node->arguments[0].get());
            llvm::Value* port = boxValue(lastValue, node->arguments[0]->inferredType);
            
            llvm::Value* host = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                host = boxValue(lastValue, node->arguments[1]->inferredType);
            }
            
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 2) {
                visit(node->arguments[2].get());
                callback = boxValue(lastValue, node->arguments[2]->inferredType);
            }
            
            llvm::FunctionType* connectFt = llvm::FunctionType::get(builder->getVoidTy(), 
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee connectFn = getRuntimeFunction("ts_net_socket_connect", connectFt);
            createCall(connectFt, connectFn.getCallee(), { socket, port, host, callback });
            
            lastValue = socket;
            return true;
        } else if (prop->name == "isIP") {
            // net.isIP(input: string) -> number (0, 4, or 6)
            if (node->arguments.empty()) {
                lastValue = builder->getInt64(0);
                return true;
            }
            visit(node->arguments[0].get());
            llvm::Value* input = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_is_ip", ft);
            lastValue = createCall(ft, fn.getCallee(), { input });
            return true;
        } else if (prop->name == "isIPv4") {
            // net.isIPv4(input: string) -> boolean
            if (node->arguments.empty()) {
                lastValue = builder->getInt1(false);
                lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
                return true;
            }
            visit(node->arguments[0].get());
            llvm::Value* input = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_is_ipv4", ft);
            lastValue = createCall(ft, fn.getCallee(), { input });
            lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
            return true;
        } else if (prop->name == "isIPv6") {
            // net.isIPv6(input: string) -> boolean
            if (node->arguments.empty()) {
                lastValue = builder->getInt1(false);
                lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
                return true;
            }
            visit(node->arguments[0].get());
            llvm::Value* input = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_is_ipv6", ft);
            lastValue = createCall(ft, fn.getCallee(), { input });
            lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
            return true;
        } else if (prop->name == "getDefaultAutoSelectFamily") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_get_default_auto_select_family", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
            return true;
        } else if (prop->name == "setDefaultAutoSelectFamily") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* value = unboxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getInt1Ty() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_set_default_auto_select_family", ft);
            createCall(ft, fn.getCallee(), { value });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        } else if (prop->name == "getDefaultAutoSelectFamilyAttemptTimeout") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_get_default_auto_select_family_attempt_timeout", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return true;
        } else if (prop->name == "setDefaultAutoSelectFamilyAttemptTimeout") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* value = unboxValue(lastValue, std::make_shared<Type>(TypeKind::Int));
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getInt64Ty() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_set_default_auto_select_family_attempt_timeout", ft);
            createCall(ft, fn.getCallee(), { value });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
    }

    // Handle net.SocketAddress
    bool isSocketAddress = false;
    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
        if (classType->name == "SocketAddress") isSocketAddress = true;
    }
    
    if (isSocketAddress) {
        // SocketAddress instance property access is handled elsewhere
        return false;
    }

    if (isSocket) {
        if (prop->name == "write") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* socket = lastValue;
            visit(node->arguments[0].get());
            llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_write", ft);
            lastValue = createCall(ft, fn.getCallee(), { socket, data });
            lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
            return true;
        } else if (prop->name == "end") {
            visit(prop->expression.get());
            llvm::Value* socket = lastValue;
            
            llvm::Value* data = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                data = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_end", ft);
            createCall(ft, fn.getCallee(), { socket, data });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy()); // returns undefined
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
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_server_listen", ft);
            createCall(ft, fn.getCallee(), { server, port, callback });
            lastValue = server; // listen() returns the server
            return true;
        }
    }

    return false;
}

} // namespace ts
