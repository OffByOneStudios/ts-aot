#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

// Static helper to register FS module's runtime functions once (~73 functions)
static bool fsFunctionsRegistered = false;
static void ensureFSFunctionsRegistered(BoxingPolicy& bp) {
    if (fsFunctionsRegistered) return;
    fsFunctionsRegistered = true;
    
    // ========== FileHandle async methods (all take boxed handle, return promise) ==========
    bp.registerRuntimeApi("ts_fs_filehandle_close_async", {true}, true);
    bp.registerRuntimeApi("ts_fs_filehandle_stat_async", {true}, true);
    bp.registerRuntimeApi("ts_fs_filehandle_chmod_async", {true, false}, true);  // handle, mode
    bp.registerRuntimeApi("ts_fs_filehandle_chown_async", {true, false, false}, true);  // handle, uid, gid
    bp.registerRuntimeApi("ts_fs_filehandle_sync_async", {true}, true);
    bp.registerRuntimeApi("ts_fs_filehandle_datasync_async", {true}, true);
    bp.registerRuntimeApi("ts_fs_filehandle_truncate_async", {true, false}, true);  // handle, len
    bp.registerRuntimeApi("ts_fs_filehandle_utimes_async", {true, false, false}, true);  // handle, atime, mtime
    bp.registerRuntimeApi("ts_fs_filehandle_readv_async", {true, true, false}, true);  // handle, buffers, position
    bp.registerRuntimeApi("ts_fs_filehandle_writev_async", {true, true, false}, true);
    bp.registerRuntimeApi("ts_fs_filehandle_read_async", {true, true, false, false, false}, true);  // handle, buffer, offset, length, position
    bp.registerRuntimeApi("ts_fs_filehandle_write_async", {true, true, false, false, false}, true);
    bp.registerRuntimeApi("ts_fs_filehandle_get_fd", {true}, false);  // returns raw int
    
    // ========== fs.promises async functions (path-based) ==========
    bp.registerRuntimeApi("ts_fs_readFile_async", {false}, true);   // path (string)
    bp.registerRuntimeApi("ts_fs_writeFile_async", {false, true}, true);  // path, data (boxed)
    bp.registerRuntimeApi("ts_fs_access_async", {false, false}, true);  // path, mode
    bp.registerRuntimeApi("ts_fs_chmod_async", {false, false}, true);
    bp.registerRuntimeApi("ts_fs_chown_async", {false, false, false}, true);
    bp.registerRuntimeApi("ts_fs_utimes_async", {false, false, false}, true);
    bp.registerRuntimeApi("ts_fs_open_async", {false, false, false}, true);  // path, flags, mode
    bp.registerRuntimeApi("ts_fs_statfs_async", {false}, true);
    bp.registerRuntimeApi("ts_fs_link_async", {false, false}, true);  // existingPath, newPath
    bp.registerRuntimeApi("ts_fs_symlink_async", {false, false, false}, true);  // target, path, type
    bp.registerRuntimeApi("ts_fs_rename_async", {false, false}, true);
    bp.registerRuntimeApi("ts_fs_copyFile_async", {false, false, false}, true);  // src, dest, mode
    bp.registerRuntimeApi("ts_fs_truncate_async", {false, false}, true);  // path, len
    bp.registerRuntimeApi("ts_fs_appendFile_async", {false, true}, true);  // path, data
    bp.registerRuntimeApi("ts_fs_unlink_async", {false}, true);
    bp.registerRuntimeApi("ts_fs_mkdtemp_async", {false}, true);
    bp.registerRuntimeApi("ts_fs_opendir_async", {false}, true);
    bp.registerRuntimeApi("ts_fs_readdir_async", {false}, true);
    
    // ========== Synchronous FS functions ==========
    bp.registerRuntimeApi("ts_fs_readFileSync", {false}, false);  // returns TsString*
    bp.registerRuntimeApi("ts_fs_writeFileSync", {false, true}, false);  // path, data (boxed)
    bp.registerRuntimeApi("ts_fs_existsSync", {false}, false);  // returns bool
    bp.registerRuntimeApi("ts_fs_unlinkSync", {false}, false);
    bp.registerRuntimeApi("ts_fs_opendirSync", {false}, true);  // returns boxed Dir
    bp.registerRuntimeApi("ts_fs_readdirSync", {false}, true);  // returns boxed array
    bp.registerRuntimeApi("ts_fs_renameSync", {false, false}, false);
    bp.registerRuntimeApi("ts_fs_copyFileSync", {false, false, false}, false);
    bp.registerRuntimeApi("ts_fs_truncateSync", {false, false}, false);
    bp.registerRuntimeApi("ts_fs_appendFileSync", {false, true}, false);
    bp.registerRuntimeApi("ts_fs_mkdtempSync", {false}, false);  // returns string
    bp.registerRuntimeApi("ts_fs_openSync", {false, false, false}, false);  // returns fd (int)
    bp.registerRuntimeApi("ts_fs_closeSync", {false}, false);  // fd
    bp.registerRuntimeApi("ts_fs_accessSync", {false, false}, false);
    bp.registerRuntimeApi("ts_fs_chmodSync", {false, false}, false);
    bp.registerRuntimeApi("ts_fs_chownSync", {false, false, false}, false);
    bp.registerRuntimeApi("ts_fs_utimesSync", {false, false, false}, false);
    bp.registerRuntimeApi("ts_fs_statfsSync", {false}, true);  // returns boxed object
    bp.registerRuntimeApi("ts_fs_linkSync", {false, false}, false);
    bp.registerRuntimeApi("ts_fs_symlinkSync", {false, false, false}, false);
    
    // ========== fd-based sync functions (fchmod, fchown, etc.) ==========
    bp.registerRuntimeApi("ts_fs_fchmod", {false, false}, false);  // fd, mode
    bp.registerRuntimeApi("ts_fs_fchmodSync", {false, false}, false);
    bp.registerRuntimeApi("ts_fs_fchown", {false, false, false}, false);  // fd, uid, gid
    bp.registerRuntimeApi("ts_fs_fchownSync", {false, false, false}, false);
    bp.registerRuntimeApi("ts_fs_futimes", {false, false, false}, false);  // fd, atime, mtime
    bp.registerRuntimeApi("ts_fs_futimesSync", {false, false, false}, false);
    bp.registerRuntimeApi("ts_fs_fstat", {false}, true);  // fd -> boxed Stats
    bp.registerRuntimeApi("ts_fs_fstatSync", {false}, true);
    bp.registerRuntimeApi("ts_fs_ftruncate", {false, false}, false);  // fd, len
    bp.registerRuntimeApi("ts_fs_ftruncateSync", {false, false}, false);
    
    // ========== Callback-style async (legacy) ==========
    bp.registerRuntimeApi("ts_fs_open", {false, false, false, true}, false);  // path, flags, mode, callback
    bp.registerRuntimeApi("ts_fs_close", {false, true}, false);  // fd, callback
    
    // ========== Stream creation ==========
    bp.registerRuntimeApi("ts_fs_createReadStream", {false, true}, true);  // path, options -> boxed stream
    bp.registerRuntimeApi("ts_fs_createWriteStream", {false, true}, true);
    
    // ========== Watch functions ==========
    bp.registerRuntimeApi("ts_fs_watch", {false, true, true}, true);  // path, options, callback -> boxed watcher
    bp.registerRuntimeApi("ts_fs_watchFile", {false, true, true}, true);
    bp.registerRuntimeApi("ts_fs_unwatchFile", {false, true}, false);  // path, listener
    
    // ========== Module-level getters ==========
    bp.registerRuntimeApi("ts_fs_get_constants", {}, true);  // returns boxed object
    bp.registerRuntimeApi("ts_fs_get_promises", {}, true);
    
    // ========== Helper functions used in FS codegen ==========
    bp.registerRuntimeApi("ts_object_get_property", {true, false}, true);  // obj (boxed), key (string) -> boxed
    bp.registerRuntimeApi("ts_value_make_int", {false}, true);  // int -> boxed
    bp.registerRuntimeApi("ts_value_make_object", {true}, true);  // ptr -> boxed
}

bool IRGenerator::tryGenerateFSCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Register boxing info for FS functions on first call
    ensureFSFunctionsRegistered(boxingPolicy);
    
    // Check if the object is "fs" or "fs.promises"
    bool isFs = false;
    bool isFsPromises = false;

    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
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

    bool isFileHandle = false;
    if (!isFs && !isFsPromises) {
        // Check if it's a FileHandle
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Object) {
            auto objType = std::static_pointer_cast<ObjectType>(prop->expression->inferredType);
            if (objType->fields.count("fd")) {
                isFileHandle = true;
            }
        }
    }

    if (!isFs && !isFsPromises && !isFileHandle) return false;

    if (isFileHandle) {
        SPDLOG_INFO("tryGenerateFSCall: isFileHandle=true, method={}", prop->name);
        if (prop->name == "close") {
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            SPDLOG_INFO("tryGenerateFSCall: filehandle.close h={}", (void*)h);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_close_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h });
            return true;
        } else if (prop->name == "stat") {
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            SPDLOG_INFO("tryGenerateFSCall: filehandle.stat h={}", (void*)h);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_stat_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h });
            return true;
        } else if (prop->name == "chmod") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            SPDLOG_INFO("tryGenerateFSCall: filehandle.chmod h={}", (void*)h);
            visit(node->arguments[0].get());
            llvm::Value* mode = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_chmod_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h, mode });
            return true;
        } else if (prop->name == "chown") {
            if (node->arguments.size() < 2) return true;
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            SPDLOG_INFO("tryGenerateFSCall: filehandle.chown h={}", (void*)h);
            visit(node->arguments[0].get());
            llvm::Value* uid = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[1].get());
            llvm::Value* gid = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_chown_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h, uid, gid });
            return true;
        } else if (prop->name == "sync") {
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            SPDLOG_INFO("tryGenerateFSCall: filehandle.sync h={}", (void*)h);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_sync_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h });
            return true;
        } else if (prop->name == "datasync") {
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            SPDLOG_INFO("tryGenerateFSCall: filehandle.datasync h={}", (void*)h);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_datasync_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h });
            return true;
        } else if (prop->name == "truncate") {
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            SPDLOG_INFO("tryGenerateFSCall: filehandle.truncate h={}", (void*)h);
            llvm::Value* len = nullptr;
            if (node->arguments.size() > 0) {
                visit(node->arguments[0].get());
                len = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                len = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_truncate_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h, len });
            return true;
        } else if (prop->name == "utimes") {
            if (node->arguments.size() < 2) return true;
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            visit(node->arguments[0].get());
            llvm::Value* atime = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[1].get());
            llvm::Value* mtime = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_utimes_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h, atime, mtime });
            return true;
        } else if (prop->name == "readv") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            visit(node->arguments[0].get());
            llvm::Value* buffers = lastValue;
            llvm::Value* position = nullptr;
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                position = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_readv_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h, buffers, position });
            return true;
        } else if (prop->name == "writev") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            visit(node->arguments[0].get());
            llvm::Value* buffers = lastValue;
            llvm::Value* position = nullptr;
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                position = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_writev_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h, buffers, position });
            return true;
        } else if (prop->name == "read") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            SPDLOG_INFO("tryGenerateFSCall: filehandle.read h={}", (void*)h);
            visit(node->arguments[0].get());
            llvm::Value* buffer = lastValue;
            
            llvm::Value* offset = nullptr;
            llvm::Value* length = nullptr;
            llvm::Value* position = nullptr;
            
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                offset = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                offset = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
            }
            
            if (node->arguments.size() > 2) {
                visit(node->arguments[2].get());
                length = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                length = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            }
            
            if (node->arguments.size() > 3) {
                visit(node->arguments[3].get());
                position = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                    { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_read_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h, buffer, offset, length, position });
            boxedValues.insert(lastValue);
            return true;
        } else if (prop->name == "write") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            SPDLOG_INFO("tryGenerateFSCall: filehandle.write h={}", (void*)h);
            visit(node->arguments[0].get());
            llvm::Value* buffer = lastValue;
            if (node->arguments[0]->inferredType && node->arguments[0]->inferredType->kind == TypeKind::Class) {
                 auto classType = std::static_pointer_cast<ClassType>(node->arguments[0]->inferredType);
                 if (classType->name == "Buffer") {
                     llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                     llvm::FunctionCallee boxFn = getRuntimeFunction("ts_value_make_object", boxFt);
                     buffer = createCall(boxFt, boxFn.getCallee(), { buffer });
                 } else {
                     buffer = boxValue(lastValue, node->arguments[0]->inferredType);
                 }
            } else {
                buffer = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            
            llvm::Value* offset = nullptr;
            llvm::Value* length = nullptr;
            llvm::Value* position = nullptr;
            
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                offset = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                offset = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
            }
            
            if (node->arguments.size() > 2) {
                visit(node->arguments[2].get());
                length = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                length = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            }
            
            if (node->arguments.size() > 3) {
                visit(node->arguments[3].get());
                position = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                    { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_write_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { h, buffer, offset, length, position });
            boxedValues.insert(lastValue);
            return true;
        } else if (prop->name == "readv" || prop->name == "writev") {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* h = boxValue(lastValue, prop->expression->inferredType);
            visit(node->arguments[0].get());
            llvm::Value* buffers = boxValue(lastValue, node->arguments[0]->inferredType);
            
            llvm::Value* position = nullptr;
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                position = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            }
            
            std::string runtimeName = "ts_fs_filehandle_" + prop->name + "_async";
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                    { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
            lastValue = createCall(ft, fn.getCallee(), { h, buffers, position });
            boxedValues.insert(lastValue);
            return true;
        }
        return false;
    }

    SPDLOG_DEBUG("tryGenerateFSCall: {} (isFsPromises: {})", prop->name, isFsPromises);

    if (isFsPromises) {
        if (prop->name == "readFile") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_readFile_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path });
            return true;
        } else if (prop->name == "writeFile") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* data = boxValue(lastValue, node->arguments[1]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_writeFile_async", ft);
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
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_access_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, mode });
            return true;
        } else if (prop->name == "chmod") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* mode = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_chmod_async", ft);
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
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_chown_async", ft);
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
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_utimes_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, atime, mtime });
            return true;
        } else if (prop->name == "open") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::Value* flags = nullptr;
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                flags = boxValue(lastValue, node->arguments[1]->inferredType);
            } else {
                flags = boxValue(llvm::ConstantPointerNull::get(builder->getPtrTy()), std::make_shared<Type>(TypeKind::Null));
            }
            llvm::Value* mode = nullptr;
            if (node->arguments.size() > 2) {
                visit(node->arguments[2].get());
                mode = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                mode = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 438.0); // 0o666
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                    { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_open_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, flags, mode });
            SPDLOG_INFO("tryGenerateFSCall: open_async lastValue={} (type={})", (void*)lastValue, (int)node->inferredType->kind);
            boxedValues.insert(lastValue);
            return true;
        } else if (prop->name == "statfs") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_statfs_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path });
            return true;
        } else if (prop->name == "link") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* existingPath = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* newPath = boxValue(lastValue, node->arguments[1]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_link_async", ft);
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
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_symlink_async", ft);
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
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_rename_async", ft);
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
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_copyFile_async", ft);
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
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_truncate_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, len });
            return true;
        } else if (prop->name == "appendFile") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* data = boxValue(lastValue, node->arguments[1]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_appendFile_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, data });
            return true;
        } else if (prop->name == "unlink") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_unlink_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path });
            return true;
        } else if (prop->name == "readv" || prop->name == "writev") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* handle = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* buffers = boxValue(lastValue, node->arguments[1]->inferredType);
            
            llvm::Value* position = nullptr;
            if (node->arguments.size() > 2) {
                visit(node->arguments[2].get());
                position = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            } else {
                position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            }
            
            llvm::Value* fd = nullptr;
            if (node->arguments[0]->inferredType->kind == TypeKind::Object) {
                // Extract fd from FileHandle object
                llvm::Value* objPtr = unboxValue(handle, node->arguments[0]->inferredType);
                
                llvm::FunctionType* getPropFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee getPropFn = getRuntimeFunction("ts_object_get_property", getPropFt);
                llvm::Value* fdKey = builder->CreateGlobalStringPtr("fd");
                llvm::Value* boxedFd = createCall(getPropFt, getPropFn.getCallee(), { objPtr, fdKey });
                boxedValues.insert(boxedFd);
                
                fd = castValue(boxedFd, llvm::Type::getInt64Ty(*context));
            } else {
                fd = castValue(handle, llvm::Type::getInt64Ty(*context));
            }
            
            std::string runtimeName = "ts_fs_" + prop->name + "_async";
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                    { llvm::Type::getInt64Ty(*context), builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
            lastValue = createCall(ft, fn.getCallee(), { fd, buffers, position });
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
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_mkdtemp_async", ft);
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
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_opendir_async", ft);
            lastValue = createCall(ft, fn.getCallee(), { path, options });
            return true;
        } else if (prop->name == "readdir") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_readdir_async", ft);
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
        llvm::FunctionCallee readFn = getRuntimeFunction("ts_fs_readFileSync", readFt);
        
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
        llvm::FunctionCallee writeFn = getRuntimeFunction("ts_fs_writeFileSync", writeFt);
        
        createCall(writeFt, writeFn.getCallee(), { path, data });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "existsSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::FunctionType* existsFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), 
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee existsFn = getRuntimeFunction("ts_fs_existsSync", existsFt);
        
        lastValue = createCall(existsFt, existsFn.getCallee(), { path });
        return true;
    } else if (prop->name == "unlinkSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_unlinkSync", ft);
        
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_opendirSync", ft);
        lastValue = createCall(ft, fn.getCallee(), { path, options });
        return true;
    } else if (prop->name == "watch") {
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

        llvm::Value* listener = nullptr;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            listener = boxValue(lastValue, node->arguments[2]->inferredType);
        } else {
            listener = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_watch", ft);
        lastValue = createCall(ft, fn.getCallee(), { path, options, listener });
        return true;
    } else if (prop->name == "watchFile") {
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

        llvm::Value* listener = nullptr;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            listener = boxValue(lastValue, node->arguments[2]->inferredType);
        } else {
            listener = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_watchFile", ft);
        createCall(ft, fn.getCallee(), { path, options, listener });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    } else if (prop->name == "unwatchFile") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
        
        llvm::Value* listener = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            listener = boxValue(lastValue, node->arguments[1]->inferredType);
        } else {
            listener = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        if (!listener) {
            listener = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_unwatchFile", ft);
        createCall(ft, fn.getCallee(), { path, listener });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    } else if (prop->name == "readdirSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_readdirSync", ft);
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_renameSync", ft);
        
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_copyFileSync", ft);
        
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_truncateSync", ft);
        
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_appendFileSync", ft);
        
        createCall(ft, fn.getCallee(), { path, data });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "mkdtempSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* prefix = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_mkdtempSync", ft);
        
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_openSync", ft);
        
        llvm::Value* fd = createCall(ft, fn.getCallee(), { path, flags });
        
        llvm::FunctionType* makeIntFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee makeIntFn = getRuntimeFunction("ts_value_make_int", makeIntFt);
        lastValue = createCall(makeIntFt, makeIntFn.getCallee(), { fd });
        return true;
    } else if (prop->name == "closeSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_closeSync", ft);
        
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
        llvm::FunctionCallee makeIntFn = getRuntimeFunction("ts_value_make_int", makeIntFt);
        lastValue = createCall(makeIntFt, makeIntFn.getCallee(), { result });
        return true;
    } else if (prop->name == "createReadStream") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_createReadStream", ft);
        lastValue = createCall(ft, fn.getCallee(), { path });
        return true;
    } else if (prop->name == "createWriteStream") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_createWriteStream", ft);
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_accessSync", ft);
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_chmodSync", ft);
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_chownSync", ft);
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_utimesSync", ft);
        createCall(ft, fn.getCallee(), { path, atime, mtime });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "statfsSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_statfsSync", ft);
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_linkSync", ft);
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
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_symlinkSync", ft);
        createCall(ft, fn.getCallee(), { target, path, type });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "fchmod") {
        if (node->arguments.size() < 3) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* mode = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[2].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[2]->inferredType);
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_fchmod", ft);
        createCall(ft, fn.getCallee(), { fd, mode, callback });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "fchown") {
        if (node->arguments.size() < 4) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* uid = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[2].get());
        llvm::Value* gid = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[3].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[3]->inferredType);
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_fchown", ft);
        createCall(ft, fn.getCallee(), { fd, uid, gid, callback });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "futimes") {
        if (node->arguments.size() < 4) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* atime = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[2].get());
        llvm::Value* mtime = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[3].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[3]->inferredType);
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_futimes", ft);
        createCall(ft, fn.getCallee(), { fd, atime, mtime, callback });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "fstat") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[1]->inferredType);
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_fstat", ft);
        createCall(ft, fn.getCallee(), { fd, callback });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "fsync" || prop->name == "fdatasync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[1]->inferredType);
        std::string runtimeName = "ts_fs_" + prop->name;
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
        createCall(ft, fn.getCallee(), { fd, callback });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "ftruncate") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        
        llvm::Value* len = nullptr;
        llvm::Value* callback = nullptr;
        if (node->arguments.size() > 2) {
            visit(node->arguments[1].get());
            len = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[2].get());
            callback = boxValue(lastValue, node->arguments[2]->inferredType);
        } else {
            len = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
            visit(node->arguments[1].get());
            callback = boxValue(lastValue, node->arguments[1]->inferredType);
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_ftruncate", ft);
        createCall(ft, fn.getCallee(), { fd, len, callback });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "readv" || prop->name == "writev") {
        if (node->arguments.size() < 3) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* buffers = boxValue(lastValue, node->arguments[1]->inferredType);
        
        llvm::Value* position = nullptr;
        llvm::Value* callback = nullptr;
        if (node->arguments.size() > 3) {
            visit(node->arguments[2].get());
            position = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[3].get());
            callback = boxValue(lastValue, node->arguments[3]->inferredType);
        } else {
            position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            visit(node->arguments[2].get());
            callback = boxValue(lastValue, node->arguments[2]->inferredType);
        }
        
        std::string runtimeName = "ts_fs_" + prop->name;
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), builder->getPtrTy(), llvm::Type::getDoubleTy(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
        createCall(ft, fn.getCallee(), { fd, buffers, position, callback });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "open") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        
        llvm::Value* flags = nullptr;
        llvm::Value* mode = nullptr;
        llvm::Value* callback = nullptr;
        
        if (node->arguments.size() == 2) {
            flags = boxValue(llvm::ConstantPointerNull::get(builder->getPtrTy()), std::make_shared<Type>(TypeKind::Null));
            mode = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
            visit(node->arguments[1].get());
            callback = boxValue(lastValue, node->arguments[1]->inferredType);
        } else if (node->arguments.size() == 3) {
            visit(node->arguments[1].get());
            flags = boxValue(lastValue, node->arguments[1]->inferredType);
            mode = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
            visit(node->arguments[2].get());
            callback = boxValue(lastValue, node->arguments[2]->inferredType);
        } else {
            visit(node->arguments[1].get());
            flags = boxValue(lastValue, node->arguments[1]->inferredType);
            visit(node->arguments[2].get());
            mode = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[3].get());
            callback = boxValue(lastValue, node->arguments[3]->inferredType);
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_open", ft);
        createCall(ft, fn.getCallee(), { path, flags, mode, callback });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "close") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[1]->inferredType);
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_close", ft);
        createCall(ft, fn.getCallee(), { fd, callback });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "read" || prop->name == "write") {
        if (node->arguments.size() < 3) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* buffer = boxValue(lastValue, node->arguments[1]->inferredType);
        
        llvm::Value* offset = nullptr;
        llvm::Value* length = nullptr;
        llvm::Value* position = nullptr;
        llvm::Value* callback = nullptr;
        
        if (node->arguments.size() == 3) {
            offset = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
            length = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            visit(node->arguments[2].get());
            callback = boxValue(lastValue, node->arguments[2]->inferredType);
        } else if (node->arguments.size() == 4) {
            visit(node->arguments[2].get());
            offset = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            length = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            visit(node->arguments[3].get());
            callback = boxValue(lastValue, node->arguments[3]->inferredType);
        } else if (node->arguments.size() == 5) {
            visit(node->arguments[2].get());
            offset = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[3].get());
            length = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
            visit(node->arguments[4].get());
            callback = boxValue(lastValue, node->arguments[4]->inferredType);
        } else {
            visit(node->arguments[2].get());
            offset = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[3].get());
            length = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[4].get());
            position = castValue(lastValue, llvm::Type::getDoubleTy(*context));
            visit(node->arguments[5].get());
            callback = boxValue(lastValue, node->arguments[5]->inferredType);
        }
        
        std::string runtimeName = "ts_fs_" + prop->name;
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), builder->getPtrTy(), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
        createCall(ft, fn.getCallee(), { fd, buffer, offset, length, position, callback });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "openSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;
        llvm::Value* flags = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            flags = boxValue(lastValue, node->arguments[1]->inferredType);
        } else {
            flags = boxValue(llvm::ConstantPointerNull::get(builder->getPtrTy()), std::make_shared<Type>(TypeKind::Null));
        }
        llvm::Value* mode = nullptr;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            mode = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        } else {
            mode = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), 
                { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_openSync", ft);
        lastValue = createCall(ft, fn.getCallee(), { path, flags, mode });
        return true;
    } else if (prop->name == "closeSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_closeSync", ft);
        createCall(ft, fn.getCallee(), { fd });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "readSync" || prop->name == "writeSync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* buffer = boxValue(lastValue, node->arguments[1]->inferredType);
        
        llvm::Value* offset = nullptr;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            offset = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        } else {
            offset = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
        }
        
        llvm::Value* length = nullptr;
        if (node->arguments.size() > 3) {
            visit(node->arguments[3].get());
            length = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        } else {
            length = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
        }
        
        llvm::Value* position = nullptr;
        if (node->arguments.size() > 4) {
            visit(node->arguments[4].get());
            position = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        } else {
            position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
        }
        
        std::string runtimeName = "ts_fs_" + prop->name;
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), 
                { llvm::Type::getDoubleTy(*context), builder->getPtrTy(), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
        lastValue = createCall(ft, fn.getCallee(), { fd, buffer, offset, length, position });
        return true;
    } else if (prop->name == "fchmodSync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* mode = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_fchmodSync", ft);
        createCall(ft, fn.getCallee(), { fd, mode });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "fchownSync") {
        if (node->arguments.size() < 3) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* uid = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[2].get());
        llvm::Value* gid = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_fchownSync", ft);
        createCall(ft, fn.getCallee(), { fd, uid, gid });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "futimesSync") {
        if (node->arguments.size() < 3) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* atime = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[2].get());
        llvm::Value* mtime = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_futimesSync", ft);
        createCall(ft, fn.getCallee(), { fd, atime, mtime });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "fstatSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                { llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_fstatSync", ft);
        lastValue = createCall(ft, fn.getCallee(), { fd });
        return true;
    } else if (prop->name == "fsyncSync" || prop->name == "fdatasyncSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        std::string runtimeName = "ts_fs_" + prop->name;
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
        createCall(ft, fn.getCallee(), { fd });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "ftruncateSync") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        llvm::Value* len = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            len = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        } else {
            len = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0);
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_ftruncateSync", ft);
        createCall(ft, fn.getCallee(), { fd, len });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "readvSync" || prop->name == "writevSync") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* fd = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        visit(node->arguments[1].get());
        llvm::Value* buffers = boxValue(lastValue, node->arguments[1]->inferredType);
        llvm::Value* position = nullptr;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            position = castValue(lastValue, llvm::Type::getDoubleTy(*context));
        } else {
            position = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), -1.0);
        }
        std::string runtimeName = "ts_fs_" + prop->name;
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), 
                { llvm::Type::getDoubleTy(*context), builder->getPtrTy(), llvm::Type::getDoubleTy(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(runtimeName, ft);
        lastValue = createCall(ft, fn.getCallee(), { fd, buffers, position });
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
    SPDLOG_INFO("tryGenerateFSPropertyAccess: node->name={}", node->name);
    // Check if the object is "fs"
    bool isFs = false;
    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (id->name == "fs") isFs = true;
    }
    
    // Only treat as fs if the identifier is "fs", not for any Any type
    // This was incorrectly claiming all Any types as fs before

    if (!isFs) {
        // Check if it's a FileHandle
        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Object) {
            auto objType = std::static_pointer_cast<ObjectType>(node->expression->inferredType);
            if (objType->fields.count("fd")) {
                if (node->name == "fd") {
                    visit(node->expression.get());
                    SPDLOG_INFO("tryGenerateFSPropertyAccess: fd lastValue={}, boxed={}", (void*)lastValue, (int)boxedValues.count(lastValue));
                    llvm::Value* h = boxValue(lastValue, node->expression->inferredType);
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_filehandle_get_fd", ft);
                    lastValue = createCall(ft, fn.getCallee(), { h });
                    return true;
                }
            }
        }
        return false;
    }

    if (node->name == "constants") {
        // Return an object with constants
        // For now, we can just return a map with the constants
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_get_constants", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    } else if (node->name == "promises") {
        // Return an object with promise-based methods
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_fs_get_promises", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    return false;
}

} // namespace ts
