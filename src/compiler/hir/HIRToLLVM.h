#pragma once

#include "HIR.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>

#include <memory>
#include <map>
#include <string>

namespace ts::hir {

//==============================================================================
// HIRToLLVM - Lower HIR to LLVM IR
//
// This pass converts the typed HIR representation to LLVM IR.
// Key responsibilities:
// 1. Map HIR types to LLVM types
// 2. Lower each HIR instruction to corresponding LLVM IR
// 3. Handle GC operations (stub to Boehm GC initially)
// 4. Generate runtime function calls where needed
//==============================================================================

class HIRToLLVM {
public:
    HIRToLLVM(llvm::LLVMContext& ctx);
    ~HIRToLLVM() = default;

    // Main entry point - lower an HIR module to LLVM module
    std::unique_ptr<llvm::Module> lower(HIRModule* hirModule, const std::string& moduleName);

private:
    llvm::LLVMContext& context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;

    // Current function being lowered
    llvm::Function* currentFunction_ = nullptr;
    HIRFunction* currentHIRFunction_ = nullptr;

    // For closures: hidden first parameter that holds the TsClosure*
    // This is set when lowering a function with captures
    llvm::Value* closureParam_ = nullptr;

    //==========================================================================
    // Type Mapping
    //==========================================================================

    // Map HIR type to LLVM type
    llvm::Type* getLLVMType(const std::shared_ptr<HIRType>& type);
    llvm::Type* getLLVMType(HIRTypeKind kind);

    // TsValue struct type for boxing (matches runtime)
    llvm::StructType* tsValueType_ = nullptr;
    void initTsValueType();

    //==========================================================================
    // Value Mapping
    //==========================================================================

    // Map HIR values to LLVM values
    std::map<uint32_t, llvm::Value*> valueMap_;

    // Map HIR blocks to LLVM blocks
    std::map<std::string, llvm::BasicBlock*> blockMap_;

    // Map global variable names to LLVM globals (for consistent lookup)
    std::map<std::string, llvm::GlobalVariable*> globalMap_;

    // Get or create LLVM value for HIR value
    llvm::Value* getValue(const std::shared_ptr<HIRValue>& hirValue);
    void setValue(const std::shared_ptr<HIRValue>& hirValue, llvm::Value* llvmValue);

    // Get or create a global variable
    llvm::GlobalVariable* getOrCreateGlobal(const std::string& name, std::shared_ptr<HIRType> type);

    // Get LLVM block for HIR block
    llvm::BasicBlock* getBlock(HIRBlock* hirBlock);

    //==========================================================================
    // Function Lowering
    //==========================================================================

    void lowerFunction(HIRFunction* fn);

    // Create main entry point that calls ts_main with user_main
    void createMainFunction();
    void lowerBlock(HIRBlock* block);
    void lowerInstruction(HIRInstruction* inst);

    //==========================================================================
    // Instruction Lowering
    //==========================================================================

    // Constants
    void lowerConstInt(HIRInstruction* inst);
    void lowerConstFloat(HIRInstruction* inst);
    void lowerConstBool(HIRInstruction* inst);
    void lowerConstString(HIRInstruction* inst);
    void lowerConstNull(HIRInstruction* inst);
    void lowerConstUndefined(HIRInstruction* inst);

    // Integer arithmetic
    void lowerAddI64(HIRInstruction* inst);
    void lowerSubI64(HIRInstruction* inst);
    void lowerMulI64(HIRInstruction* inst);
    void lowerDivI64(HIRInstruction* inst);
    void lowerModI64(HIRInstruction* inst);
    void lowerNegI64(HIRInstruction* inst);

    // Float arithmetic
    void lowerAddF64(HIRInstruction* inst);
    void lowerSubF64(HIRInstruction* inst);
    void lowerMulF64(HIRInstruction* inst);
    void lowerDivF64(HIRInstruction* inst);
    void lowerModF64(HIRInstruction* inst);
    void lowerNegF64(HIRInstruction* inst);

    // String operations
    void lowerStringConcat(HIRInstruction* inst);

    // Bitwise operations
    void lowerAndI64(HIRInstruction* inst);
    void lowerOrI64(HIRInstruction* inst);
    void lowerXorI64(HIRInstruction* inst);
    void lowerShlI64(HIRInstruction* inst);
    void lowerShrI64(HIRInstruction* inst);
    void lowerUShrI64(HIRInstruction* inst);
    void lowerNotI64(HIRInstruction* inst);

    // Comparisons
    void lowerCmpEqI64(HIRInstruction* inst);
    void lowerCmpNeI64(HIRInstruction* inst);
    void lowerCmpLtI64(HIRInstruction* inst);
    void lowerCmpLeI64(HIRInstruction* inst);
    void lowerCmpGtI64(HIRInstruction* inst);
    void lowerCmpGeI64(HIRInstruction* inst);

    void lowerCmpEqF64(HIRInstruction* inst);
    void lowerCmpNeF64(HIRInstruction* inst);
    void lowerCmpLtF64(HIRInstruction* inst);
    void lowerCmpLeF64(HIRInstruction* inst);
    void lowerCmpGtF64(HIRInstruction* inst);
    void lowerCmpGeF64(HIRInstruction* inst);

    void lowerCmpEqPtr(HIRInstruction* inst);
    void lowerCmpNePtr(HIRInstruction* inst);

    // Boolean operations
    void lowerLogicalAnd(HIRInstruction* inst);
    void lowerLogicalOr(HIRInstruction* inst);
    void lowerLogicalNot(HIRInstruction* inst);

    // Type conversions
    void lowerCastI64ToF64(HIRInstruction* inst);
    void lowerCastF64ToI64(HIRInstruction* inst);
    void lowerCastBoolToI64(HIRInstruction* inst);

    // Boxing/Unboxing
    void lowerBoxInt(HIRInstruction* inst);
    void lowerBoxFloat(HIRInstruction* inst);
    void lowerBoxBool(HIRInstruction* inst);
    void lowerBoxString(HIRInstruction* inst);
    void lowerBoxObject(HIRInstruction* inst);

    void lowerUnboxInt(HIRInstruction* inst);
    void lowerUnboxFloat(HIRInstruction* inst);
    void lowerUnboxBool(HIRInstruction* inst);
    void lowerUnboxString(HIRInstruction* inst);
    void lowerUnboxObject(HIRInstruction* inst);

    // Type checking
    void lowerTypeCheck(HIRInstruction* inst);
    void lowerTypeOf(HIRInstruction* inst);
    void lowerInstanceOf(HIRInstruction* inst);

    // GC operations (stub to Boehm)
    void lowerGCAlloc(HIRInstruction* inst);
    void lowerGCAllocArray(HIRInstruction* inst);
    void lowerGCStore(HIRInstruction* inst);
    void lowerGCLoad(HIRInstruction* inst);
    void lowerSafepoint(HIRInstruction* inst);
    void lowerSafepointPoll(HIRInstruction* inst);

    // Memory operations
    void lowerAlloca(HIRInstruction* inst);
    void lowerLoad(HIRInstruction* inst);
    void lowerStore(HIRInstruction* inst);
    void lowerGetElementPtr(HIRInstruction* inst);

    // Object operations
    void lowerNewObject(HIRInstruction* inst);
    void lowerNewObjectDynamic(HIRInstruction* inst);
    void lowerGetPropStatic(HIRInstruction* inst);
    void lowerGetPropDynamic(HIRInstruction* inst);
    void lowerSetPropStatic(HIRInstruction* inst);
    void lowerSetPropDynamic(HIRInstruction* inst);
    void lowerHasProp(HIRInstruction* inst);
    void lowerDeleteProp(HIRInstruction* inst);

    // Array operations
    void lowerNewArrayBoxed(HIRInstruction* inst);
    void lowerNewArrayTyped(HIRInstruction* inst);
    void lowerGetElem(HIRInstruction* inst);
    void lowerSetElem(HIRInstruction* inst);
    void lowerGetElemTyped(HIRInstruction* inst);
    void lowerSetElemTyped(HIRInstruction* inst);
    void lowerArrayLength(HIRInstruction* inst);
    void lowerArrayPush(HIRInstruction* inst);

    // Calls
    void lowerCall(HIRInstruction* inst);
    void lowerCallMethod(HIRInstruction* inst);
    void lowerCallVirtual(HIRInstruction* inst);
    void lowerCallIndirect(HIRInstruction* inst);

    // Globals
    void lowerLoadGlobal(HIRInstruction* inst);
    void lowerLoadFunction(HIRInstruction* inst);

    // Closures
    void lowerMakeClosure(HIRInstruction* inst);
    void lowerLoadCapture(HIRInstruction* inst);
    void lowerStoreCapture(HIRInstruction* inst);

    // Control flow
    void lowerBranch(HIRInstruction* inst);
    void lowerCondBranch(HIRInstruction* inst);
    void lowerSwitch(HIRInstruction* inst);
    void lowerReturn(HIRInstruction* inst);
    void lowerReturnVoid(HIRInstruction* inst);
    void lowerUnreachable(HIRInstruction* inst);

    // Phi and Select
    void lowerPhi(HIRInstruction* inst);
    void lowerSelect(HIRInstruction* inst);
    void lowerCopy(HIRInstruction* inst);

    // Exception handling
    void lowerSetupTry(HIRInstruction* inst);
    void lowerThrow(HIRInstruction* inst);
    void lowerGetException(HIRInstruction* inst);
    void lowerClearException(HIRInstruction* inst);
    void lowerPopHandler(HIRInstruction* inst);

    //==========================================================================
    // Runtime Function Helpers
    //==========================================================================

    // Get or declare a runtime function
    llvm::FunctionCallee getOrDeclareRuntimeFunction(
        const std::string& name,
        llvm::Type* returnType,
        llvm::ArrayRef<llvm::Type*> paramTypes,
        bool isVarArg = false
    );

    // Common runtime functions
    llvm::FunctionCallee getTsAlloc();
    llvm::FunctionCallee getTsStringCreate();
    llvm::FunctionCallee getTsValueMakeInt();
    llvm::FunctionCallee getTsValueMakeDouble();
    llvm::FunctionCallee getTsValueMakeBool();
    llvm::FunctionCallee getTsValueMakeString();
    llvm::FunctionCallee getTsValueMakeObject();
    llvm::FunctionCallee getTsValueGetInt();
    llvm::FunctionCallee getTsValueGetDouble();
    llvm::FunctionCallee getTsValueGetBool();
    llvm::FunctionCallee getTsValueGetString();
    llvm::FunctionCallee getTsValueGetObject();
    llvm::FunctionCallee getTsArrayCreate();
    llvm::FunctionCallee getTsArrayGet();
    llvm::FunctionCallee getTsArraySet();
    llvm::FunctionCallee getTsArrayLength();
    llvm::FunctionCallee getTsArrayPush();
    llvm::FunctionCallee getTsObjectCreate();
    llvm::FunctionCallee getTsObjectGetProperty();
    llvm::FunctionCallee getTsObjectSetProperty();
    llvm::FunctionCallee getTsObjectHasProperty();
    llvm::FunctionCallee getTsObjectDeleteProperty();
    llvm::FunctionCallee getTsTypeOf();
    llvm::FunctionCallee getTsInstanceOf();

    //==========================================================================
    // Helper Methods
    //==========================================================================

    // Get operand value from instruction
    llvm::Value* getOperandValue(const HIROperand& operand);

    // Get operand as integer constant
    int64_t getOperandInt(const HIROperand& operand);

    // Get operand as string
    std::string getOperandString(const HIROperand& operand);

    // Get operand as block
    HIRBlock* getOperandBlock(const HIROperand& operand);

    // Get operand as type
    std::shared_ptr<HIRType> getOperandType(const HIROperand& operand);

    // Create a global string constant
    llvm::Value* createGlobalString(const std::string& str);
};

} // namespace ts::hir
