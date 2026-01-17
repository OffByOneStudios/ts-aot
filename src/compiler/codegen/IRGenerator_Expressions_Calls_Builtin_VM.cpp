#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register VM module's runtime functions once
static bool vmFunctionsRegistered = false;
static void ensureVMFunctionsRegistered(BoxingPolicy& bp) {
    if (vmFunctionsRegistered) return;
    vmFunctionsRegistered = true;

    // All VM functions are stubs that throw errors
    bp.registerRuntimeApi("ts_vm_not_supported", {true}, false);
}

bool IRGenerator::tryGenerateVMCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureVMFunctionsRegistered(boxingPolicy);

    // Check if this is a vm.* call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "vm") return false;

    SPDLOG_DEBUG("tryGenerateVMCall: vm.{}", prop->name);

    // All vm module methods call the same stub that throws an error
    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
        { builder->getPtrTy() }, false);
    llvm::FunctionCallee fn = getRuntimeFunction("ts_vm_not_supported", ft);

    // Pass the method name as a string for the error message
    llvm::Value* methodName = builder->CreateGlobalStringPtr(prop->name);
    lastValue = createCall(ft, fn.getCallee(), { methodName });
    return true;
}

} // namespace ts
