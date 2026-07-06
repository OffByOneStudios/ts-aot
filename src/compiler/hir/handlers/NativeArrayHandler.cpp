#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <spdlog/spdlog.h>

namespace ts::hir {

//==============================================================================
// NativeArrayHandler - "use fast" NativeArray<T> method lowering
//
// NativeArray<T> is an UNMANAGED, contiguous, typed container (see
// docs/design/use-fast.md Phase 2 and src/runtime/src/TsNativeArray.cpp). This
// handler lowers its methods directly to the runtime C ABI:
//   .get(i)     -> ts_native_array_get_f64 / _get_i64   (by element type T)
//   .set(i, v)  -> ts_native_array_set_f64 / _set_i64
//   .dispose()  -> ts_native_array_dispose
// (.length is a property, lowered in HIRToLLVM_Memory.cpp, not here.)
//
// The receiver's HIRType is Class("NativeArray") with elementType carrying T
// (Int64 -> i64 slots, anything else -> f64 slots). Construction is lowered in
// ASTToHIR::visitNewExpression. Everything here is reached only when the
// analyzer confirmed a NativeArray receiver, which only exists in fast files.
//==============================================================================
class NativeArrayHandler : public BuiltinHandler {
public:
    const char* name() const override { return "NativeArrayHandler"; }

    // No plain-function form.
    bool canHandle(const std::string&, HIRInstruction*) const override { return false; }
    llvm::Value* lower(const std::string&, HIRInstruction*, HIRToLLVM&) override {
        return nullptr;
    }

    bool canHandleMethod(const std::string& methodName,
                         const std::string& className,
                         HIRInstruction*) const override {
        if (className != "NativeArray") return false;
        return methodName == "get" || methodName == "set" ||
               methodName == "dispose";
    }

    llvm::Value* lowerMethod(const std::string& methodName,
                             HIRInstruction* inst,
                             HIRToLLVM& lowerer) override {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // operands[0] = receiver, [1] = method name, [2..] = args
        llvm::Value* arr = lowerer.getOperandValue(inst->operands[0]);

        // Element type: Int64 -> i64 slots, otherwise f64 slots.
        bool isInt = false;
        if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
            if (*valPtr && (*valPtr)->type && (*valPtr)->type->elementType &&
                (*valPtr)->type->elementType->kind == HIRTypeKind::Int64) {
                isInt = true;
            }
        }

        if (methodName == "dispose") {
            auto ft = llvm::FunctionType::get(builder.getVoidTy(),
                                              { builder.getPtrTy() }, false);
            auto fn = module.getOrInsertFunction("ts_native_array_dispose", ft);
            builder.CreateCall(ft, fn.getCallee(), { arr });
            return llvm::ConstantPointerNull::get(builder.getPtrTy());
        }

        if (methodName == "get") {
            llvm::Value* idx = toI64(inst->operands.size() > 2
                                         ? lowerer.getOperandValue(inst->operands[2])
                                         : nullptr,
                                     lowerer);
            if (lowerer.fastChecks()) {
                // Dev build: bounds/dispose-checked runtime call.
                const char* rn = isInt ? "ts_native_array_get_i64" : "ts_native_array_get_f64";
                llvm::Type* rt = isInt ? (llvm::Type*)builder.getInt64Ty()
                                       : (llvm::Type*)builder.getDoubleTy();
                auto ft = llvm::FunctionType::get(
                    rt, { builder.getPtrTy(), builder.getInt64Ty() }, false);
                auto fn = module.getOrInsertFunction(rn, ft);
                return builder.CreateCall(ft, fn.getCallee(), { arr, idx });
            }
            // Release: inline unboxed load. slot ptr = base + 16 + i*8.
            llvm::Value* slot = slotPtr(arr, idx, lowerer);
            llvm::Type* rt = isInt ? (llvm::Type*)builder.getInt64Ty()
                                   : (llvm::Type*)builder.getDoubleTy();
            return builder.CreateLoad(rt, slot, "na.get");
        }

        if (methodName == "set") {
            llvm::Value* idx = toI64(inst->operands.size() > 2
                                         ? lowerer.getOperandValue(inst->operands[2])
                                         : nullptr,
                                     lowerer);
            llvm::Value* raw = inst->operands.size() > 3
                                   ? lowerer.getOperandValue(inst->operands[3])
                                   : nullptr;
            llvm::Value* v = isInt ? toI64(raw, lowerer) : toF64(raw, lowerer);
            if (lowerer.fastChecks()) {
                const char* rn = isInt ? "ts_native_array_set_i64" : "ts_native_array_set_f64";
                llvm::Type* vt = isInt ? (llvm::Type*)builder.getInt64Ty()
                                       : (llvm::Type*)builder.getDoubleTy();
                auto ft = llvm::FunctionType::get(
                    builder.getVoidTy(), { builder.getPtrTy(), builder.getInt64Ty(), vt },
                    false);
                auto fn = module.getOrInsertFunction(rn, ft);
                builder.CreateCall(ft, fn.getCallee(), { arr, idx, v });
                return llvm::ConstantPointerNull::get(builder.getPtrTy());
            }
            // Release: inline unboxed store.
            llvm::Value* slot = slotPtr(arr, idx, lowerer);
            builder.CreateStore(v, slot);
            return llvm::ConstantPointerNull::get(builder.getPtrTy());
        }

        return nullptr;
    }

private:
    // Inline slot address: raw(base) + 16 + i*8. The 16-byte header is
    // magic(4) + allocKind(4) + length(8); 8-byte element slots follow (matches
    // TsNativeArray in the runtime). Uses the raw (addrspace 0) handle so the
    // load/store isn't entangled with GC statepoints — NativeArray memory is
    // off the GC heap.
    llvm::Value* slotPtr(llvm::Value* arr, llvm::Value* idx, HIRToLLVM& lowerer) {
        auto& b = lowerer.builder();
        llvm::Value* raw = lowerer.toRawPtr(arr);
        llvm::Value* off = b.CreateAdd(
            b.CreateMul(idx, llvm::ConstantInt::get(b.getInt64Ty(), 8)),
            llvm::ConstantInt::get(b.getInt64Ty(), 16));
        return b.CreateGEP(b.getInt8Ty(), raw, off, "na.slot");
    }

    // Coerce a value to i64 (index / integer slot).
    llvm::Value* toI64(llvm::Value* v, HIRToLLVM& lowerer) {
        auto& b = lowerer.builder();
        if (!v) return llvm::ConstantInt::get(b.getInt64Ty(), 0);
        llvm::Type* t = v->getType();
        if (t->isDoubleTy()) return b.CreateFPToSI(v, b.getInt64Ty());
        if (t->isIntegerTy(64)) return v;
        if (t->isIntegerTy(1)) return b.CreateZExt(v, b.getInt64Ty());
        if (t->isIntegerTy()) return b.CreateSExtOrTrunc(v, b.getInt64Ty());
        if (t->isPointerTy()) {
            auto ft = llvm::FunctionType::get(b.getInt64Ty(), { b.getPtrTy() }, false);
            auto fn = lowerer.module().getOrInsertFunction("ts_value_get_int", ft);
            return b.CreateCall(ft, fn.getCallee(), { v });
        }
        return llvm::ConstantInt::get(b.getInt64Ty(), 0);
    }

    // Coerce a value to double (float slot).
    llvm::Value* toF64(llvm::Value* v, HIRToLLVM& lowerer) {
        auto& b = lowerer.builder();
        if (!v) return llvm::ConstantFP::get(b.getDoubleTy(), 0.0);
        llvm::Type* t = v->getType();
        if (t->isDoubleTy()) return v;
        if (t->isIntegerTy(1)) return b.CreateUIToFP(v, b.getDoubleTy());
        if (t->isIntegerTy()) return b.CreateSIToFP(v, b.getDoubleTy());
        if (t->isPointerTy()) {
            auto ft = llvm::FunctionType::get(b.getDoubleTy(), { b.getPtrTy() }, false);
            auto fn = lowerer.module().getOrInsertFunction("ts_value_get_double", ft);
            return b.CreateCall(ft, fn.getCallee(), { v });
        }
        return llvm::ConstantFP::get(b.getDoubleTy(), 0.0);
    }
};

} // namespace ts::hir

namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createNativeArrayHandler() {
        return std::make_unique<NativeArrayHandler>();
    }
}
