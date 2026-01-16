#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register URL module's runtime functions once (10 functions)
static bool urlFunctionsRegistered = false;
static void ensureURLFunctionsRegistered(BoxingPolicy& bp) {
    if (urlFunctionsRegistered) return;
    urlFunctionsRegistered = true;
    
    // URL instance methods
    bp.registerRuntimeApi("ts_url_to_string", {true}, false);  // url -> string
    bp.registerRuntimeApi("ts_url_to_json", {true}, false);  // url -> string
    
    // URLSearchParams methods
    bp.registerRuntimeApi("ts_url_search_params_append", {true, false, false}, false);  // params, name, value
    bp.registerRuntimeApi("ts_url_search_params_delete", {true, false}, false);
    bp.registerRuntimeApi("ts_url_search_params_get", {true, false}, false);  // returns string or null
    bp.registerRuntimeApi("ts_url_search_params_get_all", {true, false}, true);  // returns array
    bp.registerRuntimeApi("ts_url_search_params_has", {true, false}, false);  // returns bool
    bp.registerRuntimeApi("ts_url_search_params_set", {true, false, false}, false);
    bp.registerRuntimeApi("ts_url_search_params_sort", {true}, false);
    bp.registerRuntimeApi("ts_url_search_params_to_string", {true}, false);

    // URLSearchParams iterator methods
    bp.registerRuntimeApi("ts_url_search_params_entries", {true}, false);  // params -> array
    bp.registerRuntimeApi("ts_url_search_params_keys", {true}, false);     // params -> array
    bp.registerRuntimeApi("ts_url_search_params_values", {true}, false);   // params -> array
    bp.registerRuntimeApi("ts_url_search_params_for_each", {true, true, true}, false);  // params, callback, thisArg

    // url module static functions
    bp.registerRuntimeApi("ts_url_file_url_to_path", {true}, false);  // url -> string
    bp.registerRuntimeApi("ts_url_path_to_file_url", {true}, true);   // path -> URL
    bp.registerRuntimeApi("ts_url_format", {true, true}, false);      // urlObj, options -> string
    bp.registerRuntimeApi("ts_url_resolve", {true, true}, false);     // from, to -> string
    bp.registerRuntimeApi("ts_url_to_http_options", {true}, true);    // url -> options object
}

bool IRGenerator::tryGenerateURLCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureURLFunctionsRegistered(boxingPolicy);
    
    bool isURL = false;
    bool isURLSearchParams = false;
    
    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
        if (classType->name == "URL") isURL = true;
        else if (classType->name == "URLSearchParams") isURLSearchParams = true;
    }
    
    if (!isURL && !isURLSearchParams) return false;
    
    SPDLOG_DEBUG("tryGenerateURLCall: {}.{}", isURL ? "URL" : "URLSearchParams", prop->name);
    
    if (isURL) {
        visit(prop->expression.get());
        llvm::Value* url = lastValue;
        emitNullCheckForExpression(prop->expression.get(), url);
        
        if (prop->name == "toString" || prop->name == "toJSON") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            std::string fnName = prop->name == "toString" ? "ts_url_to_string" : "ts_url_to_json";
            llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
            lastValue = createCall(ft, fn.getCallee(), { url });
            return true;
        }
    }
    
    if (isURLSearchParams) {
        visit(prop->expression.get());
        llvm::Value* params = lastValue;
        emitNullCheckForExpression(prop->expression.get(), params);
        
        if (prop->name == "append") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            visit(node->arguments[1].get());
            llvm::Value* value = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), 
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_append", ft);
            createCall(ft, fn.getCallee(), { params, name, value });
            lastValue = getUndefinedValue();
            return true;
        }
        
        if (prop->name == "delete") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_delete", ft);
            createCall(ft, fn.getCallee(), { params, name });
            lastValue = getUndefinedValue();
            return true;
        }
        
        if (prop->name == "get") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_get", ft);
            lastValue = createCall(ft, fn.getCallee(), { params, name });
            return true;
        }
        
        if (prop->name == "getAll") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_get_all", ft);
            lastValue = createCall(ft, fn.getCallee(), { params, name });
            return true;
        }
        
        if (prop->name == "has") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_has", ft);
            lastValue = createCall(ft, fn.getCallee(), { params, name });
            return true;
        }
        
        if (prop->name == "set") {
            if (node->arguments.size() < 2) return true;
            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            visit(node->arguments[1].get());
            llvm::Value* value = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), 
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_set", ft);
            createCall(ft, fn.getCallee(), { params, name, value });
            lastValue = getUndefinedValue();
            return true;
        }
        
        if (prop->name == "sort") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), 
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_sort", ft);
            createCall(ft, fn.getCallee(), { params });
            lastValue = getUndefinedValue();
            return true;
        }
        
        if (prop->name == "toString") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_to_string", ft);
            lastValue = createCall(ft, fn.getCallee(), { params });
            return true;
        }

        if (prop->name == "entries") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_entries", ft);
            lastValue = createCall(ft, fn.getCallee(), { params });
            return true;
        }

        if (prop->name == "keys") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_keys", ft);
            lastValue = createCall(ft, fn.getCallee(), { params });
            return true;
        }

        if (prop->name == "values") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_values", ft);
            lastValue = createCall(ft, fn.getCallee(), { params });
            return true;
        }

        if (prop->name == "forEach") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* callback = lastValue;

            llvm::Value* thisArg = nullptr;
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                thisArg = lastValue;
            } else {
                thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_for_each", ft);
            createCall(ft, fn.getCallee(), { params, callback, thisArg });
            lastValue = getUndefinedValue();
            return true;
        }
    }

    return false;
}

bool IRGenerator::tryGenerateURLModuleCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureURLFunctionsRegistered(boxingPolicy);

    // Check if this is a url.* call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "url") return false;

    SPDLOG_DEBUG("tryGenerateURLModuleCall: url.{}", prop->name);

    if (prop->name == "fileURLToPath") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* url = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_url_file_url_to_path", ft);
        lastValue = createCall(ft, fn.getCallee(), { url });
        return true;
    }

    if (prop->name == "pathToFileURL") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_url_path_to_file_url", ft);
        lastValue = createCall(ft, fn.getCallee(), { path });
        return true;
    }

    // url.format(urlObject, options?) - format URL or legacy object to string
    if (prop->name == "format") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* urlObj = boxValue(lastValue, node->arguments[0]->inferredType);

        llvm::Value* options = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = boxValue(lastValue, node->arguments[1]->inferredType);
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_url_format", ft);
        lastValue = createCall(ft, fn.getCallee(), { urlObj, options });
        return true;
    }

    // url.resolve(from, to) - resolve relative URL
    if (prop->name == "resolve") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* from = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* to = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_url_resolve", ft);
        lastValue = createCall(ft, fn.getCallee(), { from, to });
        return true;
    }

    // url.urlToHttpOptions(url) - extract HTTP options from URL
    if (prop->name == "urlToHttpOptions") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* url = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_url_to_http_options", ft);
        lastValue = createCall(ft, fn.getCallee(), { url });
        return true;
    }

    return false;
}

} // namespace ts
