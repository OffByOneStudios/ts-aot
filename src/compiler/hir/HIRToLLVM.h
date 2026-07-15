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
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>

#include <memory>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

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
// 3. Handle GC operations (custom generational GC; see runtime/src/TsGC.cpp)
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

    // Enable LLVM GC statepoint infrastructure.
    // When enabled, GC-managed pointers use addrspace(1), functions get gc "ts-aot-gc"
    // attribute, and calls get "deopt" operand bundles for RS4GC pass.
    void setEnableGCStatepoints(bool enable) { enableGCStatepoints_ = enable; }

    // "use fast" (docs/design/use-fast.md Phase 2c): the entry program carried
    // a `"use fast"` directive. Enables the NativeArray Temp arena frame:
    // ts_native_arena_mark() at each function entry, ts_native_arena_release()
    // on each return, so Temp allocations are bulk-freed at frame exit.
    void setFastModule(bool enable) { fastModule_ = enable; }

    // Per-module "use fast": source files (parser-stamped paths) of fast
    // MODULES. Functions whose sourceFile is in this set get the Temp arena
    // frame even when the entry program is dynamic (hot-5% kernel pattern).
    void setFastSourceFiles(std::set<std::string> files) {
        fastSourceFiles_ = std::move(files);
    }
    // ANY compiled file (entry or module) carried "use fast". Gates the
    // fast-only lowerings that key on values/calls only fast code produces
    // (ts_math_* intrinsic map, no-gc.pin for NativeArray handles).
    void setFastAny(bool enable) { fastAny_ = enable; }

    // "use fast" NativeArray safety tiers (safety is the DEFAULT):
    //   default        -> inline unboxed load/store guarded by an INLINE
    //                     bounds check (length compare + noreturn abort);
    //                     hoistable/eliminable by LLVM like V8's checks.
    //   --fast-checks  -> the bounds/dispose-checked runtime CALL (richer
    //                     dev diagnostics: use-after-dispose, double-dispose).
    //   getUnchecked/setUnchecked -> raw inline access, NO bounds check.
    //                     The IN-LANGUAGE unsafe marker (Rust get_unchecked
    //                     analog); there is no flag that removes checks.
    void setFastChecks(bool enable) { fastChecks_ = enable; }
    bool fastChecks() const { return fastChecks_; }
    // Expose the addrspace(1)->addrspace(0) cast for handlers doing inline
    // memory access on a NativeArray handle.
    llvm::Value* toRawPtr(llvm::Value* v) { return gcPtrToRaw(v); }
    // NativeArray element-access helpers shared by the [i] sugar
    // (lowerGet/SetElem) and NativeArrayHandler (.get/.set):
    llvm::Value* emitNativeArrayIndex(llvm::Value* v);  // any -> i64
    llvm::Value* emitNativeArrayF64(llvm::Value* v);    // any -> double

    // Sized-slot element descriptor from the receiver's elementType
    // (numericBits metadata; 0 = legacy 8-byte slot).
    struct NaElem {
        unsigned bytes = 8;       // storage slot size (1/2/4/8)
        bool isInt = false;       // Int64-kind element (else Float64-kind)
        bool isUnsigned = false;  // zext (vs sext) on sub-64 int loads
    };
    static NaElem naElemInfo(const std::shared_ptr<HIRType>& elem);
    llvm::Value* emitNativeArraySlot(llvm::Value* arr, llvm::Value* i64Idx,
                                     unsigned elemBytes = 8);
    // Sized load/store: value side is always i64 (int elements) or double
    // (float elements); the STORAGE is elemBytes wide (zext/sext, trunc,
    // fpext/fptrunc as needed).
    llvm::Value* emitNativeArrayLoad(llvm::Value* arr, llvm::Value* i64Idx,
                                     const NaElem& e);
    void emitNativeArrayStore(llvm::Value* arr, llvm::Value* i64Idx,
                              const NaElem& e, llvm::Value* v);
    // Inline bounds check: load length (offset 8), unsigned-compare the
    // index (negative/NaN indexes wrap huge and fail), branch to a
    // noreturn ts_native_array_bounds_abort on out-of-range. Leaves the
    // builder in the continuation block.
    void emitNativeArrayBoundsCheck(llvm::Value* arr, llvm::Value* i64Idx);

    // Enable debug info emission (CodeView on Windows, DWARF on Linux/Mac).
    // When enabled, source file/line metadata is attached to LLVM IR instructions.
    void setEmitDebugInfo(bool enable) { emitDebugInfo_ = enable; }

    // Enable LLVM source-based coverage instrumentation.
    // When enabled, emits llvm.instrprof.increment intrinsics and coverage mapping sections.
    void setEmitCoverage(bool enable) { emitCoverage_ = enable; }

    // Emit a precompiled prelude object: no main/ts_main; the synthetic main is
    // internalized and a unique external `ts_prelude_init` runs it (called by the
    // runtime's ts_main before user_main).
    void setPreludeObject(bool enable) { preludeObject_ = enable; }

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

    bool preludeObject_ = false;  // emit ts_prelude_init instead of main (see setPreludeObject)

    // GC statepoint infrastructure (experimental)
    bool enableGCStatepoints_ = false;
    bool fastModule_ = false;                // entry program had "use fast"
    bool fastAny_ = false;                   // entry OR any module had "use fast"
    std::set<std::string> fastSourceFiles_;  // sourceFiles of fast modules
    bool fastChecks_ = false;                // dev-mode NativeArray checks (--fast-checks)
    llvm::Value* arenaMarker_ = nullptr;     // per-function ts_native_arena_mark() token

    // Debug info emission
    bool emitDebugInfo_ = false;

    // Coverage instrumentation
    bool emitCoverage_ = false;
    llvm::Function* instrProfIncrement_ = nullptr;

    struct CoverageRegion {
        uint16_t fileIdx;
        uint32_t lineStart, colStart, lineEnd, colEnd;
    };
    struct CoverageFunctionInfo {
        std::string funcName;
        std::string sourceFile;
        uint64_t funcHash;
        uint32_t numCounters;
        std::vector<CoverageRegion> regions;
    };
    std::vector<CoverageFunctionInfo> coverageFunctions_;

    void emitCoverageIncrement(const std::string& funcName, uint64_t funcHash,
                               uint32_t numCounters, uint32_t counterIdx);
    void emitCoverageMapping();
    std::unique_ptr<llvm::DIBuilder> diBuilder_;
    llvm::DICompileUnit* diCompileUnit_ = nullptr;
    llvm::DIFile* diFile_ = nullptr;
    std::map<std::string, llvm::DIFile*> diFiles_;

    llvm::DIFile* getOrCreateDIFile(const std::string& path);
    llvm::DISubroutineType* createFunctionDebugType(HIRFunction* fn);

    // Get pointer type for GC-managed pointers (addrspace 1 when statepoints enabled)
    llvm::PointerType* getGCPtrTy();

    // Check if an HIR type kind represents a GC-managed object
    bool isGCManagedType(HIRTypeKind kind);

    // Cast GC pointer (addrspace 1) to raw pointer (addrspace 0) for runtime calls
    llvm::Value* gcPtrToRaw(llvm::Value* val);

    // Box a primitive LLVM value (i1 / i64 / double) into a TsValue* via
    // the appropriate ts_value_make_* runtime function, returning a ptr.
    // If the input is already a pointer, returns it unchanged. Used at
    // runtime call sites where the runtime signature expects ptr but
    // the caller may have a primitive (e.g., Object.assign(true, src)).
    llvm::Value* boxPrimitiveToPtr(llvm::Value* val);

    // Cast raw pointer (addrspace 0) to GC pointer (addrspace 1) for internal use
    llvm::Value* rawToGCPtr(llvm::Value* val);

    // Create a call to a runtime function, handling addrspacecast at boundaries
    llvm::Value* createRuntimeCall(llvm::FunctionCallee fn,
                                    llvm::ArrayRef<llvm::Value*> args,
                                    const llvm::Twine& name = "");

    // Create a call with "deopt" operand bundle (required for RS4GC)
    llvm::CallInst* createCallWithDeopt(llvm::FunctionType* ft, llvm::Value* callee,
                                         llvm::ArrayRef<llvm::Value*> args,
                                         const llvm::Twine& name = "");

    // Current function being lowered
    llvm::Function* currentFunction_ = nullptr;
    HIRFunction* currentHIRFunction_ = nullptr;
    std::string currentBlockLabel_;
    size_t currentInstrIndex_ = 0;

    // For closures: hidden first parameter that holds the TsClosure*
    // This is set when lowering a function with captures
    llvm::Value* closureParam_ = nullptr;

    // Shared capture cells: maps (variable name, source alloca/value) to
    // (TsCell*, basic block). When multiple closures in the same basic block
    // capture the same variable, they share the same TsCell. The basic block
    // constraint ensures SSA dominance (cells from one block can't be used
    // in non-dominated blocks, e.g., across loop iterations).
    std::map<std::pair<std::string, llvm::Value*>,
             std::pair<llvm::Value*, llvm::BasicBlock*>> capturedVarCells_;

    // Per-function entry-block alloca (TsCell**) holding the canonical SHARED
    // cell for a captured variable. Keyed identically to capturedVarCells_
    // (capture name + source alloca/value). Because the slot is an entry-block
    // alloca it dominates every block, so closures capturing the same variable
    // in DIFFERENT basic blocks (e.g. across if/else or sequential statements
    // separated by calls) can all converge on one cell via
    // ts_closure_share_or_init_cell — fixing the multi-cell write desync that
    // the same-basic-block constraint of capturedVarCells_ could not cover.
    std::map<std::pair<std::string, llvm::Value*>, llvm::AllocaInst*>
        capturedVarCellSlots_;

    // HIR module pointer (set during lower())
    HIRModule* hirModule_ = nullptr;

    // Inline nursery allocator function (created once per module, inlined by LLVM)
    llvm::FunctionCallee getOrCreateNurseryAllocFn();

    // Escape analysis: stack allocation tracking per function
    int stackAllocCount_ = 0;           // Number of stack-allocated objects in current function
    int stackAllocBytes_ = 0;           // Total bytes of stack-allocated objects in current function
    static constexpr int kMaxStackAllocObjects = 4;
    static constexpr int kMaxStackAllocBytes = 512;
    static constexpr int kSizeOfTsMap = 64;
    // MUST be >= sizeof(TsArray) in src/runtime/include/TsArray.h. Escape-
    // analysis stack-allocates this many bytes then the runtime ctor
    // placement-news a TsArray into it; if too small the ctor scribbles past
    // the slot (stack corruption). 72 = magic+pad, elements, length, capacity,
    // elementSize, 3 bools+pad, originalReceiver, properties, sparseElements.
    static constexpr int kSizeOfTsArray = 72;  // fits `properties` + `sparseElements`

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

    // Suspendable async-generator lowering (GEN-001 Stage 3).
    // suspendAsyncGen_: feature flag, read once from TSAOT_SUSPEND_AGEN in the
    // constructor; default false (eager agen lowering compiled verbatim).
    // inSuspendableAgenMode_: true while lowering the impl function of a
    // suspendable async generator (drives lowerCall marker interception and
    // the suspendable branches of lowerYield/lowerYieldStar/lowerReturn[Void]).
    // agenForcedReturnBB_: lazily created per impl function (resume mode 2).
    bool suspendAsyncGen_ = false;
    bool inSuspendableAgenMode_ = false;
    llvm::BasicBlock* agenForcedReturnBB_ = nullptr;
    // Suspension-relocation edges (GEN-001 Stage 4b): when a suspendable-agen
    // suspension point terminates the current LLVM block with `ret void` and
    // relocates emission into a yield_resume_N block, the rest of the HIR
    // block's instructions (including phi-feeding short-circuit branches) are
    // emitted in the resume block. lowerPhi's predecessor DFS walks real CFG
    // successors only, so it cannot cross the suspension — record the
    // old-block -> resume-block hop here so the DFS can follow it. Cleared
    // per function; only populated in suspendable-agen mode (flag-off IR is
    // untouched).
    std::unordered_map<llvm::BasicBlock*, llvm::BasicBlock*> agenSuspendRelocation_;
    llvm::Value* generatorDataBuf_ = nullptr;            // Heap-allocated data buffer for params + locals
    int generatorLocalCount_ = 0;                        // Number of Alloca instructions in generator
    int generatorNextLocalIndex_ = 0;                    // Next local index for alloca replacement
    std::vector<llvm::Value*> generatorLocalSlots_;      // Pre-created GEPs for local slots (dominate all uses)

    // Shared-capture cellslots inside GENERATOR impls: an entry-block alloca
    // dies at every yield (each resume re-runs impl_entry and re-nulls it),
    // so sibling closures created across a yield minted DIFFERENT cells for
    // the same generator-local. These slots live in the ctx data buffer
    // instead (after params/locals/spills; ts_alloc zero-fills, so they
    // start null exactly like the alloca did). Count is an upper bound
    // (total MakeClosure capture slots) sized in collectGeneratorCounts.
    int generatorCellSlotCount_ = 0;
    int generatorNextCellSlotIdx_ = 0;
    std::map<std::pair<std::string, llvm::Value*>, int> generatorCellSlotIdx_;

    // Cross-yield SSA spill state. Populated only for generator/async-generator
    // functions where some HIR SSA value is defined before a yield and used
    // after it. The post-yield resume block is reached directly from impl_entry
    // via the state-switch, bypassing the pre-yield definition's block — so
    // without spilling, the verifier rejects with "Instruction does not
    // dominate all uses!". The fix routes such values through the heap-backed
    // data buffer: every SET also stores to a per-value slot, every GET of a
    // spilled value reads from the slot. The slot GEPs are created in
    // impl_entry so they dominate every block.
    //
    // Differs from the reverted commit de53567 in three ways:
    //   (1) only values with cross-yield liveness are spilled (not every
    //       value defined before any yield);
    //   (2) non-ptr types are *boxed* (ts_value_make_int/double/bool) before
    //       being stored — never type-punned via IntToPtr — so the GC scan of
    //       the data buffer sees real ptr values uniformly;
    //   (3) reads always come from the slot at use-site (no global valueMap_
    //       mutation that poisons subsequent blocks).
    std::unordered_set<uint32_t> crossYieldSpillIds_;            // HIRValue ids to spill
    std::unordered_map<uint32_t, size_t> crossYieldSlotOf_;      // id -> slot index
    std::unordered_map<uint32_t, llvm::Type*> crossYieldSlotType_; // id -> original LLVM type (for unboxing on reload)
    std::vector<llvm::Value*> crossYieldSlotGEPs_;               // slot index -> GEP into data buffer (in impl_entry)

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

    // Map HIR value IDs to stack allocas for GC root pinning.
    // Pointer-type values from runtime calls are stored to entry-block allocas
    // so the conservative GC stack scanner can see them.
    std::map<uint32_t, llvm::AllocaInst*> gcPinAllocas_;

    // Map HIR blocks to LLVM blocks (keyed by pointer, not label, to handle duplicate names)
    std::map<HIRBlock*, llvm::BasicBlock*> blockMap_;

    // Map global variable names to LLVM globals (for consistent lookup)
    std::map<std::string, llvm::GlobalVariable*> globalMap_;

    // Map of user-defined function names to their HIR parameter types
    // Used to avoid boxing string args when callee param is String-typed (not Any)
    std::map<std::string, std::vector<std::shared_ptr<HIRType>>> userFunctionParams_;

    // Cache of closure globals for LoadFunction: funcName -> LLVM global holding the cached TsClosure*.
    // Function declarations get a single TsClosure allocated on first reference and reused,
    // preserving JavaScript function object identity (a === a must be true).
    // Function expressions (__fn_expr_*, __arrow_fn_*) are excluded — they create new closures each time.
    std::map<std::string, llvm::GlobalVariable*> closureCache_;

    // Flat object shape tracking: maps HIR value ID to its shape (for flat object fast path)
    std::map<uint32_t, HIRShape*> flatObjectShapes_;

    // Scalar-replaced objects: maps HIR value ID to per-property allocas (SROA)
    std::map<uint32_t, std::map<std::string, llvm::AllocaInst*>> scalarReplacedObjects_;

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

    //==========================================================================
    // Generator state-machine lowering (GEN-001 Stage 1 extraction)
    //==========================================================================

    // Options parameterizing the generator state-machine lowering. Stage 1
    // (sync generators) instantiates only the defaults; the suspendable
    // async-generator path (GEN-001 Stage 3+) will pass isAsyncGen=true and
    // a different create function.
    struct GeneratorLoweringOpts {
        bool isAsyncGen = false;
        // Sync generator with a ts_generator_body_started marker: invoke the
        // impl once at gen() time so the parameter prologue runs eagerly
        // (param/default/destructuring throws escape gen() synchronously) and
        // suspends at the marker. Gated on marker presence so markerless sync
        // generators keep the lazy (pre-existing) behavior.
        bool eagerSyncParams = false;
        const char* createGenFn = "ts_generator_create";
    };

    // Count Yield/YieldStar and Alloca instructions in fn. Sets
    // generatorLocalCount_ / generatorNextLocalIndex_ and returns
    // {yieldCount, allocaCount}.
    std::pair<int, int> collectGeneratorCounts(HIRFunction* fn);

    // Cross-yield SSA liveness pre-pass: populates crossYieldSpillIds_ /
    // crossYieldSlotOf_ (and clears crossYieldSlotType_ / crossYieldSlotGEPs_).
    // markerIsSuspension (GEN-001 Stage 3): treat the HIR Call to
    // ts_async_generator_body_started as a suspension point for the
    // within-block yield-crossing rule (suspendable async generators suspend
    // at the marker, so SSA defs crossing it need spilling too).
    void computeCrossYieldSpills(HIRFunction* fn, bool markerIsSuspension = false);

    // Emit the wrapper function body into llvmFunc: AsyncContext creation,
    // resume-fn binding (generatorImplFunc_ must already be created), `this`
    // capture, param/local/spill data buffer, generator creation via
    // opts.createGenFn, and the immediate ret of the generator object.
    void emitGeneratorWrapper(HIRFunction* fn, llvm::Function* llvmFunc,
                              const GeneratorLoweringOpts& opts);

    // Emit the impl-function entry: state load, data-buffer reload, param
    // reloads, local/spill slot GEPs, HIR block creation, resume blocks,
    // the state-dispatch switch and the generator_done block. Sets
    // currentFunction_ / asyncContext_ to the impl function.
    void emitGeneratorImplPrologue(HIRFunction* fn,
                                   const GeneratorLoweringOpts& opts,
                                   int yieldCount);

    //==========================================================================
    // Suspendable async-generator lowering (GEN-001 Stage 3, flag-gated by
    // TSAOT_SUSPEND_AGEN=1; default OFF — the eager agen path is untouched)
    //==========================================================================

    // Emit the resume-mode dispatch at the start of a suspendable-agen yield
    // resume block. Builder must be positioned at the resume block on entry.
    // Emits: mode = ts_async_context_get_resume_mode(ctx); switch —
    //   mode 1 (throw):  re-arm enclosing user try handlers (GEN-001 Stage 6),
    //                    then ts_throw(resumedValue) — caught by the innermost
    //                    re-armed handler if one encloses the yield, else by
    //                    the impl barrier (reject)
    //   mode 2 (return): branch to the shared forced-return block (NO re-arm:
    //                    the forced-return path pops only the impl barrier)
    //   default (next):  re-arm enclosing user try handlers, fall through
    // Leaves the builder in the next-mode path's final block and returns the
    // resumed value (ts_async_context_get_resumed_value) for use as the yield
    // expression's value. tryCatchTargets = the HIR catch-dispatch blocks of
    // the user try scopes armed at this suspension point, outermost first
    // (HIRInstruction::tryCatchTargets).
    llvm::Value* emitAgenResumeModeDispatch(
        const std::vector<HIRBlock*>& tryCatchTargets);

    // GEN-001 Stage 6 helpers shared by sync-generator and suspendable-agen
    // suspension points.
    //
    // emitTryHandlerPushAndSetjmp: the factored body of lowerSetupTry —
    // ts_push_exception_handler + platform setjmp (Win64 2-arg form with
    // frameaddress) + NoInline on the containing function. Returns the i1
    // "is exception" value.
    llvm::Value* emitTryHandlerPushAndSetjmp();
    // emitSuspendHandlerPops: emit n ts_pop_exception_handler calls — popping
    // the user try handlers still armed at a suspension point before the impl
    // function's `ret void`. Without this every yield inside a try LEAKS a
    // handler-stack entry pointing at the dead impl frame (the E2 latent bug:
    // a later throw longjmps into the dead frame and poisons the process-wide
    // handler stack).
    void emitSuspendHandlerPops(size_t n);
    // emitRearmTryHandlers: re-execute the push+setjmp sequence for each
    // enclosing try scope (outermost first) targeting the SAME catch dispatch
    // blocks, chaining through fresh "rearm_cont" blocks. Builder ends in the
    // final continuation block.
    void emitRearmTryHandlers(const std::vector<HIRBlock*>& tryCatchTargets);

    // Lazily create the per-impl-function forced-return block (resume mode 2):
    // v = resumedValue; ts_agen_complete(ctx, v); pop impl barrier; ret void.
    // Only ever reached from a resume-mode dispatch, i.e. a state>=1
    // invocation whose entry pushed the impl barrier — the pop is balanced.
    llvm::BasicBlock* getOrCreateAgenForcedReturnBlock();

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

    // Inline NaN-boxing helpers (emit IR directly instead of runtime calls)
    llvm::Value* emitInlineBoxInt(llvm::Value* val);     // i64 → ptr (NaN-boxed)
    llvm::Value* emitInlineUnboxInt(llvm::Value* val);   // ptr (NaN-boxed) → i64
    llvm::Value* emitInlineBoxFloat(llvm::Value* val);   // double → ptr (NaN-boxed)
    llvm::Value* emitInlineUnboxFloat(llvm::Value* val); // ptr (NaN-boxed) → double
    llvm::Value* emitInlineBoxBool(llvm::Value* val);    // i1 → ptr (NaN-boxed)
    llvm::Value* emitInlineUnboxBool(llvm::Value* val);  // ptr (NaN-boxed) → i1

    // Type checking
    void lowerTypeCheck(HIRInstruction* inst);
    void lowerTypeOf(HIRInstruction* inst);
    void lowerInstanceOf(HIRInstruction* inst);

    // GC operations
    void lowerGCAlloc(HIRInstruction* inst);
    void lowerGCAllocArray(HIRInstruction* inst);
    void lowerGCStore(HIRInstruction* inst);
    void lowerGCLoad(HIRInstruction* inst);
    void lowerSafepoint(HIRInstruction* inst);
    void lowerSafepointPoll(HIRInstruction* inst);

    // Nursery write barrier (emits inline card-marking after pointer stores)
    void emitWriteBarrier(llvm::Value* slotAddr, llvm::Value* storedValue);
    llvm::GlobalVariable* getOrDeclareGCGlobal(const std::string& name, llvm::Type* type);

    // Memory operations
    void lowerAlloca(HIRInstruction* inst);
    void lowerLoad(HIRInstruction* inst);
    void lowerStore(HIRInstruction* inst);
    void lowerGetElementPtr(HIRInstruction* inst);

    // Object operations
    void lowerNewObject(HIRInstruction* inst);
    void lowerNewObjectDynamic(HIRInstruction* inst);
    void lowerNewFlatObject(HIRInstruction* inst);
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
    void lowerCallValueWithThis(HIRInstruction* inst);
    void lowerConstructFromValue(HIRInstruction* inst);

    // Registry-based call lowering
    llvm::Value* lowerRegisteredCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec);
    llvm::Value* convertArg(llvm::Value* arg, ::hir::ArgConversion conv);
    llvm::Value* coerceArgToType(llvm::Value* arg, llvm::Type* expectedType,
                                  const HIROperand& operand,
                                  std::shared_ptr<HIRType> calleeParamType = nullptr);
    llvm::Value* handleReturn(llvm::Value* result, ::hir::ReturnHandling handling);

    // Variadic function lowering helpers
    llvm::Value* lowerTypeDispatchCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec);
    llvm::Value* lowerPackArrayCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec);
    std::string getTypeSuffix(llvm::Value* arg, const ::hir::LoweringSpec& spec);

    // Globals
    void lowerLoadGlobal(HIRInstruction* inst);
    void lowerStoreGlobal(HIRInstruction* inst);
    void lowerLoadFunction(HIRInstruction* inst);

    // Closures
    void lowerMakeClosure(HIRInstruction* inst);
    void lowerLoadCapture(HIRInstruction* inst);
    void lowerStoreCapture(HIRInstruction* inst);
    void lowerLoadCaptureFromClosure(HIRInstruction* inst);
    void lowerStoreCaptureFromClosure(HIRInstruction* inst);

    // Create a TsClosure for a function (trampoline, arity, name setup)
    llvm::Value* createClosureForFunction(const std::string& funcName, llvm::Function* fn);

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
    // "use fast" Phase 2c: emit ts_native_arena_release(arenaMarker_) at the
    // current insert point if a Temp arena frame is open. No-op otherwise.
    void emitArenaReleaseIfFast();
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

    // Pin a GC pointer to an entry-block stack alloca so the conservative
    // GC scanner can see it. Returns a load from the alloca.
    // Use this for any pointer-type intermediate value that is live across
    // a runtime call that might trigger GC (i.e., any allocating call).
    llvm::Value* gcPin(llvm::Value* ptr, const char* name = "gc.pin");

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
