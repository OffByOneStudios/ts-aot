#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;

class AsyncVariableCollector : public ast::Visitor {
public:
    std::vector<std::string> variables;
    std::map<std::string, std::shared_ptr<Type>> variableTypes;

    void visit(ast::Node* node) {
        if (node) node->accept(this);
    }

    void visitVariableDeclaration(ast::VariableDeclaration* node) override {
        if (auto id = dynamic_cast<ast::Identifier*>(node->name.get())) {
            variables.push_back(id->name);
            std::shared_ptr<Type> type = node->resolvedType;
            if (!type && node->initializer && node->initializer->inferredType) {
                type = node->initializer->inferredType;
            }
            if (!type) {
                type = std::make_shared<Type>(TypeKind::Any);
            }
            variableTypes[id->name] = type;
        }
        if (node->initializer) visit(node->initializer.get());
    }

    void visitFunctionDeclaration(ast::FunctionDeclaration* node) override {}
    void visitClassDeclaration(ast::ClassDeclaration* node) override {}
    void visitInterfaceDeclaration(ast::InterfaceDeclaration* node) override {}
    void visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) override {}
    void visitEnumDeclaration(ast::EnumDeclaration* node) override {}
    void visitArrowFunction(ast::ArrowFunction* node) override {}
    void visitFunctionExpression(ast::FunctionExpression* node) override {}

    void visitIdentifier(ast::Identifier* node) override {}
    void visitNumericLiteral(ast::NumericLiteral* node) override {}
    void visitStringLiteral(ast::StringLiteral* node) override {}
    void visitBooleanLiteral(ast::BooleanLiteral* node) override {}
    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) override {
        for (auto& element : node->elements) visit(element.get());
    }
    void visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) override {
        for (auto& prop : node->properties) {
            if (prop->initializer) visit(prop->initializer.get());
        }
    }
    void visitElementAccessExpression(ast::ElementAccessExpression* node) override {
        visit(node->expression.get());
        visit(node->argumentExpression.get());
    }
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node) override {
        visit(node->expression.get());
    }
    void visitCallExpression(ast::CallExpression* node) override {
        visit(node->callee.get());
        for (auto& arg : node->arguments) visit(arg.get());
    }
    void visitNewExpression(ast::NewExpression* node) override {
        visit(node->expression.get());
        for (auto& arg : node->arguments) visit(arg.get());
    }
    void visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) override {
        visit(node->operand.get());
    }
    void visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) override {
        visit(node->operand.get());
    }
    void visitBinaryExpression(ast::BinaryExpression* node) override {
        visit(node->left.get());
        visit(node->right.get());
    }
    void visitAssignmentExpression(ast::AssignmentExpression* node) override {
        visit(node->left.get());
        visit(node->right.get());
    }
    void visitAwaitExpression(ast::AwaitExpression* node) override {
        visit(node->expression.get());
    }
    void visitObjectBindingPattern(ast::ObjectBindingPattern* node) override {}
    void visitArrayBindingPattern(ast::ArrayBindingPattern* node) override {}
    void visitBindingElement(ast::BindingElement* node) override {}
    void visitSpreadElement(ast::SpreadElement* node) override {
        visit(node->expression.get());
    }
    void visitOmittedExpression(ast::OmittedExpression* node) override {}
    void visitExpressionStatement(ast::ExpressionStatement* node) override {
        visit(node->expression.get());
    }
    void visitIfStatement(ast::IfStatement* node) override {
        visit(node->condition.get());
        visit(node->thenStatement.get());
        if (node->elseStatement) visit(node->elseStatement.get());
    }
    void visitWhileStatement(ast::WhileStatement* node) override {
        visit(node->condition.get());
        visit(node->body.get());
    }
    void visitForStatement(ast::ForStatement* node) override {
        if (node->initializer) visit(node->initializer.get());
        if (node->condition) visit(node->condition.get());
        if (node->incrementor) visit(node->incrementor.get());
        visit(node->body.get());
    }
    void visitForOfStatement(ast::ForOfStatement* node) override {
        visit(node->initializer.get());
        visit(node->expression.get());
        visit(node->body.get());
    }
    void visitForInStatement(ast::ForInStatement* node) override {
        visit(node->initializer.get());
        visit(node->expression.get());
        visit(node->body.get());
    }
    void visitSwitchStatement(ast::SwitchStatement* node) override {
        visit(node->expression.get());
        for (auto& clause : node->clauses) {
            if (auto c = dynamic_cast<ast::CaseClause*>(clause.get())) {
                visit(c->expression.get());
                for (auto& stmt : c->statements) visit(stmt.get());
            } else if (auto d = dynamic_cast<ast::DefaultClause*>(clause.get())) {
                for (auto& stmt : d->statements) visit(stmt.get());
            }
        }
    }
    void visitReturnStatement(ast::ReturnStatement* node) override {
        if (node->expression) visit(node->expression.get());
    }
    void visitBreakStatement(ast::BreakStatement* node) override {}
    void visitContinueStatement(ast::ContinueStatement* node) override {}
    void visitThrowStatement(ast::ThrowStatement* node) override {
        visit(node->expression.get());
    }
    void visitTryStatement(ast::TryStatement* node) override {
        for (auto& stmt : node->tryBlock) visit(stmt.get());
        if (node->catchClause) {
            if (node->catchClause->variable) {
                if (auto id = dynamic_cast<ast::Identifier*>(node->catchClause->variable.get())) {
                    variables.push_back(id->name);
                    variableTypes[id->name] = std::make_shared<Type>(TypeKind::Any);
                }
            }
            for (auto& stmt : node->catchClause->block) visit(stmt.get());
        }
        for (auto& stmt : node->finallyBlock) visit(stmt.get());
    }
    void visitBlockStatement(ast::BlockStatement* node) override {
        for (auto& stmt : node->statements) visit(stmt.get());
    }
    void visitImportDeclaration(ast::ImportDeclaration* node) override {}
    void visitExportDeclaration(ast::ExportDeclaration* node) override {}
    void visitExportAssignment(ast::ExportAssignment* node) override {}
    void visitProgram(ast::Program* node) override {
        for (auto& stmt : node->body) visit(stmt.get());
    }
    void visitSuperExpression(ast::SuperExpression* node) override {}
    void visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) override {}
    void visitTemplateExpression(ast::TemplateExpression* node) override {}
    void visitAsExpression(ast::AsExpression* node) override {
        visit(node->expression.get());
    }
};

void IRGenerator::visitAwaitExpression(ast::AwaitExpression* node) {
    if (!currentAsyncContext) {
        // Fallback for non-async context (should be caught by analyzer)
        visit(node->expression.get());
        return;
    }

    // 1. Evaluate expression
    visit(node->expression.get());
    llvm::Value* promiseVal = lastValue;
    
    // Box it if needed
    promiseVal = boxValue(promiseVal, node->expression->inferredType);

    // 2. Save state
    int nextState = asyncStateBlocks.size();
    llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(*context, "state" + std::to_string(nextState), builder->GetInsertBlock()->getParent());
    asyncStateBlocks.push_back(nextBB);

    // Update ctx->state
    llvm::Value* statePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 1);
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), nextState), statePtr);

    // 3. Call ts_async_await
    llvm::FunctionCallee awaitFn = module->getOrInsertFunction("ts_async_await", builder->getVoidTy(), builder->getPtrTy(), builder->getPtrTy());
    builder->CreateCall(awaitFn, { promiseVal, currentAsyncContext });

    // 4. Return from SM function
    builder->CreateRetVoid();

    // 5. Start next state
    builder->SetInsertPoint(nextBB);
    
    // The resumed value is in currentAsyncResumedValue
    // Unbox it to the expected type
    lastValue = unboxValue(currentAsyncResumedValue, node->inferredType);
}

void IRGenerator::generateAsyncFunctionBody(llvm::Function* entryFunc, ast::Node* node, const std::vector<std::shared_ptr<Type>>& argTypes, std::shared_ptr<Type> classType, const std::string& specializedName) {
    // 1. Collect variables
    AsyncVariableCollector collector;
    if (classType) {
        collector.variables.push_back("this");
        collector.variableTypes["this"] = classType;
    }
    std::vector<std::string> paramNames;
    if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(node)) {
        for (auto& stmt : funcNode->body) collector.visit(stmt.get());
        for (auto& param : funcNode->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                paramNames.push_back(id->name);
            }
        }
    } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(node)) {
        for (auto& stmt : methodNode->body) collector.visit(stmt.get());
        for (auto& param : methodNode->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                paramNames.push_back(id->name);
            }
        }
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        if (auto block = dynamic_cast<ast::BlockStatement*>(arrowNode->body.get())) {
            for (auto& stmt : block->statements) collector.visit(stmt.get());
        } else {
            collector.visit(arrowNode->body.get());
        }
        for (auto& param : arrowNode->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                paramNames.push_back(id->name);
            }
        }
    }

    // Add parameters to collector with correct types
    for (size_t i = 0; i < paramNames.size(); ++i) {
        if (std::find(collector.variables.begin(), collector.variables.end(), paramNames[i]) == collector.variables.end()) {
            collector.variables.push_back(paramNames[i]);
        }
        if (i < argTypes.size()) {
            collector.variableTypes[paramNames[i]] = argTypes[i];
        } else {
            collector.variableTypes[paramNames[i]] = std::make_shared<Type>(TypeKind::Any);
        }
    }

    // Create Frame struct type
    std::vector<llvm::Type*> frameFields;
    std::map<std::string, int> frameMap;
    for (size_t i = 0; i < collector.variables.size(); ++i) {
        frameFields.push_back(getLLVMType(collector.variableTypes[collector.variables[i]]));
        frameMap[collector.variables[i]] = i;
    }
    llvm::StructType* frameType = llvm::StructType::create(*context, specializedName + "_Frame");
    frameType->setBody(frameFields);

    // 2. Create the State Machine (SM) function
    std::string smName = specializedName + "_SM";
    std::vector<llvm::Type*> smArgTypes = { builder->getPtrTy(), builder->getPtrTy() }; // ctx, resumedValue
    llvm::FunctionType* smFt = llvm::FunctionType::get(builder->getVoidTy(), smArgTypes, false);
    llvm::Function* smFunc = llvm::Function::Create(smFt, llvm::Function::InternalLinkage, smName, module.get());

    // 3. Implement the Entry function
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", entryFunc);
    builder->SetInsertPoint(entryBB);

    // Create AsyncContext
    llvm::FunctionCallee createCtxFn = module->getOrInsertFunction("ts_async_context_create", builder->getPtrTy());
    llvm::Value* ctx = builder->CreateCall(createCtxFn);
    
    // Set resumeFn
    llvm::Value* resumeFnPtr = builder->CreateStructGEP(asyncContextType, ctx, 3);
    builder->CreateStore(smFunc, resumeFnPtr);

    // Allocate Frame on heap
    llvm::FunctionCallee allocFn = module->getOrInsertFunction("ts_alloc", builder->getPtrTy(), llvm::Type::getInt64Ty(*context));
    uint64_t frameSize = module->getDataLayout().getTypeAllocSize(frameType);
    llvm::Value* frame = builder->CreateCall(allocFn, { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), frameSize) });
    
    // Store Frame in ctx->data
    llvm::Value* dataPtr = builder->CreateStructGEP(asyncContextType, ctx, 4);
    builder->CreateStore(frame, dataPtr);

    // Copy arguments to frame
    auto argIt = entryFunc->arg_begin();
    argIt++; // Skip context
    if (classType && argIt != entryFunc->arg_end()) {
        int idx = frameMap["this"];
        llvm::Value* fieldPtr = builder->CreateStructGEP(frameType, frame, idx);
        llvm::Value* argVal = &*argIt;
        llvm::Value* castedVal = castValue(argVal, frameType->getElementType(idx));
        builder->CreateStore(castedVal, fieldPtr);
        argIt++;
    }

    std::vector<ast::Parameter*> params;
    if (auto fn = dynamic_cast<ast::FunctionDeclaration*>(node)) {
        for (auto& p : fn->parameters) params.push_back(p.get());
    } else if (auto fn = dynamic_cast<ast::MethodDefinition*>(node)) {
        for (auto& p : fn->parameters) params.push_back(p.get());
    } else if (auto fn = dynamic_cast<ast::ArrowFunction*>(node)) {
        for (auto& p : fn->parameters) params.push_back(p.get());
    }

    for (auto param : params) {
        if (argIt != entryFunc->arg_end()) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                int idx = frameMap[id->name];
                llvm::Value* fieldPtr = builder->CreateStructGEP(frameType, frame, idx);
                llvm::Value* argVal = &*argIt;
                llvm::Value* castedVal = castValue(argVal, frameType->getElementType(idx));
                builder->CreateStore(castedVal, fieldPtr);
            }
            argIt++;
        }
    }
    
    // Call SM function for the first time
    llvm::FunctionCallee smFnCallee(smFt, smFunc);
    builder->CreateCall(smFnCallee, { ctx, llvm::ConstantPointerNull::get(builder->getPtrTy()) });

    // Return the promise from the context
    llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, ctx, 2);
    llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
    
    llvm::FunctionCallee makePromiseFn = module->getOrInsertFunction("ts_value_make_promise", builder->getPtrTy(), builder->getPtrTy());
    lastValue = builder->CreateCall(makePromiseFn, { promise });
    builder->CreateRet(lastValue);

    // 4. Implement the SM function
    llvm::BasicBlock* smEntryBB = llvm::BasicBlock::Create(*context, "entry", smFunc);
    builder->SetInsertPoint(smEntryBB);

    llvm::Value* smCtx = smFunc->getArg(0);
    llvm::Value* smResumedVal = smFunc->getArg(1);

    currentAsyncContext = smCtx;
    currentAsyncResumedValue = smResumedVal;
    currentAsyncFrameType = frameType;
    currentAsyncFrameMap = frameMap;
    asyncStateBlocks.clear();

    // Load Frame from ctx->data
    llvm::Value* smDataPtr = builder->CreateStructGEP(asyncContextType, smCtx, 4);
    currentAsyncFrame = builder->CreateLoad(builder->getPtrTy(), smDataPtr);

    // Create the switch block
    llvm::BasicBlock* switchBB = llvm::BasicBlock::Create(*context, "switch", smFunc);
    builder->CreateBr(switchBB);
    builder->SetInsertPoint(switchBB);

    llvm::Value* statePtr = builder->CreateStructGEP(asyncContextType, smCtx, 1);
    llvm::Value* state = builder->CreateLoad(llvm::Type::getInt32Ty(*context), statePtr);

    llvm::BasicBlock* state0 = llvm::BasicBlock::Create(*context, "state0", smFunc);
    asyncStateBlocks.push_back(state0);

    // Start generating code for state 0
    builder->SetInsertPoint(state0);

    auto oldNamedValues = namedValues;
    
    // Map variables in frame to namedValues
    for (auto const& [name, idx] : frameMap) {
        namedValues[name] = builder->CreateStructGEP(frameType, currentAsyncFrame, idx);
    }

    // If this is a class method, ensure 'this' is in namedValues
    if (classType) {
        auto it = frameMap.find("this");
        if (it != frameMap.end()) {
            namedValues["this"] = builder->CreateStructGEP(frameType, currentAsyncFrame, it->second);
        }
    }

    if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(node)) {
        for (auto& stmt : funcNode->body) {
            visit(stmt.get());
        }
    } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(node)) {
        for (auto& stmt : methodNode->body) {
            visit(stmt.get());
        }
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        for (auto& stmt : methodNode->body) {
            visit(stmt.get());
        }
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        if (auto block = dynamic_cast<ast::BlockStatement*>(arrowNode->body.get())) {
            for (auto& stmt : block->statements) {
                visit(stmt.get());
            }
        } else {
            visit(arrowNode->body.get());
            // Resolve promise with result
            llvm::Value* res = boxValue(lastValue, arrowNode->inferredType);
            llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, smCtx, 2);
            llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
            llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve_internal", builder->getVoidTy(), builder->getPtrTy(), builder->getPtrTy());
            builder->CreateCall(resolveFn, { promise, res });
            builder->CreateRetVoid();
        }
    }

    // Finalize SM function
    if (!builder->GetInsertBlock()->getTerminator()) {
        // Resolve promise with undefined
        llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, smCtx, 2);
        llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
        llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve_internal", builder->getVoidTy(), builder->getPtrTy(), builder->getPtrTy());
        
        llvm::Value* undefinedVal = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
             // Already handled above for expression body
        } else {
            builder->CreateCall(resolveFn, { promise, undefinedVal });
        }
        builder->CreateRetVoid();
    }

    // Now create the switch
    builder->SetInsertPoint(switchBB);
    llvm::SwitchInst* sw = builder->CreateSwitch(state, asyncStateBlocks[0], asyncStateBlocks.size());
    for (size_t i = 0; i < asyncStateBlocks.size(); ++i) {
        sw->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), i), asyncStateBlocks[i]);
    }

    namedValues = oldNamedValues;
    currentAsyncContext = nullptr;
    currentAsyncResumedValue = nullptr;
    currentAsyncFrame = nullptr;
    currentAsyncFrameType = nullptr;
    currentAsyncFrameMap.clear();
}

} // namespace ts


