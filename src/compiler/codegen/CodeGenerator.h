#pragma once

#include <string>
#include <memory>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>

namespace ts {

class CodeGenerator {
public:
    CodeGenerator(llvm::Module* module);
    
    bool emitObjectFile(const std::string& filename, const std::string& optLevel = "0");
    bool emitBitcode(const std::string& filename);
    bool emitAssembly(const std::string& filename);

private:
    llvm::Module* module;
    std::unique_ptr<llvm::TargetMachine> targetMachine;

    bool setupTargetMachine(const std::string& optLevel);
    void runOptimizations(const std::string& optLevel);
};

} // namespace ts
