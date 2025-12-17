#include "IRGenerator.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>

namespace ts {

IRGenerator::IRGenerator() {
    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("ts_module", *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);
}

void IRGenerator::generate(const std::vector<Specialization>& specializations) {
    generatePrototypes(specializations);
    generateBodies(specializations);
    generateEntryPoint();
}

void IRGenerator::generateEntryPoint() {
    llvm::Function* userMain = module->getFunction("user_main");
    if (!userMain) return;

    // Declare ts_main
    std::vector<llvm::Type*> args = {
        llvm::Type::getInt32Ty(*context),
        llvm::PointerType::getUnqual(*context),
        llvm::PointerType::getUnqual(*context)
    };
    llvm::FunctionType* ft = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context), args, false);
    llvm::Function* tsMain = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_main", module.get());

    // Define main
    std::vector<llvm::Type*> mainArgs = {
        llvm::Type::getInt32Ty(*context),
        llvm::PointerType::getUnqual(*context)
    };
    llvm::FunctionType* mainFt = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context), mainArgs, false);
    llvm::Function* mainFn = llvm::Function::Create(mainFt, llvm::Function::ExternalLinkage, "main", module.get());

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", mainFn);
    builder->SetInsertPoint(bb);

    llvm::Value* argc = mainFn->getArg(0);
    llvm::Value* argv = mainFn->getArg(1);
    
    builder->CreateCall(tsMain, { argc, argv, userMain });
    
    builder->CreateRet(llvm::ConstantInt::get(*context, llvm::APInt(32, 0)));
}

llvm::Type* IRGenerator::getLLVMType(const std::shared_ptr<Type>& type) {
    switch (type->kind) {
        case TypeKind::Void: return llvm::Type::getVoidTy(*context);
        case TypeKind::Int: return llvm::Type::getInt64Ty(*context);
        case TypeKind::Double: return llvm::Type::getDoubleTy(*context);
        case TypeKind::Boolean: return llvm::Type::getInt1Ty(*context);
        // TODO: String, Any, Function
        default: return llvm::Type::getVoidTy(*context);
    }
}

llvm::AllocaInst* IRGenerator::createEntryBlockAlloca(llvm::Function* function, const std::string& varName, llvm::Type* type) {
    llvm::IRBuilder<> tmpBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
    return tmpBuilder.CreateAlloca(type, nullptr, varName);
}

void IRGenerator::generatePrototypes(const std::vector<Specialization>& specializations) {
    for (const auto& spec : specializations) {
        std::vector<llvm::Type*> argTypes;
        for (const auto& argType : spec.argTypes) {
            argTypes.push_back(getLLVMType(argType));
        }

        llvm::Type* returnType = getLLVMType(spec.returnType);
        llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, argTypes, false);

        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, spec.specializedName, module.get());
    }
}

void IRGenerator::generateBodies(const std::vector<Specialization>& specializations) {
    for (const auto& spec : specializations) {
        llvm::Function* function = module->getFunction(spec.specializedName);
        if (!function) continue;

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(bb);

        namedValues.clear();
        unsigned idx = 0;
        for (auto& arg : function->args()) {
            if (idx < spec.node->parameters.size()) {
                std::string argName = spec.node->parameters[idx]->name;
                arg.setName(argName);
                
                // Create an alloca for this variable.
                llvm::AllocaInst* alloca = createEntryBlockAlloca(function, argName, arg.getType());

                // Store the initial value into the alloca.
                builder->CreateStore(&arg, alloca);

                // Add arguments to variable symbol table.
                namedValues[argName] = alloca;
            }
            idx++;
        }

        for (auto& stmt : spec.node->body) {
            visit(stmt.get());
        }

        if (spec.returnType->kind == TypeKind::Void) {
             builder->CreateRetVoid();
        }
    }
}

void IRGenerator::visit(ast::Node* node) {
    if (!node) return;
    if (auto ret = dynamic_cast<ast::ReturnStatement*>(node)) visitReturnStatement(ret);
    else if (auto bin = dynamic_cast<ast::BinaryExpression*>(node)) visitBinaryExpression(bin);
    else if (auto assign = dynamic_cast<ast::AssignmentExpression*>(node)) visitAssignmentExpression(assign);
    else if (auto id = dynamic_cast<ast::Identifier*>(node)) visitIdentifier(id);
    else if (auto num = dynamic_cast<ast::NumericLiteral*>(node)) visitNumericLiteral(num);
    else if (auto str = dynamic_cast<ast::StringLiteral*>(node)) visitStringLiteral(str);
    else if (auto call = dynamic_cast<ast::CallExpression*>(node)) visitCallExpression(call);
    else if (auto arr = dynamic_cast<ast::ArrayLiteralExpression*>(node)) visitArrayLiteralExpression(arr);
    else if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node)) visitElementAccessExpression(elem);
    else if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node)) visitPropertyAccessExpression(prop);
    else if (auto exprStmt = dynamic_cast<ast::ExpressionStatement*>(node)) visitExpressionStatement(exprStmt);
    else if (auto ifStmt = dynamic_cast<ast::IfStatement*>(node)) visitIfStatement(ifStmt);
    else if (auto whileStmt = dynamic_cast<ast::WhileStatement*>(node)) visitWhileStatement(whileStmt);
    else if (auto forStmt = dynamic_cast<ast::ForStatement*>(node)) visitForStatement(forStmt);
    else if (auto br = dynamic_cast<ast::BreakStatement*>(node)) visitBreakStatement(br);
    else if (auto cont = dynamic_cast<ast::ContinueStatement*>(node)) visitContinueStatement(cont);
    else if (auto block = dynamic_cast<ast::BlockStatement*>(node)) visitBlockStatement(block);
    else if (auto pre = dynamic_cast<ast::PrefixUnaryExpression*>(node)) visitPrefixUnaryExpression(pre);
    else if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node)) visitVariableDeclaration(varDecl);
}

void IRGenerator::visitReturnStatement(ast::ReturnStatement* node) {
    if (node->expression) {
        visit(node->expression.get());
        
        if (lastValue) {
            llvm::Function* func = builder->GetInsertBlock()->getParent();
            llvm::Type* retType = func->getReturnType();
            
            if (lastValue->getType() != retType) {
                if (retType->isDoubleTy() && lastValue->getType()->isIntegerTy(64)) {
                    lastValue = builder->CreateSIToFP(lastValue, retType, "casttmp");
                } else if (retType->isIntegerTy(64) && lastValue->getType()->isDoubleTy()) {
                    lastValue = builder->CreateFPToSI(lastValue, retType, "casttmp");
                }
            }
            builder->CreateRet(lastValue);
        } else {
             // Error?
        }
    } else {
        builder->CreateRetVoid();
    }
}

void IRGenerator::visitBinaryExpression(ast::BinaryExpression* node) {
    visit(node->left.get());
    llvm::Value* left = lastValue;

    visit(node->right.get());
    llvm::Value* right = lastValue;

    if (!left || !right) return;

    bool leftIsDouble = left->getType()->isDoubleTy();
    bool rightIsDouble = right->getType()->isDoubleTy();
    bool isDouble = leftIsDouble || rightIsDouble;
    bool isString = left->getType()->isPointerTy() && right->getType()->isPointerTy();

    if (isDouble && !isString) {
        if (!leftIsDouble) left = builder->CreateSIToFP(left, llvm::Type::getDoubleTy(*context), "casttmp");
        if (!rightIsDouble) right = builder->CreateSIToFP(right, llvm::Type::getDoubleTy(*context), "casttmp");
    }
    // ...

    if (node->op == "+") {
        if (isString) {
             llvm::Function* concatFn = module->getFunction("ts_string_concat");
             if (!concatFn) {
                 std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) };
                 llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), args, false);
                 concatFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_concat", module.get());
             }
             lastValue = builder->CreateCall(concatFn, { left, right });
        } else if (isDouble) {
            lastValue = builder->CreateFAdd(left, right, "addtmp");
        } else {
            lastValue = builder->CreateAdd(left, right, "addtmp");
        }
    } else if (node->op == "-") {
        if (isDouble) lastValue = builder->CreateFSub(left, right, "subtmp");
        else lastValue = builder->CreateSub(left, right, "subtmp");
    } else if (node->op == "*") {
        if (isDouble) lastValue = builder->CreateFMul(left, right, "multmp");
        else lastValue = builder->CreateMul(left, right, "multmp");
    } else if (node->op == "<") {
        if (isDouble) lastValue = builder->CreateFCmpOLT(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSLT(left, right, "cmptmp");
    } else if (node->op == "<=") {
        if (isDouble) lastValue = builder->CreateFCmpOLE(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSLE(left, right, "cmptmp");
    } else if (node->op == ">") {
        if (isDouble) lastValue = builder->CreateFCmpOGT(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSGT(left, right, "cmptmp");
    } else if (node->op == ">=") {
        if (isDouble) lastValue = builder->CreateFCmpOGE(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSGE(left, right, "cmptmp");
    } else if (node->op == "==") {
        if (isDouble) lastValue = builder->CreateFCmpOEQ(left, right, "cmptmp");
        else lastValue = builder->CreateICmpEQ(left, right, "cmptmp");
    } else if (node->op == "!=") {
        if (isDouble) lastValue = builder->CreateFCmpONE(left, right, "cmptmp");
        else lastValue = builder->CreateICmpNE(left, right, "cmptmp");
    }
    // TODO: Other ops
}

void IRGenerator::visitIdentifier(ast::Identifier* node) {
    if (namedValues.count(node->name)) {
        llvm::Value* val = namedValues[node->name];
        if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
            lastValue = builder->CreateLoad(alloca->getAllocatedType(), alloca, node->name.c_str());
        } else {
            llvm::errs() << "Error: Variable " << node->name << " is not an alloca\n";
            lastValue = nullptr;
        }
    } else {
        llvm::errs() << "Error: Undefined variable " << node->name << "\n";
        lastValue = nullptr;
    }
}

void IRGenerator::visitNumericLiteral(ast::NumericLiteral* node) {
    if (node->value == (int64_t)node->value) {
        lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, (int64_t)node->value));
    } else {
        lastValue = llvm::ConstantFP::get(*context, llvm::APFloat(node->value));
    }
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

void IRGenerator::visitCallExpression(ast::CallExpression* node) {
    // Check for console.log
    if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->callee.get())) {
        if (prop->name == "log") {
            if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (obj->name == "console") {
                    if (node->arguments.empty()) return;
                    
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    if (!arg) {
                        llvm::errs() << "Error: console.log argument evaluated to null\n";
                        return;
                    }

                    llvm::Type* argType = arg->getType();

                    if (argType->isVoidTy()) {
                        llvm::errs() << "Error: Argument to console.log is void\n";
                        return;
                    }

                    std::string funcName = "ts_console_log";
                    llvm::Type* paramType = llvm::PointerType::getUnqual(*context);

                    if (argType->isIntegerTy(64)) {
                        funcName = "ts_console_log_int";
                        paramType = llvm::Type::getInt64Ty(*context);
                    } else if (argType->isDoubleTy()) {
                        funcName = "ts_console_log_double";
                        paramType = llvm::Type::getDoubleTy(*context);
                    } else if (argType->isIntegerTy(1)) {
                        funcName = "ts_console_log_bool";
                        paramType = llvm::Type::getInt1Ty(*context);
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        llvm::Type::getVoidTy(*context), { paramType }, false);
                    llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

                    builder->CreateCall(logFn, { arg });
                    lastValue = nullptr;
                    return;
                }
            }
        } else if (prop->name == "readFileSync") {
            if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (obj->name == "fs") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    llvm::FunctionCallee readFn = module->getOrInsertFunction("ts_fs_readFileSync",
                        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::PointerType::getUnqual(*context) }, false));
                    
                    lastValue = builder->CreateCall(readFn, { arg });
                    return;
                }
            }
        } else if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "Math") {
                if (prop->name == "min" || prop->name == "max") {
                    if (node->arguments.size() < 2) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg1 = lastValue;
                    visit(node->arguments[1].get());
                    llvm::Value* arg2 = lastValue;
                    
                    std::string funcName = (prop->name == "min") ? "ts_math_min" : "ts_math_max";
                    llvm::FunctionCallee fn = module->getOrInsertFunction(funcName,
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg1, arg2 });
                    return;
                } else if (prop->name == "abs") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_abs",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getInt64Ty(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                } else if (prop->name == "floor") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    if (arg->getType()->isIntegerTy(64)) {
                        // floor(int) is just int
                        lastValue = arg;
                        return;
                    }
                    
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_floor",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getDoubleTy(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                }
            }
        }
        
        if (prop->name == "charCodeAt") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* index = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_charCodeAt",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, index });
             return;
        } else if (prop->name == "split") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* sep = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_split",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, sep });
             return;
        } else if (prop->name == "sort") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_sort",
                 llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             builder->CreateCall(fn, { obj });
             lastValue = nullptr;
             return;
        }
    }

    // User function call
    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        if (id->name == "parseInt") {
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* arg = lastValue;

             llvm::FunctionCallee parseFn = module->getOrInsertFunction("ts_parseInt",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             
             lastValue = builder->CreateCall(parseFn, { arg });
             return;
        }

        std::string funcName = id->name;
        std::string mangledName = funcName;

        std::vector<llvm::Value*> args;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            if (lastValue) {
                args.push_back(lastValue);
                
                mangledName += "_";
                if (lastValue->getType()->isDoubleTy()) mangledName += "dbl";
                else if (lastValue->getType()->isIntegerTy(64)) mangledName += "int";
                else if (lastValue->getType()->isPointerTy()) mangledName += "str";
                else mangledName += "any";
            }
        }

        llvm::Function* func = module->getFunction(mangledName);
        if (func) {
            llvm::errs() << "Found function: " << mangledName << "\n";
            lastValue = builder->CreateCall(func, args);
            llvm::errs() << "Created call\n";
        } else {
            llvm::errs() << "Function not found: " << mangledName << "\n";
            // TODO: Error handling
            lastValue = nullptr;
        }
    }
}

void IRGenerator::visitExpressionStatement(ast::ExpressionStatement* node) {
    visit(node->expression.get());
}

void IRGenerator::visitIfStatement(ast::IfStatement* node) {
    visit(node->condition.get());
    llvm::Value* condValue = lastValue;
    if (!condValue) return;

    // Convert condition to bool
    if (condValue->getType()->isIntegerTy(64)) {
        condValue = builder->CreateICmpNE(condValue, llvm::ConstantInt::get(*context, llvm::APInt(64, 0)), "ifcond");
    } else if (condValue->getType()->isDoubleTy()) {
        condValue = builder->CreateFCmpONE(condValue, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "ifcond");
    }

    llvm::Function* func = builder->GetInsertBlock()->getParent();

    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "then", func);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "else");
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "ifcont");

    bool hasElse = (node->elseStatement != nullptr);

    builder->CreateCondBr(condValue, thenBB, hasElse ? elseBB : mergeBB);

    // Emit then block
    builder->SetInsertPoint(thenBB);
    visit(node->thenStatement.get());
    
    // If the then block doesn't already have a terminator (like return), branch to merge
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(mergeBB);
    }

    // Emit else block
    if (hasElse) {
        func->insert(func->end(), elseBB);
        builder->SetInsertPoint(elseBB);
        visit(node->elseStatement.get());
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(mergeBB);
        }
    }

    // Emit merge block
    func->insert(func->end(), mergeBB);
    builder->SetInsertPoint(mergeBB);
}

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
        condValue = builder->CreateFCmpONE(condValue, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "ifcond");
    } else if (condValue->getType()->isIntegerTy()) {
        condValue = builder->CreateICmpNE(condValue, llvm::ConstantInt::get(condValue->getType(), 0), "ifcond");
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
            condValue = builder->CreateFCmpONE(condValue, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "ifcond");
        } else if (condValue->getType()->isIntegerTy()) {
            condValue = builder->CreateICmpNE(condValue, llvm::ConstantInt::get(condValue->getType(), 0), "ifcond");
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

void IRGenerator::visitBreakStatement(ast::BreakStatement* node) {
    if (loopStack.empty()) {
        llvm::errs() << "Error: Break statement outside of loop\n";
        return;
    }
    builder->CreateBr(loopStack.back().breakBlock);
    // Start a new block for dead code after break
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(*context, "dead", func);
    builder->SetInsertPoint(deadBB);
}

void IRGenerator::visitContinueStatement(ast::ContinueStatement* node) {
    if (loopStack.empty()) {
        llvm::errs() << "Error: Continue statement outside of loop\n";
        return;
    }
    builder->CreateBr(loopStack.back().continueBlock);
    // Start a new block for dead code after continue
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(*context, "dead", func);
    builder->SetInsertPoint(deadBB);
}

void IRGenerator::visitBlockStatement(ast::BlockStatement* node) {
    for (const auto& stmt : node->statements) {
        visit(stmt.get());
    }
}

void IRGenerator::visitVariableDeclaration(ast::VariableDeclaration* node) {
    llvm::Function* function = builder->GetInsertBlock()->getParent();
    
    // Determine type from initializer
    visit(node->initializer.get());
    llvm::Value* initVal = lastValue;
    
    if (!initVal) {
        llvm::errs() << "Error: Variable initializer evaluated to null\n";
        return;
    }

    llvm::AllocaInst* alloca = createEntryBlockAlloca(function, node->name, initVal->getType());
    builder->CreateStore(initVal, alloca);

    namedValues[node->name] = alloca;
}

void IRGenerator::visitAssignmentExpression(ast::AssignmentExpression* node) {
    // 1. Evaluate the RHS
    visit(node->right.get());
    llvm::Value* val = lastValue;
    if (!val) return;

    // 2. Check LHS type
    if (auto id = dynamic_cast<ast::Identifier*>(node->left.get())) {
        // 3. Look up the variable
        llvm::Value* variable = namedValues[id->name];
        if (!variable) {
            llvm::errs() << "Error: Unknown variable name " << id->name << "\n";
            return;
        }

        // 4. Store the value
        builder->CreateStore(val, variable);
    } else if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node->left.get())) {
        visit(elem->expression.get());
        llvm::Value* arr = lastValue;
        
        visit(elem->argumentExpression.get());
        llvm::Value* index = lastValue;
        
        if (index->getType()->isDoubleTy()) {
            index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
        }
        
        llvm::Value* storeVal = val;
        if (storeVal->getType()->isDoubleTy()) {
            storeVal = builder->CreateBitCast(storeVal, llvm::Type::getInt64Ty(*context));
        } else if (storeVal->getType()->isPointerTy()) {
            storeVal = builder->CreatePtrToInt(storeVal, llvm::Type::getInt64Ty(*context));
        }

        llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_array_set",
            llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
                
        builder->CreateCall(setFn, { arr, index, storeVal });
    } else {
        llvm::errs() << "Error: LHS of assignment must be an identifier or element access\n";
        return;
    }
    
    // Assignment evaluates to the value
    lastValue = val;
}

void IRGenerator::dumpIR() {
    module->print(llvm::outs(), nullptr);
}

void IRGenerator::emitObjectCode(const std::string& filename) {
    // Initialize the target registry etc.
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(targetTriple);

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

    if (!target) {
        llvm::errs() << error;
        return;
    }

    auto cpu = "generic";
    auto features = "";

    llvm::TargetOptions opt;
    auto rm = std::optional<llvm::Reloc::Model>();
    auto targetMachine = target->createTargetMachine(targetTriple, cpu, features, opt, rm);

    module->setDataLayout(targetMachine->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if (ec) {
        llvm::errs() << "Could not open file: " << ec.message();
        return;
    }

    llvm::legacy::PassManager pass;
    auto fileType = llvm::CodeGenFileType::ObjectFile;

    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
        llvm::errs() << "TargetMachine can't emit a file of this type";
        return;
    }

    pass.run(*module);
    dest.flush();
}

void IRGenerator::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create", 
        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false));
    
    llvm::Value* arr = builder->CreateCall(createFn);

    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push",
        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
            { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));

    for (auto& el : node->elements) {
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

void IRGenerator::visitElementAccessExpression(ast::ElementAccessExpression* node) {
    visit(node->expression.get());
    llvm::Value* arr = lastValue;
    
    visit(node->argumentExpression.get());
    llvm::Value* index = lastValue;
    
    if (index->getType()->isDoubleTy()) {
        index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
    }
    
    llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_array_get",
        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
            { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
            
    llvm::Value* val = builder->CreateCall(getFn, { arr, index });

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
        auto arrayType = std::static_pointer_cast<ts::ArrayType>(node->expression->inferredType);
        if (arrayType->elementType->kind == TypeKind::String) {
             lastValue = builder->CreateIntToPtr(val, llvm::PointerType::getUnqual(*context));
             return;
        }
    }
    
    lastValue = val;
}

void IRGenerator::visitPropertyAccessExpression(ast::PropertyAccessExpression* node) {
    if (node->name == "length") {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;
        
        if (!node->expression->inferredType) {
             // Fallback: assume string if we can't tell? Or error?
             // For now, let's assume string as a default if type is missing (e.g. string literal)
             // Actually, string literals should have type inferred.
             llvm::errs() << "Warning: No type inferred for length access, assuming string\n";
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(lenFn, { obj });
             return;
        }
        
        if (node->expression->inferredType->kind == TypeKind::String) {
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(lenFn, { obj });
        } else if (node->expression->inferredType->kind == TypeKind::Array) {
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(lenFn, { obj });
        } else {
             llvm::errs() << "Error: length property not supported on type " << node->expression->inferredType->toString() << "\n";
             lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, 0));
        }
    } else {
        llvm::errs() << "Error: Unknown property " << node->name << "\n";
        lastValue = nullptr;
    }
}

void IRGenerator::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    visit(node->operand.get());
    llvm::Value* operandV = lastValue;
    if (!operandV) return;

    if (node->op == "-") {
        if (operandV->getType()->isDoubleTy()) {
            lastValue = builder->CreateFNeg(operandV, "negtmp");
        } else if (operandV->getType()->isIntegerTy()) {
            lastValue = builder->CreateNeg(operandV, "negtmp");
        }
    } else if (node->op == "!") {
        if (operandV->getType()->isDoubleTy()) {
            operandV = builder->CreateFCmpONE(operandV, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "tobool");
        } else if (operandV->getType()->isIntegerTy() && operandV->getType()->getIntegerBitWidth() > 1) {
             operandV = builder->CreateICmpNE(operandV, llvm::ConstantInt::get(operandV->getType(), 0), "tobool");
        }
        lastValue = builder->CreateNot(operandV, "nottmp");
    }
}

} // namespace ts
