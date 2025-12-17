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

void IRGenerator::dumpIR() {
    module->print(llvm::outs(), nullptr);
}

} // namespace ts
