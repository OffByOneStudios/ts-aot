//==============================================================================
// HIRToLLVM.cpp - Lower HIR to LLVM IR
//==============================================================================

#include "HIRToLLVM.h"

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

    // Lower all functions
    for (auto& fn : hirModule->functions) {
        lowerFunction(fn.get());
    }

    // Create main entry point
    createMainFunction();

    return std::move(module_);
}

void HIRToLLVM::createMainFunction() {
    // Look for user_main function
    llvm::Function* userMain = module_->getFunction("user_main");
    if (!userMain) {
        SPDLOG_WARN("No user_main function found, skipping main entry point generation");
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
    // For generator functions (not async), create a Generator
    else if (fn->isGenerator) {
        // Create entry block for generator setup
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "generator_entry", llvmFunc);
        builder_->SetInsertPoint(entryBB);

        // First create an AsyncContext
        llvm::FunctionType* createCtxFt = llvm::FunctionType::get(
            builder_->getPtrTy(), {}, false);
        llvm::FunctionCallee createCtxFn = module_->getOrInsertFunction(
            "ts_async_context_create", createCtxFt);
        llvm::Value* asyncContext = builder_->CreateCall(createCtxFt, createCtxFn.getCallee(), {}, "async_ctx");

        // Create a Generator: ts_generator_create(AsyncContext*) -> TsGenerator*
        llvm::FunctionType* createGeneratorFt = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy() }, false);
        llvm::FunctionCallee createGeneratorFn = module_->getOrInsertFunction(
            "ts_generator_create", createGeneratorFt);
        generatorObject_ = builder_->CreateCall(createGeneratorFt, createGeneratorFn.getCallee(), { asyncContext }, "generator");
    }

    // Create LLVM basic blocks for HIR blocks
    for (auto& block : fn->blocks) {
        llvm::BasicBlock* bb = llvm::BasicBlock::Create(context_, block->label, llvmFunc);
        blockMap_[block->label] = bb;
    }

    // For async, generator, or async generator functions, branch from entry to the first HIR block
    // (The entry block was created above based on the function type)
    if ((fn->isAsync || fn->isGenerator) && !fn->blocks.empty()) {
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
    llvm::Value* result = builder_->CreateAdd(lhs, rhs, "add");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerSubI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateSub(lhs, rhs, "sub");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerMulI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateMul(lhs, rhs, "mul");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerDivI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateSDiv(lhs, rhs, "div");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerModI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateSRem(lhs, rhs, "mod");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerNegI64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
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
            if (val->getType()->isIntegerTy(64)) {
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

void HIRToLLVM::lowerAndI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateAnd(lhs, rhs, "and");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerOrI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateOr(lhs, rhs, "or");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerXorI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateXor(lhs, rhs, "xor");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerShlI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateShl(lhs, rhs, "shl");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerShrI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateAShr(lhs, rhs, "ashr");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUShrI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateLShr(lhs, rhs, "lshr");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerNotI64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = builder_->CreateNot(val, "not");
    setValue(inst->result, result);
}

//==============================================================================
// Integer Comparisons
//==============================================================================

void HIRToLLVM::lowerCmpEqI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateICmpEQ(lhs, rhs, "eq");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpNeI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateICmpNE(lhs, rhs, "ne");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLtI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateICmpSLT(lhs, rhs, "lt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLeI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateICmpSLE(lhs, rhs, "le");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGtI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateICmpSGT(lhs, rhs, "gt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGeI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateICmpSGE(lhs, rhs, "ge");
    setValue(inst->result, result);
}

//==============================================================================
// Float Comparisons
//==============================================================================

void HIRToLLVM::lowerCmpEqF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateFCmpOEQ(lhs, rhs, "feq");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpNeF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateFCmpONE(lhs, rhs, "fne");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLtF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateFCmpOLT(lhs, rhs, "flt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLeF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateFCmpOLE(lhs, rhs, "fle");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGtF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateFCmpOGT(lhs, rhs, "fgt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGeF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
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
    llvm::Value* result = builder_->CreateAnd(lhs, rhs, "land");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerLogicalOr(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
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
    auto fn = getTsValueMakeObject();
    llvm::Value* result = builder_->CreateCall(fn, {val});
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
    auto fn = getTsObjectCreate();
    llvm::Value* result = builder_->CreateCall(fn, {});
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
            }
        }

        if (valHirType) {
            if (valHirType->kind == HIRTypeKind::String) {
                auto fn = getTsValueMakeString();
                val = builder_->CreateCall(fn, {val});
            } else if (valHirType->kind == HIRTypeKind::Object ||
                       valHirType->kind == HIRTypeKind::Class ||
                       valHirType->kind == HIRTypeKind::Array) {
                auto fn = getTsValueMakeObject();
                val = builder_->CreateCall(fn, {val});
            }
            // Any type is already boxed, no action needed
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
            } else if (valHirType->kind == HIRTypeKind::Object ||
                       valHirType->kind == HIRTypeKind::Class ||
                       valHirType->kind == HIRTypeKind::Array) {
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

    // Convert index to i64 if it's a double (numeric literal indices come through as f64)
    if (idx->getType()->isDoubleTy()) {
        idx = builder_->CreateFPToSI(idx, builder_->getInt64Ty(), "idx_to_i64");
    }

    auto fn = getTsArrayGet();
    llvm::Value* result = builder_->CreateCall(fn, {arr, idx});

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

    // Convert index to i64 if it's a double (numeric literal indices come through as f64)
    if (idx->getType()->isDoubleTy()) {
        idx = builder_->CreateFPToSI(idx, builder_->getInt64Ty(), "idx_to_i64");
    }

    // ts_array_set expects (ptr, i64, ptr) - need to box value if not a pointer
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

    auto fn = getTsArraySet();
    builder_->CreateCall(fn, {arr, idx, val});
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

    // Handle string conversion functions - they take specific types, not ptr
    if (funcName == "ts_string_from_int") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
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
        // ts_array_concat(void* arr, void* other) - returns void
        llvm::Value* arr = getOperandValue(inst->operands[1]);
        llvm::Value* other = getOperandValue(inst->operands[2]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getVoidTy(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_concat", ft);
        builder_->CreateCall(ft, fn.getCallee(), { arr, other });
        // No result - void function
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
        llvm::Value* target = getOperandValue(inst->operands[1]);
        llvm::Value* source = getOperandValue(inst->operands[2]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getPtrTy(), { builder_->getPtrTy(), builder_->getPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_object_assign", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { target, source });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Math functions - they take and return double
    if (funcName == "ts_math_floor" || funcName == "ts_math_ceil" ||
        funcName == "ts_math_round" || funcName == "ts_math_trunc" ||
        funcName == "ts_math_abs" || funcName == "ts_math_sqrt" ||
        funcName == "ts_math_sin" || funcName == "ts_math_cos" ||
        funcName == "ts_math_tan" || funcName == "ts_math_log" ||
        funcName == "ts_math_exp") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getDoubleTy(), { builder_->getDoubleTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle Math.pow - takes two double arguments
    if (funcName == "ts_math_pow") {
        llvm::Value* base = getOperandValue(inst->operands[1]);
        llvm::Value* exp = getOperandValue(inst->operands[2]);
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

void HIRToLLVM::lowerCallMethod(HIRInstruction* inst) {
    // operands[0] = object, operands[1] = methodName, operands[2..] = args
    llvm::Value* obj = getOperandValue(inst->operands[0]);
    std::string methodName = getOperandString(inst->operands[1]);

    // Handle console.log / console.error / console.warn / console.info / console.debug
    if (methodName == "log" || methodName == "error" || methodName == "warn" ||
        methodName == "info" || methodName == "debug") {

        // Get the arguments (starting at index 2)
        for (size_t i = 2; i < inst->operands.size(); ++i) {
            llvm::Value* arg = getOperandValue(inst->operands[i]);

            // Determine the right runtime function based on LLVM argument type
            std::string funcName = (methodName == "error" || methodName == "warn")
                ? "ts_console_error" : "ts_console_log";
            llvm::Type* paramType = builder_->getPtrTy();

            llvm::Type* argType = arg->getType();
            if (argType->isIntegerTy(64)) {
                funcName = (methodName == "error" || methodName == "warn")
                    ? "ts_console_error_int" : "ts_console_log_int";
                paramType = builder_->getInt64Ty();
            } else if (argType->isDoubleTy()) {
                funcName = (methodName == "error" || methodName == "warn")
                    ? "ts_console_error_double" : "ts_console_log_double";
                paramType = builder_->getDoubleTy();
            } else if (argType->isIntegerTy(1)) {
                funcName = (methodName == "error" || methodName == "warn")
                    ? "ts_console_error_bool" : "ts_console_log_bool";
                paramType = builder_->getInt1Ty();
            } else if (argType->isPointerTy()) {
                // For pointers, use the generic value function
                funcName = (methodName == "error" || methodName == "warn")
                    ? "ts_console_error_value" : "ts_console_log_value";
            }

            // Emit the call
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getVoidTy(), { paramType }, false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
            builder_->CreateCall(ft, fn.getCallee(), { arg });
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

    // Generic method call - fall back to dynamic dispatch
    // Call ts_object_call_method(obj, methodName, argsArray, argCount)
    SPDLOG_WARN("CallMethod not fully implemented: {}", methodName);

    // Return null for now
    if (inst->result) {
        setValue(inst->result, llvm::ConstantPointerNull::get(builder_->getPtrTy()));
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
    // CallIndirect is used to call closures or function pointers
    // For closures (TsClosure*):
    //   1. Extract the function pointer using ts_closure_get_func
    //   2. Pass the closure as the first argument
    //
    // Operand 0: closure/function pointer
    // Operand 1+: regular arguments

    llvm::Value* closureOrFnPtr = getOperandValue(inst->operands[0]);

    // Gather regular arguments
    std::vector<llvm::Value*> regularArgs;
    for (size_t i = 1; i < inst->operands.size(); ++i) {
        regularArgs.push_back(getOperandValue(inst->operands[i]));
    }

    // ts_closure_get_func(TsClosure* closure) -> void*
    auto getClosureFuncFt = llvm::FunctionType::get(
        builder_->getPtrTy(),
        { builder_->getPtrTy() },
        false
    );
    auto getClosureFunc = module_->getOrInsertFunction("ts_closure_get_func", getClosureFuncFt);

    // Extract the actual function pointer from the closure
    llvm::Value* funcPtr = builder_->CreateCall(getClosureFuncFt, getClosureFunc.getCallee(), { closureOrFnPtr });

    // Build argument list: closure first, then regular arguments
    std::vector<llvm::Value*> allArgs;
    allArgs.push_back(closureOrFnPtr);  // Closure context as first argument
    allArgs.insert(allArgs.end(), regularArgs.begin(), regularArgs.end());

    // Create function type for the call
    // First argument (closure context) is always a pointer
    // Other arguments use the actual LLVM types of the arguments
    std::vector<llvm::Type*> paramTypes;
    for (auto& arg : allArgs) {
        paramTypes.push_back(arg->getType());
    }

    // Determine return type from the instruction's result type
    llvm::Type* returnType = builder_->getVoidTy();
    if (inst->result && inst->result->type) {
        returnType = getLLVMType(inst->result->type);
    }

    llvm::FunctionType* ft = llvm::FunctionType::get(returnType, paramTypes, false);

    llvm::Value* result = builder_->CreateCall(ft, funcPtr, allArgs);
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
    llvm::Value* numCapturesVal = llvm::ConstantInt::get(builder_->getInt64Ty(), numCaptures);
    llvm::Value* closure = builder_->CreateCall(closureCreateFt, closureCreate.getCallee(), { fn, numCapturesVal });

    // Initialize each capture cell with its value
    for (size_t i = 0; i < numCaptures; ++i) {
        llvm::Value* capturedValue = getOperandValue(inst->operands[i + 1]);
        llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), i);

        // Get the type of the captured value to box it properly
        auto* hirValue = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[i + 1]);
        llvm::Value* boxedValue = nullptr;

        if (hirValue && (*hirValue)->type) {
            HIRTypeKind kind = (*hirValue)->type->kind;
            if (kind == HIRTypeKind::Int64) {
                boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { capturedValue });
            } else if (kind == HIRTypeKind::Float64) {
                boxedValue = builder_->CreateCall(makeDoubleFt, makeDouble.getCallee(), { capturedValue });
            } else {
                // For pointers/objects, box as object
                boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { capturedValue });
            }
        } else {
            // Default: box as object (ptr)
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

    // Box the value based on its type
    llvm::Value* boxedValue = nullptr;
    if (captureType) {
        HIRTypeKind kind = captureType->kind;
        if (kind == HIRTypeKind::Int64) {
            boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { valueToStore });
        } else if (kind == HIRTypeKind::Float64) {
            boxedValue = builder_->CreateCall(makeDoubleFt, makeDouble.getCallee(), { valueToStore });
        } else {
            // For pointers/objects, box as object
            boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { valueToStore });
        }
    } else {
        // Default: box as object (ptr)
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
    llvm::Value* val = getOperandValue(inst->operands[0]);

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
            val = builder_->CreatePtrToInt(val, expectedRetType);
        } else if (val->getType()->isIntegerTy() && expectedRetType->isPointerTy()) {
            // Int to Ptr (for Int → Any/Object conversion)
            val = builder_->CreateIntToPtr(val, expectedRetType);
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

    builder_->CreateRetVoid();
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
    // For generators, this yields a value and suspends execution.
    // The result is the value passed to next() when resumed.
    //
    // For async generators, yield produces a Promise that resolves to the value.
    //
    // For a simple implementation, we call ts_generator_yield/ts_async_generator_yield which:
    // - Stores the yielded value in the generator state
    // - Returns the value sent via next() (or undefined if none)
    // TODO: Implement full generator state machine for proper suspension/resumption

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

    llvm::Value* result;
    if (isAsyncFunction_ && isGeneratorFunction_) {
        // Async generator: yield produces a Promise
        auto yieldFn = getOrDeclareRuntimeFunction("ts_async_generator_yield",
            builder_->getPtrTy(), { builder_->getPtrTy() });
        result = builder_->CreateCall(yieldFn, { yieldVal }, "async_yield_result");
    } else {
        // Regular generator
        auto yieldFn = getOrDeclareRuntimeFunction("ts_generator_yield",
            builder_->getPtrTy(), { builder_->getPtrTy() });
        result = builder_->CreateCall(yieldFn, { yieldVal }, "yield_result");
    }

    setValue(inst->result, result);
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
    if (auto* val = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
        return getValue(*val);
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
