#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>

namespace ts {
using namespace ast;

// Forward declaration of helper for StringDecoder boxing registration
static bool stringDecoderBoxingRegistered = false;
static void ensureStringDecoderBoxingRegistered(BoxingPolicy& bp) {
    if (stringDecoderBoxingRegistered) return;
    stringDecoderBoxingRegistered = true;
    bp.registerRuntimeApi("ts_string_decoder_create", {true}, true);
    bp.registerRuntimeApi("ts_string_decoder_write", {true, true}, true);
    bp.registerRuntimeApi("ts_string_decoder_end", {true, true}, true);
    bp.registerRuntimeApi("ts_string_decoder_get_encoding", {true}, true);
}

// Forward declaration of helper for boxed primitives boxing registration
static bool boxedPrimitivesBoxingRegistered = false;
static void ensureBoxedPrimitivesBoxingRegistered(BoxingPolicy& bp) {
    if (boxedPrimitivesBoxingRegistered) return;
    boxedPrimitivesBoxingRegistered = true;
    // new Boolean(value), new Number(value), new String(value)
    // These take raw values (bool/i8, double, ptr), not boxed TsValue*
    bp.registerRuntimeApi("ts_boolean_object_create", {false}, true);  // i8 -> ptr
    bp.registerRuntimeApi("ts_number_object_create", {false}, true);   // double -> ptr
    bp.registerRuntimeApi("ts_string_object_create", {false}, true);   // ptr -> ptr
}

// Forward declaration of helper for inspector session boxing registration
static bool inspectorSessionBoxingRegistered = false;
static void ensureInspectorSessionBoxingRegistered(BoxingPolicy& bp) {
    if (inspectorSessionBoxingRegistered) return;
    inspectorSessionBoxingRegistered = true;
    // inspector.Session methods - all stubs since no V8
    bp.registerRuntimeApi("ts_inspector_session_create", {}, false);
    bp.registerRuntimeApi("ts_inspector_session_connect", {true}, false);
    bp.registerRuntimeApi("ts_inspector_session_connect_to_main_thread", {true}, false);
    bp.registerRuntimeApi("ts_inspector_session_disconnect", {true}, false);
    bp.registerRuntimeApi("ts_inspector_session_post", {true, true, true, true}, false);
}

// Helper to get class members from either ClassDeclaration or ClassExpression
static const std::vector<NodePtr>* getClassMembers(std::shared_ptr<ClassType> classType) {
    if (classType->node) {
        return &classType->node->members;
    } else if (classType->exprNode) {
        return &classType->exprNode->members;
    }
    return nullptr;
}

static bool hasClassMembers(std::shared_ptr<ClassType> classType) {
    return classType->node != nullptr || classType->exprNode != nullptr;
}

// Helper to check if a class has class-level decorators
static bool hasClassDecorators(std::shared_ptr<ClassType> classType) {
    if (classType->node && !classType->node->decorators.empty()) {
        return true;
    }
    if (classType->exprNode && !classType->exprNode->decorators.empty()) {
        return true;
    }
    return false;
}

// Helper to check if any class methods have decorators
static bool hasMethodDecorators(std::shared_ptr<ClassType> classType) {
    if (classType->node) {
        for (const auto& member : classType->node->members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                if (!method->decorators.empty()) {
                    return true;
                }
            }
        }
    }
    if (classType->exprNode) {
        for (const auto& member : classType->exprNode->members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                if (!method->decorators.empty()) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Helper to check if any class properties have decorators
static bool hasPropertyDecorators(std::shared_ptr<ClassType> classType) {
    if (classType->node) {
        for (const auto& member : classType->node->members) {
            if (auto* prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                if (!prop->decorators.empty()) {
                    return true;
                }
            }
        }
    }
    if (classType->exprNode) {
        for (const auto& member : classType->exprNode->members) {
            if (auto* prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                if (!prop->decorators.empty()) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Helper to check if any method parameters have decorators
static bool hasParameterDecorators(std::shared_ptr<ClassType> classType) {
    if (classType->node) {
        for (const auto& member : classType->node->members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                for (const auto& param : method->parameters) {
                    if (!param->decorators.empty()) {
                        return true;
                    }
                }
            }
        }
    }
    if (classType->exprNode) {
        for (const auto& member : classType->exprNode->members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                for (const auto& param : method->parameters) {
                    if (!param->decorators.empty()) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Helper to get decorators from a class
static const std::vector<ast::Decorator>* getClassDecorators(std::shared_ptr<ClassType> classType) {
    if (classType->node) {
        return &classType->node->decorators;
    }
    if (classType->exprNode) {
        return &classType->exprNode->decorators;
    }
    return nullptr;
}

void IRGenerator::generateClasses(const Analyzer& analyzer, const std::vector<Specialization>& specializations) {
    std::vector<std::shared_ptr<ClassType>> allClassTypes;
    
    // Add global classes
    for (const auto& [name, type] : analyzer.getSymbolTable().getGlobalTypes()) {
        if (name == "Date" || name == "RegExp" || name == "Promise" || name == "Map" || name == "Error" ||
            name == "EventEmitter" || name == "Stream" || name == "Readable" || name == "Writable" ||
            name == "Duplex" || name == "Transform" || name == "ReadStream" || name == "WriteStream" ||
            name == "Buffer" || name == "Socket" || name == "Server" || name == "IncomingMessage" ||
            name == "ServerResponse" || name == "ClientRequest" || name == "TextEncoder" ||
            name == "TextDecoder" || name == "OutgoingMessage" || name == "CloseEvent" ||
            name == "MessageEvent" || name == "Agent" || name == "HttpsAgent" || name == "WebSocket" ||
            // Generators - handled by runtime
            name == "Generator" || name == "AsyncGenerator" ||
            // TypedArrays - handled by runtime
            name == "Int8Array" || name == "Uint8Array" || name == "Uint8ClampedArray" ||
            name == "Int16Array" || name == "Uint16Array" || name == "Int32Array" || name == "Uint32Array" ||
            name == "Float32Array" || name == "Float64Array" || name == "BigInt64Array" || name == "BigUint64Array" ||
            name == "DataView" ||
            // WeakMap/WeakSet - handled by runtime (no VTable needed)
            name == "WeakMap" || name == "WeakSet" || name == "Set" ||
            // StringDecoder - handled by runtime
            name == "StringDecoder" ||
            // ChildProcess/Worker - handled by runtime (no VTable needed)
            name == "ChildProcess" || name == "Worker" ||
            // Readline - handled by runtime
            name == "Interface" || name == "ReadlineInterface") continue;
        if (type->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(type);
            if (classType->typeParameters.empty()) {
                allClassTypes.push_back(classType);
            }
        }
    }

    // Add classes from all modules
    for (auto& [path, module] : analyzer.modules) {
        if (!module->ast) continue;
        for (auto& stmt : module->ast->body) {
            if (auto cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
                auto type = analyzer.getSymbolTable().lookupType(cls->name);
                if (type && type->kind == TypeKind::Class) {
                    auto classType = std::static_pointer_cast<ClassType>(type);
                    if (classType->typeParameters.empty()) {
                        bool found = false;
                        for (const auto& existing : allClassTypes) {
                            if (existing->name == classType->name) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) allClassTypes.push_back(classType);
                    }
                }
            }
        }
    }
    
    // Add class expressions from all analyzed expressions
    for (const auto& expr : analyzer.expressions) {
        if (auto classExpr = dynamic_cast<ast::ClassExpression*>(expr)) {
            if (classExpr->inferredType && classExpr->inferredType->kind == TypeKind::Class) {
                auto classType = std::static_pointer_cast<ClassType>(classExpr->inferredType);
                if (classType->typeParameters.empty()) {
                    bool found = false;
                    for (const auto& existing : allClassTypes) {
                        if (existing->name == classType->name) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        SPDLOG_DEBUG("Found class expression: {}", classType->name);
                        allClassTypes.push_back(classType);
                    }
                }
            }
        }
    }

    // Add specialized classes
    for (const auto& spec : specializations) {
        if (spec.classType && spec.classType->kind == TypeKind::Class) {
            auto specClass = std::static_pointer_cast<ClassType>(spec.classType);
            
            // Skip generic templates
            if (!specClass->typeParameters.empty()) continue;

            bool found = false;
            for (const auto& existing : allClassTypes) {
                if (existing->name == specClass->name) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                allClassTypes.push_back(specClass);
            }
        }
    }

    // 1. First pass: Create opaque structs for all classes to handle circular references
    for (const auto& classType : allClassTypes) {
        llvm::StructType::create(*context, classType->name);
    }

    // 2. Second pass: Compute layouts (recursive)
    for (const auto& classType : allClassTypes) {
        SPDLOG_DEBUG("Class {} isStruct={}", classType->name, classType->isStruct);
        std::function<void(std::shared_ptr<ClassType>)> compute = [&](std::shared_ptr<ClassType> c) {
            if (classLayouts.count(c->name)) return;
            if (c->baseClass) compute(c->baseClass);
            
            ClassLayout layout;
            if (c->baseClass) layout = classLayouts[c->baseClass->name];
            else {
                layout.methodIndices["__get_property"] = 0;
                layout.allMethods.push_back({"__get_property", nullptr});
            }
            
            for (const auto& [fname, ftype] : c->fields) {
                int offset = c->isStruct ? 0 : 1;
                layout.fieldIndices[fname] = (int)layout.allFields.size() + offset; // +1 for vptr if not struct
                layout.allFields.push_back({fname, ftype});
            }

            // Add __properties__ field for classes with index signatures
            if (c->indexSignatureValueType) {
                int offset = c->isStruct ? 0 : 1;
                layout.fieldIndices["__properties__"] = (int)layout.allFields.size() + offset;
                layout.allFields.push_back({"__properties__", std::make_shared<Type>(TypeKind::Any)});  // TsMap*
                SPDLOG_DEBUG("Class {} has index signature, added __properties__ field at index {}",
                    c->name, layout.fieldIndices["__properties__"]);
            }
            for (const auto& [mname, mtype] : c->methods) {
                if (layout.methodIndices.count(mname)) {
                    layout.allMethods[layout.methodIndices[mname]] = {mname, mtype};
                } else {
                    layout.methodIndices[mname] = (int)layout.allMethods.size();
                    layout.allMethods.push_back({mname, mtype});
                }
            }
            for (const auto& [mname, mtype] : c->getters) {
                std::string vname = "get_" + mname;
                if (layout.methodIndices.count(vname)) {
                    layout.allMethods[layout.methodIndices[vname]] = {vname, mtype};
                } else {
                    layout.methodIndices[vname] = (int)layout.allMethods.size();
                    layout.allMethods.push_back({vname, mtype});
                }
            }
            for (const auto& [mname, mtype] : c->setters) {
                std::string vname = "set_" + mname;
                if (layout.methodIndices.count(vname)) {
                    layout.allMethods[layout.methodIndices[vname]] = {vname, mtype};
                } else {
                    layout.methodIndices[vname] = (int)layout.allMethods.size();
                    layout.allMethods.push_back({vname, mtype});
                }
            }
            classLayouts[c->name] = layout;
        };
        compute(classType);
    }

    // 3. Third pass: Create VTables
    for (const auto& classType : allClassTypes) {
        std::string name = classType->name;
        if (name == "Date" || name == "RegExp" || name == "Error" || name.find("Promise_") == 0 || name == "Promise" || name == "Map" || name == "Set" || name == "TextEncoder" || name == "TextDecoder" || name == "AsyncLocalStorage" || name == "AsyncResource" || name == "AsyncHook" || name == "ChildProcess" || name == "Worker" || name == "UDPSocket" || name == "InspectorSession" || name == "Interface" || name == "ReadlineInterface" || name == "TTYReadStream" || name == "TTYWriteStream") continue;
        
        auto& layout = classLayouts[name];
        llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, name);

        if (classType->isStruct) {
            // Define Class Body: { Fields... }
            std::vector<llvm::Type*> fieldTypes;
            for (const auto& [fieldName, fieldType] : layout.allFields) {
                fieldTypes.push_back(getLLVMType(fieldType));
            }
            classStruct->setBody(fieldTypes);
            continue;
        }

        // Define VTable Type
        std::string vtableName = name + "_VTable";
        llvm::StructType* vtableStruct = llvm::StructType::create(*context, vtableName);

        // Define Class Body: { VTable*, Fields... }
        std::vector<llvm::Type*> fieldTypes;
        if (!classType->isStruct) {
            fieldTypes.push_back(llvm::PointerType::getUnqual(vtableStruct)); // vptr
        }

        for (const auto& [fieldName, fieldType] : layout.allFields) {
            fieldTypes.push_back(getLLVMType(fieldType));
        }
        classStruct->setBody(fieldTypes);

        // Generate __get_property implementation
        std::string getPropName = name + "_get_property";
        llvm::FunctionType* getPropFt = llvm::FunctionType::get(
            builder->getPtrTy(), 
            { builder->getPtrTy(), builder->getPtrTy() }, 
            false
        );
        llvm::Function* getPropFunc = llvm::Function::Create(getPropFt, llvm::Function::ExternalLinkage, getPropName, module.get());
        addStackProtection(getPropFunc);

        {
            llvm::BasicBlock* oldBB = builder->GetInsertBlock();
            llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", getPropFunc);
            builder->SetInsertPoint(entryBB);

            llvm::Value* objArg = getPropFunc->getArg(0);
            llvm::Value* keyArg = getPropFunc->getArg(1);
            llvm::Value* typedObj = builder->CreateBitCast(objArg, llvm::PointerType::getUnqual(classStruct));

            llvm::FunctionCallee strCreateFn = getRuntimeFunction("ts_string_create", 
                llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
            llvm::FunctionCallee strEqFn = getRuntimeFunction("ts_string_eq", 
                llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
            llvm::FunctionCallee makeUndefinedFn = getRuntimeFunction("ts_value_make_undefined", 
                llvm::FunctionType::get(builder->getPtrTy(), {}, false));

            for (const auto& [fname, ftype] : layout.allFields) {
                llvm::Value* fieldNameStr = builder->CreateGlobalStringPtr(fname);
                llvm::Value* tsStringKey = builder->CreateCall(strCreateFn, { fieldNameStr });
                llvm::Value* isEqual = builder->CreateCall(strEqFn, { keyArg, tsStringKey });

                llvm::BasicBlock* matchBB = llvm::BasicBlock::Create(*context, "match_" + fname, getPropFunc);
                llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(*context, "next_" + fname, getPropFunc);

                builder->CreateCondBr(isEqual, matchBB, nextBB);
                builder->SetInsertPoint(matchBB);

                int fieldIdx = layout.fieldIndices[fname];
                llvm::Value* fieldPtr = builder->CreateStructGEP(classStruct, typedObj, fieldIdx);
                llvm::Value* fieldVal = builder->CreateLoad(getLLVMType(ftype), fieldPtr);
                llvm::Value* boxedVal = boxValue(fieldVal, ftype);
                builder->CreateRet(boxedVal);

                builder->SetInsertPoint(nextBB);
            }
            builder->CreateRet(builder->CreateCall(makeUndefinedFn, {}));
            
            if (oldBB) builder->SetInsertPoint(oldBB);
        }

        // Define VTable Body: { ParentVTable*, Function Pointers... }
        std::vector<llvm::Type*> vtableFieldTypes;
        std::vector<llvm::Constant*> vtableFuncs;

        vtableFieldTypes.push_back(llvm::PointerType::getUnqual(*context)); // Parent VTable
        if (classType->baseClass) {
            std::string baseVTableGlobalName = classType->baseClass->name + "_VTable_Global";
            // We don't know the type yet, so use ptr
            llvm::Constant* baseVTable = module->getOrInsertGlobal(baseVTableGlobalName, llvm::PointerType::getUnqual(*context));
            vtableFuncs.push_back(baseVTable);
        } else {
            vtableFuncs.push_back(llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context)));
        }

        for (const auto& [methodName, methodType] : layout.allMethods) {
            // Method signature: (context: ptr, this: ptr, args...) -> ret
            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(builder->getPtrTy()); // context
            paramTypes.push_back(builder->getPtrTy()); // this (opaque ptr)
            
            // Find which class actually defines this method
            std::shared_ptr<ClassType> definer = classType;
            bool isAbstract = false;
            std::string mangledName;
            std::shared_ptr<FunctionType> actualMethodType = methodType;

            if (methodName == "__get_property") {
                vtableFieldTypes.push_back(builder->getPtrTy());
                vtableFuncs.push_back(getPropFunc);
                continue;
            }

            while (definer) {
                if (methodName.substr(0, 4) == "get_") {
                    std::string propName = methodName.substr(4);
                    if (definer->getters.count(propName)) {
                        mangledName = definer->name + "_get_" + propName;
                        actualMethodType = definer->getters[propName];
                        if (definer->abstractMethods.count(propName)) isAbstract = true;
                        break;
                    }
                } else if (methodName.substr(0, 4) == "set_") {
                    std::string propName = methodName.substr(4);
                    if (definer->setters.count(propName)) {
                        mangledName = definer->name + "_set_" + propName;
                        actualMethodType = definer->setters[propName];
                        if (definer->abstractMethods.count(propName)) isAbstract = true;
                        break;
                    }
                } else if (definer->methods.count(methodName)) {
                    mangledName = definer->name + "_" + methodName;
                    actualMethodType = definer->methods[methodName];
                    if (definer->abstractMethods.count(methodName)) isAbstract = true;
                    break;
                }
                definer = definer->baseClass;
            }

            for (const auto& param : actualMethodType->paramTypes) {
                paramTypes.push_back(getLLVMType(param));
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(
                getLLVMType(actualMethodType->returnType), paramTypes, false);
            
            vtableFieldTypes.push_back(builder->getPtrTy());

            if (isAbstract) {
                vtableFuncs.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
            } else {
                llvm::Function* methodFunc = module->getFunction(mangledName);
                if (!methodFunc) {
                    methodFunc = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, mangledName, module.get());
                    addStackProtection(methodFunc);
                }
                vtableFuncs.push_back(methodFunc);
            }
        }

        vtableStruct->setBody(vtableFieldTypes);

        // Create VTable Global
        std::string vtableGlobalName = name + "_VTable_Global";
        llvm::Constant* vtableInit = llvm::ConstantStruct::get(vtableStruct, vtableFuncs);
        auto* vtableGV = new llvm::GlobalVariable(*module, vtableStruct, true, llvm::GlobalValue::ExternalLinkage, vtableInit, vtableGlobalName);

        // Add CFI metadata
        auto addTypeMetadata = [&](std::shared_ptr<ClassType> type) {
            vtableGV->addMetadata(llvm::LLVMContext::MD_type,
                *llvm::MDNode::get(*context, {
                    llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0)),
                    llvm::MDString::get(*context, type->name)
                }));
        };

        auto current = classType;
        while (current) {
            addTypeMetadata(current);
            current = current->baseClass;
        }
    }

    // 4. Fourth pass: Generate static fields and methods
    SPDLOG_DEBUG("Fourth pass: Generate static fields and methods");
    for (const auto& classType : allClassTypes) {
        std::string name = classType->name;
        SPDLOG_DEBUG("  Processing class: {}", name);
        for (const auto& [fname, ftype] : classType->staticFields) {
            SPDLOG_DEBUG("    Field: {} (type: {})", fname, ftype->toString());
            std::string globalName = name + "_" + fname;
            llvm::Type* llvmType = getLLVMType(ftype);
            
            llvm::Constant* initVal = llvm::Constant::getNullValue(llvmType);
            
            // Check if this is a comptime field
            if (classType->staticComptimeFields.count(fname)) {
                SPDLOG_DEBUG("      Comptime field detected");
                // Find the initializer
                auto* members = getClassMembers(classType);
                if (members) {
                    for (const auto& member : *members) {
                        if (auto prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                            std::string baseName = classType->originalName.empty() ? classType->name : classType->originalName;
                            std::string mangledName = manglePrivateName(prop->name, baseName);
                            if (mangledName == fname && prop->initializer) {
                                SPDLOG_DEBUG("      Found initializer for {}", fname);
                                // Simple evaluation for now: if it's a literal, use it.
                                if (auto num = dynamic_cast<ast::NumericLiteral*>(prop->initializer.get())) {
                                    if (llvmType->isIntegerTy()) {
                                        initVal = llvm::ConstantInt::get(llvmType, (uint64_t)num->value);
                                    } else if (llvmType->isDoubleTy() || llvmType->isFloatTy()) {
                                        initVal = llvm::ConstantFP::get(llvmType, num->value);
                                    } else {
                                        SPDLOG_DEBUG("      Cannot use numeric literal for non-numeric type");
                                    }
                                } else if (auto str = dynamic_cast<ast::StringLiteral*>(prop->initializer.get())) {
                                    if (ftype->kind == TypeKind::String) {
                                        // We can't easily create a TsString constant here because it requires a constructor call.
                                        // But we can store the raw string and let the static initializer handle it?
                                        // No, the point of comptime is to avoid runtime init.
                                        // For now, we'll just support numeric literals for constant globals.
                                        SPDLOG_DEBUG("      String literals not yet supported for constant globals");
                                    }
                                }
                            }
                        }
                    }
                }
            }

            new llvm::GlobalVariable(*module, llvmType, false, llvm::GlobalValue::ExternalLinkage, initVal, globalName);
        }
    }

    // 4.5 Fourth and a half pass: Generate instance field initializers
    for (const auto& classType : allClassTypes) {
        if (classType->isStruct) continue;

        bool hasInitializers = false;
        auto* members = getClassMembers(classType);
        if (members) {
            for (const auto& member : *members) {
                if (auto prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                    if (!prop->isStatic && prop->initializer) {
                        hasInitializers = true;
                        break;
                    }
                }
            }
        }
        if (!hasInitializers) continue;

        std::string initName = classType->name + "_init_fields";
        SPDLOG_DEBUG("Generating instance field initializer: {}", initName);
        llvm::FunctionType* initFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::Function* initFunc = llvm::Function::Create(initFt, llvm::Function::ExternalLinkage, initName, module.get());
        addStackProtection(initFunc);

        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", initFunc);
        llvm::BasicBlock* oldBB = builder->GetInsertBlock();
        builder->SetInsertPoint(entryBB);

        currentAsyncContext = initFunc->getArg(0);
        llvm::Value* thisPtr = initFunc->getArg(1);
        currentClass = classType;

        llvm::StructType* structType = llvm::StructType::getTypeByName(*context, classType->name);
        llvm::Value* typedThis = builder->CreateBitCast(thisPtr, llvm::PointerType::getUnqual(structType));

        auto& layout = classLayouts[classType->name];

        for (const auto& member : *members) {
            if (auto prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                if (!prop->isStatic && prop->initializer) {
                    visit(prop->initializer.get());
                    std::string baseName = classType->originalName.empty() ? classType->name : classType->originalName;
                    std::string mangledName = manglePrivateName(prop->name, baseName);
                    int fieldIdx = layout.fieldIndices[mangledName];
                    llvm::Value* fieldPtr = builder->CreateStructGEP(structType, typedThis, fieldIdx);
                    
                    // Ensure value matches field type
                    std::shared_ptr<Type> fieldType;
                    for (const auto& f : layout.allFields) {
                        if (f.first == mangledName) {
                            fieldType = f.second;
                            break;
                        }
                    }
                    if (fieldType) {
                        lastValue = castValue(lastValue, getLLVMType(fieldType));
                    }
                    
                    builder->CreateStore(lastValue, fieldPtr);
                }
            }
        }

        builder->CreateRetVoid();
        if (oldBB) builder->SetInsertPoint(oldBB);
        
        currentAsyncContext = nullptr;
        currentClass = nullptr;
    }

    // 5. Fifth pass: Generate static initializers
    for (const auto& classType : allClassTypes) {
        bool hasClassDecors = hasClassDecorators(classType);
        bool hasMethodDecors = hasMethodDecorators(classType);
        bool hasPropDecors = hasPropertyDecorators(classType);
        bool hasParamDecors = hasParameterDecorators(classType);
        SPDLOG_DEBUG("Checking static initializers for class {}: {} static blocks, {} static fields, classDecorators={}, methodDecorators={}, propertyDecorators={}, parameterDecorators={}",
            classType->name, classType->staticBlocks.size(), classType->staticFields.size(), hasClassDecors, hasMethodDecors, hasPropDecors, hasParamDecors);
        if (classType->staticBlocks.empty() && classType->staticFields.empty() && !hasClassDecors && !hasMethodDecors && !hasPropDecors && !hasParamDecors) continue;

        std::string initName = classType->name + "___static_init";
        SPDLOG_DEBUG("Generating static initializer: {}", initName);
        llvm::FunctionType* initFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
        llvm::Function* initFunc = llvm::Function::Create(initFt, llvm::Function::ExternalLinkage, initName, module.get());
        addStackProtection(initFunc);

        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", initFunc);
        llvm::BasicBlock* oldBB = builder->GetInsertBlock();
        builder->SetInsertPoint(entryBB);

        currentAsyncContext = initFunc->getArg(0);
        currentClass = classType;

        // Define 'this' for static context
        namedValues["this"] = llvm::ConstantPointerNull::get(builder->getPtrTy());

        // Initialize static fields
        auto* members = getClassMembers(classType);
        if (members) {
            for (const auto& member : *members) {
                if (auto prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                    if (prop->isStatic && prop->initializer) {
                        std::string baseName = classType->originalName.empty() ? classType->name : classType->originalName;
                        std::string mangledName = manglePrivateName(prop->name, baseName);

                        // Skip if already initialized as a constant in the global
                        if (classType->staticComptimeFields.count(mangledName)) {
                            if (dynamic_cast<ast::NumericLiteral*>(prop->initializer.get())) {
                                continue;
                            }
                        }

                        visit(prop->initializer.get());
                        std::string globalName = classType->name + "_" + mangledName;
                        llvm::GlobalVariable* gv = module->getGlobalVariable(globalName);
                        if (gv) {
                            // Ensure value matches field type
                            std::shared_ptr<Type> fieldType = classType->staticFields[mangledName];
                            if (fieldType) {
                                lastValue = castValue(lastValue, getLLVMType(fieldType));
                            }
                            builder->CreateStore(lastValue, gv);
                        }
                    }
                }
            }
        }

        for (auto* block : classType->staticBlocks) {
            visitStaticBlock(block);
        }

        // Call parameter decorators (before method decorators, per TypeScript spec)
        // Parameter decorators are called with (target, propertyKey, parameterIndex)
        ast::ClassDeclaration* classNode = classType->node;
        if (classNode) {
            for (const auto& member : classNode->members) {
                auto* method = dynamic_cast<ast::MethodDefinition*>(member.get());
                if (!method) continue;

                // Check if any parameters have decorators
                bool hasParamDecors = false;
                for (const auto& param : method->parameters) {
                    if (!param->decorators.empty()) {
                        hasParamDecors = true;
                        break;
                    }
                }
                if (!hasParamDecors) continue;

                SPDLOG_DEBUG("Processing parameter decorators for {}.{}",
                    classType->name, method->name);

                // Create the target object (prototype for instance methods)
                llvm::FunctionType* createMapFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee createMapFn = module->getOrInsertFunction("ts_map_create", createMapFt);
                llvm::Value* targetObject = builder->CreateCall(createMapFt, createMapFn.getCallee(), {});

                // Create property key string (method name)
                llvm::FunctionType* createStrFt = llvm::FunctionType::get(
                    builder->getPtrTy(),
                    { builder->getPtrTy() },
                    false
                );
                llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
                llvm::Value* methodNameCStr = builder->CreateGlobalStringPtr(method->name);
                llvm::Value* methodNameStr = builder->CreateCall(createStrFt, createStrFn.getCallee(), { methodNameCStr });

                // Box target (first param is 'any')
                llvm::FunctionType* boxObjectFt = llvm::FunctionType::get(
                    builder->getPtrTy(),
                    { builder->getPtrTy() },
                    false
                );
                llvm::FunctionCallee boxObjectFn = module->getOrInsertFunction("ts_value_make_object", boxObjectFt);
                llvm::Value* boxedTarget = builder->CreateCall(boxObjectFt, boxObjectFn.getCallee(), { targetObject });

                // Function type for making boxed int values
                llvm::FunctionType* makeIntFt = llvm::FunctionType::get(
                    builder->getPtrTy(),
                    { builder->getInt64Ty() },
                    false
                );
                llvm::FunctionCallee makeIntFn = module->getOrInsertFunction("ts_value_make_int", makeIntFt);

                // Process each parameter's decorators
                for (size_t paramIdx = 0; paramIdx < method->parameters.size(); ++paramIdx) {
                    const auto& param = method->parameters[paramIdx];
                    if (param->decorators.empty()) continue;

                    SPDLOG_DEBUG("  Parameter {} has {} decorators",
                        paramIdx, param->decorators.size());

                    // Create boxed parameter index
                    llvm::Value* paramIdxVal = llvm::ConstantInt::get(builder->getInt64Ty(), paramIdx);
                    llvm::Value* boxedParamIdx = builder->CreateCall(makeIntFt, makeIntFn.getCallee(), { paramIdxVal });

                    // Call each decorator in reverse order
                    for (auto it = param->decorators.rbegin(); it != param->decorators.rend(); ++it) {
                        const auto& decorator = *it;
                        SPDLOG_DEBUG("    Calling parameter decorator: {}", decorator.name);

                        if (!decorator.expression) continue;

                        if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                            // Parameter decorator signature: (target: any, propertyKey: string, parameterIndex: number) -> void
                            // Note: Monomorphizer uses "str" for string and "int" for number in mangled names
                            std::string mangledName = id->name + "_any_str_int";

                            llvm::FunctionType* decoratorFT = llvm::FunctionType::get(
                                builder->getVoidTy(),  // Parameter decorators return void
                                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },  // ctx, target, propertyKey, parameterIndex
                                false
                            );

                            llvm::FunctionCallee decoratorFn = module->getOrInsertFunction(mangledName, decoratorFT);

                            SPDLOG_DEBUG("    Calling parameter decorator function: {}", mangledName);

                            // Call the decorator: fn(ctx, target, propertyKey, parameterIndex)
                            // Note: propertyKey (methodNameStr) is raw TsString*, not boxed
                            // Note: parameterIndex is boxed as TsValue* since number type is 'any' in the signature
                            builder->CreateCall(decoratorFT, decoratorFn.getCallee(),
                                { llvm::ConstantPointerNull::get(builder->getPtrTy()),
                                  boxedTarget, methodNameStr, boxedParamIdx });
                        }
                        // TODO: Handle decorator factories for parameter decorators
                    }
                }
            }
        }

        // Call method decorators (before class decorators, per TypeScript spec)
        // Method decorators are called with (target, propertyKey, descriptor)
        classNode = classType->node;
        if (classNode) {
            for (const auto& member : classNode->members) {
                auto* method = dynamic_cast<ast::MethodDefinition*>(member.get());
                if (!method || method->decorators.empty()) continue;

                SPDLOG_DEBUG("Processing {} method decorators for {}.{}",
                    method->decorators.size(), classType->name, method->name);

                // Create the target object (prototype for instance methods)
                llvm::FunctionType* createMapFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee createMapFn = module->getOrInsertFunction("ts_map_create", createMapFt);
                llvm::Value* targetObject = builder->CreateCall(createMapFt, createMapFn.getCallee(), {});

                // Create property key string (method name)
                llvm::FunctionType* createStrFt = llvm::FunctionType::get(
                    builder->getPtrTy(),
                    { builder->getPtrTy() },
                    false
                );
                llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
                llvm::Value* methodNameCStr = builder->CreateGlobalStringPtr(method->name);
                llvm::Value* methodNameStr = builder->CreateCall(createStrFt, createStrFn.getCallee(), { methodNameCStr });

                // Create property descriptor with { value, writable, enumerable, configurable }
                llvm::Value* descriptor = builder->CreateCall(createMapFt, createMapFn.getCallee(), {});

                // Use ts_map_set_cstr for setting properties with C string keys
                llvm::FunctionType* setCstrFt = llvm::FunctionType::get(
                    builder->getVoidTy(),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                    false
                );
                llvm::FunctionCallee setCstrFn = module->getOrInsertFunction("ts_map_set_cstr", setCstrFt);

                // Set descriptor properties based on whether this is a getter, setter, or regular method
                // Get the actual method function - use correct prefix for getters/setters
                std::string mangledMethodName;
                if (method->isGetter) {
                    mangledMethodName = classType->name + "_get_" + method->name;
                } else if (method->isSetter) {
                    mangledMethodName = classType->name + "_set_" + method->name;
                } else {
                    mangledMethodName = classType->name + "_" + method->name;
                }
                llvm::Function* methodFunc = module->getFunction(mangledMethodName);

                // Create a TsFunction wrapper for the method using ts_value_make_native_function
                llvm::FunctionType* makeNativeFuncFt = llvm::FunctionType::get(
                    builder->getPtrTy(),
                    { builder->getPtrTy(), builder->getPtrTy() },
                    false
                );
                llvm::FunctionCallee makeNativeFuncFn = module->getOrInsertFunction("ts_value_make_native_function", makeNativeFuncFt);

                llvm::Value* wrappedMethod;
                if (methodFunc) {
                    wrappedMethod = builder->CreateCall(makeNativeFuncFt, makeNativeFuncFn.getCallee(),
                        { methodFunc, llvm::ConstantPointerNull::get(builder->getPtrTy()) });
                } else {
                    // Fallback: create null function wrapper
                    wrappedMethod = builder->CreateCall(makeNativeFuncFt, makeNativeFuncFn.getCallee(),
                        { llvm::ConstantPointerNull::get(builder->getPtrTy()),
                          llvm::ConstantPointerNull::get(builder->getPtrTy()) });
                }

                // For accessors, set 'get' or 'set' property instead of 'value'
                // For regular methods, set 'value' and 'writable'
                llvm::FunctionType* makeBoolFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt1Ty() }, false);
                llvm::FunctionCallee makeBoolFn = module->getOrInsertFunction("ts_value_make_bool", makeBoolFt);
                llvm::Value* trueVal = builder->CreateCall(makeBoolFt, makeBoolFn.getCallee(),
                    { llvm::ConstantInt::getTrue(*context) });

                if (method->isGetter) {
                    // Accessor decorator: set 'get' property
                    llvm::Value* getKeyPtr = builder->CreateGlobalStringPtr("get");
                    builder->CreateCall(setCstrFt, setCstrFn.getCallee(), { descriptor, getKeyPtr, wrappedMethod });
                } else if (method->isSetter) {
                    // Accessor decorator: set 'set' property
                    llvm::Value* setKeyPtr = builder->CreateGlobalStringPtr("set");
                    builder->CreateCall(setCstrFt, setCstrFn.getCallee(), { descriptor, setKeyPtr, wrappedMethod });
                } else {
                    // Regular method: set 'value' property
                    llvm::Value* valueKeyPtr = builder->CreateGlobalStringPtr("value");
                    builder->CreateCall(setCstrFt, setCstrFn.getCallee(), { descriptor, valueKeyPtr, wrappedMethod });

                    // Set 'writable' to true (only for regular methods, not accessors)
                    llvm::Value* writableKeyPtr = builder->CreateGlobalStringPtr("writable");
                    builder->CreateCall(setCstrFt, setCstrFn.getCallee(), { descriptor, writableKeyPtr, trueVal });
                }

                // Set 'enumerable' to false
                llvm::Value* falseVal = builder->CreateCall(makeBoolFt, makeBoolFn.getCallee(),
                    { llvm::ConstantInt::getFalse(*context) });
                llvm::Value* enumKeyPtr = builder->CreateGlobalStringPtr("enumerable");
                builder->CreateCall(setCstrFt, setCstrFn.getCallee(), { descriptor, enumKeyPtr, falseVal });

                // Set 'configurable' to true
                llvm::Value* configKeyPtr = builder->CreateGlobalStringPtr("configurable");
                builder->CreateCall(setCstrFt, setCstrFn.getCallee(), { descriptor, configKeyPtr, trueVal });

                // Box target and descriptor (first and third params are 'any')
                llvm::FunctionType* boxObjectFt = llvm::FunctionType::get(
                    builder->getPtrTy(),
                    { builder->getPtrTy() },
                    false
                );
                llvm::FunctionCallee boxObjectFn = module->getOrInsertFunction("ts_value_make_object", boxObjectFt);
                llvm::Value* boxedTarget = builder->CreateCall(boxObjectFt, boxObjectFn.getCallee(), { targetObject });
                llvm::Value* boxedDescriptor = builder->CreateCall(boxObjectFt, boxObjectFn.getCallee(), { descriptor });

                // Note: methodNameStr is already a TsString* - don't box it!
                // The decorator's propertyKey parameter is typed as 'string', not 'any'

                // Call each decorator in reverse order
                for (auto it = method->decorators.rbegin(); it != method->decorators.rend(); ++it) {
                    const auto& decorator = *it;
                    SPDLOG_DEBUG("  Calling method decorator: {}", decorator.name);

                    if (!decorator.expression) continue;

                    if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                        // Method decorator signature: (target: any, propertyKey: string, descriptor: any) -> any
                        // Note: Monomorphizer uses "str" for string type in mangled names
                        std::string mangledName = id->name + "_any_str_any";

                        llvm::FunctionType* decoratorFT = llvm::FunctionType::get(
                            builder->getPtrTy(),
                            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                            false
                        );

                        llvm::FunctionCallee decoratorFn = module->getOrInsertFunction(mangledName, decoratorFT);

                        SPDLOG_DEBUG("  Calling method decorator function: {}", mangledName);

                        // Call the decorator: fn(ctx, target, propertyKey, descriptor)
                        // Note: propertyKey (methodNameStr) is raw TsString*, not boxed
                        llvm::Value* result = builder->CreateCall(decoratorFT, decoratorFn.getCallee(),
                            { llvm::ConstantPointerNull::get(builder->getPtrTy()),
                              boxedTarget, methodNameStr, boxedDescriptor });

                        // The result could be a new descriptor or void - for now we ignore it
                        (void)result;
                    }
                    // TODO: Handle decorator factories for method decorators
                }
            }
        }

        // Call property decorators (before class decorators, per TypeScript spec)
        // Property decorators are called with (target, propertyKey) - no descriptor
        if (classNode) {
            for (const auto& member : classNode->members) {
                auto* prop = dynamic_cast<ast::PropertyDefinition*>(member.get());
                if (!prop || prop->decorators.empty()) continue;

                SPDLOG_DEBUG("Processing {} property decorators for {}.{}",
                    prop->decorators.size(), classType->name, prop->name);

                // Create the target object (prototype for instance properties)
                llvm::FunctionType* createMapFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee createMapFn = module->getOrInsertFunction("ts_map_create", createMapFt);
                llvm::Value* targetObject = builder->CreateCall(createMapFt, createMapFn.getCallee(), {});

                // Create property key string (property name)
                llvm::FunctionType* createStrFt = llvm::FunctionType::get(
                    builder->getPtrTy(),
                    { builder->getPtrTy() },
                    false
                );
                llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
                llvm::Value* propNameCStr = builder->CreateGlobalStringPtr(prop->name);
                llvm::Value* propNameStr = builder->CreateCall(createStrFt, createStrFn.getCallee(), { propNameCStr });

                // Box target (first param is 'any')
                llvm::FunctionType* boxObjectFt = llvm::FunctionType::get(
                    builder->getPtrTy(),
                    { builder->getPtrTy() },
                    false
                );
                llvm::FunctionCallee boxObjectFn = module->getOrInsertFunction("ts_value_make_object", boxObjectFt);
                llvm::Value* boxedTarget = builder->CreateCall(boxObjectFt, boxObjectFn.getCallee(), { targetObject });

                // Call each decorator in reverse order
                for (auto it = prop->decorators.rbegin(); it != prop->decorators.rend(); ++it) {
                    const auto& decorator = *it;
                    SPDLOG_DEBUG("  Calling property decorator: {}", decorator.name);

                    if (!decorator.expression) continue;

                    if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                        // Property decorator signature: (target: any, propertyKey: string) -> void
                        // Note: Monomorphizer uses "str" for string type in mangled names
                        std::string mangledName = id->name + "_any_str";

                        llvm::FunctionType* decoratorFT = llvm::FunctionType::get(
                            builder->getVoidTy(),  // Property decorators return void
                            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },  // ctx, target, propertyKey
                            false
                        );

                        llvm::FunctionCallee decoratorFn = module->getOrInsertFunction(mangledName, decoratorFT);

                        SPDLOG_DEBUG("  Calling property decorator function: {}", mangledName);

                        // Call the decorator: fn(ctx, target, propertyKey)
                        // Note: propertyKey (propNameStr) is raw TsString*, not boxed
                        builder->CreateCall(decoratorFT, decoratorFn.getCallee(),
                            { llvm::ConstantPointerNull::get(builder->getPtrTy()),
                              boxedTarget, propNameStr });
                    }
                    // TODO: Handle decorator factories for property decorators
                }
            }
        }

        // Call class decorators (in reverse order - innermost first)
        const auto* decorators = getClassDecorators(classType);
        if (decorators && !decorators->empty()) {
            SPDLOG_DEBUG("Generating {} class decorator calls for {}", decorators->size(), classType->name);

            // Create a class descriptor object with a 'name' property
            // This is the "target" passed to the decorator
            llvm::FunctionType* createMapFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee createMapFn = module->getOrInsertFunction("ts_map_create", createMapFt);
            llvm::Value* classDescriptor = builder->CreateCall(createMapFt, createMapFn.getCallee(), {});

            // Set the 'name' property on the descriptor using ts_map_set_cstr_string
            // This ensures the string value has STRING_PTR type, not OBJECT_PTR
            llvm::FunctionType* setStrFt = llvm::FunctionType::get(
                builder->getVoidTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                false
            );
            llvm::FunctionCallee setStrFn = module->getOrInsertFunction("ts_map_set_cstr_string", setStrFt);

            // Create string value for the class name
            llvm::FunctionType* createStrFt = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy() },
                false
            );
            llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
            llvm::Value* classNameCStr = builder->CreateGlobalStringPtr(classType->name);
            llvm::Value* classNameStr = builder->CreateCall(createStrFt, createStrFn.getCallee(), { classNameCStr });

            llvm::Value* nameKeyPtr = builder->CreateGlobalStringPtr("name");
            builder->CreateCall(setStrFt, setStrFn.getCallee(), { classDescriptor, nameKeyPtr, classNameStr });

            // Box the class descriptor as a TsValue*
            llvm::FunctionType* boxObjectFt = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy() },
                false
            );
            llvm::FunctionCallee boxObjectFn = module->getOrInsertFunction("ts_value_make_object", boxObjectFt);
            llvm::Value* boxedDescriptor = builder->CreateCall(boxObjectFt, boxObjectFn.getCallee(), { classDescriptor });

            // Call each decorator in reverse order (last decorator first, per TypeScript spec)
            for (auto it = decorators->rbegin(); it != decorators->rend(); ++it) {
                const auto& decorator = *it;
                SPDLOG_DEBUG("  Calling decorator: {}", decorator.name);

                if (!decorator.expression) continue;

                // If it's an identifier, look up the function directly and call it
                if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                    // Decorator functions are generated with _any suffix since they take/return any
                    std::string mangledName = id->name + "_any";

                    // Decorator function signature: (context: ptr, target: ptr) -> ptr
                    llvm::FunctionType* decoratorFT = llvm::FunctionType::get(
                        builder->getPtrTy(),
                        { builder->getPtrTy(), builder->getPtrTy() },
                        false
                    );

                    // Get or create the function declaration
                    llvm::FunctionCallee decoratorFn = module->getOrInsertFunction(mangledName, decoratorFT);

                    SPDLOG_DEBUG("  Calling decorator function: {}", mangledName);

                    // Call the decorator function: fn(ctx, target)
                    llvm::Value* result = builder->CreateCall(decoratorFT, decoratorFn.getCallee(),
                        { llvm::ConstantPointerNull::get(builder->getPtrTy()), boxedDescriptor });

                    // For now, we ignore the return value since we can't replace the class in AOT
                    (void)result;
                }
                // Handle decorator factories: @decorator(args)
                else if (auto* call = dynamic_cast<ast::CallExpression*>(decorator.expression.get())) {
                    if (auto* factoryId = dynamic_cast<ast::Identifier*>(call->callee.get())) {
                        SPDLOG_DEBUG("  Processing decorator factory: {}", factoryId->name);

                        // Build mangled name based on argument types
                        std::string mangledName = factoryId->name;
                        for (const auto& arg : call->arguments) {
                            if (arg->inferredType) {
                                mangledName += "_" + arg->inferredType->toString();
                            } else {
                                mangledName += "_any";
                            }
                        }

                        // Factory function takes its arguments and returns a decorator function
                        std::vector<llvm::Type*> factoryArgTypes;
                        factoryArgTypes.push_back(builder->getPtrTy()); // context
                        for (size_t i = 0; i < call->arguments.size(); ++i) {
                            factoryArgTypes.push_back(builder->getPtrTy()); // each arg is boxed
                        }

                        llvm::FunctionType* factoryFT = llvm::FunctionType::get(
                            builder->getPtrTy(), // returns decorator function
                            factoryArgTypes,
                            false
                        );

                        llvm::FunctionCallee factoryFn = module->getOrInsertFunction(mangledName, factoryFT);

                        // Evaluate factory arguments
                        std::vector<llvm::Value*> factoryArgs;
                        factoryArgs.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy())); // context

                        for (const auto& arg : call->arguments) {
                            visit(arg.get());
                            llvm::Value* argVal = boxValue(lastValue, arg->inferredType);
                            factoryArgs.push_back(argVal);
                        }

                        // Call factory to get the decorator function
                        SPDLOG_DEBUG("  Calling factory function: {}", mangledName);
                        llvm::Value* decoratorFnPtr = builder->CreateCall(factoryFT, factoryFn.getCallee(), factoryArgs);

                        // Now call the returned decorator with the target
                        // Use ts_call_1 to invoke the returned decorator (takes func, arg1)
                        llvm::FunctionType* callFuncFT = llvm::FunctionType::get(
                            builder->getPtrTy(),
                            { builder->getPtrTy(), builder->getPtrTy() },
                            false
                        );
                        llvm::FunctionCallee callFuncFn = module->getOrInsertFunction("ts_call_1", callFuncFT);

                        SPDLOG_DEBUG("  Calling returned decorator with target");
                        llvm::Value* result = builder->CreateCall(callFuncFT, callFuncFn.getCallee(),
                            { decoratorFnPtr, boxedDescriptor });

                        (void)result;
                    } else {
                        SPDLOG_WARN("  Decorator factory with non-identifier callee not supported: {}", decorator.name);
                    }
                } else {
                    SPDLOG_WARN("  Unknown decorator expression type: {}", decorator.name);
                }
            }
        }

        builder->CreateRetVoid();
        if (oldBB) builder->SetInsertPoint(oldBB);
        
        currentAsyncContext = nullptr;
        currentClass = nullptr;
    }
}

void IRGenerator::visitNewExpression(ast::NewExpression* node) {
    emitLocation(node);
    SPDLOG_DEBUG("visitNewExpression: inferredType={}", (node->inferredType ? (int)node->inferredType->kind : -1));

    // Ensure boxing policies are registered for built-in constructors
    ensureBoxedPrimitivesBoxingRegistered(boxingPolicy);

    // Check for built-in constructors by looking at the expression directly
    // This is done early because these types may not have proper ClassType inference
    if (auto* id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (id->name == "Proxy" && node->arguments.size() >= 2) {
            // new Proxy(target, handler)
            visit(node->arguments[0].get());
            llvm::Value* target = boxValue(lastValue, node->arguments[0]->inferredType);
            visit(node->arguments[1].get());
            llvm::Value* handler = boxValue(lastValue, node->arguments[1]->inferredType);

            llvm::FunctionType* createProxyFt = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee createProxyFn = getRuntimeFunction("ts_proxy_create", createProxyFt);
            lastValue = createCall(createProxyFt, createProxyFn.getCallee(), { target, handler });
            boxedValues.insert(lastValue);
            nonNullValues.insert(lastValue);
            return;
        } else if (id->name == "Boolean" && node->arguments.size() >= 1) {
            // new Boolean(value) - creates boxed Boolean object
            visit(node->arguments[0].get());
            llvm::Value* boolValue = lastValue;

            // Convert to boolean if not already
            if (boolValue->getType()->isIntegerTy(64) || boolValue->getType()->isIntegerTy(32)) {
                boolValue = builder->CreateICmpNE(boolValue,
                    llvm::ConstantInt::get(boolValue->getType(), 0));
            } else if (boolValue->getType()->isDoubleTy()) {
                boolValue = builder->CreateFCmpONE(boolValue,
                    llvm::ConstantFP::get(builder->getDoubleTy(), 0.0));
            } else if (boolValue->getType()->isPointerTy()) {
                boolValue = builder->CreateICmpNE(boolValue,
                    llvm::ConstantPointerNull::get(builder->getPtrTy()));
            }

            // Extend to i8 for C ABI
            if (boolValue->getType()->isIntegerTy(1)) {
                boolValue = builder->CreateZExt(boolValue, builder->getInt8Ty());
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getInt8Ty() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_boolean_object_create", ft);
            lastValue = createCall(ft, fn.getCallee(), { boolValue });
            boxedValues.insert(lastValue);
            nonNullValues.insert(lastValue);
            return;
        } else if (id->name == "Number" && node->arguments.size() >= 1) {
            // new Number(value) - creates boxed Number object
            visit(node->arguments[0].get());
            llvm::Value* numValue = lastValue;

            // Convert to double if not already
            if (numValue->getType()->isIntegerTy()) {
                numValue = builder->CreateSIToFP(numValue, builder->getDoubleTy());
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getDoubleTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_number_object_create", ft);
            lastValue = createCall(ft, fn.getCallee(), { numValue });
            boxedValues.insert(lastValue);
            nonNullValues.insert(lastValue);
            return;
        } else if (id->name == "String" && node->arguments.size() >= 1) {
            // new String(value) - creates boxed String object
            visit(node->arguments[0].get());
            llvm::Value* strValue = lastValue;

            // If not already a string, convert via ts_to_string
            auto argType = node->arguments[0]->inferredType;
            if (!argType || argType->kind != TypeKind::String) {
                llvm::Value* boxed = boxValue(strValue, argType);
                llvm::FunctionType* toStrFt = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy() }, false);
                llvm::FunctionCallee toStrFn = getRuntimeFunction("ts_to_string", toStrFt);
                strValue = createCall(toStrFt, toStrFn.getCallee(), { boxed });
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_string_object_create", ft);
            lastValue = createCall(ft, fn.getCallee(), { strValue });
            boxedValues.insert(lastValue);
            nonNullValues.insert(lastValue);
            return;
        }
    }

    // Check for module.SourceMap constructor
    if (tryGenerateModuleSourceMapNew(node)) {
        return;
    }

    if (node->inferredType && node->inferredType->kind == TypeKind::Map) {
        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_map_create_explicit", createFt);
        lastValue = createCall(createFt, fn.getCallee(), {});
        nonNullValues.insert(lastValue);
        return;
    }

    if (node->inferredType && node->inferredType->kind == TypeKind::SetType) {
        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_set_create", createFt);
        lastValue = createCall(createFt, fn.getCallee(), {});
        nonNullValues.insert(lastValue);
        return;
    }

    if (node->inferredType && node->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(node->inferredType);
        std::string className = classType->name;

        if (className == "WeakMap") {
            // WeakMap is implemented as regular Map (weak semantics not supported with Boehm GC)
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_weakmap_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), {});
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "WeakSet") {
            // WeakSet is implemented as regular Set (weak semantics not supported with Boehm GC)
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_weakset_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), {});
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "StringDecoder") {
            // Register boxing policy for string decoder functions
            ensureStringDecoderBoxingRegistered(boxingPolicy);

            // new StringDecoder(encoding?)
            llvm::Value* encoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                encoding = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            // Call ts_string_decoder_create(encoding)
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_string_decoder_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { encoding });
            boxedValues.insert(lastValue);
            nonNullValues.insert(lastValue);
            concreteTypes[lastValue] = classType.get();
            return;
        } else if (className == "Date") {
            if (node->arguments.empty()) {
                llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_date_create", createFt);
                lastValue = createCall(createFt, fn.getCallee(), {});
            } else {
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy() || arg->getType()->isDoubleTy()) {
                    if (arg->getType()->isDoubleTy()) {
                        arg = builder->CreateFPToSI(arg, llvm::Type::getInt64Ty(*context));
                    }
                    llvm::FunctionType* createMsFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::Type::getInt64Ty(*context) }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_date_create_ms", createMsFt);
                    lastValue = createCall(createMsFt, fn.getCallee(), { arg });
                } else {
                    llvm::FunctionType* createStrFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::PointerType::getUnqual(*context) }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_date_create_str", createStrFt);
                    lastValue = createCall(createStrFt, fn.getCallee(), { arg });
                }
            }
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "Error") {
            llvm::Value* message = nullptr;
            llvm::Value* options = nullptr;

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                message = lastValue;
            } else {
                // Create empty string
                llvm::Constant* emptyStr = builder->CreateGlobalStringPtr("");
                llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee createFn = getRuntimeFunction("ts_string_create", createFt);
                message = createCall(createFt, createFn.getCallee(), { emptyStr });
            }

            // ES2022: Check for second argument { cause: ... }
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                options = boxValue(lastValue, node->arguments[1]->inferredType);

                llvm::FunctionType* createErrorFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee createErrorFn = getRuntimeFunction("ts_error_create_with_options", createErrorFt);
                lastValue = createCall(createErrorFt, createErrorFn.getCallee(), { message, options });
            } else {
                llvm::FunctionType* createErrorFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee createErrorFn = getRuntimeFunction("ts_error_create", createErrorFt);
                lastValue = createCall(createErrorFt, createErrorFn.getCallee(), { message });
            }
            nonNullValues.insert(lastValue);
            return;
        } else if (className.find("Promise") == 0 && !node->arguments.empty()) {
            // new Promise((resolve, reject) => { ... })
            visit(node->arguments[0].get());
            llvm::Value* executor = lastValue;

            llvm::FunctionType* createPromiseFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createPromiseFn = getRuntimeFunction("ts_promise_new", createPromiseFt);
            lastValue = createCall(createPromiseFt, createPromiseFn.getCallee(), { executor });
            boxedValues.insert(lastValue);
            nonNullValues.insert(lastValue);

            if (node->inferredType && node->inferredType->kind == TypeKind::Class) {
                auto promiseClassType = std::static_pointer_cast<ClassType>(node->inferredType);
                concreteTypes[lastValue] = promiseClassType.get();
            }
            return;
        } else if (className == "RegExp") {
            llvm::Value* pattern = nullptr;
            llvm::Value* flags = nullptr;
            
            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                pattern = lastValue;
            } else {
                llvm::Constant* emptyStr = builder->CreateGlobalStringPtr("");
                llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::PointerType::getUnqual(*context) }, false);
                llvm::FunctionCallee createFn = getRuntimeFunction("ts_string_create", createFt);
                pattern = createCall(createFt, createFn.getCallee(), { emptyStr });
            }
            
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                flags = lastValue;
            } else {
                flags = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
            }
            
            llvm::FunctionType* createRegExpFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                    { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_regexp_create", createRegExpFt);
            lastValue = createCall(createRegExpFt, fn.getCallee(), { pattern, flags });
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "URL") {
            llvm::Value* url = nullptr;
            llvm::Value* base = nullptr;
            
            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                url = lastValue;
            }
            
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                base = lastValue;
            } else {
                base = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
            }
            
            std::string vtableGlobalName = className + "_VTable_Global";
            llvm::Value* vtable = module->getGlobalVariable(vtableGlobalName);
            if (!vtable) {
                vtable = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
            }

            llvm::FunctionType* createUrlFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                    { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_create", createUrlFt);
            lastValue = createCall(createUrlFt, fn.getCallee(), { vtable, url, base });
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "URLSearchParams") {
            llvm::Value* query = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                query = lastValue;
            }
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { query });
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "Int8Array" || className == "Uint8Array" || className == "Uint8ClampedArray" ||
                   className == "Int16Array" || className == "Uint16Array" ||
                   className == "Int32Array" || className == "Uint32Array" ||
                   className == "Float32Array" || className == "Float64Array" ||
                   className == "BigInt64Array" || className == "BigUint64Array") {
            // Determine TypedArrayType enum value for this type
            // Enum: Int8=0, Uint8=1, Uint8Clamped=2, Int16=3, Uint16=4, Int32=5, Uint32=6, Float32=7, Float64=8, BigInt64=9, BigUint64=10
            int typeVal = 0;  // default Int8
            if (className == "Int8Array") typeVal = 0;
            else if (className == "Uint8Array") typeVal = 1;
            else if (className == "Uint8ClampedArray") typeVal = 2;
            else if (className == "Int16Array") typeVal = 3;
            else if (className == "Uint16Array") typeVal = 4;
            else if (className == "Int32Array") typeVal = 5;
            else if (className == "Uint32Array") typeVal = 6;
            else if (className == "Float32Array") typeVal = 7;
            else if (className == "Float64Array") typeVal = 8;
            else if (className == "BigInt64Array") typeVal = 9;
            else if (className == "BigUint64Array") typeVal = 10;

            // Also compute element size for backwards compatibility
            int elementSize = 1;  // default for i8/u8
            if (className == "Int16Array" || className == "Uint16Array") elementSize = 2;
            else if (className == "Int32Array" || className == "Uint32Array" || className == "Float32Array") elementSize = 4;
            else if (className == "Float64Array" || className == "BigInt64Array" || className == "BigUint64Array") elementSize = 8;

            bool isClamped = (className == "Uint8ClampedArray");
            bool isBigInt = (className == "BigInt64Array" || className == "BigUint64Array");

            llvm::Value* arg = nullptr;
            bool isArrayLiteral = false;
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                arg = lastValue;

                if (node->arguments[0]->inferredType && (node->arguments[0]->inferredType->kind == TypeKind::Array || node->arguments[0]->inferredType->kind == TypeKind::Tuple)) {
                    isArrayLiteral = true;
                }
            } else {
                arg = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            }

            if (isArrayLiteral) {
                if (isClamped) {
                    // Handle new Uint8ClampedArray([1, 2, 3]) - use clamped from_array function
                    llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                            { llvm::PointerType::getUnqual(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_typed_array_from_array_clamped", createFt);
                    lastValue = createCall(createFt, fn.getCallee(), { arg });
                } else if (isBigInt) {
                    // Handle new BigInt64Array([1n, 2n, 3n]) - use typed from_array function
                    llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                            { llvm::PointerType::getUnqual(*context), llvm::Type::getInt32Ty(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_typed_array_from_array_typed", createFt);
                    lastValue = createCall(createFt, fn.getCallee(), {
                        arg,
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), typeVal)
                    });
                } else {
                    // Handle new Int8Array([1, 2, 3]) - use generic from_array function
                    llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                            { llvm::PointerType::getUnqual(*context), llvm::Type::getInt32Ty(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_typed_array_from_array", createFt);
                    lastValue = createCall(createFt, fn.getCallee(), {
                        arg,
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), elementSize)
                    });
                }
            } else {
                // Ensure arg is i64
                if (arg->getType()->isIntegerTy()) {
                    arg = builder->CreateIntCast(arg, llvm::Type::getInt64Ty(*context), true);
                }

                if (isClamped) {
                    // new Uint8ClampedArray(length) - use clamped create function
                    llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                            { llvm::Type::getInt64Ty(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_typed_array_create_clamped", createFt);
                    lastValue = createCall(createFt, fn.getCallee(), { arg });
                } else if (isBigInt) {
                    // new BigInt64Array(length) - use typed create function
                    llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                            { llvm::Type::getInt64Ty(*context), llvm::Type::getInt32Ty(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_typed_array_create_typed", createFt);
                    lastValue = createCall(createFt, fn.getCallee(), {
                        arg,
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), typeVal)
                    });
                } else {
                    llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                            { llvm::Type::getInt64Ty(*context), llvm::Type::getInt32Ty(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_typed_array_create", createFt);
                    lastValue = createCall(createFt, fn.getCallee(), {
                        arg,
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), elementSize)
                    });
                }
            }
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "DataView") {
            visit(node->arguments[0].get());
            llvm::Value* buffer = lastValue;
            
            llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                    { llvm::PointerType::getUnqual(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_data_view_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { buffer });
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "EventEmitter") {
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), {});
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "Readable" || className == "Writable" || className == "Stream" || className == "Duplex" || className == "Transform") {
            // For now, just create an EventEmitter as a base
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), {});
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "TextEncoder") {
            // new TextEncoder()
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_text_encoder_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), {});
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "TextDecoder") {
            // new TextDecoder(label?, options?)
            llvm::Value* label = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* fatal = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
            llvm::Value* ignoreBOM = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
            
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                label = lastValue;
            }
            // TODO: Parse options object for fatal and ignoreBOM if node->arguments.size() >= 2
            
            llvm::FunctionType* createFt = llvm::FunctionType::get(
                builder->getPtrTy(), 
                { builder->getPtrTy(), llvm::Type::getInt1Ty(*context), llvm::Type::getInt1Ty(*context) }, 
                false
            );
            llvm::FunctionCallee fn = getRuntimeFunction("ts_text_decoder_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { label, fatal, ignoreBOM });
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "SocketAddress") {
            // new net.SocketAddress(options?)
            llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                options = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_socket_address_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { options });
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "BlockList") {
            // new net.BlockList()
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_block_list_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), {});
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "Socket") {
            // new net.Socket(options?)
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_net_create_socket", createFt);
            lastValue = createCall(createFt, fn.getCallee(), {});
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "Transform") {
            // new stream.Transform(options?)
            llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                options = boxValue(lastValue, node->arguments[0]->inferredType);
            }

            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_stream_transform_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { options });
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "Agent") {
            // new http.Agent(options?)
            llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                options = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_http_agent_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { options });
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "HttpsAgent") {
            // new https.Agent(options?)
            llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                options = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_https_agent_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { options });
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "WebSocket") {
            // new WebSocket(url, protocols?)
            llvm::Value* url = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* protocols = llvm::ConstantPointerNull::get(builder->getPtrTy());
            
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                url = lastValue;
            }
            
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                protocols = boxValue(lastValue, node->arguments[1]->inferredType);
            }
            
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_websocket_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { url, protocols });
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "AsyncLocalStorage") {
            // new AsyncLocalStorage()
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_async_local_storage_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), {});
            boxedValues.insert(lastValue);
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "AsyncResource") {
            // new AsyncResource(type, triggerAsyncId?)
            llvm::Value* typeStr = nullptr;
            llvm::Value* triggerId = llvm::ConstantInt::get(builder->getInt64Ty(), -1);

            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                typeStr = lastValue;
            } else {
                // Create a default "UNKNOWN" string
                llvm::Constant* unknownCStr = builder->CreateGlobalStringPtr("UNKNOWN");
                llvm::FunctionType* strFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee strFn = getRuntimeFunction("ts_string_create", strFt);
                typeStr = createCall(strFt, strFn.getCallee(), { unknownCStr });
            }

            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                triggerId = lastValue;
                // Ensure it's an i64
                if (triggerId->getType() != builder->getInt64Ty()) {
                    if (triggerId->getType()->isDoubleTy()) {
                        triggerId = builder->CreateFPToSI(triggerId, builder->getInt64Ty());
                    }
                }
            }

            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getInt64Ty() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_async_resource_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { typeStr, triggerId });
            boxedValues.insert(lastValue);
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "InspectorSession") {
            // new inspector.Session() - stub implementation
            ensureInspectorSessionBoxingRegistered(boxingPolicy);
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_session_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), {});
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "TTYReadStream") {
            // new tty.ReadStream(fd)
            llvm::Value* fd = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            if (node->arguments.size() > 0) {
                visit(node->arguments[0].get());
                fd = lastValue;
            }
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getInt64Ty() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_read_stream_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { fd });
            boxedValues.insert(lastValue);
            nonNullValues.insert(lastValue);
            return;
        } else if (className == "TTYWriteStream") {
            // new tty.WriteStream(fd)
            llvm::Value* fd = llvm::ConstantInt::get(builder->getInt64Ty(), 1);
            if (node->arguments.size() > 0) {
                visit(node->arguments[0].get());
                fd = lastValue;
            }
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getInt64Ty() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_tty_write_stream_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), { fd });
            boxedValues.insert(lastValue);
            nonNullValues.insert(lastValue);
            return;
        }

        // 1. Get Struct Type
        llvm::StructType* structType = llvm::StructType::getTypeByName(*context, className);
        if (!structType) {
            lastValue = llvm::Constant::getNullValue(llvm::PointerType::getUnqual(*context));
            return;
        }
        
        // 2. Allocate
        const llvm::DataLayout& dl = module->getDataLayout();
        
        llvm::Value* thisPtr;
        if (classType->isStruct /* || !node->escapes */) {
            llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
            llvm::IRBuilder<> entryBuilder(&currentFunc->getEntryBlock(), currentFunc->getEntryBlock().begin());
            thisPtr = entryBuilder.CreateAlloca(structType, nullptr, className + "_stack");
            // Zero-initialize
            builder->CreateMemSet(thisPtr, builder->getInt8(0), dl.getTypeAllocSize(structType), dl.getABITypeAlign(structType));
        } else {
            llvm::FunctionType* allocFt = llvm::FunctionType::get(
                llvm::PointerType::getUnqual(*context),
                { llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee allocFn = getRuntimeFunction("ts_alloc", allocFt);
            
            uint64_t size = dl.getTypeAllocSize(structType);
            SPDLOG_DEBUG("Allocating {} size={}", className, size);
            llvm::Value* sizeVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), size);
            llvm::Value* mem = createCall(allocFt, allocFn.getCallee(), { sizeVal });
            thisPtr = builder->CreateBitCast(mem, llvm::PointerType::getUnqual(structType));
        }
        
        // 3. Initialize VPtr
        if (!classType->isStruct) {
            std::string vtableName = className + "_VTable";
            llvm::GlobalVariable* vtable = module->getGlobalVariable(vtableName + "_Global");
            if (vtable) {
                llvm::Value* vptrField = builder->CreateStructGEP(structType, thisPtr, 0);
                builder->CreateStore(vtable, vptrField);
            }
        }

        // 3.25 Initialize __properties__ for classes with index signatures
        if (classType->indexSignatureValueType) {
            auto& layout = classLayouts[className];
            if (layout.fieldIndices.count("__properties__")) {
                int fieldIndex = layout.fieldIndices["__properties__"];
                SPDLOG_DEBUG("Initializing __properties__ field for {} at index {}", className, fieldIndex);

                // Create a TsMap for dynamic properties
                llvm::FunctionType* createMapFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee createMapFn = getRuntimeFunction("ts_map_create", createMapFt);
                llvm::Value* propsMap = createCall(createMapFt, createMapFn.getCallee(), {});

                // Store the map in the __properties__ field
                llvm::Value* propsField = builder->CreateStructGEP(structType, thisPtr, fieldIndex);
                builder->CreateStore(propsMap, propsField);
            }
        }

        // 3.5 Initialize Fields
        std::string initFieldsName = className + "_init_fields";
        if (llvm::Function* initFieldsFunc = module->getFunction(initFieldsName)) {
            SPDLOG_DEBUG("Calling field initializer: {}", initFieldsName);
            llvm::Value* contextVal = currentAsyncContext;
            if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());
            createCall(initFieldsFunc->getFunctionType(), initFieldsFunc, { contextVal, thisPtr });
        }
        
        // 4. Call Constructor
        std::string baseCtorName = className + "_constructor";
        
        // Try specialized constructor
        std::vector<std::shared_ptr<Type>> argTypes;
        argTypes.push_back(classType); // this
        for (auto& arg : node->arguments) {
            argTypes.push_back(arg->inferredType ? arg->inferredType : std::make_shared<Type>(TypeKind::Any));
        }
        std::string specializedCtorName = Monomorphizer::generateMangledName(baseCtorName, argTypes, {});
        
        llvm::Function* ctor = module->getFunction(specializedCtorName);
        if (!ctor) {
            ctor = module->getFunction(baseCtorName);
        }

        if (ctor) {
            std::vector<llvm::Value*> args;
            if (currentAsyncContext) {
                args.push_back(currentAsyncContext);
            } else {
                args.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
            }
            args.push_back(thisPtr);
            
            int argIdx = 0;
            for (auto& arg : node->arguments) {
                visit(arg.get());
                llvm::Value* val = lastValue;
                
                // Cast if necessary (index + 2 because of context and this)
                if (argIdx + 2 < (int)ctor->arg_size()) {
                    val = castValue(val, ctor->getArg(argIdx + 2)->getType());
                }
                
                args.push_back(val);
                argIdx++;
            }
            
            createCall(ctor->getFunctionType(), ctor, args);
        } else {
            // Constructor not found
        }
        
        if (classType->isStruct) {
            lastValue = builder->CreateLoad(structType, thisPtr);
        } else {
            lastValue = thisPtr;
            nonNullValues.insert(lastValue);
            lastConcreteType = thisPtr;
            concreteTypes[lastValue] = classType.get();
        }
        return;
    }

    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (id->name == "Map") {
            llvm::FunctionType* createMapFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_map_create_explicit", createMapFt);
            lastValue = createCall(createMapFt, fn.getCallee(), {});
            nonNullValues.insert(lastValue);
            return;
        } else if (id->name == "Array") {
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                llvm::Value* size = lastValue;
                
                if (size->getType()->isDoubleTy()) {
                    size = builder->CreateFPToSI(size, llvm::Type::getInt64Ty(*context));
                }
                
                llvm::FunctionType* createSizedFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                        { llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_array_create_sized", createSizedFt);
                lastValue = createCall(createSizedFt, fn.getCallee(), { size });
            } else {
                llvm::FunctionType* createArrayFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_array_create", createArrayFt);
                lastValue = createCall(createArrayFt, fn.getCallee(), {});
            }
            nonNullValues.insert(lastValue);
            return;
        } else if (id->name == "Date") {
            if (node->arguments.empty()) {
                llvm::FunctionType* createDateFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_date_create", createDateFt);
                lastValue = createCall(createDateFt, fn.getCallee(), {});
            } else {
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy() || arg->getType()->isDoubleTy()) {
                    if (arg->getType()->isDoubleTy()) {
                        arg = builder->CreateFPToSI(arg, llvm::Type::getInt64Ty(*context));
                    }
                    llvm::FunctionType* createMsFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::Type::getInt64Ty(*context) }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_date_create_ms", createMsFt);
                    lastValue = createCall(createMsFt, fn.getCallee(), { arg });
                } else {
                    llvm::FunctionType* createStrFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::PointerType::getUnqual(*context) }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_date_create_str", createStrFt);
                    lastValue = createCall(createStrFt, fn.getCallee(), { arg });
                }
            }
            nonNullValues.insert(lastValue);
            return;
        } else if (id->name == "RegExp") {
            llvm::Value* pattern = nullptr;
            llvm::Value* flags = nullptr;
            
            if (node->arguments.size() >= 1) {
                visit(node->arguments[0].get());
                pattern = lastValue;
            } else {
                llvm::Constant* emptyStr = builder->CreateGlobalStringPtr("");
                llvm::FunctionType* createStrFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::PointerType::getUnqual(*context) }, false);
                llvm::FunctionCallee createFn = getRuntimeFunction("ts_string_create", createStrFt);
                pattern = createCall(createStrFt, createFn.getCallee(), { emptyStr });
            }
            
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                flags = lastValue;
            } else {
                flags = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
            }
            
            llvm::FunctionType* createRegExpFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                    { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_regexp_create", createRegExpFt);
            lastValue = createCall(createRegExpFt, fn.getCallee(), { pattern, flags });
            nonNullValues.insert(lastValue);
            return;
        } else if (id->name == "EventEmitter") {
            llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_event_emitter_create", createFt);
            lastValue = createCall(createFt, fn.getCallee(), {});
            nonNullValues.insert(lastValue);
            return;
        }
    }
    lastValue = llvm::Constant::getNullValue(llvm::PointerType::getUnqual(*context));
}

void IRGenerator::visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) {
    SPDLOG_DEBUG("visitObjectLiteralExpression");
    llvm::FunctionType* createMapFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    llvm::FunctionCallee createFn = getRuntimeFunction("ts_map_create", createMapFt);
    llvm::Value* map = createCall(createMapFt, createFn.getCallee(), {});
    nonNullValues.insert(map);

    llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);

    for (auto& prop : node->properties) {
        if (!prop) {
            continue;
        }
        if (auto pa = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
            visit(pa->initializer.get());
            llvm::Value* val = lastValue;
            
            std::string keyName = "unknown";
            if (auto id = dynamic_cast<ast::Identifier*>(pa->nameNode.get())) keyName = id->name;
            SPDLOG_DEBUG("PropertyAssignment: key={} typeKind={}", keyName, (pa->initializer->inferredType ? (int)pa->initializer->inferredType->kind : -1));

            // Box the value
            llvm::Value* boxedVal = boxValue(val, pa->initializer->inferredType);
            
            llvm::Value* keyStr = nullptr;
            if (auto id = dynamic_cast<ast::Identifier*>(pa->nameNode.get())) {
                keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(id->name) });
            } else if (auto strLit = dynamic_cast<ast::StringLiteral*>(pa->nameNode.get())) {
                // String literal key (e.g., "[Symbol.iterator]")
                keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(strLit->value) });
            } else if (auto computed = dynamic_cast<ast::ComputedPropertyName*>(pa->nameNode.get())) {
                visit(computed->expression.get());
                keyStr = unboxValue(lastValue, std::make_shared<Type>(TypeKind::String));
            } else {
                keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(pa->name) });
            }
            
            llvm::Value* boxedKey = boxValue(keyStr, std::make_shared<Type>(TypeKind::String));
            // Use inline map set operation
            emitInlineMapSet(map, boxedKey, boxedVal);
        } else if (auto spa = dynamic_cast<ast::ShorthandPropertyAssignment*>(prop.get())) {
            // Shorthand property: { foo } => { foo: foo }
            // Need to properly visit the identifier to handle cell variables
            ast::Identifier id;
            id.name = spa->name;
            // Try to get inferredType from the namedValues or use Any
            id.inferredType = std::make_shared<Type>(TypeKind::Any);
            visitIdentifier(&id);
            
            // The value from visitIdentifier is already boxed if it came from a cell
            llvm::Value* val = lastValue;
            llvm::Value* boxedVal = val;
            
            // Only box if not already boxed
            if (!boxedValues.count(val)) {
                boxedVal = boxValue(val, std::make_shared<Type>(TypeKind::Any));
            }
            
            llvm::Value* keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(spa->name) });
            llvm::Value* boxedKey = boxValue(keyStr, std::make_shared<Type>(TypeKind::String));
            // Use inline map set operation
            emitInlineMapSet(map, boxedKey, boxedVal);
        } else if (auto method = dynamic_cast<ast::MethodDefinition*>(prop.get())) {
            visitMethodDefinition(method);
            llvm::Value* val = lastValue; // This is already boxed by visitMethodDefinition
            
            std::string keyName;
            if (auto id = dynamic_cast<ast::Identifier*>(method->nameNode.get())) {
                // For getters/setters, use special key names
                if (method->isGetter) {
                    keyName = "__getter_" + id->name;
                } else if (method->isSetter) {
                    keyName = "__setter_" + id->name;
                } else {
                    keyName = id->name;
                }
            }
            
            llvm::Value* keyStr = nullptr;
            if (!keyName.empty()) {
                keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(keyName) });
            } else if (auto computed = dynamic_cast<ast::ComputedPropertyName*>(method->nameNode.get())) {
                visit(computed->expression.get());
                keyStr = unboxValue(lastValue, std::make_shared<Type>(TypeKind::String));
            }
            
            if (keyStr && val) {
                llvm::Value* boxedKey = boxValue(keyStr, std::make_shared<Type>(TypeKind::String));
                // Use inline map set operation
                emitInlineMapSet(map, boxedKey, val);
            }
        } else if (auto spread = dynamic_cast<ast::SpreadElement*>(prop.get())) {
            visit(spread->expression.get());
            llvm::Value* source = lastValue;
            
            // Box source if needed
            llvm::Value* boxedSource = boxValue(source, spread->expression->inferredType);
            
            llvm::FunctionType* assignFt = llvm::FunctionType::get(builder->getPtrTy(), 
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee assignFn = getRuntimeFunction("ts_object_assign", assignFt);
            
            // ts_object_assign(target, source) - capture return and unbox to get updated map
            llvm::Value* boxedMap = boxValue(map, std::make_shared<Type>(TypeKind::Object));
            llvm::Value* resultBoxed = createCall(assignFt, assignFn.getCallee(), { boxedMap, boxedSource });
            
            // Unbox the result to get the raw map pointer
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_object", unboxFt);
            map = createCall(unboxFt, unboxFn.getCallee(), { resultBoxed });
        }
    }

    // Return the raw map pointer, not boxed
    // The caller will box it if needed (e.g., when passing to a function that expects boxed values)
    // This prevents double-boxing when the object literal is stored to an async frame and then loaded back
    lastValue = map;
}

void IRGenerator::visitPropertyAssignment(ast::PropertyAssignment* node) {
    // Handled in visitObjectLiteralExpression
    lastValue = nullptr;
}

void IRGenerator::visitComputedPropertyName(ast::ComputedPropertyName* node) {
    visit(node->expression.get());
}

void IRGenerator::visitSuperExpression(ast::SuperExpression* node) {
    // Handled in visitCallExpression
    lastValue = nullptr;
}

void IRGenerator::visitStaticBlock(ast::StaticBlock* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

// ES2022: Handle local class declarations (classes declared inside functions)
// This is called when a ClassDeclaration is visited as a statement inside a function body.
// Top-level classes are handled by generateClasses() which runs before function codegen.
void IRGenerator::visitClassDeclaration(ast::ClassDeclaration* node) {
    std::string className = node->name;

    // ES2022: For local classes, we need to create globals for static fields
    // and execute static blocks. Top-level classes are handled in generateClasses.
    for (auto& member : node->members) {
        if (auto prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
            if (prop->isStatic) {
                std::string globalName = className + "_" + prop->name;

                // Check if the global already exists (from generateClasses for top-level classes)
                if (!module->getGlobalVariable(globalName)) {
                    // Default type is double (number) for static fields
                    llvm::Type* llvmType = builder->getDoubleTy();

                    // Create the global variable
                    llvm::Constant* initVal = llvm::Constant::getNullValue(llvmType);
                    new llvm::GlobalVariable(*module, llvmType, false,
                        llvm::GlobalValue::ExternalLinkage, initVal, globalName);
                }

                // Initialize if there's an initializer
                if (prop->initializer) {
                    visit(prop->initializer.get());
                    llvm::Value* initValue = lastValue;
                    llvm::GlobalVariable* gVar = module->getGlobalVariable(globalName);
                    if (gVar) {
                        initValue = castValue(initValue, gVar->getValueType());
                        builder->CreateStore(initValue, gVar);
                    }
                }
            }
        }
    }

    // ES2022: Execute static blocks inline for local class declarations
    for (auto& member : node->members) {
        if (auto staticBlock = dynamic_cast<ast::StaticBlock*>(member.get())) {
            visitStaticBlock(staticBlock);
        }
    }

    // The value of the class declaration is the class type itself
    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
}

} // namespace ts


