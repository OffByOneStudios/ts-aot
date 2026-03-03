#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace ts::hir {

//==============================================================================
// RegExpHandler - Handles RegExp method calls
//
// This handler extracts RegExp method lowering from the monolithic
// lowerCallMethod function.
//
// Supported methods:
// - RegExp.test(string) -> boolean
// - RegExp.exec(string) -> array | null
//==============================================================================
class RegExpHandler : public BuiltinHandler {
public:
    const char* name() const override { return "RegExpHandler"; }

    //==========================================================================
    // Function call interface (for lowerCall)
    // RegExp operations are method-based, not function-based
    //==========================================================================

    bool canHandle(const std::string& funcName, HIRInstruction* inst) const override {
        // RegExp operations are method-based, not function-based
        return false;
    }

    llvm::Value* lower(const std::string& funcName, HIRInstruction* inst,
                       HIRToLLVM& lowerer) override {
        return nullptr;
    }

    //==========================================================================
    // Method call interface (for lowerCallMethod - e.g., regex.test(str))
    //==========================================================================

    bool canHandleMethod(const std::string& methodName,
                         const std::string& className,
                         HIRInstruction* inst) const override {
        // RegExp methods - handle for known RegExp class
        if (className == "RegExp") {
            if (methodName == "test" || methodName == "exec") {
                return true;
            }
        }
        // For unknown types (untyped JS), do NOT assume .test()/.exec() are
        // RegExp methods. User-defined classes (Range, Comparator, etc.) may
        // also have .test()/.exec() methods with the same arity.
        // Let runtime dynamic dispatch handle these via ts_object_get_property.
        return false;
    }

    llvm::Value* lowerMethod(const std::string& methodName,
                             HIRInstruction* inst,
                             HIRToLLVM& lowerer) override {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // For method calls: operands[0] = object, operands[1] = methodName, operands[2..] = args
        llvm::Value* obj = lowerer.getOperandValue(inst->operands[0]);

        if (methodName == "test") {
            return lowerRegExpTest(inst, obj, lowerer);
        }
        if (methodName == "exec") {
            return lowerRegExpExec(inst, obj, lowerer);
        }

        return nullptr;
    }

private:
    //==========================================================================
    // RegExp.test(string) -> boolean
    //
    // Returns true if the string matches the pattern, false otherwise.
    //==========================================================================
    llvm::Value* lowerRegExpTest(HIRInstruction* inst, llvm::Value* obj, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // RegExp_test(re, str) -> returns i32 (0 or 1)
        llvm::FunctionType* testFt = llvm::FunctionType::get(
            builder.getInt32Ty(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false
        );
        llvm::FunctionCallee testFn = module.getOrInsertFunction("RegExp_test", testFt);

        // Get the string argument
        llvm::Value* str = llvm::ConstantPointerNull::get(builder.getPtrTy());
        if (inst->operands.size() > 2) {
            str = lowerer.getOperandValue(inst->operands[2]);
        }

        // Call RegExp_test and convert i32 result to i1 (bool)
        llvm::Value* result32 = builder.CreateCall(testFt, testFn.getCallee(), { obj, str });
        return builder.CreateICmpNE(result32,
            llvm::ConstantInt::get(builder.getInt32Ty(), 0));
    }

    //==========================================================================
    // RegExp.exec(string) -> array | null
    //
    // Returns an array of matches or null if no match.
    //==========================================================================
    llvm::Value* lowerRegExpExec(HIRInstruction* inst, llvm::Value* obj, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // RegExp_exec(void* regexp, void* string) -> void* (array or null)
        llvm::FunctionType* execFt = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false
        );
        llvm::FunctionCallee execFn = module.getOrInsertFunction("RegExp_exec", execFt);

        // Get the string argument
        llvm::Value* str = llvm::ConstantPointerNull::get(builder.getPtrTy());
        if (inst->operands.size() > 2) {
            str = lowerer.getOperandValue(inst->operands[2]);
        }

        return builder.CreateCall(execFt, execFn.getCallee(), { obj, str });
    }
};

} // namespace ts::hir

// Factory function to create RegExpHandler - called from HandlerRegistry
namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createRegExpHandler() {
        return std::make_unique<RegExpHandler>();
    }
}
