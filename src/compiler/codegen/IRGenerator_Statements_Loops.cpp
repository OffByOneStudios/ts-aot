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
    
    // Create loop variables
    std::string baseName = "forof_" + std::to_string(anonVarCounter++);
    llvm::Value* indexVar = nullptr;
    llvm::Value* iterableVar = nullptr;
    llvm::Value* lengthVar = nullptr;

    if (currentAsyncFrame) {
        indexVar = namedValues[baseName + "_index"];
        iterableVar = namedValues[baseName + "_iterable"];
        lengthVar = namedValues[baseName + "_length"];
    } else {
        indexVar = createEntryBlockAlloca(function, baseName + "_index", llvm::Type::getInt64Ty(*context));
        iterableVar = createEntryBlockAlloca(function, baseName + "_iterable", builder->getPtrTy());
        lengthVar = createEntryBlockAlloca(function, baseName + "_length", llvm::Type::getInt64Ty(*context));
    }

    // Evaluate iterable
    visit(node->expression.get());
    llvm::Value* iterable = lastValue;
    if (!iterable) return;
    builder->CreateStore(iterable, iterableVar);

    bool isString = false;
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::String) {
        isString = true;
    }

    // Initialize index
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), indexVar);

    // Get length
    llvm::Value* lengthVal = nullptr;
    if (isString) {
        llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
        llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length", lenFt);
        lengthVal = createCall(lenFt, lenFn.getCallee(), { iterable });
    } else {
        llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
        llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length", lenFt);
        lengthVal = createCall(lenFt, lenFn.getCallee(), { iterable });
    }
    builder->CreateStore(lengthVal, lengthVar);

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "loop.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "loop.body", function);
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(*context, "loop.inc", function);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "loop.end", function);

    builder->CreateBr(condBB);
    builder->SetInsertPoint(condBB);

    llvm::Value* currIndex = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "idx");
    llvm::Value* currLength = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthVar, "len");
    llvm::Value* cond = builder->CreateICmpSLT(currIndex, currLength, "loopcond");
    builder->CreateCondBr(cond, bodyBB, afterBB);

    builder->SetInsertPoint(bodyBB);

    llvm::Value* currIterable = builder->CreateLoad(builder->getPtrTy(), iterableVar, "iter");
    llvm::Value* elementVal = nullptr;
    if (isString) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) };
        llvm::FunctionType* substrFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), args, false);
        llvm::FunctionCallee substrFn = module->getOrInsertFunction("ts_string_substring", substrFt);
        llvm::Value* nextIndex = builder->CreateAdd(currIndex, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1));
        elementVal = createCall(substrFt, substrFn.getCallee(), { currIterable, currIndex, nextIndex });
    } else {
        std::vector<llvm::Type*> args = { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) };
        llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), args, false);
        llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_array_get", getFt);
        elementVal = createCall(getFt, getFn.getCallee(), { currIterable, currIndex });
    }

    if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get())) {
        std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
            elementType = std::static_pointer_cast<ArrayType>(node->expression->inferredType)->elementType;
        }
        llvm::Value* unboxed = unboxValue(elementVal, elementType);
        generateDestructuring(unboxed, elementType, varDecl->name.get());
    }

    loopStack.push_back({incBB, afterBB});
    visit(node->body.get());
    loopStack.pop_back();

    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incBB);
    }

    builder->SetInsertPoint(incBB);
    llvm::Value* reloadIdx = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "idx.reload");
    llvm::Value* nextIdx = builder->CreateAdd(reloadIdx, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1));
    builder->CreateStore(nextIdx, indexVar);
    builder->CreateBr(condBB);

    builder->SetInsertPoint(afterBB);
}

void IRGenerator::visitForInStatement(ast::ForInStatement* node) {
    llvm::Function* function = builder->GetInsertBlock()->getParent();
    
    // Create loop variables
    std::string baseName = "forin_" + std::to_string(anonVarCounter++);
    llvm::Value* indexVar = nullptr;
    llvm::Value* keysVar = nullptr;
    llvm::Value* lengthVar = nullptr;

    if (currentAsyncFrame) {
        indexVar = namedValues[baseName + "_index"];
        keysVar = namedValues[baseName + "_keys"];
        lengthVar = namedValues[baseName + "_length"];
    } else {
        indexVar = createEntryBlockAlloca(function, baseName + "_index", llvm::Type::getInt64Ty(*context));
        keysVar = createEntryBlockAlloca(function, baseName + "_keys", builder->getPtrTy());
        lengthVar = createEntryBlockAlloca(function, baseName + "_length", llvm::Type::getInt64Ty(*context));
    }

    visit(node->expression.get());
    llvm::Value* obj = lastValue;
    if (!obj) return;

    llvm::Value* keys = nullptr;
    if (node->expression->inferredType && (node->expression->inferredType->kind == TypeKind::Object || node->expression->inferredType->kind == TypeKind::Class)) {
        llvm::FunctionType* createArrFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
        llvm::FunctionCallee createArrFn = module->getOrInsertFunction("ts_array_create", createArrFt);
        keys = createCall(createArrFt, createArrFn.getCallee(), {});

        llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push", pushFt);
        
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);

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
            llvm::Value* tsStr = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
            createCall(pushFt, pushFn.getCallee(), { keys, tsStr });
        }
    } else {
        llvm::FunctionType* keysFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::PointerType::getUnqual(*context) }, false);
        llvm::FunctionCallee keysFn = module->getOrInsertFunction("ts_map_keys", keysFt);
        keys = createCall(keysFt, keysFn.getCallee(), { obj });
    }
    builder->CreateStore(keys, keysVar);

    // Get length
    llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length", lenFt);
    llvm::Value* lengthVal = createCall(lenFt, lenFn.getCallee(), { keys });
    builder->CreateStore(lengthVal, lengthVar);

    // Initialize index
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), indexVar);

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "forin_cond", function);
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(*context, "forin_loop", function);
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(*context, "forin_inc", function);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "forin_after", function);

    builder->CreateBr(condBB);
    builder->SetInsertPoint(condBB);

    llvm::Value* index = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "index");
    llvm::Value* currLength = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthVar, "len");
    llvm::Value* cond = builder->CreateICmpSLT(index, currLength, "forin_cond_tmp");
    builder->CreateCondBr(cond, loopBB, afterBB);

    builder->SetInsertPoint(loopBB);

    llvm::Value* currKeys = builder->CreateLoad(builder->getPtrTy(), keysVar, "keys");
    llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
    llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_array_get", getFt);
    llvm::Value* key = createCall(getFt, getFn.getCallee(), { currKeys, index });

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
