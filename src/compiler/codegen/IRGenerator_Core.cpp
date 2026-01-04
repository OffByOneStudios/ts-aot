#include "IRGenerator.h"
#include <fmt/core.h>
#include <filesystem>
#include <set>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#endif
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Transforms/Scalar/SROA.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/IPO/Inliner.h>
#include <llvm/Transforms/IPO/GlobalDCE.h>
#include <llvm/Analysis/InlineCost.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Linker/Linker.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace ts {
using namespace ast;

IRGenerator::IRGenerator() {
    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("ts_module", *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);
}

void IRGenerator::generate(ast::Program* program, const std::vector<Specialization>& specializations, Analyzer& analyzer, const std::string& sourceFile) {
    this->specializations = specializations;
    this->analyzer = &analyzer;
    this->currentSourceFile = sourceFile;
    concreteTypes.clear();
    lastConcreteType = nullptr;

    // Initialize target for DataLayout
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(targetTriple);

    if (debug) {
        diBuilder = std::make_unique<llvm::DIBuilder>(*module);
        std::filesystem::path p = std::filesystem::absolute(sourceFile);
        std::string fileName = p.filename().string();
        std::string directory = p.parent_path().string();
        
        llvm::DIFile* file = diBuilder->createFile(fileName, directory);
        
        compileUnit = diBuilder->createCompileUnit(
            llvm::dwarf::DW_LANG_C_plus_plus_14,
            file,
            "ts-aot",
            false,
            "",
            0);
        
        module->addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
        if (llvm::Triple(module->getTargetTriple()).isOSBinFormatCOFF()) {
            module->addModuleFlag(llvm::Module::Warning, "CodeView", 1);
            module->addModuleFlag(llvm::Module::Max, "Dwarf Version", (uint32_t)0);
        }
    }

    // Add linker metadata for Debug/Release CRT compatibility
    if (llvm::Triple(module->getTargetTriple()).isOSBinFormatCOFF()) {
        llvm::NamedMDNode *linkerOpts = module->getOrInsertNamedMetadata("llvm.linker.options");
        
        if (debugRuntime) {
            // Static Debug runtime: _ITERATOR_DEBUG_LEVEL=2, link against libcmtd (static debug CRT)
            linkerOpts->addOperand(llvm::MDNode::get(*context, {
                llvm::MDString::get(*context, "/FAILIFMISMATCH:\"_ITERATOR_DEBUG_LEVEL=2\"")
            }));
            linkerOpts->addOperand(llvm::MDNode::get(*context, {
                llvm::MDString::get(*context, "/DEFAULTLIB:libcmtd.lib")
            }));
            linkerOpts->addOperand(llvm::MDNode::get(*context, {
                llvm::MDString::get(*context, "/NODEFAULTLIB:libcmt.lib")
            }));
            linkerOpts->addOperand(llvm::MDNode::get(*context, {
                llvm::MDString::get(*context, "/FAILIFMISMATCH:\"RuntimeLibrary=MTd_StaticDebug\"")
            }));
        } else {
            // Static Release runtime: _ITERATOR_DEBUG_LEVEL=0, link against libcmt (static release CRT)
            linkerOpts->addOperand(llvm::MDNode::get(*context, {
                llvm::MDString::get(*context, "/FAILIFMISMATCH:\"_ITERATOR_DEBUG_LEVEL=0\"")
            }));
            linkerOpts->addOperand(llvm::MDNode::get(*context, {
                llvm::MDString::get(*context, "/DEFAULTLIB:libcmt.lib")
            }));
            linkerOpts->addOperand(llvm::MDNode::get(*context, {
                llvm::MDString::get(*context, "/NODEFAULTLIB:libcmtd.lib")
            }));
            linkerOpts->addOperand(llvm::MDNode::get(*context, {
                llvm::MDString::get(*context, "/FAILIFMISMATCH:\"RuntimeLibrary=MT_StaticRelease\"")
            }));
        }
    }

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
    if (target) {
        llvm::TargetOptions opt;
        auto targetMachine = target->createTargetMachine(targetTriple, "generic", "", opt, std::nullopt, std::nullopt, llvm::CodeGenOptLevel::None);
        module->setDataLayout(targetMachine->createDataLayout());
    }

    if (optLevel != "0") {
        llvm::FastMathFlags fmf;
        fmf.setFast();
        builder->setFastMathFlags(fmf);
    }

    // Register runtime symbols for TypedArray and DataView
    auto ptrTy = builder->getPtrTy();
    auto addRuntimeSymbol = [&](const std::string& name, llvm::Type* retTy, std::vector<llvm::Type*> argTypes) {
        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, argTypes, false);
        module->getOrInsertFunction(name, ft);
    };

    // TypedArray
    addRuntimeSymbol("ts_typed_array_get_u8", builder->getInt8Ty(), {ptrTy, builder->getInt64Ty()});
    addRuntimeSymbol("ts_typed_array_set_u8", builder->getVoidTy(), {ptrTy, builder->getInt64Ty(), builder->getInt8Ty()});
    addRuntimeSymbol("ts_typed_array_get_u32", builder->getInt32Ty(), {ptrTy, builder->getInt64Ty()});
    addRuntimeSymbol("ts_typed_array_set_u32", builder->getVoidTy(), {ptrTy, builder->getInt64Ty(), builder->getInt32Ty()});
    addRuntimeSymbol("ts_typed_array_get_f64", builder->getDoubleTy(), {ptrTy, builder->getInt64Ty()});
    addRuntimeSymbol("ts_typed_array_set_f64", builder->getVoidTy(), {ptrTy, builder->getInt64Ty(), builder->getDoubleTy()});
    addRuntimeSymbol("ts_typed_array_create_u8", ptrTy, {builder->getInt64Ty()});
    addRuntimeSymbol("ts_typed_array_create_u32", ptrTy, {builder->getInt64Ty()});
    addRuntimeSymbol("ts_typed_array_create_f64", ptrTy, {builder->getInt64Ty()});
    addRuntimeSymbol("ts_typed_array_from_array_u8", ptrTy, {ptrTy});
    addRuntimeSymbol("ts_typed_array_from_array_u32", ptrTy, {ptrTy});
    addRuntimeSymbol("ts_typed_array_from_array_f64", ptrTy, {ptrTy});

    // Bitwise helpers
    addRuntimeSymbol("ts_double_to_int32", builder->getInt32Ty(), {builder->getDoubleTy()});
    addRuntimeSymbol("ts_double_to_uint32", builder->getInt32Ty(), {builder->getDoubleTy()});

    // DataView
    addRuntimeSymbol("ts_data_view_create", ptrTy, {ptrTy});
    addRuntimeSymbol("DataView_getUint32", builder->getInt64Ty(), {ptrTy, ptrTy, builder->getInt64Ty(), builder->getInt1Ty()});
    addRuntimeSymbol("DataView_setUint32", builder->getVoidTy(), {ptrTy, ptrTy, builder->getInt64Ty(), builder->getInt64Ty(), builder->getInt1Ty()});

    generateGlobals(analyzer);
    
    auto tsArrayType = llvm::StructType::create(*context, "TsArray");
    tsArrayType->setBody({
        llvm::Type::getInt32Ty(*context), // magic
        builder->getPtrTy(),              // elements
        llvm::Type::getInt64Ty(*context), // length
        llvm::Type::getInt64Ty(*context), // capacity
        llvm::Type::getInt64Ty(*context)  // elementSize
    });

    auto tsBufferType = llvm::StructType::create(*context, "TsBuffer");
    tsBufferType->setBody({
        builder->getPtrTy(),              // C++ vtable
        builder->getPtrTy(),              // TsObject::vtable
        llvm::Type::getInt32Ty(*context), // magic
        builder->getPtrTy(),              // data
        llvm::Type::getInt64Ty(*context)  // length
    });

    auto tsStringType = llvm::StructType::create(*context, "TsString");
    tsStringType->setBody({
        llvm::Type::getInt32Ty(*context), // magic
        llvm::Type::getInt32Ty(*context)  // length
    });

    auto tsTypedArrayType = llvm::StructType::create(*context, "TsTypedArray");
    tsTypedArrayType->setBody({
        builder->getPtrTy(),              // C++ vtable
        builder->getPtrTy(),              // TsObject::vtable
        builder->getPtrTy(),              // buffer
        llvm::Type::getInt64Ty(*context)  // length
    });

    asyncContextType = llvm::StructType::create(*context, "AsyncContext");
    asyncContextType->setBody({
        builder->getPtrTy(), // vtable
        llvm::Type::getInt32Ty(*context), // state
        llvm::Type::getInt8Ty(*context), // error (bool)
        llvm::Type::getInt8Ty(*context), // yielded (bool)
        llvm::StructType::get(*context, { llvm::Type::getInt8Ty(*context), llvm::Type::getInt64Ty(*context) }), // yieldedValue (TsValue)
        builder->getPtrTy(), // promise
        builder->getPtrTy(), // pendingNextPromise
        builder->getPtrTy(), // generator
        builder->getPtrTy(), // resumeFn
        builder->getPtrTy()  // data
    });

    generateClasses(analyzer, specializations);
    generatePrototypes(specializations);
    generateBodies(specializations);

    generateEntryPoint();

    if (debug) {
        diBuilder->finalize();
    }
}

void IRGenerator::emitLocation(ast::Node* node) {
    if (!debug || !node) {
        builder->SetCurrentDebugLocation(llvm::DebugLoc());
        return;
    }
    
    llvm::DIScope* scope = debugScopes.empty() ? compileUnit : debugScopes.back();
    builder->SetCurrentDebugLocation(llvm::DILocation::get(*context, node->line, node->column, scope));
}

llvm::DIType* IRGenerator::createDebugType(std::shared_ptr<Type> type) {
    if (!debug || !type) {
        return nullptr;
    }
    
    // Map TypeScript types to LLVM debug types
    switch (type->kind) {
        case TypeKind::Int:
            return diBuilder->createBasicType("number", 64, llvm::dwarf::DW_ATE_signed);
        case TypeKind::Double:
            return diBuilder->createBasicType("number", 64, llvm::dwarf::DW_ATE_float);
        case TypeKind::Boolean:
            return diBuilder->createBasicType("boolean", 1, llvm::dwarf::DW_ATE_boolean);
        case TypeKind::String:
        case TypeKind::Any:
        case TypeKind::Object:
        case TypeKind::Array:
        case TypeKind::Function:
            // All pointer types represented as generic pointer
            return diBuilder->createPointerType(nullptr, 64);
        case TypeKind::Void:
            return nullptr;
        default:
            // Unknown type - use generic pointer
            return diBuilder->createPointerType(nullptr, 64);
    }
}

void IRGenerator::generateGlobals(const Analyzer& analyzer) {
    const auto& globals = analyzer.getSymbolTable().getGlobalSymbols();
    
    static const std::set<std::string> runtimeGlobals = {
        "Object", "Array", "Math", "JSON", "console", "process", "Buffer",
        "global", "parseInt", "parseFloat"
    };

    // Some runtime globals are injected into per-module analyzer scopes (especially for
    // untyped JavaScript modules), which means they may not appear in the analyzer's
    // global symbol table. However, codegen expects these to exist as LLVM globals so
    // identifiers like `global` or `parseFloat` can be loaded reliably.
    for (const auto& name : runtimeGlobals) {
        if (!module->getGlobalVariable(name)) {
            llvm::Type* type = builder->getPtrTy();
            new llvm::GlobalVariable(*module, type, false, llvm::GlobalValue::ExternalLinkage,
                nullptr, name);
        }
    }

    for (auto& [name, symbol] : globals) {
        // These are injected by the analyzer for CommonJS support and must remain
        // function-local bindings in module init, not process-wide LLVM globals.
        if (name == "module" || name == "exports") continue;
        if (module->getGlobalVariable(name)) continue;
        
        // Most functions are handled elsewhere, but some are runtime-provided globals
        // that must exist as TsValue* values (e.g., parseFloat used as a value).
        if (symbol->type->kind == TypeKind::Function) {
            if (runtimeGlobals.find(name) == runtimeGlobals.end()) {
                continue;
            }

            // Runtime global functions are exposed as boxed TsValue*.
            llvm::Type* type = builder->getPtrTy();
            new llvm::GlobalVariable(*module, type, false, llvm::GlobalValue::ExternalLinkage,
                nullptr, name);
            continue;
        }
        
        // Skip runtime functions and other ts_ symbols
        if (name.find("ts_") == 0) continue;
        
        // Skip interfaces (they don't have runtime representation)
        if (symbol->type->kind == TypeKind::Interface) continue;

        llvm::Type* type = getLLVMType(symbol->type);
        
        // If it's a runtime global, create it as a declaration (no initializer)
        // Otherwise, create it as a definition with a null initializer
        llvm::Constant* initializer = nullptr;
        if (runtimeGlobals.find(name) == runtimeGlobals.end()) {
            initializer = llvm::Constant::getNullValue(type);
        }

        new llvm::GlobalVariable(*module, type, false, llvm::GlobalValue::ExternalLinkage,
            initializer, name);
    }

    // Also process top-level variables from modules
    // Note: these are variable declarations (const x = ...), not function declarations
    for (auto& symbol : analyzer.topLevelVariables) {
        if (module->getGlobalVariable(symbol->name)) continue;
        if (symbol->name.find("ts_") == 0) continue;
        if (symbol->type->kind == TypeKind::Interface) continue;
        
        // For function-typed variables (e.g., const myAdd = add;), create a pointer global
        // to hold the boxed function reference. Don't skip these.
        llvm::Type* type;
        if (symbol->type->kind == TypeKind::Function) {
            // Store as a pointer (TsValue* boxed function)
            type = builder->getPtrTy();
        } else {
            type = getLLVMType(symbol->type);
            SPDLOG_INFO("  Top-level variable: {} (type kind {})", symbol->name, (int)symbol->type->kind);
        }
        new llvm::GlobalVariable(*module, type, false, llvm::GlobalValue::ExternalLinkage,
            llvm::Constant::getNullValue(type), symbol->name);
    }
}

void IRGenerator::addStackProtection(llvm::Function* func) {
    if (!func) return;
    func->addFnAttr("sspstrong");
    func->addFnAttr("stack-protector-buffer-size", "8");
}

void IRGenerator::emitCFICheck(llvm::Value* ptr, const std::string& typeId) {
    return; // Disable CFI for now to fix backend crash
    /*
    llvm::Function* typeTest = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::type_test);
    
    llvm::Value* typeIdVal = llvm::MetadataAsValue::get(*context, llvm::MDString::get(*context, typeId));
    
    // Cast ptr to i8* (ptr in LLVM 18)
    llvm::Value* check = builder->CreateCall(typeTest, { ptr, typeIdVal });
    
    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* failBB = llvm::BasicBlock::Create(*context, "cfi_fail", currentFunc);
    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "cfi_cont", currentFunc);
    
    builder->CreateCondBr(check, contBB, failBB);
    
    builder->SetInsertPoint(failBB);
    llvm::FunctionType* failFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {}, false);
    llvm::FunctionCallee failFn = getRuntimeFunction("ts_cfi_fail", failFt);
    builder->CreateCall(failFt, failFn.getCallee(), {});
    builder->CreateUnreachable();
    
    builder->SetInsertPoint(contBB);
    */
}

void IRGenerator::emitBoundsCheck(llvm::Value* index, llvm::Value* length) {
    // if (index < 0 || index >= length) ts_panic("Index out of bounds")
    llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
    llvm::Value* isNegative = builder->CreateICmpSLT(index, zero);
    llvm::Value* isOver = builder->CreateICmpSGE(index, length);
    llvm::Value* isOutOfBounds = builder->CreateOr(isNegative, isOver);

    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* failBB = llvm::BasicBlock::Create(*context, "bounds_fail", currentFunc);
    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "bounds_cont", currentFunc);

    builder->CreateCondBr(isOutOfBounds, failBB, contBB);

    builder->SetInsertPoint(failBB);
    llvm::FunctionType* panicFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
    llvm::FunctionCallee panicFn = getRuntimeFunction("ts_panic", panicFt);
    
    llvm::Value* msg = builder->CreateGlobalStringPtr("Index out of bounds");
    builder->CreateCall(panicFt, panicFn.getCallee(), { msg });
    builder->CreateUnreachable();

    builder->SetInsertPoint(contBB);
}

void IRGenerator::emitNullCheck(llvm::Value* ptr) {
    if (!ptr->getType()->isPointerTy()) return;
    if (nonNullValues.count(ptr)) return;

    // Check if this is a load from a known non-null alloca
    if (auto load = llvm::dyn_cast<llvm::LoadInst>(ptr)) {
        if (checkedAllocas.count(load->getPointerOperand())) {
            nonNullValues.insert(ptr);
            return;
        }
    }

    llvm::Value* isNull = builder->CreateICmpEQ(ptr, llvm::ConstantPointerNull::get(builder->getPtrTy()));

    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* failBB = llvm::BasicBlock::Create(*context, "null_fail", currentFunc);
    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "null_cont", currentFunc);

    builder->CreateCondBr(isNull, failBB, contBB);

    builder->SetInsertPoint(failBB);
    llvm::FunctionType* panicFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
    llvm::FunctionCallee panicFn = getRuntimeFunction("ts_panic", panicFt);
    
    llvm::Value* msg = builder->CreateGlobalStringPtr("Null or undefined dereference");
    builder->CreateCall(panicFt, panicFn.getCallee(), { msg });
    builder->CreateUnreachable();

    builder->SetInsertPoint(contBB);
    nonNullValues.insert(ptr);

    // If this was a load from an alloca, mark the alloca as checked
    if (auto load = llvm::dyn_cast<llvm::LoadInst>(ptr)) {
        checkedAllocas.insert(load->getPointerOperand());
    }
}

void IRGenerator::emitNullCheckForExpression(ast::Expression* expr, llvm::Value* ptr) {
    if (auto id = dynamic_cast<ast::Identifier*>(expr)) {
        if (id->name == "this") return;
    }
    emitNullCheck(ptr);
}

void IRGenerator::generateEntryPoint() {
    // Look for the synthetic user_main first, fall back to user-defined user_main
    llvm::Function* userMain = module->getFunction("__synthetic_user_main");
    if (!userMain) {
        userMain = module->getFunction("user_main");
    }
    if (!userMain) return;

    // Declare ts_main: int ts_main(int argc, char** argv, TsValue* (*user_main)(void*))
    std::vector<llvm::Type*> args = {
        llvm::Type::getInt32Ty(*context),
        llvm::PointerType::getUnqual(*context),
        llvm::PointerType::getUnqual(*context)
    };
    llvm::FunctionType* ft = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context), args, false);
    llvm::Function* tsMain = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_main", module.get());
    addStackProtection(tsMain);

    // Define main: int main(int argc, char** argv)
    std::vector<llvm::Type*> mainArgs = {
        llvm::Type::getInt32Ty(*context),
        llvm::PointerType::getUnqual(*context)
    };
    llvm::FunctionType* mainFt = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context), mainArgs, false);
    llvm::Function* mainFn = llvm::Function::Create(mainFt, llvm::Function::ExternalLinkage, "main", module.get());
    addStackProtection(mainFn);

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", mainFn);
    builder->SetInsertPoint(bb);

    llvm::Value* argc = mainFn->getArg(0);
    llvm::Value* argv = mainFn->getArg(1);
    
    // Call static initializers for all classes
    if (analyzer) {
        SPDLOG_DEBUG("Calling static initializers for {} global types", analyzer->getSymbolTable().getGlobalTypes().size());
        for (auto& [name, type] : analyzer->getSymbolTable().getGlobalTypes()) {
            SPDLOG_DEBUG("Checking global type: {} (kind {})", name, (int)type->kind);
            if (type->kind == TypeKind::Class) {
                auto classType = std::static_pointer_cast<ClassType>(type);
                std::string initName = classType->name + "___static_init";
                if (llvm::Function* sinitFunc = module->getFunction(initName)) {
                    SPDLOG_DEBUG("Calling static initializer: {}", initName);
                    llvm::FunctionType* sinitFt = sinitFunc->getFunctionType();
                    createCall(sinitFt, sinitFunc, { llvm::ConstantPointerNull::get(builder->getPtrTy()) });
                }
            }
        }
    }

    createCall(ft, tsMain, { argc, argv, userMain });
    
    builder->CreateRet(llvm::ConstantInt::get(*context, llvm::APInt(32, 0)));
}

llvm::Type* IRGenerator::getLLVMType(const std::shared_ptr<Type>& type) {
    if (!type) return builder->getPtrTy();
    switch (type->kind) {
        case TypeKind::Void: return llvm::Type::getVoidTy(*context);
        case TypeKind::Int: return llvm::Type::getInt64Ty(*context);
        case TypeKind::Enum: return llvm::Type::getInt64Ty(*context);
        case TypeKind::Double: return llvm::Type::getDoubleTy(*context);
        case TypeKind::Boolean: return llvm::Type::getInt1Ty(*context);
        case TypeKind::Object: {
            auto objType = std::static_pointer_cast<ObjectType>(type);
            std::vector<llvm::Type*> fieldTypes;
            for (auto& [name, fieldType] : objType->fields) {
                fieldTypes.push_back(getLLVMType(fieldType));
            }
            llvm::StructType* structType = llvm::StructType::get(*context, fieldTypes);
            return llvm::PointerType::getUnqual(structType);
        }
        case TypeKind::Class: {
            auto classType = std::static_pointer_cast<ClassType>(type);
            llvm::StructType* structType = llvm::StructType::getTypeByName(*context, classType->name);
            if (!structType) {
                // Should have been created in generateClasses, but fallback just in case
                structType = llvm::StructType::create(*context, classType->name);
            }
            if (classType->isStruct) {
                return structType;
            }
            return llvm::PointerType::getUnqual(structType);
        }
        case TypeKind::String:
        case TypeKind::Any:
        case TypeKind::Array:
        case TypeKind::Tuple:
        case TypeKind::Map:
        case TypeKind::SetType:
        case TypeKind::BigInt:
        case TypeKind::Symbol:
        case TypeKind::Interface:
        case TypeKind::Union:
        case TypeKind::Intersection:
        case TypeKind::TypeParameter:
        case TypeKind::Function:
        case TypeKind::Unknown:
        case TypeKind::Undefined:
        case TypeKind::Null:
            return llvm::PointerType::getUnqual(*context);
        default: return llvm::Type::getInt8Ty(*context);
    }
}

void IRGenerator::generateDestructuring(llvm::Value* value, std::shared_ptr<Type> type, ast::Node* pattern) {
    SPDLOG_DEBUG("generateDestructuring: pattern={}", (pattern ? pattern->getKind() : "null"));
    if (auto id = dynamic_cast<ast::Identifier*>(pattern)) {
        bool isGlobal = false;
        if (auto gv = module->getGlobalVariable(id->name)) {
            builder->CreateStore(value, gv);
            
            if (boxedValues.count(value)) {
                boxedVariables.insert(id->name);
            }

            if (!lastLengthArray.empty()) {
                lengthAliases[gv] = lastLengthArray;
                lastLengthArray = "";
            } else {
                lengthAliases.erase(gv);
            }
            
            isGlobal = true;
        }

        if (currentAsyncFrame) {
            auto it = currentAsyncFrameMap.find(id->name);
            if (it != currentAsyncFrameMap.end()) {
                llvm::Value* ptr = namedValues[id->name];
                if (!ptr) {
                    ptr = builder->CreateStructGEP(currentAsyncFrameType, currentAsyncFrame, it->second);
                    namedValues[id->name] = ptr;
                }
                
                // Ensure value matches the frame field type
                llvm::Type* fieldType = currentAsyncFrameType->getElementType(it->second);
                if (value->getType() != fieldType) {
                    if (fieldType->isPointerTy() && !value->getType()->isPointerTy()) {
                        value = boxValue(value, type);
                    } else if (!fieldType->isPointerTy() && value->getType()->isPointerTy()) {
                        value = unboxValue(value, type);
                    } else {
                        value = castValue(value, fieldType);
                    }
                }

                builder->CreateStore(value, ptr);
                namedValues[id->name] = ptr;

                if (boxedValues.count(value)) {
                    boxedVariables.insert(id->name);
                }

                return;
            }
        }

        if (isGlobal) return;

        // Check if this variable needs a cell (captured and potentially mutated)
        if (cellVariables.count(id->name)) {
            // Box the value first - BUT skip if type is Any (already boxed TsValue*)
            llvm::Value* boxedVal;
            if (type && type->kind == TypeKind::Any) {
                // Any-typed values are always already boxed as TsValue*
                boxedVal = value;
            } else {
                boxedVal = boxValue(value, type);
            }
            
            if (!namedValues.count(id->name)) {
                // First time: create cell and store pointer in alloca
                llvm::FunctionType* cellCreateFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee cellCreateFn = getRuntimeFunction("ts_cell_create", cellCreateFt);
                llvm::Value* cell = createCall(cellCreateFt, cellCreateFn.getCallee(), { boxedVal });
                
                llvm::AllocaInst* cellAlloca = createEntryBlockAlloca(builder->GetInsertBlock()->getParent(), id->name + "_cell", builder->getPtrTy());
                builder->CreateStore(cell, cellAlloca);
                namedValues[id->name] = cellAlloca;
                cellPointers[id->name] = cell;  // Track the cell itself
                SPDLOG_INFO("Created cell for captured variable: {}", id->name);
            } else {
                // Subsequent assignment: update cell value via ts_cell_set
                llvm::Value* cellAlloca = namedValues[id->name];
                llvm::Value* cell = builder->CreateLoad(builder->getPtrTy(), cellAlloca);
                
                llvm::FunctionType* cellSetFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee cellSetFn = getRuntimeFunction("ts_cell_set", cellSetFt);
                createCall(cellSetFt, cellSetFn.getCallee(), { cell, boxedVal });
            }
            
            // Store the TS type for this variable
            if (type) {
                variableTypes[id->name] = type;
            }
            
            // Cell variables are always boxed
            boxedVariables.insert(id->name);
            return;
        }

        llvm::Type* varType = value->getType();
        if (forcedVariableTypes.count(id->name)) {
            varType = forcedVariableTypes[id->name];
            if (value->getType() != varType) {
                if (varType->isIntegerTy() && value->getType()->isDoubleTy()) {
                    value = builder->CreateFPToSI(value, varType, "promotetmp");
                } else if (varType->isDoubleTy() && value->getType()->isIntegerTy()) {
                    value = builder->CreateSIToFP(value, varType, "promotetmp");
                } else if (varType->isPointerTy() && !value->getType()->isPointerTy()) {
                    value = boxValue(value, type);
                } else if (!varType->isPointerTy() && value->getType()->isPointerTy()) {
                    value = unboxValue(value, type);
                }
            }
        }

        if (value->getType() != varType) {
            value = castValue(value, varType);
        }

        llvm::Value* varPtr = nullptr;
        if (namedValues.count(id->name)) {
            varPtr = namedValues[id->name];
        } else {
            varPtr = createEntryBlockAlloca(builder->GetInsertBlock()->getParent(), id->name, varType);
            namedValues[id->name] = varPtr;
            
            // Add debug info for local variable
            if (debug && !debugScopes.empty()) {
                llvm::DILocalVariable* debugVar = diBuilder->createAutoVariable(
                    debugScopes.back(),
                    id->name,
                    compileUnit->getFile(),
                    id->line,
                    createDebugType(type),
                    true // alwaysPreserve
                );
                diBuilder->insertDeclare(
                    varPtr,
                    debugVar,
                    diBuilder->createExpression(),
                    llvm::DILocation::get(*context, id->line, id->column, debugScopes.back()),
                    builder->GetInsertBlock()
                );
            }
        }
        builder->CreateStore(value, varPtr);

        // Store the TS type for this variable (used for closure capture)
        if (type) {
            variableTypes[id->name] = type;
        }

        if (concreteTypes.count(value)) {
            concreteTypes[varPtr] = concreteTypes[value];
        } else if (type && type->kind == TypeKind::Class) {
            concreteTypes[varPtr] = std::static_pointer_cast<ClassType>(type).get();
        }

        SPDLOG_DEBUG("generateDestructuring: checking {} in boxedValues (size={})", (void*)value, boxedValues.size());
        if (boxedValues.count(value)) {
            SPDLOG_DEBUG("Variable {} marked as BOXED", id->name);
            boxedVariables.insert(id->name);
        } else {
            boxedVariables.erase(id->name);
        }

        if (!lastLengthArray.empty()) {
            lengthAliases[varPtr] = lastLengthArray;
            lastLengthArray = "";
        } else {
            lengthAliases.erase(varPtr);
        }

        if (nonNullValues.count(value)) {
            checkedAllocas.insert(varPtr);
        } else {
            checkedAllocas.erase(varPtr);
        }
    } else if (auto obp = dynamic_cast<ast::ObjectBindingPattern*>(pattern)) {
        if (!type || type->kind != TypeKind::Class) {
            // Collect excluded keys for rest pattern
            std::vector<std::string> excludedKeys;
            for (auto& elementNode : obp->elements) {
                auto element = dynamic_cast<ast::BindingElement*>(elementNode.get());
                if (!element || element->isSpread) continue;
                
                std::string fieldName;
                if (!element->propertyName.empty()) {
                    fieldName = element->propertyName;
                } else if (auto nameId = dynamic_cast<ast::Identifier*>(element->name.get())) {
                    fieldName = nameId->name;
                }
                if (!fieldName.empty()) excludedKeys.push_back(fieldName);
            }

            for (auto& elementNode : obp->elements) {
                auto element = dynamic_cast<ast::BindingElement*>(elementNode.get());
                if (!element) continue;

                if (element->isSpread) {
                    // Handle rest pattern
                    llvm::FunctionType* createArrayFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                    llvm::FunctionCallee createArrayFn = getRuntimeFunction("ts_array_create", createArrayFt);
                    llvm::Value* excludedArray = createCall(createArrayFt, createArrayFn.getCallee(), {});

                    llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);

                    llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);

                    for (const auto& key : excludedKeys) {
                        llvm::Value* keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(key) });
                        llvm::Value* boxedKey = boxValue(keyStr, std::make_shared<Type>(TypeKind::String));
                        createCall(pushFt, pushFn.getCallee(), { excludedArray, boxedKey });
                    }

                    llvm::FunctionType* copyExcludingFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee copyExcludingFn = getRuntimeFunction("ts_map_copy_excluding_v2", copyExcludingFt);
                    
                    // Ensure value is unboxed if it's boxed (Any or known boxed value)
                    llvm::Value* objPtr = value;
                    if (boxedValues.count(value) || (type && type->kind == TypeKind::Any)) {
                         objPtr = unboxValue(value, std::make_shared<Type>(TypeKind::Object));
                    }
                    
                    llvm::Value* restObj = createCall(copyExcludingFt, copyExcludingFn.getCallee(), { objPtr, excludedArray });
                    
                    // restObj is TsMap*. We need to box it to assign to Any/Object variable.
                    llvm::Value* boxedRest = boxValue(restObj, std::make_shared<Type>(TypeKind::Object));
                    
                    generateDestructuring(boxedRest, std::make_shared<Type>(TypeKind::Any), element->name.get());
                    continue;
                }

                std::string fieldName;
                if (!element->propertyName.empty()) {
                    fieldName = element->propertyName;
                } else if (auto nameId = dynamic_cast<ast::Identifier*>(element->name.get())) {
                    fieldName = nameId->name;
                }

                if (fieldName.empty()) continue;

                std::shared_ptr<Type> fieldType = std::make_shared<Type>(TypeKind::Any);
                if (type && type->kind == TypeKind::Object) {
                    auto objType = std::static_pointer_cast<ObjectType>(type);
                    if (objType->fields.count(fieldName)) fieldType = objType->fields[fieldName];
                }

                llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
                llvm::Value* propName = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(fieldName) });
                // propName = boxValue(propName, std::make_shared<Type>(TypeKind::String));
                
                llvm::FunctionType* getPropFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee getPropFn = getRuntimeFunction("ts_value_get_property", getPropFt);
                
                // Ensure we pass the object pointer, not the TsValue* if possible, or let runtime handle it.
                // But ts_value_get_property expects void* obj.
                // If value is TsValue*, we should probably unbox it.
                llvm::Value* objPtr = value;
                if (type && type->kind != TypeKind::Any) {
                    objPtr = boxValue(value, type);
                }

                llvm::Value* propVal = createCall(getPropFt, getPropFn.getCallee(), { objPtr, propName });
                
                llvm::Value* finalVal = nullptr;

                if (element->initializer) {
                    llvm::Value* typeTag = builder->CreateLoad(llvm::Type::getInt8Ty(*context), builder->CreateBitCast(propVal, builder->getPtrTy()));
                    llvm::Value* isUndefined = builder->CreateICmpEQ(typeTag, llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0));
                    
                    llvm::Function* function = builder->GetInsertBlock()->getParent();
                    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "destruct.default", function);
                    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "destruct.value", function);
                    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "destruct.merge", function);
                    
                    builder->CreateCondBr(isUndefined, thenBB, elseBB);
                    
                    builder->SetInsertPoint(thenBB);
                    visit(element->initializer.get());
                    llvm::Value* defaultVal = lastValue;
                    defaultVal = castValue(defaultVal, getLLVMType(fieldType));
                    builder->CreateBr(mergeBB);
                    thenBB = builder->GetInsertBlock();
                    
                    builder->SetInsertPoint(elseBB);
                    llvm::Value* unboxedProp = propVal;
                    if (fieldType && fieldType->kind != TypeKind::Any) {
                        unboxedProp = unboxValue(propVal, fieldType);
                    }
                    builder->CreateBr(mergeBB);
                    elseBB = builder->GetInsertBlock();
                    
                    builder->SetInsertPoint(mergeBB);
                    llvm::PHINode* phi = builder->CreatePHI(getLLVMType(fieldType), 2);
                    phi->addIncoming(defaultVal, thenBB);
                    phi->addIncoming(unboxedProp, elseBB);
                    finalVal = phi;
                } else {
                    finalVal = propVal;
                    if (fieldType && fieldType->kind != TypeKind::Any) {
                        finalVal = unboxValue(propVal, fieldType);
                    }
                }

                generateDestructuring(finalVal, fieldType, element->name.get());
            }
            return;
        }

        auto classType = std::static_pointer_cast<ClassType>(type);
        std::string className = classType->name;
        llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
        if (!classStruct) return;

        llvm::Value* typedObjPtr = builder->CreateBitCast(value, llvm::PointerType::getUnqual(classStruct));

        for (auto& elementNode : obp->elements) {
            auto element = dynamic_cast<ast::BindingElement*>(elementNode.get());
            if (!element) continue;

            std::string fieldName;
            if (!element->propertyName.empty()) {
                fieldName = element->propertyName;
            } else if (auto nameId = dynamic_cast<ast::Identifier*>(element->name.get())) {
                fieldName = nameId->name;
            }

            if (fieldName.empty()) continue;

            if (classLayouts.count(className) && classLayouts[className].fieldIndices.count(fieldName)) {
                int fieldIndex = classLayouts[className].fieldIndices[fieldName];
                llvm::Value* fieldPtr = builder->CreateStructGEP(classStruct, typedObjPtr, fieldIndex);
                
                std::shared_ptr<Type> fieldType;
                for (const auto& f : classLayouts[className].allFields) {
                    if (f.first == fieldName) {
                        fieldType = f.second;
                        break;
                    }
                }
                
                if (fieldType) {
                    llvm::Value* fieldValue = builder->CreateLoad(getLLVMType(fieldType), fieldPtr);
                    
                    if (element->initializer) {
                        if (fieldValue->getType()->isPointerTy()) {
                            llvm::Value* isNull = builder->CreateICmpEQ(fieldValue, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(fieldValue->getType())));
                            
                            llvm::Function* function = builder->GetInsertBlock()->getParent();
                            llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "destruct.default", function);
                            llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "destruct.value", function);
                            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "destruct.merge", function);
                            
                            builder->CreateCondBr(isNull, thenBB, elseBB);
                            
                            builder->SetInsertPoint(thenBB);
                            visit(element->initializer.get());
                            llvm::Value* defaultVal = lastValue;
                            defaultVal = castValue(defaultVal, getLLVMType(fieldType));
                            builder->CreateBr(mergeBB);
                            thenBB = builder->GetInsertBlock();
                            
                            builder->SetInsertPoint(elseBB);
                            builder->CreateBr(mergeBB);
                            elseBB = builder->GetInsertBlock();
                            
                            builder->SetInsertPoint(mergeBB);
                            llvm::PHINode* phi = builder->CreatePHI(fieldValue->getType(), 2);
                            phi->addIncoming(defaultVal, thenBB);
                            phi->addIncoming(fieldValue, elseBB);
                            fieldValue = phi;
                        }
                    }

                    generateDestructuring(fieldValue, fieldType, element->name.get());
                }
            }
        }
    } else if (auto abp = dynamic_cast<ast::ArrayBindingPattern*>(pattern)) {
        std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
        if (type && type->kind == TypeKind::Array) {
            elementType = std::static_pointer_cast<ArrayType>(type)->elementType;
        }

        llvm::Value* arrayPtr = value;
        if (type && type->kind == TypeKind::Any) {
             llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
             llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
             arrayPtr = createCall(getObjFt, getObjFn.getCallee(), { value });
        }

        llvm::Function* getFn = module->getFunction("ts_array_get_as_value");
        if (!getFn) {
             std::vector<llvm::Type*> args = { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) };
             llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), args, false);
             getFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_array_get_as_value", module.get());
        }

        for (size_t i = 0; i < abp->elements.size(); ++i) {
            auto elementNode = abp->elements[i].get();
            if (auto oe = dynamic_cast<ast::OmittedExpression*>(elementNode)) continue;
            
            auto element = dynamic_cast<ast::BindingElement*>(elementNode);
            if (!element) continue;

            llvm::Value* idxVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), i);

            if (element->isSpread) {
                llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee lenFn = getRuntimeFunction("ts_array_length", lenFt);
                llvm::Value* lenVal = createCall(lenFt, lenFn.getCallee(), { arrayPtr });

                llvm::FunctionType* sliceFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                    { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee sliceFn = getRuntimeFunction("ts_array_slice", sliceFt);
                llvm::Value* restArr = createCall(sliceFt, sliceFn.getCallee(), { arrayPtr, idxVal, lenVal });
                generateDestructuring(restArr, type, element->name.get());
                break; 
            }

            llvm::Value* elementVal = createCall(getFn->getFunctionType(), getFn, { arrayPtr, idxVal });
            
            llvm::Value* finalVal = nullptr;
            if (element->initializer) {
                llvm::Value* typeTag = builder->CreateLoad(llvm::Type::getInt8Ty(*context), builder->CreateBitCast(elementVal, builder->getPtrTy()));
                llvm::Value* isUndefined = builder->CreateICmpEQ(typeTag, llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0));
                
                llvm::Function* function = builder->GetInsertBlock()->getParent();
                llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "destruct.arr.default", function);
                llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "destruct.arr.value", function);
                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "destruct.arr.merge", function);
                
                builder->CreateCondBr(isUndefined, thenBB, elseBB);
                
                builder->SetInsertPoint(thenBB);
                visit(element->initializer.get());
                llvm::Value* defaultVal = lastValue;
                defaultVal = castValue(defaultVal, getLLVMType(elementType));
                builder->CreateBr(mergeBB);
                thenBB = builder->GetInsertBlock();
                
                builder->SetInsertPoint(elseBB);
                llvm::Value* unboxedProp = unboxValue(elementVal, elementType);
                builder->CreateBr(mergeBB);
                elseBB = builder->GetInsertBlock();
                
                builder->SetInsertPoint(mergeBB);
                llvm::PHINode* phi = builder->CreatePHI(getLLVMType(elementType), 2);
                phi->addIncoming(defaultVal, thenBB);
                phi->addIncoming(unboxedProp, elseBB);
                finalVal = phi;
            } else {
                finalVal = unboxValue(elementVal, elementType);
            }

            generateDestructuring(finalVal, elementType, element->name.get());
        }
    }
}

llvm::AllocaInst* IRGenerator::createEntryBlockAlloca(llvm::Function* function, const std::string& varName, llvm::Type* type) {
    SPDLOG_INFO("createEntryBlockAlloca: creating alloca for {} in function {}", varName, function->getName().str());
    llvm::IRBuilder<> tmpBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
    return tmpBuilder.CreateAlloca(type, nullptr, varName);
}

llvm::Function* IRGenerator::getRuntimeFunction(const std::string& name) {
    if (auto* f = module->getFunction(name)) return f;

    if (name == "ts_push_exception_handler") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context), {}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "ts_pop_exception_handler") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context), {}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "ts_throw") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context), {llvm::PointerType::getUnqual(*context)}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "ts_get_exception") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context), {}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "_setjmp") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*context), 
            {llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context)}, 
            false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    }

    return nullptr;
}

llvm::Value* IRGenerator::getUndefinedValue() {
    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    llvm::FunctionCallee fn = getRuntimeFunction("ts_value_make_undefined", ft);
    return createCall(ft, fn.getCallee(), {});
}

llvm::Value* IRGenerator::getNullValue() {
    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    llvm::FunctionCallee fn = getRuntimeFunction("ts_value_make_null", ft);
    return createCall(ft, fn.getCallee(), {});
}

void IRGenerator::visit(ast::Node* node) {
    if (node) {
        if (valueOverrides.count(node)) {
            lastValue = valueOverrides[node];
            return;
        }
        node->accept(this);
    }
}

void IRGenerator::collectVariables(ast::Node* node, std::vector<VariableInfo>& vars) {
    if (!node) return;

    if (auto decl = dynamic_cast<ast::VariableDeclaration*>(node)) {
        if (auto id = dynamic_cast<ast::Identifier*>(decl->name.get())) {
            vars.push_back({id->name, decl->resolvedType, getLLVMType(decl->resolvedType)});
        } else if (auto obp = dynamic_cast<ast::ObjectBindingPattern*>(decl->name.get())) {
            for (auto& elem : obp->elements) {
                if (auto bindingElem = dynamic_cast<ast::BindingElement*>(elem.get())) {
                    if (auto id = dynamic_cast<ast::Identifier*>(bindingElem->name.get())) {
                        vars.push_back({id->name, std::make_shared<Type>(TypeKind::Any), builder->getPtrTy()});
                    }
                }
            }
        } else if (auto abp = dynamic_cast<ast::ArrayBindingPattern*>(decl->name.get())) {
            for (auto& elem : abp->elements) {
                if (!elem) continue;
                if (auto bindingElem = dynamic_cast<ast::BindingElement*>(elem.get())) {
                    if (auto id = dynamic_cast<ast::Identifier*>(bindingElem->name.get())) {
                        vars.push_back({id->name, std::make_shared<Type>(TypeKind::Any), builder->getPtrTy()});
                    }
                }
            }
        }
    } else if (auto block = dynamic_cast<ast::BlockStatement*>(node)) {
        for (auto& stmt : block->statements) collectVariables(stmt.get(), vars);
    } else if (auto ifStmt = dynamic_cast<ast::IfStatement*>(node)) {
        collectVariables(ifStmt->thenStatement.get(), vars);
        collectVariables(ifStmt->elseStatement.get(), vars);
    } else if (auto tryStmt = dynamic_cast<ast::TryStatement*>(node)) {
        // Add implicit variable for pending exception
        std::string baseName = "try_" + std::to_string((uintptr_t)node);
        std::string pendingExcName = baseName + "_pendingExc";
        vars.push_back({ pendingExcName, std::make_shared<Type>(TypeKind::Any), builder->getPtrTy() });

        for (auto& stmt : tryStmt->tryBlock) collectVariables(stmt.get(), vars);
        if (tryStmt->catchClause) {
            collectVariables(tryStmt->catchClause.get(), vars);
        }
        for (auto& stmt : tryStmt->finallyBlock) {
            collectVariables(stmt.get(), vars);
        }
    } else if (auto catchClause = dynamic_cast<ast::CatchClause*>(node)) {
        if (catchClause->variable) {
            if (auto id = dynamic_cast<ast::Identifier*>(catchClause->variable.get())) {
                vars.push_back({ id->name, std::make_shared<Type>(TypeKind::Any), builder->getPtrTy() });
            } else {
                collectVariables(catchClause->variable.get(), vars);
            }
        }
        for (auto& stmt : catchClause->block) collectVariables(stmt.get(), vars);
    } else if (auto switchStmt = dynamic_cast<ast::SwitchStatement*>(node)) {
        for (auto& clause : switchStmt->clauses) {
            if (auto cc = dynamic_cast<ast::CaseClause*>(clause.get())) {
                for (auto& stmt : cc->statements) collectVariables(stmt.get(), vars);
            } else if (auto dc = dynamic_cast<ast::DefaultClause*>(clause.get())) {
                for (auto& stmt : dc->statements) collectVariables(stmt.get(), vars);
            }
        }
    } else if (auto whileStmt = dynamic_cast<ast::WhileStatement*>(node)) {
        collectVariables(whileStmt->body.get(), vars);
    } else if (auto forStmt = dynamic_cast<ast::ForStatement*>(node)) {
        collectVariables(forStmt->initializer.get(), vars);
        collectVariables(forStmt->body.get(), vars);
    } else if (auto forOf = dynamic_cast<ast::ForOfStatement*>(node)) {
        // Add implicit variables for the loop
        std::string baseName = "forof_" + std::to_string((uintptr_t)node);
        
        // 1. Index
        vars.push_back({ baseName + "_index", std::make_shared<Type>(TypeKind::Int), llvm::Type::getInt64Ty(*context) });
        
        // 2. Iterable
        vars.push_back({ baseName + "_iterable", forOf->expression->inferredType, getLLVMType(forOf->expression->inferredType) });
        
        // 3. Length
        vars.push_back({ baseName + "_length", std::make_shared<Type>(TypeKind::Int), llvm::Type::getInt64Ty(*context) });

        // 4. Iterator (for async for-of)
        if (forOf->isAwait) {
            vars.push_back({ baseName + "_iterator", std::make_shared<Type>(TypeKind::Any), builder->getPtrTy() });
        }

        collectVariables(forOf->initializer.get(), vars);
        collectVariables(forOf->body.get(), vars);
    } else if (auto forIn = dynamic_cast<ast::ForInStatement*>(node)) {
        // Add implicit variables for the loop
        std::string baseName = "forin_" + std::to_string((uintptr_t)node);
        
        // 1. Index
        vars.push_back({ baseName + "_index", std::make_shared<Type>(TypeKind::Int), llvm::Type::getInt64Ty(*context) });
        
        // 2. Keys (Array of strings)
        vars.push_back({ baseName + "_keys", std::make_shared<Type>(TypeKind::Array), builder->getPtrTy() });
        
        // 3. Length
        vars.push_back({ baseName + "_length", std::make_shared<Type>(TypeKind::Int), llvm::Type::getInt64Ty(*context) });

        collectVariables(forIn->initializer.get(), vars);
        collectVariables(forIn->body.get(), vars);
    }
}

// Helper class to collect free variables (identifiers referenced but not defined locally)
class FreeVariableCollector : public ast::Visitor {
public:
    std::set<std::string> localScope;       // Variables defined in the current scope
    std::set<std::string> referencedNames;  // All identifiers referenced
    
    FreeVariableCollector(const std::set<std::string>& outerLocalScope) 
        : localScope(outerLocalScope) {}
    
    // Required pure virtual methods - empty implementations for unused ones
    void visitProgram(ast::Program* node) override {}
    void visitFunctionDeclaration(ast::FunctionDeclaration* node) override {}
    void visitBreakStatement(ast::BreakStatement* node) override {}
    void visitContinueStatement(ast::ContinueStatement* node) override {}
    void visitSwitchStatement(ast::SwitchStatement* node) override {}
    void visitTryStatement(ast::TryStatement* node) override {}
    void visitThrowStatement(ast::ThrowStatement* node) override {}
    void visitImportDeclaration(ast::ImportDeclaration* node) override {}
    void visitExportDeclaration(ast::ExportDeclaration* node) override {}
    void visitExportAssignment(ast::ExportAssignment* node) override {}
    void visitComputedPropertyName(ast::ComputedPropertyName* node) override {}
    void visitMethodDefinition(ast::MethodDefinition* node) override {}
    void visitStaticBlock(ast::StaticBlock* node) override {}
    void visitSuperExpression(ast::SuperExpression* node) override {}
    void visitStringLiteral(ast::StringLiteral* node) override {}
    void visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) override {}
    void visitNumericLiteral(ast::NumericLiteral* node) override {}
    void visitBigIntLiteral(ast::BigIntLiteral* node) override {}
    void visitBooleanLiteral(ast::BooleanLiteral* node) override {}
    void visitNullLiteral(ast::NullLiteral* node) override {}
    void visitUndefinedLiteral(ast::UndefinedLiteral* node) override {}
    void visitAwaitExpression(ast::AwaitExpression* node) override { if (node->expression) node->expression->accept(this); }
    void visitYieldExpression(ast::YieldExpression* node) override { if (node->expression) node->expression->accept(this); }
    void visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node) override {}
    void visitAsExpression(ast::AsExpression* node) override { if (node->expression) node->expression->accept(this); }
    void visitClassDeclaration(ast::ClassDeclaration* node) override {}
    void visitInterfaceDeclaration(ast::InterfaceDeclaration* node) override {}
    void visitObjectBindingPattern(ast::ObjectBindingPattern* node) override {}
    void visitArrayBindingPattern(ast::ArrayBindingPattern* node) override {}
    void visitBindingElement(ast::BindingElement* node) override {}
    void visitOmittedExpression(ast::OmittedExpression* node) override {}
    void visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) override {}
    void visitEnumDeclaration(ast::EnumDeclaration* node) override {}
    
    void visitIdentifier(ast::Identifier* node) override {
        // Record this identifier as referenced
        if (!localScope.count(node->name)) {
            referencedNames.insert(node->name);
        }
    }
    
    void visitVariableDeclaration(ast::VariableDeclaration* node) override {
        // First, process the initializer (before adding to scope)
        if (node->initializer) {
            node->initializer->accept(this);
        }
        // Then add the variable to local scope
        if (auto id = dynamic_cast<ast::Identifier*>(node->name.get())) {
            localScope.insert(id->name);
        } else if (auto obp = dynamic_cast<ast::ObjectBindingPattern*>(node->name.get())) {
            for (auto& elem : obp->elements) {
                if (auto bindingElem = dynamic_cast<ast::BindingElement*>(elem.get())) {
                    if (auto id = dynamic_cast<ast::Identifier*>(bindingElem->name.get())) {
                        localScope.insert(id->name);
                    }
                }
            }
        } else if (auto abp = dynamic_cast<ast::ArrayBindingPattern*>(node->name.get())) {
            for (auto& elem : abp->elements) {
                if (!elem) continue;
                if (auto bindingElem = dynamic_cast<ast::BindingElement*>(elem.get())) {
                    if (auto id = dynamic_cast<ast::Identifier*>(bindingElem->name.get())) {
                        localScope.insert(id->name);
                    }
                }
            }
        }
    }
    
    void visitBlockStatement(ast::BlockStatement* node) override {
        for (auto& stmt : node->statements) {
            stmt->accept(this);
        }
    }
    
    void visitExpressionStatement(ast::ExpressionStatement* node) override {
        node->expression->accept(this);
    }
    
    void visitReturnStatement(ast::ReturnStatement* node) override {
        if (node->expression) node->expression->accept(this);
    }
    
    void visitBinaryExpression(ast::BinaryExpression* node) override {
        node->left->accept(this);
        node->right->accept(this);
    }
    
    void visitConditionalExpression(ast::ConditionalExpression* node) override {
        node->condition->accept(this);
        node->whenTrue->accept(this);
        node->whenFalse->accept(this);
    }
    
    void visitAssignmentExpression(ast::AssignmentExpression* node) override {
        node->left->accept(this);
        node->right->accept(this);
    }
    
    void visitCallExpression(ast::CallExpression* node) override {
        node->callee->accept(this);
        for (auto& arg : node->arguments) {
            arg->accept(this);
        }
    }

    void visitParenthesizedExpression(ast::ParenthesizedExpression* node) override {
        if (node->expression) node->expression->accept(this);
    }
    
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node) override {
        node->expression->accept(this);
        // Don't visit node->name - it's a property, not a variable
    }
    
    void visitElementAccessExpression(ast::ElementAccessExpression* node) override {
        node->expression->accept(this);
        node->argumentExpression->accept(this);
    }
    
    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) override {
        for (auto& elem : node->elements) {
            elem->accept(this);
        }
    }
    
    void visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) override {
        for (auto& prop : node->properties) {
            prop->accept(this);
        }
    }
    
    void visitPropertyAssignment(ast::PropertyAssignment* node) override {
        node->initializer->accept(this);
    }
    
    void visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) override {
        // The name IS a variable reference
        if (!localScope.count(node->name)) {
            referencedNames.insert(node->name);
        }
    }
    
    void visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) override {
        node->operand->accept(this);
    }
    
    void visitDeleteExpression(ast::DeleteExpression* node) override {
        node->expression->accept(this);
    }
    
    void visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) override {
        node->operand->accept(this);
    }
    
    void visitIfStatement(ast::IfStatement* node) override {
        node->condition->accept(this);
        node->thenStatement->accept(this);
        if (node->elseStatement) node->elseStatement->accept(this);
    }
    
    void visitWhileStatement(ast::WhileStatement* node) override {
        node->condition->accept(this);
        node->body->accept(this);
    }
    
    void visitForStatement(ast::ForStatement* node) override {
        if (node->initializer) node->initializer->accept(this);
        if (node->condition) node->condition->accept(this);
        if (node->incrementor) node->incrementor->accept(this);
        node->body->accept(this);
    }
    
    void visitForOfStatement(ast::ForOfStatement* node) override {
        node->expression->accept(this);
        if (node->initializer) node->initializer->accept(this);
        node->body->accept(this);
    }
    
    void visitForInStatement(ast::ForInStatement* node) override {
        node->expression->accept(this);
        if (node->initializer) node->initializer->accept(this);
        node->body->accept(this);
    }
    
    void visitSpreadElement(ast::SpreadElement* node) override {
        node->expression->accept(this);
    }
    
    void visitTemplateExpression(ast::TemplateExpression* node) override {
        for (auto& span : node->spans) {
            span.expression->accept(this);
        }
    }
    
    void visitNewExpression(ast::NewExpression* node) override {
        node->expression->accept(this);
        for (auto& arg : node->arguments) {
            arg->accept(this);
        }
    }
    
    // Nested arrow functions create a new scope - don't descend
    void visitArrowFunction(ast::ArrowFunction* node) override {
        // We should still collect free variables from nested arrow functions
        // The nested function's local scope starts with ONLY its parameters,
        // NOT the enclosing scope's variables (those are what we want to capture!)
        std::set<std::string> nestedScope;
        for (auto& param : node->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                nestedScope.insert(id->name);
            }
        }
        FreeVariableCollector nestedCollector(nestedScope);
        if (node->body) {
            node->body->accept(&nestedCollector);
        }
        // Merge referenced names - any variable not local to the nested function
        // becomes a reference for the outer function (we want to capture it)
        for (const auto& name : nestedCollector.referencedNames) {
            // If it's not local to THIS scope either, it's a free variable
            if (!localScope.count(name)) {
                referencedNames.insert(name);
            }
        }
    }
    
    void visitFunctionExpression(ast::FunctionExpression* node) override {
        // Similar to arrow function - nested function starts with only its parameters
        std::set<std::string> nestedScope;
        for (auto& param : node->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                nestedScope.insert(id->name);
            }
        }
        FreeVariableCollector nestedCollector(nestedScope);
        for (auto& stmt : node->body) {
            stmt->accept(&nestedCollector);
        }
        for (const auto& name : nestedCollector.referencedNames) {
            if (!localScope.count(name)) {
                referencedNames.insert(name);
            }
        }
    }
};

void IRGenerator::collectFreeVariables(ast::Node* node, 
                                       const std::set<std::string>& localScope,
                                       std::vector<CapturedVariable>& captured) {
    FreeVariableCollector collector(localScope);
    node->accept(&collector);
    
    // Filter: only include variables that exist in namedValues (outer scope)
    for (const auto& name : collector.referencedNames) {
        auto it = namedValues.find(name);
        if (it != namedValues.end()) {
            // Found in outer scope - this is a captured variable
            CapturedVariable cv;
            cv.name = name;
            cv.value = it->second;
            // Look up the TS type from variableTypes (populated by generateDestructuring)
            auto typeIt = variableTypes.find(name);
            if (typeIt != variableTypes.end()) {
                cv.type = typeIt->second;
            } else {
                // Fallback: try typeEnvironment (for type parameters)
                auto envIt = typeEnvironment.find(name);
                if (envIt != typeEnvironment.end()) {
                    cv.type = envIt->second;
                } else {
                    cv.type = std::make_shared<Type>(TypeKind::Any);
                }
            }
            captured.push_back(cv);
        }
    }
}

// Helper visitor to collect all variables captured by inner arrow functions
// Uses a recursive AST walk to find arrow functions, then uses FreeVariableCollector
// to determine which outer variables they capture
class CapturedNameCollector : public ast::Visitor {
    std::set<std::string> outerScope;  // Variables defined in outer function
public:
    std::set<std::string> capturedNames;  // Variables from outer scope used in inner closures
    
    CapturedNameCollector(const std::set<std::string>& outer) : outerScope(outer) {}
    
    void visitArrowFunction(ast::ArrowFunction* node) override {
        // This is an inner closure - collect its free variables
        std::set<std::string> arrowScope;
        for (auto& param : node->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                arrowScope.insert(id->name);
            }
        }
        
        // Use FreeVariableCollector to find what the closure references
        FreeVariableCollector fvc(arrowScope);
        if (node->body) {
            node->body->accept(&fvc);
        }
        
        // Any reference to a variable in outerScope is a captured variable
        for (const auto& name : fvc.referencedNames) {
            if (outerScope.count(name)) {
                capturedNames.insert(name);
            }
        }
        
        // Also recurse into nested closures (they might capture outer vars too)
        CapturedNameCollector nestedCollector(outerScope);
        if (node->body) {
            node->body->accept(&nestedCollector);
        }
        for (const auto& name : nestedCollector.capturedNames) {
            capturedNames.insert(name);
        }
    }
    
    void visitFunctionExpression(ast::FunctionExpression* node) override {
        // Similar to arrow function
        std::set<std::string> funcScope;
        for (auto& param : node->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                funcScope.insert(id->name);
            }
        }
        
        FreeVariableCollector fvc(funcScope);
        for (auto& stmt : node->body) {
            stmt->accept(&fvc);
        }
        
        for (const auto& name : fvc.referencedNames) {
            if (outerScope.count(name)) {
                capturedNames.insert(name);
            }
        }
    }
    
    // --- Standard traversal: just recurse to find inner arrow functions ---
    void visitVariableDeclaration(ast::VariableDeclaration* node) override {
        if (node->initializer) node->initializer->accept(this);
    }
    void visitBlockStatement(ast::BlockStatement* node) override {
        for (auto& stmt : node->statements) stmt->accept(this);
    }
    void visitIfStatement(ast::IfStatement* node) override {
        node->condition->accept(this);
        node->thenStatement->accept(this);
        if (node->elseStatement) node->elseStatement->accept(this);
    }
    void visitWhileStatement(ast::WhileStatement* node) override {
        node->condition->accept(this);
        node->body->accept(this);
    }
    void visitForStatement(ast::ForStatement* node) override {
        if (node->initializer) node->initializer->accept(this);
        if (node->condition) node->condition->accept(this);
        if (node->incrementor) node->incrementor->accept(this);
        node->body->accept(this);
    }
    void visitForOfStatement(ast::ForOfStatement* node) override {
        node->expression->accept(this);
        if (node->initializer) node->initializer->accept(this);
        node->body->accept(this);
    }
    void visitForInStatement(ast::ForInStatement* node) override {
        node->expression->accept(this);
        if (node->initializer) node->initializer->accept(this);
        node->body->accept(this);
    }
    void visitReturnStatement(ast::ReturnStatement* node) override {
        if (node->expression) node->expression->accept(this);
    }
    void visitExpressionStatement(ast::ExpressionStatement* node) override {
        node->expression->accept(this);
    }
    void visitCallExpression(ast::CallExpression* node) override {
        node->callee->accept(this);
        for (auto& arg : node->arguments) arg->accept(this);
    }

    void visitParenthesizedExpression(ast::ParenthesizedExpression* node) override {
        if (node->expression) node->expression->accept(this);
    }
    void visitBinaryExpression(ast::BinaryExpression* node) override {
        node->left->accept(this);
        node->right->accept(this);
    }
    void visitAssignmentExpression(ast::AssignmentExpression* node) override {
        node->left->accept(this);
        node->right->accept(this);
    }
    void visitConditionalExpression(ast::ConditionalExpression* node) override {
        node->condition->accept(this);
        node->whenTrue->accept(this);
        node->whenFalse->accept(this);
    }
    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) override {
        for (auto& elem : node->elements) if (elem) elem->accept(this);
    }
    void visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) override {
        for (auto& prop : node->properties) prop->accept(this);
    }
    void visitPropertyAssignment(ast::PropertyAssignment* node) override {
        node->initializer->accept(this);
    }
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node) override {
        node->expression->accept(this);
    }
    void visitElementAccessExpression(ast::ElementAccessExpression* node) override {
        node->expression->accept(this);
        node->argumentExpression->accept(this);
    }
    void visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) override {
        node->operand->accept(this);
    }
    void visitDeleteExpression(ast::DeleteExpression* node) override {
        node->expression->accept(this);
    }
    void visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) override {
        node->operand->accept(this);
    }
    void visitNewExpression(ast::NewExpression* node) override {
        node->expression->accept(this);
        for (auto& arg : node->arguments) arg->accept(this);
    }
    void visitAwaitExpression(ast::AwaitExpression* node) override {
        if (node->expression) node->expression->accept(this);
    }
    void visitYieldExpression(ast::YieldExpression* node) override {
        if (node->expression) node->expression->accept(this);
    }
    void visitTemplateExpression(ast::TemplateExpression* node) override {
        for (auto& span : node->spans) span.expression->accept(this);
    }
    void visitSpreadElement(ast::SpreadElement* node) override {
        node->expression->accept(this);
    }
    void visitTryStatement(ast::TryStatement* node) override {
        for (auto& stmt : node->tryBlock) stmt->accept(this);
        if (node->catchClause) {
            for (auto& stmt : node->catchClause->block) stmt->accept(this);
        }
        for (auto& stmt : node->finallyBlock) stmt->accept(this);
    }
    void visitThrowStatement(ast::ThrowStatement* node) override {
        node->expression->accept(this);
    }
    void visitSwitchStatement(ast::SwitchStatement* node) override {
        node->expression->accept(this);
        for (auto& clause : node->clauses) {
            if (auto cc = dynamic_cast<ast::CaseClause*>(clause.get())) {
                if (cc->expression) cc->expression->accept(this);
                for (auto& stmt : cc->statements) stmt->accept(this);
            } else if (auto dc = dynamic_cast<ast::DefaultClause*>(clause.get())) {
                for (auto& stmt : dc->statements) stmt->accept(this);
            }
        }
    }
    void visitAsExpression(ast::AsExpression* node) override {
        node->expression->accept(this);
    }
    
    // --- Leaf nodes / no-ops: don't need recursion ---
    void visitIdentifier(ast::Identifier*) override {}
    void visitNumericLiteral(ast::NumericLiteral*) override {}
    void visitStringLiteral(ast::StringLiteral*) override {}
    void visitBooleanLiteral(ast::BooleanLiteral*) override {}
    void visitNullLiteral(ast::NullLiteral*) override {}
    void visitUndefinedLiteral(ast::UndefinedLiteral*) override {}
    void visitBigIntLiteral(ast::BigIntLiteral*) override {}
    void visitRegularExpressionLiteral(ast::RegularExpressionLiteral*) override {}
    void visitBreakStatement(ast::BreakStatement*) override {}
    void visitContinueStatement(ast::ContinueStatement*) override {}
    void visitProgram(ast::Program*) override {}
    void visitFunctionDeclaration(ast::FunctionDeclaration*) override {}
    void visitClassDeclaration(ast::ClassDeclaration*) override {}
    void visitInterfaceDeclaration(ast::InterfaceDeclaration*) override {}
    void visitTypeAliasDeclaration(ast::TypeAliasDeclaration*) override {}
    void visitEnumDeclaration(ast::EnumDeclaration*) override {}
    void visitImportDeclaration(ast::ImportDeclaration*) override {}
    void visitExportDeclaration(ast::ExportDeclaration*) override {}
    void visitExportAssignment(ast::ExportAssignment*) override {}
    void visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment*) override {}
    void visitComputedPropertyName(ast::ComputedPropertyName*) override {}
    void visitMethodDefinition(ast::MethodDefinition*) override {}
    void visitStaticBlock(ast::StaticBlock*) override {}
    void visitSuperExpression(ast::SuperExpression*) override {}
    void visitObjectBindingPattern(ast::ObjectBindingPattern*) override {}
    void visitArrayBindingPattern(ast::ArrayBindingPattern*) override {}
    void visitBindingElement(ast::BindingElement*) override {}
    void visitOmittedExpression(ast::OmittedExpression*) override {}
    void visitTaggedTemplateExpression(ast::TaggedTemplateExpression*) override {}
};

void IRGenerator::collectCapturedVariableNames(ast::Node* node, 
                                               const std::set<std::string>& outerScope,
                                               std::set<std::string>& capturedNames) {
    CapturedNameCollector collector(outerScope);
    node->accept(&collector);
    for (const auto& name : collector.capturedNames) {
        capturedNames.insert(name);
    }
}

// ============================================================================
// Inline Boxing Infrastructure
// ============================================================================
// These helpers emit LLVM IR directly for boxing/unboxing, avoiding function
// call overhead and heap allocation. TsValue is stack-allocated and LLVM's
// mem2reg pass will optimize it to registers when possible.

void IRGenerator::initTsValueType() {
    if (tsValueType) return;  // Already initialized
    
    // TsValue structure: { i8 type, [7 x i8] padding, i64 value }
    // This matches the C++ TaggedValue layout:
    //   - 1 byte for ValueType enum
    //   - 7 bytes padding for alignment
    //   - 8 bytes for the union (int64/double/bool/ptr)
    // Total: 16 bytes
    tsValueType = llvm::StructType::create(*context, "TsValue");
    tsValueType->setBody({
        builder->getInt8Ty(),                           // type field (ValueType enum)
        llvm::ArrayType::get(builder->getInt8Ty(), 7),  // padding for alignment
        builder->getInt64Ty()                           // value union (all members fit in i64)
    });
}

// Convert TypeKind to runtime ValueType enum value
// Must match runtime/include/TsObject.h ValueType enum
static uint8_t typeKindToValueType(ts::TypeKind kind) {
    switch (kind) {
        case ts::TypeKind::Undefined: return 0;  // UNDEFINED
        case ts::TypeKind::Int:       return 1;  // NUMBER_INT
        case ts::TypeKind::Double:    return 2;  // NUMBER_DBL
        case ts::TypeKind::Boolean:   return 3;  // BOOLEAN
        case ts::TypeKind::String:    return 4;  // STRING_PTR
        case ts::TypeKind::Object:
        case ts::TypeKind::Class:
        case ts::TypeKind::Interface:
        case ts::TypeKind::Intersection:
        case ts::TypeKind::Map:
        case ts::TypeKind::SetType:   return 5;  // OBJECT_PTR
        // Note: TypeKind::Promise conflicts with Windows macro, using explicit namespace
        case (ts::TypeKind)16:        return 6;  // PROMISE_PTR (TypeKind::Promise = 16)
        case ts::TypeKind::Array:
        case ts::TypeKind::Tuple:     return 7;  // ARRAY_PTR
        case ts::TypeKind::BigInt:    return 8;  // BIGINT_PTR
        case ts::TypeKind::Symbol:    return 9;  // SYMBOL_PTR
        case ts::TypeKind::Function:  return 10; // FUNCTION_PTR
        default:                      return 5;  // Default to OBJECT_PTR for unknown types
    }
}

llvm::Value* IRGenerator::emitInlineBox(llvm::Value* rawPtr, uint8_t valueType) {
    initTsValueType();
    
    // Allocate TsValue on stack
    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::IRBuilder<> entryBuilder(&currentFunc->getEntryBlock(), currentFunc->getEntryBlock().begin());
    llvm::AllocaInst* boxed = entryBuilder.CreateAlloca(tsValueType, nullptr, "inlineBoxed");
    
    // Store type field
    llvm::Value* typePtr = builder->CreateStructGEP(tsValueType, boxed, 0, "typePtr");
    builder->CreateStore(builder->getInt8(valueType), typePtr);
    
    // Store value field (index 2, after padding)
    llvm::Value* valuePtr = builder->CreateStructGEP(tsValueType, boxed, 2, "valuePtr");
    
    // Convert pointer to i64 for storage in the union
    llvm::Value* ptrAsInt = builder->CreatePtrToInt(rawPtr, builder->getInt64Ty(), "ptrAsInt");
    builder->CreateStore(ptrAsInt, valuePtr);
    
    // Track as boxed for downstream code
    boxedValues.insert(boxed);
    
    return boxed;
}

llvm::Value* IRGenerator::emitInlineUnbox(llvm::Value* boxedVal) {
    initTsValueType();
    
    // Load value field (index 2, after padding)
    llvm::Value* valuePtr = builder->CreateStructGEP(tsValueType, boxedVal, 2, "valuePtr");
    llvm::Value* valueAsInt = builder->CreateLoad(builder->getInt64Ty(), valuePtr, "valueAsInt");
    
    // Convert i64 back to pointer
    return builder->CreateIntToPtr(valueAsInt, builder->getPtrTy(), "unboxed");
}

llvm::Value* IRGenerator::emitTypeCheck(llvm::Value* boxedVal, uint8_t expectedType) {
    initTsValueType();
    
    // Load type field
    llvm::Value* typePtr = builder->CreateStructGEP(tsValueType, boxedVal, 0, "typePtr");
    llvm::Value* actualType = builder->CreateLoad(builder->getInt8Ty(), typePtr, "actualType");
    
    // Compare with expected type
    return builder->CreateICmpEQ(actualType, builder->getInt8(expectedType), "typeMatch");
}

// ============================================================================
// Value-passing API Helpers (_v variants)
// ============================================================================
// These helpers call runtime functions that take/return TsValue by value.
// This avoids heap allocation for intermediate TsValue objects.

llvm::Value* IRGenerator::emitObjectGetPropV(llvm::Value* objBoxed, llvm::Value* keyBoxed) {
    initTsValueType();
    
    // Allocate space for return value on stack
    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::IRBuilder<> entryBuilder(&currentFunc->getEntryBlock(), currentFunc->getEntryBlock().begin());
    llvm::AllocaInst* result = entryBuilder.CreateAlloca(tsValueType, nullptr, "getPropResult");
    
    // Load obj and key TsValue structs
    llvm::Value* objVal = builder->CreateLoad(tsValueType, objBoxed, "objVal");
    llvm::Value* keyVal = builder->CreateLoad(tsValueType, keyBoxed, "keyVal");
    
    // Call ts_object_get_prop_v: TsValue (TsValue, TsValue)
    llvm::FunctionType* ft = llvm::FunctionType::get(tsValueType, { tsValueType, tsValueType }, false);
    llvm::FunctionCallee fn = getRuntimeFunction("ts_object_get_prop_v", ft);
    
    llvm::Value* retVal = createCall(ft, fn.getCallee(), { objVal, keyVal });
    
    // Store result
    builder->CreateStore(retVal, result);
    boxedValues.insert(result);
    
    return result;
}

llvm::Value* IRGenerator::emitMapGetV(llvm::Value* rawMap, llvm::Value* keyBoxed) {
    initTsValueType();
    
    // Allocate space for return value on stack
    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::IRBuilder<> entryBuilder(&currentFunc->getEntryBlock(), currentFunc->getEntryBlock().begin());
    llvm::AllocaInst* result = entryBuilder.CreateAlloca(tsValueType, nullptr, "mapGetResult");
    
    // Load key TsValue struct
    llvm::Value* keyVal = builder->CreateLoad(tsValueType, keyBoxed, "keyVal");
    
    // Call ts_map_get_v: TsValue (void*, TsValue)
    llvm::FunctionType* ft = llvm::FunctionType::get(tsValueType, { builder->getPtrTy(), tsValueType }, false);
    llvm::FunctionCallee fn = getRuntimeFunction("ts_map_get_v", ft);
    
    llvm::Value* retVal = createCall(ft, fn.getCallee(), { rawMap, keyVal });
    
    // Store result
    builder->CreateStore(retVal, result);
    boxedValues.insert(result);
    
    return result;
}

llvm::Value* IRGenerator::emitArrayGetV(llvm::Value* rawArr, llvm::Value* index) {
    initTsValueType();
    
    // Allocate space for return value on stack
    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::IRBuilder<> entryBuilder(&currentFunc->getEntryBlock(), currentFunc->getEntryBlock().begin());
    llvm::AllocaInst* result = entryBuilder.CreateAlloca(tsValueType, nullptr, "arrayGetResult");
    
    // Call ts_array_get_v: TsValue (void*, int64)
    llvm::FunctionType* ft = llvm::FunctionType::get(tsValueType, { builder->getPtrTy(), builder->getInt64Ty() }, false);
    llvm::FunctionCallee fn = getRuntimeFunction("ts_array_get_v", ft);
    
    llvm::Value* retVal = createCall(ft, fn.getCallee(), { rawArr, index });
    
    // Store result
    builder->CreateStore(retVal, result);
    boxedValues.insert(result);
    
    return result;
}

llvm::Value* IRGenerator::boxValue(llvm::Value* val, std::shared_ptr<Type> type) {
    // Handle explicit null/undefined first
    if (type) {
        if (type->kind == TypeKind::Null) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_value_make_null", ft);
            llvm::Value* res = createCall(ft, fn.getCallee(), {});
            boxedValues.insert(res);
            return res;
        }
        if (type->kind == TypeKind::Undefined) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_value_make_undefined", ft);
            llvm::Value* res = createCall(ft, fn.getCallee(), {});
            boxedValues.insert(res);
            return res;
        }
        if (type->kind == TypeKind::Any) {
            if (val && boxedValues.count(val)) return val;

            // Handle primitives explicitly
            if (val) {
                llvm::Type* valType = val->getType();
                if (valType->isIntegerTy(64)) {
                    return boxValue(val, std::make_shared<Type>(TypeKind::Int));
                }
                if (valType->isDoubleTy()) return boxValue(val, std::make_shared<Type>(TypeKind::Double));
                if (valType->isIntegerTy(1)) return boxValue(val, std::make_shared<Type>(TypeKind::Boolean));

                // CRITICAL: `any` values can still be raw compiled function pointers (e.g. hoisted
                // function expressions or IIFEs). Boxing them as `ts_value_box_any` will produce an
                // invalid object and crash when invoked; wrap them as TsFunction instead.
                llvm::Value* stripped = val->stripPointerCasts();
                if (stripped && llvm::isa<llvm::Function>(stripped)) {
                    llvm::FunctionType* fnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_value_make_function", fnFt);

                    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
                    llvm::Value* currentContext = nullptr;
                    if (currentFunc->arg_size() > 0 && currentFunc->getArg(0)->getType()->isPointerTy()) {
                        currentContext = currentFunc->getArg(0);
                    } else {
                        currentContext = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    }

                    llvm::Value* res = createCall(fnFt, fn.getCallee(), { builder->CreateBitCast(stripped, builder->getPtrTy()), currentContext });
                    boxedValues.insert(res);
                    return res;
                }
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_value_box_any", ft);
            llvm::Value* res = createCall(ft, fn.getCallee(), { val ? val : llvm::ConstantPointerNull::get(builder->getPtrTy()) });
            boxedValues.insert(res);
            return res;
        }
    }

    if (!val) {
        if (type && type->kind == TypeKind::Void) {
            return getUndefinedValue();
        }
        return getUndefinedValue();
    }
    if (boxedValues.count(val)) {
        return val;
    }

    llvm::Type* valType = val->getType();
    if (valType->isPointerTy()) {
        if (type && (type->kind == TypeKind::Int || type->kind == TypeKind::Double || type->kind == TypeKind::Boolean)) {
            // Already boxed primitive masquerading as pointer
            return val;
        }
    }

    std::string funcName;

    // Prefer semantic type to choose boxing
    if (type) {
        switch (type->kind) {
            case TypeKind::Int:       funcName = "ts_value_make_int"; break;
            case TypeKind::Double:    funcName = "ts_value_make_double"; break;
            case TypeKind::Boolean:   funcName = "ts_value_make_bool"; break;
            case TypeKind::String:    funcName = "ts_value_make_string"; break;
            case TypeKind::Array:
            case TypeKind::Tuple:     funcName = "ts_value_make_array"; break;
            case TypeKind::BigInt:    funcName = "ts_value_make_bigint"; break;
            case TypeKind::Symbol:    funcName = "ts_value_make_symbol"; break;
            case TypeKind::Map:
            case TypeKind::SetType:
            case TypeKind::Object:
            case TypeKind::Intersection:
            case TypeKind::Union:     funcName = "ts_value_make_object"; break;
            case TypeKind::Class:
            case TypeKind::Interface: {
                std::string name;
                if (type->kind == TypeKind::Class) name = std::static_pointer_cast<ClassType>(type)->name;
                else name = std::static_pointer_cast<InterfaceType>(type)->name;
                funcName = (name.find("Promise") == 0) ? "ts_value_make_promise" : "ts_value_make_object";
                break;
            }
            case TypeKind::Enum:      funcName = "ts_value_make_int"; break;
            default: break;
        }
    }

    // Fallback to LLVM type
    if (funcName.empty()) {
        if (valType->isIntegerTy(64)) funcName = "ts_value_make_int";
        else if (valType->isDoubleTy()) funcName = "ts_value_make_double";
        else if (valType->isIntegerTy(1)) funcName = "ts_value_make_bool";
        else if (valType->isVoidTy() || valType->isIntegerTy(8)) {
            llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee undefFn = getRuntimeFunction("ts_value_make_undefined", undefFt);
            llvm::Value* res = createCall(undefFt, undefFn.getCallee(), {});
            boxedValues.insert(res);
            return res;
        } else if ((type && type->kind == TypeKind::Function) || llvm::isa<llvm::Function>(val->stripPointerCasts())) {
            llvm::FunctionType* fnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_value_make_function", fnFt);

            // If the value is a bitcast/gep/etc of a function, strip the cast so we reliably
            // create a TsFunction wrapper instead of treating the raw code pointer as an object.
            llvm::Value* funcPtrVal = val;
            if (llvm::isa<llvm::Function>(val->stripPointerCasts())) {
                funcPtrVal = val->stripPointerCasts();
            }

            llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
            llvm::Value* currentContext = nullptr;
            if (currentFunc->arg_size() > 0 && currentFunc->getArg(0)->getType()->isPointerTy()) {
                currentContext = currentFunc->getArg(0);
            } else {
                currentContext = llvm::ConstantPointerNull::get(builder->getPtrTy());
            }

            llvm::Value* res = createCall(fnFt, fn.getCallee(), { builder->CreateBitCast(funcPtrVal, builder->getPtrTy()), currentContext });
            boxedValues.insert(res);
            return res;
        } else if (valType->isPointerTy()) {
            funcName = "ts_value_box_any";
        }
    }

    if (funcName.empty()) return val;
    
    llvm::FunctionType* boxFt = nullptr;
    if (funcName == "ts_value_make_undefined") boxFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    else if (funcName == "ts_value_make_int") boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
    else if (funcName == "ts_value_make_double") boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getDoubleTy() }, false);
    else if (funcName == "ts_value_make_bool") boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt1Ty() }, false);
    else boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);

    llvm::FunctionCallee boxFn = module->getOrInsertFunction(funcName, boxFt);
    llvm::Value* res = createCall(boxFt, boxFn.getCallee(), { val });
    boxedValues.insert(res);
    SPDLOG_DEBUG("boxValue: added {} to boxedValues via {}", (void*)res, funcName);
    return res;
}

llvm::Value* IRGenerator::unboxValue(llvm::Value* val, std::shared_ptr<Type> type) {
    if (!type || !val) return val;
    
    // If it's Any, it's already in its standard boxed form (TsValue*)
    if (type->kind == TypeKind::Any) return val;
    
    // If it's not a pointer, it's already unboxed (primitive)
    if (!val->getType()->isPointerTy()) return val;

    // For Object/Array/Map/Set/Tuple/BigInt/Symbol types, ALWAYS unbox
    // These can be loaded from globals/allocas that store boxed values
    // ts_value_get_object is safe to call even if already unboxed
    if (type->kind == TypeKind::Object || type->kind == TypeKind::Intersection || 
        type->kind == TypeKind::Array || type->kind == TypeKind::Map || type->kind == TypeKind::SetType ||
        type->kind == TypeKind::Tuple || type->kind == TypeKind::BigInt || type->kind == TypeKind::Symbol) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_object", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    }

    // For primitive types, only unbox if tracked in boxedValues
    if (!boxedValues.count(val)) return val;

    if (type->kind == TypeKind::Int) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_int", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Double) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_double", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Boolean) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_bool", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::String) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_string", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(type);
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_object", unboxFt);
        if (classType->isStruct) {
            llvm::Value* objPtr = createCall(unboxFt, unboxFn.getCallee(), { val });
            
            llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, classType->name);
            return builder->CreateLoad(classStruct, objPtr);
        }
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Void) {
        return nullptr;
    }
    
    return val;
}

void IRGenerator::visitObjectBindingPattern(ast::ObjectBindingPattern* node) {}
void IRGenerator::visitArrayBindingPattern(ast::ArrayBindingPattern* node) {}
void IRGenerator::visitBindingElement(ast::BindingElement* node) {}
void IRGenerator::visitSpreadElement(ast::SpreadElement* node) {}
void IRGenerator::visitOmittedExpression(ast::OmittedExpression* node) {}

void IRGenerator::visitImportDeclaration(ast::ImportDeclaration* node) {}
void IRGenerator::visitExportDeclaration(ast::ExportDeclaration* node) {}

void IRGenerator::visitProgram(ast::Program* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

void IRGenerator::visitClassDeclaration(ast::ClassDeclaration* node) {}
void IRGenerator::visitInterfaceDeclaration(ast::InterfaceDeclaration* node) {}
void IRGenerator::visitFunctionDeclaration(ast::FunctionDeclaration* node) {
    // Handle function declarations inside function bodies (e.g., function lodash() {} inside an IIFE)
    // These functions should be hoisted and accessible by name in the enclosing scope.
    
    if (node->name.empty()) {
        return; // Anonymous function declarations are not hoisted
    }
    
    // Check if this function is already generated by the monomorphizer
    llvm::Function* existingFunc = module->getFunction(node->name);
    if (existingFunc) {
        // Function already exists, just create a reference to it
        llvm::FunctionType* makeFnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee makeFnFn = getRuntimeFunction("ts_value_make_function", makeFnFt);
        llvm::Value* boxedFunc = createCall(makeFnFt, makeFnFn.getCallee(), { 
            existingFunc, 
            currentContext ? currentContext : llvm::ConstantPointerNull::get(builder->getPtrTy())
        });
        boxedValues.insert(boxedFunc);
        
        // Check if we have a hoisted placeholder to update
        if (namedValues.count(node->name)) {
            builder->CreateStore(boxedFunc, namedValues[node->name]);
        } else {
            llvm::AllocaInst* alloca = createEntryBlockAlloca(
                builder->GetInsertBlock()->getParent(), 
                node->name, 
                builder->getPtrTy()
            );
            builder->CreateStore(boxedFunc, alloca);
            namedValues[node->name] = alloca;
        }
        boxedVariables.insert(node->name);
        return;
    }
    
    // Generate the function inline (for permissive/untyped JS mode)
    static int declFuncCounter = 0;
    std::string funcName = node->name + "_decl_" + std::to_string(declFuncCounter++);
    
    std::vector<llvm::Type*> argTypes;
    argTypes.push_back(builder->getPtrTy()); // context
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        argTypes.push_back(builder->getPtrTy()); // TsValue*
    }
    
    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), argTypes, false);
    llvm::Function* function = llvm::Function::Create(ft, llvm::Function::InternalLinkage, funcName, module.get());
    
    llvm::BasicBlock* oldBB = builder->GetInsertBlock();
    
    // === CLOSURE CAPTURE: Collect free variables BEFORE clearing namedValues ===
    std::set<std::string> paramNames;
    for (auto& param : node->parameters) {
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramNames.insert(id->name);
        }
    }
    
    std::vector<CapturedVariable> capturedVars;
    for (auto& stmt : node->body) {
        collectFreeVariables(stmt.get(), paramNames, capturedVars);
    }
    
    // Create closure context struct type (if we have captures)
    llvm::StructType* closureContextType = nullptr;
    llvm::Value* closureContext = nullptr;
    std::map<std::string, int> capturedVarIndices;
    
    std::set<std::string> capturedCellVarNames;
    if (!capturedVars.empty()) {
        std::vector<llvm::Type*> contextFields;
        for (size_t i = 0; i < capturedVars.size(); ++i) {
            contextFields.push_back(builder->getPtrTy());
            capturedVarIndices[capturedVars[i].name] = static_cast<int>(i);
        }
        closureContextType = llvm::StructType::create(*context, funcName + "_closure");
        closureContextType->setBody(contextFields);
        
        // Allocate and populate the closure context (in the OUTER function)
        // Use ts_pool_alloc for faster closure allocation from size-class pools
        llvm::FunctionType* allocFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee allocFn = getRuntimeFunction("ts_pool_alloc", allocFt);
        uint64_t contextSize = module->getDataLayout().getTypeAllocSize(closureContextType);
        closureContext = createCall(allocFt, allocFn.getCallee(), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), contextSize) });
        
        // Store captured values into the context struct
        for (size_t i = 0; i < capturedVars.size(); ++i) {
            llvm::Value* fieldPtr = builder->CreateStructGEP(closureContextType, closureContext, static_cast<unsigned>(i));
            
            // Check if this is a cell variable
            if (cellVariables.count(capturedVars[i].name)) {
                llvm::Value* cellPtr = builder->CreateLoad(builder->getPtrTy(), capturedVars[i].value);
                builder->CreateStore(cellPtr, fieldPtr);
                capturedCellVarNames.insert(capturedVars[i].name);
                SPDLOG_INFO("FunctionDecl: Captured cell variable {} at index {}", capturedVars[i].name, i);
                continue;
            }
            
            llvm::Type* loadType = builder->getPtrTy();
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(capturedVars[i].value)) {
                loadType = alloca->getAllocatedType();
            }
            
            llvm::Value* outerValue = builder->CreateLoad(loadType, capturedVars[i].value);
            
            bool shouldBox = !loadType->isPointerTy() || !boxedValues.count(outerValue);
            if (capturedVars[i].type && capturedVars[i].type->kind == TypeKind::Function && loadType->isPointerTy()) {
                shouldBox = false;
            }
            
            if (shouldBox) {
                outerValue = boxValue(outerValue, capturedVars[i].type);
            }
            builder->CreateStore(outerValue, fieldPtr);
        }
        
        SPDLOG_INFO("FunctionDeclaration {} captures {} variables", funcName, capturedVars.size());
        functionsWithClosures.insert(funcName);
    }

    // Save ALL function-scoped state (same as visitFunctionExpression)
    struct SavedFunctionState {
        std::map<std::string, llvm::Value*> namedValues;
        std::map<std::string, llvm::Type*> forcedVariableTypes;
        std::map<std::string, std::shared_ptr<Type>> variableTypes;
        std::map<ast::Node*, llvm::Value*> valueOverrides;
        std::set<llvm::Value*> boxedValues;
        std::set<std::string> boxedVariables;
        std::set<std::string> boxedElementArrayVars;
        std::set<std::string> cellVariables;
        std::map<std::string, llvm::Value*> cellPointers;
        std::map<llvm::Value*, std::string> lengthAliases;
        std::string lastLengthArray;
        std::vector<CatchInfo> catchStack;
        std::vector<FinallyInfo> finallyStack;
        std::vector<LoopInfo> loopStack;
        std::set<llvm::Value*> nonNullValues;
        std::set<llvm::Value*> checkedAllocas;
        std::shared_ptr<Type> currentClass;
        std::shared_ptr<Type> currentReturnType;
        llvm::Value* currentContext;
        llvm::BasicBlock* currentBreakBB;
        llvm::BasicBlock* currentContinueBB;
        llvm::BasicBlock* currentReturnBB;
        llvm::Value* currentReturnValueAlloca;
        llvm::Value* currentShouldReturnAlloca;
        llvm::Value* currentShouldBreakAlloca;
        llvm::Value* currentShouldContinueAlloca;
        llvm::Value* currentBreakTargetAlloca;
        llvm::Value* currentContinueTargetAlloca;
        llvm::Value* currentAsyncContext;
        llvm::Value* currentAsyncResumedValue;
        llvm::Value* currentAsyncFrame;
        llvm::StructType* currentAsyncFrameType;
        std::map<std::string, int> currentAsyncFrameMap;
        llvm::BasicBlock* asyncDispatcherBB;
        std::vector<llvm::BasicBlock*> asyncStateBlocks;
        bool currentIsGenerator;
        bool currentIsAsync;
        int anonVarCounter;
    };
    
    SavedFunctionState saved{
        namedValues,
        forcedVariableTypes,
        variableTypes,
        valueOverrides,
        boxedValues,
        boxedVariables,
        boxedElementArrayVars,
        cellVariables,
        cellPointers,
        lengthAliases,
        lastLengthArray,
        catchStack,
        finallyStack,
        loopStack,
        nonNullValues,
        checkedAllocas,
        currentClass,
        currentReturnType,
        currentContext,
        currentBreakBB,
        currentContinueBB,
        currentReturnBB,
        currentReturnValueAlloca,
        currentShouldReturnAlloca,
        currentShouldBreakAlloca,
        currentShouldContinueAlloca,
        currentBreakTargetAlloca,
        currentContinueTargetAlloca,
        currentAsyncContext,
        currentAsyncResumedValue,
        currentAsyncFrame,
        currentAsyncFrameType,
        currentAsyncFrameMap,
        asyncDispatcherBB,
        asyncStateBlocks,
        currentIsGenerator,
        currentIsAsync,
        anonVarCounter,
    };
    
    // Create function entry
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(entry);
    
    // Clear function-specific state for this nested function body
    namedValues.clear();
    forcedVariableTypes.clear();
    variableTypes.clear();
    valueOverrides.clear();
    boxedVariables.clear();
    boxedValues.clear();
    boxedElementArrayVars.clear();
    cellVariables.clear();
    cellPointers.clear();
    lengthAliases.clear();
    lastLengthArray.clear();
    catchStack.clear();
    finallyStack.clear();
    loopStack.clear();
    nonNullValues.clear();
    checkedAllocas.clear();
    lastValue = nullptr;
    anonVarCounter = 0;
    
    // Force return type to Any for function declarations
    currentReturnType = std::make_shared<Type>(TypeKind::Any);
    
    // Initialize per-function return/break/continue state
    currentBreakBB = nullptr;
    currentContinueBB = nullptr;
    currentReturnBB = llvm::BasicBlock::Create(*context, "return", function);
    currentShouldReturnAlloca = createEntryBlockAlloca(function, "shouldReturn", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldReturnAlloca);
    currentShouldBreakAlloca = createEntryBlockAlloca(function, "shouldBreak", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
    currentShouldContinueAlloca = createEntryBlockAlloca(function, "shouldContinue", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldContinueAlloca);
    currentBreakTargetAlloca = createEntryBlockAlloca(function, "breakTarget", builder->getPtrTy());
    currentContinueTargetAlloca = createEntryBlockAlloca(function, "continueTarget", builder->getPtrTy());
    currentReturnValueAlloca = createEntryBlockAlloca(function, "returnValue", builder->getPtrTy());
    builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), currentReturnValueAlloca);
    
    auto argIt = function->arg_begin();
    llvm::Value* contextArg = nullptr;
    if (argIt != function->arg_end()) {
        argIt->setName("context");
        contextArg = &*argIt;
        currentContext = contextArg;
        ++argIt;
    }
    
    // === CLOSURE CAPTURE: Extract captured values from context at function entry ===
    if (!capturedVars.empty() && closureContextType && contextArg) {
        for (size_t i = 0; i < capturedVars.size(); ++i) {
            const auto& cv = capturedVars[i];
            // Create a local alloca for this captured variable
            llvm::AllocaInst* alloca = createEntryBlockAlloca(function, cv.name, builder->getPtrTy());
            // Load the value from the context struct
            llvm::Value* fieldPtr = builder->CreateStructGEP(closureContextType, contextArg, static_cast<unsigned>(i));
            llvm::Value* capturedValue = builder->CreateLoad(builder->getPtrTy(), fieldPtr);
            // Store into local alloca
            builder->CreateStore(capturedValue, alloca);
            // Add to namedValues so the body can use it
            namedValues[cv.name] = alloca;
            
            // Check if this was a cell variable in the outer scope
            if (capturedCellVarNames.count(cv.name)) {
                cellVariables.insert(cv.name);
                cellPointers[cv.name] = capturedValue;
                SPDLOG_INFO("FunctionDecl: Extracted cell variable {} at index {}", cv.name, i);
            } else {
                boxedValues.insert(capturedValue);
                boxedVariables.insert(cv.name);
                SPDLOG_INFO("FunctionDecl: Extracted captured var {} at index {}", cv.name, i);
            }
        }
    }
    
    // Handle parameters
    unsigned idx = 0;
    while (argIt != function->arg_end() && idx < node->parameters.size()) {
        auto& param = node->parameters[idx];
        auto& arg = *argIt;
        if (auto* id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            arg.setName(id->name);
            boxedValues.insert(&arg);
            llvm::AllocaInst* paramAlloca = createEntryBlockAlloca(function, id->name, builder->getPtrTy());
            builder->CreateStore(&arg, paramAlloca);
            namedValues[id->name] = paramAlloca;
            boxedVariables.insert(id->name);
        }
        ++argIt;
        ++idx;
    }
    
    // Pre-pass: Hoist function declarations (JavaScript hoisting behavior)
    hoistFunctionDeclarations(node->body, function);
    
    // Pre-pass: Hoist variable declarations for closure capture
    hoistVariableDeclarations(node->body, function);
    
    // Visit function body
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
    
    // Finalize function
    llvm::BasicBlock* currentBB = builder->GetInsertBlock();
    if (currentBB && !currentBB->getTerminator()) {
        builder->CreateBr(currentReturnBB);
    }
    
    // Emit unified return block if it has predecessors
    if (llvm::pred_begin(currentReturnBB) == llvm::pred_end(currentReturnBB)) {
        currentReturnBB->eraseFromParent();
    } else {
        builder->SetInsertPoint(currentReturnBB);
        llvm::Value* retVal = builder->CreateLoad(builder->getPtrTy(), currentReturnValueAlloca);
        builder->CreateRet(retVal);
    }
    
    // Restore all state
    builder->SetInsertPoint(oldBB);
    namedValues = saved.namedValues;
    forcedVariableTypes = saved.forcedVariableTypes;
    variableTypes = saved.variableTypes;
    valueOverrides = saved.valueOverrides;
    boxedValues = saved.boxedValues;
    boxedVariables = saved.boxedVariables;
    boxedElementArrayVars = saved.boxedElementArrayVars;
    cellVariables = saved.cellVariables;
    cellPointers = saved.cellPointers;
    lengthAliases = saved.lengthAliases;
    lastLengthArray = saved.lastLengthArray;
    catchStack = saved.catchStack;
    finallyStack = saved.finallyStack;
    loopStack = saved.loopStack;
    nonNullValues = saved.nonNullValues;
    checkedAllocas = saved.checkedAllocas;
    currentClass = saved.currentClass;
    currentReturnType = saved.currentReturnType;
    currentContext = saved.currentContext;
    currentBreakBB = saved.currentBreakBB;
    currentContinueBB = saved.currentContinueBB;
    currentReturnBB = saved.currentReturnBB;
    currentReturnValueAlloca = saved.currentReturnValueAlloca;
    currentShouldReturnAlloca = saved.currentShouldReturnAlloca;
    currentShouldBreakAlloca = saved.currentShouldBreakAlloca;
    currentShouldContinueAlloca = saved.currentShouldContinueAlloca;
    currentBreakTargetAlloca = saved.currentBreakTargetAlloca;
    currentContinueTargetAlloca = saved.currentContinueTargetAlloca;
    currentAsyncContext = saved.currentAsyncContext;
    currentAsyncResumedValue = saved.currentAsyncResumedValue;
    currentAsyncFrame = saved.currentAsyncFrame;
    currentAsyncFrameType = saved.currentAsyncFrameType;
    currentAsyncFrameMap = saved.currentAsyncFrameMap;
    asyncDispatcherBB = saved.asyncDispatcherBB;
    asyncStateBlocks = saved.asyncStateBlocks;
    currentIsGenerator = saved.currentIsGenerator;
    currentIsAsync = saved.currentIsAsync;
    anonVarCounter = saved.anonVarCounter;
    
    // Box the function and store in namedValues for the enclosing scope
    llvm::FunctionType* makeFnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee makeFnFn = getRuntimeFunction("ts_value_make_function", makeFnFt);
    
    // Use closure context if we have captures, otherwise use the enclosing context
    llvm::Value* contextToPass = closureContext ? closureContext : 
        (saved.currentContext ? saved.currentContext : llvm::ConstantPointerNull::get(builder->getPtrTy()));
    
    llvm::Value* boxedFunc = createCall(makeFnFt, makeFnFn.getCallee(), { 
        function, 
        contextToPass
    });
    boxedValues.insert(boxedFunc);
    
    // Check if we have a hoisted placeholder to update
    if (namedValues.count(node->name)) {
        // Update the existing alloca with the real function
        builder->CreateStore(boxedFunc, namedValues[node->name]);
    } else {
        // No hoisted placeholder, create a new alloca
        llvm::AllocaInst* alloca = createEntryBlockAlloca(
            builder->GetInsertBlock()->getParent(), 
            node->name, 
            builder->getPtrTy()
        );
        builder->CreateStore(boxedFunc, alloca);
        namedValues[node->name] = alloca;
    }
    boxedVariables.insert(node->name);
}
void IRGenerator::visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) {}
void IRGenerator::visitEnumDeclaration(ast::EnumDeclaration* node) {}

llvm::FunctionCallee IRGenerator::getRuntimeFunction(const std::string& name, llvm::FunctionType* ft, bool enforceRegistration) {
    // Enforce registration for ts_* functions (runtime API)
    if (enforceRegistration && name.find("ts_") == 0) {
        // Check if this runtime function is in our registry
        boxingPolicy.assertRuntimeApiRegistered(name);
    }
    return module->getOrInsertFunction(name, ft);
}

void IRGenerator::dumpIR() {
    module->print(llvm::outs(), nullptr);
}

llvm::Value* IRGenerator::createCall(llvm::FunctionType* ft, llvm::Value* callee, std::vector<llvm::Value*> args) {
    std::string name;
    llvm::Value* actualCallee = callee->stripPointerCasts();
    if (auto func = llvm::dyn_cast<llvm::Function>(actualCallee)) name = func->getName().str();

    if (ft->getNumParams() != args.size()) {
        // Some functions might be vararg or we might have optional args
        // For now, just warn if not vararg
        if (!ft->isVarArg() && args.size() < ft->getNumParams()) {
            // Warning: Too few arguments
        }
    }
    
    std::vector<llvm::Value*> castedArgs;
    for (unsigned i = 0; i < args.size(); ++i) {
        if (i < ft->getNumParams()) {
            castedArgs.push_back(castValue(args[i], ft->getParamType(i)));
        } else {
            // For varargs, we don't have an expected type, but we should probably box them if they are primitives
            // to match our runtime's expectations for 'any'
            castedArgs.push_back(args[i]);
        }
    }
    
    llvm::Value* res = builder->CreateCall(ft, callee, castedArgs);
    if (res) {
        SPDLOG_INFO("createCall: {} with {} args, returnType={}, resType={}", name, args.size(), (int)ft->getReturnType()->getTypeID(), (int)res->getType()->getTypeID());
    } else {
        SPDLOG_INFO("createCall: {} with {} args, returnType={}, res=NULL", name, args.size(), (int)ft->getReturnType()->getTypeID());
    }

    if (ft->getReturnType()->isPointerTy()) {
        std::string name;
        llvm::Value* actualCallee = callee->stripPointerCasts();
        if (auto func = llvm::dyn_cast<llvm::Function>(actualCallee)) name = func->getName().str();
        
        // Use BoxingPolicy to determine if result is boxed
        // This is the SINGLE SOURCE OF TRUTH for boxing decisions
        if (!name.empty() && name.find("ts_") == 0) {
            if (boxingPolicy.runtimeReturnsBoxed(name)) {
                boxedValues.insert(res);
                SPDLOG_DEBUG("createCall: {} returns boxed value", name);
            } else {
                // Raw pointer - don't add to boxedValues
                SPDLOG_DEBUG("createCall: {} returns raw pointer", name);
            }
        }
        // User-defined functions are NOT assumed to return boxed values
    }
    return res;
}

llvm::Value* IRGenerator::castValue(llvm::Value* val, llvm::Type* expectedType) {
    if (!val) return nullptr;

    if (val->getType() == expectedType) {
        return val;
    }

    llvm::Value* result = nullptr;

    // Boxing: primitive -> ptr
    if (!val->getType()->isPointerTy() && expectedType->isPointerTy()) {
        if (val->getType()->isVoidTy()) {
            // Void -> undefined
            llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee undefFn = getRuntimeFunction("ts_value_make_undefined", undefFt);
            result = builder->CreateCall(undefFt, undefFn.getCallee());
        } else if (val->getType()->isStructTy()) {
            // Box struct
            llvm::FunctionType* allocFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee allocFn = getRuntimeFunction("ts_alloc", allocFt);
            
            uint64_t size = module->getDataLayout().getTypeAllocSize(val->getType());
            llvm::Value* heapPtr = createCall(allocFt, allocFn.getCallee(), { builder->getInt64(size) });
            builder->CreateStore(val, heapPtr);
            
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee boxFn = getRuntimeFunction("ts_value_make_object", boxFt);
            result = createCall(boxFt, boxFn.getCallee(), { heapPtr });
        } else if (val->getType()->isIntegerTy(1)) {
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt1Ty() }, false);
            llvm::FunctionCallee boxFn = getRuntimeFunction("ts_value_make_bool", boxFt);
            result = builder->CreateCall(boxFt, boxFn.getCallee(), { val });
        } else if (val->getType()->isIntegerTy(8)) {
            // Void/bool8 -> undefined
            llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee undefFn = getRuntimeFunction("ts_value_make_undefined", undefFt);
            result = builder->CreateCall(undefFt, undefFn.getCallee());
        } else if (val->getType()->isIntegerTy()) {
            llvm::Value* i64Val = builder->CreateIntCast(val, builder->getInt64Ty(), true);
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
            llvm::FunctionCallee boxFn = getRuntimeFunction("ts_value_make_int", boxFt);
            result = builder->CreateCall(boxFt, boxFn.getCallee(), { i64Val });
        } else if (val->getType()->isDoubleTy()) {
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getDoubleTy() }, false);
            llvm::FunctionCallee boxFn = getRuntimeFunction("ts_value_make_double", boxFt);
            result = builder->CreateCall(boxFt, boxFn.getCallee(), { val });
        }
    }

    // Unboxing: ptr -> primitive
    if (!result && val->getType()->isPointerTy() && !expectedType->isPointerTy()) {
        if (expectedType->isIntegerTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_int", unboxFt);
            llvm::Value* i64Val = builder->CreateCall(unboxFt, unboxFn.getCallee(), { val });
            result = builder->CreateIntCast(i64Val, expectedType, true);
        } else if (expectedType->isDoubleTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getDoubleTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_double", unboxFt);
            result = builder->CreateCall(unboxFt, unboxFn.getCallee(), { val });
        } else if (expectedType->isIntegerTy(1)) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_bool", unboxFt);
            result = builder->CreateCall(unboxFt, unboxFn.getCallee(), { val });
        } else if (expectedType->isStructTy()) {
            // Unbox struct from TsValue*
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_object", unboxFt);
            llvm::Value* objPtr = createCall(unboxFt, unboxFn.getCallee(), { val });
            result = builder->CreateLoad(expectedType, objPtr);
        }
    }

    if (!result && val->getType()->isIntegerTy() && expectedType->isIntegerTy()) {
        result = builder->CreateIntCast(val, expectedType, true);
    }
    
    if (!result && val->getType()->isIntegerTy() && expectedType->isDoubleTy()) {
        result = builder->CreateSIToFP(val, expectedType);
    }
    
    if (!result && val->getType()->isDoubleTy() && expectedType->isIntegerTy()) {
        result = builder->CreateFPToSI(val, expectedType);
    }

    if (!result && val->getType()->isPointerTy() && expectedType->isPointerTy()) {
        result = builder->CreateBitCast(val, expectedType);
    }

    if (!result && val->getType()->isIntegerTy() && expectedType->isPointerTy()) {
        result = builder->CreateIntToPtr(val, expectedType);
    }

    if (!result && val->getType()->isPointerTy() && expectedType->isIntegerTy()) {
        result = builder->CreatePtrToInt(val, expectedType);
    }
    
    if (!result) result = val;

    if (result && concreteTypes.count(val)) {
        concreteTypes[result] = concreteTypes[val];
    }

    return result;
}

llvm::Value* IRGenerator::emitToBoolean(llvm::Value* val, std::shared_ptr<Type> type) {
    if (!val) return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
    
    if (val->getType()->isIntegerTy(1)) return val;
    
    if (val->getType()->isDoubleTy()) {
        return builder->CreateFCmpONE(val, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "tobool");
    } else if (val->getType()->isIntegerTy(64)) {
        return builder->CreateICmpNE(val, llvm::ConstantInt::get(*context, llvm::APInt(64, 0)), "tobool");
    } else if (val->getType()->isPointerTy()) {
        // For Any type, use ts_value_to_bool for JavaScript truthiness semantics
        if (type && type->kind == TypeKind::Any) {
            llvm::FunctionType* toBoolFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
            llvm::FunctionCallee toBoolFn = getRuntimeFunction("ts_value_to_bool", toBoolFt);
            return createCall(toBoolFt, toBoolFn.getCallee(), { val });
        }
        // For Boolean type (e.g., callback returning boolean), unbox the TsValue* to i1
        if (type && type->kind == TypeKind::Boolean) {
            llvm::FunctionType* getBoolFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
            llvm::FunctionCallee getBoolFn = getRuntimeFunction("ts_value_get_bool", getBoolFt);
            return createCall(getBoolFt, getBoolFn.getCallee(), { val });
        }
        return builder->CreateICmpNE(val, llvm::ConstantPointerNull::get(builder->getPtrTy()), "tobool");
    }
    
    return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 1);
}

llvm::Value* IRGenerator::toInt32(llvm::Value* val) {
    if (val->getType()->isIntegerTy(32)) return val;
    if (val->getType()->isIntegerTy()) return builder->CreateIntCast(val, llvm::Type::getInt32Ty(*context), true);
    if (val->getType()->isDoubleTy()) {
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getDoubleTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_double_to_int32", ft);
        return createCall(ft, fn.getCallee(), { val });
    }
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0);
}

llvm::Value* IRGenerator::toUint32(llvm::Value* val) {
    if (val->getType()->isIntegerTy(32)) return val;
    if (val->getType()->isIntegerTy()) return builder->CreateIntCast(val, llvm::Type::getInt32Ty(*context), false);
    if (val->getType()->isDoubleTy()) {
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getDoubleTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_double_to_uint32", ft);
        return createCall(ft, fn.getCallee(), { val });
    }
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0);
}

void IRGenerator::visitExportAssignment(ast::ExportAssignment* node) {
    visit(node->expression.get());
}

} // namespace ts
