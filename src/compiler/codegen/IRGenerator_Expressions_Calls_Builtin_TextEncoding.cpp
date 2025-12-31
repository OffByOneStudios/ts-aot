#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

// Static helper to register TextEncoding module's runtime functions once (4 functions)
static bool textEncodingFunctionsRegistered = false;
static void ensureTextEncodingFunctionsRegistered(BoxingPolicy& bp) {
    if (textEncodingFunctionsRegistered) return;
    textEncodingFunctionsRegistered = true;
    
    bp.registerRuntimeApi("ts_text_encoder_encode", {true, false}, true);  // encoder, string -> Uint8Array
    bp.registerRuntimeApi("ts_text_encoder_encode_into", {true, false, true}, true);  // encoder, string, dest
    bp.registerRuntimeApi("ts_text_decoder_decode", {true, true}, false);  // decoder, buffer -> string
    bp.registerRuntimeApi("ts_typed_array_create_u8", {false}, true);  // length -> Uint8Array
}

bool IRGenerator::tryGenerateTextEncodingCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureTextEncodingFunctionsRegistered(boxingPolicy);
    
    // Check if this is a TextEncoder or TextDecoder method call
    if (!prop->expression->inferredType) return false;
    if (prop->expression->inferredType->kind != TypeKind::Class) return false;
    
    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    
    if (classType->name == "TextEncoder") {
        visit(prop->expression.get());
        llvm::Value* encoder = lastValue;
        
        if (prop->name == "encode") {
            if (node->arguments.empty()) {
                // Return empty Uint8Array
                llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_typed_array_create_u8", createFt);
                lastValue = createCall(createFt, fn.getCallee(), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0) });
                return true;
            }
            
            visit(node->arguments[0].get());
            llvm::Value* input = lastValue;
            
            llvm::FunctionType* encodeFt = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() },
                false
            );
            llvm::FunctionCallee encodeFn = module->getOrInsertFunction("ts_text_encoder_encode", encodeFt);
            lastValue = createCall(encodeFt, encodeFn.getCallee(), { encoder, input });
            return true;
        }
        
        if (prop->name == "encodeInto") {
            if (node->arguments.size() < 2) return true;
            
            visit(node->arguments[0].get());
            llvm::Value* source = lastValue;
            
            visit(node->arguments[1].get());
            llvm::Value* dest = lastValue;
            
            llvm::FunctionType* encodeIntoFt = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                false
            );
            llvm::FunctionCallee encodeIntoFn = module->getOrInsertFunction("ts_text_encoder_encode_into", encodeIntoFt);
            lastValue = createCall(encodeIntoFt, encodeIntoFn.getCallee(), { encoder, source, dest });
            return true;
        }
        
        return false;
    }
    
    if (classType->name == "TextDecoder") {
        visit(prop->expression.get());
        llvm::Value* decoder = lastValue;
        
        if (prop->name == "decode") {
            llvm::Value* input = nullptr;
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                input = lastValue;
            } else {
                input = llvm::ConstantPointerNull::get(builder->getPtrTy());
            }
            
            llvm::FunctionType* decodeFt = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() },
                false
            );
            llvm::FunctionCallee decodeFn = module->getOrInsertFunction("ts_text_decoder_decode", decodeFt);
            lastValue = createCall(decodeFt, decodeFn.getCallee(), { decoder, input });
            return true;
        }
        
        return false;
    }
    
    return false;
}

} // namespace ts
