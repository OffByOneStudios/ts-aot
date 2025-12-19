#include "IRGenerator.h"

namespace ts {
using namespace ast;

void IRGenerator::visitWhileStatement(ast::WhileStatement* node) {
    llvm::Function* func = builder->GetInsertBlock()->getParent();

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "whilecond", func);
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(*context, "whileloop");
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "whileafter");

    // Jump to condition
    builder->CreateBr(condBB);

    // Emit condition
    builder->SetInsertPoint(condBB);
    visit(node->condition.get());
    llvm::Value* condValue = lastValue;

    if (!condValue) {
        llvm::errs() << "Error: While condition evaluated to null\n";
        return;
    }

    // Convert condition to bool
    if (condValue->getType()->isDoubleTy()) {
        condValue = builder->CreateFCmpONE(condValue, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "whilecond");
    } else if (condValue->getType()->isIntegerTy(64)) {
        condValue = builder->CreateICmpNE(condValue, llvm::ConstantInt::get(*context, llvm::APInt(64, 0)), "whilecond");
    } else if (condValue->getType()->isPointerTy()) {
        condValue = builder->CreateICmpNE(condValue, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(condValue->getType())), "whilecond");
    }

    builder->CreateCondBr(condValue, loopBB, afterBB);

    // Push loop info
    loopStack.push_back({ condBB, afterBB });

    // Emit loop body
    func->insert(func->end(), loopBB);
    builder->SetInsertPoint(loopBB);
    visit(node->body.get());
    
    // Pop loop info
    loopStack.pop_back();

    // Jump back to condition
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(condBB);
    }

    // Emit after block
    func->insert(func->end(), afterBB);
    builder->SetInsertPoint(afterBB);
}

void IRGenerator::visitForStatement(ast::ForStatement* node) {
    llvm::Function* func = builder->GetInsertBlock()->getParent();

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "forcond", func);
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(*context, "forloop");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(*context, "forinc");
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "forafter");

    // Emit initializer
    if (node->initializer) {
        visit(node->initializer.get());
    }

    // Jump to condition
    builder->CreateBr(condBB);

    // Emit condition
    builder->SetInsertPoint(condBB);
    if (node->condition) {
        visit(node->condition.get());
        llvm::Value* condValue = lastValue;

        if (!condValue) {
            llvm::errs() << "Error: For condition evaluated to null\n";
            return;
        }

        // Convert condition to bool
        if (condValue->getType()->isDoubleTy()) {
            condValue = builder->CreateFCmpONE(condValue, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "forcond");
        } else if (condValue->getType()->isIntegerTy(64)) {
            condValue = builder->CreateICmpNE(condValue, llvm::ConstantInt::get(*context, llvm::APInt(64, 0)), "forcond");
        } else if (condValue->getType()->isPointerTy()) {
            condValue = builder->CreateICmpNE(condValue, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(condValue->getType())), "forcond");
        }

        builder->CreateCondBr(condValue, loopBB, afterBB);
    } else {
        // Infinite loop
        builder->CreateBr(loopBB);
    }

    // Push loop info
    loopStack.push_back({ incBB, afterBB });

    // Emit loop body
    func->insert(func->end(), loopBB);
    builder->SetInsertPoint(loopBB);
    visit(node->body.get());
    
    // Pop loop info
    loopStack.pop_back();
    
    // Jump to incrementor
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incBB);
    }

    // Emit incrementor
    func->insert(func->end(), incBB);
    builder->SetInsertPoint(incBB);
    if (node->incrementor) {
        visit(node->incrementor.get());
    }
    builder->CreateBr(condBB);

    // Emit after block
    func->insert(func->end(), afterBB);
    builder->SetInsertPoint(afterBB);
}

void IRGenerator::visitForOfStatement(ast::ForOfStatement* node) {
    llvm::Function* function = builder->GetInsertBlock()->getParent();
    
    // Evaluate iterable
    visit(node->expression.get());
    llvm::Value* iterable = lastValue;
    if (!iterable) return;

    bool isString = false;
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::String) {
        isString = true;
    }

    // Create loop variables
    llvm::AllocaInst* indexVar = createEntryBlockAlloca(function, "forof_index", llvm::Type::getInt64Ty(*context));
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), indexVar);

    // Get length
    llvm::Value* lengthVal = nullptr;
    if (isString) {
        llvm::Function* lenFn = module->getFunction("ts_string_length");
        if (!lenFn) {
             llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
             lenFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_length", module.get());
        }
        lengthVal = builder->CreateCall(lenFn, { iterable }, "len");
    } else {
        llvm::Function* lenFn = module->getFunction("ts_array_length");
        if (!lenFn) {
             llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
             lenFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_array_length", module.get());
        }
        lengthVal = builder->CreateCall(lenFn, { iterable }, "len");
    }

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "loop.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "loop.body", function);
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(*context, "loop.inc", function);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "loop.end", function);

    builder->CreateBr(condBB);
    builder->SetInsertPoint(condBB);

    llvm::Value* currIndex = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "idx");
    llvm::Value* cond = builder->CreateICmpSLT(currIndex, lengthVal, "loopcond");
    builder->CreateCondBr(cond, bodyBB, afterBB);

    builder->SetInsertPoint(bodyBB);

    llvm::Value* elementVal = nullptr;
    if (isString) {
        llvm::Function* substrFn = module->getFunction("ts_string_substring");
        if (!substrFn) {
             std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) };
             llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), args, false);
             substrFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_substring", module.get());
        }
        llvm::Value* nextIndex = builder->CreateAdd(currIndex, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1));
        elementVal = builder->CreateCall(substrFn, { iterable, currIndex, nextIndex }, "char");
    } else {
        llvm::Function* getFn = module->getFunction("ts_array_get");
        if (!getFn) {
             std::vector<llvm::Type*> args = { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) };
             llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), args, false);
             getFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_array_get", module.get());
        }
        elementVal = builder->CreateCall(getFn, { iterable, currIndex }, "elem");
    }

    if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get())) {
        std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
            elementType = std::static_pointer_cast<ArrayType>(node->expression->inferredType)->elementType;
        }
        generateDestructuring(elementVal, elementType, varDecl->name.get());
    }

    loopStack.push_back({incBB, afterBB});
    visit(node->body.get());
    loopStack.pop_back();

    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incBB);
    }

    builder->SetInsertPoint(incBB);
    llvm::Value* nextIdx = builder->CreateAdd(currIndex, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1));
    builder->CreateStore(nextIdx, indexVar);
    builder->CreateBr(condBB);

    builder->SetInsertPoint(afterBB);
}

void IRGenerator::visitForInStatement(ast::ForInStatement* node) {
    llvm::Function* function = builder->GetInsertBlock()->getParent();
    
    visit(node->expression.get());
    llvm::Value* obj = lastValue;
    if (!obj) return;

    llvm::Value* keys = nullptr;
    if (node->expression->inferredType && (node->expression->inferredType->kind == TypeKind::Object || node->expression->inferredType->kind == TypeKind::Class)) {
        llvm::FunctionCallee createArrFn = module->getOrInsertFunction("ts_array_create",
            llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false));
        keys = builder->CreateCall(createArrFn, {}, "keys_arr");

        llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push",
            llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
        
        llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create",
            llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));

        std::vector<std::string> fieldNames;
        if (node->expression->inferredType->kind == TypeKind::Object) {
            auto objType = std::static_pointer_cast<ObjectType>(node->expression->inferredType);
            for (auto const& [name, type] : objType->fields) fieldNames.push_back(name);
        } else {
            auto clsType = std::static_pointer_cast<ClassType>(node->expression->inferredType);
            auto current = clsType;
            while (current) {
                for (auto const& [name, type] : current->fields) fieldNames.push_back(name);
                current = current->baseClass;
            }
        }

        for (const auto& name : fieldNames) {
            llvm::Value* keyStr = builder->CreateGlobalStringPtr(name);
            llvm::Value* tsStr = builder->CreateCall(createStrFn, { keyStr });
            builder->CreateCall(pushFn, { keys, tsStr });
        }
    } else {
        llvm::FunctionCallee keysFn = module->getOrInsertFunction("ts_map_keys",
            llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::PointerType::getUnqual(*context) }, false));
        keys = builder->CreateCall(keysFn, { obj }, "keys");
    }

    llvm::AllocaInst* indexVar = createEntryBlockAlloca(function, "forin_index", llvm::Type::getInt64Ty(*context));
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), indexVar);

    llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length",
        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false));
    llvm::Value* lengthVal = builder->CreateCall(lenFn, { keys }, "len");

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "forin_cond", function);
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(*context, "forin_loop", function);
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(*context, "forin_inc", function);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "forin_after", function);

    builder->CreateBr(condBB);
    builder->SetInsertPoint(condBB);

    llvm::Value* index = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "index");
    llvm::Value* cond = builder->CreateICmpSLT(index, lengthVal, "forin_cond_tmp");
    builder->CreateCondBr(cond, loopBB, afterBB);

    builder->SetInsertPoint(loopBB);

    llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_array_get",
        llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false));
    llvm::Value* key = builder->CreateCall(getFn, { keys, index }, "key");

    if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get())) {
        generateDestructuring(key, std::make_shared<ts::Type>(ts::TypeKind::String), varDecl->name.get());
    }

    loopStack.push_back({incBB, afterBB});
    visit(node->body.get());
    loopStack.pop_back();

    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incBB);
    }

    builder->SetInsertPoint(incBB);
    index = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "index");
    llvm::Value* nextIndex = builder->CreateAdd(index, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "next_index");
    builder->CreateStore(nextIndex, indexVar);
    builder->CreateBr(condBB);

    builder->SetInsertPoint(afterBB);
}

} // namespace ts
