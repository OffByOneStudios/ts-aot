#pragma once
//==============================================================================
// HIRToLLVM_Internal.h — PRIVATE shared header for the split HIRToLLVM_*.cpp
// files. NOT the public interface (that is HIRToLLVM.h). Carries the common
// include set plus the two file-local free helpers that were `static` in the
// original monolithic HIRToLLVM.cpp, now `inline` (one merged definition across
// TUs). emitSaturatingFPToSI in particular is used far from where it was defined
// (call-arg coercion), so a shared header is required once the file is split.
//==============================================================================

#include "HIRToLLVM.h"
#include "handlers/HandlerRegistry.h"

#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Intrinsics.h>

#include <spdlog/spdlog.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <limits>

#include <stdexcept>
#include <cassert>
#include <unordered_set>
#include <filesystem>

namespace ts::hir {

// Coerce a value to i64 for an i64 arithmetic op. Handles three off-type
// inputs that the SpecializationPass can hand us: NaN-boxed pointers (unbox),
// i1 bools (ZExt to i64; matches ToNumber semantics where false=0, true=1),
// and doubles (FPToSI). Anything else passes through unchanged.
inline llvm::Value* coerceToI64Operand(
    llvm::IRBuilder<>* builder,
    llvm::Value* val,
    llvm::FunctionCallee unboxFn,
    const char* unboxName)
{
    auto* ty = val->getType();
    if (ty->isPointerTy()) {
        return builder->CreateCall(unboxFn, {val}, unboxName);
    }
    if (ty->isIntegerTy(1)) {
        return builder->CreateZExt(val, builder->getInt64Ty(), "bool_to_i64");
    }
    if (ty->isDoubleTy()) {
        return builder->CreateFPToSI(val, builder->getInt64Ty(), "f64_to_i64");
    }
    return val;
}

// Saturating double -> signed-integer conversion for JS number ARGUMENTS to
// builtins (lengths, counts, indices). A bare fptosi is UB on NaN/Infinity and
// out-of-range values: depending on FP state it yields garbage that flows into
// allocation sizes / loop counts -> multi-GB allocs, OOM, or wrong results
// (e.g. `new ArrayBuffer(NaN)` allocated 256MB; `"x".repeat(NaN)` blew up).
// llvm.fptosi.sat gives NaN -> 0, +Inf -> INT_MAX, -Inf -> INT_MIN, and clamps
// out-of-range — i.e. ECMA ToIntegerOrInfinity clamped to the int width, with
// finite in-range values converting identically to the old fptosi.
inline llvm::Value* emitSaturatingFPToSI(
    llvm::IRBuilder<>* builder, llvm::Value* val, llvm::Type* intTy)
{
    return builder->CreateIntrinsic(llvm::Intrinsic::fptosi_sat,
                                    {intTy, val->getType()}, {val});
}

}  // namespace ts::hir
