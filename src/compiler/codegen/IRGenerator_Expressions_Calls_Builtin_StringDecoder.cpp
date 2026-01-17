#include "IRGenerator.h"
#include "BoxingPolicy.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

static bool stringDecoderFunctionsRegistered = false;
static void ensureStringDecoderFunctionsRegistered(BoxingPolicy& bp) {
    if (stringDecoderFunctionsRegistered) return;
    stringDecoderFunctionsRegistered = true;

    bp.registerRuntimeApi("ts_string_decoder_create", {true}, true);
    bp.registerRuntimeApi("ts_string_decoder_write", {true, true}, true);
    bp.registerRuntimeApi("ts_string_decoder_end", {true, true}, true);
    bp.registerRuntimeApi("ts_string_decoder_get_encoding", {true}, true);
}

bool IRGenerator::tryGenerateStringDecoderCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check for StringDecoder method calls (decoder.write(), decoder.end())
    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
        if (classType->name == "StringDecoder") {
            ensureStringDecoderFunctionsRegistered(boxingPolicy);
            SPDLOG_DEBUG("tryGenerateStringDecoderCall: StringDecoder.{}", prop->name);

            const std::string& methodName = prop->name;

            // Get the decoder instance
            visit(prop->expression.get());
            llvm::Value* decoder = lastValue;
            decoder = boxValue(decoder, prop->expression->inferredType);

            if (methodName == "write") {
                llvm::Value* buffer = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    buffer = boxValue(lastValue, node->arguments[0]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_string_decoder_write", ft);
                lastValue = createCall(ft, fn.getCallee(), { decoder, buffer });
                return true;
            }

            if (methodName == "end") {
                llvm::Value* buffer = llvm::ConstantPointerNull::get(builder->getPtrTy());
                if (!node->arguments.empty()) {
                    visit(node->arguments[0].get());
                    buffer = boxValue(lastValue, node->arguments[0]->inferredType);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_string_decoder_end", ft);
                lastValue = createCall(ft, fn.getCallee(), { decoder, buffer });
                return true;
            }
        }
    }

    return false;
}

bool IRGenerator::tryGenerateStringDecoderNew(ast::NewExpression* node) {
    auto id = dynamic_cast<Identifier*>(node->expression.get());
    if (!id || id->name != "StringDecoder") return false;

    ensureStringDecoderFunctionsRegistered(boxingPolicy);
    SPDLOG_DEBUG("tryGenerateStringDecoderNew: new StringDecoder()");

    // Get optional encoding argument
    llvm::Value* encoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
    if (!node->arguments.empty()) {
        visit(node->arguments[0].get());
        encoding = boxValue(lastValue, node->arguments[0]->inferredType);
    }

    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
        { builder->getPtrTy() }, false);
    llvm::FunctionCallee fn = getRuntimeFunction("ts_string_decoder_create", ft);
    lastValue = createCall(ft, fn.getCallee(), { encoding });
    return true;
}

} // namespace ts
