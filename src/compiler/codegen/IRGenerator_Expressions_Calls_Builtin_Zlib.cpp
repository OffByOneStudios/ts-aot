/*
 * Zlib Module IR Generation
 *
 * Generates LLVM IR for zlib module method calls.
 */

#include "IRGenerator.h"
#include <spdlog/spdlog.h>

namespace ts {

bool IRGenerator::tryGenerateZlibCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "zlib") return false;

    const std::string& methodName = prop->name;
    SPDLOG_DEBUG("tryGenerateZlibCall: methodName={}", methodName);

    // ==========================================================================
    // Sync convenience methods
    // ==========================================================================

    if (methodName == "gzipSync") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_gzip_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { buffer, options });
        return true;
    }

    if (methodName == "gunzipSync") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_gunzip_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { buffer, options });
        return true;
    }

    if (methodName == "deflateSync") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_deflate_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { buffer, options });
        return true;
    }

    if (methodName == "inflateSync") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_inflate_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { buffer, options });
        return true;
    }

    if (methodName == "deflateRawSync") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_deflate_raw_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { buffer, options });
        return true;
    }

    if (methodName == "inflateRawSync") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_inflate_raw_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { buffer, options });
        return true;
    }

    if (methodName == "unzipSync") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_unzip_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { buffer, options });
        return true;
    }

    if (methodName == "brotliCompressSync") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_brotli_compress_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { buffer, options });
        return true;
    }

    if (methodName == "brotliDecompressSync") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_brotli_decompress_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { buffer, options });
        return true;
    }

    // ==========================================================================
    // Async convenience methods
    // ==========================================================================

    if (methodName == "gzip") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        llvm::Value* callback = nullptr;

        if (node->arguments.size() == 2) {
            // Could be (buffer, callback) or (buffer, options)
            // For simplicity, treat second arg as callback if it's a function
            visit(node->arguments[1].get());
            callback = lastValue;
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        } else if (node->arguments.size() >= 3) {
            visit(node->arguments[1].get());
            options = lastValue;
            visit(node->arguments[2].get());
            callback = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_gzip", ft);
        createCall(ft, fn.getCallee(), { buffer, options, callback });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "gunzip") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        llvm::Value* callback = nullptr;

        if (node->arguments.size() == 2) {
            visit(node->arguments[1].get());
            callback = lastValue;
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        } else if (node->arguments.size() >= 3) {
            visit(node->arguments[1].get());
            options = lastValue;
            visit(node->arguments[2].get());
            callback = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_gunzip", ft);
        createCall(ft, fn.getCallee(), { buffer, options, callback });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "deflate") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        llvm::Value* callback = nullptr;

        if (node->arguments.size() > 2) {
            visit(node->arguments[1].get());
            options = lastValue;
            visit(node->arguments[2].get());
            callback = lastValue;
        } else if (node->arguments.size() > 1) {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            visit(node->arguments[1].get());
            callback = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_deflate", ft);
        createCall(ft, fn.getCallee(), { buffer, options, callback });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "inflate") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        llvm::Value* callback = nullptr;

        if (node->arguments.size() > 2) {
            visit(node->arguments[1].get());
            options = lastValue;
            visit(node->arguments[2].get());
            callback = lastValue;
        } else if (node->arguments.size() > 1) {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            visit(node->arguments[1].get());
            callback = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_inflate", ft);
        createCall(ft, fn.getCallee(), { buffer, options, callback });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "brotliCompress") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        llvm::Value* callback = nullptr;

        if (node->arguments.size() > 2) {
            visit(node->arguments[1].get());
            options = lastValue;
            visit(node->arguments[2].get());
            callback = lastValue;
        } else if (node->arguments.size() > 1) {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            visit(node->arguments[1].get());
            callback = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_brotli_compress", ft);
        createCall(ft, fn.getCallee(), { buffer, options, callback });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "brotliDecompress") {
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::Value* options = nullptr;
        llvm::Value* callback = nullptr;

        if (node->arguments.size() > 2) {
            visit(node->arguments[1].get());
            options = lastValue;
            visit(node->arguments[2].get());
            callback = lastValue;
        } else if (node->arguments.size() > 1) {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            visit(node->arguments[1].get());
            callback = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_brotli_decompress", ft);
        createCall(ft, fn.getCallee(), { buffer, options, callback });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    // ==========================================================================
    // Stream creators
    // ==========================================================================

    if (methodName == "createGzip") {
        llvm::Value* options = nullptr;
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_create_gzip", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        return true;
    }

    if (methodName == "createGunzip") {
        llvm::Value* options = nullptr;
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_create_gunzip", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        return true;
    }

    if (methodName == "createDeflate") {
        llvm::Value* options = nullptr;
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_create_deflate", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        return true;
    }

    if (methodName == "createInflate") {
        llvm::Value* options = nullptr;
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_create_inflate", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        return true;
    }

    if (methodName == "createDeflateRaw") {
        llvm::Value* options = nullptr;
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_create_deflate_raw", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        return true;
    }

    if (methodName == "createInflateRaw") {
        llvm::Value* options = nullptr;
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_create_inflate_raw", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        return true;
    }

    if (methodName == "createUnzip") {
        llvm::Value* options = nullptr;
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_create_unzip", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        return true;
    }

    if (methodName == "createBrotliCompress") {
        llvm::Value* options = nullptr;
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_create_brotli_compress", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        return true;
    }

    if (methodName == "createBrotliDecompress") {
        llvm::Value* options = nullptr;
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            options = lastValue;
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        auto ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_create_brotli_decompress", ft);
        lastValue = createCall(ft, fn.getCallee(), { options });
        return true;
    }

    // ==========================================================================
    // Utility functions
    // ==========================================================================

    if (methodName == "crc32") {
        visit(node->arguments[0].get());
        llvm::Value* data = lastValue;

        llvm::Value* value = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            value = lastValue;
        } else {
            value = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
        }

        // Ensure value is i64
        if (value->getType() != builder->getInt64Ty()) {
            if (value->getType()->isPointerTy()) {
                value = builder->CreatePtrToInt(value, builder->getInt64Ty());
            }
        }

        auto ft = llvm::FunctionType::get(builder->getInt64Ty(),
            { builder->getPtrTy(), builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_crc32", ft);
        lastValue = createCall(ft, fn.getCallee(), { data, value });
        return true;
    }

    return false;
}

// Handle zlib.constants property access
bool IRGenerator::tryGenerateZlibConstantsAccess(ast::PropertyAccessExpression* prop) {
    // Check if this is zlib.constants.XXX
    auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get());
    if (!innerProp) return false;

    auto id = dynamic_cast<ast::Identifier*>(innerProp->expression.get());
    if (!id || id->name != "zlib") return false;

    if (innerProp->name != "constants") return false;

    // Get the constants object and then the specific constant
    auto ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_get_constants", ft);
    llvm::Value* constants = createCall(ft, fn.getCallee(), {});

    // Now get the specific constant by name - create a TsString from the property name
    llvm::Constant* keyCStr = builder->CreateGlobalStringPtr(prop->name);
    llvm::FunctionType* strFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee strFn = module->getOrInsertFunction("ts_string_create", strFt);
    llvm::Value* keyStr = createCall(strFt, strFn.getCallee(), { keyCStr });

    auto getFt = llvm::FunctionType::get(builder->getPtrTy(),
        { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get", getFt);
    lastValue = createCall(getFt, getFn.getCallee(), { constants, keyStr });

    return true;
}

bool IRGenerator::tryGenerateZlibPropertyAccess(ast::PropertyAccessExpression* node) {
    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (!id || id->name != "zlib") return false;

    const std::string& propName = node->name;

    // Handle zlib.constants property
    if (propName == "constants") {
        auto ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_zlib_get_constants", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    return false;
}

} // namespace ts
