#pragma once

#include "HIR.h"
#include "LoweringRegistry.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Intrinsics.h>

#include <memory>
#include <map>
#include <string>

namespace ts::hir {

// Forward declarations for handler classes
class MathHandler;
class ConsoleHandler;
class ArrayHandler;
class MapSetHandler;
class TimerHandler;
class BigIntHandler;
class PathHandler;

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
    // Friend declarations for builtin handlers
    friend class MathHandler;
    friend class ConsoleHandler;
    friend class ArrayHandler;
    friend class MapSetHandler;
    friend class TimerHandler;
    friend class BigIntHandler;
    friend class PathHandler;

public:
    HIRToLLVM(llvm::LLVMContext& ctx);
    ~HIRToLLVM() = default;

    // Main entry point - lower an HIR module to LLVM module
    std::unique_ptr<llvm::Module> lower(HIRModule* hirModule, const std::string& moduleName);

    // Set the ICU data path to embed in the generated binary.
    // When set, emits a global @__ts_icu_data_path so the runtime can find icudt74l.dat
    // without copying it next to every compiled executable.
    void setIcuDataPath(const std::string& path) { icuDataPath_ = path; }

    //==========================================================================
    // Handler Accessors - Used by BuiltinHandler implementations
    //==========================================================================

    /// Get the LLVM IRBuilder
    llvm::IRBuilder<>& builder() { return *builder_; }

    /// Get the LLVM Module
    llvm::Module& module() { return *module_; }

    /// Get an operand value from an HIR instruction
    llvm::Value* getOperandValue(const HIROperand& operand);

    /// Set the result value for an HIR instruction
    void setValue(const std::shared_ptr<HIRValue>& hirValue, llvm::Value* llvmValue);

private:
    llvm::LLVMContext& context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;

    // ICU data path to embed in generated binary (empty = don't embed)
    std::string icuDataPath_;

    // Current function being lowered
    llvm::Function* currentFunction_ = nullptr;
    HIRFunction* currentHIRFunction_ = nullptr;

    // For closures: hidden first parameter that holds the TsClosure*
    // This is set when lowering a function with captures
    llvm::Value* closureParam_ = nullptr;

    // Escape analysis: stack allocation tracking per function
    int stackAllocCount_ = 0;           // Number of stack-allocated objects in current function
    int stackAllocBytes_ = 0;           // Total bytes of stack-allocated objects in current function
    static constexpr int kMaxStackAllocObjects = 4;
    static constexpr int kMaxStackAllocBytes = 512;
    static constexpr int kSizeOfTsMap = 64;
    static constexpr int kSizeOfTsArray = 56;

    // For async functions
    bool isAsyncFunction_ = false;
    llvm::Value* asyncPromise_ = nullptr;  // The Promise to resolve/reject

    // For generator functions
    bool isGeneratorFunction_ = false;
    llvm::Value* generatorObject_ = nullptr;  // The Generator object
    llvm::Value* asyncContext_ = nullptr;     // AsyncContext* for state machine

    // Generator state machine tracking
    int currentYieldState_ = 0;               // Yield state counter (0 = initial, 1+ = resume points)
    std::vector<llvm::BasicBlock*> yieldResumeBlocks_;  // Resume blocks for each yield
    llvm::BasicBlock* generatorDoneBlock_ = nullptr;    // Block when generator is done
    llvm::Function* generatorImplFunc_ = nullptr;       // The state machine implementation function
    llvm::Value* generatorDataBuf_ = nullptr;            // Heap-allocated data buffer for params + locals
    int generatorLocalCount_ = 0;                        // Number of Alloca instructions in generator
    int generatorNextLocalIndex_ = 0;                    // Next local index for alloca replacement
    std::vector<llvm::Value*> generatorLocalSlots_;      // Pre-created GEPs for local slots (dominate all uses)

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

    // Get or create a global variable
    llvm::GlobalVariable* getOrCreateGlobal(const std::string& name, std::shared_ptr<HIRType> type);

    // Get LLVM block for HIR block
    llvm::BasicBlock* getBlock(HIRBlock* hirBlock);

    //==========================================================================
    // Function Lowering
    //==========================================================================

    void forwardDeclareFunction(HIRFunction* fn);
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
    void lowerConstCString(HIRInstruction* inst);
    void lowerConstNull(HIRInstruction* inst);
    void lowerConstUndefined(HIRInstruction* inst);

    // Integer arithmetic
    void lowerAddI64(HIRInstruction* inst);
    void lowerSubI64(HIRInstruction* inst);
    void lowerMulI64(HIRInstruction* inst);
    void lowerDivI64(HIRInstruction* inst);
    void lowerModI64(HIRInstruction* inst);
    void lowerNegI64(HIRInstruction* inst);

    // Checked integer arithmetic (with overflow detection)
    void lowerAddI64Checked(HIRInstruction* inst);
    void lowerSubI64Checked(HIRInstruction* inst);
    void lowerMulI64Checked(HIRInstruction* inst);

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
    llvm::Value* ensureI64ForBitwise(llvm::Value* val);  // Convert f64 to i64 if needed
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

    // Registry-based call lowering
    llvm::Value* lowerRegisteredCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec);
    llvm::Value* convertArg(llvm::Value* arg, ::hir::ArgConversion conv);
    llvm::Value* coerceArgToType(llvm::Value* arg, llvm::Type* expectedType,
                                  const HIROperand& operand);
    llvm::Value* handleReturn(llvm::Value* result, ::hir::ReturnHandling handling);

    // Variadic function lowering helpers
    llvm::Value* lowerTypeDispatchCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec);
    llvm::Value* lowerPackArrayCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec);
    std::string getTypeSuffix(llvm::Value* arg, const ::hir::LoweringSpec& spec);

    // Globals
    void lowerLoadGlobal(HIRInstruction* inst);
    void lowerLoadFunction(HIRInstruction* inst);

    // Closures
    void lowerMakeClosure(HIRInstruction* inst);
    void lowerLoadCapture(HIRInstruction* inst);
    void lowerStoreCapture(HIRInstruction* inst);
    void lowerLoadCaptureFromClosure(HIRInstruction* inst);
    void lowerStoreCaptureFromClosure(HIRInstruction* inst);

    // Function trampolines for dynamic calls
    // Generates a trampoline that converts a function's native calling convention
    // to the closure calling convention (context pointer, returns TsValue*)
    llvm::Function* getOrCreateTrampoline(llvm::Function* originalFunc);

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

    // Async/Await
    void lowerAwait(HIRInstruction* inst);
    void lowerAsyncReturn(HIRInstruction* inst);

    // Generator/Yield
    void lowerYield(HIRInstruction* inst);
    void lowerYieldStar(HIRInstruction* inst);

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
    llvm::FunctionCallee getTsValueMakeFunction();
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

    //==========================================================================
    // Dynamic Method Call Helpers
    //==========================================================================

    /// Box an argument value for use in dynamic dispatch (ts_call_with_this_N).
    /// Examines LLVM type and HIR type to determine appropriate boxing.
    /// @param arg The LLVM value to box
    /// @param operand The HIR operand for type information
    /// @return The boxed TsValue* or original value if already suitable
    llvm::Value* boxArgumentForDynamicCall(llvm::Value* arg, const HIROperand& operand);

    /// Emit a dynamic method call using ts_call_with_this_N.
    /// Boxes arguments and calls the appropriate runtime function.
    /// @param funcVal The function value (TsValue* from property lookup)
    /// @param thisArg The boxed 'this' value
    /// @param inst The HIR instruction containing operands
    /// @param argStartIdx Index of first argument in inst->operands
    /// @return The result of the call (TsValue*)
    llvm::Value* emitDynamicMethodCall(llvm::Value* funcVal, llvm::Value* thisArg,
                                       HIRInstruction* inst, size_t argStartIdx);
};

} // namespace ts::hir
