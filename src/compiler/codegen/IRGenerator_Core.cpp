#include "IRGenerator.h"
#include <fmt/core.h>
#include <filesystem>
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

void IRGenerator::generateGlobals(const Analyzer& analyzer) {
    const auto& globals = analyzer.getSymbolTable().getGlobalSymbols();
    SPDLOG_INFO("generateGlobals: processing {} global symbols", globals.size());
    for (auto& [name, symbol] : globals) {
        SPDLOG_INFO("  Global symbol: {} (type kind {})", name, (int)symbol->type->kind);
        if (module->getGlobalVariable(name)) continue;
        
        // Skip functions, they are handled by generatePrototypes or are runtime functions
        if (symbol->type->kind == TypeKind::Function) continue;
        
        // Skip runtime functions and other ts_ symbols
        if (name.find("ts_") == 0) continue;
        
        // Skip interfaces (they don't have runtime representation)
        if (symbol->type->kind == TypeKind::Interface) continue;

        llvm::Type* type = getLLVMType(symbol->type);
        new llvm::GlobalVariable(*module, type, false, llvm::GlobalValue::ExternalLinkage,
            llvm::Constant::getNullValue(type), name);
    }

    // Also process top-level variables from modules
    // Note: these are variable declarations (const x = ...), not function declarations
    SPDLOG_INFO("generateGlobals: processing {} top-level variables", analyzer.topLevelVariables.size());
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
            SPDLOG_INFO("  Top-level function variable: {} -> creating pointer global", symbol->name);
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
    llvm::FunctionCallee failFn = module->getOrInsertFunction("ts_cfi_fail", failFt);
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
    llvm::FunctionCallee panicFn = module->getOrInsertFunction("ts_panic", panicFt);
    
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
    llvm::FunctionCallee panicFn = module->getOrInsertFunction("ts_panic", panicFt);
    
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
            // Box the value first
            llvm::Value* boxedVal = boxValue(value, type);
            
            if (!namedValues.count(id->name)) {
                // First time: create cell and store pointer in alloca
                llvm::FunctionType* cellCreateFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee cellCreateFn = module->getOrInsertFunction("ts_cell_create", cellCreateFt);
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
                llvm::FunctionCallee cellSetFn = module->getOrInsertFunction("ts_cell_set", cellSetFt);
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
                    llvm::FunctionCallee createArrayFn = module->getOrInsertFunction("ts_array_create", createArrayFt);
                    llvm::Value* excludedArray = createCall(createArrayFt, createArrayFn.getCallee(), {});

                    llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push", pushFt);

                    llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);

                    for (const auto& key : excludedKeys) {
                        llvm::Value* keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(key) });
                        llvm::Value* boxedKey = boxValue(keyStr, std::make_shared<Type>(TypeKind::String));
                        createCall(pushFt, pushFn.getCallee(), { excludedArray, boxedKey });
                    }

                    llvm::FunctionType* copyExcludingFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee copyExcludingFn = module->getOrInsertFunction("ts_map_copy_excluding_v2", copyExcludingFt);
                    
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
                llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
                llvm::Value* propName = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(fieldName) });
                // propName = boxValue(propName, std::make_shared<Type>(TypeKind::String));
                
                llvm::FunctionType* getPropFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee getPropFn = module->getOrInsertFunction("ts_value_get_property", getPropFt);
                
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
             llvm::FunctionCallee getObjFn = module->getOrInsertFunction("ts_value_get_object", getObjFt);
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
                llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length", lenFt);
                llvm::Value* lenVal = createCall(lenFt, lenFn.getCallee(), { arrayPtr });

                llvm::FunctionType* sliceFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                    { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee sliceFn = module->getOrInsertFunction("ts_array_slice", sliceFt);
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
    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_value_make_undefined", ft);
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
        // but they're in a new scope, so we don't add their parameters
        std::set<std::string> nestedScope = localScope;
        for (auto& param : node->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                nestedScope.insert(id->name);
            }
        }
        FreeVariableCollector nestedCollector(nestedScope);
        if (node->body) {
            node->body->accept(&nestedCollector);
        }
        // Merge referenced names (excluding nested locals)
        for (const auto& name : nestedCollector.referencedNames) {
            if (!localScope.count(name)) {
                referencedNames.insert(name);
            }
        }
    }
    
    void visitFunctionExpression(ast::FunctionExpression* node) override {
        // Similar to arrow function
        std::set<std::string> nestedScope = localScope;
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

llvm::Value* IRGenerator::boxValue(llvm::Value* val, std::shared_ptr<Type> type) {
    if (!val) {
        if (type && type->kind == TypeKind::Void) {
            return getUndefinedValue();
        }
        SPDLOG_ERROR("boxValue: val is NULL! type={}", type ? (int)type->kind : -1);
        // Print stack trace or more info if possible
        return getUndefinedValue();
    }
    if (boxedValues.count(val)) {
        return val;
    }

    SPDLOG_INFO("boxValue: val={}, type={}, llvm_type={}", (void*)val, type ? (int)type->kind : -1, (int)val->getType()->getTypeID());
    if (auto func = llvm::dyn_cast<llvm::Function>(val)) {
        SPDLOG_INFO("boxValue: val is function {}", func->getName().str());
    }

    llvm::Type* valType = val->getType();
    std::string funcName;

    if (valType->isIntegerTy(64)) funcName = "ts_value_make_int";
    else if (valType->isDoubleTy()) funcName = "ts_value_make_double";
    else if (valType->isIntegerTy(1)) funcName = "ts_value_make_bool";
    else if (valType->isVoidTy() || valType->isIntegerTy(8)) {
        llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", undefFt);
        llvm::Value* res = createCall(undefFt, undefFn.getCallee(), {});
        boxedValues.insert(res);
        return res;
    }
    else if ((type && type->kind == TypeKind::Function) || llvm::isa<llvm::Function>(val)) {
        SPDLOG_DEBUG("boxValue: boxing function val={}", (void*)val);
        llvm::FunctionType* fnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_value_make_function", fnFt);
        
        llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
        llvm::Value* currentContext = nullptr;
        if (currentFunc->arg_size() > 0 && currentFunc->getArg(0)->getType()->isPointerTy()) {
            currentContext = currentFunc->getArg(0);
        } else {
            currentContext = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::Value* res = createCall(fnFt, fn.getCallee(), { builder->CreateBitCast(val, builder->getPtrTy()), currentContext });
        boxedValues.insert(res);
        return res;
    } else if (valType->isPointerTy()) {
        if (type && type->kind == TypeKind::String) funcName = "ts_value_make_string";
        else if (type && (type->kind == TypeKind::Array || type->kind == TypeKind::Tuple)) funcName = "ts_value_make_array";
        else if (type && type->kind == TypeKind::BigInt) funcName = "ts_value_make_bigint";
        else if (type && type->kind == TypeKind::Symbol) funcName = "ts_value_make_symbol";
        else if (type && (type->kind == TypeKind::Class || type->kind == TypeKind::Interface)) {
            std::string name;
            if (type->kind == TypeKind::Class) name = std::static_pointer_cast<ClassType>(type)->name;
            else name = std::static_pointer_cast<InterfaceType>(type)->name;
            
            if (name.find("Promise") == 0) {
                funcName = "ts_value_make_promise";
            } else {
                funcName = "ts_value_make_object";
            }
        }
        else if (type && (type->kind == TypeKind::Object || type->kind == TypeKind::Intersection || type->kind == TypeKind::Map || type->kind == TypeKind::SetType)) {
            funcName = "ts_value_make_object";
        }
    }
    
    if (funcName.empty()) {
        if (type && type->kind == TypeKind::Function) {
            SPDLOG_INFO("boxValue: boxing function");
            funcName = "ts_value_make_function";
        }
        else if (type && type->kind == TypeKind::Any && !val->getType()->isPointerTy()) {
             // Fallback for Any that isn't a pointer yet
             if (val->getType()->isIntegerTy(64)) funcName = "ts_value_make_int";
             else if (val->getType()->isDoubleTy()) funcName = "ts_value_make_double";
             else if (val->getType()->isIntegerTy(1)) funcName = "ts_value_make_bool";
        }
    }

    if (funcName.empty()) return val;

    if (type && type->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(type);
        if (classType->isStruct) {
            // Allocate on heap
            llvm::FunctionType* allocFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee allocFn = module->getOrInsertFunction("ts_alloc", allocFt);
            
            llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, classType->name);
            uint64_t size = module->getDataLayout().getTypeAllocSize(classStruct);
            llvm::Value* heapPtr = createCall(allocFt, allocFn.getCallee(), { builder->getInt64(size) });
            
            builder->CreateStore(val, heapPtr);
            
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee boxFn = module->getOrInsertFunction("ts_value_make_object", boxFt);
            llvm::Value* res = createCall(boxFt, boxFn.getCallee(), { heapPtr });
            boxedValues.insert(res);
            return res;
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
    SPDLOG_DEBUG("boxValue: added {} to boxedValues", (void*)res);
    return res;
}

llvm::Value* IRGenerator::unboxValue(llvm::Value* val, std::shared_ptr<Type> type) {
    if (!type || !val) return val;
    
    // If it's not a pointer, it's already unboxed (primitive)
    if (!val->getType()->isPointerTy()) return val;

    // If it's a pointer but not in boxedValues, it's already unboxed (raw pointer)
    if (!boxedValues.count(val)) return val;

    if (type->kind == TypeKind::Int) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Double) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_double", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Boolean) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_bool", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::String) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_string", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(type);
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_object", unboxFt);
        if (classType->isStruct) {
            llvm::Value* objPtr = createCall(unboxFt, unboxFn.getCallee(), { val });
            
            llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, classType->name);
            return builder->CreateLoad(classStruct, objPtr);
        }
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Object || type->kind == TypeKind::Intersection || 
               type->kind == TypeKind::Array || type->kind == TypeKind::Map || type->kind == TypeKind::SetType ||
               type->kind == TypeKind::Tuple || type->kind == TypeKind::BigInt || type->kind == TypeKind::Symbol) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_object", unboxFt);
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
void IRGenerator::visitFunctionDeclaration(ast::FunctionDeclaration* node) {}
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
    SPDLOG_INFO("createCall: {} with {} args, returnType={}", name, args.size(), (int)ft->getReturnType()->getTypeID());

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
        // If it returns a pointer, check if it's a TsValue*
        // For now, assume any function returning ptr that isn't ts_map_create, etc. returns a boxed value
        // Actually, let's be more specific: if it returns TsValue* in the runtime, it's boxed.
        // Our runtime functions that return raw pointers are:
        // ts_map_create, ts_string_create, ts_array_create, ts_alloc, etc.
        std::string name;
        llvm::Value* actualCallee = callee->stripPointerCasts();
        if (auto func = llvm::dyn_cast<llvm::Function>(actualCallee)) name = func->getName().str();
        
        if (!name.empty() && name.find("ts_value_make_") == 0) {
            boxedValues.insert(res);
        } else if (!name.empty() && (name == "ts_value_get_int" || name == "ts_value_get_double" || name == "ts_value_get_bool" || name == "ts_value_get_string" || name == "ts_value_get_object" ||
                                   name == "ts_map_create" || name == "ts_set_create" || name == "ts_string_create" || name == "ts_array_create" || name == "ts_array_create_specialized" ||
                                   name == "ts_array_get_elements_ptr" || name == "ts_alloc" ||
                                   name == "ts_bigint_create_str" || name == "ts_bigint_create_int" || name == "ts_bigint_from_value" ||
                                   name == "ts_symbol_create" || name == "ts_symbol_for" || name == "ts_symbol_key_for" ||
                                   name == "ts_path_format" || name == "ts_path_to_namespaced_path" ||
                                   name == "ts_path_get_sep" || name == "ts_path_get_delimiter" ||
                                   name == "ts_http_request" || name == "ts_http_get" || name == "ts_http_create_server" ||
                                   name == "ts_https_request" || name == "ts_https_get" || name == "ts_https_create_server" ||
                                   name == "ts_writable_write" || name == "ts_writable_end" ||
                                   (name.find("ts_fs_") == 0 && name != "ts_fs_watch" && name.find("_async") == std::string::npos))) {
            // Raw pointers - don't add to boxedValues
        } else if (!name.empty() && name.find("ts_") == 0 && ft->getReturnType()->isPointerTy()) {
            // Other runtime functions (ts_*) that return pointers are assumed to return boxed TsValue*
            // User-defined functions are NOT assumed to return boxed values
            boxedValues.insert(res);
        }
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
            llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", undefFt);
            result = builder->CreateCall(undefFt, undefFn.getCallee());
        } else if (val->getType()->isStructTy()) {
            // Box struct
            llvm::FunctionType* allocFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee allocFn = module->getOrInsertFunction("ts_alloc", allocFt);
            
            uint64_t size = module->getDataLayout().getTypeAllocSize(val->getType());
            llvm::Value* heapPtr = createCall(allocFt, allocFn.getCallee(), { builder->getInt64(size) });
            builder->CreateStore(val, heapPtr);
            
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee boxFn = module->getOrInsertFunction("ts_value_make_object", boxFt);
            result = createCall(boxFt, boxFn.getCallee(), { heapPtr });
        } else if (val->getType()->isIntegerTy(1)) {
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt1Ty() }, false);
            llvm::FunctionCallee boxFn = module->getOrInsertFunction("ts_value_make_bool", boxFt);
            result = builder->CreateCall(boxFt, boxFn.getCallee(), { val });
        } else if (val->getType()->isIntegerTy(8)) {
            // Void/bool8 -> undefined
            llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", undefFt);
            result = builder->CreateCall(undefFt, undefFn.getCallee());
        } else if (val->getType()->isIntegerTy()) {
            llvm::Value* i64Val = builder->CreateIntCast(val, builder->getInt64Ty(), true);
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
            llvm::FunctionCallee boxFn = module->getOrInsertFunction("ts_value_make_int", boxFt);
            result = builder->CreateCall(boxFt, boxFn.getCallee(), { i64Val });
        } else if (val->getType()->isDoubleTy()) {
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getDoubleTy() }, false);
            llvm::FunctionCallee boxFn = module->getOrInsertFunction("ts_value_make_double", boxFt);
            result = builder->CreateCall(boxFt, boxFn.getCallee(), { val });
        }
    }

    // Unboxing: ptr -> primitive
    if (!result && val->getType()->isPointerTy() && !expectedType->isPointerTy()) {
        if (expectedType->isIntegerTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int", unboxFt);
            llvm::Value* i64Val = builder->CreateCall(unboxFt, unboxFn.getCallee(), { val });
            result = builder->CreateIntCast(i64Val, expectedType, true);
        } else if (expectedType->isDoubleTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getDoubleTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_double", unboxFt);
            result = builder->CreateCall(unboxFt, unboxFn.getCallee(), { val });
        } else if (expectedType->isIntegerTy(1)) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_bool", unboxFt);
            result = builder->CreateCall(unboxFt, unboxFn.getCallee(), { val });
        } else if (expectedType->isStructTy()) {
            // Unbox struct from TsValue*
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_object", unboxFt);
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
            llvm::FunctionCallee toBoolFn = module->getOrInsertFunction("ts_value_to_bool", toBoolFt);
            return createCall(toBoolFt, toBoolFn.getCallee(), { val });
        }
        // For Boolean type (e.g., callback returning boolean), unbox the TsValue* to i1
        if (type && type->kind == TypeKind::Boolean) {
            llvm::FunctionType* getBoolFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
            llvm::FunctionCallee getBoolFn = module->getOrInsertFunction("ts_value_get_bool", getBoolFt);
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
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_double_to_int32", ft);
        return createCall(ft, fn.getCallee(), { val });
    }
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0);
}

llvm::Value* IRGenerator::toUint32(llvm::Value* val) {
    if (val->getType()->isIntegerTy(32)) return val;
    if (val->getType()->isIntegerTy()) return builder->CreateIntCast(val, llvm::Type::getInt32Ty(*context), false);
    if (val->getType()->isDoubleTy()) {
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getDoubleTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_double_to_uint32", ft);
        return createCall(ft, fn.getCallee(), { val });
    }
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0);
}

void IRGenerator::visitExportAssignment(ast::ExportAssignment* node) {
    visit(node->expression.get());
}

} // namespace ts
