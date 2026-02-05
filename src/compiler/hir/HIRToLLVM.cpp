//==============================================================================
// HIRToLLVM.cpp - Lower HIR to LLVM IR
//==============================================================================

#include "HIRToLLVM.h"
#include "handlers/HandlerRegistry.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Intrinsics.h>

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace ts::hir {

//==============================================================================
// Constructor
//==============================================================================

HIRToLLVM::HIRToLLVM(llvm::LLVMContext& ctx)
    : context_(ctx)
    , builder_(std::make_unique<llvm::IRBuilder<>>(ctx))
{
}

//==============================================================================
// Main Entry Point
//==============================================================================

std::unique_ptr<llvm::Module> HIRToLLVM::lower(HIRModule* hirModule, const std::string& moduleName) {
    module_ = std::make_unique<llvm::Module>(moduleName, context_);

    // Initialize TsValue type
    initTsValueType();

    // Pre-create all global variables before lowering functions
    // This ensures each global is created exactly once with the correct name
    for (const auto& [name, type] : hirModule->globals) {
        getOrCreateGlobal(name, type);
    }

    // Forward-declare all functions first
    // This is necessary because functions may call each other before they are defined
    for (auto& fn : hirModule->functions) {
        forwardDeclareFunction(fn.get());
    }

    // Create VTable globals for all classes (even empty ones for instanceof)
    // VTable structure: { ParentVTable*, FunctionPtr1, FunctionPtr2, ... }
    // This must happen AFTER forward-declaring functions so we can get the correct function types
    for (auto& hirClass : hirModule->classes) {
        std::string vtableGlobalName = hirClass->name + "_VTable_Global";

        // Build vtable struct type - first entry is parent vtable, then function pointers
        std::vector<llvm::Type*> vtableFieldTypes;
        std::vector<llvm::Constant*> vtableFuncs;

        // First entry: parent vtable pointer (null if no base class)
        vtableFieldTypes.push_back(builder_->getPtrTy());  // Parent VTable
        if (hirClass->baseClass) {
            std::string baseVTableGlobalName = hirClass->baseClass->name + "_VTable_Global";
            // Get or create reference to base class vtable
            llvm::Constant* baseVTable = module_->getOrInsertGlobal(baseVTableGlobalName, builder_->getPtrTy());
            vtableFuncs.push_back(baseVTable);
        } else {
            vtableFuncs.push_back(llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }

        // Add function pointers for each method in vtable
        for (const auto& [methodName, methodFunc] : hirClass->vtable) {
            vtableFieldTypes.push_back(builder_->getPtrTy());

            if (methodFunc) {
                // Get the forward-declared function
                llvm::Function* llvmFunc = module_->getFunction(methodFunc->mangledName);
                if (llvmFunc) {
                    vtableFuncs.push_back(llvmFunc);
                } else {
                    // Function not found - use null pointer
                    vtableFuncs.push_back(llvm::ConstantPointerNull::get(builder_->getPtrTy()));
                    SPDLOG_WARN("VTable method {} not found for class {}", methodFunc->mangledName, hirClass->name);
                }
            } else {
                // Abstract method - use null pointer
                vtableFuncs.push_back(llvm::ConstantPointerNull::get(builder_->getPtrTy()));
            }
        }

        // Create vtable struct type (always has at least the parent pointer)
        llvm::StructType* vtableStruct = llvm::StructType::create(context_, vtableFieldTypes, hirClass->name + "_VTable");

        // Create vtable global
        llvm::Constant* vtableInit = llvm::ConstantStruct::get(vtableStruct, vtableFuncs);
        new llvm::GlobalVariable(*module_, vtableStruct, true, llvm::GlobalValue::ExternalLinkage, vtableInit, vtableGlobalName);

        SPDLOG_DEBUG("Created VTable global: {} with {} entries (+ parent ptr)", vtableGlobalName, hirClass->vtable.size());
    }

    // Lower all functions
    for (auto& fn : hirModule->functions) {
        lowerFunction(fn.get());
    }

    // Create main entry point
    createMainFunction();

    return std::move(module_);
}

void HIRToLLVM::createMainFunction() {
    // Look for the synthetic user_main first (created by Monomorphizer),
    // fall back to user-defined user_main (same pattern as legacy IRGenerator)
    llvm::Function* userMain = module_->getFunction("__synthetic_user_main");
    if (!userMain) {
        userMain = module_->getFunction("user_main");
    }

    if (!userMain) {
        SPDLOG_WARN("No __synthetic_user_main or user_main function found, skipping main entry point generation");
        return;
    }

    // Declare ts_main: int ts_main(int argc, char** argv, TsValue* (*user_main)(void*))
    std::vector<llvm::Type*> tsMainArgs = {
        llvm::Type::getInt32Ty(context_),    // argc
        builder_->getPtrTy(),                 // argv
        builder_->getPtrTy()                  // user_main function pointer
    };
    llvm::FunctionType* tsMainFt = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(context_), tsMainArgs, false);
    llvm::FunctionCallee tsMain = module_->getOrInsertFunction("ts_main", tsMainFt);

    // Define main: int main(int argc, char** argv)
    std::vector<llvm::Type*> mainArgs = {
        llvm::Type::getInt32Ty(context_),    // argc
        builder_->getPtrTy()                  // argv
    };
    llvm::FunctionType* mainFt = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(context_), mainArgs, false);
    llvm::Function* mainFn = llvm::Function::Create(
        mainFt, llvm::Function::ExternalLinkage, "main", module_.get());

    // Create entry block
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "entry", mainFn);
    builder_->SetInsertPoint(entryBB);

    // Get main arguments
    llvm::Value* argc = mainFn->getArg(0);
    llvm::Value* argv = mainFn->getArg(1);

    // Call ts_main(argc, argv, user_main)
    llvm::Value* result = builder_->CreateCall(
        tsMainFt, tsMain.getCallee(),
        { argc, argv, userMain }
    );

    // Return the result
    builder_->CreateRet(result);
}

//==============================================================================
// Type System
//==============================================================================

void HIRToLLVM::initTsValueType() {
    // TsValue struct: { i8 type, [7 x i8] padding, i64 value }
    // This matches the runtime's TsValue structure
    tsValueType_ = llvm::StructType::create(context_, "TsValue");
    tsValueType_->setBody({
        builder_->getInt8Ty(),                                  // type field
        llvm::ArrayType::get(builder_->getInt8Ty(), 7),        // padding
        builder_->getInt64Ty()                                  // union (i64 for int, bitcast for ptr/double)
    });
}

llvm::Type* HIRToLLVM::getLLVMType(const std::shared_ptr<HIRType>& type) {
    if (!type) return builder_->getPtrTy();
    return getLLVMType(type->kind);
}

llvm::Type* HIRToLLVM::getLLVMType(HIRTypeKind kind) {
    switch (kind) {
        case HIRTypeKind::Void:    return builder_->getVoidTy();
        case HIRTypeKind::Bool:    return builder_->getInt1Ty();
        case HIRTypeKind::Int64:   return builder_->getInt64Ty();
        case HIRTypeKind::Float64: return builder_->getDoubleTy();
        case HIRTypeKind::String:  return builder_->getPtrTy();  // TsString*
        case HIRTypeKind::Object:  return builder_->getPtrTy();  // TsObject*
        case HIRTypeKind::Array:   return builder_->getPtrTy();  // TsArray*
        case HIRTypeKind::Function: return builder_->getPtrTy(); // Function pointer
        case HIRTypeKind::Class:   return builder_->getPtrTy();  // Class instance
        case HIRTypeKind::Any:     return builder_->getPtrTy();  // TsValue*
        case HIRTypeKind::Ptr:     return builder_->getPtrTy();  // Raw pointer
        default: return builder_->getPtrTy();
    }
}

//==============================================================================
// Value Mapping
//==============================================================================

llvm::Value* HIRToLLVM::getValue(const std::shared_ptr<HIRValue>& hirValue) {
    if (!hirValue) return nullptr;

    // Handle global variables
    if (hirValue->isGlobal && !hirValue->globalName.empty()) {
        return getOrCreateGlobal(hirValue->globalName, hirValue->globalType);
    }

    auto it = valueMap_.find(hirValue->id);
    if (it != valueMap_.end()) {
        return it->second;
    }
    return nullptr;
}

llvm::GlobalVariable* HIRToLLVM::getOrCreateGlobal(const std::string& name, std::shared_ptr<HIRType> type) {
    // Check our map first for consistent lookup
    auto it = globalMap_.find(name);
    if (it != globalMap_.end()) {
        return it->second;
    }

    // Create the global variable
    llvm::Type* llvmType = getLLVMType(type);
    llvm::GlobalVariable* gv = new llvm::GlobalVariable(
        *module_,
        llvmType,
        false,  // Not constant
        llvm::GlobalValue::InternalLinkage,
        llvm::Constant::getNullValue(llvmType),
        name
    );

    // Cache it in our map
    globalMap_[name] = gv;
    return gv;
}

void HIRToLLVM::setValue(const std::shared_ptr<HIRValue>& hirValue, llvm::Value* llvmValue) {
    if (hirValue) {
        valueMap_[hirValue->id] = llvmValue;
    }
}

llvm::BasicBlock* HIRToLLVM::getBlock(HIRBlock* hirBlock) {
    if (!hirBlock) return nullptr;
    auto it = blockMap_.find(hirBlock->label);
    if (it != blockMap_.end()) {
        return it->second;
    }
    return nullptr;
}

//==============================================================================
// Function Lowering
//==============================================================================

void HIRToLLVM::forwardDeclareFunction(HIRFunction* fn) {
    // Skip if already declared
    if (module_->getFunction(fn->mangledName)) {
        return;
    }

    // Build function type
    // For async and generator functions, the return type is always ptr (Promise*/Generator*)
    llvm::Type* returnType = (fn->isAsync || fn->isGenerator) ? builder_->getPtrTy() : getLLVMType(fn->returnType);
    std::vector<llvm::Type*> paramTypes;

    // If this function has captures, add a hidden first parameter for the closure context
    if (!fn->captures.empty()) {
        paramTypes.push_back(builder_->getPtrTy());  // TsClosure* __closure
    }

    for (auto& param : fn->params) {
        paramTypes.push_back(getLLVMType(param.second));
    }

    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        fn->mangledName,
        module_.get()
    );
}

void HIRToLLVM::lowerFunction(HIRFunction* fn) {
    SPDLOG_INFO("Lowering function: {}", fn->mangledName);
    // Get the forward-declared function (or create it if not yet declared)
    llvm::Function* llvmFunc = module_->getFunction(fn->mangledName);
    if (!llvmFunc) {
        // Function wasn't forward-declared, create it now
        // For async and generator functions, the return type is always ptr (Promise*/Generator*)
        llvm::Type* returnType = (fn->isAsync || fn->isGenerator) ? builder_->getPtrTy() : getLLVMType(fn->returnType);
        std::vector<llvm::Type*> paramTypes;

        if (!fn->captures.empty()) {
            paramTypes.push_back(builder_->getPtrTy());  // TsClosure* __closure
        }

        for (auto& param : fn->params) {
            paramTypes.push_back(getLLVMType(param.second));
        }

        llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        llvmFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            fn->mangledName,
            module_.get()
        );
    }

    bool hasCaptureParams = !fn->captures.empty();

    // Set current function
    currentFunction_ = llvmFunc;
    currentHIRFunction_ = fn;
    isAsyncFunction_ = fn->isAsync;
    isGeneratorFunction_ = fn->isGenerator;
    valueMap_.clear();
    blockMap_.clear();
    closureParam_ = nullptr;
    asyncPromise_ = nullptr;
    generatorObject_ = nullptr;
    asyncContext_ = nullptr;
    currentYieldState_ = 0;
    yieldResumeBlocks_.clear();
    generatorDoneBlock_ = nullptr;
    generatorImplFunc_ = nullptr;

    // For async generator functions (both isAsync and isGenerator), create an AsyncGenerator
    if (fn->isAsync && fn->isGenerator) {
        // Create entry block for async generator setup
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "async_gen_entry", llvmFunc);
        builder_->SetInsertPoint(entryBB);

        // Create an AsyncGenerator: ts_async_generator_create() -> TsAsyncGenerator*
        llvm::FunctionType* createAsyncGenFt = llvm::FunctionType::get(
            builder_->getPtrTy(), {}, false);
        llvm::FunctionCallee createAsyncGenFn = module_->getOrInsertFunction(
            "ts_async_generator_create", createAsyncGenFt);
        generatorObject_ = builder_->CreateCall(createAsyncGenFt, createAsyncGenFn.getCallee(), {}, "async_generator");
        // Note: async generators don't use asyncPromise_ - they yield Promises directly
    }
    // For async functions (not generators), create a Promise
    else if (fn->isAsync) {
        // Create entry block for async setup
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "async_entry", llvmFunc);
        builder_->SetInsertPoint(entryBB);

        // Create a Promise: ts_promise_create() -> TsPromise*
        llvm::FunctionType* createPromiseFt = llvm::FunctionType::get(
            builder_->getPtrTy(), {}, false);
        llvm::FunctionCallee createPromiseFn = module_->getOrInsertFunction(
            "ts_promise_create", createPromiseFt);
        asyncPromise_ = builder_->CreateCall(createPromiseFt, createPromiseFn.getCallee(), {}, "promise");
    }
    // For generator functions (not async), create a state machine
    else if (fn->isGenerator) {
        // Reset generator state tracking
        currentYieldState_ = 0;
        yieldResumeBlocks_.clear();
        generatorDoneBlock_ = nullptr;
        generatorImplFunc_ = nullptr;
        asyncContext_ = nullptr;

        // First, count the number of yields to create resume blocks
        int yieldCount = 0;
        for (auto& block : fn->blocks) {
            for (auto& inst : block->instructions) {
                if (inst->opcode == HIROpcode::Yield) {
                    yieldCount++;
                }
            }
        }

        // Create the implementation function (state machine)
        // Signature: void impl(AsyncContext* ctx)
        std::string implName = fn->mangledName + "$impl";
        llvm::FunctionType* implFuncType = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { builder_->getPtrTy() },  // AsyncContext*
            false
        );
        generatorImplFunc_ = llvm::Function::Create(
            implFuncType,
            llvm::Function::InternalLinkage,
            implName,
            module_.get()
        );

        // Now set up the wrapper function (the original function)
        // It creates AsyncContext, sets resumeFn, creates Generator, and returns it
        llvm::BasicBlock* wrapperEntry = llvm::BasicBlock::Create(context_, "wrapper_entry", llvmFunc);
        builder_->SetInsertPoint(wrapperEntry);

        // Create AsyncContext
        llvm::FunctionType* createCtxFt = llvm::FunctionType::get(
            builder_->getPtrTy(), {}, false);
        llvm::FunctionCallee createCtxFn = module_->getOrInsertFunction(
            "ts_async_context_create", createCtxFt);
        llvm::Value* asyncCtx = builder_->CreateCall(createCtxFt, createCtxFn.getCallee(), {}, "async_ctx");

        // Set ctx->resumeFn = impl function
        // AsyncContext layout: { TsObject base (16 bytes), int state (4), bool error (1), bool yielded (1), 2 padding, TsValue yieldedValue (16), ... }
        // state is at offset 16, error at 20, yielded at 21, yieldedValue at 24, promise at 40, pendingNextPromise at 48, generator at 56, resumeFn at 64
        // Actually let's use ts_async_context_set_resume_fn(ctx, fn) for safety
        llvm::FunctionType* setResumeFt = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false
        );
        llvm::FunctionCallee setResumeFn = module_->getOrInsertFunction(
            "ts_async_context_set_resume_fn", setResumeFt);
        builder_->CreateCall(setResumeFt, setResumeFn.getCallee(), { asyncCtx, generatorImplFunc_ });

        // Create Generator
        llvm::FunctionType* createGeneratorFt = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee createGeneratorFn = module_->getOrInsertFunction(
            "ts_generator_create", createGeneratorFt);
        llvm::Value* generator = builder_->CreateCall(createGeneratorFt, createGeneratorFn.getCallee(), { asyncCtx }, "generator");

        // Return the generator immediately (don't execute the body)
        builder_->CreateRet(generator);

        // Now build the implementation function (state machine)
        currentFunction_ = generatorImplFunc_;
        llvm::Argument* ctxArg = generatorImplFunc_->getArg(0);
        ctxArg->setName("ctx");
        asyncContext_ = ctxArg;

        // Create entry block with state dispatch
        llvm::BasicBlock* implEntry = llvm::BasicBlock::Create(context_, "impl_entry", generatorImplFunc_);
        builder_->SetInsertPoint(implEntry);

        // Load state: ctx->state (offset 16 in AsyncContext after TsObject base)
        llvm::FunctionType* getStateFt = llvm::FunctionType::get(
            builder_->getInt32Ty(),
            { builder_->getPtrTy() },
            false
        );
        llvm::FunctionCallee getStateFn = module_->getOrInsertFunction(
            "ts_async_context_get_state", getStateFt);
        llvm::Value* state = builder_->CreateCall(getStateFt, getStateFn.getCallee(), { asyncContext_ }, "state");

        // Create blocks for each state
        // State 0 = initial (start of generator)
        // State 1..N = resume after yield 1..N
        // State N+1 = done (generator finished)

        // Create LLVM basic blocks for HIR blocks (these go in the impl function)
        for (auto& block : fn->blocks) {
            llvm::BasicBlock* bb = llvm::BasicBlock::Create(context_, block->label, generatorImplFunc_);
            blockMap_[block->label] = bb;
        }

        // Create the done block
        generatorDoneBlock_ = llvm::BasicBlock::Create(context_, "generator_done", generatorImplFunc_);

        // Create resume blocks for each yield point (state 1, 2, 3, ...)
        for (int i = 0; i < yieldCount; i++) {
            llvm::BasicBlock* resumeBlock = llvm::BasicBlock::Create(
                context_,
                "yield_resume_" + std::to_string(i + 1),
                generatorImplFunc_
            );
            yieldResumeBlocks_.push_back(resumeBlock);
        }

        // Create the state dispatch switch
        llvm::BasicBlock* firstHIRBlock = fn->blocks.empty() ? generatorDoneBlock_ : blockMap_[fn->blocks[0]->label];
        llvm::SwitchInst* stateSwitch = builder_->CreateSwitch(state, generatorDoneBlock_, yieldCount + 1);

        // State 0 -> start of generator (first HIR block)
        stateSwitch->addCase(builder_->getInt32(0), firstHIRBlock);

        // States 1..N -> resume after corresponding yield
        for (int i = 0; i < yieldCount; i++) {
            stateSwitch->addCase(builder_->getInt32(i + 1), yieldResumeBlocks_[i]);
        }

        // Build the done block - just return without setting yielded
        builder_->SetInsertPoint(generatorDoneBlock_);
        builder_->CreateRetVoid();

        // Map function parameters - but for generators, params are stored in ctx->data
        // For simplicity, we'll just skip parameter mapping for now
        // TODO: Store params in ctx->data when wrapper is called, load them in impl

        // Lower each HIR block in the impl function
        for (auto& block : fn->blocks) {
            lowerBlock(block.get());
        }

        // Restore state
        currentFunction_ = llvmFunc;
        asyncContext_ = nullptr;
        generatorImplFunc_ = nullptr;

        // Skip the normal block lowering below since we already did it
        currentFunction_ = nullptr;
        currentHIRFunction_ = nullptr;
        isAsyncFunction_ = false;
        isGeneratorFunction_ = false;
        closureParam_ = nullptr;
        asyncPromise_ = nullptr;
        generatorObject_ = nullptr;
        return;  // Exit early - we've handled everything for generators
    }

    // Create LLVM basic blocks for HIR blocks
    for (auto& block : fn->blocks) {
        llvm::BasicBlock* bb = llvm::BasicBlock::Create(context_, block->label, llvmFunc);
        blockMap_[block->label] = bb;
    }

    // For async functions, branch from entry to the first HIR block
    // (The entry block was created above based on the function type)
    if (fn->isAsync && !fn->blocks.empty()) {
        llvm::BasicBlock* firstBlock = blockMap_[fn->blocks[0]->label];
        builder_->CreateBr(firstBlock);
    }

    // Map function parameters to values
    auto argIt = llvmFunc->arg_begin();

    // If we have captures, the first argument is the closure context
    if (hasCaptureParams) {
        argIt->setName("__closure");
        closureParam_ = &*argIt;
        ++argIt;
    }

    for (size_t i = 0; i < fn->params.size(); ++i, ++argIt) {
        argIt->setName(fn->params[i].first);
        // Create a value mapping for the parameter
        // Parameters are represented as values with IDs starting from 0
        valueMap_[static_cast<uint32_t>(i)] = &*argIt;
    }

    // Lower each block
    for (auto& block : fn->blocks) {
        lowerBlock(block.get());
    }

    currentFunction_ = nullptr;
    currentHIRFunction_ = nullptr;
    isAsyncFunction_ = false;
    isGeneratorFunction_ = false;
    closureParam_ = nullptr;
    asyncPromise_ = nullptr;
    generatorObject_ = nullptr;
    asyncContext_ = nullptr;
    currentYieldState_ = 0;
    yieldResumeBlocks_.clear();
    generatorDoneBlock_ = nullptr;
    generatorImplFunc_ = nullptr;
}

void HIRToLLVM::lowerBlock(HIRBlock* block) {
    SPDLOG_INFO("  Lowering block: {}", block->label);
    llvm::BasicBlock* bb = getBlock(block);
    if (!bb) return;

    builder_->SetInsertPoint(bb);

    // Lower each instruction
    for (auto& inst : block->instructions) {
        SPDLOG_INFO("    Lowering instruction: opcode={}", static_cast<int>(inst->opcode));
        lowerInstruction(inst.get());
    }
}

void HIRToLLVM::lowerInstruction(HIRInstruction* inst) {
    switch (inst->opcode) {
        // Constants
        case HIROpcode::ConstInt:       lowerConstInt(inst); break;
        case HIROpcode::ConstFloat:     lowerConstFloat(inst); break;
        case HIROpcode::ConstBool:      lowerConstBool(inst); break;
        case HIROpcode::ConstString:    lowerConstString(inst); break;
        case HIROpcode::ConstNull:      lowerConstNull(inst); break;
        case HIROpcode::ConstUndefined: lowerConstUndefined(inst); break;

        // Integer arithmetic
        case HIROpcode::AddI64: lowerAddI64(inst); break;
        case HIROpcode::SubI64: lowerSubI64(inst); break;
        case HIROpcode::MulI64: lowerMulI64(inst); break;
        case HIROpcode::DivI64: lowerDivI64(inst); break;
        case HIROpcode::ModI64: lowerModI64(inst); break;
        case HIROpcode::NegI64: lowerNegI64(inst); break;

        // Float arithmetic
        case HIROpcode::AddF64: lowerAddF64(inst); break;
        case HIROpcode::SubF64: lowerSubF64(inst); break;
        case HIROpcode::MulF64: lowerMulF64(inst); break;
        case HIROpcode::DivF64: lowerDivF64(inst); break;
        case HIROpcode::ModF64: lowerModF64(inst); break;
        case HIROpcode::NegF64: lowerNegF64(inst); break;

        // String operations
        case HIROpcode::StringConcat: lowerStringConcat(inst); break;

        // Bitwise
        case HIROpcode::AndI64:  lowerAndI64(inst); break;
        case HIROpcode::OrI64:   lowerOrI64(inst); break;
        case HIROpcode::XorI64:  lowerXorI64(inst); break;
        case HIROpcode::ShlI64:  lowerShlI64(inst); break;
        case HIROpcode::ShrI64:  lowerShrI64(inst); break;
        case HIROpcode::UShrI64: lowerUShrI64(inst); break;
        case HIROpcode::NotI64:  lowerNotI64(inst); break;

        // Integer comparisons
        case HIROpcode::CmpEqI64: lowerCmpEqI64(inst); break;
        case HIROpcode::CmpNeI64: lowerCmpNeI64(inst); break;
        case HIROpcode::CmpLtI64: lowerCmpLtI64(inst); break;
        case HIROpcode::CmpLeI64: lowerCmpLeI64(inst); break;
        case HIROpcode::CmpGtI64: lowerCmpGtI64(inst); break;
        case HIROpcode::CmpGeI64: lowerCmpGeI64(inst); break;

        // Float comparisons
        case HIROpcode::CmpEqF64: lowerCmpEqF64(inst); break;
        case HIROpcode::CmpNeF64: lowerCmpNeF64(inst); break;
        case HIROpcode::CmpLtF64: lowerCmpLtF64(inst); break;
        case HIROpcode::CmpLeF64: lowerCmpLeF64(inst); break;
        case HIROpcode::CmpGtF64: lowerCmpGtF64(inst); break;
        case HIROpcode::CmpGeF64: lowerCmpGeF64(inst); break;

        // Pointer comparisons
        case HIROpcode::CmpEqPtr: lowerCmpEqPtr(inst); break;
        case HIROpcode::CmpNePtr: lowerCmpNePtr(inst); break;

        // Boolean operations
        case HIROpcode::LogicalAnd: lowerLogicalAnd(inst); break;
        case HIROpcode::LogicalOr:  lowerLogicalOr(inst); break;
        case HIROpcode::LogicalNot: lowerLogicalNot(inst); break;

        // Type conversions
        case HIROpcode::CastI64ToF64:  lowerCastI64ToF64(inst); break;
        case HIROpcode::CastF64ToI64:  lowerCastF64ToI64(inst); break;
        case HIROpcode::CastBoolToI64: lowerCastBoolToI64(inst); break;

        // Boxing
        case HIROpcode::BoxInt:    lowerBoxInt(inst); break;
        case HIROpcode::BoxFloat:  lowerBoxFloat(inst); break;
        case HIROpcode::BoxBool:   lowerBoxBool(inst); break;
        case HIROpcode::BoxString: lowerBoxString(inst); break;
        case HIROpcode::BoxObject: lowerBoxObject(inst); break;

        // Unboxing
        case HIROpcode::UnboxInt:    lowerUnboxInt(inst); break;
        case HIROpcode::UnboxFloat:  lowerUnboxFloat(inst); break;
        case HIROpcode::UnboxBool:   lowerUnboxBool(inst); break;
        case HIROpcode::UnboxString: lowerUnboxString(inst); break;
        case HIROpcode::UnboxObject: lowerUnboxObject(inst); break;

        // Type checking
        case HIROpcode::TypeCheck:  lowerTypeCheck(inst); break;
        case HIROpcode::TypeOf:     lowerTypeOf(inst); break;
        case HIROpcode::InstanceOf: lowerInstanceOf(inst); break;

        // GC operations
        case HIROpcode::GCAlloc:       lowerGCAlloc(inst); break;
        case HIROpcode::GCAllocArray:  lowerGCAllocArray(inst); break;
        case HIROpcode::GCStore:       lowerGCStore(inst); break;
        case HIROpcode::GCLoad:        lowerGCLoad(inst); break;
        case HIROpcode::Safepoint:     lowerSafepoint(inst); break;
        case HIROpcode::SafepointPoll: lowerSafepointPoll(inst); break;

        // Memory operations
        case HIROpcode::Alloca:        lowerAlloca(inst); break;
        case HIROpcode::Load:          lowerLoad(inst); break;
        case HIROpcode::Store:         lowerStore(inst); break;
        case HIROpcode::GetElementPtr: lowerGetElementPtr(inst); break;

        // Object operations
        case HIROpcode::NewObject:        lowerNewObject(inst); break;
        case HIROpcode::NewObjectDynamic: lowerNewObjectDynamic(inst); break;
        case HIROpcode::GetPropStatic:    lowerGetPropStatic(inst); break;
        case HIROpcode::GetPropDynamic:   lowerGetPropDynamic(inst); break;
        case HIROpcode::SetPropStatic:    lowerSetPropStatic(inst); break;
        case HIROpcode::SetPropDynamic:   lowerSetPropDynamic(inst); break;
        case HIROpcode::HasProp:          lowerHasProp(inst); break;
        case HIROpcode::DeleteProp:       lowerDeleteProp(inst); break;

        // Array operations
        case HIROpcode::NewArrayBoxed:  lowerNewArrayBoxed(inst); break;
        case HIROpcode::NewArrayTyped:  lowerNewArrayTyped(inst); break;
        case HIROpcode::GetElem:        lowerGetElem(inst); break;
        case HIROpcode::SetElem:        lowerSetElem(inst); break;
        case HIROpcode::GetElemTyped:   lowerGetElemTyped(inst); break;
        case HIROpcode::SetElemTyped:   lowerSetElemTyped(inst); break;
        case HIROpcode::ArrayLength:    lowerArrayLength(inst); break;
        case HIROpcode::ArrayPush:      lowerArrayPush(inst); break;

        // Calls
        case HIROpcode::Call:         lowerCall(inst); break;
        case HIROpcode::CallMethod:   lowerCallMethod(inst); break;
        case HIROpcode::CallVirtual:  lowerCallVirtual(inst); break;
        case HIROpcode::CallIndirect: lowerCallIndirect(inst); break;

        // Globals
        case HIROpcode::LoadGlobal:   lowerLoadGlobal(inst); break;
        case HIROpcode::LoadFunction: lowerLoadFunction(inst); break;

        // Closures
        case HIROpcode::MakeClosure:   lowerMakeClosure(inst); break;
        case HIROpcode::LoadCapture:   lowerLoadCapture(inst); break;
        case HIROpcode::StoreCapture:  lowerStoreCapture(inst); break;
        case HIROpcode::LoadCaptureFromClosure:  lowerLoadCaptureFromClosure(inst); break;
        case HIROpcode::StoreCaptureFromClosure: lowerStoreCaptureFromClosure(inst); break;

        // Control flow
        case HIROpcode::Branch:      lowerBranch(inst); break;
        case HIROpcode::CondBranch:  lowerCondBranch(inst); break;
        case HIROpcode::Switch:      lowerSwitch(inst); break;
        case HIROpcode::Return:      lowerReturn(inst); break;
        case HIROpcode::ReturnVoid:  lowerReturnVoid(inst); break;
        case HIROpcode::Unreachable: lowerUnreachable(inst); break;

        // Phi and Select
        case HIROpcode::Phi:    lowerPhi(inst); break;
        case HIROpcode::Select: lowerSelect(inst); break;
        case HIROpcode::Copy:   lowerCopy(inst); break;

        // Exception handling
        case HIROpcode::SetupTry:       lowerSetupTry(inst); break;
        case HIROpcode::Throw:          lowerThrow(inst); break;
        case HIROpcode::GetException:   lowerGetException(inst); break;
        case HIROpcode::ClearException: lowerClearException(inst); break;
        case HIROpcode::PopHandler:     lowerPopHandler(inst); break;

        // Async/Await
        case HIROpcode::Await:          lowerAwait(inst); break;
        case HIROpcode::AsyncReturn:    lowerAsyncReturn(inst); break;

        // Generator/Yield
        case HIROpcode::Yield:          lowerYield(inst); break;
        case HIROpcode::YieldStar:      lowerYieldStar(inst); break;

        // Checked arithmetic (with overflow detection)
        case HIROpcode::AddI64Checked: lowerAddI64Checked(inst); break;
        case HIROpcode::SubI64Checked: lowerSubI64Checked(inst); break;
        case HIROpcode::MulI64Checked: lowerMulI64Checked(inst); break;

        default:
            SPDLOG_ERROR("Unknown HIR opcode: {}", static_cast<int>(inst->opcode));
            break;
    }
}

//==============================================================================
// Constant Instructions
//==============================================================================

void HIRToLLVM::lowerConstInt(HIRInstruction* inst) {
    int64_t value = getOperandInt(inst->operands[0]);
    llvm::Value* result = llvm::ConstantInt::get(builder_->getInt64Ty(), value);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstFloat(HIRInstruction* inst) {
    double value = std::get<double>(inst->operands[0]);
    llvm::Value* result = llvm::ConstantFP::get(builder_->getDoubleTy(), value);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstBool(HIRInstruction* inst) {
    bool value = std::get<bool>(inst->operands[0]);
    llvm::Value* result = llvm::ConstantInt::get(builder_->getInt1Ty(), value ? 1 : 0);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstString(HIRInstruction* inst) {
    std::string value = getOperandString(inst->operands[0]);

    // Create global string constant
    llvm::Value* strPtr = createGlobalString(value);

    // Call ts_string_create to create TsString*
    auto fn = getTsStringCreate();
    llvm::Value* result = builder_->CreateCall(fn, {strPtr});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstNull(HIRInstruction* inst) {
    llvm::Value* result = llvm::ConstantPointerNull::get(builder_->getPtrTy());
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstUndefined(HIRInstruction* inst) {
    // Create undefined value as null pointer (or call ts_undefined_value)
    llvm::Value* result = llvm::ConstantPointerNull::get(builder_->getPtrTy());
    setValue(inst->result, result);
}

//==============================================================================
// Integer Arithmetic
//==============================================================================

void HIRToLLVM::lowerAddI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Handle boxed values (pointers) by unboxing to i64
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    llvm::Value* result = builder_->CreateAdd(lhs, rhs, "add");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerSubI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Handle boxed values (pointers) by unboxing to i64
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    llvm::Value* result = builder_->CreateSub(lhs, rhs, "sub");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerMulI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Handle boxed values (pointers) by unboxing to i64
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    llvm::Value* result = builder_->CreateMul(lhs, rhs, "mul");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerDivI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Handle boxed values (pointers) by unboxing to i64
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    llvm::Value* result = builder_->CreateSDiv(lhs, rhs, "div");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerModI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Handle boxed values (pointers) by unboxing to i64
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    llvm::Value* result = builder_->CreateSRem(lhs, rhs, "mod");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerNegI64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);

    // Handle boxed values (pointers) by unboxing to i64
    if (val->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        val = builder_->CreateCall(unboxFn, {val}, "unbox_val");
    }

    llvm::Value* result = builder_->CreateNeg(val, "neg");
    setValue(inst->result, result);
}

//==============================================================================
// Checked Integer Arithmetic (with overflow detection)
//==============================================================================

void HIRToLLVM::lowerAddI64Checked(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    HIRBlock* overflowBlock = getOperandBlock(inst->operands[2]);

    // Use LLVM's signed add with overflow intrinsic
    llvm::Function* intrinsic = llvm::Intrinsic::getDeclaration(
        module_.get(), llvm::Intrinsic::sadd_with_overflow, {builder_->getInt64Ty()});
    llvm::Value* resultStruct = builder_->CreateCall(intrinsic, {lhs, rhs}, "add_overflow");

    // Extract result and overflow flag
    llvm::Value* result = builder_->CreateExtractValue(resultStruct, 0, "add_result");
    llvm::Value* overflow = builder_->CreateExtractValue(resultStruct, 1, "add_overflow_flag");

    setValue(inst->result, result);

    // Create continuation block for the non-overflow case
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(
        context_, "add_no_overflow", currentFunction_);

    // Branch to overflow block if overflow occurred
    builder_->CreateCondBr(overflow, getBlock(overflowBlock), continueBB);
    builder_->SetInsertPoint(continueBB);
}

void HIRToLLVM::lowerSubI64Checked(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    HIRBlock* overflowBlock = getOperandBlock(inst->operands[2]);

    // Use LLVM's signed sub with overflow intrinsic
    llvm::Function* intrinsic = llvm::Intrinsic::getDeclaration(
        module_.get(), llvm::Intrinsic::ssub_with_overflow, {builder_->getInt64Ty()});
    llvm::Value* resultStruct = builder_->CreateCall(intrinsic, {lhs, rhs}, "sub_overflow");

    // Extract result and overflow flag
    llvm::Value* result = builder_->CreateExtractValue(resultStruct, 0, "sub_result");
    llvm::Value* overflow = builder_->CreateExtractValue(resultStruct, 1, "sub_overflow_flag");

    setValue(inst->result, result);

    // Create continuation block for the non-overflow case
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(
        context_, "sub_no_overflow", currentFunction_);

    // Branch to overflow block if overflow occurred
    builder_->CreateCondBr(overflow, getBlock(overflowBlock), continueBB);
    builder_->SetInsertPoint(continueBB);
}

void HIRToLLVM::lowerMulI64Checked(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    HIRBlock* overflowBlock = getOperandBlock(inst->operands[2]);

    // Use LLVM's signed mul with overflow intrinsic
    llvm::Function* intrinsic = llvm::Intrinsic::getDeclaration(
        module_.get(), llvm::Intrinsic::smul_with_overflow, {builder_->getInt64Ty()});
    llvm::Value* resultStruct = builder_->CreateCall(intrinsic, {lhs, rhs}, "mul_overflow");

    // Extract result and overflow flag
    llvm::Value* result = builder_->CreateExtractValue(resultStruct, 0, "mul_result");
    llvm::Value* overflow = builder_->CreateExtractValue(resultStruct, 1, "mul_overflow_flag");

    setValue(inst->result, result);

    // Create continuation block for the non-overflow case
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(
        context_, "mul_no_overflow", currentFunction_);

    // Branch to overflow block if overflow occurred
    builder_->CreateCondBr(overflow, getBlock(overflowBlock), continueBB);
    builder_->SetInsertPoint(continueBB);
}

//==============================================================================
// Float Arithmetic
//==============================================================================

void HIRToLLVM::lowerAddF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert operands to double if they're integers or pointers (boxed values)
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFAdd(lhs, rhs, "fadd");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerSubF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert operands to double if they're integers or pointers (boxed values)
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFSub(lhs, rhs, "fsub");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerMulF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert operands to double if they're integers or pointers (boxed values)
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFMul(lhs, rhs, "fmul");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerDivF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert operands to double if they're integers or pointers (boxed values)
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFDiv(lhs, rhs, "fdiv");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerModF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert operands to double if they're integers or pointers (boxed values)
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFRem(lhs, rhs, "fmod");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerNegF64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);

    // Convert to double if integer or pointer (boxed value)
    if (val->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        val = builder_->CreateCall(unboxFn, {val});
    } else if (val->getType()->isIntegerTy()) {
        val = builder_->CreateSIToFP(val, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFNeg(val, "fneg");
    setValue(inst->result, result);
}

//==============================================================================
// String Operations
//==============================================================================

void HIRToLLVM::lowerStringConcat(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Get HIR types to determine if conversion is needed
    std::shared_ptr<HIRType> lhsType = nullptr;
    std::shared_ptr<HIRType> rhsType = nullptr;

    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*hirVal) lhsType = (*hirVal)->type;
    }
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[1])) {
        if (*hirVal) rhsType = (*hirVal)->type;
    }

    // Helper lambda to convert value to string based on type
    auto convertToString = [&](llvm::Value* val, std::shared_ptr<HIRType> type) -> llvm::Value* {
        // For pointer types (String, Any, Object, Class), always call ts_value_get_string
        // which handles both TsValue* (boxed) and raw TsString* pointers
        if (val->getType()->isPointerTy()) {
            if (!type || type->kind == HIRTypeKind::String ||
                type->kind == HIRTypeKind::Any || type->kind == HIRTypeKind::Object ||
                type->kind == HIRTypeKind::Class) {
                auto fn = getOrDeclareRuntimeFunction(
                    "ts_value_get_string",
                    builder_->getPtrTy(),
                    { builder_->getPtrTy() }
                );
                return builder_->CreateCall(fn, { val });
            }
        }

        if (!type || type->kind == HIRTypeKind::String) {
            return val; // Already a string (non-pointer type, shouldn't happen but handle it)
        }

        if (type->kind == HIRTypeKind::Float64 || val->getType()->isDoubleTy()) {
            // Convert double to string
            auto fn = getOrDeclareRuntimeFunction(
                "ts_double_to_string",
                builder_->getPtrTy(),
                { builder_->getDoubleTy(), builder_->getInt64Ty() }
            );
            return builder_->CreateCall(fn, { val, llvm::ConstantInt::get(builder_->getInt64Ty(), 10) });
        }

        if (type->kind == HIRTypeKind::Int64 || val->getType()->isIntegerTy(64)) {
            // Convert int to string
            auto fn = getOrDeclareRuntimeFunction(
                "ts_int_to_string",
                builder_->getPtrTy(),
                { builder_->getInt64Ty(), builder_->getInt64Ty() }
            );
            return builder_->CreateCall(fn, { val, llvm::ConstantInt::get(builder_->getInt64Ty(), 10) });
        }

        if (type->kind == HIRTypeKind::Bool || val->getType()->isIntegerTy(1)) {
            // Convert bool to string using select
            auto trueStr = createGlobalString("true");
            auto falseStr = createGlobalString("false");
            auto strFn = getTsStringCreate();
            llvm::Value* trueVal = builder_->CreateCall(strFn, {trueStr});
            llvm::Value* falseVal = builder_->CreateCall(strFn, {falseStr});
            llvm::Value* boolVal = val;
            if (val->getType()->isPointerTy()) {
                // Unbox the boolean from TsValue*
                auto unboxFn = getOrDeclareRuntimeFunction(
                    "ts_value_get_bool",
                    builder_->getInt1Ty(),
                    { builder_->getPtrTy() }
                );
                boolVal = builder_->CreateCall(unboxFn, { val }, "unbox_bool");
            } else if (val->getType()->isIntegerTy(64)) {
                boolVal = builder_->CreateICmpNE(val, llvm::ConstantInt::get(builder_->getInt64Ty(), 0));
            }
            return builder_->CreateSelect(boolVal, trueVal, falseVal);
        }

        // For any other types (Array, Function, etc.) - pass through
        // The pointer types (Any, Object, Class, String) are already handled above
        return val;
    };

    llvm::Value* lhsStr = convertToString(lhs, lhsType);
    llvm::Value* rhsStr = convertToString(rhs, rhsType);

    // Call ts_string_concat(void* a, void* b) -> void*
    auto fn = getOrDeclareRuntimeFunction(
        "ts_string_concat",
        builder_->getPtrTy(),
        { builder_->getPtrTy(), builder_->getPtrTy() }
    );
    llvm::Value* result = builder_->CreateCall(fn, { lhsStr, rhsStr }, "strcat");
    setValue(inst->result, result);
}

//==============================================================================
// Bitwise Operations
//==============================================================================

// Helper to ensure value is i64 for bitwise operations (convert from f64 or unbox if needed)
llvm::Value* HIRToLLVM::ensureI64ForBitwise(llvm::Value* val) {
    if (val->getType()->isDoubleTy()) {
        return builder_->CreateFPToSI(val, builder_->getInt64Ty(), "toi64");
    }
    if (val->getType()->isPointerTy()) {
        // Unbox pointer (TsValue*) to i64
        auto unboxFn = getTsValueGetInt();
        return builder_->CreateCall(unboxFn, {val}, "unbox_i64");
    }
    return val;
}

void HIRToLLVM::lowerAndI64(HIRInstruction* inst) {
    // JavaScript & semantics (ES5 11.10):
    // Both operands are converted to 32-bit integers, result is signed 32-bit

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "toi32_l");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "toi32_r");

    // AND on 32-bit values
    llvm::Value* result32 = builder_->CreateAnd(lhs32, rhs32, "and32");

    // Sign-extend to 64-bit
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerOrI64(HIRInstruction* inst) {
    // JavaScript | semantics (ES5 11.10):
    // Both operands are converted to 32-bit integers, result is signed 32-bit

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "toi32_l");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "toi32_r");

    // OR on 32-bit values
    llvm::Value* result32 = builder_->CreateOr(lhs32, rhs32, "or32");

    // Sign-extend to 64-bit
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerXorI64(HIRInstruction* inst) {
    // JavaScript ^ semantics (ES5 11.10):
    // Both operands are converted to 32-bit integers, result is signed 32-bit

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "toi32_l");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "toi32_r");

    // XOR on 32-bit values
    llvm::Value* result32 = builder_->CreateXor(lhs32, rhs32, "xor32");

    // Sign-extend to 64-bit
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerShlI64(HIRInstruction* inst) {
    // JavaScript << semantics (ES5 11.7.1):
    // 1. ToInt32(lhs) - truncate to 32 bits, treat as signed
    // 2. Shift amount is rhs & 0x1F (5 bits)
    // 3. Result is a signed 32-bit integer

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "toi32");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "shamt32");

    // Mask shift amount to 5 bits
    rhs32 = builder_->CreateAnd(rhs32,
        llvm::ConstantInt::get(builder_->getInt32Ty(), 0x1F), "shamt_masked");

    // Left shift on 32-bit value
    llvm::Value* result32 = builder_->CreateShl(lhs32, rhs32, "shl32");

    // Sign-extend to 64-bit (result is signed 32-bit)
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64 using signed conversion
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerShrI64(HIRInstruction* inst) {
    // JavaScript >> semantics (ES5 11.7.2):
    // 1. ToInt32(lhs) - truncate to 32 bits, treat as signed
    // 2. Shift amount is rhs & 0x1F (5 bits)
    // 3. Result is a signed 32-bit integer (arithmetic shift preserves sign)

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "toi32");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "shamt32");

    // Mask shift amount to 5 bits
    rhs32 = builder_->CreateAnd(rhs32,
        llvm::ConstantInt::get(builder_->getInt32Ty(), 0x1F), "shamt_masked");

    // Arithmetic (signed) shift right on 32-bit value
    llvm::Value* result32 = builder_->CreateAShr(lhs32, rhs32, "ashr32");

    // Sign-extend to 64-bit (preserves signed semantics)
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64 using signed conversion
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUShrI64(HIRInstruction* inst) {
    // JavaScript >>> semantics (ES5 11.7.3):
    // 1. ToUint32(lhs) - truncate to 32 bits, treat as unsigned
    // 2. Shift amount is rhs & 0x1F (5 bits)
    // 3. Result is an unsigned 32-bit integer (0 to 4294967295)

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToUint32 - the truncation gives us the low 32 bits)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "tou32");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "shamt32");

    // Mask shift amount to 5 bits (JS spec: shift by rhs & 0x1F)
    rhs32 = builder_->CreateAnd(rhs32,
        llvm::ConstantInt::get(builder_->getInt32Ty(), 0x1F), "shamt_masked");

    // Logical (unsigned) shift right on 32-bit value
    llvm::Value* result32 = builder_->CreateLShr(lhs32, rhs32, "lshr32");

    // Zero-extend to 64-bit (preserves unsigned semantics)
    llvm::Value* result64 = builder_->CreateZExt(result32, builder_->getInt64Ty(), "zext64");

    // Convert to f64 using UNSIGNED conversion (UIToFP) to get correct positive value
    llvm::Value* result = builder_->CreateUIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerNotI64(HIRInstruction* inst) {
    // JavaScript ~ semantics (ES5 11.4.8):
    // ToInt32(operand), then complement, result is signed 32-bit

    llvm::Value* val = ensureI64ForBitwise(getOperandValue(inst->operands[0]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* val32 = builder_->CreateTrunc(val, builder_->getInt32Ty(), "toi32");

    // Bitwise NOT on 32-bit value
    llvm::Value* result32 = builder_->CreateNot(val32, "not32");

    // Sign-extend to 64-bit
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

//==============================================================================
// Integer Comparisons
//==============================================================================

void HIRToLLVM::lowerCmpEqI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Check if this is a pointer comparison with null
    // Use LLVM-level check: if both are pointers and one is ConstantPointerNull
    bool lhsIsPointer = lhs->getType()->isPointerTy();
    bool rhsIsPointer = rhs->getType()->isPointerTy();
    bool lhsIsNull = llvm::isa<llvm::ConstantPointerNull>(lhs);
    bool rhsIsNull = llvm::isa<llvm::ConstantPointerNull>(rhs);

    if (lhsIsPointer && rhsIsPointer && (lhsIsNull || rhsIsNull)) {
        // Direct pointer comparison for obj === null or null === obj
        llvm::Value* result = builder_->CreateICmpEQ(lhs, rhs, "ptreq");
        setValue(inst->result, result);
        return;
    }

    // Also check HIR types for object/class comparisons (e.g., obj1 === obj2)
    std::shared_ptr<HIRType> lhsType = nullptr;
    std::shared_ptr<HIRType> rhsType = nullptr;
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*hirVal) lhsType = (*hirVal)->type;
    }
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[1])) {
        if (*hirVal) rhsType = (*hirVal)->type;
    }

    bool isObjectComparison = (lhsType && (lhsType->kind == HIRTypeKind::Class ||
                                           lhsType->kind == HIRTypeKind::Object ||
                                           lhsType->kind == HIRTypeKind::Array ||
                                           lhsType->kind == HIRTypeKind::Ptr)) ||
                              (rhsType && (rhsType->kind == HIRTypeKind::Class ||
                                           rhsType->kind == HIRTypeKind::Object ||
                                           rhsType->kind == HIRTypeKind::Array ||
                                           rhsType->kind == HIRTypeKind::Ptr));

    if (lhsIsPointer && rhsIsPointer && isObjectComparison) {
        // Direct pointer comparison for object === object checks
        llvm::Value* result = builder_->CreateICmpEQ(lhs, rhs, "ptreq");
        setValue(inst->result, result);
        return;
    }

    // Handle boxed values (pointers) by unboxing to i64
    if (lhsIsPointer) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhsIsPointer) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    // Handle type mismatch between i64 and i1 (boolean literal comparisons like x === true)
    if (lhs->getType()->isIntegerTy(64) && rhs->getType()->isIntegerTy(1)) {
        // Extend i1 to i64 for comparison
        rhs = builder_->CreateZExt(rhs, builder_->getInt64Ty(), "bool_to_i64");
    } else if (lhs->getType()->isIntegerTy(1) && rhs->getType()->isIntegerTy(64)) {
        // Extend i1 to i64 for comparison
        lhs = builder_->CreateZExt(lhs, builder_->getInt64Ty(), "bool_to_i64");
    }

    llvm::Value* result = builder_->CreateICmpEQ(lhs, rhs, "eq");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpNeI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Check if this is a pointer comparison with null
    // Use LLVM-level check: if both are pointers and one is ConstantPointerNull
    bool lhsIsPointer = lhs->getType()->isPointerTy();
    bool rhsIsPointer = rhs->getType()->isPointerTy();
    bool lhsIsNull = llvm::isa<llvm::ConstantPointerNull>(lhs);
    bool rhsIsNull = llvm::isa<llvm::ConstantPointerNull>(rhs);

    if (lhsIsPointer && rhsIsPointer && (lhsIsNull || rhsIsNull)) {
        // Direct pointer comparison for obj !== null or null !== obj
        llvm::Value* result = builder_->CreateICmpNE(lhs, rhs, "ptrne");
        setValue(inst->result, result);
        return;
    }

    // Also check HIR types for object/class comparisons (e.g., obj1 !== obj2)
    std::shared_ptr<HIRType> lhsType = nullptr;
    std::shared_ptr<HIRType> rhsType = nullptr;
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*hirVal) lhsType = (*hirVal)->type;
    }
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[1])) {
        if (*hirVal) rhsType = (*hirVal)->type;
    }

    bool isObjectComparison = (lhsType && (lhsType->kind == HIRTypeKind::Class ||
                                           lhsType->kind == HIRTypeKind::Object ||
                                           lhsType->kind == HIRTypeKind::Array ||
                                           lhsType->kind == HIRTypeKind::Ptr)) ||
                              (rhsType && (rhsType->kind == HIRTypeKind::Class ||
                                           rhsType->kind == HIRTypeKind::Object ||
                                           rhsType->kind == HIRTypeKind::Array ||
                                           rhsType->kind == HIRTypeKind::Ptr));

    if (lhsIsPointer && rhsIsPointer && isObjectComparison) {
        // Direct pointer comparison for object !== object checks
        llvm::Value* result = builder_->CreateICmpNE(lhs, rhs, "ptrne");
        setValue(inst->result, result);
        return;
    }

    // Handle boxed values (pointers) by unboxing to i64
    if (lhsIsPointer) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhsIsPointer) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    // Handle type mismatch between i64 and i1 (boolean literal comparisons like x !== true)
    if (lhs->getType()->isIntegerTy(64) && rhs->getType()->isIntegerTy(1)) {
        // Extend i1 to i64 for comparison
        rhs = builder_->CreateZExt(rhs, builder_->getInt64Ty(), "bool_to_i64");
    } else if (lhs->getType()->isIntegerTy(1) && rhs->getType()->isIntegerTy(64)) {
        // Extend i1 to i64 for comparison
        lhs = builder_->CreateZExt(lhs, builder_->getInt64Ty(), "bool_to_i64");
    }

    llvm::Value* result = builder_->CreateICmpNE(lhs, rhs, "ne");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLtI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Handle boxed values (pointers) by unboxing to i64
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    llvm::Value* result = builder_->CreateICmpSLT(lhs, rhs, "lt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLeI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Handle boxed values (pointers) by unboxing to i64
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    llvm::Value* result = builder_->CreateICmpSLE(lhs, rhs, "le");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGtI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Handle boxed values (pointers) by unboxing to i64
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    llvm::Value* result = builder_->CreateICmpSGT(lhs, rhs, "gt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGeI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Handle boxed values (pointers) by unboxing to i64
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetInt();
        rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
    }

    llvm::Value* result = builder_->CreateICmpSGE(lhs, rhs, "ge");
    setValue(inst->result, result);
}

//==============================================================================
// Float Comparisons
//==============================================================================

void HIRToLLVM::lowerCmpEqF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpOEQ(lhs, rhs, "feq");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpNeF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpONE(lhs, rhs, "fne");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLtF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpOLT(lhs, rhs, "flt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLeF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpOLE(lhs, rhs, "fle");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGtF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpOGT(lhs, rhs, "fgt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGeF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpOGE(lhs, rhs, "fge");
    setValue(inst->result, result);
}

//==============================================================================
// Pointer Comparisons
//==============================================================================

void HIRToLLVM::lowerCmpEqPtr(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateICmpEQ(lhs, rhs, "ptreq");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpNePtr(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateICmpNE(lhs, rhs, "ptrne");
    setValue(inst->result, result);
}

//==============================================================================
// Boolean Operations
//==============================================================================

void HIRToLLVM::lowerLogicalAnd(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert both operands to i1 (boolean) for logical AND
    auto convertToBool = [this](llvm::Value* val) -> llvm::Value* {
        if (val->getType()->isPointerTy()) {
            // For pointers (boxed values), call ts_value_to_bool
            llvm::FunctionType* toBoolFt = llvm::FunctionType::get(
                builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
            llvm::FunctionCallee toBoolFn = module_->getOrInsertFunction("ts_value_to_bool", toBoolFt);
            return builder_->CreateCall(toBoolFt, toBoolFn.getCallee(), { val }, "tobool");
        } else if (val->getType()->isIntegerTy(1)) {
            return val;  // Already boolean
        } else if (val->getType()->isIntegerTy()) {
            // Integer - convert to bool (non-zero check)
            return builder_->CreateICmpNE(val, llvm::ConstantInt::get(val->getType(), 0), "tobool");
        } else if (val->getType()->isDoubleTy()) {
            // Float - convert to bool (non-zero check)
            return builder_->CreateFCmpONE(val, llvm::ConstantFP::get(val->getType(), 0.0), "tobool");
        }
        return val;
    };

    lhs = convertToBool(lhs);
    rhs = convertToBool(rhs);

    llvm::Value* result = builder_->CreateAnd(lhs, rhs, "land");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerLogicalOr(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert both operands to i1 (boolean) for logical OR
    auto convertToBool = [this](llvm::Value* val) -> llvm::Value* {
        if (val->getType()->isPointerTy()) {
            // For pointers (boxed values), call ts_value_to_bool
            llvm::FunctionType* toBoolFt = llvm::FunctionType::get(
                builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
            llvm::FunctionCallee toBoolFn = module_->getOrInsertFunction("ts_value_to_bool", toBoolFt);
            return builder_->CreateCall(toBoolFt, toBoolFn.getCallee(), { val }, "tobool");
        } else if (val->getType()->isIntegerTy(1)) {
            return val;  // Already boolean
        } else if (val->getType()->isIntegerTy()) {
            // Integer - convert to bool (non-zero check)
            return builder_->CreateICmpNE(val, llvm::ConstantInt::get(val->getType(), 0), "tobool");
        } else if (val->getType()->isDoubleTy()) {
            // Float - convert to bool (non-zero check)
            return builder_->CreateFCmpONE(val, llvm::ConstantFP::get(val->getType(), 0.0), "tobool");
        }
        return val;
    };

    lhs = convertToBool(lhs);
    rhs = convertToBool(rhs);

    llvm::Value* result = builder_->CreateOr(lhs, rhs, "lor");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerLogicalNot(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);

    llvm::Value* result;
    if (val->getType()->isPointerTy()) {
        // For pointer types (boxed values or objects), we need to:
        // 1. Call ts_value_to_bool to convert to actual boolean
        // 2. Then negate the result
        llvm::FunctionType* toBoolFt = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee toBoolFn = module_->getOrInsertFunction("ts_value_to_bool", toBoolFt);
        llvm::Value* boolVal = builder_->CreateCall(toBoolFt, toBoolFn.getCallee(), { val }, "tobool");
        result = builder_->CreateNot(boolVal, "lnot");
    } else if (val->getType()->isIntegerTy(1)) {
        // Already a boolean - just negate
        result = builder_->CreateNot(val, "lnot");
    } else if (val->getType()->isIntegerTy()) {
        // Integer type - convert to boolean first (non-zero check)
        llvm::Value* boolVal = builder_->CreateICmpNE(
            val, llvm::ConstantInt::get(val->getType(), 0), "tobool");
        result = builder_->CreateNot(boolVal, "lnot");
    } else {
        // Fall back to CreateNot (may fail for unsupported types)
        result = builder_->CreateNot(val, "lnot");
    }

    setValue(inst->result, result);
}

//==============================================================================
// Type Conversions
//==============================================================================

void HIRToLLVM::lowerCastI64ToF64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = builder_->CreateSIToFP(val, builder_->getDoubleTy(), "i2f");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCastF64ToI64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = builder_->CreateFPToSI(val, builder_->getInt64Ty(), "f2i");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCastBoolToI64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = builder_->CreateZExt(val, builder_->getInt64Ty(), "b2i");
    setValue(inst->result, result);
}

//==============================================================================
// Boxing Operations
//==============================================================================

void HIRToLLVM::lowerBoxInt(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    auto fn = getTsValueMakeInt();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerBoxFloat(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    auto fn = getTsValueMakeDouble();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerBoxBool(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    auto fn = getTsValueMakeBool();
    // Extend bool to i32 for calling convention
    llvm::Value* extended = builder_->CreateZExt(val, builder_->getInt32Ty());
    llvm::Value* result = builder_->CreateCall(fn, {extended});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerBoxString(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    auto fn = getTsValueMakeString();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerBoxObject(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);

    llvm::Value* result;
    if (val->getType()->isPointerTy()) {
        // Actual pointer - use ts_value_make_object
        auto fn = getTsValueMakeObject();
        result = builder_->CreateCall(fn, {val});
    } else if (val->getType()->isIntegerTy(1)) {
        // Boolean - use ts_value_make_bool
        auto fn = getTsValueMakeBool();
        llvm::Value* extended = builder_->CreateZExt(val, builder_->getInt32Ty(), "bool_ext");
        result = builder_->CreateCall(fn, {extended});
    } else if (val->getType()->isIntegerTy(64)) {
        // Integer - use ts_value_make_int
        auto fn = getTsValueMakeInt();
        result = builder_->CreateCall(fn, {val});
    } else if (val->getType()->isDoubleTy()) {
        // Double - use ts_value_make_double
        auto fn = getTsValueMakeDouble();
        result = builder_->CreateCall(fn, {val});
    } else if (val->getType()->isIntegerTy(32)) {
        // i32 - extend to i64 and box as int
        auto fn = getTsValueMakeInt();
        llvm::Value* extended = builder_->CreateSExt(val, builder_->getInt64Ty(), "i32_ext");
        result = builder_->CreateCall(fn, {extended});
    } else {
        // Fall back to treating it as a pointer (may fail)
        auto fn = getTsValueMakeObject();
        result = builder_->CreateCall(fn, {val});
    }
    setValue(inst->result, result);
}

//==============================================================================
// Unboxing Operations
//==============================================================================

void HIRToLLVM::lowerUnboxInt(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    auto fn = getTsValueGetInt();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUnboxFloat(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    auto fn = getTsValueGetDouble();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUnboxBool(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    auto fn = getTsValueGetBool();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    // Truncate to i1
    result = builder_->CreateTrunc(result, builder_->getInt1Ty());
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUnboxString(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    auto fn = getTsValueGetString();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUnboxObject(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    auto fn = getTsValueGetObject();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

//==============================================================================
// Type Checking
//==============================================================================

void HIRToLLVM::lowerTypeCheck(HIRInstruction* inst) {
    // TODO: Implement type checking
    // For now, return true
    llvm::Value* result = llvm::ConstantInt::getTrue(context_);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerTypeOf(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);

    // ts_typeof expects a boxed TsValue*, so we need to box primitives
    if (val->getType()->isDoubleTy()) {
        auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
        auto boxFn = module_->getOrInsertFunction("ts_value_make_double", ft);
        val = builder_->CreateCall(ft, boxFn.getCallee(), { val });
    } else if (val->getType()->isIntegerTy(64)) {
        auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
        auto boxFn = module_->getOrInsertFunction("ts_value_make_int", ft);
        val = builder_->CreateCall(ft, boxFn.getCallee(), { val });
    } else if (val->getType()->isIntegerTy(1)) {
        auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt1Ty() }, false);
        auto boxFn = module_->getOrInsertFunction("ts_value_make_bool", ft);
        val = builder_->CreateCall(ft, boxFn.getCallee(), { val });
    }
    // If val is already a pointer, pass it directly

    auto fn = getTsTypeOf();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerInstanceOf(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* ctor = getOperandValue(inst->operands[1]);
    auto fn = getTsInstanceOf();
    llvm::Value* result = builder_->CreateCall(fn, {val, ctor});
    // Truncate to i1
    result = builder_->CreateTrunc(result, builder_->getInt1Ty());
    setValue(inst->result, result);
}

//==============================================================================
// GC Operations (Boehm GC Stubs)
//==============================================================================

void HIRToLLVM::lowerGCAlloc(HIRInstruction* inst) {
    // Get size from operand
    llvm::Value* size = getOperandValue(inst->operands[0]);

    // Call ts_alloc
    auto fn = getTsAlloc();
    llvm::Value* result = builder_->CreateCall(fn, {size});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerGCAllocArray(HIRInstruction* inst) {
    // For now, just use ts_alloc with computed size
    llvm::Value* elemSize = getOperandValue(inst->operands[0]);
    llvm::Value* length = getOperandValue(inst->operands[1]);
    llvm::Value* totalSize = builder_->CreateMul(elemSize, length, "arrsize");

    auto fn = getTsAlloc();
    llvm::Value* result = builder_->CreateCall(fn, {totalSize});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerGCStore(HIRInstruction* inst) {
    // With Boehm GC, no write barrier needed - just a plain store
    llvm::Value* ptr = getOperandValue(inst->operands[0]);
    llvm::Value* val = getOperandValue(inst->operands[1]);
    builder_->CreateStore(val, ptr);
}

void HIRToLLVM::lowerGCLoad(HIRInstruction* inst) {
    // With Boehm GC, no read barrier needed - just a plain load
    auto type = getOperandType(inst->operands[0]);
    llvm::Type* llvmType = getLLVMType(type);
    llvm::Value* ptr = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateLoad(llvmType, ptr, "gcload");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerSafepoint(HIRInstruction* inst) {
    // With Boehm GC, safepoints are no-ops
    // Future: could call GC_safe_point() for cooperative collection
}

void HIRToLLVM::lowerSafepointPoll(HIRInstruction* inst) {
    // With Boehm GC, safepoint polls are no-ops
}

//==============================================================================
// Memory Operations
//==============================================================================

void HIRToLLVM::lowerAlloca(HIRInstruction* inst) {
    auto type = getOperandType(inst->operands[0]);
    llvm::Type* llvmType = getLLVMType(type);

    // Emit alloca at the entry block for better optimization
    llvm::IRBuilder<>::InsertPointGuard guard(*builder_);
    llvm::BasicBlock* entryBB = &currentFunction_->getEntryBlock();
    builder_->SetInsertPoint(entryBB, entryBB->getFirstInsertionPt());

    llvm::AllocaInst* alloca = builder_->CreateAlloca(llvmType, nullptr, "local");
    setValue(inst->result, alloca);
}

void HIRToLLVM::lowerLoad(HIRInstruction* inst) {
    auto type = getOperandType(inst->operands[0]);
    llvm::Type* llvmType = getLLVMType(type);
    llvm::Value* ptr = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateLoad(llvmType, ptr, "load");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerStore(HIRInstruction* inst) {
    SPDLOG_INFO("      lowerStore: operands.size()={}", inst->operands.size());
    if (inst->operands.size() < 2) {
        SPDLOG_ERROR("      lowerStore: not enough operands");
        return;
    }
    llvm::Value* val = getOperandValue(inst->operands[0]);
    SPDLOG_INFO("      lowerStore: val={}", val ? "non-null" : "NULL");
    if (!val) {
        SPDLOG_ERROR("      lowerStore: value is null!");
        return;
    }
    llvm::Value* ptr = getOperandValue(inst->operands[1]);
    SPDLOG_INFO("      lowerStore: ptr={}", ptr ? "non-null" : "NULL");
    if (!ptr) {
        SPDLOG_ERROR("      lowerStore: ptr is null!");
        return;
    }

    // Get the expected type from the HIR operand (the type being stored)
    auto expectedType = inst->operands.size() > 2 ? getOperandType(inst->operands[2]) : nullptr;  // operands[2] is the element type
    if (expectedType) {
        llvm::Type* targetType = getLLVMType(expectedType);
        llvm::Type* valType = val->getType();

        // When storing to an Any-typed alloca (ptr), we need to box non-pointer values
        if (expectedType->kind == HIRTypeKind::Any && targetType->isPointerTy()) {
            if (valType->isIntegerTy(64)) {
                // Box integer
                auto fn = getTsValueMakeInt();
                val = builder_->CreateCall(fn, {val});
            } else if (valType->isDoubleTy()) {
                // Box double
                auto fn = getTsValueMakeDouble();
                val = builder_->CreateCall(fn, {val});
            } else if (valType->isIntegerTy(1)) {
                // Box boolean
                llvm::Value* i32Val = builder_->CreateZExt(val, builder_->getInt32Ty());
                auto fn = getTsValueMakeBool();
                val = builder_->CreateCall(fn, {i32Val});
            }
            // If val is already a pointer, no boxing needed
        }
        // When storing to a primitive-typed alloca but value is a pointer (e.g., from await),
        // we need to unbox the value first
        else if (valType->isPointerTy() && targetType->isDoubleTy()) {
            // Unbox: TsValue* -> double
            auto unboxFn = getOrDeclareRuntimeFunction("ts_value_get_double",
                builder_->getDoubleTy(), { builder_->getPtrTy() });
            val = builder_->CreateCall(unboxFn, { val }, "unboxed_double");
        }
        else if (valType->isPointerTy() && targetType->isIntegerTy(64)) {
            // Unbox: TsValue* -> int64
            auto unboxFn = getOrDeclareRuntimeFunction("ts_value_get_int",
                builder_->getInt64Ty(), { builder_->getPtrTy() });
            val = builder_->CreateCall(unboxFn, { val }, "unboxed_int");
        }
        else if (valType->isPointerTy() && targetType->isIntegerTy(1)) {
            // Unbox: TsValue* -> bool
            auto unboxFn = getOrDeclareRuntimeFunction("ts_value_get_bool",
                builder_->getInt1Ty(), { builder_->getPtrTy() });
            val = builder_->CreateCall(unboxFn, { val }, "unboxed_bool");
        }
        // Handle type coercion: i64 -> f64
        else if (valType->isIntegerTy(64) && targetType->isDoubleTy()) {
            val = builder_->CreateSIToFP(val, builder_->getDoubleTy(), "i64_to_f64");
        }
        // Handle type coercion: f64 -> i64
        else if (valType->isDoubleTy() && targetType->isIntegerTy(64)) {
            val = builder_->CreateFPToSI(val, builder_->getInt64Ty(), "f64_to_i64");
        }
    }

    builder_->CreateStore(val, ptr);
}

void HIRToLLVM::lowerGetElementPtr(HIRInstruction* inst) {
    // This is a simplified GEP - for arrays only
    llvm::Value* ptr = getOperandValue(inst->operands[0]);
    llvm::Value* idx = getOperandValue(inst->operands[1]);

    // Use byte-level GEP
    llvm::Value* result = builder_->CreateGEP(builder_->getInt8Ty(), ptr, idx, "gep");
    setValue(inst->result, result);
}

//==============================================================================
// Object Operations
//==============================================================================

void HIRToLLVM::lowerNewObject(HIRInstruction* inst) {
    // Get the class name from the operand
    std::string className;
    if (!inst->operands.empty()) {
        className = getOperandString(inst->operands[0]);
    }

    // Always create a TsMap-based object first (compatible with property access)
    auto createFn = getTsObjectCreate();
    llvm::Value* result = builder_->CreateCall(createFn, {});

    // Look up the vtable global for this class
    std::string vtableGlobalName = className + "_VTable_Global";
    llvm::GlobalVariable* vtableGlobal = module_->getGlobalVariable(vtableGlobalName);

    if (vtableGlobal) {
        // Store the TypeScript vtable at offset 8 of the TsMap (TsObject::vtable member)
        // TsObject layout: [C++ vtable (8 bytes), void* vtable, ...]
        // ts_instanceof reads from offset 8 to get TypeScript vtable
        llvm::Value* vtableSlot = builder_->CreateGEP(
            builder_->getInt8Ty(), result,
            llvm::ConstantInt::get(builder_->getInt64Ty(), 8));
        builder_->CreateStore(vtableGlobal, vtableSlot);
    }

    setValue(inst->result, result);
}

void HIRToLLVM::lowerNewObjectDynamic(HIRInstruction* inst) {
    // Same as NewObject for now
    lowerNewObject(inst);
}

void HIRToLLVM::lowerGetPropStatic(HIRInstruction* inst) {
    llvm::Value* obj = getOperandValue(inst->operands[0]);
    std::string propName = getOperandString(inst->operands[1]);

    // Check if the source value is already a boxed TsValue* (Any type)
    // If so, we should NOT box it again to avoid double-boxing
    bool alreadyBoxed = false;
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*hirVal && (*hirVal)->type && (*hirVal)->type->kind == HIRTypeKind::Any) {
            alreadyBoxed = true;
        }
    }

    // Box the object if it's not already a pointer-sized boxed value
    // ts_object_get_dynamic expects TsValue*, not raw TsMap*
    if (obj->getType()->isPointerTy() && !alreadyBoxed) {
        auto boxObjFn = getTsValueMakeObject();
        obj = builder_->CreateCall(boxObjFn, {obj});
    }

    // Create property key string
    llvm::Value* key = createGlobalString(propName);
    auto strFn = getTsStringCreate();
    llvm::Value* keyStr = builder_->CreateCall(strFn, {key});

    // Box the key
    auto boxFn = getTsValueMakeString();
    llvm::Value* keyBoxed = builder_->CreateCall(boxFn, {keyStr});

    auto fn = getTsObjectGetProperty();
    llvm::Value* result = builder_->CreateCall(fn, {obj, keyBoxed});

    // Unbox if the expected result type is a primitive
    // First try to get type from operands[2] (where createGetPropStatic stores it)
    // Fall back to inst->result->type if not available
    std::shared_ptr<HIRType> type = nullptr;
    if (inst->operands.size() > 2) {
        type = getOperandType(inst->operands[2]);
    }
    if (!type && inst->result && inst->result->type) {
        type = inst->result->type;
    }

    if (type) {
        if (type->kind == HIRTypeKind::Int64) {
            auto unboxFn = getTsValueGetInt();
            result = builder_->CreateCall(unboxFn, {result});
        } else if (type->kind == HIRTypeKind::Float64) {
            auto unboxFn = getTsValueGetDouble();
            result = builder_->CreateCall(unboxFn, {result});
        } else if (type->kind == HIRTypeKind::Bool) {
            auto unboxFn = getTsValueGetBool();
            result = builder_->CreateCall(unboxFn, {result});
        } else if (type->kind == HIRTypeKind::String) {
            auto unboxFn = getTsValueGetString();
            result = builder_->CreateCall(unboxFn, {result});
        }
    }

    setValue(inst->result, result);
}

void HIRToLLVM::lowerGetPropDynamic(HIRInstruction* inst) {
    llvm::Value* obj = getOperandValue(inst->operands[0]);
    llvm::Value* key = getOperandValue(inst->operands[1]);

    // Check if the source value is already a boxed TsValue* (Any type)
    // If so, we should NOT box it again to avoid double-boxing
    bool alreadyBoxed = false;
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*hirVal && (*hirVal)->type && (*hirVal)->type->kind == HIRTypeKind::Any) {
            alreadyBoxed = true;
        }
    }

    // Box the object if it's not already boxed
    if (obj->getType()->isPointerTy() && !alreadyBoxed) {
        auto boxObjFn = getTsValueMakeObject();
        obj = builder_->CreateCall(boxObjFn, {obj});
    }

    // Box the key - ts_object_get_dynamic expects a boxed TsValue* for the key
    // The key is a TsString* (from const.string), so box it as a string
    if (key->getType()->isPointerTy()) {
        auto boxKeyFn = getTsValueMakeString();
        key = builder_->CreateCall(boxKeyFn, {key});
    }

    auto fn = getTsObjectGetProperty();
    llvm::Value* result = builder_->CreateCall(fn, {obj, key});

    // Unbox if the expected result type is a primitive
    if (inst->result && inst->result->type) {
        auto type = inst->result->type;
        if (type->kind == HIRTypeKind::Int64) {
            auto unboxFn = getTsValueGetInt();
            result = builder_->CreateCall(unboxFn, {result});
        } else if (type->kind == HIRTypeKind::Float64) {
            auto unboxFn = getTsValueGetDouble();
            result = builder_->CreateCall(unboxFn, {result});
        } else if (type->kind == HIRTypeKind::Bool) {
            auto unboxFn = getTsValueGetBool();
            result = builder_->CreateCall(unboxFn, {result});
        } else if (type->kind == HIRTypeKind::String) {
            auto unboxFn = getTsValueGetString();
            result = builder_->CreateCall(unboxFn, {result});
        }
    }

    setValue(inst->result, result);
}

void HIRToLLVM::lowerSetPropStatic(HIRInstruction* inst) {
    llvm::Value* obj = getOperandValue(inst->operands[0]);
    std::string propName = getOperandString(inst->operands[1]);
    llvm::Value* val = getOperandValue(inst->operands[2]);

    // Box the object - ts_object_set_dynamic expects TsValue*, not raw TsMap*
    if (obj->getType()->isPointerTy()) {
        auto boxObjFn = getTsValueMakeObject();
        obj = builder_->CreateCall(boxObjFn, {obj});
    }

    // Create property key string
    llvm::Value* key = createGlobalString(propName);
    auto strFn = getTsStringCreate();
    llvm::Value* keyStr = builder_->CreateCall(strFn, {key});

    // Box the key
    auto boxFn = getTsValueMakeString();
    llvm::Value* keyBoxed = builder_->CreateCall(boxFn, {keyStr});

    // Box the value based on HIR type information
    if (!val->getType()->isPointerTy()) {
        // Primitive types
        if (val->getType()->isIntegerTy(64)) {
            auto fn = getTsValueMakeInt();
            val = builder_->CreateCall(fn, {val});
        } else if (val->getType()->isDoubleTy()) {
            auto fn = getTsValueMakeDouble();
            val = builder_->CreateCall(fn, {val});
        } else if (val->getType()->isIntegerTy(1)) {
            auto fn = getTsValueMakeBool();
            llvm::Value* extended = builder_->CreateZExt(val, builder_->getInt32Ty());
            val = builder_->CreateCall(fn, {extended});
        }
    } else {
        // Pointer types - use HIR type to determine boxing
        std::shared_ptr<HIRType> valHirType = nullptr;
        if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[2])) {
            if (*hirVal) {
                valHirType = (*hirVal)->type;
                SPDLOG_DEBUG("lowerSetPropStatic: prop={} valHirType->kind={}", propName, static_cast<int>(valHirType->kind));
            }
        } else {
            SPDLOG_DEBUG("lowerSetPropStatic: prop={} operand[2] is not an HIRValue", propName);
        }

        // Check if val is an LLVM function pointer - if so, always use function boxing
        // This is a fallback for when HIR type info is lost (e.g., after inlining)
        bool isLLVMFunction = llvm::isa<llvm::Function>(val);
        if (isLLVMFunction) {
            SPDLOG_DEBUG("lowerSetPropStatic: detected LLVM Function for prop={}, boxing as function", propName);
        }

        // Workaround: if prop starts with __getter_ or __setter_, always use function boxing
        if (propName.rfind("__getter_", 0) == 0 || propName.rfind("__setter_", 0) == 0) {
            SPDLOG_DEBUG("lowerSetPropStatic: forcing function boxing for getter/setter prop={}", propName);

            // Create a trampoline for getter/setter functions
            llvm::Value* funcToBox = val;
            if (llvm::Function* originalFunc = llvm::dyn_cast<llvm::Function>(val)) {
                llvm::Function* trampoline = getOrCreateTrampoline(originalFunc);
                if (trampoline && trampoline != originalFunc) {
                    funcToBox = trampoline;
                    SPDLOG_DEBUG("lowerSetPropStatic: created trampoline {} for getter/setter {}", trampoline->getName().str(), originalFunc->getName().str());
                }
            }

            auto fn = getTsValueMakeFunction();
            llvm::Value* nullContext = llvm::ConstantPointerNull::get(builder_->getPtrTy());
            val = builder_->CreateCall(fn, {funcToBox, nullContext});
        } else if (isLLVMFunction) {
            // Fallback: LLVM value is a function pointer, box it as a function with trampoline
            SPDLOG_DEBUG("lowerSetPropStatic: boxing LLVM function for prop={}", propName);

            // Create a trampoline that converts the function's calling convention
            llvm::Value* funcToBox = val;
            if (llvm::Function* originalFunc = llvm::dyn_cast<llvm::Function>(val)) {
                llvm::Function* trampoline = getOrCreateTrampoline(originalFunc);
                if (trampoline && trampoline != originalFunc) {
                    funcToBox = trampoline;
                    SPDLOG_DEBUG("lowerSetPropStatic: created trampoline {} for {}", trampoline->getName().str(), originalFunc->getName().str());
                }
            }

            auto fn = getTsValueMakeFunction();
            llvm::Value* nullContext = llvm::ConstantPointerNull::get(builder_->getPtrTy());
            val = builder_->CreateCall(fn, {funcToBox, nullContext});
        } else if (valHirType) {
            if (valHirType->kind == HIRTypeKind::String) {
                auto fn = getTsValueMakeString();
                val = builder_->CreateCall(fn, {val});
            } else if (valHirType->kind == HIRTypeKind::Function) {
                // Functions need to be wrapped in TsFunction with a trampoline for proper calling convention
                SPDLOG_DEBUG("lowerSetPropStatic: boxing function for prop={}", propName);

                // If val is a function, create a trampoline that converts its calling convention
                llvm::Value* funcToBox = val;
                if (llvm::Function* originalFunc = llvm::dyn_cast<llvm::Function>(val)) {
                    llvm::Function* trampoline = getOrCreateTrampoline(originalFunc);
                    if (trampoline && trampoline != originalFunc) {
                        funcToBox = trampoline;
                        SPDLOG_DEBUG("lowerSetPropStatic: created trampoline {} for {}", trampoline->getName().str(), originalFunc->getName().str());
                    }
                }

                auto fn = getTsValueMakeFunction();
                llvm::Value* nullContext = llvm::ConstantPointerNull::get(builder_->getPtrTy());
                val = builder_->CreateCall(fn, {funcToBox, nullContext});
            } else if (valHirType->kind == HIRTypeKind::Object ||
                       valHirType->kind == HIRTypeKind::Class ||
                       valHirType->kind == HIRTypeKind::Array) {
                // Objects, classes, and arrays need to be boxed as objects
                SPDLOG_DEBUG("lowerSetPropStatic: boxing object for prop={}", propName);
                auto fn = getTsValueMakeObject();
                val = builder_->CreateCall(fn, {val});
            } else if (valHirType->kind == HIRTypeKind::Ptr) {
                // Raw pointer - also box as object for safety
                SPDLOG_DEBUG("lowerSetPropStatic: boxing raw ptr for prop={}", propName);
                auto fn = getTsValueMakeObject();
                val = builder_->CreateCall(fn, {val});
            }
            // Any type is already boxed, no action needed
        } else {
            // Fallback: if no type info and property starts with __getter_ or __setter_, use function boxing
            if (propName.rfind("__getter_", 0) == 0 || propName.rfind("__setter_", 0) == 0) {
                SPDLOG_DEBUG("lowerSetPropStatic: no type info for prop={}, detected getter/setter, boxing as function", propName);

                // Create trampoline if val is a function
                llvm::Value* funcToBox = val;
                if (llvm::Function* originalFunc = llvm::dyn_cast<llvm::Function>(val)) {
                    llvm::Function* trampoline = getOrCreateTrampoline(originalFunc);
                    if (trampoline && trampoline != originalFunc) {
                        funcToBox = trampoline;
                    }
                }

                auto fn = getTsValueMakeFunction();
                llvm::Value* nullContext = llvm::ConstantPointerNull::get(builder_->getPtrTy());
                val = builder_->CreateCall(fn, {funcToBox, nullContext});
            } else {
                SPDLOG_DEBUG("lowerSetPropStatic: no type info for prop={}, boxing as object", propName);
                auto fn = getTsValueMakeObject();
                val = builder_->CreateCall(fn, {val});
            }
        }
    }

    auto fn = getTsObjectSetProperty();
    builder_->CreateCall(fn, {obj, keyBoxed, val});
}

void HIRToLLVM::lowerSetPropDynamic(HIRInstruction* inst) {
    llvm::Value* obj = getOperandValue(inst->operands[0]);
    llvm::Value* key = getOperandValue(inst->operands[1]);
    llvm::Value* val = getOperandValue(inst->operands[2]);

    // Box the object - ts_object_set_dynamic expects TsValue*, not raw TsMap*
    if (obj->getType()->isPointerTy()) {
        auto boxObjFn = getTsValueMakeObject();
        obj = builder_->CreateCall(boxObjFn, {obj});
    }

    // Box the value based on HIR type information
    if (!val->getType()->isPointerTy()) {
        // Primitive types
        if (val->getType()->isIntegerTy(64)) {
            auto fn = getTsValueMakeInt();
            val = builder_->CreateCall(fn, {val});
        } else if (val->getType()->isDoubleTy()) {
            auto fn = getTsValueMakeDouble();
            val = builder_->CreateCall(fn, {val});
        } else if (val->getType()->isIntegerTy(1)) {
            auto fn = getTsValueMakeBool();
            llvm::Value* extended = builder_->CreateZExt(val, builder_->getInt32Ty());
            val = builder_->CreateCall(fn, {extended});
        }
    } else {
        // Pointer types - use HIR type to determine boxing
        std::shared_ptr<HIRType> valHirType = nullptr;
        if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[2])) {
            if (*hirVal) {
                valHirType = (*hirVal)->type;
            }
        }

        if (valHirType) {
            if (valHirType->kind == HIRTypeKind::String) {
                auto fn = getTsValueMakeString();
                val = builder_->CreateCall(fn, {val});
            } else if (valHirType->kind == HIRTypeKind::Function) {
                // Functions need to be wrapped in TsFunction with a trampoline for proper calling convention
                SPDLOG_DEBUG("lowerSetPropDynamic: boxing function value");

                // If val is a function, create a trampoline that converts its calling convention
                llvm::Value* funcToBox = val;
                if (llvm::Function* originalFunc = llvm::dyn_cast<llvm::Function>(val)) {
                    llvm::Function* trampoline = getOrCreateTrampoline(originalFunc);
                    if (trampoline && trampoline != originalFunc) {
                        funcToBox = trampoline;
                        SPDLOG_DEBUG("lowerSetPropDynamic: created trampoline {} for {}", trampoline->getName().str(), originalFunc->getName().str());
                    }
                }

                auto fn = getTsValueMakeFunction();
                llvm::Value* nullContext = llvm::ConstantPointerNull::get(builder_->getPtrTy());
                val = builder_->CreateCall(fn, {funcToBox, nullContext});
            } else if (valHirType->kind == HIRTypeKind::Object ||
                       valHirType->kind == HIRTypeKind::Class ||
                       valHirType->kind == HIRTypeKind::Array) {
                // Objects, classes, and arrays need to be boxed as objects
                auto fn = getTsValueMakeObject();
                val = builder_->CreateCall(fn, {val});
            }
            // Any type is already boxed, no action needed
        }
    }

    auto fn = getTsObjectSetProperty();
    builder_->CreateCall(fn, {obj, key, val});
}

void HIRToLLVM::lowerHasProp(HIRInstruction* inst) {
    llvm::Value* obj = getOperandValue(inst->operands[0]);
    llvm::Value* key = getOperandValue(inst->operands[1]);

    auto fn = getTsObjectHasProperty();
    llvm::Value* result = builder_->CreateCall(fn, {obj, key});
    result = builder_->CreateTrunc(result, builder_->getInt1Ty());
    setValue(inst->result, result);
}

void HIRToLLVM::lowerDeleteProp(HIRInstruction* inst) {
    llvm::Value* obj = getOperandValue(inst->operands[0]);
    llvm::Value* key = getOperandValue(inst->operands[1]);

    auto fn = getTsObjectDeleteProperty();
    llvm::Value* result = builder_->CreateCall(fn, {obj, key});
    result = builder_->CreateTrunc(result, builder_->getInt1Ty());
    setValue(inst->result, result);
}

//==============================================================================
// Array Operations
//==============================================================================

void HIRToLLVM::lowerNewArrayBoxed(HIRInstruction* inst) {
    llvm::Value* len = getOperandValue(inst->operands[0]);
    auto fn = getTsArrayCreate();
    llvm::Value* result = builder_->CreateCall(fn, {len});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerNewArrayTyped(HIRInstruction* inst) {
    // For now, same as boxed array
    lowerNewArrayBoxed(inst);
}

void HIRToLLVM::lowerGetElem(HIRInstruction* inst) {
    llvm::Value* arr = getOperandValue(inst->operands[0]);
    llvm::Value* idx = getOperandValue(inst->operands[1]);

    llvm::Value* result;

    // Check if index is a string/pointer (dynamic property access) vs numeric (array index)
    if (idx->getType()->isPointerTy()) {
        // Dynamic property access: obj[stringKey] - call ts_object_get_dynamic
        auto ft = llvm::FunctionType::get(builder_->getPtrTy(),
                                          {builder_->getPtrTy(), builder_->getPtrTy()}, false);
        auto fn = module_->getOrInsertFunction("ts_object_get_dynamic", ft);
        result = builder_->CreateCall(ft, fn.getCallee(), {arr, idx});
    } else {
        // Array index access
        // Convert index to i64 if it's a double (numeric literal indices come through as f64)
        if (idx->getType()->isDoubleTy()) {
            idx = builder_->CreateFPToSI(idx, builder_->getInt64Ty(), "idx_to_i64");
        }

        auto fn = getTsArrayGet();
        result = builder_->CreateCall(fn, {arr, idx});
    }

    // If the expected result type is a primitive, unbox the value
    if (inst->result && inst->result->type) {
        auto& type = inst->result->type;
        if (type->kind == HIRTypeKind::Int64) {
            auto unboxFn = getTsValueGetInt();
            result = builder_->CreateCall(unboxFn, {result});
        } else if (type->kind == HIRTypeKind::Float64) {
            auto unboxFn = getTsValueGetDouble();
            result = builder_->CreateCall(unboxFn, {result});
        } else if (type->kind == HIRTypeKind::Bool) {
            auto unboxFn = getTsValueGetBool();
            result = builder_->CreateCall(unboxFn, {result});
        }
        // For Any, String, Object - leave as pointer
    }

    setValue(inst->result, result);
}

void HIRToLLVM::lowerSetElem(HIRInstruction* inst) {
    llvm::Value* arr = getOperandValue(inst->operands[0]);
    llvm::Value* idx = getOperandValue(inst->operands[1]);
    llvm::Value* val = getOperandValue(inst->operands[2]);

    // Box value if not a pointer (needed for both array and dynamic set)
    if (!val->getType()->isPointerTy()) {
        if (val->getType()->isIntegerTy(64)) {
            // Box integer
            auto fn = getTsValueMakeInt();
            val = builder_->CreateCall(fn, {val});
        } else if (val->getType()->isDoubleTy()) {
            // Box double
            auto fn = getTsValueMakeDouble();
            val = builder_->CreateCall(fn, {val});
        } else if (val->getType()->isIntegerTy(1)) {
            // Box boolean (convert i1 to i32 first)
            llvm::Value* i32Val = builder_->CreateZExt(val, builder_->getInt32Ty());
            auto fn = getTsValueMakeBool();
            val = builder_->CreateCall(fn, {i32Val});
        } else {
            // For other types, try to cast to ptr (may fail)
            SPDLOG_WARN("lowerSetElem: unexpected value type, attempting pointer cast");
            val = builder_->CreateIntToPtr(val, builder_->getPtrTy());
        }
    }

    // Check if index is a string/pointer (dynamic property access) vs numeric (array index)
    if (idx->getType()->isPointerTy()) {
        // Dynamic property set: obj[stringKey] = val - call ts_object_set_dynamic
        auto ft = llvm::FunctionType::get(builder_->getVoidTy(),
                                          {builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy()}, false);
        auto fn = module_->getOrInsertFunction("ts_object_set_dynamic", ft);
        builder_->CreateCall(ft, fn.getCallee(), {arr, idx, val});
    } else {
        // Array index set
        // Convert index to i64 if it's a double (numeric literal indices come through as f64)
        if (idx->getType()->isDoubleTy()) {
            idx = builder_->CreateFPToSI(idx, builder_->getInt64Ty(), "idx_to_i64");
        }

        auto fn = getTsArraySet();
        builder_->CreateCall(fn, {arr, idx, val});
    }
}

void HIRToLLVM::lowerGetElemTyped(HIRInstruction* inst) {
    // For now, same as boxed get
    lowerGetElem(inst);
}

void HIRToLLVM::lowerSetElemTyped(HIRInstruction* inst) {
    // For now, same as boxed set
    lowerSetElem(inst);
}

void HIRToLLVM::lowerArrayLength(HIRInstruction* inst) {
    llvm::Value* arr = getOperandValue(inst->operands[0]);

    auto fn = getTsArrayLength();
    llvm::Value* result = builder_->CreateCall(fn, {arr});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerArrayPush(HIRInstruction* inst) {
    llvm::Value* arr = getOperandValue(inst->operands[0]);
    llvm::Value* val = getOperandValue(inst->operands[1]);

    // ts_array_push expects (ptr, ptr), so we need to box the value if it's not a pointer
    if (!val->getType()->isPointerTy()) {
        if (val->getType()->isIntegerTy(64)) {
            auto boxFn = getTsValueMakeInt();
            val = builder_->CreateCall(boxFn, {val});
        } else if (val->getType()->isDoubleTy()) {
            auto boxFn = getTsValueMakeDouble();
            val = builder_->CreateCall(boxFn, {val});
        } else if (val->getType()->isIntegerTy(1)) {
            auto boxFn = getTsValueMakeBool();
            llvm::Value* extended = builder_->CreateZExt(val, builder_->getInt32Ty());
            val = builder_->CreateCall(boxFn, {extended});
        }
    }

    auto fn = getTsArrayPush();
    llvm::Value* result = builder_->CreateCall(fn, {arr, val});
    if (inst->result) {
        setValue(inst->result, result);
    }
}

//==============================================================================
// Call Operations
//==============================================================================

void HIRToLLVM::lowerCall(HIRInstruction* inst) {
    std::string funcName = getOperandString(inst->operands[0]);

    // Try handler registry first - this is the new modular approach
    if (auto* result = HandlerRegistry::instance().tryLower(funcName, inst, *this)) {
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle console functions specially - they need type-specific dispatch
    if (funcName == "ts_console_log" || funcName == "ts_console_error" ||
        funcName == "ts_console_warn" || funcName == "ts_console_info" ||
        funcName == "ts_console_debug") {

        bool isError = (funcName == "ts_console_error" || funcName == "ts_console_warn");

        // Handle each argument
        for (size_t i = 1; i < inst->operands.size(); ++i) {
            llvm::Value* arg = getOperandValue(inst->operands[i]);
            llvm::Type* argType = arg->getType();

            std::string actualFuncName;
            llvm::Type* paramType = builder_->getPtrTy();

            if (argType->isIntegerTy(64)) {
                actualFuncName = isError ? "ts_console_error_int" : "ts_console_log_int";
                paramType = builder_->getInt64Ty();
            } else if (argType->isDoubleTy()) {
                actualFuncName = isError ? "ts_console_error_double" : "ts_console_log_double";
                paramType = builder_->getDoubleTy();
            } else if (argType->isIntegerTy(1)) {
                actualFuncName = isError ? "ts_console_error_bool" : "ts_console_log_bool";
                paramType = builder_->getInt1Ty();
            } else if (argType->isPointerTy()) {
                actualFuncName = isError ? "ts_console_error_value" : "ts_console_log_value";
            } else {
                SPDLOG_WARN("Unknown argument type for console function");
                actualFuncName = isError ? "ts_console_error_value" : "ts_console_log_value";
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getVoidTy(), { paramType }, false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction(actualFuncName, ft);
            builder_->CreateCall(ft, fn.getCallee(), { arg });
        }

        // console functions return undefined
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
        return;
    }

    // Handle ts_value_to_bool - returns bool (i1), not ptr
    if (funcName == "ts_value_to_bool") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_to_bool", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_value_is_undefined - returns bool, not ptr
    if (funcName == "ts_value_is_undefined") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_is_undefined", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_value_is_nullish - returns bool, not ptr
    if (funcName == "ts_value_is_nullish") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_is_nullish", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_object_has_property - returns bool (i1), not ptr
    if (funcName == "ts_object_has_property") {
        llvm::Value* obj = getOperandValue(inst->operands[1]);
        llvm::Value* key = getOperandValue(inst->operands[2]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_object_has_property", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, key });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle string conversion functions - they take specific types, not ptr
    // If the argument is a pointer (boxed value), unbox it first
    if (funcName == "ts_string_from_int") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // If arg is a pointer (boxed value), unbox it to get the int
        if (arg->getType()->isPointerTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(
                builder_->getInt64Ty(), { builder_->getPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_int", unboxFt);
            arg = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { arg }, "unbox_int");
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_string_from_int", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_string_from_double") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // If arg is a pointer (boxed value), unbox it to get the double
        if (arg->getType()->isPointerTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(
                builder_->getDoubleTy(), { builder_->getPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_double", unboxFt);
            arg = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { arg }, "unbox_double");
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_string_from_double", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_string_from_bool") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // If arg is a pointer (boxed value), unbox it to get the bool
        if (arg->getType()->isPointerTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(
                builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFt);
            arg = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { arg }, "unbox_bool");
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getInt1Ty() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_string_from_bool", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_string_from_value") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_string_from_value", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle spread-related functions
    if (funcName == "ts_array_create") {
        // ts_array_create() - returns ptr, no args
        llvm::FunctionType* ft = llvm::FunctionType::get(builder_->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_create", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), {});
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_array_concat") {
        // ts_array_concat(void* arr, void* other) - returns new array
        // JavaScript concat() can take multiple arguments: arr.concat(a, b, c)
        // We chain calls: concat(concat(concat(arr, a), b), c)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_concat", ft);

        llvm::Value* result = getOperandValue(inst->operands[1]); // Start with the source array

        // Chain concat calls for each argument (operands[2], operands[3], ...)
        for (size_t i = 2; i < inst->operands.size(); ++i) {
            llvm::Value* other = getOperandValue(inst->operands[i]);
            result = builder_->CreateCall(ft, fn.getCallee(), { result, other });
        }

        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_array_push") {
        // ts_array_push(void* arr, void* value) - returns int64_t (new length)
        llvm::Value* arr = getOperandValue(inst->operands[1]);
        llvm::Value* val = getOperandValue(inst->operands[2]);

        // Box the value if needed
        if (!val->getType()->isPointerTy()) {
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = getTsValueMakeInt();
                val = builder_->CreateCall(boxFn, {val});
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = getTsValueMakeDouble();
                val = builder_->CreateCall(boxFn, {val});
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = getTsValueMakeBool();
                llvm::Value* extended = builder_->CreateZExt(val, builder_->getInt32Ty());
                val = builder_->CreateCall(boxFn, {extended});
            }
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt64Ty(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_push", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arr, val });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_object_assign") {
        // ts_object_assign(TsValue* target, TsValue* source) - returns TsValue*
        // Object.assign can have multiple sources: Object.assign(target, src1, src2, ...)
        // We need to call ts_object_assign for each source in sequence
        llvm::Value* target = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_object_assign", ft);

        // Loop through all sources (operands[2] onward)
        for (size_t i = 2; i < inst->operands.size(); ++i) {
            llvm::Value* source = getOperandValue(inst->operands[i]);
            target = builder_->CreateCall(ft, fn.getCallee(), { target, source });
        }

        if (inst->result) {
            setValue(inst->result, target);
        }
        return;
    }

    // Handle Math functions - they take and return double
    if (funcName == "ts_math_floor" || funcName == "ts_math_ceil" ||
        funcName == "ts_math_round" || funcName == "ts_math_trunc" ||
        funcName == "ts_math_abs" || funcName == "ts_math_sqrt" ||
        funcName == "ts_math_sin" || funcName == "ts_math_cos" ||
        funcName == "ts_math_tan" || funcName == "ts_math_log" ||
        funcName == "ts_math_exp" || funcName == "ts_math_sign" ||
        funcName == "ts_math_fround" || funcName == "ts_math_cbrt" ||
        funcName == "ts_math_sinh" || funcName == "ts_math_cosh" ||
        funcName == "ts_math_tanh" || funcName == "ts_math_asinh" ||
        funcName == "ts_math_acosh" || funcName == "ts_math_atanh" ||
        funcName == "ts_math_asin" || funcName == "ts_math_acos" ||
        funcName == "ts_math_atan" || funcName == "ts_math_expm1" ||
        funcName == "ts_math_log10" || funcName == "ts_math_log2" ||
        funcName == "ts_math_log1p") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // Convert i64 to double if needed (JS numbers are doubles)
        if (arg->getType()->isIntegerTy()) {
            arg = builder_->CreateSIToFP(arg, builder_->getDoubleTy(), "i64_to_f64");
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getDoubleTy(), { builder_->getDoubleTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Math.pow, atan2, hypot - takes two double arguments
    if (funcName == "ts_math_pow" || funcName == "ts_math_atan2" || funcName == "ts_math_hypot") {
        llvm::Value* base = getOperandValue(inst->operands[1]);
        llvm::Value* exp = getOperandValue(inst->operands[2]);
        // Convert i64 to double if needed
        if (base->getType()->isIntegerTy()) {
            base = builder_->CreateSIToFP(base, builder_->getDoubleTy(), "i64_to_f64");
        }
        if (exp->getType()->isIntegerTy()) {
            exp = builder_->CreateSIToFP(exp, builder_->getDoubleTy(), "i64_to_f64");
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getDoubleTy(), { builder_->getDoubleTy(), builder_->getDoubleTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { base, exp });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Math.min/max - take two double arguments
    if (funcName == "ts_math_min" || funcName == "ts_math_max") {
        llvm::Value* a = getOperandValue(inst->operands[1]);
        llvm::Value* b = getOperandValue(inst->operands[2]);
        // Convert i64 to double if needed
        if (a->getType()->isIntegerTy()) {
            a = builder_->CreateSIToFP(a, builder_->getDoubleTy(), "i64_to_f64");
        }
        if (b->getType()->isIntegerTy()) {
            b = builder_->CreateSIToFP(b, builder_->getDoubleTy(), "i64_to_f64");
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getDoubleTy(), { builder_->getDoubleTy(), builder_->getDoubleTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { a, b });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Math.random - takes no arguments
    if (funcName == "ts_math_random") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getDoubleTy(), {}, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), {});
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Math.clz32 - takes one i64 argument, returns i64
    if (funcName == "ts_math_clz32") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // Convert double to i64 if needed
        if (arg->getType()->isDoubleTy()) {
            arg = builder_->CreateFPToSI(arg, builder_->getInt64Ty(), "f64_to_i64");
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt64Ty(), { builder_->getInt64Ty() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Math.imul - takes two i64 arguments, returns i64
    if (funcName == "ts_math_imul") {
        llvm::Value* a = getOperandValue(inst->operands[1]);
        llvm::Value* b = getOperandValue(inst->operands[2]);
        // Convert double to i64 if needed
        if (a->getType()->isDoubleTy()) {
            a = builder_->CreateFPToSI(a, builder_->getInt64Ty(), "f64_to_i64");
        }
        if (b->getType()->isDoubleTy()) {
            b = builder_->CreateFPToSI(b, builder_->getInt64Ty(), "f64_to_i64");
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt64Ty(), { builder_->getInt64Ty(), builder_->getInt64Ty() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { a, b });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle setTimeout/setInterval - mangled names like setTimeout_any_dbl
    // Runtime function: ts_set_timeout(TsValue* callback, int64_t delay) -> TsValue*
    if (funcName.rfind("setTimeout", 0) == 0 || funcName.rfind("setInterval", 0) == 0) {
        bool isTimeout = funcName.rfind("setTimeout", 0) == 0;
        std::string runtimeFunc = isTimeout ? "ts_set_timeout" : "ts_set_interval";

        llvm::Value* callback = getOperandValue(inst->operands[1]);
        llvm::Value* delay = getOperandValue(inst->operands[2]);

        // Box callback if needed
        if (!callback->getType()->isPointerTy()) {
            if (callback->getType()->isIntegerTy(64)) {
                auto boxFn = getTsValueMakeInt();
                callback = builder_->CreateCall(boxFn, { callback });
            }
        }

        // Convert delay from double to i64 if needed
        if (delay->getType()->isDoubleTy()) {
            delay = builder_->CreateFPToSI(delay, builder_->getInt64Ty(), "delay_to_i64");
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy(), builder_->getInt64Ty() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(runtimeFunc, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { callback, delay });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle setImmediate - mangled names like setImmediate_any
    // Runtime function: ts_set_immediate(TsValue* callback) -> TsValue*
    if (funcName.rfind("setImmediate", 0) == 0 && funcName.rfind("clearImmediate", 0) != 0) {
        llvm::Value* callback = getOperandValue(inst->operands[1]);

        // Box callback if needed
        if (!callback->getType()->isPointerTy()) {
            if (callback->getType()->isIntegerTy(64)) {
                auto boxFn = getTsValueMakeInt();
                callback = builder_->CreateCall(boxFn, { callback });
            }
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_set_immediate", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { callback });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle clearTimeout/clearInterval/clearImmediate - mangled names
    // Runtime function: ts_clear_timer(TsValue* timerId) -> void
    if (funcName.rfind("clearTimeout", 0) == 0 || funcName.rfind("clearInterval", 0) == 0 ||
        funcName.rfind("clearImmediate", 0) == 0) {
        llvm::Value* timerId = getOperandValue(inst->operands[1]);

        // Box timerId if needed - it should be a TsValue* (timer handle)
        if (timerId->getType()->isDoubleTy()) {
            // Convert double to i64 first, then box
            timerId = builder_->CreateFPToSI(timerId, builder_->getInt64Ty(), "timer_to_i64");
            auto boxFn = getTsValueMakeInt();
            timerId = builder_->CreateCall(boxFn, { timerId });
        } else if (timerId->getType()->isIntegerTy(64)) {
            auto boxFn = getTsValueMakeInt();
            timerId = builder_->CreateCall(boxFn, { timerId });
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getVoidTy(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_clear_timer", ft);
        builder_->CreateCall(ft, fn.getCallee(), { timerId });
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
        return;
    }

    // Handle Number.isFinite - inline implementation (not a runtime call)
    // Returns true if value is finite (not NaN, not Infinity)
    if (funcName == "ts_number_isFinite") {
        llvm::Value* val = getOperandValue(inst->operands[1]);
        // Cast to double if needed
        if (val->getType()->isIntegerTy(64)) {
            val = builder_->CreateSIToFP(val, builder_->getDoubleTy(), "to_dbl");
        } else if (val->getType()->isIntegerTy(1)) {
            // Boolean to double
            val = builder_->CreateUIToFP(val, builder_->getDoubleTy(), "bool_to_dbl");
        } else if (val->getType()->isPointerTy()) {
            // Boxed value - return false (strict Number.isFinite doesn't do type coercion)
            if (inst->result) {
                setValue(inst->result, builder_->getInt1(false));
            }
            return;
        }
        // A number is finite if it's not NaN and not infinite
        llvm::Value* isNotNaN = builder_->CreateFCmpOEQ(val, val);
        llvm::Value* posInf = llvm::ConstantFP::getInfinity(builder_->getDoubleTy(), false);
        llvm::Value* negInf = llvm::ConstantFP::getInfinity(builder_->getDoubleTy(), true);
        llvm::Value* notPosInf = builder_->CreateFCmpONE(val, posInf);
        llvm::Value* notNegInf = builder_->CreateFCmpONE(val, negInf);
        llvm::Value* result = builder_->CreateAnd(isNotNaN, builder_->CreateAnd(notPosInf, notNegInf));
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Number.isNaN - inline implementation
    // Returns true only if value is NaN
    if (funcName == "ts_number_isNaN") {
        llvm::Value* val = getOperandValue(inst->operands[1]);
        // Cast to double if needed
        if (val->getType()->isIntegerTy(64)) {
            val = builder_->CreateSIToFP(val, builder_->getDoubleTy(), "to_dbl");
        } else if (val->getType()->isIntegerTy(1)) {
            val = builder_->CreateUIToFP(val, builder_->getDoubleTy(), "bool_to_dbl");
        } else if (val->getType()->isPointerTy()) {
            // Boxed value - return false (strict Number.isNaN doesn't do type coercion)
            if (inst->result) {
                setValue(inst->result, builder_->getInt1(false));
            }
            return;
        }
        // NaN is the only value that is not equal to itself
        llvm::Value* result = builder_->CreateFCmpUNO(val, val);
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Number.isInteger - inline implementation
    // Returns true if value is an integer (finite and floor(value) == value)
    if (funcName == "ts_number_isInteger") {
        llvm::Value* val = getOperandValue(inst->operands[1]);
        // Cast to double if needed
        if (val->getType()->isIntegerTy(64)) {
            // Integers are always integers
            if (inst->result) {
                setValue(inst->result, builder_->getInt1(true));
            }
            return;
        } else if (val->getType()->isIntegerTy(1)) {
            // Booleans converted to 0/1 are integers
            if (inst->result) {
                setValue(inst->result, builder_->getInt1(true));
            }
            return;
        } else if (val->getType()->isPointerTy()) {
            // Boxed value - return false (strict Number.isInteger doesn't do type coercion)
            if (inst->result) {
                setValue(inst->result, builder_->getInt1(false));
            }
            return;
        }
        // For doubles: isInteger = isFinite && floor(value) == value
        llvm::Value* isNotNaN = builder_->CreateFCmpOEQ(val, val);
        llvm::Value* posInf = llvm::ConstantFP::getInfinity(builder_->getDoubleTy(), false);
        llvm::Value* negInf = llvm::ConstantFP::getInfinity(builder_->getDoubleTy(), true);
        llvm::Value* notPosInf = builder_->CreateFCmpONE(val, posInf);
        llvm::Value* notNegInf = builder_->CreateFCmpONE(val, negInf);
        llvm::Value* isFinite = builder_->CreateAnd(isNotNaN, builder_->CreateAnd(notPosInf, notNegInf));
        // Check floor(val) == val using intrinsic
        llvm::Function* floorFn = llvm::Intrinsic::getDeclaration(module_.get(), llvm::Intrinsic::floor, {builder_->getDoubleTy()});
        llvm::Value* floorVal = builder_->CreateCall(floorFn, {val});
        llvm::Value* isWholeNumber = builder_->CreateFCmpOEQ(val, floorVal);
        llvm::Value* result = builder_->CreateAnd(isFinite, isWholeNumber);
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Number.isSafeInteger - inline implementation
    // Returns true if value is a safe integer (-2^53+1 to 2^53-1)
    if (funcName == "ts_number_isSafeInteger") {
        llvm::Value* val = getOperandValue(inst->operands[1]);
        // Cast to double if needed
        if (val->getType()->isIntegerTy(64)) {
            // i64 can hold values outside safe range, need to check
            val = builder_->CreateSIToFP(val, builder_->getDoubleTy(), "to_dbl");
        } else if (val->getType()->isIntegerTy(1)) {
            // Booleans (0/1) are always safe integers
            if (inst->result) {
                setValue(inst->result, builder_->getInt1(true));
            }
            return;
        } else if (val->getType()->isPointerTy()) {
            // Boxed value - return false
            if (inst->result) {
                setValue(inst->result, builder_->getInt1(false));
            }
            return;
        }
        // isSafeInteger = isInteger && abs(value) <= MAX_SAFE_INTEGER
        llvm::Value* isNotNaN = builder_->CreateFCmpOEQ(val, val);
        llvm::Value* posInf = llvm::ConstantFP::getInfinity(builder_->getDoubleTy(), false);
        llvm::Value* negInf = llvm::ConstantFP::getInfinity(builder_->getDoubleTy(), true);
        llvm::Value* notPosInf = builder_->CreateFCmpONE(val, posInf);
        llvm::Value* notNegInf = builder_->CreateFCmpONE(val, negInf);
        llvm::Value* isFinite = builder_->CreateAnd(isNotNaN, builder_->CreateAnd(notPosInf, notNegInf));
        llvm::Function* floorFn = llvm::Intrinsic::getDeclaration(module_.get(), llvm::Intrinsic::floor, {builder_->getDoubleTy()});
        llvm::Value* floorVal = builder_->CreateCall(floorFn, {val});
        llvm::Value* isWholeNumber = builder_->CreateFCmpOEQ(val, floorVal);
        llvm::Value* isInteger = builder_->CreateAnd(isFinite, isWholeNumber);
        // MAX_SAFE_INTEGER = 2^53 - 1 = 9007199254740991
        llvm::Value* maxSafe = llvm::ConstantFP::get(builder_->getDoubleTy(), 9007199254740991.0);
        llvm::Value* minSafe = llvm::ConstantFP::get(builder_->getDoubleTy(), -9007199254740991.0);
        llvm::Value* notTooLarge = builder_->CreateFCmpOLE(val, maxSafe);
        llvm::Value* notTooSmall = builder_->CreateFCmpOGE(val, minSafe);
        llvm::Value* inRange = builder_->CreateAnd(notTooLarge, notTooSmall);
        llvm::Value* result = builder_->CreateAnd(isInteger, inRange);
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Object.is(value1, value2) - needs to box both arguments
    if (funcName == "ts_object_is") {
        llvm::Value* val1 = getOperandValue(inst->operands[1]);
        llvm::Value* val2 = getOperandValue(inst->operands[2]);

        // Box val1 based on type
        if (val1->getType()->isIntegerTy(64)) {
            val1 = builder_->CreateCall(getTsValueMakeInt(), { val1 });
        } else if (val1->getType()->isDoubleTy()) {
            auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_make_double", ft);
            val1 = builder_->CreateCall(ft, fn.getCallee(), { val1 });
        } else if (val1->getType()->isIntegerTy(1)) {
            auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt1Ty() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_make_bool", ft);
            val1 = builder_->CreateCall(ft, fn.getCallee(), { val1 });
        }
        // For pointers, assume already boxed or wrap if needed
        if (!val1->getType()->isPointerTy()) {
            val1 = builder_->CreateIntToPtr(val1, builder_->getPtrTy());
        }

        // Box val2 based on type
        if (val2->getType()->isIntegerTy(64)) {
            val2 = builder_->CreateCall(getTsValueMakeInt(), { val2 });
        } else if (val2->getType()->isDoubleTy()) {
            auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_make_double", ft);
            val2 = builder_->CreateCall(ft, fn.getCallee(), { val2 });
        } else if (val2->getType()->isIntegerTy(1)) {
            auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt1Ty() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_make_bool", ft);
            val2 = builder_->CreateCall(ft, fn.getCallee(), { val2 });
        }
        if (!val2->getType()->isPointerTy()) {
            val2 = builder_->CreateIntToPtr(val2, builder_->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_object_is", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { val1, val2 });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle BigInt creation - ts_bigint_create_str(const char*, int32_t)
    if (funcName == "ts_bigint_create_str") {
        llvm::Value* strArg = getOperandValue(inst->operands[1]);
        llvm::Value* radixArg = getOperandValue(inst->operands[2]);
        // Convert radix from i64 to i32
        if (radixArg->getType()->isIntegerTy(64)) {
            radixArg = builder_->CreateTrunc(radixArg, builder_->getInt32Ty());
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy(), builder_->getInt32Ty() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_bigint_create_str", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { strArg, radixArg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle BigInt arithmetic - all take (ptr, ptr) and return ptr
    if (funcName == "ts_bigint_add" || funcName == "ts_bigint_sub" ||
        funcName == "ts_bigint_mul" || funcName == "ts_bigint_div" ||
        funcName == "ts_bigint_mod") {
        llvm::Value* a = getOperandValue(inst->operands[1]);
        llvm::Value* b = getOperandValue(inst->operands[2]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { a, b });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle BigInt comparison - take (ptr, ptr) and return bool (i1)
    if (funcName == "ts_bigint_lt" || funcName == "ts_bigint_le" ||
        funcName == "ts_bigint_gt" || funcName == "ts_bigint_ge" ||
        funcName == "ts_bigint_eq" || funcName == "ts_bigint_ne") {
        llvm::Value* a = getOperandValue(inst->operands[1]);
        llvm::Value* b = getOperandValue(inst->operands[2]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { a, b });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_array_find and ts_array_findLast - return ptr (TsValue*), need to unbox
    if (funcName == "ts_array_find" || funcName == "ts_array_findLast") {
        llvm::Value* arr = getOperandValue(inst->operands[1]);
        llvm::Value* callback = getOperandValue(inst->operands[2]);
        llvm::Value* thisArg = (inst->operands.size() > 3)
            ? getOperandValue(inst->operands[3])
            : llvm::ConstantPointerNull::get(builder_->getPtrTy());

        // Pass the raw closure pointer - runtime will check ts_is_closure and handle it
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arr, callback, thisArg });

        // The result is a boxed TsValue* - keep it as ptr for now
        // The caller (store instruction) will handle type conversion as needed
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_array_findIndex and ts_array_findLastIndex - return int64_t directly
    if (funcName == "ts_array_findIndex" || funcName == "ts_array_findLastIndex") {
        llvm::Value* arr = getOperandValue(inst->operands[1]);
        llvm::Value* callback = getOperandValue(inst->operands[2]);
        llvm::Value* thisArg = (inst->operands.size() > 3)
            ? getOperandValue(inst->operands[3])
            : llvm::ConstantPointerNull::get(builder_->getPtrTy());

        // Pass the raw closure pointer - runtime will check ts_is_closure and handle it
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt64Ty(),
            { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arr, callback, thisArg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_array_some and ts_array_every - return bool (i1)
    if (funcName == "ts_array_some" || funcName == "ts_array_every") {
        llvm::Value* arr = getOperandValue(inst->operands[1]);
        llvm::Value* callback = getOperandValue(inst->operands[2]);
        llvm::Value* thisArg = (inst->operands.size() > 3)
            ? getOperandValue(inst->operands[3])
            : llvm::ConstantPointerNull::get(builder_->getPtrTy());

        // Pass the raw closure pointer - runtime will check ts_is_closure and handle it
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(),
            { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arr, callback, thisArg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_array_slice - takes (ptr, i64, i64), returns ptr
    // HIR passes f64 for the indices but runtime expects i64
    if (funcName == "ts_array_slice") {
        llvm::Value* arr = getOperandValue(inst->operands[1]);
        llvm::Value* startVal = getOperandValue(inst->operands[2]);
        llvm::Value* endVal = getOperandValue(inst->operands[3]);

        // Convert f64 indices to i64
        if (startVal->getType()->isDoubleTy()) {
            startVal = builder_->CreateFPToSI(startVal, builder_->getInt64Ty());
        }
        if (endVal->getType()->isDoubleTy()) {
            endVal = builder_->CreateFPToSI(endVal, builder_->getInt64Ty());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getInt64Ty(), builder_->getInt64Ty() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arr, startVal, endVal });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_array_concat - takes (ptr, ptr), returns ptr
    if (funcName == "ts_array_concat") {
        llvm::Value* arr1 = getOperandValue(inst->operands[1]);
        llvm::Value* arr2 = getOperandValue(inst->operands[2]);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arr1, arr2 });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Helper to box a value for Map/Set operations
    // These need TsValue* so we must box primitives AND strings
    auto boxForMapSet = [this](llvm::Value* val, const HIROperand& operand) -> llvm::Value* {
        // Get HIR type from operand
        std::shared_ptr<HIRType> hirType = nullptr;
        if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
            if (*valPtr) hirType = (*valPtr)->type;
        }

        if (!val->getType()->isPointerTy()) {
            // Primitive types - box based on LLVM type
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = getTsValueMakeInt();
                return builder_->CreateCall(boxFn, {val});
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = getTsValueMakeDouble();
                return builder_->CreateCall(boxFn, {val});
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = getTsValueMakeBool();
                llvm::Value* extended = builder_->CreateZExt(val, builder_->getInt32Ty());
                return builder_->CreateCall(boxFn, {extended});
            }
        } else {
            // Pointer type - check HIR type to determine boxing method
            if (hirType && hirType->kind == HIRTypeKind::String) {
                // Box string with ts_value_make_string
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder_->getPtrTy(), { builder_->getPtrTy() }, false);
                llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_make_string", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { val });
            } else if (hirType && (hirType->kind == HIRTypeKind::Object ||
                                   hirType->kind == HIRTypeKind::Class ||
                                   hirType->kind == HIRTypeKind::Array)) {
                // Box object with ts_value_make_object
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder_->getPtrTy(), { builder_->getPtrTy() }, false);
                llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_make_object", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { val });
            }
            // If no HIR type info, assume it's already boxed or an object pointer
        }
        return val;
    };

    // Handle Map methods - they expect boxed TsValue* arguments
    // ts_map_set(map, key, value), ts_map_get(map, key), ts_map_has(map, key), ts_map_delete(map, key), ts_map_clear(map)
    if (funcName == "ts_map_set") {
        llvm::Value* map = getOperandValue(inst->operands[1]);
        llvm::Value* key = getOperandValue(inst->operands[2]);
        llvm::Value* value = getOperandValue(inst->operands[3]);

        // Box key and value
        key = boxForMapSet(key, inst->operands[2]);
        value = boxForMapSet(value, inst->operands[3]);

        // Call ts_map_set_wrapper(void* map, TsValue* key, TsValue* value) -> TsValue*
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_set_wrapper", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { map, key, value });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_map_get") {
        llvm::Value* map = getOperandValue(inst->operands[1]);
        llvm::Value* key = getOperandValue(inst->operands[2]);

        // Box key
        key = boxForMapSet(key, inst->operands[2]);

        // Call ts_map_get_wrapper(void* map, TsValue* key) -> TsValue*
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_get_wrapper", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { map, key });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_map_has") {
        llvm::Value* map = getOperandValue(inst->operands[1]);
        llvm::Value* key = getOperandValue(inst->operands[2]);

        // Box key
        key = boxForMapSet(key, inst->operands[2]);

        // Call ts_map_has_wrapper(void* map, TsValue* key) -> TsValue* (boxed bool)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_has_wrapper", ft);
        llvm::Value* boxedResult = builder_->CreateCall(ft, fn.getCallee(), { map, key });

        // Unbox the result to bool
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFt);
        llvm::Value* result = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { boxedResult });

        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_map_delete") {
        llvm::Value* map = getOperandValue(inst->operands[1]);
        llvm::Value* key = getOperandValue(inst->operands[2]);

        // Box key
        key = boxForMapSet(key, inst->operands[2]);

        // Call ts_map_delete_wrapper(void* map, TsValue* key) -> TsValue* (boxed bool)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_delete_wrapper", ft);
        llvm::Value* boxedResult = builder_->CreateCall(ft, fn.getCallee(), { map, key });

        // Unbox the result to bool
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFt);
        llvm::Value* result = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { boxedResult });

        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_map_clear") {
        llvm::Value* map = getOperandValue(inst->operands[1]);

        // Call ts_map_clear_wrapper(void* map) -> void
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_clear_wrapper", ft);
        builder_->CreateCall(ft, fn.getCallee(), { map });
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
        return;
    }

    if (funcName == "ts_map_size") {
        llvm::Value* map = getOperandValue(inst->operands[1]);

        // Call ts_map_size(void* map) -> int64_t
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt64Ty(),
            { builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_size", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { map });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Set methods - they expect boxed TsValue* arguments
    // ts_set_add(set, value), ts_set_has(set, value), ts_set_delete(set, value), ts_set_clear(set)
    if (funcName == "ts_set_add") {
        llvm::Value* set = getOperandValue(inst->operands[1]);
        llvm::Value* value = getOperandValue(inst->operands[2]);

        // Box value
        value = boxForMapSet(value, inst->operands[2]);

        // Call ts_set_add_wrapper(void* set, TsValue* value) -> TsValue*
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_set_add_wrapper", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { set, value });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_set_has") {
        llvm::Value* set = getOperandValue(inst->operands[1]);
        llvm::Value* value = getOperandValue(inst->operands[2]);

        // Box value
        value = boxForMapSet(value, inst->operands[2]);

        // Call ts_set_has_wrapper(void* set, TsValue* value) -> TsValue* (boxed bool)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_set_has_wrapper", ft);
        llvm::Value* boxedResult = builder_->CreateCall(ft, fn.getCallee(), { set, value });

        // Unbox the result to bool
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFt);
        llvm::Value* result = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { boxedResult });

        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_set_delete") {
        llvm::Value* set = getOperandValue(inst->operands[1]);
        llvm::Value* value = getOperandValue(inst->operands[2]);

        // Box value
        value = boxForMapSet(value, inst->operands[2]);

        // Call ts_set_delete_wrapper(void* set, TsValue* value) -> TsValue* (boxed bool)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_set_delete_wrapper", ft);
        llvm::Value* boxedResult = builder_->CreateCall(ft, fn.getCallee(), { set, value });

        // Unbox the result to bool
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(
            builder_->getInt1Ty(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFt);
        llvm::Value* result = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { boxedResult });

        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_set_clear") {
        llvm::Value* set = getOperandValue(inst->operands[1]);

        // Call ts_set_clear_wrapper(void* set) -> void
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_set_clear_wrapper", ft);
        builder_->CreateCall(ft, fn.getCallee(), { set });
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
        return;
    }

    if (funcName == "ts_set_size") {
        llvm::Value* set = getOperandValue(inst->operands[1]);

        // Call ts_set_size(void* set) -> int64_t
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt64Ty(),
            { builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_set_size", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { set });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle path module functions
    if (funcName == "ts_path_basename") {
        // ts_path_basename(void* path, void* ext) -> TsString*
        // ext is optional - pass null if not provided
        llvm::Value* path = getOperandValue(inst->operands[1]);
        llvm::Value* ext = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            ext = getOperandValue(inst->operands[2]);
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_path_basename", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { path, ext });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_path_dirname") {
        // ts_path_dirname(void* path) -> TsString*
        llvm::Value* path = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_path_dirname", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { path });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_path_extname") {
        // ts_path_extname(void* path) -> TsString*
        llvm::Value* path = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_path_extname", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { path });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_path_join") {
        // ts_path_join(void* path1, void* path2) -> TsString*
        llvm::Value* path1 = getOperandValue(inst->operands[1]);
        llvm::Value* path2 = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            path2 = getOperandValue(inst->operands[2]);
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_path_join", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { path1, path2 });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_path_normalize") {
        // ts_path_normalize(void* path) -> TsString*
        llvm::Value* path = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_path_normalize", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { path });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_path_resolve") {
        // ts_path_resolve(void* paths) -> TsString*
        llvm::Value* paths = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_path_resolve", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { paths });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_path_relative") {
        // ts_path_relative(void* from, void* to) -> TsString*
        llvm::Value* from = getOperandValue(inst->operands[1]);
        llvm::Value* to = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            to = getOperandValue(inst->operands[2]);
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_path_relative", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { from, to });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_path_isAbsolute") {
        // ts_path_is_absolute(void* path) -> int (bool as int)
        llvm::Value* path = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt32Ty(),
            { builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_path_is_absolute", ft);
        llvm::Value* result32 = builder_->CreateCall(ft, fn.getCallee(), { path });
        // Convert to bool
        llvm::Value* result = builder_->CreateICmpNE(result32,
            llvm::ConstantInt::get(builder_->getInt32Ty(), 0));
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle JSON module functions
    if (funcName == "ts_json_stringify") {
        // ts_json_stringify(void* obj, void* replacer, void* space) -> TsString*
        // replacer and space are optional - pass null if not provided
        llvm::Value* obj = getOperandValue(inst->operands[1]);
        // Box the object if it's a primitive type (int, bool, double)
        if (obj->getType()->isIntegerTy(64)) {
            // Box integer as TsValue
            llvm::FunctionType* boxFt = llvm::FunctionType::get(
                builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
            llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_int", boxFt);
            obj = builder_->CreateCall(boxFt, boxFn.getCallee(), { obj });
        } else if (obj->getType()->isIntegerTy(1)) {
            // Box boolean as TsValue
            llvm::Value* extended = builder_->CreateZExt(obj, builder_->getInt64Ty());
            llvm::FunctionType* boxFt = llvm::FunctionType::get(
                builder_->getPtrTy(), { builder_->getInt1Ty() }, false);
            llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_bool", boxFt);
            obj = builder_->CreateCall(boxFt, boxFn.getCallee(), { builder_->CreateTrunc(extended, builder_->getInt1Ty()) });
        } else if (obj->getType()->isDoubleTy()) {
            // Box double as TsValue
            llvm::FunctionType* boxFt = llvm::FunctionType::get(
                builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
            llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_double", boxFt);
            obj = builder_->CreateCall(boxFt, boxFn.getCallee(), { obj });
        }
        llvm::Value* replacer = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        llvm::Value* space = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            replacer = getOperandValue(inst->operands[2]);
        }
        if (inst->operands.size() > 3) {
            space = getOperandValue(inst->operands[3]);
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_json_stringify", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, replacer, space });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Try registry-based lowering for runtime functions
    if (auto* spec = ::hir::LoweringRegistry::instance().lookup(funcName)) {
        llvm::Value* result = lowerRegisteredCall(inst, *spec);
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Generic function call
    llvm::Function* fn = module_->getFunction(funcName);
    if (!fn) {
        // Declare as external if not found
        // Use generic signature for now: ptr(ptr, ptr, ...)
        std::vector<llvm::Type*> paramTypes;
        for (size_t i = 1; i < inst->operands.size(); ++i) {
            paramTypes.push_back(builder_->getPtrTy());
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(builder_->getPtrTy(), paramTypes, false);
        fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, funcName, module_.get());
    }

    // Gather arguments
    std::vector<llvm::Value*> args;
    for (size_t i = 1; i < inst->operands.size(); ++i) {
        args.push_back(getOperandValue(inst->operands[i]));
    }

    llvm::Value* result = builder_->CreateCall(fn, args);
    if (inst->result) {
        setValue(inst->result, result);
    }
}

//==============================================================================
// Registry-Based Call Lowering
//==============================================================================

llvm::Value* HIRToLLVM::lowerRegisteredCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec) {
    // Check for variadic handling
    if (spec.variadicHandling != ::hir::VariadicHandling::None) {
        switch (spec.variadicHandling) {
            case ::hir::VariadicHandling::TypeDispatch:
                return lowerTypeDispatchCall(inst, spec);
            case ::hir::VariadicHandling::PackArray:
                return lowerPackArrayCall(inst, spec);
            case ::hir::VariadicHandling::Inline:
                // Inline handling is done elsewhere (e.g., Math.min/max)
                // Fall through to standard handling for now
                break;
            default:
                break;
        }
    }

    // Build LLVM function type
    llvm::Type* retTy = spec.returnType
        ? spec.returnType(context_)
        : builder_->getVoidTy();

    std::vector<llvm::Type*> argTys;
    for (const auto& argType : spec.argTypes) {
        argTys.push_back(argType(context_));
    }

    auto* ft = llvm::FunctionType::get(retTy, argTys, spec.isVariadic);
    auto fn = module_->getOrInsertFunction(spec.runtimeFuncName, ft);

    // Convert arguments
    std::vector<llvm::Value*> llvmArgs;
    for (size_t i = 1; i < inst->operands.size() && (i - 1) < spec.argConversions.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);
        arg = convertArg(arg, spec.argConversions[i - 1]);
        llvmArgs.push_back(arg);
    }

    // Pad missing optional arguments with null/zero values
    size_t numProvidedArgs = inst->operands.size() - 1;  // -1 for function name
    while (llvmArgs.size() < argTys.size()) {
        size_t idx = llvmArgs.size();
        llvm::Type* expectedType = argTys[idx];
        if (expectedType->isPointerTy()) {
            llvmArgs.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(expectedType)));
        } else if (expectedType->isIntegerTy()) {
            llvmArgs.push_back(llvm::ConstantInt::get(expectedType, 0));
        } else if (expectedType->isDoubleTy()) {
            llvmArgs.push_back(llvm::ConstantFP::get(expectedType, 0.0));
        } else {
            // Fallback to null pointer for unknown types
            llvmArgs.push_back(llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
    }

    // Standard call
    llvm::Value* result;
    if (retTy->isVoidTy()) {
        builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
        result = llvm::ConstantPointerNull::get(builder_->getPtrTy());
    } else {
        result = builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
    }

    // Handle return
    return handleReturn(result, spec.returnHandling);
}

llvm::Value* HIRToLLVM::convertArg(llvm::Value* arg, ::hir::ArgConversion conv) {
    switch (conv) {
        case ::hir::ArgConversion::None:
            return arg;

        case ::hir::ArgConversion::Box: {
            // Box the value based on its LLVM type
            llvm::Type* argType = arg->getType();

            if (argType->isIntegerTy(64)) {
                auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_int", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { arg });
            } else if (argType->isDoubleTy()) {
                auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_double", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { arg });
            } else if (argType->isIntegerTy(1)) {
                // ts_value_make_bool expects i32, extend i1 to i32
                auto fn = getTsValueMakeBool();
                llvm::Value* extended = builder_->CreateZExt(arg, builder_->getInt32Ty(), "bool_ext");
                return builder_->CreateCall(fn, { extended });
            } else if (argType->isPointerTy()) {
                // Already a pointer, box as object
                auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getPtrTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_object", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { arg });
            }
            return arg;
        }

        case ::hir::ArgConversion::Unbox: {
            auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getPtrTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_get_object", ft);
            return builder_->CreateCall(ft, fn.getCallee(), { arg });
        }

        case ::hir::ArgConversion::ToI64: {
            if (arg->getType()->isDoubleTy()) {
                return builder_->CreateFPToSI(arg, builder_->getInt64Ty());
            } else if (arg->getType()->isPointerTy()) {
                return builder_->CreatePtrToInt(arg, builder_->getInt64Ty());
            }
            return arg;
        }

        case ::hir::ArgConversion::ToF64: {
            if (arg->getType()->isIntegerTy(64)) {
                return builder_->CreateSIToFP(arg, builder_->getDoubleTy());
            }
            return arg;
        }

        case ::hir::ArgConversion::ToI32: {
            if (arg->getType()->isIntegerTy(64)) {
                return builder_->CreateTrunc(arg, builder_->getInt32Ty());
            }
            return arg;
        }

        case ::hir::ArgConversion::ToBool: {
            if (arg->getType()->isIntegerTy()) {
                return builder_->CreateICmpNE(arg,
                    llvm::ConstantInt::get(arg->getType(), 0));
            }
            return arg;
        }

        case ::hir::ArgConversion::PtrToInt:
            return builder_->CreatePtrToInt(arg, builder_->getInt64Ty());

        case ::hir::ArgConversion::IntToPtr:
            return builder_->CreateIntToPtr(arg, builder_->getPtrTy());
    }
    return arg;
}

llvm::Value* HIRToLLVM::handleReturn(llvm::Value* result, ::hir::ReturnHandling handling) {
    switch (handling) {
        case ::hir::ReturnHandling::Void:
            return llvm::ConstantPointerNull::get(builder_->getPtrTy());

        case ::hir::ReturnHandling::Raw:
            return result;

        case ::hir::ReturnHandling::Boxed:
            // Result is already boxed, just return it
            return result;

        case ::hir::ReturnHandling::BoxResult: {
            // Box the raw result
            llvm::Type* resType = result->getType();
            if (resType->isIntegerTy(64)) {
                auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_int", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { result });
            } else if (resType->isDoubleTy()) {
                auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_double", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { result });
            } else if (resType->isIntegerTy(1)) {
                auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt1Ty() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_bool", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { result });
            }
            return result;
        }
    }
    return result;
}

//==============================================================================
// Variadic Function Lowering Helpers
//==============================================================================

std::string HIRToLLVM::getTypeSuffix(llvm::Value* arg, const ::hir::LoweringSpec& spec) {
    llvm::Type* argType = arg->getType();

    // Check available suffixes in spec
    const auto& suffixes = spec.typeDispatchSuffixes;

    if (argType->isIntegerTy(64)) {
        // Check for _int suffix
        for (const auto& suffix : suffixes) {
            if (suffix == "_int") return "_int";
        }
    } else if (argType->isDoubleTy()) {
        // Check for _double suffix
        for (const auto& suffix : suffixes) {
            if (suffix == "_double") return "_double";
        }
    } else if (argType->isIntegerTy(1)) {
        // Check for _bool suffix
        for (const auto& suffix : suffixes) {
            if (suffix == "_bool") return "_bool";
        }
    } else if (argType->isPointerTy()) {
        // Could be string or object - try _string first, then _object
        for (const auto& suffix : suffixes) {
            if (suffix == "_string") return "_string";
        }
        for (const auto& suffix : suffixes) {
            if (suffix == "_object") return "_object";
        }
        // For pointers without specific suffix, use _value if available
        for (const auto& suffix : suffixes) {
            if (suffix == "_value") return "_value";
        }
    }

    // Return default suffix if no specific match
    return spec.defaultSuffix;
}

llvm::Value* HIRToLLVM::lowerTypeDispatchCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec) {
    // TypeDispatch: Call type-specific functions for each variadic argument
    // e.g., console.log(42, "hello") -> ts_console_log_int(42); ts_console_log_string("hello");

    size_t restIndex = spec.restParamIndex;
    llvm::Value* lastResult = llvm::ConstantPointerNull::get(builder_->getPtrTy());

    // Process each argument starting at restParamIndex
    // operands[0] is the function/method name, operands[1+] are arguments
    for (size_t i = restIndex + 1; i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);

        // Determine the type suffix for this argument
        std::string suffix = getTypeSuffix(arg, spec);

        // Build the type-specific function name
        std::string funcName = spec.runtimeFuncName + suffix;

        // Determine the LLVM type for this argument
        llvm::Type* argType = arg->getType();
        llvm::Type* paramType = argType;

        // Get the return type from spec
        llvm::Type* retTy = spec.returnType
            ? spec.returnType(context_)
            : builder_->getVoidTy();

        // Create function type and call
        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, { paramType }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        lastResult = builder_->CreateCall(ft, fn.getCallee(), { arg });
    }

    return handleReturn(lastResult, spec.returnHandling);
}

llvm::Value* HIRToLLVM::lowerPackArrayCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec) {
    // PackArray: Pack rest arguments into a TsArray, then call the runtime function
    // e.g., Array.of(1, 2, 3) -> arr = ts_array_create(); ts_array_push(arr, 1); ts_array_push(arr, 2); ...

    size_t restIndex = spec.restParamIndex;

    // Create a new array for the rest arguments
    auto createFt = llvm::FunctionType::get(builder_->getPtrTy(), {}, false);
    auto createFn = module_->getOrInsertFunction("ts_array_create", createFt);
    llvm::Value* restArray = builder_->CreateCall(createFt, createFn.getCallee(), {});

    // Push each rest argument to the array
    auto pushFt = llvm::FunctionType::get(builder_->getInt64Ty(),
        { builder_->getPtrTy(), builder_->getPtrTy() }, false);
    auto pushFn = module_->getOrInsertFunction("ts_array_push", pushFt);

    // operands[0] is the function name, operands[1+] are arguments
    for (size_t i = restIndex + 1; i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);

        // Box the argument if needed
        arg = convertArg(arg, ::hir::ArgConversion::Box);

        // Push to array
        builder_->CreateCall(pushFt, pushFn.getCallee(), { restArray, arg });
    }

    // Now call the actual runtime function with the packed array
    // Build fixed arguments (before restParamIndex) + the rest array
    std::vector<llvm::Value*> llvmArgs;

    // Add fixed arguments (if any)
    for (size_t i = 1; i <= restIndex && i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);
        if (i - 1 < spec.argConversions.size()) {
            arg = convertArg(arg, spec.argConversions[i - 1]);
        }
        llvmArgs.push_back(arg);
    }

    // Add the rest array
    llvmArgs.push_back(restArray);

    // Build LLVM function type
    llvm::Type* retTy = spec.returnType
        ? spec.returnType(context_)
        : builder_->getVoidTy();

    std::vector<llvm::Type*> argTys;
    for (size_t i = 0; i < spec.argTypes.size(); ++i) {
        argTys.push_back(spec.argTypes[i](context_));
    }

    // If spec doesn't include the rest array type, add it
    if (argTys.size() < llvmArgs.size()) {
        argTys.push_back(builder_->getPtrTy());
    }

    auto* ft = llvm::FunctionType::get(retTy, argTys, false);
    auto fn = module_->getOrInsertFunction(spec.runtimeFuncName, ft);

    // Call the function
    llvm::Value* result;
    if (retTy->isVoidTy()) {
        builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
        result = llvm::ConstantPointerNull::get(builder_->getPtrTy());
    } else {
        result = builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
    }

    return handleReturn(result, spec.returnHandling);
}

void HIRToLLVM::lowerCallMethod(HIRInstruction* inst) {
    // operands[0] = object, operands[1] = methodName, operands[2..] = args
    llvm::Value* obj = getOperandValue(inst->operands[0]);
    std::string methodName = getOperandString(inst->operands[1]);

    // Handle console.log / console.error / console.warn / console.info / console.debug via registry
    if (methodName == "log" || methodName == "error" || methodName == "warn" ||
        methodName == "info" || methodName == "debug") {

        // Determine the base runtime function name
        std::string baseFuncName = (methodName == "error" || methodName == "warn")
            ? "ts_console_error" : "ts_console_log";

        // Look up the lowering spec from the registry
        auto& registry = ::hir::LoweringRegistry::instance();
        const ::hir::LoweringSpec* spec = registry.lookup(baseFuncName);

        if (spec && spec->variadicHandling == ::hir::VariadicHandling::TypeDispatch) {
            // Use registry-driven type dispatch
            // For console methods, operands[2..] are the arguments (skip object and methodName)
            for (size_t i = 2; i < inst->operands.size(); ++i) {
                llvm::Value* arg = getOperandValue(inst->operands[i]);

                // Get type suffix based on argument type
                std::string suffix = getTypeSuffix(arg, *spec);
                std::string funcName = baseFuncName + suffix;

                // Get the LLVM type for this argument
                llvm::Type* paramType = arg->getType();

                // Emit the call
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder_->getVoidTy(), { paramType }, false);
                llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
                builder_->CreateCall(ft, fn.getCallee(), { arg });
            }
        } else {
            // Fallback for non-registered functions (shouldn't happen with proper registration)
            for (size_t i = 2; i < inst->operands.size(); ++i) {
                llvm::Value* arg = getOperandValue(inst->operands[i]);

                std::string funcName = baseFuncName + "_value";
                llvm::Type* paramType = builder_->getPtrTy();

                // Convert to pointer type if needed
                if (!arg->getType()->isPointerTy()) {
                    arg = convertArg(arg, ::hir::ArgConversion::Box);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder_->getVoidTy(), { paramType }, false);
                llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
                builder_->CreateCall(ft, fn.getCallee(), { arg });
            }
        }

        // console.log returns undefined
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
        return;
    }

    // Check if we can determine the object type from the HIRValue
    std::string className;
    if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*valPtr && (*valPtr)->type) {
            if ((*valPtr)->type->kind == HIRTypeKind::Class) {
                className = (*valPtr)->type->className;
            } else if ((*valPtr)->type->kind == HIRTypeKind::Map) {
                className = "Map";
            } else if ((*valPtr)->type->kind == HIRTypeKind::Set) {
                className = "Set";
            }
        }
    }

    // Handle Generator.next()
    if ((className == "Generator" || className.empty()) && methodName == "next") {
        // Generator_next(gen, value) -> returns iterator result {value, done}
        llvm::FunctionType* nextFt = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false
        );
        llvm::FunctionCallee nextFn = module_->getOrInsertFunction("Generator_next", nextFt);

        // The generator object - must box it with ts_value_make_object
        llvm::Value* boxedGen = obj;
        if (!obj->getType()->isPointerTy()) {
            boxedGen = builder_->CreateIntToPtr(obj, builder_->getPtrTy());
        }
        // Box the generator object pointer
        llvm::FunctionType* boxObjFt = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee boxObjFn = module_->getOrInsertFunction("ts_value_make_object", boxObjFt);
        boxedGen = builder_->CreateCall(boxObjFt, boxObjFn.getCallee(), { boxedGen });

        // Get the argument (or null if none)
        llvm::Value* val = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            val = getOperandValue(inst->operands[2]);
            // Ensure val is a pointer (box if needed)
            if (!val->getType()->isPointerTy()) {
                if (val->getType()->isIntegerTy(64)) {
                    // Box integer value
                    llvm::FunctionType* boxFt = llvm::FunctionType::get(
                        builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
                    llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_int", boxFt);
                    val = builder_->CreateCall(boxFt, boxFn.getCallee(), { val });
                } else if (val->getType()->isDoubleTy()) {
                    // Box double value
                    llvm::FunctionType* boxFt = llvm::FunctionType::get(
                        builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
                    llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_double", boxFt);
                    val = builder_->CreateCall(boxFt, boxFn.getCallee(), { val });
                }
            }
        }

        llvm::Value* result = builder_->CreateCall(nextFt, nextFn.getCallee(), { boxedGen, val });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle AsyncGenerator.next()
    if (className == "AsyncGenerator" && methodName == "next") {
        // AsyncGenerator_next(gen, value) -> returns Promise<iterator result>
        llvm::FunctionType* nextFt = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false
        );
        llvm::FunctionCallee nextFn = module_->getOrInsertFunction("AsyncGenerator_next", nextFt);

        // The generator object - must box it with ts_value_make_object
        llvm::Value* boxedGen = obj;
        if (!obj->getType()->isPointerTy()) {
            boxedGen = builder_->CreateIntToPtr(obj, builder_->getPtrTy());
        }
        // Box the generator object pointer
        llvm::FunctionType* boxObjFt = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee boxObjFn = module_->getOrInsertFunction("ts_value_make_object", boxObjFt);
        boxedGen = builder_->CreateCall(boxObjFt, boxObjFn.getCallee(), { boxedGen });

        // Get the argument (or null if none)
        llvm::Value* val = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            val = getOperandValue(inst->operands[2]);
            // Ensure val is a pointer (box if needed)
            if (!val->getType()->isPointerTy()) {
                if (val->getType()->isIntegerTy(64)) {
                    llvm::FunctionType* boxFt = llvm::FunctionType::get(
                        builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
                    llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_int", boxFt);
                    val = builder_->CreateCall(boxFt, boxFn.getCallee(), { val });
                } else if (val->getType()->isDoubleTy()) {
                    llvm::FunctionType* boxFt = llvm::FunctionType::get(
                        builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
                    llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_double", boxFt);
                    val = builder_->CreateCall(boxFt, boxFn.getCallee(), { val });
                }
            }
        }

        llvm::Value* result = builder_->CreateCall(nextFt, nextFn.getCallee(), { boxedGen, val });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle RegExp.test(string) -> bool
    if ((className == "RegExp" || className.empty()) && methodName == "test") {
        // RegExp_test(re, str) -> returns i32 (0 or 1)
        llvm::FunctionType* testFt = llvm::FunctionType::get(
            builder_->getInt32Ty(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false
        );
        llvm::FunctionCallee testFn = module_->getOrInsertFunction("RegExp_test", testFt);

        // Get the string argument
        llvm::Value* str = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            str = getOperandValue(inst->operands[2]);
        }

        // Call RegExp_test and convert i32 result to i1 (bool)
        llvm::Value* result32 = builder_->CreateCall(testFt, testFn.getCallee(), { obj, str });
        llvm::Value* result = builder_->CreateICmpNE(result32,
            llvm::ConstantInt::get(builder_->getInt32Ty(), 0));

        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Try to look up user-defined class method
    if (!className.empty()) {
        std::string funcName = className + "_" + methodName;
        llvm::Function* fn = module_->getFunction(funcName);
        if (fn) {
            // Found the method function - call it with 'this' and arguments
            std::vector<llvm::Value*> args;
            args.push_back(obj);  // 'this' pointer
            for (size_t i = 2; i < inst->operands.size(); ++i) {
                args.push_back(getOperandValue(inst->operands[i]));
            }

            llvm::Value* result = builder_->CreateCall(fn, args);
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
    }

    // Handle common array methods on Any-typed values
    // This handles cases where MethodResolutionPass couldn't resolve due to Any type
    if (methodName == "join") {
        // ts_array_join(void* arr, void* separator) -> TsString*
        llvm::Value* separator = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            separator = getOperandValue(inst->operands[2]);
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_join", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, separator });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (methodName == "push") {
        // ts_array_push(void* arr, void* value) -> int64_t (new length)
        if (inst->operands.size() > 2) {
            llvm::Value* val = getOperandValue(inst->operands[2]);

            // Box the value if needed
            if (!val->getType()->isPointerTy()) {
                if (val->getType()->isIntegerTy(64)) {
                    auto boxFn = getTsValueMakeInt();
                    val = builder_->CreateCall(boxFn, {val});
                } else if (val->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    val = builder_->CreateCall(boxFn, {val});
                } else if (val->getType()->isIntegerTy(1)) {
                    auto boxFn = getTsValueMakeBool();
                    llvm::Value* extended = builder_->CreateZExt(val, builder_->getInt32Ty());
                    val = builder_->CreateCall(boxFn, {extended});
                }
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getInt64Ty(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_push", ft);
            llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, val });
            if (inst->result) {
                setValue(inst->result, result);
            }
        }
        return;
    }

    // Handle RegExp.exec method
    if (methodName == "exec") {
        // RegExp_exec(void* regexp, void* string) -> void* (array or null)
        llvm::Value* str = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            str = getOperandValue(inst->operands[2]);
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("RegExp_exec", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, str });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle RegExp.test method
    if (methodName == "test") {
        // RegExp_test(void* regexp, void* string) -> int32_t (boolean as int)
        llvm::Value* str = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            str = getOperandValue(inst->operands[2]);
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt32Ty(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("RegExp_test", ft);
        llvm::Value* result32 = builder_->CreateCall(ft, fn.getCallee(), { obj, str });
        // Convert to i1 for boolean
        llvm::Value* result = builder_->CreateICmpNE(result32, builder_->getInt32(0));
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Helper lambda to box a value for Map/Set operations
    // Returns TsValue* from the LLVM value based on its HIR type
    auto boxValueForMapSet = [this](HIROperand& operand) -> llvm::Value* {
        llvm::Value* val = getOperandValue(operand);

        // Check the HIR type of the operand to decide how to box
        std::shared_ptr<HIRType> hirType = nullptr;
        if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
            if (*valPtr) hirType = (*valPtr)->type;
        }

        // If it's already a pointer, we need to check if it's a string or object
        if (val->getType()->isPointerTy()) {
            // Check HIR type to determine boxing function
            if (hirType && hirType->kind == HIRTypeKind::String) {
                // Box as string
                llvm::FunctionType* boxFt = llvm::FunctionType::get(
                    builder_->getPtrTy(), { builder_->getPtrTy() }, false);
                llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_string", boxFt);
                return builder_->CreateCall(boxFt, boxFn.getCallee(), { val });
            } else {
                // Box as object for any other pointer type
                llvm::FunctionType* boxFt = llvm::FunctionType::get(
                    builder_->getPtrTy(), { builder_->getPtrTy() }, false);
                llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_object", boxFt);
                return builder_->CreateCall(boxFt, boxFn.getCallee(), { val });
            }
        } else if (val->getType()->isIntegerTy(64)) {
            llvm::FunctionType* boxFt = llvm::FunctionType::get(
                builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
            llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_int", boxFt);
            return builder_->CreateCall(boxFt, boxFn.getCallee(), { val });
        } else if (val->getType()->isDoubleTy()) {
            llvm::FunctionType* boxFt = llvm::FunctionType::get(
                builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
            llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_double", boxFt);
            return builder_->CreateCall(boxFt, boxFn.getCallee(), { val });
        } else if (val->getType()->isIntegerTy(1)) {
            // Bool
            llvm::FunctionType* boxFt = llvm::FunctionType::get(
                builder_->getPtrTy(), { builder_->getInt1Ty() }, false);
            llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_bool", boxFt);
            return builder_->CreateCall(boxFt, boxFn.getCallee(), { val });
        }

        // Fallback: return as-is (already boxed or unknown type)
        return val;
    };

    // Handle Map methods
    if (className == "Map" || className.empty()) {
        if (methodName == "set") {
            // ts_map_set_wrapper(void* map, TsValue* key, TsValue* value) -> TsValue* (returns map for chaining)
            llvm::Value* key = llvm::ConstantPointerNull::get(builder_->getPtrTy());
            llvm::Value* value = llvm::ConstantPointerNull::get(builder_->getPtrTy());
            if (inst->operands.size() > 2) {
                key = boxValueForMapSet(inst->operands[2]);
            }
            if (inst->operands.size() > 3) {
                value = boxValueForMapSet(inst->operands[3]);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_set_wrapper", ft);
            llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, key, value });
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
        if (methodName == "get") {
            // ts_map_get_wrapper(void* map, TsValue* key) -> TsValue*
            llvm::Value* key = llvm::ConstantPointerNull::get(builder_->getPtrTy());
            if (inst->operands.size() > 2) {
                key = boxValueForMapSet(inst->operands[2]);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_get_wrapper", ft);
            llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, key });
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
        if (methodName == "has" && className == "Map") {
            // ts_map_has_wrapper(void* map, TsValue* key) -> TsValue* (bool boxed)
            llvm::Value* key = llvm::ConstantPointerNull::get(builder_->getPtrTy());
            if (inst->operands.size() > 2) {
                key = boxValueForMapSet(inst->operands[2]);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_has_wrapper", ft);
            llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, key });
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
        if (methodName == "delete" && className == "Map") {
            // ts_map_delete_wrapper(void* map, TsValue* key) -> TsValue* (bool boxed)
            llvm::Value* key = llvm::ConstantPointerNull::get(builder_->getPtrTy());
            if (inst->operands.size() > 2) {
                key = boxValueForMapSet(inst->operands[2]);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_delete_wrapper", ft);
            llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, key });
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
        if (methodName == "clear" && className == "Map") {
            // ts_map_clear_wrapper(void* map) -> TsValue* (undefined)
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy() },
                false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_clear_wrapper", ft);
            llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj });
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
    }

    // Handle Set methods
    if (className == "Set" || className.empty()) {
        if (methodName == "add") {
            // ts_set_add_wrapper(void* set, TsValue* value) -> TsValue* (returns set for chaining)
            llvm::Value* value = llvm::ConstantPointerNull::get(builder_->getPtrTy());
            if (inst->operands.size() > 2) {
                value = boxValueForMapSet(inst->operands[2]);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_set_add_wrapper", ft);
            llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, value });
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
        if (methodName == "has" && className == "Set") {
            // ts_set_has_wrapper(void* set, TsValue* value) -> TsValue* (bool boxed)
            llvm::Value* value = llvm::ConstantPointerNull::get(builder_->getPtrTy());
            if (inst->operands.size() > 2) {
                value = boxValueForMapSet(inst->operands[2]);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_set_has_wrapper", ft);
            llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, value });
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
        if (methodName == "delete" && className == "Set") {
            // ts_set_delete_wrapper(void* set, TsValue* value) -> TsValue* (bool boxed)
            llvm::Value* value = llvm::ConstantPointerNull::get(builder_->getPtrTy());
            if (inst->operands.size() > 2) {
                value = boxValueForMapSet(inst->operands[2]);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_set_delete_wrapper", ft);
            llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, value });
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
        if (methodName == "clear" && className == "Set") {
            // ts_set_clear_wrapper(void* set) -> TsValue* (undefined)
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy() },
                false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_set_clear_wrapper", ft);
            llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj });
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
    }

    // Handle has method when className is empty (could be Map or Set)
    if (className.empty() && methodName == "has") {
        // Try to use Map.has as default
        llvm::Value* key = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        if (inst->operands.size() > 2) {
            key = boxValueForMapSet(inst->operands[2]);
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        // Use ts_map_has_wrapper as it works for both Map and Set conceptually
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_map_has_wrapper", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, key });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Try to call a function stored as an object property
    // This handles cases like task.fn() where fn is a function property
    // IMPORTANT: Use ts_call_with_this_X to pass 'this' binding for methods like hasOwnProperty
    {
        // Get the property value (the function)
        // ts_object_get_property_static(void* obj, const char* key) -> TsValue*
        llvm::FunctionType* getFt = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee getFn = module_->getOrInsertFunction("ts_object_get_property", getFt);

        // Create method name as global string
        llvm::Constant* methodNameStr = builder_->CreateGlobalStringPtr(methodName, "method_name");

        // Get the function property
        llvm::Value* funcVal = builder_->CreateCall(getFt, getFn.getCallee(), { obj, methodNameStr });

        // Box obj if needed for thisArg
        llvm::Value* thisArg = obj;
        if (!obj->getType()->isPointerTy()) {
            thisArg = builder_->CreateIntToPtr(obj, builder_->getPtrTy());
        }
        // Box thisArg as TsValue for ts_call_with_this_X
        llvm::FunctionType* boxObjFt = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee boxObjFn = module_->getOrInsertFunction("ts_value_make_object", boxObjFt);
        thisArg = builder_->CreateCall(boxObjFt, boxObjFn.getCallee(), { thisArg });

        // Call the function with 'this' binding
        // Use ts_call_with_this_X to properly bind 'this' for methods like hasOwnProperty
        size_t argCount = inst->operands.size() - 2;  // Subtract obj and method name
        llvm::Value* result;

        if (argCount == 0) {
            llvm::FunctionType* callFt = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_0", callFt);
            result = builder_->CreateCall(callFt, callFn.getCallee(), { funcVal, thisArg });
        } else if (argCount == 1) {
            llvm::Value* arg1 = getOperandValue(inst->operands[2]);
            // Box arg1 if needed - check both LLVM type and HIR type
            if (!arg1->getType()->isPointerTy()) {
                if (arg1->getType()->isIntegerTy(64)) {
                    auto boxFn = getTsValueMakeInt();
                    arg1 = builder_->CreateCall(boxFn, {arg1});
                } else if (arg1->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    arg1 = builder_->CreateCall(boxFn, {arg1});
                }
            } else {
                // Pointer type - check HIR type to determine boxing
                if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[2])) {
                    auto hirType = (*hirVal)->type;
                    if (hirType && hirType->kind == HIRTypeKind::String) {
                        auto boxFn = getTsValueMakeString();
                        arg1 = builder_->CreateCall(boxFn, {arg1});
                    } else if (hirType && (hirType->kind == HIRTypeKind::Object ||
                                           hirType->kind == HIRTypeKind::Array ||
                                           hirType->kind == HIRTypeKind::Class ||
                                           hirType->kind == HIRTypeKind::Map)) {
                        // Box objects with ts_value_make_object
                        llvm::FunctionType* boxFt = llvm::FunctionType::get(
                            builder_->getPtrTy(), { builder_->getPtrTy() }, false);
                        llvm::FunctionCallee boxFn = module_->getOrInsertFunction("ts_value_make_object", boxFt);
                        arg1 = builder_->CreateCall(boxFt, boxFn.getCallee(), {arg1});
                    }
                    // For Any type, it might already be boxed - pass as-is
                }
            }
            llvm::FunctionType* callFt = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_1", callFt);
            result = builder_->CreateCall(callFt, callFn.getCallee(), { funcVal, thisArg, arg1 });
        } else if (argCount == 2) {
            llvm::Value* arg1 = getOperandValue(inst->operands[2]);
            llvm::Value* arg2 = getOperandValue(inst->operands[3]);
            // Box args if needed
            if (!arg1->getType()->isPointerTy()) {
                if (arg1->getType()->isIntegerTy(64)) {
                    auto boxFn = getTsValueMakeInt();
                    arg1 = builder_->CreateCall(boxFn, {arg1});
                } else if (arg1->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    arg1 = builder_->CreateCall(boxFn, {arg1});
                }
            } else if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[2])) {
                auto hirType = (*hirVal)->type;
                if (hirType && hirType->kind == HIRTypeKind::String) {
                    auto boxFn = getTsValueMakeString();
                    arg1 = builder_->CreateCall(boxFn, {arg1});
                }
            }
            if (!arg2->getType()->isPointerTy()) {
                if (arg2->getType()->isIntegerTy(64)) {
                    auto boxFn = getTsValueMakeInt();
                    arg2 = builder_->CreateCall(boxFn, {arg2});
                } else if (arg2->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    arg2 = builder_->CreateCall(boxFn, {arg2});
                }
            } else if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[3])) {
                auto hirType = (*hirVal)->type;
                if (hirType && hirType->kind == HIRTypeKind::String) {
                    auto boxFn = getTsValueMakeString();
                    arg2 = builder_->CreateCall(boxFn, {arg2});
                }
            }
            llvm::FunctionType* callFt = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_2", callFt);
            result = builder_->CreateCall(callFt, callFn.getCallee(), { funcVal, thisArg, arg1, arg2 });
        } else if (argCount == 3) {
            llvm::Value* arg1 = getOperandValue(inst->operands[2]);
            llvm::Value* arg2 = getOperandValue(inst->operands[3]);
            llvm::Value* arg3 = getOperandValue(inst->operands[4]);
            // Box args if needed
            if (!arg1->getType()->isPointerTy()) {
                if (arg1->getType()->isIntegerTy(64)) {
                    auto boxFn = getTsValueMakeInt();
                    arg1 = builder_->CreateCall(boxFn, {arg1});
                } else if (arg1->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    arg1 = builder_->CreateCall(boxFn, {arg1});
                }
            } else if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[2])) {
                auto hirType = (*hirVal)->type;
                if (hirType && hirType->kind == HIRTypeKind::String) {
                    auto boxFn = getTsValueMakeString();
                    arg1 = builder_->CreateCall(boxFn, {arg1});
                }
            }
            if (!arg2->getType()->isPointerTy()) {
                if (arg2->getType()->isIntegerTy(64)) {
                    auto boxFn = getTsValueMakeInt();
                    arg2 = builder_->CreateCall(boxFn, {arg2});
                } else if (arg2->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    arg2 = builder_->CreateCall(boxFn, {arg2});
                }
            } else if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[3])) {
                auto hirType = (*hirVal)->type;
                if (hirType && hirType->kind == HIRTypeKind::String) {
                    auto boxFn = getTsValueMakeString();
                    arg2 = builder_->CreateCall(boxFn, {arg2});
                }
            }
            if (!arg3->getType()->isPointerTy()) {
                if (arg3->getType()->isIntegerTy(64)) {
                    auto boxFn = getTsValueMakeInt();
                    arg3 = builder_->CreateCall(boxFn, {arg3});
                } else if (arg3->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    arg3 = builder_->CreateCall(boxFn, {arg3});
                }
            } else if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[4])) {
                auto hirType = (*hirVal)->type;
                if (hirType && hirType->kind == HIRTypeKind::String) {
                    auto boxFn = getTsValueMakeString();
                    arg3 = builder_->CreateCall(boxFn, {arg3});
                }
            }
            llvm::FunctionType* callFt = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_3", callFt);
            result = builder_->CreateCall(callFt, callFn.getCallee(), { funcVal, thisArg, arg1, arg2, arg3 });
        } else if (argCount == 4) {
            llvm::Value* arg1 = getOperandValue(inst->operands[2]);
            llvm::Value* arg2 = getOperandValue(inst->operands[3]);
            llvm::Value* arg3 = getOperandValue(inst->operands[4]);
            llvm::Value* arg4 = getOperandValue(inst->operands[5]);
            // Box args if needed
            auto boxArg = [&](llvm::Value* arg, size_t operandIdx) -> llvm::Value* {
                if (!arg->getType()->isPointerTy()) {
                    if (arg->getType()->isIntegerTy(64)) {
                        auto boxFn = getTsValueMakeInt();
                        return builder_->CreateCall(boxFn, {arg});
                    } else if (arg->getType()->isDoubleTy()) {
                        auto boxFn = getTsValueMakeDouble();
                        return builder_->CreateCall(boxFn, {arg});
                    }
                } else if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[operandIdx])) {
                    auto hirType = (*hirVal)->type;
                    if (hirType && hirType->kind == HIRTypeKind::String) {
                        auto boxFn = getTsValueMakeString();
                        return builder_->CreateCall(boxFn, {arg});
                    }
                }
                return arg;
            };
            arg1 = boxArg(arg1, 2);
            arg2 = boxArg(arg2, 3);
            arg3 = boxArg(arg3, 4);
            arg4 = boxArg(arg4, 5);
            llvm::FunctionType* callFt = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_4", callFt);
            result = builder_->CreateCall(callFt, callFn.getCallee(), { funcVal, thisArg, arg1, arg2, arg3, arg4 });
        } else if (argCount == 5) {
            llvm::Value* arg1 = getOperandValue(inst->operands[2]);
            llvm::Value* arg2 = getOperandValue(inst->operands[3]);
            llvm::Value* arg3 = getOperandValue(inst->operands[4]);
            llvm::Value* arg4 = getOperandValue(inst->operands[5]);
            llvm::Value* arg5 = getOperandValue(inst->operands[6]);
            // Box args if needed
            auto boxArg = [&](llvm::Value* arg, size_t operandIdx) -> llvm::Value* {
                if (!arg->getType()->isPointerTy()) {
                    if (arg->getType()->isIntegerTy(64)) {
                        auto boxFn = getTsValueMakeInt();
                        return builder_->CreateCall(boxFn, {arg});
                    } else if (arg->getType()->isDoubleTy()) {
                        auto boxFn = getTsValueMakeDouble();
                        return builder_->CreateCall(boxFn, {arg});
                    }
                } else if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[operandIdx])) {
                    auto hirType = (*hirVal)->type;
                    if (hirType && hirType->kind == HIRTypeKind::String) {
                        auto boxFn = getTsValueMakeString();
                        return builder_->CreateCall(boxFn, {arg});
                    }
                }
                return arg;
            };
            arg1 = boxArg(arg1, 2);
            arg2 = boxArg(arg2, 3);
            arg3 = boxArg(arg3, 4);
            arg4 = boxArg(arg4, 5);
            arg5 = boxArg(arg5, 6);
            llvm::FunctionType* callFt = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_5", callFt);
            result = builder_->CreateCall(callFt, callFn.getCallee(), { funcVal, thisArg, arg1, arg2, arg3, arg4, arg5 });
        } else if (argCount == 6) {
            llvm::Value* arg1 = getOperandValue(inst->operands[2]);
            llvm::Value* arg2 = getOperandValue(inst->operands[3]);
            llvm::Value* arg3 = getOperandValue(inst->operands[4]);
            llvm::Value* arg4 = getOperandValue(inst->operands[5]);
            llvm::Value* arg5 = getOperandValue(inst->operands[6]);
            llvm::Value* arg6 = getOperandValue(inst->operands[7]);
            // Box args if needed
            auto boxArg = [&](llvm::Value* arg, size_t operandIdx) -> llvm::Value* {
                if (!arg->getType()->isPointerTy()) {
                    if (arg->getType()->isIntegerTy(64)) {
                        auto boxFn = getTsValueMakeInt();
                        return builder_->CreateCall(boxFn, {arg});
                    } else if (arg->getType()->isDoubleTy()) {
                        auto boxFn = getTsValueMakeDouble();
                        return builder_->CreateCall(boxFn, {arg});
                    }
                } else if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[operandIdx])) {
                    auto hirType = (*hirVal)->type;
                    if (hirType && hirType->kind == HIRTypeKind::String) {
                        auto boxFn = getTsValueMakeString();
                        return builder_->CreateCall(boxFn, {arg});
                    }
                }
                return arg;
            };
            arg1 = boxArg(arg1, 2);
            arg2 = boxArg(arg2, 3);
            arg3 = boxArg(arg3, 4);
            arg4 = boxArg(arg4, 5);
            arg5 = boxArg(arg5, 6);
            arg6 = boxArg(arg6, 7);
            llvm::FunctionType* callFt = llvm::FunctionType::get(
                builder_->getPtrTy(),
                { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
                false);
            llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_6", callFt);
            result = builder_->CreateCall(callFt, callFn.getCallee(), { funcVal, thisArg, arg1, arg2, arg3, arg4, arg5, arg6 });
        } else {
            // For more than 6 arguments, fall back to null (we'd need ts_function_call_with_this with array)
            SPDLOG_WARN("CallMethod with {} args not fully implemented: {}", argCount, methodName);
            result = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        }

        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Generic method call - fall back to dynamic dispatch via property access
    // 1. Get the property from the object (returns a boxed function)
    // 2. Box the object as 'this'
    // 3. Call ts_call_with_this_N with the function, this, and args

    // Create global constant for the method name string
    llvm::Constant* methodNameStr = builder_->CreateGlobalStringPtr(methodName, ".methodname");

    // Call ts_object_get_property(obj, methodName) to get the function
    llvm::FunctionType* getPropFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy(), builder_->getPtrTy() },
        false);
    llvm::FunctionCallee getPropFn = module_->getOrInsertFunction("ts_object_get_property", getPropFt);
    llvm::Value* func = builder_->CreateCall(getPropFt, getPropFn.getCallee(), { obj, methodNameStr });

    // Box the object as 'thisArg' using ts_value_make_object
    llvm::FunctionType* boxObjFt = llvm::FunctionType::get(
        builder_->getPtrTy(), { builder_->getPtrTy() }, false);
    llvm::FunctionCallee boxObjFn = module_->getOrInsertFunction("ts_value_make_object", boxObjFt);
    llvm::Value* thisArg = builder_->CreateCall(boxObjFt, boxObjFn.getCallee(), { obj });

    // Collect and box arguments
    std::vector<llvm::Value*> callArgs;
    callArgs.push_back(func);
    callArgs.push_back(thisArg);

    size_t argCount = inst->operands.size() - 2; // Subtract obj and methodName
    for (size_t i = 2; i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);
        // Box the argument if needed
        if (!arg->getType()->isPointerTy()) {
            if (arg->getType()->isIntegerTy(64)) {
                auto boxFn = getTsValueMakeInt();
                arg = builder_->CreateCall(boxFn, {arg});
            } else if (arg->getType()->isDoubleTy()) {
                auto boxFn = getTsValueMakeDouble();
                arg = builder_->CreateCall(boxFn, {arg});
            } else if (arg->getType()->isIntegerTy(1)) {
                auto boxFn = getTsValueMakeBool();
                llvm::Value* extended = builder_->CreateZExt(arg, builder_->getInt32Ty());
                arg = builder_->CreateCall(boxFn, {extended});
            }
        } else {
            // Check HIR type for string boxing
            if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[i])) {
                auto hirType = (*hirVal)->type;
                if (hirType && hirType->kind == HIRTypeKind::String) {
                    auto boxFn = getTsValueMakeString();
                    arg = builder_->CreateCall(boxFn, {arg});
                }
            }
        }
        callArgs.push_back(arg);
    }

    // Call the appropriate ts_call_with_this_N function based on argument count
    llvm::Value* result;
    if (argCount == 0) {
        llvm::FunctionType* callFt = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_0", callFt);
        result = builder_->CreateCall(callFt, callFn.getCallee(), { func, thisArg });
    } else if (argCount == 1) {
        llvm::FunctionType* callFt = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_1", callFt);
        result = builder_->CreateCall(callFt, callFn.getCallee(), { func, thisArg, callArgs[2] });
    } else if (argCount == 2) {
        llvm::FunctionType* callFt = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_2", callFt);
        result = builder_->CreateCall(callFt, callFn.getCallee(), { func, thisArg, callArgs[2], callArgs[3] });
    } else if (argCount == 3) {
        llvm::FunctionType* callFt = llvm::FunctionType::get(
            builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() },
            false);
        llvm::FunctionCallee callFn = module_->getOrInsertFunction("ts_call_with_this_3", callFt);
        result = builder_->CreateCall(callFt, callFn.getCallee(), { func, thisArg, callArgs[2], callArgs[3], callArgs[4] });
    } else {
        // For more arguments, we'd need ts_function_call_with_this with array
        SPDLOG_WARN("Generic method call with {} args not fully implemented: {}", argCount, methodName);
        result = llvm::ConstantPointerNull::get(builder_->getPtrTy());
    }

    if (inst->result) {
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerCallVirtual(HIRInstruction* inst) {
    // CallVirtual: %r = call_virtual %obj, <vtable_idx>, (%args...)
    // Used for virtual method dispatch through an object's vtable
    //
    // TsObject layout (with C++ virtual inheritance):
    //   offset 0: C++ vtable pointer (implicit from virtual methods)
    //   offset 8: TsObject::vtable (explicit vtable for custom dispatch)
    //   offset 16: TsObject::magic
    //
    // The explicit vtable is an array of function pointers:
    //   vtable[0] = parent vtable pointer (for inheritance)
    //   vtable[1+] = method function pointers

    llvm::Value* obj = getOperandValue(inst->operands[0]);
    int64_t vtableIdx = getOperandInt(inst->operands[1]);

    // Load the explicit vtable pointer from offset 8 (TsObject::vtable)
    // GEP with i8 type to get byte offset
    llvm::Value* vtablePtrAddr = builder_->CreateGEP(
        builder_->getInt8Ty(),
        obj,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 8),
        "vtable_addr"
    );
    llvm::Value* vtable = builder_->CreateLoad(builder_->getPtrTy(), vtablePtrAddr, "vtable");

    // Load the function pointer from vtable[vtableIdx]
    // vtable is void** so each entry is 8 bytes
    llvm::Value* funcPtrAddr = builder_->CreateGEP(
        builder_->getPtrTy(),
        vtable,
        llvm::ConstantInt::get(builder_->getInt64Ty(), vtableIdx),
        "func_ptr_addr"
    );
    llvm::Value* funcPtr = builder_->CreateLoad(builder_->getPtrTy(), funcPtrAddr, "func_ptr");

    // Build argument list: object as 'this', then remaining arguments
    std::vector<llvm::Value*> args;
    args.push_back(obj);  // 'this' pointer
    for (size_t i = 2; i < inst->operands.size(); ++i) {
        args.push_back(getOperandValue(inst->operands[i]));
    }

    // Build function type: ptr (ptr, ptr, ...) - all pointers
    std::vector<llvm::Type*> paramTypes(args.size(), builder_->getPtrTy());
    llvm::FunctionType* ft = llvm::FunctionType::get(builder_->getPtrTy(), paramTypes, false);

    // Call the virtual function
    llvm::Value* result = builder_->CreateCall(ft, funcPtr, args, "vcall_result");

    if (inst->result) {
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerCallIndirect(HIRInstruction* inst) {
    // CallIndirect is used to call closures or function pointers dynamically
    // We use ts_call_N runtime functions which handle different function types:
    // - TsClosure: passes closure as context
    // - TsFunction: handles compiled functions with various signatures
    //
    // Operand 0: closure/function pointer (boxed TsValue*)
    // Operand 1+: regular arguments (boxed TsValue*)

    llvm::Value* callablePtr = getOperandValue(inst->operands[0]);

    // Gather regular arguments - ensure they're boxed for the ts_call_N interface
    std::vector<llvm::Value*> regularArgs;
    for (size_t i = 1; i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);
        // Box the argument if it's not already a pointer
        if (!arg->getType()->isPointerTy()) {
            if (arg->getType()->isDoubleTy()) {
                auto boxFt = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
                auto boxFn = module_->getOrInsertFunction("ts_value_make_double", boxFt);
                arg = builder_->CreateCall(boxFt, boxFn.getCallee(), { arg });
            } else if (arg->getType()->isIntegerTy(64)) {
                auto boxFt = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
                auto boxFn = module_->getOrInsertFunction("ts_value_make_int", boxFt);
                arg = builder_->CreateCall(boxFt, boxFn.getCallee(), { arg });
            } else if (arg->getType()->isIntegerTy(1)) {
                // Convert i1 to i64 for boxing
                llvm::Value* extended = builder_->CreateZExt(arg, builder_->getInt64Ty());
                auto boxFt = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
                auto boxFn = module_->getOrInsertFunction("ts_value_make_bool", boxFt);
                arg = builder_->CreateCall(boxFt, boxFn.getCallee(), { extended });
            }
        }
        regularArgs.push_back(arg);
    }

    // Use ts_call_N based on argument count
    // All ts_call_N functions take boxed TsValue* arguments and return TsValue*
    llvm::Value* result = nullptr;
    size_t argCount = regularArgs.size();

    if (argCount == 0) {
        auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getPtrTy() }, false);
        auto fn = module_->getOrInsertFunction("ts_call_0", ft);
        result = builder_->CreateCall(ft, fn.getCallee(), { callablePtr });
    } else if (argCount == 1) {
        auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
        auto fn = module_->getOrInsertFunction("ts_call_1", ft);
        result = builder_->CreateCall(ft, fn.getCallee(), { callablePtr, regularArgs[0] });
    } else if (argCount == 2) {
        auto ft = llvm::FunctionType::get(builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() }, false);
        auto fn = module_->getOrInsertFunction("ts_call_2", ft);
        result = builder_->CreateCall(ft, fn.getCallee(), { callablePtr, regularArgs[0], regularArgs[1] });
    } else if (argCount == 3) {
        auto ft = llvm::FunctionType::get(builder_->getPtrTy(),
            { builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy() }, false);
        auto fn = module_->getOrInsertFunction("ts_call_3", ft);
        result = builder_->CreateCall(ft, fn.getCallee(), { callablePtr, regularArgs[0], regularArgs[1], regularArgs[2] });
    } else {
        // For more arguments, fall back to ts_call_N with array
        // TODO: Implement ts_call_n(callable, argc, argv) for >3 args
        auto ft = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getPtrTy() }, false);
        auto fn = module_->getOrInsertFunction("ts_call_0", ft);
        result = builder_->CreateCall(ft, fn.getCallee(), { callablePtr });
    }

    // ts_call_N returns a boxed TsValue*
    // Note: HIR pipeline tracks boxing through HIR types, not a separate set

    if (inst->result) {
        setValue(inst->result, result);
    }
}

//==============================================================================
// Globals
//==============================================================================

void HIRToLLVM::lowerLoadGlobal(HIRInstruction* inst) {
    std::string globalName = getOperandString(inst->operands[0]);

    // For known globals, we return a sentinel pointer that the runtime recognizes
    // or call a runtime function to get the global object
    llvm::Value* result = nullptr;

    // For now, we use runtime functions for each global
    // These return a pointer to the global object
    std::string funcName = "ts_get_global_" + globalName;

    // Check for known globals and use specific runtime functions
    if (globalName == "console") {
        funcName = "ts_get_global_console";
    } else if (globalName == "Math") {
        funcName = "ts_get_global_Math";
    } else if (globalName == "JSON") {
        funcName = "ts_get_global_JSON";
    } else if (globalName == "Object") {
        funcName = "ts_get_global_Object";
    } else if (globalName == "Array") {
        funcName = "ts_get_global_Array";
    } else if (globalName == "String") {
        funcName = "ts_get_global_String";
    } else if (globalName == "Number") {
        funcName = "ts_get_global_Number";
    } else if (globalName == "Boolean") {
        funcName = "ts_get_global_Boolean";
    } else if (globalName == "Date") {
        funcName = "ts_get_global_Date";
    } else if (globalName == "RegExp") {
        funcName = "ts_get_global_RegExp";
    } else if (globalName == "Promise") {
        funcName = "ts_get_global_Promise";
    } else if (globalName == "Error") {
        funcName = "ts_get_global_Error";
    } else if (globalName == "Buffer") {
        funcName = "ts_get_global_Buffer";
    } else if (globalName == "process") {
        funcName = "ts_get_global_process";
    } else if (globalName == "global" || globalName == "globalThis") {
        funcName = "ts_get_global_globalThis";
    } else if (globalName == "path") {
        // path module - return a sentinel that lowerCallMethod will recognize
        // We use the global name as a marker - the runtime doesn't need an actual object
        funcName = "ts_get_global_path";
    } else if (globalName == "fs") {
        funcName = "ts_get_global_fs";
    } else if (globalName == "os") {
        funcName = "ts_get_global_os";
    } else if (globalName == "url") {
        funcName = "ts_get_global_url";
    } else if (globalName == "util") {
        funcName = "ts_get_global_util";
    } else if (globalName == "crypto") {
        funcName = "ts_get_global_crypto";
    } else if (globalName == "http") {
        funcName = "ts_get_global_http";
    } else if (globalName == "https") {
        funcName = "ts_get_global_https";
    } else if (globalName == "net") {
        funcName = "ts_get_global_net";
    } else if (globalName == "dgram") {
        funcName = "ts_get_global_dgram";
    } else if (globalName == "dns") {
        funcName = "ts_get_global_dns";
    } else if (globalName == "tls") {
        funcName = "ts_get_global_tls";
    } else if (globalName == "zlib") {
        funcName = "ts_get_global_zlib";
    } else if (globalName == "stream") {
        funcName = "ts_get_global_stream";
    } else if (globalName == "events") {
        funcName = "ts_get_global_events";
    } else if (globalName == "querystring") {
        funcName = "ts_get_global_querystring";
    } else if (globalName == "assert") {
        funcName = "ts_get_global_assert";
    } else if (globalName == "child_process") {
        funcName = "ts_get_global_child_process";
    } else if (globalName.find("_VTable_Global") != std::string::npos) {
        // VTable globals are LLVM globals, not runtime globals
        llvm::GlobalVariable* vtableGlobal = module_->getGlobalVariable(globalName);
        if (vtableGlobal) {
            result = vtableGlobal;
        } else {
            // VTable doesn't exist yet - return null
            result = llvm::ConstantPointerNull::get(builder_->getPtrTy());
        }
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    } else {
        // Generic global lookup
        funcName = "ts_get_global";
        // For generic globals, pass the name as an argument
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* nameStr = createGlobalString(globalName);
        result = builder_->CreateCall(ft, fn.getCallee(), { nameStr });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // For known globals, call the specific function (no args)
    llvm::FunctionType* ft = llvm::FunctionType::get(builder_->getPtrTy(), false);
    llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
    result = builder_->CreateCall(ft, fn.getCallee());

    if (inst->result) {
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerLoadFunction(HIRInstruction* inst) {
    std::string funcName = getOperandString(inst->operands[0]);

    // Look up the function in the LLVM module
    llvm::Function* fn = module_->getFunction(funcName);
    if (fn) {
        if (inst->result) {
            setValue(inst->result, fn);
        }
    } else {
        // Function not found - create a null pointer
        SPDLOG_WARN("LoadFunction: function '{}' not found", funcName);
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
    }
}

//==============================================================================
// Function Trampolines
//==============================================================================

llvm::Function* HIRToLLVM::getOrCreateTrampoline(llvm::Function* originalFunc) {
    if (!originalFunc) return nullptr;

    // Check if we already created a trampoline for this function
    std::string trampolineName = originalFunc->getName().str() + "$trampoline";
    if (llvm::Function* existing = module_->getFunction(trampolineName)) {
        return existing;
    }

    llvm::FunctionType* origFT = originalFunc->getFunctionType();
    unsigned numOrigParams = origFT->getNumParams();

    // Check if the function already matches the closure convention:
    // (ptr context, TsValue* args...) -> ptr
    // A function matches if: first param is ptr (context), all other params are ptr (TsValue*), returns ptr
    bool alreadyMatches = origFT->getReturnType()->isPointerTy();
    if (alreadyMatches && numOrigParams >= 1) {
        // First param must be context pointer
        alreadyMatches = origFT->getParamType(0)->isPointerTy();
        // All other params must be pointers (TsValue*)
        for (unsigned i = 1; i < numOrigParams && alreadyMatches; ++i) {
            alreadyMatches = origFT->getParamType(i)->isPointerTy();
        }
    }
    if (alreadyMatches && numOrigParams >= 1) {
        // No trampoline needed, function already has closure convention
        return originalFunc;
    }

    // Determine how many user-visible parameters the original function has
    // (i.e., parameters that aren't leading context pointers)
    // For closures, the first N pointer parameters are closure contexts,
    // and the remaining parameters are actual user parameters.
    unsigned numContextParams = 0;
    for (unsigned i = 0; i < numOrigParams; ++i) {
        if (origFT->getParamType(i)->isPointerTy()) {
            numContextParams++;
        } else {
            // First non-pointer param marks start of user params
            break;
        }
    }
    unsigned numUserParams = numOrigParams - numContextParams;

    SPDLOG_DEBUG("getOrCreateTrampoline: {} has {} total params, {} context params, {} user params",
                 originalFunc->getName().str(), numOrigParams, numContextParams, numUserParams);

    // Create trampoline: (ptr %ctx, TsValue* %arg1, TsValue* %arg2, ...) -> ptr
    // The trampoline accepts boxed arguments and returns a boxed result
    std::vector<llvm::Type*> trampolineParams;
    trampolineParams.push_back(builder_->getPtrTy());  // context
    for (unsigned i = 0; i < numUserParams; ++i) {
        trampolineParams.push_back(builder_->getPtrTy());  // TsValue* for each arg
    }

    llvm::FunctionType* trampolineFT = llvm::FunctionType::get(
        builder_->getPtrTy(),
        trampolineParams,
        false
    );

    llvm::Function* trampoline = llvm::Function::Create(
        trampolineFT,
        llvm::GlobalValue::PrivateLinkage,
        trampolineName,
        module_.get()
    );

    SPDLOG_DEBUG("getOrCreateTrampoline: creating trampoline {} for {} with {} user params",
                 trampolineName, originalFunc->getName().str(), numUserParams);

    // Save current insertion point
    llvm::BasicBlock* savedBB = builder_->GetInsertBlock();
    llvm::BasicBlock::iterator savedPt = builder_->GetInsertPoint();

    // Create entry block for trampoline
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "entry", trampoline);
    builder_->SetInsertPoint(entryBB);

    // Build arguments for the original function call
    std::vector<llvm::Value*> callArgs;

    // Get trampoline arguments iterator
    auto trampolineArg = trampoline->arg_begin();
    llvm::Value* ctxArg = trampolineArg++;  // Skip context argument

    // First, pass the context to all context parameters of the original function
    for (unsigned i = 0; i < numContextParams; ++i) {
        callArgs.push_back(ctxArg);
    }

    // Unbox each user argument and add to callArgs
    for (unsigned i = 0; i < numUserParams; ++i) {
        llvm::Value* boxedArg = trampolineArg++;
        llvm::Type* expectedType = origFT->getParamType(numContextParams + i);

        llvm::Value* unboxedArg;
        if (expectedType->isDoubleTy()) {
            // Unbox to double
            auto unboxFT = llvm::FunctionType::get(builder_->getDoubleTy(), { builder_->getPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_double", unboxFT);
            unboxedArg = builder_->CreateCall(unboxFT, unboxFn.getCallee(), { boxedArg });
        } else if (expectedType->isIntegerTy(64)) {
            // Unbox to i64
            auto unboxFT = llvm::FunctionType::get(builder_->getInt64Ty(), { builder_->getPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_int", unboxFT);
            unboxedArg = builder_->CreateCall(unboxFT, unboxFn.getCallee(), { boxedArg });
        } else if (expectedType->isIntegerTy(1)) {
            // Unbox to bool
            auto unboxFT = llvm::FunctionType::get(builder_->getInt64Ty(), { builder_->getPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFT);
            llvm::Value* boolAsInt = builder_->CreateCall(unboxFT, unboxFn.getCallee(), { boxedArg });
            unboxedArg = builder_->CreateICmpNE(boolAsInt, llvm::ConstantInt::get(builder_->getInt64Ty(), 0));
        } else if (expectedType->isPointerTy()) {
            // Unbox to object pointer
            auto unboxFT = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_object", unboxFT);
            unboxedArg = builder_->CreateCall(unboxFT, unboxFn.getCallee(), { boxedArg });
        } else {
            // Unknown type, pass through as-is (hope it's a pointer)
            unboxedArg = boxedArg;
        }
        callArgs.push_back(unboxedArg);
    }

    // Call the original function
    llvm::Value* result = builder_->CreateCall(origFT, originalFunc, callArgs);

    // Box the result based on return type
    llvm::Value* boxedResult;
    llvm::Type* returnType = origFT->getReturnType();

    if (returnType->isPointerTy()) {
        // Already a pointer, might be TsValue* or raw object
        boxedResult = result;
    } else if (returnType->isDoubleTy()) {
        auto boxFT = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getDoubleTy() }, false);
        auto boxFn = module_->getOrInsertFunction("ts_value_make_double", boxFT);
        boxedResult = builder_->CreateCall(boxFT, boxFn.getCallee(), { result });
    } else if (returnType->isIntegerTy(64)) {
        auto boxFT = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
        auto boxFn = module_->getOrInsertFunction("ts_value_make_int", boxFT);
        boxedResult = builder_->CreateCall(boxFT, boxFn.getCallee(), { result });
    } else if (returnType->isIntegerTy(1)) {
        llvm::Value* extended = builder_->CreateZExt(result, builder_->getInt64Ty());
        auto boxFT = llvm::FunctionType::get(builder_->getPtrTy(), { builder_->getInt64Ty() }, false);
        auto boxFn = module_->getOrInsertFunction("ts_value_make_bool", boxFT);
        boxedResult = builder_->CreateCall(boxFT, boxFn.getCallee(), { extended });
    } else if (returnType->isVoidTy()) {
        auto undefFT = llvm::FunctionType::get(builder_->getPtrTy(), {}, false);
        auto undefFn = module_->getOrInsertFunction("ts_value_make_undefined", undefFT);
        boxedResult = builder_->CreateCall(undefFT, undefFn.getCallee(), {});
    } else {
        // Fallback: return undefined
        auto undefFT = llvm::FunctionType::get(builder_->getPtrTy(), {}, false);
        auto undefFn = module_->getOrInsertFunction("ts_value_make_undefined", undefFT);
        boxedResult = builder_->CreateCall(undefFT, undefFn.getCallee(), {});
    }

    builder_->CreateRet(boxedResult);

    // Restore insertion point
    if (savedBB) {
        builder_->SetInsertPoint(savedBB, savedPt);
    }

    return trampoline;
}

//==============================================================================
// Closures
//==============================================================================

void HIRToLLVM::lowerMakeClosure(HIRInstruction* inst) {
    // MakeClosure creates a closure object with function pointer and captured values
    // Operand 0: function name
    // Operand 1+: captured values (to be stored in TsCells)
    //
    // Runtime: TsClosure = { func_ptr, num_captures, TsCell** cells }

    std::string funcName = getOperandString(inst->operands[0]);
    llvm::Function* fn = module_->getFunction(funcName);

    if (!fn) {
        SPDLOG_WARN("MakeClosure: function '{}' not found", funcName);
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
        return;
    }

    size_t numCaptures = inst->operands.size() - 1;

    // Declare runtime functions
    // ts_closure_create(void* funcPtr, int64_t numCaptures) -> TsClosure*
    auto closureCreateFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto closureCreate = module_->getOrInsertFunction("ts_closure_create", closureCreateFt);

    // ts_closure_init_capture(TsClosure* closure, int64_t index, TsValue* initialValue) -> void
    auto initCaptureFt = llvm::FunctionType::get(
        builder_->getVoidTy(),
        { builder_->getPtrTy(), builder_->getInt64Ty(), builder_->getPtrTy() },
        false
    );
    auto initCapture = module_->getOrInsertFunction("ts_closure_init_capture", initCaptureFt);

    // ts_value_make_int(int64_t) -> TsValue*
    auto makeIntFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getInt64Ty() },
        false
    );
    auto makeInt = module_->getOrInsertFunction("ts_value_make_int", makeIntFt);

    // ts_value_make_double(double) -> TsValue*
    auto makeDoubleFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getDoubleTy() },
        false
    );
    auto makeDouble = module_->getOrInsertFunction("ts_value_make_double", makeDoubleFt);

    // ts_value_make_object(void*) -> TsValue*
    auto makeObjectFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy() },
        false
    );
    auto makeObject = module_->getOrInsertFunction("ts_value_make_object", makeObjectFt);

    // Create the closure: ts_closure_create(funcPtr, numCaptures)
    // The function needs a trampoline to convert from native calling convention
    // to closure calling convention: (ptr ctx, TsValue* arg1, ...) -> TsValue*
    llvm::Function* trampolineFunc = getOrCreateTrampoline(fn);
    llvm::Value* funcPtrToUse = trampolineFunc ? (llvm::Value*)trampolineFunc : (llvm::Value*)fn;

    llvm::Value* numCapturesVal = llvm::ConstantInt::get(builder_->getInt64Ty(), numCaptures);
    llvm::Value* closure = builder_->CreateCall(closureCreateFt, closureCreate.getCallee(), { funcPtrToUse, numCapturesVal });

    // Initialize each capture cell with its value
    for (size_t i = 0; i < numCaptures; ++i) {
        llvm::Value* capturedValue = getOperandValue(inst->operands[i + 1]);
        llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), i);

        // Get the type of the captured value to box it properly
        // Use LLVM type as the primary source of truth
        llvm::Value* boxedValue = nullptr;
        llvm::Type* valType = capturedValue->getType();

        if (valType->isIntegerTy(64)) {
            boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { capturedValue });
        } else if (valType->isDoubleTy()) {
            boxedValue = builder_->CreateCall(makeDoubleFt, makeDouble.getCallee(), { capturedValue });
        } else if (valType->isIntegerTy(1)) {
            // Boolean - use ts_value_make_bool with i32
            auto makeBool = getTsValueMakeBool();
            llvm::Value* extended = builder_->CreateZExt(capturedValue, builder_->getInt32Ty(), "bool_ext");
            boxedValue = builder_->CreateCall(makeBool, { extended });
        } else if (valType->isIntegerTy(32)) {
            // i32 - extend to i64 and use makeInt
            llvm::Value* extended = builder_->CreateSExt(capturedValue, builder_->getInt64Ty(), "i32_ext");
            boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { extended });
        } else if (valType->isPointerTy()) {
            // For pointers/objects, box as object
            boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { capturedValue });
        } else {
            // Default: box as object (may fail for non-pointer types)
            boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { capturedValue });
        }

        // Initialize the capture: ts_closure_init_capture(closure, index, boxedValue)
        builder_->CreateCall(initCaptureFt, initCapture.getCallee(), { closure, indexVal, boxedValue });
    }

    if (inst->result) {
        setValue(inst->result, closure);
    }
}

void HIRToLLVM::lowerLoadCapture(HIRInstruction* inst) {
    // LoadCapture loads a captured variable from the closure environment
    // Operand 0: variable name
    //
    // The closure is passed as a hidden first parameter (closureParam_)
    // We look up the index of the variable in currentHIRFunction_->captures
    // Then get the cell at that index and extract the value

    std::string varName = getOperandString(inst->operands[0]);

    if (!closureParam_) {
        SPDLOG_ERROR("LoadCapture '{}': no closure parameter available", varName);
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
        return;
    }

    // Find the index of this capture in the function's captures list
    int64_t captureIndex = -1;
    for (size_t i = 0; i < currentHIRFunction_->captures.size(); ++i) {
        if (currentHIRFunction_->captures[i].first == varName) {
            captureIndex = static_cast<int64_t>(i);
            break;
        }
    }

    if (captureIndex < 0) {
        SPDLOG_ERROR("LoadCapture: variable '{}' not found in captures list", varName);
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
        return;
    }

    // Declare runtime functions
    // ts_closure_get_cell(TsClosure* closure, int64_t index) -> TsCell*
    auto getCellFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto getCell = module_->getOrInsertFunction("ts_closure_get_cell", getCellFt);

    // ts_cell_get(TsCell* cell) -> TsValue*
    auto cellGetFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy() },
        false
    );
    auto cellGet = module_->getOrInsertFunction("ts_cell_get", cellGetFt);

    // ts_value_get_int(TsValue*) -> int64_t
    auto getIntFt = llvm::FunctionType::get(
        builder_->getInt64Ty(),
        { builder_->getPtrTy() },
        false
    );
    auto getInt = module_->getOrInsertFunction("ts_value_get_int", getIntFt);

    // ts_value_get_double(TsValue*) -> double
    auto getDoubleFt = llvm::FunctionType::get(
        builder_->getDoubleTy(),
        { builder_->getPtrTy() },
        false
    );
    auto getDouble = module_->getOrInsertFunction("ts_value_get_double", getDoubleFt);

    // ts_value_get_object(TsValue*) -> void*
    auto getObjectFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy() },
        false
    );
    auto getObject = module_->getOrInsertFunction("ts_value_get_object", getObjectFt);

    // Get the cell: cell = ts_closure_get_cell(closure, index)
    llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), captureIndex);
    llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(), { closureParam_, indexVal });

    // Get the boxed value from the cell: boxedValue = ts_cell_get(cell)
    llvm::Value* boxedValue = builder_->CreateCall(cellGetFt, cellGet.getCallee(), { cell });

    // Unbox based on the expected type
    llvm::Value* result = nullptr;
    if (inst->result && inst->result->type) {
        HIRTypeKind kind = inst->result->type->kind;
        if (kind == HIRTypeKind::Int64) {
            result = builder_->CreateCall(getIntFt, getInt.getCallee(), { boxedValue });
        } else if (kind == HIRTypeKind::Float64) {
            result = builder_->CreateCall(getDoubleFt, getDouble.getCallee(), { boxedValue });
        } else {
            // For pointers/objects, use ts_value_get_object
            result = builder_->CreateCall(getObjectFt, getObject.getCallee(), { boxedValue });
        }
    } else {
        // Default: return the boxed value as-is
        result = boxedValue;
    }

    if (inst->result) {
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerStoreCapture(HIRInstruction* inst) {
    // StoreCapture stores a value to a captured variable in the closure environment
    // Operand 0: variable name
    // Operand 1: value to store
    //
    // The closure is passed as a hidden first parameter (closureParam_)
    // We look up the index of the variable in currentHIRFunction_->captures
    // Then get the cell at that index and store the value

    std::string varName = getOperandString(inst->operands[0]);
    llvm::Value* valueToStore = getOperandValue(inst->operands[1]);

    if (!closureParam_) {
        SPDLOG_ERROR("StoreCapture '{}': no closure parameter available", varName);
        return;
    }

    // Find the index of this capture in the function's captures list
    int64_t captureIndex = -1;
    std::shared_ptr<HIRType> captureType = nullptr;
    for (size_t i = 0; i < currentHIRFunction_->captures.size(); ++i) {
        if (currentHIRFunction_->captures[i].first == varName) {
            captureIndex = static_cast<int64_t>(i);
            captureType = currentHIRFunction_->captures[i].second;
            break;
        }
    }

    if (captureIndex < 0) {
        SPDLOG_ERROR("StoreCapture: variable '{}' not found in captures list", varName);
        return;
    }

    // Declare runtime functions
    // ts_closure_get_cell(TsClosure* closure, int64_t index) -> TsCell*
    auto getCellFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto getCell = module_->getOrInsertFunction("ts_closure_get_cell", getCellFt);

    // ts_cell_set(TsCell* cell, TsValue* value) -> void
    auto cellSetFt = llvm::FunctionType::get(
        builder_->getVoidTy(),
        { builder_->getPtrTy(), builder_->getPtrTy() },
        false
    );
    auto cellSet = module_->getOrInsertFunction("ts_cell_set", cellSetFt);

    // ts_value_make_int(int64_t) -> TsValue*
    auto makeIntFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getInt64Ty() },
        false
    );
    auto makeInt = module_->getOrInsertFunction("ts_value_make_int", makeIntFt);

    // ts_value_make_double(double) -> TsValue*
    auto makeDoubleFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getDoubleTy() },
        false
    );
    auto makeDouble = module_->getOrInsertFunction("ts_value_make_double", makeDoubleFt);

    // ts_value_make_object(void*) -> TsValue*
    auto makeObjectFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy() },
        false
    );
    auto makeObject = module_->getOrInsertFunction("ts_value_make_object", makeObjectFt);

    // Get the cell: cell = ts_closure_get_cell(closure, index)
    llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), captureIndex);
    llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(), { closureParam_, indexVal });

    // Box the value based on its LLVM type
    llvm::Value* boxedValue = nullptr;
    llvm::Type* valType = valueToStore->getType();

    if (valType->isIntegerTy(64)) {
        boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { valueToStore });
    } else if (valType->isDoubleTy()) {
        boxedValue = builder_->CreateCall(makeDoubleFt, makeDouble.getCallee(), { valueToStore });
    } else if (valType->isIntegerTy(1)) {
        // Boolean - use ts_value_make_bool with i32
        auto makeBool = getTsValueMakeBool();
        llvm::Value* extended = builder_->CreateZExt(valueToStore, builder_->getInt32Ty(), "bool_ext");
        boxedValue = builder_->CreateCall(makeBool, { extended });
    } else if (valType->isIntegerTy(32)) {
        // i32 - extend to i64 and use makeInt
        llvm::Value* extended = builder_->CreateSExt(valueToStore, builder_->getInt64Ty(), "i32_ext");
        boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { extended });
    } else if (valType->isPointerTy()) {
        // For pointers/objects, box as object
        boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { valueToStore });
    } else {
        // Default: box as object (may fail for non-pointer types)
        boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { valueToStore });
    }

    // Store the value in the cell: ts_cell_set(cell, boxedValue)
    builder_->CreateCall(cellSetFt, cellSet.getCallee(), { cell, boxedValue });
}

void HIRToLLVM::lowerLoadCaptureFromClosure(HIRInstruction* inst) {
    // LoadCaptureFromClosure loads a captured variable from a specific closure
    // Operand 0: closure pointer (HIRValue)
    // Operand 1: capture index (int64_t)
    // Result: the loaded value

    llvm::Value* closurePtr = getOperandValue(inst->operands[0]);
    int64_t captureIndex = getOperandInt(inst->operands[1]);

    if (!closurePtr) {
        SPDLOG_ERROR("LoadCaptureFromClosure: closure pointer is null");
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
        }
        return;
    }

    // Declare runtime functions
    // ts_closure_get_cell(TsClosure* closure, int64_t index) -> TsCell*
    auto getCellFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto getCell = module_->getOrInsertFunction("ts_closure_get_cell", getCellFt);

    // ts_cell_get(TsCell* cell) -> TsValue*
    auto cellGetFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy() },
        false
    );
    auto cellGet = module_->getOrInsertFunction("ts_cell_get", cellGetFt);

    // ts_value_get_int(TsValue*) -> int64_t
    auto getIntFt = llvm::FunctionType::get(
        builder_->getInt64Ty(),
        { builder_->getPtrTy() },
        false
    );
    auto getInt = module_->getOrInsertFunction("ts_value_get_int", getIntFt);

    // ts_value_get_double(TsValue*) -> double
    auto getDoubleFt = llvm::FunctionType::get(
        builder_->getDoubleTy(),
        { builder_->getPtrTy() },
        false
    );
    auto getDouble = module_->getOrInsertFunction("ts_value_get_double", getDoubleFt);

    // ts_value_get_object(TsValue*) -> void*
    auto getObjectFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy() },
        false
    );
    auto getObject = module_->getOrInsertFunction("ts_value_get_object", getObjectFt);

    // Get the cell: cell = ts_closure_get_cell(closure, index)
    llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), captureIndex);
    llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(), { closurePtr, indexVal });

    // Get the boxed value from the cell: boxedValue = ts_cell_get(cell)
    llvm::Value* boxedValue = builder_->CreateCall(cellGetFt, cellGet.getCallee(), { cell });

    // Unbox based on the expected type
    llvm::Value* result = nullptr;
    if (inst->result && inst->result->type) {
        HIRTypeKind kind = inst->result->type->kind;
        if (kind == HIRTypeKind::Int64) {
            result = builder_->CreateCall(getIntFt, getInt.getCallee(), { boxedValue });
        } else if (kind == HIRTypeKind::Float64) {
            result = builder_->CreateCall(getDoubleFt, getDouble.getCallee(), { boxedValue });
        } else {
            // For pointers/objects, use ts_value_get_object
            result = builder_->CreateCall(getObjectFt, getObject.getCallee(), { boxedValue });
        }
    } else {
        // Default: return the boxed value as-is
        result = boxedValue;
    }

    if (inst->result) {
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerStoreCaptureFromClosure(HIRInstruction* inst) {
    // StoreCaptureFromClosure stores a value to a captured variable in a specific closure
    // Operand 0: closure pointer (HIRValue)
    // Operand 1: capture index (int64_t)
    // Operand 2: value to store (HIRValue)

    llvm::Value* closurePtr = getOperandValue(inst->operands[0]);
    int64_t captureIndex = getOperandInt(inst->operands[1]);
    llvm::Value* valueToStore = getOperandValue(inst->operands[2]);

    if (!closurePtr) {
        SPDLOG_ERROR("StoreCaptureFromClosure: closure pointer is null");
        return;
    }

    // Declare runtime functions
    // ts_closure_get_cell(TsClosure* closure, int64_t index) -> TsCell*
    auto getCellFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto getCell = module_->getOrInsertFunction("ts_closure_get_cell", getCellFt);

    // ts_cell_set(TsCell* cell, TsValue* value) -> void
    auto cellSetFt = llvm::FunctionType::get(
        builder_->getVoidTy(),
        { builder_->getPtrTy(), builder_->getPtrTy() },
        false
    );
    auto cellSet = module_->getOrInsertFunction("ts_cell_set", cellSetFt);

    // ts_value_make_int(int64_t) -> TsValue*
    auto makeIntFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getInt64Ty() },
        false
    );
    auto makeInt = module_->getOrInsertFunction("ts_value_make_int", makeIntFt);

    // ts_value_make_double(double) -> TsValue*
    auto makeDoubleFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getDoubleTy() },
        false
    );
    auto makeDouble = module_->getOrInsertFunction("ts_value_make_double", makeDoubleFt);

    // ts_value_make_object(void*) -> TsValue*
    auto makeObjectFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy() },
        false
    );
    auto makeObject = module_->getOrInsertFunction("ts_value_make_object", makeObjectFt);

    // Get the cell: cell = ts_closure_get_cell(closure, index)
    llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), captureIndex);
    llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(), { closurePtr, indexVal });

    // Box the value based on its LLVM type
    llvm::Value* boxedValue = nullptr;
    llvm::Type* valType = valueToStore->getType();

    if (valType->isIntegerTy(64)) {
        boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { valueToStore });
    } else if (valType->isDoubleTy()) {
        boxedValue = builder_->CreateCall(makeDoubleFt, makeDouble.getCallee(), { valueToStore });
    } else if (valType->isIntegerTy(1)) {
        // Boolean - use ts_value_make_bool with i32
        auto makeBool = getTsValueMakeBool();
        llvm::Value* extended = builder_->CreateZExt(valueToStore, builder_->getInt32Ty(), "bool_ext");
        boxedValue = builder_->CreateCall(makeBool, { extended });
    } else if (valType->isIntegerTy(32)) {
        // i32 - extend to i64 and use makeInt
        llvm::Value* extended = builder_->CreateSExt(valueToStore, builder_->getInt64Ty(), "i32_ext");
        boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { extended });
    } else if (valType->isPointerTy()) {
        // For pointers/objects, box as object
        boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { valueToStore });
    } else {
        // Default: box as object (may fail for non-pointer types)
        boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { valueToStore });
    }

    // Store the value in the cell: ts_cell_set(cell, boxedValue)
    builder_->CreateCall(cellSetFt, cellSet.getCallee(), { cell, boxedValue });
}

//==============================================================================
// Control Flow
//==============================================================================

void HIRToLLVM::lowerBranch(HIRInstruction* inst) {
    HIRBlock* target = getOperandBlock(inst->operands[0]);
    llvm::BasicBlock* targetBB = getBlock(target);
    builder_->CreateBr(targetBB);
}

void HIRToLLVM::lowerCondBranch(HIRInstruction* inst) {
    llvm::Value* cond = getOperandValue(inst->operands[0]);
    HIRBlock* thenBlock = getOperandBlock(inst->operands[1]);
    HIRBlock* elseBlock = getOperandBlock(inst->operands[2]);

    llvm::BasicBlock* thenBB = getBlock(thenBlock);
    llvm::BasicBlock* elseBB = getBlock(elseBlock);

    // Convert condition to i1 (boolean) if it's not already
    if (!cond->getType()->isIntegerTy(1)) {
        if (cond->getType()->isIntegerTy(64)) {
            // Integer: compare != 0
            cond = builder_->CreateICmpNE(cond,
                llvm::ConstantInt::get(builder_->getInt64Ty(), 0), "tobool");
        } else if (cond->getType()->isIntegerTy(32)) {
            cond = builder_->CreateICmpNE(cond,
                llvm::ConstantInt::get(builder_->getInt32Ty(), 0), "tobool");
        } else if (cond->getType()->isDoubleTy()) {
            // Double: compare != 0.0
            cond = builder_->CreateFCmpONE(cond,
                llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0), "tobool");
        } else if (cond->getType()->isPointerTy()) {
            // Pointer (boxed value): use runtime ts_value_to_bool for JS truthiness
            auto toBoolFn = getOrDeclareRuntimeFunction("ts_value_to_bool",
                builder_->getInt1Ty(), { builder_->getPtrTy() });
            cond = builder_->CreateCall(toBoolFn, { cond }, "tobool");
        }
    }

    builder_->CreateCondBr(cond, thenBB, elseBB);
}

void HIRToLLVM::lowerSwitch(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);

    // LLVM switch instruction only supports integer types.
    // If the switch value is a double (TypeScript number), convert it to i64.
    if (val->getType()->isDoubleTy()) {
        val = builder_->CreateFPToSI(val, builder_->getInt64Ty(), "switch.val.i64");
    }

    llvm::BasicBlock* defaultBB = getBlock(inst->switchDefault);
    if (!defaultBB) {
        // Create unreachable block as default
        defaultBB = llvm::BasicBlock::Create(context_, "switch.default", currentFunction_);
        builder_->SetInsertPoint(defaultBB);
        builder_->CreateUnreachable();
        builder_->SetInsertPoint(getBlock(currentHIRFunction_->blocks.back().get()));
    }

    llvm::SwitchInst* switchInst = builder_->CreateSwitch(val, defaultBB, inst->switchCases.size());

    for (auto& [caseVal, caseBlock] : inst->switchCases) {
        llvm::BasicBlock* caseBB = getBlock(caseBlock);
        if (caseBB) {
            switchInst->addCase(
                llvm::ConstantInt::get(builder_->getInt64Ty(), caseVal),
                caseBB
            );
        }
    }
}

void HIRToLLVM::lowerReturn(HIRInstruction* inst) {
    SPDLOG_INFO("      lowerReturn: entered, operands.size()={}", inst->operands.size());
    if (inst->operands.empty()) {
        SPDLOG_ERROR("      lowerReturn: no operands!");
        builder_->CreateRet(llvm::UndefValue::get(currentFunction_->getReturnType()));
        return;
    }
    llvm::Value* val = getOperandValue(inst->operands[0]);
    SPDLOG_INFO("      lowerReturn: val={}", val ? "non-null" : "null");

    // Handle null value (e.g., from null HIRValue shared_ptr)
    if (!val) {
        SPDLOG_WARN("      lowerReturn: null value, using undefined");
        // If function returns void, just create void return
        if (currentFunction_->getReturnType()->isVoidTy()) {
            builder_->CreateRetVoid();
            return;
        }
        // Otherwise use undefined value
        val = llvm::UndefValue::get(currentFunction_->getReturnType());
        builder_->CreateRet(val);
        return;
    }

    // For async generator functions, mark the async generator as done with the return value
    if (isAsyncFunction_ && isGeneratorFunction_ && generatorObject_) {
        // Box the return value if needed
        llvm::Value* boxedVal = val;
        if (!val->getType()->isPointerTy()) {
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                    builder_->getPtrTy(), { builder_->getInt64Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_int");
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                    builder_->getPtrTy(), { builder_->getDoubleTy() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_double");
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                    builder_->getPtrTy(), { builder_->getInt1Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_bool");
            }
        }

        // Call ts_async_generator_return to mark as done with return value
        auto returnFn = getOrDeclareRuntimeFunction("ts_async_generator_return",
            builder_->getVoidTy(), { builder_->getPtrTy(), builder_->getPtrTy() });
        builder_->CreateCall(returnFn, { generatorObject_, boxedVal });

        // Return the async generator object
        builder_->CreateRet(generatorObject_);
        return;
    }

    // For regular generator functions, a return marks the generator as done with the return value
    if (isGeneratorFunction_ && generatorObject_) {
        // Box the return value if needed
        llvm::Value* boxedVal = val;
        if (!val->getType()->isPointerTy()) {
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                    builder_->getPtrTy(), { builder_->getInt64Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_int");
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                    builder_->getPtrTy(), { builder_->getDoubleTy() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_double");
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                    builder_->getPtrTy(), { builder_->getInt1Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_bool");
            }
        }

        // Call ts_generator_return to mark as done with return value
        auto returnFn = getOrDeclareRuntimeFunction("ts_generator_return",
            builder_->getVoidTy(), { builder_->getPtrTy(), builder_->getPtrTy() });
        builder_->CreateCall(returnFn, { generatorObject_, boxedVal });

        // Return the generator object
        builder_->CreateRet(generatorObject_);
        return;
    }

    // For async functions, a regular return should resolve the promise and return it
    if (isAsyncFunction_ && asyncPromise_) {
        // Box the return value if it's not already a pointer
        llvm::Value* boxedVal = val;
        if (!val->getType()->isPointerTy()) {
            // Need to box the value for ts_promise_resolve_internal
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                    builder_->getPtrTy(), { builder_->getInt64Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_int");
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                    builder_->getPtrTy(), { builder_->getDoubleTy() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_double");
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                    builder_->getPtrTy(), { builder_->getInt1Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_bool");
            }
        }

        // Resolve the promise with the value
        auto resolveFn = getOrDeclareRuntimeFunction("ts_promise_resolve_internal",
            builder_->getVoidTy(), { builder_->getPtrTy(), builder_->getPtrTy() });
        builder_->CreateCall(resolveFn, { asyncPromise_, boxedVal });

        // Return the promise (wrapped for boxing)
        auto makePromiseFn = getOrDeclareRuntimeFunction("ts_value_make_promise",
            builder_->getPtrTy(), { builder_->getPtrTy() });
        llvm::Value* boxedPromise = builder_->CreateCall(makePromiseFn, { asyncPromise_ }, "boxed_promise");
        builder_->CreateRet(boxedPromise);
        return;
    }

    // Get the function's declared return type
    llvm::Type* expectedRetType = currentFunction_->getReturnType();

    // Convert value to expected return type if needed
    if (val->getType() != expectedRetType) {
        if (val->getType()->isIntegerTy() && expectedRetType->isDoubleTy()) {
            // Int to Double
            val = builder_->CreateSIToFP(val, expectedRetType);
        } else if (val->getType()->isDoubleTy() && expectedRetType->isIntegerTy()) {
            // Double to Int
            val = builder_->CreateFPToSI(val, expectedRetType);
        } else if (val->getType()->isPointerTy() && expectedRetType->isIntegerTy()) {
            // Ptr to Int (for Any/Object → Int conversion)
            auto unboxFn = getOrDeclareRuntimeFunction("ts_value_get_int",
                builder_->getInt64Ty(), { builder_->getPtrTy() });
            val = builder_->CreateCall(unboxFn, { val }, "unbox_int_ret");
            if (expectedRetType != builder_->getInt64Ty()) {
                val = builder_->CreateTrunc(val, expectedRetType);
            }
        } else if (val->getType()->isPointerTy() && expectedRetType->isDoubleTy()) {
            // Ptr to Double (for Any/Object → number conversion, needs unboxing)
            auto unboxFn = getOrDeclareRuntimeFunction("ts_value_get_double",
                builder_->getDoubleTy(), { builder_->getPtrTy() });
            val = builder_->CreateCall(unboxFn, { val }, "unbox_double_ret");
        } else if (val->getType()->isIntegerTy() && expectedRetType->isPointerTy()) {
            // Int to Ptr (for Int → Any/Object conversion) - need to box
            if (val->getType()->isIntegerTy(1)) {
                // Bool needs boxing
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                    builder_->getPtrTy(), { builder_->getInt1Ty() });
                val = builder_->CreateCall(boxFn, { val }, "boxed_bool_ret");
            } else {
                // Integer needs boxing
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                    builder_->getPtrTy(), { builder_->getInt64Ty() });
                val = builder_->CreateCall(boxFn, { val }, "boxed_int_ret");
            }
        } else if (val->getType()->isDoubleTy() && expectedRetType->isPointerTy()) {
            // Double to Ptr (for number → Any/Object conversion, needs boxing)
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                builder_->getPtrTy(), { builder_->getDoubleTy() });
            val = builder_->CreateCall(boxFn, { val }, "boxed_double_ret");
        }
        // If types still don't match after conversion attempts, LLVM will error
    }

    builder_->CreateRet(val);
}

void HIRToLLVM::lowerReturnVoid(HIRInstruction* inst) {
    // For async generator functions, a void return marks the async generator as done with undefined
    if (isAsyncFunction_ && isGeneratorFunction_ && generatorObject_) {
        // Get undefined value
        auto undefFn = getOrDeclareRuntimeFunction("ts_value_make_undefined",
            builder_->getPtrTy(), {});
        llvm::Value* undefinedVal = builder_->CreateCall(undefFn, {}, "undefined");

        // Call ts_async_generator_return to mark as done with undefined
        auto returnFn = getOrDeclareRuntimeFunction("ts_async_generator_return",
            builder_->getVoidTy(), { builder_->getPtrTy(), builder_->getPtrTy() });
        builder_->CreateCall(returnFn, { generatorObject_, undefinedVal });

        // Return the async generator object
        builder_->CreateRet(generatorObject_);
        return;
    }

    // For regular generator functions, a void return marks the generator as done with undefined
    if (isGeneratorFunction_ && generatorObject_) {
        // Get undefined value
        auto undefFn = getOrDeclareRuntimeFunction("ts_value_make_undefined",
            builder_->getPtrTy(), {});
        llvm::Value* undefinedVal = builder_->CreateCall(undefFn, {}, "undefined");

        // Call ts_generator_return to mark as done with undefined
        auto returnFn = getOrDeclareRuntimeFunction("ts_generator_return",
            builder_->getVoidTy(), { builder_->getPtrTy(), builder_->getPtrTy() });
        builder_->CreateCall(returnFn, { generatorObject_, undefinedVal });

        // Return the generator object
        builder_->CreateRet(generatorObject_);
        return;
    }

    // For async functions (not generators), a void return should resolve the promise with undefined
    if (isAsyncFunction_ && asyncPromise_) {
        // Get undefined value
        auto undefFn = getOrDeclareRuntimeFunction("ts_value_make_undefined",
            builder_->getPtrTy(), {});
        llvm::Value* undefinedVal = builder_->CreateCall(undefFn, {}, "undefined");

        // Resolve the promise with undefined
        auto resolveFn = getOrDeclareRuntimeFunction("ts_promise_resolve_internal",
            builder_->getVoidTy(), { builder_->getPtrTy(), builder_->getPtrTy() });
        builder_->CreateCall(resolveFn, { asyncPromise_, undefinedVal });

        // Return the promise (wrapped for boxing)
        auto makePromiseFn = getOrDeclareRuntimeFunction("ts_value_make_promise",
            builder_->getPtrTy(), { builder_->getPtrTy() });
        llvm::Value* boxedPromise = builder_->CreateCall(makePromiseFn, { asyncPromise_ }, "boxed_promise");
        builder_->CreateRet(boxedPromise);
        return;
    }

    // Check if the function's return type is void or non-void
    llvm::Type* expectedRetType = currentFunction_->getReturnType();
    if (expectedRetType->isVoidTy()) {
        builder_->CreateRetVoid();
    } else {
        // Function expects a non-void return but we have ReturnVoid
        // Return a default value (undefined for ptr, 0 for int, 0.0 for double)
        if (expectedRetType->isPointerTy()) {
            // Return undefined (boxed)
            auto undefFn = getOrDeclareRuntimeFunction("ts_value_make_undefined",
                builder_->getPtrTy(), {});
            llvm::Value* undefinedVal = builder_->CreateCall(undefFn, {}, "undefined");
            builder_->CreateRet(undefinedVal);
        } else if (expectedRetType->isDoubleTy()) {
            builder_->CreateRet(llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0));
        } else if (expectedRetType->isIntegerTy(64)) {
            builder_->CreateRet(llvm::ConstantInt::get(builder_->getInt64Ty(), 0));
        } else if (expectedRetType->isIntegerTy(1)) {
            builder_->CreateRet(llvm::ConstantInt::get(builder_->getInt1Ty(), 0));
        } else {
            // For any other type, return a null pointer (best effort)
            builder_->CreateRet(llvm::Constant::getNullValue(expectedRetType));
        }
    }
}

void HIRToLLVM::lowerUnreachable(HIRInstruction* inst) {
    builder_->CreateUnreachable();
}

//==============================================================================
// Phi and Select
//==============================================================================

void HIRToLLVM::lowerPhi(HIRInstruction* inst) {
    llvm::Type* type = getLLVMType(inst->result->type);
    llvm::PHINode* phi = builder_->CreatePHI(type, inst->phiIncoming.size(), "phi");

    for (auto& [val, block] : inst->phiIncoming) {
        llvm::Value* llvmVal = getValue(val);
        llvm::BasicBlock* llvmBlock = getBlock(block);
        if (llvmVal && llvmBlock) {
            phi->addIncoming(llvmVal, llvmBlock);
        }
    }

    setValue(inst->result, phi);
}

void HIRToLLVM::lowerSelect(HIRInstruction* inst) {
    llvm::Value* cond = getOperandValue(inst->operands[0]);
    llvm::Value* trueVal = getOperandValue(inst->operands[1]);
    llvm::Value* falseVal = getOperandValue(inst->operands[2]);

    // Ensure condition is i1 (boolean) - LLVM select requires i1 for first operand
    if (!cond->getType()->isIntegerTy(1)) {
        if (cond->getType()->isPointerTy()) {
            // Convert pointer to boolean (non-null check)
            cond = builder_->CreateICmpNE(cond, llvm::ConstantPointerNull::get(builder_->getPtrTy()), "cond_bool");
        } else if (cond->getType()->isIntegerTy()) {
            // Convert integer to boolean (non-zero check)
            cond = builder_->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0), "cond_bool");
        } else if (cond->getType()->isDoubleTy()) {
            // Convert double to boolean (non-zero and not NaN)
            cond = builder_->CreateFCmpONE(cond, llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0), "cond_bool");
        }
    }

    llvm::Value* result = builder_->CreateSelect(cond, trueVal, falseVal, "select");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCopy(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    setValue(inst->result, val);
}

//==============================================================================
// Exception Handling
//==============================================================================

void HIRToLLVM::lowerSetupTry(HIRInstruction* inst) {
    // SetupTry: push exception handler, call setjmp, return result
    // Returns bool: true = exception path, false = normal entry

    // Get the catch block target
    HIRBlock* catchBlock = std::get<HIRBlock*>(inst->operands[0]);

    // Call ts_push_exception_handler() - returns jmp_buf pointer
    auto pushFn = getOrDeclareRuntimeFunction("ts_push_exception_handler",
        builder_->getPtrTy(), {});
    llvm::Value* jmpBuf = builder_->CreateCall(pushFn, {});

    // Call _setjmp(jmp_buf, frame_ptr) - Windows signature
    // Returns 0 on normal entry, non-zero when returning from longjmp
    auto setjmpFn = getOrDeclareRuntimeFunction("_setjmp",
        builder_->getInt32Ty(),
        {builder_->getPtrTy(), builder_->getPtrTy()});
    llvm::Value* framePtr = llvm::ConstantPointerNull::get(builder_->getPtrTy());
    llvm::Value* setjmpResult = builder_->CreateCall(setjmpFn, {jmpBuf, framePtr});

    // Convert result to bool: true if non-zero (exception path)
    llvm::Value* isException = builder_->CreateICmpNE(setjmpResult,
        llvm::ConstantInt::get(builder_->getInt32Ty(), 0));

    setValue(inst->result, isException);
}

void HIRToLLVM::lowerThrow(HIRInstruction* inst) {
    // Throw: call ts_throw with the exception value
    // ts_throw does not return (calls longjmp)

    llvm::Value* exception = getOperandValue(inst->operands[0]);

    auto throwFn = getOrDeclareRuntimeFunction("ts_throw",
        builder_->getVoidTy(), {builder_->getPtrTy()});
    builder_->CreateCall(throwFn, {exception});

    // ts_throw doesn't return, mark as unreachable
    builder_->CreateUnreachable();
}

void HIRToLLVM::lowerGetException(HIRInstruction* inst) {
    // GetException: call ts_get_exception to get current exception value

    auto getFn = getOrDeclareRuntimeFunction("ts_get_exception",
        builder_->getPtrTy(), {});
    llvm::Value* exception = builder_->CreateCall(getFn, {});

    setValue(inst->result, exception);
}

void HIRToLLVM::lowerClearException(HIRInstruction* inst) {
    // ClearException: call ts_set_exception(nullptr)

    auto setFn = getOrDeclareRuntimeFunction("ts_set_exception",
        builder_->getVoidTy(), {builder_->getPtrTy()});
    builder_->CreateCall(setFn, {llvm::ConstantPointerNull::get(builder_->getPtrTy())});
}

void HIRToLLVM::lowerPopHandler(HIRInstruction* inst) {
    // PopHandler: call ts_pop_exception_handler

    auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
        builder_->getVoidTy(), {});
    builder_->CreateCall(popFn, {});
}

//==============================================================================
// Async/Await Instructions
//==============================================================================

void HIRToLLVM::lowerAwait(HIRInstruction* inst) {
    // Await instruction: %r = await %promise
    // For a simple implementation (without full state machine), we call ts_promise_await
    // which blocks until the promise resolves.
    // TODO: Implement full state machine for proper async/await

    llvm::Value* promiseVal = getOperandValue(inst->operands[0]);

    // If the value is not a pointer (e.g., inlined async returned a raw value),
    // we need to box it first. In JavaScript, await on a non-promise value
    // simply returns the value itself.
    if (!promiseVal->getType()->isPointerTy()) {
        // Box the value first
        if (promiseVal->getType()->isIntegerTy(64)) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                builder_->getPtrTy(), { builder_->getInt64Ty() });
            promiseVal = builder_->CreateCall(boxFn, { promiseVal }, "boxed_int");
        } else if (promiseVal->getType()->isDoubleTy()) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                builder_->getPtrTy(), { builder_->getDoubleTy() });
            promiseVal = builder_->CreateCall(boxFn, { promiseVal }, "boxed_double");
        } else if (promiseVal->getType()->isIntegerTy(1)) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                builder_->getPtrTy(), { builder_->getInt1Ty() });
            promiseVal = builder_->CreateCall(boxFn, { promiseVal }, "boxed_bool");
        }
    }

    // Call ts_promise_await(promise) -> TsValue* (the resolved value)
    auto awaitFn = getOrDeclareRuntimeFunction("ts_promise_await",
        builder_->getPtrTy(), { builder_->getPtrTy() });
    llvm::Value* result = builder_->CreateCall(awaitFn, { promiseVal }, "await_result");

    setValue(inst->result, result);
}

void HIRToLLVM::lowerAsyncReturn(HIRInstruction* inst) {
    // AsyncReturn: resolve the promise and return it
    // async_return %val

    llvm::Value* val = getOperandValue(inst->operands[0]);

    // Box the value if needed (for ts_promise_resolve_internal which expects TsValue*)
    // For now, assume values coming from HIR are already properly typed

    // Call ts_promise_resolve_internal(promise, value)
    auto resolveFn = getOrDeclareRuntimeFunction("ts_promise_resolve_internal",
        builder_->getVoidTy(), { builder_->getPtrTy(), builder_->getPtrTy() });
    builder_->CreateCall(resolveFn, { asyncPromise_, val });

    // Return the promise (wrapped in ts_value_make_promise for boxing)
    auto makePromiseFn = getOrDeclareRuntimeFunction("ts_value_make_promise",
        builder_->getPtrTy(), { builder_->getPtrTy() });
    llvm::Value* boxedPromise = builder_->CreateCall(makePromiseFn, { asyncPromise_ }, "boxed_promise");

    builder_->CreateRet(boxedPromise);
}

void HIRToLLVM::lowerYield(HIRInstruction* inst) {
    // Yield instruction: %r = yield %value
    // For generators with state machine, this:
    // 1. Stores the yielded value in ctx->yieldedValue
    // 2. Sets ctx->yielded = true
    // 3. Sets ctx->state to next state
    // 4. Returns from the impl function
    // 5. Continues in the corresponding resume block

    llvm::Value* yieldVal = getOperandValue(inst->operands[0]);

    // Box the value if it's not already a pointer
    if (!yieldVal->getType()->isPointerTy()) {
        if (yieldVal->getType()->isIntegerTy(64)) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                builder_->getPtrTy(), { builder_->getInt64Ty() });
            yieldVal = builder_->CreateCall(boxFn, { yieldVal }, "boxed_int");
        } else if (yieldVal->getType()->isDoubleTy()) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                builder_->getPtrTy(), { builder_->getDoubleTy() });
            yieldVal = builder_->CreateCall(boxFn, { yieldVal }, "boxed_double");
        } else if (yieldVal->getType()->isIntegerTy(1)) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                builder_->getPtrTy(), { builder_->getInt1Ty() });
            yieldVal = builder_->CreateCall(boxFn, { yieldVal }, "boxed_bool");
        }
    }

    if (isAsyncFunction_ && isGeneratorFunction_) {
        // Async generator: yield produces a Promise
        auto yieldFn = getOrDeclareRuntimeFunction("ts_async_generator_yield",
            builder_->getPtrTy(), { builder_->getPtrTy() });
        llvm::Value* result = builder_->CreateCall(yieldFn, { yieldVal }, "async_yield_result");
        setValue(inst->result, result);
    } else if (isGeneratorFunction_ && asyncContext_ != nullptr) {
        // Generator with state machine - use the new implementation
        // Call ts_async_context_yield(ctx, value) to store value and set yielded=true
        auto yieldFn = getOrDeclareRuntimeFunction("ts_async_context_yield",
            builder_->getVoidTy(), { builder_->getPtrTy(), builder_->getPtrTy() });
        builder_->CreateCall(yieldFn, { asyncContext_, yieldVal });

        // Set state to next state (currentYieldState_ + 1)
        int nextState = currentYieldState_ + 1;
        auto setStateFn = getOrDeclareRuntimeFunction("ts_async_context_set_state",
            builder_->getVoidTy(), { builder_->getPtrTy(), builder_->getInt32Ty() });
        builder_->CreateCall(setStateFn, { asyncContext_, builder_->getInt32(nextState) });

        // Return from the impl function (suspend)
        builder_->CreateRetVoid();

        // Move to the corresponding resume block for subsequent instructions
        if (currentYieldState_ < static_cast<int>(yieldResumeBlocks_.size())) {
            llvm::BasicBlock* resumeBlock = yieldResumeBlocks_[currentYieldState_];
            builder_->SetInsertPoint(resumeBlock);

            // Get the resumed value from ctx->resumedValue for the yield result
            auto getResumedFn = getOrDeclareRuntimeFunction("ts_async_context_get_resumed_value",
                builder_->getPtrTy(), { builder_->getPtrTy() });
            llvm::Value* resumedValue = builder_->CreateCall(getResumedFn, { asyncContext_ }, "resumed_value");
            setValue(inst->result, resumedValue);
        }

        currentYieldState_++;
    } else {
        // Regular generator (fallback to old implementation)
        auto yieldFn = getOrDeclareRuntimeFunction("ts_generator_yield",
            builder_->getPtrTy(), { builder_->getPtrTy() });
        llvm::Value* result = builder_->CreateCall(yieldFn, { yieldVal }, "yield_result");
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerYieldStar(HIRInstruction* inst) {
    // YieldStar instruction: %r = yield* %iterable
    // Delegates to another generator or iterable.
    // This iterates over the iterable, yielding each value.
    // The result is the return value of the delegated generator.
    //
    // For async generators, yield* produces Promises for each yielded value.
    //
    // For a simple implementation, we call ts_generator_yield_star/ts_async_generator_yield_star which:
    // - Iterates the iterable calling next() and yield on each value
    // - Returns the final return value of the iterable
    // TODO: Implement full delegation with proper value passing

    llvm::Value* iterableVal = getOperandValue(inst->operands[0]);

    llvm::Value* result;
    if (isAsyncFunction_ && isGeneratorFunction_) {
        // Async generator: yield* delegates to async iterable
        auto yieldStarFn = getOrDeclareRuntimeFunction("ts_async_generator_yield_star",
            builder_->getPtrTy(), { builder_->getPtrTy() });
        result = builder_->CreateCall(yieldStarFn, { iterableVal }, "async_yield_star_result");
    } else {
        // Regular generator
        auto yieldStarFn = getOrDeclareRuntimeFunction("ts_generator_yield_star",
            builder_->getPtrTy(), { builder_->getPtrTy() });
        result = builder_->CreateCall(yieldStarFn, { iterableVal }, "yield_star_result");
    }

    setValue(inst->result, result);
}

//==============================================================================
// Runtime Function Helpers
//==============================================================================

llvm::FunctionCallee HIRToLLVM::getOrDeclareRuntimeFunction(
    const std::string& name,
    llvm::Type* returnType,
    llvm::ArrayRef<llvm::Type*> paramTypes,
    bool isVarArg
) {
    llvm::FunctionType* ft = llvm::FunctionType::get(returnType, paramTypes, isVarArg);
    return module_->getOrInsertFunction(name, ft);
}

llvm::FunctionCallee HIRToLLVM::getTsAlloc() {
    return getOrDeclareRuntimeFunction("ts_alloc", builder_->getPtrTy(), {builder_->getInt64Ty()});
}

llvm::FunctionCallee HIRToLLVM::getTsStringCreate() {
    return getOrDeclareRuntimeFunction("ts_string_create", builder_->getPtrTy(), {builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeInt() {
    return getOrDeclareRuntimeFunction("ts_value_make_int", builder_->getPtrTy(), {builder_->getInt64Ty()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeDouble() {
    return getOrDeclareRuntimeFunction("ts_value_make_double", builder_->getPtrTy(), {builder_->getDoubleTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeBool() {
    return getOrDeclareRuntimeFunction("ts_value_make_bool", builder_->getPtrTy(), {builder_->getInt32Ty()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeString() {
    return getOrDeclareRuntimeFunction("ts_value_make_string", builder_->getPtrTy(), {builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeObject() {
    return getOrDeclareRuntimeFunction("ts_value_make_object", builder_->getPtrTy(), {builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeFunction() {
    // ts_value_make_function(void* funcPtr, void* context) -> TsValue*
    return getOrDeclareRuntimeFunction("ts_value_make_function", builder_->getPtrTy(), {builder_->getPtrTy(), builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueGetInt() {
    return getOrDeclareRuntimeFunction("ts_value_get_int", builder_->getInt64Ty(), {builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueGetDouble() {
    return getOrDeclareRuntimeFunction("ts_value_get_double", builder_->getDoubleTy(), {builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueGetBool() {
    return getOrDeclareRuntimeFunction("ts_value_get_bool", builder_->getInt32Ty(), {builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueGetString() {
    return getOrDeclareRuntimeFunction("ts_value_get_string", builder_->getPtrTy(), {builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueGetObject() {
    return getOrDeclareRuntimeFunction("ts_value_get_object", builder_->getPtrTy(), {builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsArrayCreate() {
    return getOrDeclareRuntimeFunction("ts_array_create_sized", builder_->getPtrTy(), {builder_->getInt64Ty()});
}

llvm::FunctionCallee HIRToLLVM::getTsArrayGet() {
    return getOrDeclareRuntimeFunction("ts_array_get_unchecked", builder_->getPtrTy(), {builder_->getPtrTy(), builder_->getInt64Ty()});
}

llvm::FunctionCallee HIRToLLVM::getTsArraySet() {
    return getOrDeclareRuntimeFunction("ts_array_set_unchecked", builder_->getVoidTy(), {builder_->getPtrTy(), builder_->getInt64Ty(), builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsArrayLength() {
    return getOrDeclareRuntimeFunction("ts_array_length", builder_->getInt64Ty(), {builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsArrayPush() {
    return getOrDeclareRuntimeFunction("ts_array_push", builder_->getInt64Ty(), {builder_->getPtrTy(), builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsObjectCreate() {
    // ts_map_create() returns a TsMap* (plain object)
    return getOrDeclareRuntimeFunction("ts_map_create", builder_->getPtrTy(), {});
}

llvm::FunctionCallee HIRToLLVM::getTsObjectGetProperty() {
    // ts_object_get_dynamic(TsValue* obj, TsValue* key) -> TsValue*
    return getOrDeclareRuntimeFunction("ts_object_get_dynamic", builder_->getPtrTy(), {builder_->getPtrTy(), builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsObjectSetProperty() {
    // ts_object_set_dynamic(TsValue* obj, TsValue* key, TsValue* value) -> void
    return getOrDeclareRuntimeFunction("ts_object_set_dynamic", builder_->getVoidTy(), {builder_->getPtrTy(), builder_->getPtrTy(), builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsObjectHasProperty() {
    return getOrDeclareRuntimeFunction("ts_object_has_property", builder_->getInt32Ty(), {builder_->getPtrTy(), builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsObjectDeleteProperty() {
    return getOrDeclareRuntimeFunction("ts_object_delete_property", builder_->getInt32Ty(), {builder_->getPtrTy(), builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsTypeOf() {
    return getOrDeclareRuntimeFunction("ts_typeof", builder_->getPtrTy(), {builder_->getPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsInstanceOf() {
    return getOrDeclareRuntimeFunction("ts_instanceof", builder_->getInt32Ty(), {builder_->getPtrTy(), builder_->getPtrTy()});
}

//==============================================================================
// Helper Methods
//==============================================================================

llvm::Value* HIRToLLVM::getOperandValue(const HIROperand& operand) {
    SPDLOG_INFO("getOperandValue: operand.index()={}", operand.index());
    if (auto* val = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
        SPDLOG_INFO("getOperandValue: got HIRValue, val={}", (*val) ? std::to_string((*val)->id) : "null");
        auto result = getValue(*val);
        if (!result && *val) {
            SPDLOG_WARN("getOperandValue: HIRValue id={} not found in valueMap_", (*val)->id);
        }
        return result;
    }
    if (auto* i = std::get_if<int64_t>(&operand)) {
        return llvm::ConstantInt::get(builder_->getInt64Ty(), *i);
    }
    if (auto* d = std::get_if<double>(&operand)) {
        return llvm::ConstantFP::get(builder_->getDoubleTy(), *d);
    }
    if (auto* b = std::get_if<bool>(&operand)) {
        return llvm::ConstantInt::get(builder_->getInt1Ty(), *b ? 1 : 0);
    }
    SPDLOG_WARN("getOperandValue: unknown operand type index={}", operand.index());
    return nullptr;
}

int64_t HIRToLLVM::getOperandInt(const HIROperand& operand) {
    if (auto* i = std::get_if<int64_t>(&operand)) {
        return *i;
    }
    return 0;
}

std::string HIRToLLVM::getOperandString(const HIROperand& operand) {
    if (auto* s = std::get_if<std::string>(&operand)) {
        return *s;
    }
    return "";
}

HIRBlock* HIRToLLVM::getOperandBlock(const HIROperand& operand) {
    if (auto* b = std::get_if<HIRBlock*>(&operand)) {
        return *b;
    }
    return nullptr;
}

std::shared_ptr<HIRType> HIRToLLVM::getOperandType(const HIROperand& operand) {
    if (auto* t = std::get_if<std::shared_ptr<HIRType>>(&operand)) {
        return *t;
    }
    return nullptr;
}

llvm::Value* HIRToLLVM::createGlobalString(const std::string& str) {
    // Create a global string constant
    return builder_->CreateGlobalStringPtr(str);
}

} // namespace ts::hir
