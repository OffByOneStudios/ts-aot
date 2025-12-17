#pragma once

#include <memory>
#include <string>
#include <vector>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>

#include "../analysis/Monomorphizer.h"

namespace ts {

class IRGenerator {
public:
    IRGenerator();

    void generate(const std::vector<Specialization>& specializations);
    void dumpIR();

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    llvm::Type* getLLVMType(const std::shared_ptr<Type>& type);
    void generatePrototypes(const std::vector<Specialization>& specializations);
};

} // namespace ts
