#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

bool IRGenerator::tryGenerateFSCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check if the object is "fs"
    bool isFs = false;
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "fs") isFs = true;
    }
    
    // Also allow if the expression is 'any' (e.g. from require)
    if (!isFs && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
        isFs = true; 
    }

    if (!isFs) return false;

    SPDLOG_DEBUG("tryGenerateFSCall: {}", prop->name);

    if (prop->name == "readFileSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* arg = lastValue;
        if (arg->getType()->isIntegerTy(64)) {
            arg = builder->CreateIntToPtr(arg, builder->getPtrTy());
        }
        
        llvm::FunctionType* readFt = llvm::FunctionType::get(builder->getPtrTy(), 
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee readFn = module->getOrInsertFunction("ts_fs_readFileSync", readFt);
        
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
        llvm::FunctionCallee writeFn = module->getOrInsertFunction("ts_fs_writeFileSync", writeFt);
        
        createCall(writeFt, writeFn.getCallee(), { path, data });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "existsSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::FunctionType* existsFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), 
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee existsFn = module->getOrInsertFunction("ts_fs_existsSync", existsFt);
        
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
    } else if (prop->name == "statSync" || prop->name == "readdirSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        std::string runtimeName = "ts_fs_" + prop->name;
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
        
        lastValue = createCall(ft, fn.getCallee(), { path });
        return true;
    } else if (prop->name == "openSync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* flags = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_openSync", ft);
        
        llvm::Value* fd = createCall(ft, fn.getCallee(), { path, flags });
        
        llvm::FunctionType* makeIntFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee makeIntFn = module->getOrInsertFunction("ts_value_make_int", makeIntFt);
        lastValue = createCall(makeIntFt, makeIntFn.getCallee(), { fd });
        return true;
    } else if (prop->name == "closeSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_closeSync", ft);
        
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
        llvm::FunctionCallee makeIntFn = module->getOrInsertFunction("ts_value_make_int", makeIntFt);
        lastValue = createCall(makeIntFt, makeIntFn.getCallee(), { result });
        return true;
    } else if (prop->name == "createReadStream") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_createReadStream", ft);
        lastValue = createCall(ft, fn.getCallee(), { path });
        return true;
    } else if (prop->name == "createWriteStream") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_createWriteStream", ft);
        lastValue = createCall(ft, fn.getCallee(), { path });
        return true;
    } else if (prop->name == "accessSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        llvm::Value* mode = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            mode = castValue(lastValue, llvm::Type::getInt32Ty(*context));
        } else {
            mode = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0); // F_OK
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_accessSync", ft);
        createCall(ft, fn.getCallee(), { path, mode });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "chmodSync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* mode = castValue(lastValue, llvm::Type::getInt32Ty(*context));
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_chmodSync", ft);
        createCall(ft, fn.getCallee(), { path, mode });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "chownSync") {
        if (node->arguments.size() < 3) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* uid = castValue(lastValue, llvm::Type::getInt32Ty(*context));
        visit(node->arguments[2].get());
        llvm::Value* gid = castValue(lastValue, llvm::Type::getInt32Ty(*context));
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), llvm::Type::getInt32Ty(*context), llvm::Type::getInt32Ty(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_chownSync", ft);
        createCall(ft, fn.getCallee(), { path, uid, gid });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "utimesSync") {
        if (node->arguments.size() < 3) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* atime = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[2].get());
        llvm::Value* mtime = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_utimesSync", ft);
        createCall(ft, fn.getCallee(), { path, atime, mtime });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "statfsSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_statfsSync", ft);
        lastValue = createCall(ft, fn.getCallee(), { path });
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateFSPropertyAccess(ast::PropertyAccessExpression* node) {
    // Check if the object is "fs"
    bool isFs = false;
    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (id->name == "fs") isFs = true;
    }
    
    // Also allow if the expression is 'any' (e.g. from require)
    if (!isFs && node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Any) {
        isFs = true; 
    }

    if (!isFs) return false;

    if (node->name == "constants") {
        // Return an object with constants
        // For now, we can just return a map with the constants
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_get_constants", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    } else if (node->name == "promises") {
        // Return an object with promise-based methods
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_get_promises", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    return false;
}

} // namespace ts
