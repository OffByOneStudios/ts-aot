//==============================================================================
// HIRToLLVM.cpp - Lower HIR to LLVM IR
//==============================================================================

#include "HIRToLLVM_Internal.h"

namespace ts::hir {

//==============================================================================
// Constructor
//==============================================================================

HIRToLLVM::HIRToLLVM(llvm::LLVMContext& ctx)
    : context_(ctx)
    , builder_(std::make_unique<llvm::IRBuilder<>>(ctx))
{
    // GEN-001 Stage 8: suspendable async-generator lowering is the DEFAULT
    // (lane measured +91 net vs the eager baseline, 93 wins / 2 flaky).
    // TSAOT_SUSPEND_AGEN=0 selects the eager fallback (rollback = flip the
    // default below; the eager path remains fully intact until Stage 9).
    suspendAsyncGen_ = true;
    if (const char* sa = std::getenv("TSAOT_SUSPEND_AGEN"); sa && sa[0] == '0') {
        suspendAsyncGen_ = false;
    }
}

//==============================================================================
// GC Statepoint Helpers
//==============================================================================

llvm::PointerType* HIRToLLVM::getGCPtrTy() {
    return llvm::PointerType::get(context_, enableGCStatepoints_ ? 1 : 0);
}

bool HIRToLLVM::isGCManagedType(HIRTypeKind kind) {
    switch (kind) {
        case HIRTypeKind::String:
        case HIRTypeKind::Object:
        case HIRTypeKind::Array:
        case HIRTypeKind::Map:
        case HIRTypeKind::Set:
        case HIRTypeKind::Symbol:
        case HIRTypeKind::BigInt:
        case HIRTypeKind::Function:
        case HIRTypeKind::Class:
        case HIRTypeKind::Any:
            return true;
        default:
            return false;
    }
}

llvm::Value* HIRToLLVM::boxPrimitiveToPtr(llvm::Value* val) {
    if (!val) return val;
    auto* ty = val->getType();
    if (ty->isPointerTy()) return val;
    if (ty->isIntegerTy(1)) {
        // ts_value_make_bool's canonical signature is `ptr(i32)` (matches
        // convertArg's Box path). ZExt the i1 to i32 first to avoid a
        // conflicting `ptr(i1)` declaration in the module.
        llvm::Value* ext = builder_->CreateZExt(val, builder_->getInt32Ty(), "bool_ext");
        auto fnTy = llvm::FunctionType::get(getGCPtrTy(),
                                            {builder_->getInt32Ty()}, false);
        auto bf = module_->getOrInsertFunction("ts_value_make_bool", fnTy);
        return builder_->CreateCall(fnTy, bf.getCallee(), {ext});
    }
    if (ty->isIntegerTy(64)) {
        auto fnTy = llvm::FunctionType::get(getGCPtrTy(),
                                            {builder_->getInt64Ty()}, false);
        auto bf = module_->getOrInsertFunction("ts_value_make_int", fnTy);
        return builder_->CreateCall(fnTy, bf.getCallee(), {val});
    }
    if (ty->isDoubleTy()) {
        auto fnTy = llvm::FunctionType::get(getGCPtrTy(),
                                            {builder_->getDoubleTy()}, false);
        auto bf = module_->getOrInsertFunction("ts_value_make_double", fnTy);
        return builder_->CreateCall(fnTy, bf.getCallee(), {val});
    }
    // Other integer widths (i8, i32, etc.) widen to i64 first.
    if (ty->isIntegerTy()) {
        auto widened = builder_->CreateSExt(val, builder_->getInt64Ty(), "widen.i64");
        return boxPrimitiveToPtr(widened);
    }
    // Float widens to double.
    if (ty->isFloatTy()) {
        auto widened = builder_->CreateFPExt(val, builder_->getDoubleTy(), "widen.f64");
        return boxPrimitiveToPtr(widened);
    }
    return val;  // Unknown type — caller will hit verifier and we'll diagnose.
}

llvm::Value* HIRToLLVM::gcPtrToRaw(llvm::Value* val) {
    // No-op in BOTH modes. Default mode never reached the cast anyway (the old
    // code short-circuited on !enableGCStatepoints_), so this is byte-identical
    // for the default build. Under --gc-statepoints, the centralized
    // normalizeRuntimeBoundaryAddrSpaces pass inserts the addrspace(1)->(0) casts
    // at runtime-call boundaries; laundering here additionally STORED the
    // addrspace(0) result into local slots, hiding GC values from RS4GC — after a
    // collection the addrspace(0) local was a stale pointer (the OBJ-LOST/CLO-LOST
    // bug for nested-function GC return values). Keeping values addrspace(1) lets
    // RS4GC root and relocate them.
    return val;
}

llvm::Value* HIRToLLVM::rawToGCPtr(llvm::Value* val) {
    if (!enableGCStatepoints_ || !val->getType()->isPointerTy()) return val;
    if (val->getType()->getPointerAddressSpace() != 0) return val;
    return builder_->CreateAddrSpaceCast(val, getGCPtrTy(), "raw.to.gc");
}

llvm::Value* HIRToLLVM::createRuntimeCall(llvm::FunctionCallee fn,
                                           llvm::ArrayRef<llvm::Value*> args,
                                           const llvm::Twine& name) {
    if (!enableGCStatepoints_) {
        return builder_->CreateCall(fn, args, name);
    }

    // Cast GC pointers to raw pointers for runtime function arguments
    std::vector<llvm::Value*> rawArgs;
    rawArgs.reserve(args.size());
    for (auto* arg : args) {
        rawArgs.push_back(gcPtrToRaw(arg));
    }

    // Add deopt operand bundle for RS4GC
    llvm::OperandBundleDef deoptBundle("deopt", llvm::ArrayRef<llvm::Value*>());
    llvm::CallInst* result = builder_->CreateCall(fn, rawArgs, {deoptBundle}, name);

    // If the return type is a pointer, cast from raw to GC pointer
    if (result->getType()->isPointerTy()) {
        return rawToGCPtr(result);
    }
    return result;
}

llvm::CallInst* HIRToLLVM::createCallWithDeopt(llvm::FunctionType* ft, llvm::Value* callee,
                                                llvm::ArrayRef<llvm::Value*> args,
                                                const llvm::Twine& name) {
    if (enableGCStatepoints_) {
        llvm::OperandBundleDef deoptBundle("deopt", llvm::ArrayRef<llvm::Value*>());
        return builder_->CreateCall(ft, callee, args, {deoptBundle}, name);
    }
    return builder_->CreateCall(ft, callee, args, name);
}

//==============================================================================
// Main Entry Point
//==============================================================================

std::unique_ptr<llvm::Module> HIRToLLVM::lower(HIRModule* hirModule, const std::string& moduleName) {
    hirModule_ = hirModule;
    module_ = std::make_unique<llvm::Module>(moduleName, context_);
    closureCache_.clear();
    globalMap_.clear();

    // Initialize debug info if enabled
    if (emitDebugInfo_) {
        diBuilder_ = std::make_unique<llvm::DIBuilder>(*module_);
        diFile_ = getOrCreateDIFile(hirModule->sourcePath);
        diCompileUnit_ = diBuilder_->createCompileUnit(
            llvm::dwarf::DW_LANG_C_plus_plus,
            diFile_,
            "ts-aot",       // Producer
            false,           // isOptimized
            "",              // Flags
            0                // Runtime version
        );
        module_->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                               llvm::DEBUG_METADATA_VERSION);
#ifdef _WIN32
        // Windows: CodeView format for VS/WinDbg
        module_->addModuleFlag(llvm::Module::Warning, "CodeView", 1);
#else
        // Linux/Mac: DWARF format for GDB/LLDB
        module_->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);
#endif
    }

    // Declare llvm.instrprof.increment intrinsic for coverage
    if (emitCoverage_) {
        llvm::Type* instrProfArgs[] = {
            getGCPtrTy(),    // i8* function name
            builder_->getInt64Ty(),  // i64 hash
            builder_->getInt32Ty(),  // i32 num_counters
            builder_->getInt32Ty()   // i32 counter_idx
        };
        llvm::FunctionType* instrProfTy = llvm::FunctionType::get(
            builder_->getVoidTy(), instrProfArgs, false);
        instrProfIncrement_ = llvm::Function::Create(
            instrProfTy, llvm::Function::ExternalLinkage,
            "llvm.instrprof.increment", module_.get());
    }

    // Initialize TsValue type
    initTsValueType();

    // Emit ICU data path global if set (allows runtime to find icudt74l.dat).
    // Skip in prelude-object mode: the user object always provides this symbol;
    // emitting it here too would be a duplicate definition at link time.
    if (!icuDataPath_.empty() && !preludeObject_) {
        auto* strConst = llvm::ConstantDataArray::getString(context_, icuDataPath_, true);
        new llvm::GlobalVariable(
            *module_, strConst->getType(), true,
            llvm::GlobalValue::ExternalLinkage, strConst, "__ts_icu_data_path");
    }

    // Pre-create all global variables before lowering functions
    // This ensures each global is created exactly once with the correct name
    for (const auto& [name, type] : hirModule->globals) {
        getOrCreateGlobal(name, type);
    }

    // GC root registration will be done after all functions are lowered
    // (see end of this function) since globals may be created during lowering.

    // Forward-declare all functions first
    // This is necessary because functions may call each other before they are defined
    for (size_t i = 0; i < hirModule->functions.size(); ++i) {
        auto& fn = hirModule->functions[i];
        forwardDeclareFunction(fn.get());
        // Store HIR parameter types for each user function
        std::vector<std::shared_ptr<HIRType>> paramTypes;
        for (auto& [name, type] : fn->params) {
            paramTypes.push_back(type);
        }
        userFunctionParams_[fn->mangledName] = std::move(paramTypes);
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
        vtableFieldTypes.push_back(getGCPtrTy());  // Parent VTable
        if (hirClass->baseClass) {
            std::string baseVTableGlobalName = hirClass->baseClass->name + "_VTable_Global";
            // Get or create reference to base class vtable
            llvm::Constant* baseVTable = module_->getOrInsertGlobal(baseVTableGlobalName, getGCPtrTy());
            vtableFuncs.push_back(baseVTable);
        } else {
            vtableFuncs.push_back(llvm::ConstantPointerNull::get(getGCPtrTy()));
        }

        // Add function pointers for each method in vtable
        for (const auto& [methodName, methodFunc] : hirClass->vtable) {
            vtableFieldTypes.push_back(getGCPtrTy());

            if (methodFunc) {
                // Get the forward-declared function
                llvm::Function* llvmFunc = module_->getFunction(methodFunc->mangledName);
                if (llvmFunc) {
                    vtableFuncs.push_back(llvmFunc);
                } else {
                    // Function not found - use null pointer
                    vtableFuncs.push_back(llvm::ConstantPointerNull::get(getGCPtrTy()));
                    SPDLOG_WARN("VTable method {} not found for class {}", methodFunc->mangledName, hirClass->name);
                }
            } else {
                // Abstract method - use null pointer
                vtableFuncs.push_back(llvm::ConstantPointerNull::get(getGCPtrTy()));
            }
        }

        // Create vtable struct type (always has at least the parent pointer)
        llvm::StructType* vtableStruct = llvm::StructType::create(context_, vtableFieldTypes, hirClass->name + "_VTable");

        // Create vtable global. If a forward declaration already exists
        // (e.g., from `class X extends X` self-extension or
        // `class A extends B; class B {...}` order-of-declaration), the
        // forward decl was inserted by `getOrInsertGlobal` with type `ptr`
        // and ExternalLinkage. We must replace its uses with the new
        // typed-struct global so the linker doesn't end up with a
        // dangling external `X_VTable_Global` reference (LLVM otherwise
        // auto-renames the new global to `X_VTable_Global.1`).
        llvm::Constant* vtableInit = llvm::ConstantStruct::get(vtableStruct, vtableFuncs);
        llvm::GlobalVariable* existing = module_->getGlobalVariable(vtableGlobalName, /*AllowInternal=*/true);
        auto* newGlobal = new llvm::GlobalVariable(*module_, vtableStruct, true,
                                                    llvm::GlobalValue::ExternalLinkage,
                                                    vtableInit, vtableGlobalName);
        if (existing && existing != newGlobal) {
            // The forward decl used `ptr` type; the new global uses the
            // typed struct. Bitcast-replace and erase the old.
            existing->replaceAllUsesWith(
                llvm::ConstantExpr::getBitCast(newGlobal, existing->getType()));
            existing->eraseFromParent();
            // The new global may have been auto-renamed by LLVM (with a
            // `.1` suffix) because the name was previously taken. Rename
            // it back to the canonical name now that the old extern is
            // gone.
            newGlobal->setName(vtableGlobalName);
        }

        SPDLOG_DEBUG("Created VTable global: {} with {} entries (+ parent ptr)", vtableGlobalName, hirClass->vtable.size());
    }


    // Lower all functions
    for (size_t fi = 0; fi < hirModule->functions.size(); ++fi) {
        auto& fn = hirModule->functions[fi];
        lowerFunction(fn.get());
    }

    // Create main entry point
    createMainFunction();

    // Register GC-root globals so the garbage collector scans them. Without
    // this, GC pointers stored in module-level data-segment globals can be
    // collected — or, under the moving nursery, left pointing at a stale
    // (promoted, then zeroed) nursery address. Two families need rooting:
    //   __modvar_*        — module-level let/const/var bindings
    //   __closure_cache_* — per-function cached closure identity (function
    //                       declarations share one closure object). These are
    //                       long-lived and frequently promoted, so an
    //                       unregistered cache slot becomes a dangling pointer
    //                       to a zeroed nursery slot after a minor GC.
    {
        llvm::FunctionType* regFt = llvm::FunctionType::get(
            llvm::Type::getVoidTy(context_), {llvm::PointerType::get(context_, 0)}, false);
        llvm::FunctionCallee regFn = module_->getOrInsertFunction("ts_gc_register_root", regFt);

        llvm::FunctionType* ctorFt = llvm::FunctionType::get(
            llvm::Type::getVoidTy(context_), false);
        llvm::Function* ctorFn = llvm::Function::Create(
            ctorFt, llvm::Function::InternalLinkage, "__ts_register_gc_roots", module_.get());
        llvm::BasicBlock* ctorBB = llvm::BasicBlock::Create(context_, "entry", ctorFn);
        llvm::IRBuilder<> ctorBuilder(ctorBB);

        for (auto& gv : module_->globals()) {
            if (gv.getName().starts_with("__modvar_") ||
                gv.getName().starts_with("__closure_cache_") ||
                gv.getName().starts_with("__enumobj_")) {
                ctorBuilder.CreateCall(regFt, regFn.getCallee(), {&gv});
            }
        }
        ctorBuilder.CreateRetVoid();

        llvm::appendToGlobalCtors(*module_, ctorFn, 65535);
    }

    // Emit coverage: register atexit handler to write profraw
    if (emitCoverage_) {
        emitCoverageMapping();

        // Create a global constructor that registers atexit(__ts_profile_write)
        // and starts a background flush thread via __ts_profile_start_flush().
        // The flush thread writes profraw every 2s so coverage survives kills.
        llvm::FunctionType* voidFt = llvm::FunctionType::get(
            builder_->getVoidTy(), false);
        llvm::FunctionCallee writeProfileFn = module_->getOrInsertFunction(
            "__ts_profile_write", voidFt);
        llvm::FunctionCallee startFlushFn = module_->getOrInsertFunction(
            "__ts_profile_start_flush", voidFt);

        llvm::FunctionType* atexitFt = llvm::FunctionType::get(
            builder_->getInt32Ty(), {getGCPtrTy()}, false);
        llvm::FunctionCallee atexitFn = module_->getOrInsertFunction("atexit", atexitFt);

        llvm::Function* ctorFn = llvm::Function::Create(
            voidFt, llvm::Function::InternalLinkage, "__ts_coverage_init", module_.get());
        llvm::BasicBlock* ctorBB = llvm::BasicBlock::Create(context_, "entry", ctorFn);
        llvm::IRBuilder<> ctorBuilder(ctorBB);
        ctorBuilder.CreateCall(atexitFt, atexitFn.getCallee(),
                               {writeProfileFn.getCallee()});
        ctorBuilder.CreateCall(voidFt, startFlushFn.getCallee());
        ctorBuilder.CreateRetVoid();

        llvm::appendToGlobalCtors(*module_, ctorFn, 65534);
    }

    // Finalize debug info
    if (diBuilder_) {
        diBuilder_->finalize();
    }

    return std::move(module_);
}

void HIRToLLVM::createMainFunction() {
    // Look for the synthetic user_main first (created by Monomorphizer),
    // fall back to user-defined user_main
    llvm::Function* userMain = module_->getFunction("__synthetic_user_main");
    if (!userMain) {
        userMain = module_->getFunction("user_main");
    }

    if (!userMain) {
        SPDLOG_WARN("No __synthetic_user_main or user_main function found, skipping main entry point generation");
        return;
    }

    // Entry point: normally `int main(argc, argv)` which calls ts_main(...).
    // In --prelude-object mode we instead emit `void ts_prelude_init()` (no main,
    // no ts_main) — the runtime's ts_main calls it after init, before user_main.
    llvm::Value* argc = nullptr;
    llvm::Value* argv = nullptr;
    if (preludeObject_) {
        // Internalize the synthetic main so it doesn't collide with the user
        // object's user_main / __synthetic_user_main when both are linked.
        userMain->setLinkage(llvm::GlobalValue::InternalLinkage);
        llvm::FunctionType* initFt =
            llvm::FunctionType::get(builder_->getVoidTy(), {}, false);
        llvm::Function* initFn = llvm::Function::Create(
            initFt, llvm::Function::ExternalLinkage, "ts_prelude_init", module_.get());
        if (enableGCStatepoints_) initFn->setGC("ts-aot-gc");
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "entry", initFn);
        builder_->SetInsertPoint(entryBB);
    } else {
        // Define main: int main(int argc, char** argv)
        std::vector<llvm::Type*> mainArgs = {
            llvm::Type::getInt32Ty(context_),    // argc
            getGCPtrTy()                  // argv
        };
        llvm::FunctionType* mainFt = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(context_), mainArgs, false);
        llvm::Function* mainFn = llvm::Function::Create(
            mainFt, llvm::Function::ExternalLinkage, "main", module_.get());
        if (enableGCStatepoints_) {
            mainFn->setGC("ts-aot-gc");
        }
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "entry", mainFn);
        builder_->SetInsertPoint(entryBB);
        argc = mainFn->getArg(0);
        argv = mainFn->getArg(1);
    }

    // Call all ___static_init functions before ts_main (for decorators, etc.)
    // Static init signature: void ClassName___static_init(void* ctx)
    llvm::FunctionType* staticInitFt = llvm::FunctionType::get(
        builder_->getVoidTy(),
        { getGCPtrTy() },  // ctx parameter
        false
    );
    llvm::Value* nullCtx = llvm::ConstantPointerNull::get(getGCPtrTy());

    for (auto& fn : module_->functions()) {
        std::string fnName = fn.getName().str();
        if (fnName.size() > 14 && fnName.substr(fnName.size() - 14) == "___static_init") {
            SPDLOG_DEBUG("Calling static init: {}", fnName);
            builder_->CreateCall(staticInitFt, &fn, { nullCtx });
        }
    }

    // Register flat object shapes before ts_main
    if (hirModule_ && !hirModule_->shapes.empty()) {
        // Declare ts_shape_register(uint32_t shapeId, ShapeDescriptor* desc)
        llvm::FunctionType* registerFT = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { builder_->getInt32Ty(), getGCPtrTy() },
            false
        );
        llvm::FunctionCallee registerFn = module_->getOrInsertFunction("ts_shape_register", registerFT);

        // ShapeDescriptor struct type:
        // { i32 magic, i32 numSlots, ptr propNames, i32 numMethods, ptr methodNames, ptr constructorSlot }
        llvm::StructType* shapeDescTy = llvm::StructType::get(context_, {
            builder_->getInt32Ty(),  // magic
            builder_->getInt32Ty(),  // numSlots
            getGCPtrTy(),    // propNames
            builder_->getInt32Ty(),  // numMethods
            getGCPtrTy(),    // methodNames
            getGCPtrTy()     // constructorSlot (address of __closure_cache_<ClassName>_constructor; null for non-classes)
        });

        for (auto& shape : hirModule_->shapes) {
            uint32_t numSlots = (uint32_t)shape->propertyOffsets.size();

            // Build ordered list of property names (sorted by slot index)
            std::vector<std::pair<uint32_t, std::string>> orderedProps;
            for (auto& [name, idx] : shape->propertyOffsets) {
                orderedProps.push_back({idx, name});
            }
            std::sort(orderedProps.begin(), orderedProps.end());

            // Emit global string constants for each property name.
            // Private fields ("#x") use the hidden storage key "\x01#x" so
            // the inline slot matches the prefixed writes (ASTToHIR
            // privateStorageKey) and never surfaces as an own property key
            // (hasOwnProperty / getOwnPropertyNames — B-lever).
            std::vector<llvm::Constant*> namePtrs;
            for (auto& [idx, name] : orderedProps) {
                std::string storageName = name;
                // CLASS shapes only: a '#'-name in a class is a private field
                // by grammar. Object-literal shapes (className empty) can hold
                // "#x" as a legitimate string key — must stay visible.
                if (!shape->className.empty() &&
                    !storageName.empty() && storageName[0] == '#') {
                    storageName = std::string("\x01") + storageName;
                }
                auto* strConst = llvm::ConstantDataArray::getString(context_, storageName, true);
                auto* strGlobal = new llvm::GlobalVariable(
                    *module_, strConst->getType(), true,
                    llvm::GlobalValue::PrivateLinkage, strConst,
                    "flat.prop." + std::to_string(shape->id) + "." + name);
                namePtrs.push_back(strGlobal);
            }

            // Emit global array of property name pointers
            auto* ptrArrayTy = llvm::ArrayType::get(getGCPtrTy(), numSlots);
            auto* ptrArray = llvm::ConstantArray::get(ptrArrayTy, namePtrs);
            auto* nameArrayGlobal = new llvm::GlobalVariable(
                *module_, ptrArrayTy, true,
                llvm::GlobalValue::PrivateLinkage, ptrArray,
                "flat.names." + std::to_string(shape->id));

            // Find the HIRClass for this shape to get vtable method names
            uint32_t numMethods = 0;
            llvm::Constant* methodNameArrayGlobal = llvm::ConstantPointerNull::get(getGCPtrTy());

            if (!shape->className.empty()) {
                for (auto& hirClass : hirModule_->classes) {
                    if (hirClass->name == shape->className && !hirClass->vtable.empty()) {
                        numMethods = (uint32_t)hirClass->vtable.size();

                        // Emit method name string constants
                        std::vector<llvm::Constant*> methodNamePtrs;
                        for (auto& [methodName, methodFunc] : hirClass->vtable) {
                            // Private methods/accessors install under the
                            // class-qualified key ("#m@Cls" / "__getter_#m@Cls")
                            // to keep nested-class brands distinct — mirror that
                            // in the runtime method-name table.
                            std::string mangled = methodName;
                            if (!mangled.empty() &&
                                (mangled[0] == '#' ||
                                 mangled.rfind("__getter_#", 0) == 0 ||
                                 mangled.rfind("__setter_#", 0) == 0))
                                mangled += "@" + hirClass->name;
                            auto* mStrConst = llvm::ConstantDataArray::getString(context_, mangled, true);
                            auto* mStrGlobal = new llvm::GlobalVariable(
                                *module_, mStrConst->getType(), true,
                                llvm::GlobalValue::PrivateLinkage, mStrConst,
                                "flat.method." + std::to_string(shape->id) + "." + methodName);
                            methodNamePtrs.push_back(mStrGlobal);
                        }

                        // Emit global array of method name pointers
                        auto* mPtrArrayTy = llvm::ArrayType::get(getGCPtrTy(), numMethods);
                        auto* mPtrArray = llvm::ConstantArray::get(mPtrArrayTy, methodNamePtrs);
                        methodNameArrayGlobal = new llvm::GlobalVariable(
                            *module_, mPtrArrayTy, true,
                            llvm::GlobalValue::PrivateLinkage, mPtrArray,
                            "flat.methodnames." + std::to_string(shape->id));
                        break;
                    }
                }
            }

            // Resolve constructor closure cache slot for class shapes.
            // The cache global is `__closure_cache_<ClassName>_constructor`,
            // created lazily by lowerFunction. Internal-linkage globals
            // require AllowInternal=true on getGlobalVariable, otherwise
            // it returns null and the back-pointer stays unset.
            llvm::Constant* ctorSlotConst = llvm::ConstantPointerNull::get(getGCPtrTy());
            if (!shape->className.empty()) {
                std::string cacheName = "__closure_cache_" + shape->className + "_constructor";
                if (auto* gv = module_->getGlobalVariable(cacheName, /*AllowInternal=*/true)) {
                    ctorSlotConst = gv;
                }
            }

            // Emit ShapeDescriptor struct constant
            auto* descConst = llvm::ConstantStruct::get(shapeDescTy, {
                llvm::ConstantInt::get(builder_->getInt32Ty(), 0x464C4154),  // magic
                llvm::ConstantInt::get(builder_->getInt32Ty(), numSlots),     // numSlots
                nameArrayGlobal,                                              // propNames
                llvm::ConstantInt::get(builder_->getInt32Ty(), numMethods),   // numMethods
                methodNameArrayGlobal,                                        // methodNames
                ctorSlotConst                                                 // constructorSlot
            });
            auto* descGlobal = new llvm::GlobalVariable(
                *module_, shapeDescTy, true,
                llvm::GlobalValue::PrivateLinkage, descConst,
                "flat.desc." + std::to_string(shape->id));

            // Call ts_shape_register(shapeId, &descriptor)
            builder_->CreateCall(registerFT, registerFn.getCallee(), {
                llvm::ConstantInt::get(builder_->getInt32Ty(), shape->id),
                descGlobal
            });
        }
    }

    if (preludeObject_) {
        // The runtime (ts_main) is already initialized when it calls us. Just run
        // the prelude's installs (the internalized synthetic main) and return.
        // Pass a null/zero of the correct type for each of its parameters.
        std::vector<llvm::Value*> umArgs;
        for (auto& p : userMain->args()) {
            umArgs.push_back(llvm::Constant::getNullValue(p.getType()));
        }
        builder_->CreateCall(userMain->getFunctionType(), userMain, umArgs);
        builder_->CreateRetVoid();

        // Internalize every defined symbol except the entry so the prelude
        // object's counter-named functions/globals (__fn_expr_N, etc.) don't
        // collide with the user object's at link time. Runtime symbols the
        // prelude references are declarations (no body/initializer) → stay
        // external and resolve against the runtime.
        for (auto& fn : module_->functions()) {
            if (!fn.isDeclaration() && fn.getName() != "ts_prelude_init") {
                fn.setLinkage(llvm::GlobalValue::InternalLinkage);
            }
        }
        for (auto& gv : module_->globals()) {
            if (gv.hasInitializer()) {
                gv.setLinkage(llvm::GlobalValue::InternalLinkage);
            }
        }
    } else {
        // Weak no-op ts_prelude_init so the EXE always defines the symbol; a
        // linked --prelude-object's strong ts_prelude_init overrides it. We pass
        // it to ts_main by value, so the call works identically whether ts_main
        // lives in the static runtime or the shared-runtime DLL (no cross-module
        // symbol lookup) — one linking strategy for both runtimes.
        llvm::Function* preludeInit = module_->getFunction("ts_prelude_init");
        if (!preludeInit) {
            llvm::FunctionType* piFt =
                llvm::FunctionType::get(builder_->getVoidTy(), {}, false);
            preludeInit = llvm::Function::Create(
                piFt, llvm::Function::WeakAnyLinkage, "ts_prelude_init", module_.get());
            llvm::BasicBlock* piBB = llvm::BasicBlock::Create(context_, "entry", preludeInit);
            llvm::IRBuilder<> piB(piBB);
            piB.CreateRetVoid();
        }

        // Call ts_main(argc, argv, user_main, prelude_init)
        std::vector<llvm::Type*> tsMainArgs = {
            llvm::Type::getInt32Ty(context_), getGCPtrTy(), getGCPtrTy(), getGCPtrTy()
        };
        llvm::FunctionType* tsMainFt = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(context_), tsMainArgs, false);
        llvm::FunctionCallee tsMain = module_->getOrInsertFunction("ts_main", tsMainFt);
        llvm::Value* result = builder_->CreateCall(
            tsMainFt, tsMain.getCallee(), { argc, argv, userMain, preludeInit });
        builder_->CreateRet(result);
    }
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
    if (!type) return getGCPtrTy();
    return getLLVMType(type->kind);
}

llvm::Type* HIRToLLVM::getLLVMType(HIRTypeKind kind) {
    // NOTE: When --gc-statepoints is fully implemented, GC-managed types
    // will return getGCPtrTy() (addrspace 1) instead of getPtrTy() (addrspace 0).
    // For now, all pointers use addrspace 0 to avoid type mismatches at the
    // 600+ runtime function call boundaries. The address space conversion
    // will be added incrementally in a follow-up step.
    switch (kind) {
        case HIRTypeKind::Void:    return builder_->getVoidTy();
        case HIRTypeKind::Bool:    return builder_->getInt1Ty();
        case HIRTypeKind::Int64:   return builder_->getInt64Ty();
        case HIRTypeKind::Float64: return builder_->getDoubleTy();
        case HIRTypeKind::String:  return getGCPtrTy();  // TsString*
        case HIRTypeKind::Object:  return getGCPtrTy();  // TsObject*
        case HIRTypeKind::Array:   return getGCPtrTy();  // TsArray*
        case HIRTypeKind::Map:     return getGCPtrTy();  // TsMap*
        case HIRTypeKind::Set:     return getGCPtrTy();  // TsSet*
        case HIRTypeKind::Symbol:  return getGCPtrTy();  // TsSymbol*
        case HIRTypeKind::BigInt:  return getGCPtrTy();  // TsBigInt*
        case HIRTypeKind::Function: return getGCPtrTy(); // Function pointer
        case HIRTypeKind::Class:   return getGCPtrTy();  // Class instance
        case HIRTypeKind::Any:     return getGCPtrTy();  // TsValue*
        case HIRTypeKind::Ptr:     return getGCPtrTy();  // Raw pointer
        default: return getGCPtrTy();
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

    // Cross-yield SSA spill: for values that were marked as cross-yield-live
    // in the generator pre-pass, ALWAYS load from the heap-backed spill slot.
    // The slot's GEP was created in impl_entry so it dominates every block;
    // the load happens at the use site so it lives in the using block.
    // This avoids the "Instruction does not dominate all uses!" failures that
    // arise when a value defined pre-yield is referenced post-yield, because
    // the post-yield block is reached directly from impl_entry via the
    // state-switch (bypassing the pre-yield definition's block).
    auto spillIt = crossYieldSlotOf_.find(hirValue->id);
    if (spillIt != crossYieldSlotOf_.end() && spillIt->second < crossYieldSlotGEPs_.size()) {
        llvm::Value* slotGEP = crossYieldSlotGEPs_[spillIt->second];
        llvm::Value* loaded = builder_->CreateLoad(getGCPtrTy(), slotGEP, "spill_load");
        // Unbox to the original LLVM type if the value was boxed at spill time.
        auto typeIt = crossYieldSlotType_.find(hirValue->id);
        if (typeIt != crossYieldSlotType_.end() && typeIt->second != loaded->getType()) {
            llvm::Type* origType = typeIt->second;
            if (origType->isIntegerTy(64)) {
                auto ft = llvm::FunctionType::get(builder_->getInt64Ty(), { getGCPtrTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_get_int", ft);
                loaded = builder_->CreateCall(ft, fn.getCallee(), { loaded }, "spill_unbox_i64");
            } else if (origType->isDoubleTy()) {
                auto ft = llvm::FunctionType::get(builder_->getDoubleTy(), { getGCPtrTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_get_double", ft);
                loaded = builder_->CreateCall(ft, fn.getCallee(), { loaded }, "spill_unbox_f64");
            } else if (origType->isIntegerTy(1)) {
                auto ft = llvm::FunctionType::get(builder_->getInt1Ty(), { getGCPtrTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_get_bool", ft);
                loaded = builder_->CreateCall(ft, fn.getCallee(), { loaded }, "spill_unbox_bool");
            } else if (origType->isIntegerTy()) {
                auto ft = llvm::FunctionType::get(builder_->getInt64Ty(), { getGCPtrTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_get_int", ft);
                loaded = builder_->CreateCall(ft, fn.getCallee(), { loaded }, "spill_unbox_iN");
                if (origType != builder_->getInt64Ty()) {
                    loaded = builder_->CreateTrunc(loaded, origType, "spill_trunc");
                }
            }
        }
        return loaded;
    }

    auto it = valueMap_.find(hirValue->id);
    if (it != valueMap_.end()) {
        // If this value has a GC pin alloca, reload from it.
        // This ensures we get the latest value from the stack slot
        // (which the conservative GC scanner can see), not a stale register.
        auto pinIt = gcPinAllocas_.find(hirValue->id);
        if (pinIt != gcPinAllocas_.end()) {
            return builder_->CreateLoad(getGCPtrTy(), pinIt->second, "gc.reload");
        }
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
    // A void-typed slot (e.g. `var x: void` / `var r = voidA && voidB`) holds
    // undefined at runtime; LLVM cannot create a void global (getNullValue
    // of void crashes). Same widening lowerStore applies to void allocas.
    if (llvmType->isVoidTy()) llvmType = getGCPtrTy();
    llvm::Constant* init = llvm::Constant::getNullValue(llvmType);
    // Module-level let/const (__modvar_*): seed with the TDZ sentinel
    // (NANBOX_TDZ = 0x9) so reads before the declaration executes are
    // distinguishable from undefined (ts_tdz_check throws ReferenceError).
    if (hirModule_ && hirModule_->tdzGlobals.count(name) && llvmType->isPointerTy()) {
        init = llvm::ConstantExpr::getIntToPtr(
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 9),
            llvmType);
    }
    llvm::GlobalVariable* gv = new llvm::GlobalVariable(
        *module_,
        llvmType,
        false,  // Not constant
        llvm::GlobalValue::InternalLinkage,
        init,
        name
    );

    // Cache it in our map
    globalMap_[name] = gv;
    return gv;
}

void HIRToLLVM::setValue(const std::shared_ptr<HIRValue>& hirValue, llvm::Value* llvmValue) {
    if (hirValue) {
        valueMap_[hirValue->id] = llvmValue;

        // Cross-yield SSA spill: if this value was marked as cross-yield-live
        // by the generator pre-pass, also store it into its slot in the
        // heap-backed data buffer so it survives across the impl-function's
        // suspend/resume. Non-ptr types are boxed via ts_value_make_*; the
        // slot is uniformly `ptr` to keep GC scanning of the buffer correct.
        if (llvmValue) {
            auto spillIt = crossYieldSlotOf_.find(hirValue->id);
            if (spillIt != crossYieldSlotOf_.end() && spillIt->second < crossYieldSlotGEPs_.size() &&
                crossYieldSlotGEPs_[spillIt->second] != nullptr) {
                // Remember the original LLVM type so getValue can unbox correctly.
                crossYieldSlotType_[hirValue->id] = llvmValue->getType();
                llvm::Value* toStore = llvmValue;
                llvm::Type* ty = toStore->getType();
                if (!ty->isPointerTy()) {
                    if (ty->isIntegerTy(64)) {
                        auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt64Ty() }, false);
                        auto fn = module_->getOrInsertFunction("ts_value_make_int", ft);
                        toStore = builder_->CreateCall(ft, fn.getCallee(), { toStore }, "spill_box_i64");
                    } else if (ty->isDoubleTy()) {
                        auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getDoubleTy() }, false);
                        auto fn = module_->getOrInsertFunction("ts_value_make_double", ft);
                        toStore = builder_->CreateCall(ft, fn.getCallee(), { toStore }, "spill_box_f64");
                    } else if (ty->isIntegerTy(1)) {
                        auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt1Ty() }, false);
                        auto fn = module_->getOrInsertFunction("ts_value_make_bool", ft);
                        toStore = builder_->CreateCall(ft, fn.getCallee(), { toStore }, "spill_box_bool");
                    } else if (ty->isIntegerTy()) {
                        llvm::Value* widened = builder_->CreateZExt(toStore, builder_->getInt64Ty(), "spill_zext");
                        auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt64Ty() }, false);
                        auto fn = module_->getOrInsertFunction("ts_value_make_int", ft);
                        toStore = builder_->CreateCall(ft, fn.getCallee(), { widened }, "spill_box_iN");
                    } else {
                        // Unhandled type (e.g. struct) — don't spill, will fail loudly later.
                        toStore = nullptr;
                    }
                }
                if (toStore) {
                    builder_->CreateStore(toStore, crossYieldSlotGEPs_[spillIt->second]);
                }
            }
        }

        // GC root pinning: if this is a pointer-type value from a runtime call
        // (i.e., potentially a GC-allocated object), store it to a stack alloca
        // so the conservative GC stack scanner can see it.
        // Skip constants (GlobalVariable, ConstantPointerNull, etc.) and allocas
        // (already on stack) — only pin CallInst results and loads from GC pointers.
        // IMPORTANT: Skip for generator/async impl functions — their state machine
        // saves/restores across yields, so stack allocas don't survive between calls.
        // Their state is already in the GC-tracked async context data buffer.
        if (llvmValue && llvmValue->getType()->isPointerTy() &&
            currentFunction_ &&
            !generatorDataBuf_ &&
            !llvm::isa<llvm::Constant>(llvmValue) &&
            !llvm::isa<llvm::AllocaInst>(llvmValue) &&
            !llvm::isa<llvm::Argument>(llvmValue)) {

            // Create an alloca in the entry block for this GC root
            llvm::AllocaInst* pin;
            {
                llvm::IRBuilder<>::InsertPointGuard guard(*builder_);
                llvm::BasicBlock* entryBB = &currentFunction_->getEntryBlock();
                builder_->SetInsertPoint(entryBB, entryBB->getFirstInsertionPt());
                pin = builder_->CreateAlloca(getGCPtrTy(), nullptr, "gc.pin");
            }
            builder_->CreateStore(llvmValue, pin);
            gcPinAllocas_[hirValue->id] = pin;
        }
    }
}

llvm::BasicBlock* HIRToLLVM::getBlock(HIRBlock* hirBlock) {
    if (!hirBlock) return nullptr;
    auto it = blockMap_.find(hirBlock);
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
    llvm::Type* returnType = (fn->isAsync || fn->isGenerator) ? getGCPtrTy() : getLLVMType(fn->returnType);
    std::vector<llvm::Type*> paramTypes;

    // Check if the function already has a hidden closure parameter from ASTToHIR
    // Arrow functions and function expressions add __closure__ as first param for call_indirect
    bool hasHiddenClosureParam = (!fn->params.empty() && fn->params[0].first == "__closure__");

    // If the HIR return type resolved to void, check if the function actually has
    // Return (non-void) instructions. This happens for untyped JavaScript functions
    // where the analyzer doesn't infer return types. Upgrade to ptr to avoid LLVM
    // verifier errors ("Found return instr that returns non-void in void function").
    if (returnType->isVoidTy()) {
        for (auto& block : fn->blocks) {
            for (auto& inst : block->instructions) {
                if (inst->opcode == HIROpcode::Return) {
                    returnType = getGCPtrTy();
                    goto returnTypeFixed;
                }
            }
        }
        returnTypeFixed:;
    }

    // If this function has captures AND doesn't already have a closure param, add one
    if (!fn->captures.empty() && !hasHiddenClosureParam) {
        paramTypes.push_back(getGCPtrTy());  // TsClosure* __closure
    }

    for (auto& param : fn->params) {
        paramTypes.push_back(getLLVMType(param.second));
    }

    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    auto* func = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        fn->mangledName,
        module_.get()
    );
    if (enableGCStatepoints_) {
        func->setGC("ts-aot-gc");
    }

    // Attach debug info (DISubprogram) to function
    if (diBuilder_ && fn->sourceLine > 0) {
        llvm::DIFile* fnFile = fn->sourceFile.empty() ? diFile_ :
                               getOrCreateDIFile(fn->sourceFile);
        llvm::DISubroutineType* fnDebugType = createFunctionDebugType(fn);
        llvm::DISubprogram* sp = diBuilder_->createFunction(
            fnFile,
            fn->displayName.empty() ? fn->name : fn->displayName,
            fn->mangledName,
            fnFile,
            fn->sourceLine,
            fnDebugType,
            fn->sourceLine,
            llvm::DINode::FlagPrototyped,
            llvm::DISubprogram::SPFlagDefinition
        );
        func->setSubprogram(sp);
    }

    // Set param names immediately so getOrCreateTrampoline can detect closure
    // params before function bodies are emitted (e.g., during module init lowering).
    auto argIt = func->arg_begin();
    if (!fn->captures.empty() && !hasHiddenClosureParam) {
        argIt->setName("__closure");
        ++argIt;
    }
    for (size_t i = 0; i < fn->params.size(); ++i, ++argIt) {
        if (argIt == func->arg_end()) break;
        argIt->setName(fn->params[i].first);
    }
}

//==============================================================================
// Generator state-machine lowering helpers (GEN-001 Stage 1 extraction).
// These are a zero-behavior extraction of the sync-generator branch of
// lowerFunction; the suspendable async-generator path (GEN-001 Stage 3+) will
// reuse them with different GeneratorLoweringOpts.
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

llvm::FunctionCallee HIRToLLVM::getOrCreateNurseryAllocFn() {
    // Check if already created in this module
    if (auto* existing = module_->getFunction("__ts_nursery_alloc")) {
        return llvm::FunctionCallee(existing->getFunctionType(), existing);
    }

    // Create an AlwaysInline function that does nursery bump-pointer with fallback
    llvm::FunctionType* ft = llvm::FunctionType::get(
        getGCPtrTy(), {builder_->getInt64Ty()}, false);
    llvm::Function* fn = llvm::Function::Create(
        ft, llvm::Function::InternalLinkage, "__ts_nursery_alloc", module_.get());
    fn->addFnAttr(llvm::Attribute::AlwaysInline);
    fn->addFnAttr(llvm::Attribute::NoUnwind);

    // Save current insert point
    llvm::IRBuilder<>::InsertPointGuard guard(*builder_);

    // Create basic blocks
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "entry", fn);
    llvm::BasicBlock* fastBB = llvm::BasicBlock::Create(context_, "nursery.fast", fn);
    llvm::BasicBlock* slowBB = llvm::BasicBlock::Create(context_, "nursery.slow", fn);
    llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(context_, "done", fn);

    llvm::Argument* sizeArg = fn->getArg(0);
    sizeArg->setName("size");

    // Entry: load cursor/limit, check if nursery has space
    builder_->SetInsertPoint(entryBB);

    // Align size to 8 bytes: allocSize = (size + 7) & ~7
    llvm::Value* seven = llvm::ConstantInt::get(builder_->getInt64Ty(), 7);
    llvm::Value* mask = llvm::ConstantInt::get(builder_->getInt64Ty(), ~(uint64_t)7);
    llvm::Value* allocSize = builder_->CreateAnd(
        builder_->CreateAdd(sizeArg, seven), mask, "alloc_size");
    // total = allocSize + 8 (size prefix)
    llvm::Value* total = builder_->CreateAdd(
        allocSize, llvm::ConstantInt::get(builder_->getInt64Ty(), 8), "total");

    auto* cursorGlobal = module_->getOrInsertGlobal("ts_nursery_cursor", getGCPtrTy());
    auto* limitGlobal = module_->getOrInsertGlobal("ts_nursery_cursor_limit", getGCPtrTy());

    llvm::Value* cursor = builder_->CreateLoad(getGCPtrTy(), cursorGlobal, "cursor");
    llvm::Value* limit = builder_->CreateLoad(getGCPtrTy(), limitGlobal, "limit");

    // Check if cursor is non-null (nursery enabled) and has space
    llvm::Value* cursorNotNull = builder_->CreateICmpNE(
        cursor, llvm::ConstantPointerNull::get(getGCPtrTy()), "cursor_ok");
    llvm::Value* newCursor = builder_->CreateGEP(
        builder_->getInt8Ty(), cursor, total, "new_cursor");
    llvm::Value* fits = builder_->CreateICmpULE(newCursor, limit, "fits");
    llvm::Value* canAlloc = builder_->CreateAnd(cursorNotNull, fits, "can_alloc");
    builder_->CreateCondBr(canAlloc, fastBB, slowBB);

    // Fast path: bump pointer
    builder_->SetInsertPoint(fastBB);
    // Write size prefix at cursor
    builder_->CreateStore(allocSize, cursor);
    // Object is after the 8-byte prefix
    llvm::Value* objFast = builder_->CreateGEP(
        builder_->getInt8Ty(), cursor,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 8), "obj");
    // Bump cursor
    builder_->CreateStore(newCursor, cursorGlobal);
    // Zero the object memory
    builder_->CreateMemSet(objFast,
        llvm::ConstantInt::get(builder_->getInt8Ty(), 0),
        allocSize, llvm::MaybeAlign(8));
    builder_->CreateBr(doneBB);

    // Slow path: call ts_gc_alloc
    builder_->SetInsertPoint(slowBB);
    auto gcAllocFn = getOrDeclareRuntimeFunction("ts_gc_alloc",
        getGCPtrTy(), {builder_->getInt64Ty()});
    llvm::Value* objSlow = builder_->CreateCall(gcAllocFn, {sizeArg});
    builder_->CreateBr(doneBB);

    // Done: phi merge
    builder_->SetInsertPoint(doneBB);
    llvm::PHINode* phi = builder_->CreatePHI(getGCPtrTy(), 2, "result");
    phi->addIncoming(objFast, fastBB);
    phi->addIncoming(objSlow, slowBB);
    builder_->CreateRet(phi);

    return llvm::FunctionCallee(ft, fn);
}

llvm::FunctionCallee HIRToLLVM::getTsAlloc() {
    return getOrDeclareRuntimeFunction("ts_alloc", getGCPtrTy(), {builder_->getInt64Ty()});
}

llvm::FunctionCallee HIRToLLVM::getTsStringCreate() {
    return getOrDeclareRuntimeFunction("ts_string_create", getGCPtrTy(), {getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeInt() {
    return getOrDeclareRuntimeFunction("ts_value_make_int", getGCPtrTy(), {builder_->getInt64Ty()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeDouble() {
    return getOrDeclareRuntimeFunction("ts_value_make_double", getGCPtrTy(), {builder_->getDoubleTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeBool() {
    return getOrDeclareRuntimeFunction("ts_value_make_bool", getGCPtrTy(), {builder_->getInt32Ty()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeString() {
    return getOrDeclareRuntimeFunction("ts_value_make_string", getGCPtrTy(), {getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeObject() {
    return getOrDeclareRuntimeFunction("ts_value_make_object", getGCPtrTy(), {getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueMakeFunction() {
    // ts_value_make_function(void* funcPtr, void* context) -> TsValue*
    return getOrDeclareRuntimeFunction("ts_value_make_function", getGCPtrTy(), {getGCPtrTy(), getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueGetInt() {
    return getOrDeclareRuntimeFunction("ts_value_get_int", builder_->getInt64Ty(), {getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueGetDouble() {
    return getOrDeclareRuntimeFunction("ts_value_get_double", builder_->getDoubleTy(), {getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueGetBool() {
    return getOrDeclareRuntimeFunction("ts_value_get_bool", builder_->getInt32Ty(), {getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueGetString() {
    return getOrDeclareRuntimeFunction("ts_value_get_string", getGCPtrTy(), {getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsValueGetObject() {
    return getOrDeclareRuntimeFunction("ts_value_get_object", getGCPtrTy(), {getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsArrayCreate() {
    return getOrDeclareRuntimeFunction("ts_array_create_sized", getGCPtrTy(), {builder_->getInt64Ty()});
}

llvm::FunctionCallee HIRToLLVM::getTsArrayGet() {
    return getOrDeclareRuntimeFunction("ts_array_get_unchecked", getGCPtrTy(), {getGCPtrTy(), builder_->getInt64Ty()});
}

llvm::FunctionCallee HIRToLLVM::getTsArraySet() {
    return getOrDeclareRuntimeFunction("ts_array_set_unchecked", builder_->getVoidTy(), {getGCPtrTy(), builder_->getInt64Ty(), getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsArrayLength() {
    return getOrDeclareRuntimeFunction("ts_array_length", builder_->getInt64Ty(), {getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsArrayPush() {
    return getOrDeclareRuntimeFunction("ts_array_push", builder_->getInt64Ty(), {getGCPtrTy(), getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsObjectCreate() {
    // ts_map_create() returns a TsMap* (plain object)
    return getOrDeclareRuntimeFunction("ts_map_create", getGCPtrTy(), {});
}

llvm::FunctionCallee HIRToLLVM::getTsObjectGetProperty() {
    // ts_object_get_dynamic(TsValue* obj, TsValue* key) -> TsValue*
    return getOrDeclareRuntimeFunction("ts_object_get_dynamic_checked", getGCPtrTy(), {getGCPtrTy(), getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsObjectSetProperty() {
    // ts_object_set_dynamic(TsValue* obj, TsValue* key, TsValue* value) -> void
    return getOrDeclareRuntimeFunction("ts_object_set_dynamic_checked", builder_->getVoidTy(), {getGCPtrTy(), getGCPtrTy(), getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsObjectHasProperty() {
    return getOrDeclareRuntimeFunction("ts_object_has_property", builder_->getInt1Ty(), {getGCPtrTy(), getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsObjectDeleteProperty() {
    return getOrDeclareRuntimeFunction("ts_object_delete_property", builder_->getInt32Ty(), {getGCPtrTy(), getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsTypeOf() {
    return getOrDeclareRuntimeFunction("ts_typeof", getGCPtrTy(), {getGCPtrTy()});
}

llvm::FunctionCallee HIRToLLVM::getTsInstanceOf() {
    return getOrDeclareRuntimeFunction("ts_instanceof", builder_->getInt32Ty(), {getGCPtrTy(), getGCPtrTy()});
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
            // Find where this value is defined
            std::string defInfo = "unknown";
            if (currentHIRFunction_) {
                for (auto& block : currentHIRFunction_->blocks) {
                    for (auto& inst : block->instructions) {
                        if (inst->result && inst->result->id == (*val)->id) {
                            defInfo = fmt::format("block={} opcode={}", block->label, static_cast<int>(inst->opcode));
                            break;
                        }
                    }
                }
            }
            SPDLOG_WARN("getOperandValue: HIRValue id={} not found in valueMap_ (func={} use_block={} defined_at={})",
                (*val)->id, currentHIRFunction_ ? currentHIRFunction_->mangledName : "?",
                currentBlockLabel_, defInfo);

            // Return a typed placeholder instead of null to prevent LLVM verification errors.
            // This handles cases where values are defined in blocks not yet lowered
            // (forward references) or orphaned by the AST-to-HIR lowering.
            if ((*val)->type) {
                auto kind = (*val)->type->kind;
                if (kind == HIRTypeKind::Int64) return llvm::ConstantInt::get(builder_->getInt64Ty(), 0);
                if (kind == HIRTypeKind::Float64) return llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0);
                if (kind == HIRTypeKind::Bool) return llvm::ConstantInt::getFalse(builder_->getContext());
            }
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(builder_->getContext(), 0));
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
    SPDLOG_WARN("getOperandValue: unknown operand type index={} in func={} block={} instr={} operand_addr={}",
        operand.index(),
        currentHIRFunction_ ? currentHIRFunction_->mangledName : "?",
        currentBlockLabel_,
        currentInstrIndex_,
        (void*)&operand);
    // Abort here to get a clean stack trace instead of segfaulting later
    abort();
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

llvm::Value* HIRToLLVM::gcPin(llvm::Value* ptr, const char* name) {
    if (!ptr || !ptr->getType()->isPointerTy() || !currentFunction_) return ptr;

    // Create an alloca in the entry block
    llvm::AllocaInst* pin;
    {
        llvm::IRBuilder<>::InsertPointGuard guard(*builder_);
        llvm::BasicBlock* entryBB = &currentFunction_->getEntryBlock();
        builder_->SetInsertPoint(entryBB, entryBB->getFirstInsertionPt());
        pin = builder_->CreateAlloca(getGCPtrTy(), nullptr, name);
    }
    // Store the pointer and return a load from the alloca
    builder_->CreateStore(ptr, pin);
    return builder_->CreateLoad(getGCPtrTy(), pin, std::string(name) + ".ld");
}

llvm::Value* HIRToLLVM::createGlobalString(const std::string& str) {
    // Create a global string constant
    return builder_->CreateGlobalStringPtr(str);
}

//==============================================================================
// Dynamic Method Call Helpers
//==============================================================================

llvm::Value* HIRToLLVM::boxArgumentForDynamicCall(llvm::Value* arg, const HIROperand& operand) {
    // Box based on LLVM type first
    if (!arg->getType()->isPointerTy()) {
        if (arg->getType()->isIntegerTy(64)) {
            auto boxFn = getTsValueMakeInt();
            return builder_->CreateCall(boxFn, {arg});
        } else if (arg->getType()->isDoubleTy()) {
            auto boxFn = getTsValueMakeDouble();
            return builder_->CreateCall(boxFn, {arg});
        } else if (arg->getType()->isIntegerTy(1)) {
            auto boxFn = getTsValueMakeBool();
            llvm::Value* extended = builder_->CreateZExt(arg, builder_->getInt32Ty());
            return builder_->CreateCall(boxFn, {extended});
        }
    } else {
        // Pointer type - check HIR type to determine boxing
        // Strip addrspace(1) for runtime calls
        llvm::Value* rawArg = gcPtrToRaw(arg);
        if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
            auto hirType = (*hirVal)->type;
            if (hirType) {
                if (hirType->kind == HIRTypeKind::String) {
                    auto boxFn = getTsValueMakeString();
                    return builder_->CreateCall(boxFn, {rawArg});
                } else if (hirType->kind == HIRTypeKind::Object ||
                           hirType->kind == HIRTypeKind::Array ||
                           hirType->kind == HIRTypeKind::Class ||
                           hirType->kind == HIRTypeKind::Map ||
                           hirType->kind == HIRTypeKind::Function ||
                           hirType->kind == HIRTypeKind::Ptr) {
                    // Box objects/functions/raw pointers with ts_value_make_object
                    auto boxFn = getTsValueMakeObject();
                    return builder_->CreateCall(boxFn, {rawArg});
                }
                // For Any type, it might already be boxed - pass as-is
            }
        }
    }
    return arg;
}

llvm::Value* HIRToLLVM::emitDynamicMethodCall(llvm::Value* funcVal, llvm::Value* thisArg,
                                              HIRInstruction* inst, size_t argStartIdx) {
    size_t argCount = inst->operands.size() - argStartIdx;

    // Collect and box all arguments
    std::vector<llvm::Value*> boxedArgs;
    for (size_t i = argStartIdx; i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);
        arg = boxArgumentForDynamicCall(arg, inst->operands[i]);
        boxedArgs.push_back(arg);
    }

    // Emit ONE unified ts_call_with_this(func, thisArg, argc, a0..a8): args
    // padded to 9 undefined slots, count explicit (runtime ignores slots
    // >= argc). Collapses the ts_call_with_this_0..8 family and fixes the old
    // >8-args silent-null (now >9; 9-arg method calls work).
    if (argCount <= 9) {
        llvm::Value* undef = builder_->CreateIntToPtr(
            llvm::ConstantInt::get(builder_->getInt64Ty(), 0x0A), getGCPtrTy());
        std::vector<llvm::Value*> callArgs;
        callArgs.push_back(funcVal);
        callArgs.push_back(thisArg);
        callArgs.push_back(llvm::ConstantInt::get(builder_->getInt64Ty(), (int64_t)argCount));
        for (size_t i = 0; i < 9; ++i)
            callArgs.push_back(i < boxedArgs.size() ? boxedArgs[i] : undef);
        std::vector<llvm::Type*> paramTypes = { getGCPtrTy(), getGCPtrTy(), builder_->getInt64Ty() };
        for (int i = 0; i < 9; ++i) paramTypes.push_back(getGCPtrTy());
        auto callFt = llvm::FunctionType::get(getGCPtrTy(), paramTypes, false);
        auto callFn = module_->getOrInsertFunction("ts_call_with_this", callFt);
        return builder_->CreateCall(callFt, callFn.getCallee(), callArgs);
    }
    // >9 args: pack into an argc/argv array and use the variable-arity
    // ts_function_call_with_this(fn, thisArg, argc, argv) entry. (Previously
    // returned null — a silent miscompile for 10+-arg method/.call() sites.)
    auto arrayType = llvm::ArrayType::get(getGCPtrTy(), argCount);
    auto alloca = builder_->CreateAlloca(arrayType);
    for (size_t i = 0; i < argCount; ++i) {
        auto gep = builder_->CreateConstGEP2_32(arrayType, alloca, 0, (unsigned)i);
        builder_->CreateStore(boxedArgs[i], gep);
    }
    auto argvPtr = builder_->CreateConstGEP2_32(arrayType, alloca, 0, 0);
    auto ft = llvm::FunctionType::get(getGCPtrTy(),
        { getGCPtrTy(), getGCPtrTy(), builder_->getInt32Ty(), getGCPtrTy() }, false);
    auto fn = module_->getOrInsertFunction("ts_function_call_with_this", ft);
    return builder_->CreateCall(ft, fn.getCallee(), {
        funcVal, thisArg,
        llvm::ConstantInt::get(builder_->getInt32Ty(), (int)argCount),
        argvPtr
    });
}

//==============================================================================
// Debug Info Helpers
//==============================================================================

llvm::DIFile* HIRToLLVM::getOrCreateDIFile(const std::string& path) {
    if (path.empty()) {
        if (diFile_) return diFile_;
        // Fallback: create a placeholder file
        diFile_ = diBuilder_->createFile("<unknown>", ".");
        return diFile_;
    }
    auto it = diFiles_.find(path);
    if (it != diFiles_.end()) return it->second;

    std::filesystem::path p(path);
    auto* file = diBuilder_->createFile(p.filename().string(), p.parent_path().string());
    diFiles_[path] = file;
    return file;
}

llvm::DISubroutineType* HIRToLLVM::createFunctionDebugType(HIRFunction* fn) {
    // All params as unspecified type (sufficient for line mapping)
    llvm::SmallVector<llvm::Metadata*, 8> types;
    types.push_back(nullptr);  // Return type
    for (size_t i = 0; i < fn->params.size(); ++i) {
        types.push_back(nullptr);  // Parameter types
    }
    return diBuilder_->createSubroutineType(diBuilder_->getOrCreateTypeArray(types));
}

//==============================================================================
// Coverage Instrumentation
//==============================================================================

void HIRToLLVM::emitCoverageIncrement(const std::string& funcName, uint64_t funcHash,
                                       uint32_t numCounters, uint32_t counterIdx) {
    if (!instrProfIncrement_) return;

    // Create or reuse the __profn_ global for this function name
    std::string profnName = "__profn_" + funcName;
    llvm::GlobalVariable* nameVar = module_->getGlobalVariable(profnName);
    if (!nameVar) {
        auto* strConst = llvm::ConstantDataArray::getString(context_, funcName, false);
        nameVar = new llvm::GlobalVariable(
            *module_, strConst->getType(), true,
            llvm::GlobalValue::LinkOnceODRLinkage, strConst, profnName);
        nameVar->setAlignment(llvm::Align(1));
    }

    llvm::Value* args[] = {
        nameVar,
        llvm::ConstantInt::get(builder_->getInt64Ty(), funcHash),
        llvm::ConstantInt::get(builder_->getInt32Ty(), numCounters),
        llvm::ConstantInt::get(builder_->getInt32Ty(), counterIdx)
    };
    builder_->CreateCall(instrProfIncrement_, args);
}

void HIRToLLVM::emitCoverageMapping() {
    // Phase 3: coverage mapping sections will be implemented here.
    // For now, the instrprof intrinsics are emitted and will be lowered
    // by InstrProfilingLoweringPass into counter globals + profraw output.
}

} // namespace ts::hir
