#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

bool IRGenerator::tryGenerateFSCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check if the object is "fs" or "fs.promises"
    bool isFs = false;
    bool isFsPromises = false;

    printf("DEBUG: tryGenerateFSCall %s\n", prop->name.c_str());

    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        printf("DEBUG: tryGenerateFSCall id=%s\n", id->name.c_str());
        if (id->name == "fs") isFs = true;
    } else if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
        if (innerProp->name == "promises") {
            if (auto id = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                if (id->name == "fs") isFsPromises = true;
            }
        }
    }
    
    // Also allow if the expression is 'any' (e.g. from require)
    if (!isFs && !isFsPromises && prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
        isFs = true; 
    }

    if (!isFs && !isFsPromises) return false;

    SPDLOG_DEBUG("tryGenerateFSCall: {} (isFsPromises: {})", prop->name, isFsPromises);

    if (isFsPromises) {
        if (prop->name == "readFile") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_readFile_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path });
            return true;
        } else if (prop->name == "writeFile") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* data = boxValue(lastValue, node->arguments[1]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_writeFile_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, data });
            return true;
        } else if (prop->name == "access") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::Value* mode = nullptr;
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                mode = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                mode = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_access_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, mode });
            return true;
        } else if (prop->name == "chmod") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* mode = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_chmod_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, mode });
            return true;
        } else if (prop->name == "chown") {
            if (node->arguments.size() < 3) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* uid = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[2].get());
            llvm::Value* gid = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_chown_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, uid, gid });
            return true;
        } else if (prop->name == "utimes") {
            if (node->arguments.size() < 3) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* atime = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[2].get());
            llvm::Value* mtime = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_utimes_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, atime, mtime });
            return true;
        } else if (prop->name == "statfs") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_statfs_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path });
            return true;
        } else if (prop->name == "link") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* existingPath = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* newPath = boxValue(lastValue, node->arguments[1]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_link_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { existingPath, newPath });
            return true;
        } else if (prop->name == "symlink") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[1]->inferredType);
            llvm::Value* type = nullptr;
            if (node->arguments.size() > 2) {
                visit(node->arguments[2].get());
                type = boxValue(lastValue, node->arguments[2]->inferredType);
            } else {
                type = llvm::ConstantPointerNull::get(builder->getPtrTy());
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_symlink_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { target, path, type });
            return true;
        } else if (prop->name == "readlink" || prop->name == "realpath" || prop->name == "lstat" || prop->name == "stat") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            std::string runtimeName = "ts_fs_" + prop->name + "_async";
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
            lastValue = createCall(ft, fn.getCallee(), { path });
            return true;
        } else if (prop->name == "rename") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* oldPath = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* newPath = boxValue(lastValue, node->arguments[1]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_rename_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { oldPath, newPath });
            return true;
        } else if (prop->name == "copyFile") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* src = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* dest = boxValue(lastValue, node->arguments[1]->inferredType);
            llvm::Value* flags = nullptr;
            if (node->arguments.size() > 2) {
                visit(node->arguments[2].get());
                flags = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                flags = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_copyFile_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { src, dest, flags });
            return true;
        } else if (prop->name == "truncate") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::Value* len = nullptr;
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                len = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                len = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_truncate_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, len });
            return true;
        } else if (prop->name == "appendFile") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* data = boxValue(lastValue, node->arguments[1]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_appendFile_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, data });
            return true;
        } else if (prop->name == "mkdir" || prop->name == "rmdir" || prop->name == "rm") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::Value* options = nullptr;
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                options = boxValue(lastValue, node->arguments[1]->inferredType);
            } else {
                options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            }
            std::string runtimeName = "ts_fs_" + prop->name + "_async";
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
            lastValue = createCall(ft, fn.getCallee(), { path, options });
            return true;
        } else if (prop->name == "mkdtemp") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* prefix = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_mkdtemp_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { prefix });
            return true;
        } else if (prop->name == "opendir") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::Value* options = nullptr;
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                options = boxValue(lastValue, node->arguments[1]->inferredType);
            } else {
                options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_opendir_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, options });
            return true;
        } else if (prop->name == "readdir") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_readdir_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path });
            return true;
        }
        return false;
    }

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
    } else if (prop->name == "unlinkSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_unlinkSync", ft);
        
        createCall(ft, fn.getCallee(), { path });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "mkdirSync" || prop->name == "rmdirSync" || prop->name == "rmSync") {
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
        
        std::string runtimeName = "ts_fs_" + prop->name;
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
        
        createCall(ft, fn.getCallee(), { path, options });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "opendirSync") {
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
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_opendirSync", ft);
        lastValue = createCall(ft, fn.getCallee(), { path, options });
        return true;
    } else if (prop->name == "readdirSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_readdirSync", ft);
        lastValue = createCall(ft, fn.getCallee(), { path });
        return true;
    } else if (prop->name == "renameSync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* oldPath = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* newPath = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_renameSync", ft);
        
        createCall(ft, fn.getCallee(), { oldPath, newPath });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "copyFileSync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* src = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* dest = lastValue;
        llvm::Value* flags = nullptr;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            flags = castValue(lastValue, llvm::Type::getInt32Ty(*context));
        } else {
            flags = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0);
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_copyFileSync", ft);
        
        createCall(ft, fn.getCallee(), { src, dest, flags });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "truncateSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        llvm::Value* len = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            len = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        } else {
            len = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_truncateSync", ft);
        
        createCall(ft, fn.getCallee(), { path, len });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "appendFileSync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* data = boxValue(lastValue, node->arguments[1]->inferredType);
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_appendFileSync", ft);
        
        createCall(ft, fn.getCallee(), { path, data });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "mkdtempSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* prefix = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_mkdtempSync", ft);
        
        lastValue = createCall(ft, fn.getCallee(), { prefix });
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
    } else if (prop->name == "linkSync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* existingPath = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* newPath = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_linkSync", ft);
        createCall(ft, fn.getCallee(), { existingPath, newPath });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "symlinkSync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* target = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* path = lastValue;
        llvm::Value* type = nullptr;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            type = lastValue;
        } else {
            type = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_fs_symlinkSync", ft);
        createCall(ft, fn.getCallee(), { target, path, type });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "readlinkSync" || prop->name == "realpathSync" || prop->name == "lstatSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        std::string runtimeName = "ts_fs_" + prop->name;
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
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
