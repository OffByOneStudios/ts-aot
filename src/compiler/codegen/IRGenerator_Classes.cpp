#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;

void IRGenerator::generateClasses(const Analyzer& analyzer, const std::vector<Specialization>& specializations) {
    std::vector<std::shared_ptr<ClassType>> allClassTypes;
    
    // Add global classes
    for (const auto& [name, type] : analyzer.getSymbolTable().getGlobalTypes()) {
        if (name == "Date" || name == "RegExp" || name == "Promise" || name == "Map") continue;
        llvm::errs() << "Global type: " << name << " kind: " << (int)type->kind << "\n";
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
        std::function<void(std::shared_ptr<ClassType>)> compute = [&](std::shared_ptr<ClassType> c) {
            if (classLayouts.count(c->name)) return;
            if (c->baseClass) compute(c->baseClass);
            
            ClassLayout layout;
            if (c->baseClass) layout = classLayouts[c->baseClass->name];
            
            for (const auto& [fname, ftype] : c->fields) {
                int offset = c->isStruct ? 0 : 1;
                layout.fieldIndices[fname] = (int)layout.allFields.size() + offset; // +1 for vptr if not struct
                layout.allFields.push_back({fname, ftype});
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
        if (name == "Date" || name == "RegExp" || name.find("Promise_") == 0 || name == "Promise") continue;
        
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
        llvm::errs() << "Setting body for class: " << name << " with " << fieldTypes.size() << " fields\n";
        classStruct->setBody(fieldTypes);

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
    for (const auto& classType : allClassTypes) {
        std::string name = classType->name;
        for (const auto& [fname, ftype] : classType->staticFields) {
            std::string globalName = name + "_" + fname;
            llvm::Type* llvmType = getLLVMType(ftype);
            new llvm::GlobalVariable(*module, llvmType, false, llvm::GlobalValue::ExternalLinkage, llvm::Constant::getNullValue(llvmType), globalName);
        }
    }
}

void IRGenerator::visitNewExpression(ast::NewExpression* node) {
    if (node->inferredType && node->inferredType->kind == TypeKind::Map) {
        llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_create", createFt);
        lastValue = createCall(createFt, fn.getCallee(), {});
        return;
    }

    if (node->inferredType && node->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(node->inferredType);
        std::string className = classType->name;

        if (className == "Date") {
            if (node->arguments.empty()) {
                llvm::FunctionType* createFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create", createFt);
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
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create_ms", createMsFt);
                    lastValue = createCall(createMsFt, fn.getCallee(), { arg });
                } else {
                    llvm::FunctionType* createStrFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::PointerType::getUnqual(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create_str", createStrFt);
                    lastValue = createCall(createStrFt, fn.getCallee(), { arg });
                }
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
                llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", createFt);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_regexp_create", createRegExpFt);
            lastValue = createCall(createRegExpFt, fn.getCallee(), { pattern, flags });
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_url_create", createUrlFt);
            lastValue = createCall(createUrlFt, fn.getCallee(), { vtable, url, base });
            return;
        }
        
        // 1. Get Struct Type
        llvm::StructType* structType = llvm::StructType::getTypeByName(*context, className);
        if (!structType) {
            llvm::errs() << "Error: Class struct not found for " << className << "\n";
            lastValue = llvm::Constant::getNullValue(llvm::PointerType::getUnqual(*context));
            return;
        }
        
        // 2. Allocate
        const llvm::DataLayout& dl = module->getDataLayout();
        
        llvm::Value* thisPtr;
        if (classType->isStruct || !node->escapes) {
            llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
            llvm::IRBuilder<> entryBuilder(&currentFunc->getEntryBlock(), currentFunc->getEntryBlock().begin());
            thisPtr = entryBuilder.CreateAlloca(structType, nullptr, className + "_stack");
            // Zero-initialize
            builder->CreateMemSet(thisPtr, builder->getInt8(0), dl.getTypeAllocSize(structType), dl.getABITypeAlign(structType));
        } else {
            llvm::FunctionType* allocFt = llvm::FunctionType::get(
                llvm::PointerType::getUnqual(*context),
                { llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee allocFn = module->getOrInsertFunction("ts_alloc", allocFt);
            
            llvm::Value* sizeVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), dl.getTypeAllocSize(structType));
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
            llvm::errs() << "Constructor not found: " << baseCtorName << "\n";
        }
        
        if (classType->isStruct) {
            lastValue = builder->CreateLoad(structType, thisPtr);
        } else {
            lastValue = thisPtr;
        }
        return;
    }

    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (id->name == "Map") {
            llvm::FunctionType* createMapFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_create", createMapFt);
            lastValue = createCall(createMapFt, fn.getCallee(), {});
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
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_create_sized", createSizedFt);
                lastValue = createCall(createSizedFt, fn.getCallee(), { size });
                return;
            } else {
                llvm::FunctionType* createArrayFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_create", createArrayFt);
                lastValue = createCall(createArrayFt, fn.getCallee(), {});
                return;
            }
        } else if (id->name == "Date") {
            if (node->arguments.empty()) {
                llvm::FunctionType* createDateFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create", createDateFt);
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
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create_ms", createMsFt);
                    lastValue = createCall(createMsFt, fn.getCallee(), { arg });
                } else {
                    llvm::FunctionType* createStrFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::PointerType::getUnqual(*context) }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create_str", createStrFt);
                    lastValue = createCall(createStrFt, fn.getCallee(), { arg });
                }
            }
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
                llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", createStrFt);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_regexp_create", createRegExpFt);
            lastValue = createCall(createRegExpFt, fn.getCallee(), { pattern, flags });
            return;
        }
    }
    lastValue = llvm::Constant::getNullValue(llvm::PointerType::getUnqual(*context));
}

void IRGenerator::visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) {
    llvm::FunctionType* createMapFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_map_create", createMapFt);
    llvm::Value* map = createCall(createMapFt, createFn.getCallee(), {});

    llvm::FunctionType* setMapFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_map_set", setMapFt);

    for (auto& prop : node->properties) {
        visit(prop->initializer.get());
        llvm::Value* val = lastValue;
        
        // Box the value
        llvm::Value* boxedVal = boxValue(val, prop->initializer->inferredType);
        
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
        llvm::Value* keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(prop->name) });
        
        createCall(setMapFt, setFn.getCallee(), { map, keyStr, boxedVal });
    }

    lastValue = map;
}

void IRGenerator::visitSuperExpression(ast::SuperExpression* node) {
    // Handled in visitCallExpression
    lastValue = nullptr;
}

} // namespace ts


