#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {

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

        // Skip if this is a method signature without a body
        if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            if (methodNode->isAbstract || !methodNode->hasBody) continue;
        }

        // If function already has a body (from a previous specialization that mapped to the same name), skip
        if (!function->empty()) continue;

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(bb);

        namedValues.clear();
        currentClass = spec.classType;
        typeEnvironment.clear();

        if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            // Populate type environment
            for (size_t i = 0; i < funcNode->typeParameters.size(); ++i) {
                if (i < spec.typeArguments.size()) {
                    typeEnvironment[funcNode->typeParameters[i]->name] = spec.typeArguments[i];
                }
            }

            unsigned idx = 0;
            for (auto& arg : function->args()) {
                if (idx < funcNode->parameters.size()) {
                    auto param = funcNode->parameters[idx].get();
                    if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                        arg.setName(id->name);
                    }
                    generateDestructuring(&arg, spec.argTypes[idx], param->name.get());
                }
                idx++;
            }

            for (auto& stmt : funcNode->body) {
                visit(stmt.get());
            }
        } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            auto argIt = function->arg_begin();
            if (!methodNode->isStatic) {
                // Handle 'this' parameter (first argument)
                if (argIt != function->arg_end()) {
                    argIt->setName("this");
                    llvm::AllocaInst* alloca = createEntryBlockAlloca(function, "this", argIt->getType());
                    builder->CreateStore(&*argIt, alloca);
                    namedValues["this"] = alloca;
                    ++argIt;
                }
            }

            // Handle explicit parameters
            unsigned idx = 0;
            while (argIt != function->arg_end() && idx < methodNode->parameters.size()) {
                auto param = methodNode->parameters[idx].get();
                if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    argIt->setName(id->name);
                }
                
                unsigned argTypeIdx = methodNode->isStatic ? idx : idx + 1;
                if (argTypeIdx < spec.argTypes.size()) {
                    generateDestructuring(&*argIt, spec.argTypes[argTypeIdx], param->name.get());
                }
                
                ++argIt;
                ++idx;
            }

            for (auto& stmt : methodNode->body) {
                visit(stmt.get());
            }
        }

        if (!builder->GetInsertBlock()->getTerminator()) {
            llvm::Type* retType = function->getReturnType();
            if (retType->isVoidTy()) {
                builder->CreateRetVoid();
            } else {
                builder->CreateRet(llvm::Constant::getNullValue(retType));
            }
        }
    }
}

void IRGenerator::visitArrowFunction(ast::ArrowFunction* node) {
    static int lambdaCounter = 0;
    std::string name = "lambda_" + std::to_string(lambdaCounter++);
    
    std::vector<llvm::Type*> argTypes;
    for (auto& param : node->parameters) {
        argTypes.push_back(builder->getPtrTy()); // TsValue*
    }
    
    llvm::Type* retType = builder->getPtrTy(); // TsValue*
    
    llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
    llvm::Function* function = llvm::Function::Create(ft, llvm::Function::InternalLinkage, name, module.get());
    
    llvm::BasicBlock* oldBB = builder->GetInsertBlock();
    
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(bb);
    
    auto oldNamedValues = namedValues;
    namedValues.clear();
    
    unsigned idx = 0;
    auto funcType = std::static_pointer_cast<FunctionType>(node->inferredType);
    for (auto& arg : function->args()) {
        auto param = node->parameters[idx].get();
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            arg.setName(id->name);
        }
        std::shared_ptr<Type> paramType = (funcType && idx < funcType->paramTypes.size()) ? funcType->paramTypes[idx] : std::make_shared<Type>(TypeKind::Any);
        
        // Unbox the argument
        llvm::Value* unboxedArg = unboxValue(&arg, paramType);
        generateDestructuring(unboxedArg, paramType, param->name.get());
        idx++;
    }
    
    visit(node->body.get());
    
    if (lastValue) {
        // Box the return value
        std::shared_ptr<Type> returnType = funcType ? funcType->returnType : std::make_shared<Type>(TypeKind::Any);
        llvm::Value* boxedRet = boxValue(lastValue, returnType);
        builder->CreateRet(boxedRet);
    } else {
        builder->CreateRet(llvm::ConstantPointerNull::get(builder->getPtrTy()));
    }
    
    builder->SetInsertPoint(oldBB);
    namedValues = oldNamedValues;
    
    lastValue = function;
}

} // namespace ts
