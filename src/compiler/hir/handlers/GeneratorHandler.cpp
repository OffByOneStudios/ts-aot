#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace ts::hir {

//==============================================================================
// GeneratorHandler - Handles Generator and AsyncGenerator method calls
//
// This handler extracts Generator and AsyncGenerator method lowering from the
// monolithic lowerCallMethod function. Generator operations need proper boxing
// of the generator object and any passed values.
//
// Supported methods:
// - Generator.next(value?) -> IteratorResult { value, done }
// - AsyncGenerator.next(value?) -> Promise<IteratorResult>
//==============================================================================
class GeneratorHandler : public BuiltinHandler {
public:
    const char* name() const override { return "GeneratorHandler"; }

    //==========================================================================
    // Function call interface (for lowerCall)
    // Generators don't have function-style calls, only method calls
    //==========================================================================

    bool canHandle(const std::string& funcName, HIRInstruction* inst) const override {
        // Generator operations are method-based, not function-based
        return false;
    }

    llvm::Value* lower(const std::string& funcName, HIRInstruction* inst,
                       HIRToLLVM& lowerer) override {
        return nullptr;
    }

    //==========================================================================
    // Method call interface (for lowerCallMethod - e.g., gen.next(value))
    //==========================================================================

    bool canHandleMethod(const std::string& methodName,
                         const std::string& className,
                         HIRInstruction* inst) const override {
        // Generator methods
        if (className == "Generator" || className.empty()) {
            if (methodName == "next" || methodName == "return" || methodName == "throw") {
                return true;
            }
        }
        // AsyncGenerator methods
        if (className == "AsyncGenerator") {
            if (methodName == "next" || methodName == "return" || methodName == "throw") {
                return true;
            }
        }
        return false;
    }

    llvm::Value* lowerMethod(const std::string& methodName,
                             HIRInstruction* inst,
                             HIRToLLVM& lowerer) override {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // For method calls: operands[0] = object, operands[1] = methodName, operands[2..] = args
        llvm::Value* obj = lowerer.getOperandValue(inst->operands[0]);

        // Check for AsyncGenerator type
        std::string className;
        if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
            if (*valPtr && (*valPtr)->type && (*valPtr)->type->kind == HIRTypeKind::Class) {
                className = (*valPtr)->type->className;
            }
        }

        bool isAsync = (className == "AsyncGenerator");

        if (methodName == "next") {
            return lowerGeneratorNext(inst, obj, isAsync, lowerer);
        }
        if (methodName == "return") {
            return lowerGeneratorReturn(inst, obj, isAsync, lowerer);
        }
        if (methodName == "throw") {
            return lowerGeneratorThrow(inst, obj, isAsync, lowerer);
        }

        return nullptr;
    }

private:
    //==========================================================================
    // Boxing helper for generator values
    //==========================================================================
    llvm::Value* boxValue(llvm::Value* val, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        if (!val->getType()->isPointerTy()) {
            if (val->getType()->isIntegerTy(64)) {
                // Box integer: ts_value_make_int(i64) -> ptr
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder.getPtrTy(), { builder.getInt64Ty() }, false);
                llvm::FunctionCallee fn = module.getOrInsertFunction("ts_value_make_int", ft);
                return builder.CreateCall(ft, fn.getCallee(), {val});
            } else if (val->getType()->isDoubleTy()) {
                // Box double: ts_value_make_double(double) -> ptr
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder.getPtrTy(), { builder.getDoubleTy() }, false);
                llvm::FunctionCallee fn = module.getOrInsertFunction("ts_value_make_double", ft);
                return builder.CreateCall(ft, fn.getCallee(), {val});
            } else if (val->getType()->isIntegerTy(1)) {
                // Box bool: ts_value_make_bool(i32) -> ptr
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder.getPtrTy(), { builder.getInt32Ty() }, false);
                llvm::FunctionCallee fn = module.getOrInsertFunction("ts_value_make_bool", ft);
                llvm::Value* extended = builder.CreateZExt(val, builder.getInt32Ty());
                return builder.CreateCall(ft, fn.getCallee(), {extended});
            }
        }
        return val;
    }

    llvm::Value* boxGeneratorObject(llvm::Value* obj, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* boxedGen = obj;
        if (!obj->getType()->isPointerTy()) {
            boxedGen = builder.CreateIntToPtr(obj, builder.getPtrTy());
        }

        // Box the generator object pointer
        llvm::FunctionType* boxObjFt = llvm::FunctionType::get(
            builder.getPtrTy(), { builder.getPtrTy() }, false);
        llvm::FunctionCallee boxObjFn = module.getOrInsertFunction("ts_value_make_object", boxObjFt);
        return builder.CreateCall(boxObjFt, boxObjFn.getCallee(), { boxedGen });
    }

    //==========================================================================
    // Generator/AsyncGenerator.next(value?) -> IteratorResult or Promise
    //==========================================================================
    llvm::Value* lowerGeneratorNext(HIRInstruction* inst, llvm::Value* obj,
                                     bool isAsync, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // Determine runtime function name
        std::string fnName = isAsync ? "AsyncGenerator_next" : "Generator_next";

        // Generator_next(gen, value) -> returns iterator result {value, done}
        // AsyncGenerator_next(gen, value) -> returns Promise<iterator result>
        llvm::FunctionType* nextFt = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false
        );
        llvm::FunctionCallee nextFn = module.getOrInsertFunction(fnName, nextFt);

        // Box the generator object
        llvm::Value* boxedGen = boxGeneratorObject(obj, lowerer);

        // Get the argument (or null if none)
        llvm::Value* val = llvm::ConstantPointerNull::get(builder.getPtrTy());
        if (inst->operands.size() > 2) {
            val = lowerer.getOperandValue(inst->operands[2]);
            val = boxValue(val, lowerer);
        }

        return builder.CreateCall(nextFt, nextFn.getCallee(), { boxedGen, val });
    }

    //==========================================================================
    // Generator/AsyncGenerator.return(value?) -> IteratorResult or Promise
    //==========================================================================
    llvm::Value* lowerGeneratorReturn(HIRInstruction* inst, llvm::Value* obj,
                                       bool isAsync, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // Determine runtime function name
        std::string fnName = isAsync ? "AsyncGenerator_return" : "Generator_return";

        llvm::FunctionType* returnFt = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false
        );
        llvm::FunctionCallee returnFn = module.getOrInsertFunction(fnName, returnFt);

        // Box the generator object
        llvm::Value* boxedGen = boxGeneratorObject(obj, lowerer);

        // Get the argument (or null if none)
        llvm::Value* val = llvm::ConstantPointerNull::get(builder.getPtrTy());
        if (inst->operands.size() > 2) {
            val = lowerer.getOperandValue(inst->operands[2]);
            val = boxValue(val, lowerer);
        }

        return builder.CreateCall(returnFt, returnFn.getCallee(), { boxedGen, val });
    }

    //==========================================================================
    // Generator/AsyncGenerator.throw(exception?) -> IteratorResult or Promise
    //==========================================================================
    llvm::Value* lowerGeneratorThrow(HIRInstruction* inst, llvm::Value* obj,
                                      bool isAsync, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // Determine runtime function name
        std::string fnName = isAsync ? "AsyncGenerator_throw" : "Generator_throw";

        llvm::FunctionType* throwFt = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false
        );
        llvm::FunctionCallee throwFn = module.getOrInsertFunction(fnName, throwFt);

        // Box the generator object
        llvm::Value* boxedGen = boxGeneratorObject(obj, lowerer);

        // Get the argument (or null if none)
        llvm::Value* val = llvm::ConstantPointerNull::get(builder.getPtrTy());
        if (inst->operands.size() > 2) {
            val = lowerer.getOperandValue(inst->operands[2]);
            val = boxValue(val, lowerer);
        }

        return builder.CreateCall(throwFt, throwFn.getCallee(), { boxedGen, val });
    }
};

} // namespace ts::hir

// Factory function to create GeneratorHandler - called from HandlerRegistry
namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createGeneratorHandler() {
        return std::make_unique<GeneratorHandler>();
    }
}
