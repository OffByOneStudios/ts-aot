#include "IRGenerator.h"
#include <llvm/Support/raw_ostream.h>

namespace ts {

IRGenerator::IRGenerator() {
    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("ts_module", *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);
}

void IRGenerator::generate(const std::vector<Specialization>& specializations) {
    // TODO: Implement generation logic
}

void IRGenerator::dumpIR() {
    module->print(llvm::outs(), nullptr);
}

} // namespace ts
