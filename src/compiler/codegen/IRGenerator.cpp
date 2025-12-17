#include "IRGenerator.h"
#include <llvm/Support/raw_ostream.h>

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
                namedValues[argName] = &arg;
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
    else if (auto id = dynamic_cast<ast::Identifier*>(node)) visitIdentifier(id);
    else if (auto num = dynamic_cast<ast::NumericLiteral*>(node)) visitNumericLiteral(num);
    else if (auto str = dynamic_cast<ast::StringLiteral*>(node)) visitStringLiteral(str);
    else if (auto call = dynamic_cast<ast::CallExpression*>(node)) visitCallExpression(call);
    else if (auto exprStmt = dynamic_cast<ast::ExpressionStatement*>(node)) visitExpressionStatement(exprStmt);
}

void IRGenerator::visitReturnStatement(ast::ReturnStatement* node) {
    if (node->expression) {
        visit(node->expression.get());
        builder->CreateRet(lastValue);
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

    bool isDouble = left->getType()->isDoubleTy() || right->getType()->isDoubleTy();

    if (node->op == "+") {
        if (isDouble) lastValue = builder->CreateFAdd(left, right, "addtmp");
        else lastValue = builder->CreateAdd(left, right, "addtmp");
    } else if (node->op == "-") {
        if (isDouble) lastValue = builder->CreateFSub(left, right, "subtmp");
        else lastValue = builder->CreateSub(left, right, "subtmp");
    } else if (node->op == "*") {
        if (isDouble) lastValue = builder->CreateFMul(left, right, "multmp");
        else lastValue = builder->CreateMul(left, right, "multmp");
    }
    // TODO: Other ops
}

void IRGenerator::visitIdentifier(ast::Identifier* node) {
    if (namedValues.count(node->name)) {
        lastValue = namedValues[node->name];
    } else {
        // TODO: Global variables or error
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
                    if (!arg) return;

                    llvm::Function* logFn = module->getFunction("ts_console_log");
                    if (!logFn) {
                        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context) };
                        llvm::FunctionType* ft = llvm::FunctionType::get(
                            llvm::Type::getVoidTy(*context), args, false);
                        logFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_console_log", module.get());
                    }

                    builder->CreateCall(logFn, { arg });
                    lastValue = nullptr;
                    return;
                }
            }
        }
    }

    // User function call
    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
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
            lastValue = builder->CreateCall(func, args);
        } else {
            // TODO: Error handling
            lastValue = nullptr;
        }
    }
}

void IRGenerator::visitExpressionStatement(ast::ExpressionStatement* node) {
    visit(node->expression.get());
}

void IRGenerator::dumpIR() {
    module->print(llvm::outs(), nullptr);
}

} // namespace ts
