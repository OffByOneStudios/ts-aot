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
               methodName == "getUnchecked" || methodName == "setUnchecked" ||
               methodName == "dispose" ||
               methodName == "copyFrom" || methodName == "toBuffer";
    }

    llvm::Value* lowerMethod(const std::string& methodName,
                             HIRInstruction* inst,
                             HIRToLLVM& lowerer) override {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // operands[0] = receiver, [1] = method name, [2..] = args
        llvm::Value* arr = lowerer.getOperandValue(inst->operands[0]);

        // Sized element descriptor from the receiver's elementType (bytes /
        // int-vs-float / signedness; legacy default is an 8-byte f64 slot).
        // A Class elementType is a STRUCT element (AoS) — handled by the
        // runtime memcpy helpers below.
        HIRToLLVM::NaElem e;
        uint32_t structShapeId = 0;
        bool isStructElem = false;
        if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
            if (*valPtr && (*valPtr)->type) {
                auto& et = (*valPtr)->type->elementType;
                if (et && et->kind == HIRTypeKind::Class) {
                    isStructElem = true;
                    structShapeId = et->shapeId;
                } else {
                    e = HIRToLLVM::naElemInfo(et);
                }
            }
        }

        if (isStructElem && (methodName == "get" || methodName == "getUnchecked")) {
            llvm::Value* idx = toI64(inst->operands.size() > 2
                                         ? lowerer.getOperandValue(inst->operands[2])
                                         : nullptr,
                                     lowerer);
            // Runtime call: bounds-checked inside (always aborts on OOB);
            // returns a fresh flat object of the struct's shape (value copy).
            auto ft = llvm::FunctionType::get(
                builder.getPtrTy(),
                { builder.getPtrTy(), builder.getInt64Ty(), builder.getInt32Ty() },
                false);
            auto fn = module.getOrInsertFunction("ts_native_array_get_struct", ft);
            return builder.CreateCall(ft, fn.getCallee(),
                { arr, idx,
                  llvm::ConstantInt::get(builder.getInt32Ty(), structShapeId) });
        }
        if (isStructElem && (methodName == "set" || methodName == "setUnchecked")) {
            llvm::Value* idx = toI64(inst->operands.size() > 2
                                         ? lowerer.getOperandValue(inst->operands[2])
                                         : nullptr,
                                     lowerer);
            llvm::Value* v = inst->operands.size() > 3
                                 ? lowerer.getOperandValue(inst->operands[3])
                                 : llvm::ConstantPointerNull::get(builder.getPtrTy());
            auto ft = llvm::FunctionType::get(
                builder.getVoidTy(),
                { builder.getPtrTy(), builder.getInt64Ty(), builder.getPtrTy() },
                false);
            auto fn = module.getOrInsertFunction("ts_native_array_set_struct", ft);
            builder.CreateCall(ft, fn.getCallee(), { arr, idx, v });
            return llvm::ConstantPointerNull::get(builder.getPtrTy());
        }

        if (methodName == "dispose") {
            auto ft = llvm::FunctionType::get(builder.getVoidTy(),
                                              { builder.getPtrTy() }, false);
            auto fn = module.getOrInsertFunction("ts_native_array_dispose", ft);
            builder.CreateCall(ft, fn.getCallee(), { arr });
            return llvm::ConstantPointerNull::get(builder.getPtrTy());
        }

        // Buffer bridge: bulk byte copy between GC Buffers and native memory
        // (one memcpy instead of a per-byte boxed loop).
        if (methodName == "copyFrom") {
            llvm::Value* buf = inst->operands.size() > 2
                                   ? lowerer.getOperandValue(inst->operands[2])
                                   : llvm::ConstantPointerNull::get(builder.getPtrTy());
            auto ft = llvm::FunctionType::get(
                builder.getInt64Ty(), { builder.getPtrTy(), builder.getPtrTy() }, false);
            auto fn = module.getOrInsertFunction("ts_native_array_copy_from_buffer", ft);
            llvm::Value* n = builder.CreateCall(ft, fn.getCallee(), { arr, buf });
            // Analyzer types copyFrom's result as number (Double).
            return builder.CreateSIToFP(n, builder.getDoubleTy());
        }
        if (methodName == "toBuffer") {
            auto ft = llvm::FunctionType::get(
                builder.getPtrTy(), { builder.getPtrTy() }, false);
            auto fn = module.getOrInsertFunction("ts_native_array_to_buffer", ft);
            return builder.CreateCall(ft, fn.getCallee(), { arr });
        }

        if (methodName == "get" || methodName == "getUnchecked") {
            bool unchecked = (methodName == "getUnchecked");
            llvm::Value* idx = toI64(inst->operands.size() > 2
                                         ? lowerer.getOperandValue(inst->operands[2])
                                         : nullptr,
                                     lowerer);
            if (lowerer.fastChecks() && !unchecked) {
                // Dev build: bounds/dispose-checked runtime call (sized slots
                // use the code-carrying accessors).
                if (e.bytes == 8) {
                    const char* rn = e.isInt ? "ts_native_array_get_i64"
                                             : "ts_native_array_get_f64";
                    llvm::Type* rt = e.isInt ? (llvm::Type*)builder.getInt64Ty()
                                             : (llvm::Type*)builder.getDoubleTy();
                    auto ft = llvm::FunctionType::get(
                        rt, { builder.getPtrTy(), builder.getInt64Ty() }, false);
                    auto fn = module.getOrInsertFunction(rn, ft);
                    return builder.CreateCall(ft, fn.getCallee(), { arr, idx });
                }
                int32_t code = (int32_t)e.bytes | (e.isUnsigned ? 0x100 : 0);
                const char* rn = e.isInt ? "ts_native_array_get_int"
                                         : "ts_native_array_get_fp";
                llvm::Type* rt = e.isInt ? (llvm::Type*)builder.getInt64Ty()
                                         : (llvm::Type*)builder.getDoubleTy();
                auto ft = llvm::FunctionType::get(
                    rt, { builder.getPtrTy(), builder.getInt64Ty(),
                          builder.getInt32Ty() }, false);
                auto fn = module.getOrInsertFunction(rn, ft);
                return builder.CreateCall(ft, fn.getCallee(),
                    { arr, idx, llvm::ConstantInt::get(builder.getInt32Ty(), code) });
            }
            // Default: inline sized load guarded by an inline bounds check.
            // getUnchecked is the IN-LANGUAGE unsafe opt-out (Rust
            // get_unchecked analog) — no check, in any build mode.
            if (!unchecked)
                lowerer.emitNativeArrayBoundsCheck(arr, idx);
            return lowerer.emitNativeArrayLoad(arr, idx, e);
        }

        if (methodName == "set" || methodName == "setUnchecked") {
            bool unchecked = (methodName == "setUnchecked");
            llvm::Value* idx = toI64(inst->operands.size() > 2
                                         ? lowerer.getOperandValue(inst->operands[2])
                                         : nullptr,
                                     lowerer);
            llvm::Value* raw = inst->operands.size() > 3
                                   ? lowerer.getOperandValue(inst->operands[3])
                                   : nullptr;
            llvm::Value* v = e.isInt ? toI64(raw, lowerer) : toF64(raw, lowerer);
            if (lowerer.fastChecks() && !unchecked) {
                if (e.bytes == 8) {
                    const char* rn = e.isInt ? "ts_native_array_set_i64"
                                             : "ts_native_array_set_f64";
                    llvm::Type* vt = e.isInt ? (llvm::Type*)builder.getInt64Ty()
                                             : (llvm::Type*)builder.getDoubleTy();
                    auto ft = llvm::FunctionType::get(
                        builder.getVoidTy(),
                        { builder.getPtrTy(), builder.getInt64Ty(), vt }, false);
                    auto fn = module.getOrInsertFunction(rn, ft);
                    builder.CreateCall(ft, fn.getCallee(), { arr, idx, v });
                    return llvm::ConstantPointerNull::get(builder.getPtrTy());
                }
                int32_t code = (int32_t)e.bytes | (e.isUnsigned ? 0x100 : 0);
                const char* rn = e.isInt ? "ts_native_array_set_int"
                                         : "ts_native_array_set_fp";
                llvm::Type* vt = e.isInt ? (llvm::Type*)builder.getInt64Ty()
                                         : (llvm::Type*)builder.getDoubleTy();
                auto ft = llvm::FunctionType::get(
                    builder.getVoidTy(),
                    { builder.getPtrTy(), builder.getInt64Ty(),
                      builder.getInt32Ty(), vt }, false);
                auto fn = module.getOrInsertFunction(rn, ft);
                builder.CreateCall(ft, fn.getCallee(),
                    { arr, idx, llvm::ConstantInt::get(builder.getInt32Ty(), code), v });
                return llvm::ConstantPointerNull::get(builder.getPtrTy());
            }
            // Default: inline sized store guarded by an inline bounds
            // check; setUnchecked skips it (in-language unsafe opt-out).
            if (!unchecked)
                lowerer.emitNativeArrayBoundsCheck(arr, idx);
            lowerer.emitNativeArrayStore(arr, idx, e, v);
            return llvm::ConstantPointerNull::get(builder.getPtrTy());
        }

        return nullptr;
    }

private:

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
