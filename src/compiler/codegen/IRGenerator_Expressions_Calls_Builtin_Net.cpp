#include "IRGenerator.h"

namespace ts {

bool IRGenerator::tryGenerateNetCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    bool isNet = false;
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "net") isNet = true;
    }

    bool isSocket = false;
    bool isServer = false;

    if (!isNet) {
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            if (classType->name == "Socket") isSocket = true;
            else if (classType->name == "Server") isServer = true;
        }
    }

    if (!isNet && !isSocket && !isServer) return false;

    if (isNet) {
        if (prop->name == "createServer") {
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                callback = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_net_create_server", ft);
            lastValue = createCall(ft, fn.getCallee(), { callback });
            return true;
        } else if (prop->name == "connect" || prop->name == "createConnection") {
            // net.connect(port, host?, callback?)
            if (node->arguments.empty()) return true;
            
            // Create socket first
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_net_create_socket", createFt);
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
            llvm::FunctionCallee connectFn = module->getOrInsertFunction("ts_net_socket_connect", connectFt);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_net_is_ip", ft);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_net_is_ipv4", ft);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_net_is_ipv6", ft);
            lastValue = createCall(ft, fn.getCallee(), { input });
            lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
            return true;
        } else if (prop->name == "getDefaultAutoSelectFamily") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_net_get_default_auto_select_family", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
            return true;
        } else if (prop->name == "setDefaultAutoSelectFamily") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* value = unboxValue(lastValue, std::make_shared<Type>(TypeKind::Boolean));
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getInt1Ty() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_net_set_default_auto_select_family", ft);
            createCall(ft, fn.getCallee(), { value });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        } else if (prop->name == "getDefaultAutoSelectFamilyAttemptTimeout") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_net_get_default_auto_select_family_attempt_timeout", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return true;
        } else if (prop->name == "setDefaultAutoSelectFamilyAttemptTimeout") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* value = unboxValue(lastValue, std::make_shared<Type>(TypeKind::Int));
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getInt64Ty() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_net_set_default_auto_select_family_attempt_timeout", ft);
            createCall(ft, fn.getCallee(), { value });
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
    }

    if (isSocket) {
        if (prop->name == "write") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* socket = lastValue;
            visit(node->arguments[0].get());
            llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_writable_write", ft);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_writable_end", ft);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_net_server_listen", ft);
            createCall(ft, fn.getCallee(), { server, port, callback });
            lastValue = server; // listen() returns the server
            return true;
        }
    }

    return false;
}

} // namespace ts
