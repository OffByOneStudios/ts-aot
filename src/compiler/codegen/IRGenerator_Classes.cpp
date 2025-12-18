#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {

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
                layout.fieldIndices[fname] = (int)layout.allFields.size() + 1; // +1 for vptr
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

        // Define VTable Type
        std::string vtableName = name + "_VTable";
        llvm::StructType* vtableStruct = llvm::StructType::create(*context, vtableName);

        // Define Class Body: { VTable*, Fields... }
        std::vector<llvm::Type*> fieldTypes;
        fieldTypes.push_back(llvm::PointerType::getUnqual(vtableStruct)); // vptr

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
            // Method signature: (this: ptr, args...) -> ret
            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(llvm::PointerType::getUnqual(*context)); // this (opaque ptr)
            for (const auto& param : methodType->paramTypes) {
                paramTypes.push_back(getLLVMType(param));
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(
                getLLVMType(methodType->returnType), paramTypes, false);
            
            vtableFieldTypes.push_back(llvm::PointerType::getUnqual(ft));

            // Find which class actually defines this method
            std::shared_ptr<ClassType> definer = classType;
            bool isAbstract = false;
            std::string mangledName;
            while (definer) {
                if (methodName.substr(0, 4) == "get_") {
                    std::string propName = methodName.substr(4);
                    if (definer->getters.count(propName)) {
                        mangledName = definer->name + "_get_" + propName;
                        if (definer->abstractMethods.count(propName)) isAbstract = true;
                        break;
                    }
                } else if (methodName.substr(0, 4) == "set_") {
                    std::string propName = methodName.substr(4);
                    if (definer->setters.count(propName)) {
                        mangledName = definer->name + "_set_" + propName;
                        if (definer->abstractMethods.count(propName)) isAbstract = true;
                        break;
                    }
                } else if (definer->methods.count(methodName)) {
                    mangledName = definer->name + "_" + methodName;
                    if (definer->abstractMethods.count(methodName)) isAbstract = true;
                    break;
                }
                definer = definer->baseClass;
            }

            if (isAbstract) {
                vtableFuncs.push_back(llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(ft)));
            } else {
                llvm::Function* methodFunc = module->getFunction(mangledName);
                if (!methodFunc) {
                    methodFunc = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, mangledName, module.get());
                }
                vtableFuncs.push_back(methodFunc);
            }
        }

        vtableStruct->setBody(vtableFieldTypes);

        // Create VTable Global
        std::string vtableGlobalName = name + "_VTable_Global";
        llvm::Constant* vtableInit = llvm::ConstantStruct::get(vtableStruct, vtableFuncs);
        new llvm::GlobalVariable(*module, vtableStruct, true, llvm::GlobalValue::ExternalLinkage, vtableInit, vtableGlobalName);
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
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_create",
            llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false));
        lastValue = builder->CreateCall(fn);
        return;
    }

    if (node->inferredType && node->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(node->inferredType);
        std::string className = classType->name;

        if (className == "Date") {
            if (node->arguments.empty()) {
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create",
                    llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false));
                lastValue = builder->CreateCall(fn);
            } else {
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy() || arg->getType()->isDoubleTy()) {
                    if (arg->getType()->isDoubleTy()) {
                        arg = builder->CreateFPToSI(arg, llvm::Type::getInt64Ty(*context));
                    }
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create_ms",
                        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::Type::getInt64Ty(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                } else {
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create_str",
                        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::PointerType::getUnqual(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
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
                llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create",
                    llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::PointerType::getUnqual(*context) }, false));
                pattern = builder->CreateCall(createFn, { emptyStr });
            }
            
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                flags = lastValue;
            } else {
                flags = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
            }
            
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_regexp_create",
                llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                    { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
            lastValue = builder->CreateCall(fn, { pattern, flags });
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
        llvm::errs() << "Allocating class: " << className << " opaque: " << structType->isOpaque() << "\n";
        uint64_t size = dl.getTypeAllocSize(structType);
        
        llvm::Function* allocFn = module->getFunction("ts_alloc");
        if (!allocFn) {
            llvm::FunctionType* ft = llvm::FunctionType::get(
                llvm::PointerType::getUnqual(*context),
                { llvm::Type::getInt64Ty(*context) }, false);
            allocFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_alloc", module.get());
        }
        
        llvm::Value* sizeVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), size);
        llvm::Value* mem = builder->CreateCall(allocFn, { sizeVal });
        llvm::Value* thisPtr = builder->CreateBitCast(mem, llvm::PointerType::getUnqual(structType));
        
        // 3. Initialize VPtr
        std::string vtableName = className + "_VTable";
        llvm::GlobalVariable* vtable = module->getGlobalVariable(vtableName + "_Global");
        if (vtable) {
            llvm::Value* vptrField = builder->CreateStructGEP(structType, thisPtr, 0);
            builder->CreateStore(vtable, vptrField);
        }
        
        // 4. Call Constructor
        std::string ctorName = className + "_constructor";
        llvm::Function* ctor = module->getFunction(ctorName);
        if (ctor) {
            llvm::errs() << "Calling constructor: " << ctorName << " with " << ctor->arg_size() << " args\n";
            std::vector<llvm::Value*> args;
            args.push_back(thisPtr);
            
            int argIdx = 0;
            for (auto& arg : node->arguments) {
                visit(arg.get());
                llvm::Value* val = lastValue;
                
                // Cast if necessary
                if (argIdx + 1 < (int)ctor->arg_size()) {
                    llvm::errs() << "Casting arg " << argIdx << " to " << argIdx + 1 << "\n";
                    val = castValue(val, ctor->getArg(argIdx + 1)->getType());
                }
                
                args.push_back(val);
                argIdx++;
            }
            
            builder->CreateCall(ctor, args);
        } else {
            llvm::errs() << "Constructor not found: " << ctorName << "\n";
        }
        
        lastValue = thisPtr;
        return;
    }

    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (id->name == "Map") {
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_create",
                llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false));
            lastValue = builder->CreateCall(fn);
            return;
        } else if (id->name == "Array") {
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                llvm::Value* size = lastValue;
                
                if (size->getType()->isDoubleTy()) {
                    size = builder->CreateFPToSI(size, llvm::Type::getInt64Ty(*context));
                }
                
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_create_sized",
                    llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                        { llvm::Type::getInt64Ty(*context) }, false));
                lastValue = builder->CreateCall(fn, { size });
                return;
            } else {
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_create",
                    llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false));
                lastValue = builder->CreateCall(fn);
                return;
            }
        } else if (id->name == "Date") {
            if (node->arguments.empty()) {
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create",
                    llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false));
                lastValue = builder->CreateCall(fn);
            } else {
                visit(node->arguments[0].get());
                llvm::Value* arg = lastValue;
                if (arg->getType()->isIntegerTy() || arg->getType()->isDoubleTy()) {
                    if (arg->getType()->isDoubleTy()) {
                        arg = builder->CreateFPToSI(arg, llvm::Type::getInt64Ty(*context));
                    }
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create_ms",
                        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::Type::getInt64Ty(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                } else {
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_date_create_str",
                        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::PointerType::getUnqual(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
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
                llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create",
                    llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::PointerType::getUnqual(*context) }, false));
                pattern = builder->CreateCall(createFn, { emptyStr });
            }
            
            if (node->arguments.size() >= 2) {
                visit(node->arguments[1].get());
                flags = lastValue;
            } else {
                flags = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
            }
            
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_regexp_create",
                llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                    { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
            lastValue = builder->CreateCall(fn, { pattern, flags });
            return;
        }
    }
    lastValue = llvm::Constant::getNullValue(llvm::PointerType::getUnqual(*context));
}

void IRGenerator::visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) {
    auto objType = std::static_pointer_cast<ObjectType>(node->inferredType);
    if (!objType) {
        llvm::errs() << "Error: Object literal has no inferred type\n";
        lastValue = nullptr;
        return;
    }

    std::vector<llvm::Type*> fieldTypes;
    for (auto& [name, type] : objType->fields) {
        // getLLVMType returns pointer for Object, which is correct for field type
        fieldTypes.push_back(getLLVMType(type));
    }
    llvm::StructType* structType = llvm::StructType::get(*context, fieldTypes);

    llvm::DataLayout dl(module.get());
    uint64_t size = dl.getTypeAllocSize(structType);
    
    llvm::Function* allocFn = module->getFunction("ts_alloc");
    if (!allocFn) {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context),
            { llvm::Type::getInt64Ty(*context) }, false);
        allocFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_alloc", module.get());
    }
    
    llvm::Value* sizeVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), size);
    llvm::Value* mem = builder->CreateCall(allocFn, { sizeVal });
    llvm::Value* ptr = builder->CreateBitCast(mem, llvm::PointerType::getUnqual(structType));

    int idx = 0;
    for (auto& [name, type] : objType->fields) {
        ast::Expression* initExpr = nullptr;
        for (auto& prop : node->properties) {
            if (prop->name == name) {
                initExpr = prop->initializer.get();
                break;
            }
        }
        
        if (initExpr) {
            visit(initExpr);
            llvm::Value* val = lastValue;
            
            llvm::Value* fieldPtr = builder->CreateStructGEP(structType, ptr, idx);
            builder->CreateStore(val, fieldPtr);
        }
        idx++;
    }

    lastValue = ptr;
}

void IRGenerator::visitSuperExpression(ast::SuperExpression* node) {
    // Handled in visitCallExpression
    lastValue = nullptr;
}

} // namespace ts
