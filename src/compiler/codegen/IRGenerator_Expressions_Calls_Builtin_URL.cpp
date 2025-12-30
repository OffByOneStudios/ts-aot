#include "IRGenerator.h"
#include <spdlog/spdlog.h>

namespace ts {

bool IRGenerator::tryGenerateURLCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_url_search_params_append", ft);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_url_search_params_delete", ft);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_url_search_params_get", ft);
            lastValue = createCall(ft, fn.getCallee(), { params, name });
            return true;
        }
        
        if (prop->name == "getAll") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_url_search_params_get_all", ft);
            lastValue = createCall(ft, fn.getCallee(), { params, name });
            return true;
        }
        
        if (prop->name == "has") {
            if (node->arguments.empty()) return true;
            visit(node->arguments[0].get());
            llvm::Value* name = lastValue;
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), 
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_url_search_params_has", ft);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_url_search_params_set", ft);
            createCall(ft, fn.getCallee(), { params, name, value });
            lastValue = getUndefinedValue();
            return true;
        }
        
        if (prop->name == "sort") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), 
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_url_search_params_sort", ft);
            createCall(ft, fn.getCallee(), { params });
            lastValue = getUndefinedValue();
            return true;
        }
        
        if (prop->name == "toString") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_url_search_params_to_string", ft);
            lastValue = createCall(ft, fn.getCallee(), { params });
            return true;
        }
    }
    
    return false;
}

} // namespace ts
