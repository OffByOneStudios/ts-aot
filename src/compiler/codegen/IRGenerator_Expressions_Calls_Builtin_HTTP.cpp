#include "IRGenerator.h"
#include <spdlog/spdlog.h>

namespace ts {

bool IRGenerator::tryGenerateHTTPCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    SPDLOG_INFO("tryGenerateHTTPCall: prop->name={}", prop->name);
    bool isHttp = false;
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        SPDLOG_INFO("tryGenerateHTTPCall: id->name={}", id->name);
        if (id->name == "http") isHttp = true;
    }

    bool isServer = false;
    bool isServerResponse = false;
    bool isIncomingMessage = false;
    bool isClientRequest = false;

    if (!isHttp) {
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            if (classType->name == "Server") isServer = true;
            else if (classType->name == "ServerResponse") isServerResponse = true;
            else if (classType->name == "IncomingMessage") isIncomingMessage = true;
            else if (classType->name == "ClientRequest") isClientRequest = true;
        }
    }

    if (!isHttp && !isServer && !isServerResponse && !isIncomingMessage && !isClientRequest) return false;

    if (isHttp) {
        if (prop->name == "createServer") {
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                callback = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http_create_server", ft);
            lastValue = createCall(ft, fn.getCallee(), { callback });
            return true;
        } else if (prop->name == "request" || prop->name == "get") {
            if (node->arguments.empty()) return true;
            
            visit(node->arguments[0].get());
            llvm::Value* options = boxValue(lastValue, node->arguments[0]->inferredType);
            
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            const char* fnName = (prop->name == "request") ? "ts_http_request" : "ts_http_get";
            llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
            lastValue = createCall(ft, fn.getCallee(), { options, callback });
            return true;
        }
    }

    if (isServer) {
        if (prop->name == "listen") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* server = lastValue;
            
            visit(node->arguments[0].get());
            llvm::Value* port = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            
            llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                callback = boxValue(lastValue, node->arguments[1]->inferredType);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http_server_listen", ft);
            createCall(ft, fn.getCallee(), { server, port, callback });
            lastValue = server;
            return true;
        }
    }

    if (isServerResponse || isClientRequest) {
        if (prop->name == "writeHead" && isServerResponse) {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* res = lastValue;
            
            visit(node->arguments[0].get());
            llvm::Value* status = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            
            llvm::Value* headers = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                headers = boxValue(lastValue, node->arguments[1]->inferredType);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_http_server_response_write_head", ft);
            createCall(ft, fn.getCallee(), { res, status, headers });
            lastValue = res;
            return true;
        } else if (prop->name == "write") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* res = lastValue;
            if (!res) {
                SPDLOG_ERROR("tryGenerateHTTPCall: write: res is NULL!");
                return true;
            }
            visit(node->arguments[0].get());
            llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_writable_write", ft);
            createCall(ft, fn.getCallee(), { res, data });
            lastValue = res; // Return 'this' for chaining
            return true;
        } else if (prop->name == "end") {
            visit(prop->expression.get());
            llvm::Value* res = lastValue;
            if (!res) {
                SPDLOG_ERROR("tryGenerateHTTPCall: end: res is NULL!");
                return true;
            }
            
            llvm::Value* data = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                data = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_writable_end", ft);
            createCall(ft, fn.getCallee(), { res, data });
            lastValue = res; // Return 'this' for chaining
            return true;
        }
    }

    return false;
}

} // namespace ts
