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
    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", createFt);

    llvm::Constant* strConst = builder->CreateGlobalStringPtr(node->value);
    lastValue = createCall(createFt, createFn.getCallee(), { strConst });
}

void IRGenerator::visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) {
    // First create a string literal for the regex text
    llvm::FunctionType* createStrFt = llvm::FunctionType::get(
        llvm::PointerType::getUnqual(*context),
        { llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);

    llvm::Constant* strConst = builder->CreateGlobalStringPtr(node->text);
    llvm::Value* strVal = createCall(createStrFt, createStrFn.getCallee(), { strConst });

    // Then call ts_regexp_from_literal
    llvm::FunctionType* createRegExpFt = llvm::FunctionType::get(
        llvm::PointerType::getUnqual(*context),
        { llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee createRegExpFn = module->getOrInsertFunction("ts_regexp_from_literal", createRegExpFt);

    lastValue = createCall(createRegExpFt, createRegExpFn.getCallee(), { strVal });
}

void IRGenerator::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    std::shared_ptr<Type> elemType = nullptr;
    if (node->inferredType && node->inferredType->kind == TypeKind::Array) {
        elemType = std::static_pointer_cast<ArrayType>(node->inferredType)->elementType;
    }

    bool isSpecialized = false;
    llvm::Type* llvmElemType = nullptr;
    uint64_t elementSize = 8;

    if (elemType) {
        if (elemType->kind == TypeKind::Double) {
            isSpecialized = true;
            llvmElemType = llvm::Type::getDoubleTy(*context);
            elementSize = 8;
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
                { llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create_specialized", createFt);
        
        llvm::Value* arr = createCall(createFt, createFn.getCallee(), {
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), node->elements.size()),
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elementSize)
        });

        llvm::FunctionType* getPtrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee getPtrFn = module->getOrInsertFunction("ts_array_get_elements_ptr", getPtrFt);
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
    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create", createFt);
    
    llvm::Value* arr = createCall(createFt, createFn.getCallee(), {});

    llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
            { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push", pushFt);

    for (auto& el : node->elements) {
        if (auto spread = dynamic_cast<ast::SpreadElement*>(el.get())) {
            visit(spread->expression.get());
            llvm::Value* otherArr = lastValue;
            
            llvm::FunctionType* concatFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee concatFn = module->getOrInsertFunction("ts_array_concat", concatFt);
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
    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", createFt);
    
    llvm::FunctionType* concatFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
        { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee concatFn = module->getOrInsertFunction("ts_string_concat", concatFt);

    llvm::Constant* headStr = builder->CreateGlobalStringPtr(node->head);
    llvm::Value* currentStr = createCall(createFt, createFn.getCallee(), { headStr });
    
    for ( auto& span : node->spans ) {
        visit(span.expression.get());
        llvm::Value* exprVal = lastValue;
        
        if (exprVal->getType()->isIntegerTy()) {
            llvm::FunctionType* fromIntFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee fromIntFn = module->getOrInsertFunction("ts_string_from_int", fromIntFt);
            exprVal = createCall(fromIntFt, fromIntFn.getCallee(), { exprVal });
        } else if (exprVal->getType()->isDoubleTy()) {
            llvm::FunctionType* fromDoubleFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getDoubleTy(*context) }, false);
            llvm::FunctionCallee fromDoubleFn = module->getOrInsertFunction("ts_string_from_double", fromDoubleFt);
            exprVal = createCall(fromDoubleFt, fromDoubleFn.getCallee(), { exprVal });
        }
        
        currentStr = createCall(concatFt, concatFn.getCallee(), { currentStr, exprVal });
        
        llvm::Constant* litStr = builder->CreateGlobalStringPtr(span.literal);
        llvm::Value* litVal = createCall(createFt, createFn.getCallee(), { litStr });
        currentStr = createCall(concatFt, concatFn.getCallee(), { currentStr, litVal });
    }
    
    lastValue = currentStr;
}

} // namespace ts


