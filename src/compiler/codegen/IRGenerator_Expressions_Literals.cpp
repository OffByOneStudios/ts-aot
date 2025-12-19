#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;
void IRGenerator::visitNumericLiteral(ast::NumericLiteral* node) {
    if (node->value == (int64_t)node->value) {
        lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, (int64_t)node->value));
    } else {
        lastValue = llvm::ConstantFP::get(*context, llvm::APFloat(node->value));
    }
}

void IRGenerator::visitBooleanLiteral(ast::BooleanLiteral* node) {
    lastValue = llvm::ConstantInt::get(*context, llvm::APInt(1, node->value ? 1 : 0));
}

void IRGenerator::visitStringLiteral(ast::StringLiteral* node) {
    llvm::Function* createFn = module->getFunction("ts_string_create");
    if (!createFn) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context) };
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context),
            args, false);
        createFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_create", module.get());
    }

    llvm::Constant* strConst = builder->CreateGlobalStringPtr(node->value);
    lastValue = builder->CreateCall(createFn, { strConst });
}

void IRGenerator::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create", 
        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false));
    
    llvm::Value* arr = builder->CreateCall(createFn);

    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push",
        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
            { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));

    for (auto& el : node->elements) {
        if (auto spread = dynamic_cast<ast::SpreadElement*>(el.get())) {
            visit(spread->expression.get());
            llvm::Value* otherArr = lastValue;
            
            llvm::FunctionCallee concatFn = module->getOrInsertFunction("ts_array_concat",
                llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
            builder->CreateCall(concatFn, { arr, otherArr });
            continue;
        }

        visit(el.get());
        llvm::Value* val = lastValue;
        if (!val) continue;

        if (val->getType()->isDoubleTy()) {
             val = builder->CreateBitCast(val, llvm::Type::getInt64Ty(*context));
        } else if (val->getType()->isPointerTy()) {
             val = builder->CreatePtrToInt(val, llvm::Type::getInt64Ty(*context));
        }
        
        builder->CreateCall(pushFn, { arr, val });
    }
    
    lastValue = arr;
}

void IRGenerator::visitTemplateExpression(ast::TemplateExpression* node) {
    llvm::Function* createFn = module->getFunction("ts_string_create");
    if (!createFn) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context) };
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context),
            args, false);
        createFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_create", module.get());
    }
    
    llvm::Function* concatFn = module->getFunction("ts_string_concat");
    if (!concatFn) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) };
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), args, false);
        concatFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_concat", module.get());
    }

    llvm::Constant* headStr = builder->CreateGlobalStringPtr(node->head);
    llvm::Value* currentStr = builder->CreateCall(createFn, { headStr });
    
    for ( auto& span : node->spans ) {
        visit(span.expression.get());
        llvm::Value* exprVal = lastValue;
        
        if (exprVal->getType()->isIntegerTy()) {
            llvm::Function* fromIntFn = module->getFunction("ts_string_from_int");
            if (!fromIntFn) {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getInt64Ty(*context) }, false);
                fromIntFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_from_int", module.get());
            }
            exprVal = builder->CreateCall(fromIntFn, { exprVal });
        } else if (exprVal->getType()->isDoubleTy()) {
            llvm::Function* fromDoubleFn = module->getFunction("ts_string_from_double");
            if (!fromDoubleFn) {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getDoubleTy(*context) }, false);
                fromDoubleFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_from_double", module.get());
            }
            exprVal = builder->CreateCall(fromDoubleFn, { exprVal });
        }
        
        currentStr = builder->CreateCall(concatFn, { currentStr, exprVal });
        
        llvm::Constant* litStr = builder->CreateGlobalStringPtr(span.literal);
        llvm::Value* litVal = builder->CreateCall(createFn, { litStr });
        currentStr = builder->CreateCall(concatFn, { currentStr, litVal });
    }
    
    lastValue = currentStr;
}

} // namespace ts


