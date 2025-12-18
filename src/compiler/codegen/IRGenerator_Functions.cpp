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
        
        if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            unsigned idx = 0;
            for (auto& arg : function->args()) {
                if (idx < funcNode->parameters.size()) {
                    std::string argName = funcNode->parameters[idx]->name;
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
                std::string argName = methodNode->parameters[idx]->name;
                argIt->setName(argName);
                
                llvm::AllocaInst* alloca = createEntryBlockAlloca(function, argName, argIt->getType());
                builder->CreateStore(&*argIt, alloca);
                namedValues[argName] = alloca;
                
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
        if (param->type == "number") argTypes.push_back(llvm::Type::getDoubleTy(*context));
        else if (param->type == "string") argTypes.push_back(llvm::PointerType::getUnqual(*context));
        else argTypes.push_back(llvm::Type::getDoubleTy(*context));
    }
    
    llvm::Type* retType = llvm::Type::getDoubleTy(*context); // Default to double
    
    llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
    llvm::Function* function = llvm::Function::Create(ft, llvm::Function::InternalLinkage, name, module.get());
    
    llvm::BasicBlock* oldBB = builder->GetInsertBlock();
    
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(bb);
    
    auto oldNamedValues = namedValues;
    namedValues.clear();
    
    unsigned idx = 0;
    for (auto& arg : function->args()) {
        std::string argName = node->parameters[idx]->name;
        arg.setName(argName);
        llvm::AllocaInst* alloca = createEntryBlockAlloca(function, argName, arg.getType());
        builder->CreateStore(&arg, alloca);
        namedValues[argName] = alloca;
        idx++;
    }
    
    visit(node->body.get());
    
    if (lastValue) {
        builder->CreateRet(lastValue);
    } else {
        if (dynamic_cast<ast::Expression*>(node->body.get())) {
             builder->CreateRet(lastValue);
        } else {
             if (!builder->GetInsertBlock()->getTerminator()) {
                 builder->CreateRet(llvm::ConstantFP::get(*context, llvm::APFloat(0.0)));
             }
        }
    }
    
    builder->SetInsertPoint(oldBB);
    namedValues = oldNamedValues;
    
    lastValue = function;
}

} // namespace ts
