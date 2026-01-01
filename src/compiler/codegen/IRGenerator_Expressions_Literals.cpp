#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;
void IRGenerator::visitNumericLiteral(ast::NumericLiteral* node) {
    if (node->inferredType && node->inferredType->kind == TypeKind::Double) {
        lastValue = llvm::ConstantFP::get(*context, llvm::APFloat(node->value));
    } else if (node->value == (int64_t)node->value) {
        lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, (int64_t)node->value));
    } else {
        lastValue = llvm::ConstantFP::get(*context, llvm::APFloat(node->value));
    }
}

void IRGenerator::visitBigIntLiteral(ast::BigIntLiteral* node) {
    llvm::FunctionType* createFt = llvm::FunctionType::get(
        builder->getPtrTy(),
        { builder->getPtrTy(), builder->getInt32Ty() }, false);
    llvm::FunctionCallee createFn = getRuntimeFunction("ts_bigint_create_str", createFt);

    // Remove 'n' suffix if present
    std::string val = node->value;
    if (!val.empty() && val.back() == 'n') {
        val.pop_back();
    }

    llvm::Constant* strConst = builder->CreateGlobalStringPtr(val);
    lastValue = createCall(createFt, createFn.getCallee(), { strConst, builder->getInt32(10) });
    nonNullValues.insert(lastValue);
}

void IRGenerator::visitBooleanLiteral(ast::BooleanLiteral* node) {
    lastValue = llvm::ConstantInt::get(*context, llvm::APInt(1, node->value ? 1 : 0));
}

void IRGenerator::visitNullLiteral(ast::NullLiteral* node) {
    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
}

void IRGenerator::visitUndefinedLiteral(ast::UndefinedLiteral* node) {
    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
}

void IRGenerator::visitStringLiteral(ast::StringLiteral* node) {
    llvm::FunctionType* createFt = llvm::FunctionType::get(
        llvm::PointerType::getUnqual(*context),
        { llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee createFn = getRuntimeFunction("ts_string_create", createFt);

    llvm::Constant* strConst = builder->CreateGlobalStringPtr(node->value);
    lastValue = createCall(createFt, createFn.getCallee(), { strConst });
    nonNullValues.insert(lastValue);
}

void IRGenerator::visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) {
    // First create a string literal for the regex text
    llvm::FunctionType* createStrFt = llvm::FunctionType::get(
        llvm::PointerType::getUnqual(*context),
        { llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);

    llvm::Constant* strConst = builder->CreateGlobalStringPtr(node->text);
    llvm::Value* strVal = createCall(createStrFt, createStrFn.getCallee(), { strConst });
    nonNullValues.insert(strVal);

    // Then call ts_regexp_from_literal
    llvm::FunctionType* createRegExpFt = llvm::FunctionType::get(
        llvm::PointerType::getUnqual(*context),
        { llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee createRegExpFn = getRuntimeFunction("ts_regexp_from_literal", createRegExpFt);

    lastValue = createCall(createRegExpFt, createRegExpFn.getCallee(), { strVal });
    nonNullValues.insert(lastValue);
}

void IRGenerator::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    std::shared_ptr<Type> elemType = nullptr;
    if (node->inferredType && node->inferredType->kind == TypeKind::Array) {
        elemType = std::static_pointer_cast<ArrayType>(node->inferredType)->elementType;
    }

    bool isSpecialized = false;
    llvm::Type* llvmElemType = nullptr;
    uint64_t elementSize = 8;

    bool isDouble = false;

    if (elemType) {
        if (elemType->kind == TypeKind::Double) {
            isSpecialized = true;
            llvmElemType = llvm::Type::getDoubleTy(*context);
            elementSize = 8;
            isDouble = true;
        } else if (elemType->kind == TypeKind::Int) {
            isSpecialized = true;
            llvmElemType = llvm::Type::getInt64Ty(*context);
            elementSize = 8;
        } else if (elemType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(elemType);
            if (cls->isStruct) {
                isSpecialized = true;
                llvmElemType = llvm::StructType::getTypeByName(*context, cls->name);
                elementSize = module->getDataLayout().getTypeAllocSize(llvmElemType);
            }
        }
    }

    if (isSpecialized) {
        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), 
                { llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt1Ty(*context) }, false);
        llvm::FunctionCallee createFn = getRuntimeFunction("ts_array_create_specialized", createFt);
        
        llvm::Value* arr = createCall(createFt, createFn.getCallee(), {
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), node->elements.size()),
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSize),
            llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), isDouble ? 1 : 0)
        });
        nonNullValues.insert(arr);

        llvm::FunctionType* getPtrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee getPtrFn = getRuntimeFunction("ts_array_get_elements_ptr", getPtrFt);
        llvm::Value* elementsPtr = createCall(getPtrFt, getPtrFn.getCallee(), { arr });
        
        for (size_t i = 0; i < node->elements.size(); ++i) {
            visit(node->elements[i].get());
            llvm::Value* val = lastValue;
            
            llvm::Value* ptr = builder->CreateGEP(llvmElemType, elementsPtr, { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), i) });
            builder->CreateStore(val, ptr);
        }
        
        lastValue = arr;
        return;
    }

    llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
    llvm::FunctionCallee createFn = getRuntimeFunction("ts_array_create", createFt);
    
    llvm::Value* arr = createCall(createFt, createFn.getCallee(), {});
    nonNullValues.insert(arr);

    llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
            { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);

    for (auto& el : node->elements) {
        if (auto spread = dynamic_cast<ast::SpreadElement*>(el.get())) {
            visit(spread->expression.get());
            llvm::Value* otherArr = lastValue;
            
            llvm::FunctionType* concatFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee concatFn = getRuntimeFunction("ts_array_concat", concatFt);
            createCall(concatFt, concatFn.getCallee(), { arr, otherArr });
            continue;
        }

        visit(el.get());
        llvm::Value* val = lastValue;
        if (!val) continue;

        val = boxValue(val, el->inferredType);
        
        createCall(pushFt, pushFn.getCallee(), { arr, val });
    }
    
    lastValue = arr;
}

void IRGenerator::visitTemplateExpression(ast::TemplateExpression* node) {
    llvm::FunctionType* createFt = llvm::FunctionType::get(
        llvm::PointerType::getUnqual(*context),
        { llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee createFn = getRuntimeFunction("ts_string_create", createFt);
    
    llvm::FunctionType* concatFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
        { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee concatFn = getRuntimeFunction("ts_string_concat", concatFt);

    llvm::Constant* headStr = builder->CreateGlobalStringPtr(node->head);
    llvm::Value* currentStr = createCall(createFt, createFn.getCallee(), { headStr });
    nonNullValues.insert(currentStr);
    
    for ( auto& span : node->spans ) {
        visit(span.expression.get());
        llvm::Value* exprVal = lastValue;
        
        if (exprVal->getType()->isIntegerTy()) {
            llvm::FunctionType* fromIntFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee fromIntFn = getRuntimeFunction("ts_string_from_int", fromIntFt);
            exprVal = createCall(fromIntFt, fromIntFn.getCallee(), { exprVal });
            nonNullValues.insert(exprVal);
        } else if (exprVal->getType()->isDoubleTy()) {
            llvm::FunctionType* fromDoubleFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fromDoubleFn = getRuntimeFunction("ts_string_from_double", fromDoubleFt);
            exprVal = createCall(fromDoubleFt, fromDoubleFn.getCallee(), { exprVal });
            nonNullValues.insert(exprVal);
        } else {
            // Handle boxed values or other types by unboxing to string
            exprVal = unboxValue(exprVal, std::make_shared<Type>(TypeKind::String));
        }
        
        currentStr = createCall(concatFt, concatFn.getCallee(), { currentStr, exprVal });
        nonNullValues.insert(currentStr);
        
        llvm::Constant* litStr = builder->CreateGlobalStringPtr(span.literal);
        llvm::Value* litVal = createCall(createFt, createFn.getCallee(), { litStr });
        nonNullValues.insert(litVal);
        currentStr = createCall(concatFt, concatFn.getCallee(), { currentStr, litVal });
        nonNullValues.insert(currentStr);
    }
    
    lastValue = currentStr;
}

void IRGenerator::visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node) {
    // 1. Prepare the strings array
    std::vector<std::string> strings;
    std::vector<ast::Expression*> expressions;

    if (auto* templateExpr = dynamic_cast<ast::TemplateExpression*>(node->templateExpr.get())) {
        strings.push_back(templateExpr->head);
        for (auto& span : templateExpr->spans) {
            strings.push_back(span.literal);
            expressions.push_back(span.expression.get());
        }
    } else if (auto* stringLit = dynamic_cast<ast::StringLiteral*>(node->templateExpr.get())) {
        strings.push_back(stringLit->value);
    }

    // 2. Create the TsArray for strings
    llvm::FunctionType* createArrayFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    llvm::FunctionCallee createArrayFn = getRuntimeFunction("ts_array_create", createArrayFt);
    llvm::Value* stringsArray = createCall(createArrayFt, createArrayFn.getCallee(), {});

    llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);

    llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);

    for (const auto& s : strings) {
        llvm::Value* strPtr = builder->CreateGlobalStringPtr(s);
        llvm::Value* tsStr = createCall(createStrFt, createStrFn.getCallee(), { strPtr });
        createCall(pushFt, pushFn.getCallee(), { stringsArray, boxValue(tsStr, std::make_shared<Type>(TypeKind::String)) });
    }

    // 3. Prepare arguments for the tag call
    std::vector<llvm::Value*> callArgs;
    std::vector<std::shared_ptr<Type>> argTypes;

    // Add context first
    if (currentAsyncContext) {
        callArgs.push_back(currentAsyncContext);
    } else {
        callArgs.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
    }

    // First argument is the strings array
    auto stringsType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    callArgs.push_back(stringsArray);
    argTypes.push_back(stringsType);

    for (auto* expr : expressions) {
        visit(expr);
        callArgs.push_back(lastValue);
        argTypes.push_back(expr->inferredType ? expr->inferredType : std::make_shared<Type>(TypeKind::Any));
    }

    // 4. Call the tag function
    if (auto id = dynamic_cast<ast::Identifier*>(node->tag.get())) {
        std::string mangledName = Monomorphizer::generateMangledName(id->name, argTypes, {});

        // Update inferred type from specialization if available
        for (const auto& spec : specializations) {
            if (spec.specializedName == mangledName) {
                node->inferredType = spec.returnType;
                break;
            }
        }

        llvm::Function* func = module->getFunction(mangledName);
        if (func) {
            lastValue = createCall(func->getFunctionType(), func, callArgs);
            return;
        }
    }

    // Fallback: dynamic call (not fully implemented here, but we can try)
    visit(node->tag.get());
    llvm::Value* tagFunc = lastValue;
    if (tagFunc) {
        std::vector<llvm::Type*> paramTypes;
        for (auto* arg : callArgs) paramTypes.push_back(arg->getType());
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), paramTypes, false);
        lastValue = createCall(ft, tagFunc, callArgs);
    }
}

} // namespace ts


