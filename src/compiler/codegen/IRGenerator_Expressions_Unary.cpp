#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

void IRGenerator::visitParenthesizedExpression(ast::ParenthesizedExpression* node) {
    visit(node->expression.get());
}

void IRGenerator::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    if (node->op == "typeof") {
        visit(node->operand.get());
        llvm::Value* val = lastValue;
        
        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createFn = getRuntimeFunction("ts_string_create", createFt);

        if (val->getType()->isDoubleTy() || val->getType()->isIntegerTy(64)) {
            llvm::Value* strPtr = builder->CreateGlobalStringPtr("number");
            lastValue = createCall(createFt, createFn.getCallee(), {strPtr});
            return;
        }
        
        if (val->getType()->isIntegerTy(1)) {
            llvm::Value* strPtr = builder->CreateGlobalStringPtr("boolean");
            lastValue = createCall(createFt, createFn.getCallee(), {strPtr});
            return;
        }

        // If statically typed as Function, return "function" directly
        if (node->operand->inferredType && node->operand->inferredType->kind == TypeKind::Function) {
            llvm::Value* strPtr = builder->CreateGlobalStringPtr("function");
            lastValue = createCall(createFt, createFn.getCallee(), {strPtr});
            return;
        }

        if (val->getType()->isPointerTy()) {
            // If it's 'any' type, use the slow path ts_value_typeof
            if (node->operand->inferredType && node->operand->inferredType->kind == TypeKind::Any) {
                llvm::FunctionType* typeofFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee typeofFn = getRuntimeFunction("ts_value_typeof", typeofFt);
                lastValue = createCall(typeofFt, typeofFn.getCallee(), {val});
                return;
            }

            llvm::FunctionType* typeofFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee typeofFn = getRuntimeFunction("ts_typeof", typeofFt);
            lastValue = createCall(typeofFt, typeofFn.getCallee(), {val});
            return;
        }

        llvm::Value* strPtr = builder->CreateGlobalStringPtr("unknown");
        lastValue = createCall(createFt, createFn.getCallee(), {strPtr});
        return;
    }

    visit(node->operand.get());
    llvm::Value* operandV = lastValue;

    if (node->op == "++" || node->op == "--") {
        // Prefix increment/decrement: increment first, then return new value
        if (auto id = dynamic_cast<ast::Identifier*>(node->operand.get())) {
            // Check if this is a cell variable (captured mutable variable in closure)
            if (cellVariables.count(id->name) && namedValues.count(id->name)) {
                SPDLOG_DEBUG("visitPrefixUnaryExpression: {} is a cell variable", id->name);
                llvm::Value* cellAlloca = namedValues[id->name];
                llvm::Value* cell = builder->CreateLoad(builder->getPtrTy(), cellAlloca);

                // Get current value from cell (returns boxed TsValue*)
                llvm::FunctionType* cellGetFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee cellGetFn = getRuntimeFunction("ts_cell_get", cellGetFt);
                llvm::Value* currentBoxed = createCall(cellGetFt, cellGetFn.getCallee(), { cell });

                // Create boxed 1 for arithmetic
                llvm::FunctionType* makeIntFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
                llvm::FunctionCallee makeIntFn = getRuntimeFunction("ts_value_make_int", makeIntFt);
                llvm::Value* oneBoxed = createCall(makeIntFt, makeIntFn.getCallee(), { llvm::ConstantInt::get(builder->getInt64Ty(), 1) });

                // Compute new value using runtime arithmetic (boxed values)
                const char* opFnName = (node->op == "++") ? "ts_value_add" : "ts_value_sub";
                llvm::FunctionType* arithFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee arithFn = getRuntimeFunction(opFnName, arithFt);
                llvm::Value* newBoxed = createCall(arithFt, arithFn.getCallee(), { currentBoxed, oneBoxed });

                // Store new value back to cell
                llvm::FunctionType* cellSetFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee cellSetFn = getRuntimeFunction("ts_cell_set", cellSetFt);
                createCall(cellSetFt, cellSetFn.getCallee(), { cell, newBoxed });

                // Prefix returns new value
                lastValue = newBoxed;
                boxedValues.insert(lastValue);
                SPDLOG_DEBUG("visitPrefixUnaryExpression: {} cell variable updated via ts_cell_set", id->name);
                return;
            }

            llvm::Value* variable = nullptr;
            if (namedValues.count(id->name)) {
                variable = namedValues[id->name];
            } else {
                variable = module->getGlobalVariable(id->name);
            }
            if (variable) {
                llvm::Value* newVal = nullptr;
                if (operandV->getType()->isPointerTy()) {
                    // Boxed 'any' / runtime value: use runtime arithmetic
                    llvm::FunctionType* makeIntFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
                    llvm::FunctionCallee makeIntFn = getRuntimeFunction("ts_value_make_int", makeIntFt);
                    llvm::Value* oneBoxed = createCall(makeIntFt, makeIntFn.getCallee(), { llvm::ConstantInt::get(builder->getInt64Ty(), 1) });

                    const char* opFnName = (node->op == "++") ? "ts_value_add" : "ts_value_sub";
                    llvm::FunctionType* arithFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee arithFn = getRuntimeFunction(opFnName, arithFt);
                    newVal = createCall(arithFt, arithFn.getCallee(), { operandV, oneBoxed });
                } else if (operandV->getType()->isDoubleTy()) {
                    llvm::Value* one = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 1.0);
                    newVal = (node->op == "++") ? builder->CreateFAdd(operandV, one) : builder->CreateFSub(operandV, one);
                } else if (operandV->getType()->isIntegerTy()) {
                    llvm::Value* one = llvm::ConstantInt::get(operandV->getType(), 1);
                    newVal = (node->op == "++") ? builder->CreateAdd(operandV, one) : builder->CreateSub(operandV, one);
                }

                if (newVal) {
                    // Cast for store if needed
                    llvm::Type* varType = nullptr;
                    if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
                        varType = alloca->getAllocatedType();
                    } else if (auto gv = llvm::dyn_cast<llvm::GlobalVariable>(variable)) {
                        varType = gv->getValueType();
                    }
                    if (varType && newVal->getType() != varType) {
                        newVal = castValue(newVal, varType);
                    }
                    builder->CreateStore(newVal, variable);
                    lastValue = newVal;  // Prefix: return new value
                    return;
                }
            }
        }
        return;
    }

    if (node->op == "-") {
        // Check for BigInt negation
        if (node->operand->inferredType && node->operand->inferredType->kind == TypeKind::BigInt) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_bigint_neg", ft);
            lastValue = createCall(ft, fn.getCallee(), { operandV });
        } else if (operandV->getType()->isDoubleTy()) {
            lastValue = builder->CreateFNeg(operandV, "negtmp");
        } else if (operandV->getType()->isIntegerTy()) {
            lastValue = builder->CreateNeg(operandV, "negtmp");
        }
    } else if (node->op == "!") {
        operandV = emitToBoolean(operandV, node->operand->inferredType);
        lastValue = builder->CreateNot(operandV, "nottmp");
    } else if (node->op == "~") {
        // Check for BigInt bitwise NOT
        if (node->operand->inferredType && node->operand->inferredType->kind == TypeKind::BigInt) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_bigint_not", ft);
            lastValue = createCall(ft, fn.getCallee(), { operandV });
        } else {
            operandV = toInt32(operandV);
            lastValue = builder->CreateNot(operandV, "bitnot");
            if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
                lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), true);
            } else {
                lastValue = castValue(lastValue, builder->getDoubleTy());
            }
        }
    }
}

void IRGenerator::visitDeleteExpression(ast::DeleteExpression* node) {
    if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node->expression.get())) {
        visit(elem->expression.get());
        llvm::Value* obj = boxValue(lastValue, elem->expression->inferredType);
        visit(elem->argumentExpression.get());
        llvm::Value* key = boxValue(lastValue, elem->argumentExpression->inferredType);
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_object_delete_prop", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, key });
        return;
    } else if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get())) {
        visit(prop->expression.get());
        llvm::Value* obj = boxValue(lastValue, prop->expression->inferredType);
        
        llvm::Value* keyStr = builder->CreateGlobalStringPtr(prop->name);
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
        llvm::Value* key = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
        key = boxValue(key, std::make_shared<Type>(TypeKind::String));

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_object_delete_prop", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, key });
        return;
    }
    lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 1);
}

void IRGenerator::visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) {
    SPDLOG_DEBUG("visitPostfixUnaryExpression: {}", node->op);
    visit(node->operand.get());
    llvm::Value* val = lastValue;

    if (!val) {
        SPDLOG_ERROR("visitPostfixUnaryExpression: operand did not produce a value");
        return;
    }

    if (node->op == "++" || node->op == "--") {
        // Check if this is a cell variable (captured mutable variable in closure)
        if (auto id = dynamic_cast<ast::Identifier*>(node->operand.get())) {
            if (cellVariables.count(id->name) && namedValues.count(id->name)) {
                SPDLOG_DEBUG("visitPostfixUnaryExpression: {} is a cell variable", id->name);
                llvm::Value* cellAlloca = namedValues[id->name];
                llvm::Value* cell = builder->CreateLoad(builder->getPtrTy(), cellAlloca);

                // Get current value from cell (returns boxed TsValue*)
                llvm::FunctionType* cellGetFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee cellGetFn = getRuntimeFunction("ts_cell_get", cellGetFt);
                llvm::Value* currentBoxed = createCall(cellGetFt, cellGetFn.getCallee(), { cell });

                // Create boxed 1 for arithmetic
                llvm::FunctionType* makeIntFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
                llvm::FunctionCallee makeIntFn = getRuntimeFunction("ts_value_make_int", makeIntFt);
                llvm::Value* oneBoxed = createCall(makeIntFt, makeIntFn.getCallee(), { llvm::ConstantInt::get(builder->getInt64Ty(), 1) });

                // Compute new value using runtime arithmetic (boxed values)
                const char* opFnName = (node->op == "++") ? "ts_value_add" : "ts_value_sub";
                llvm::FunctionType* arithFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee arithFn = getRuntimeFunction(opFnName, arithFt);
                llvm::Value* newBoxed = createCall(arithFt, arithFn.getCallee(), { currentBoxed, oneBoxed });

                // Store new value back to cell
                llvm::FunctionType* cellSetFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee cellSetFn = getRuntimeFunction("ts_cell_set", cellSetFt);
                createCall(cellSetFt, cellSetFn.getCallee(), { cell, newBoxed });

                // Postfix returns old value
                lastValue = currentBoxed;
                boxedValues.insert(lastValue);
                SPDLOG_DEBUG("visitPostfixUnaryExpression: {} cell variable updated via ts_cell_set", id->name);
                return;
            }
        }

        llvm::Value* next;
        if (val->getType()->isPointerTy()) {
            // Boxed runtime value: increment/decrement via runtime helpers.
            llvm::FunctionType* makeIntFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
            llvm::FunctionCallee makeIntFn = getRuntimeFunction("ts_value_make_int", makeIntFt);
            llvm::Value* oneBoxed = createCall(makeIntFt, makeIntFn.getCallee(), { llvm::ConstantInt::get(builder->getInt64Ty(), 1) });

            const char* opFnName = (node->op == "++") ? "ts_value_add" : "ts_value_sub";
            llvm::FunctionType* arithFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee arithFn = getRuntimeFunction(opFnName, arithFt);
            next = createCall(arithFt, arithFn.getCallee(), { val, oneBoxed });
        } else if (val->getType()->isDoubleTy()) {
            double delta = (node->op == "++") ? 1.0 : -1.0;
            next = builder->CreateFAdd(val, llvm::ConstantFP::get(*context, llvm::APFloat(delta)), "incdec");
        } else {
            int64_t delta = (node->op == "++") ? 1 : -1;
            next = builder->CreateAdd(val, llvm::ConstantInt::get(val->getType(), delta), "incdec");
        }

        // Store back
        if (auto id = dynamic_cast<ast::Identifier*>(node->operand.get())) {
            llvm::Value* variable = nullptr;
            if (namedValues.count(id->name)) {
                variable = namedValues[id->name];
            } else {
                variable = module->getGlobalVariable(id->name);
            }

            if (variable) {
                if (verbose) {
                std::string varTypeStr, nextTypeStr;
                llvm::raw_string_ostream varOs(varTypeStr), nextOs(nextTypeStr);
                variable->getType()->print(varOs);
                next->getType()->print(nextOs);
                SPDLOG_DEBUG("  Storing back to {} type: {} value type: {}", id->name, varTypeStr, nextTypeStr);
            }

                // Ensure types match for store
                llvm::Type* varType = nullptr;
                if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
                    varType = alloca->getAllocatedType();
                } else if (auto gv = llvm::dyn_cast<llvm::GlobalVariable>(variable)) {
                    varType = gv->getValueType();
                }

                if (varType && next->getType() != varType) {
                    std::string nextTypeStr, varTypeStr;
                    llvm::raw_string_ostream nextOs(nextTypeStr), varOs(varTypeStr);
                    next->getType()->print(nextOs);
                    varType->print(varOs);
                    SPDLOG_DEBUG("  Casting for store: {} -> {}", nextTypeStr, varTypeStr);
                    next = castValue(next, varType);
                }
                builder->CreateStore(next, variable);
            }
        } else if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->operand.get())) {
            // Handle property access (e.g., this.#count++ or obj.field++)
            SPDLOG_DEBUG("visitPostfixUnary PropertyAccess: prop->name={}", prop->name);
            if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
                auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
                std::string className = classType->name;
                std::string fieldName = prop->name;

                SPDLOG_DEBUG("visitPostfixUnary PropertyAccess: className={} fieldName={}", className, fieldName);

                // Handle private field name mangling
                if (fieldName.starts_with("#")) {
                    if (currentClass && currentClass->kind == TypeKind::Class) {
                        auto cls = std::static_pointer_cast<ClassType>(currentClass);
                        std::string baseName = cls->originalName.empty() ? cls->name : cls->originalName;
                        fieldName = manglePrivateName(fieldName, baseName);
                        SPDLOG_DEBUG("visitPostfixUnary: mangled to {}", fieldName);
                    }
                }

                SPDLOG_DEBUG("visitPostfixUnary: classLayouts.count({})={} fieldIndices.count({})={}",
                    className, classLayouts.count(className),
                    fieldName, classLayouts.count(className) ? classLayouts[className].fieldIndices.count(fieldName) : -1);

                // Check if it's a field access in the class layout
                if (classLayouts.count(className) && classLayouts[className].fieldIndices.count(fieldName)) {
                    // Get the object pointer
                    llvm::Value* objPtr = nullptr;
                    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                        if (namedValues.count(id->name)) {
                            objPtr = namedValues[id->name];
                            // If it's a pointer to a pointer (like 'this'), load it
                            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(objPtr)) {
                                if (alloca->getAllocatedType()->isPointerTy()) {
                                    objPtr = builder->CreateLoad(alloca->getAllocatedType(), objPtr);
                                }
                            }
                        } else {
                            objPtr = module->getGlobalVariable(id->name);
                        }
                    }

                    if (!objPtr) {
                        visit(prop->expression.get());
                        objPtr = lastValue;
                    }

                    llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
                    if (classStruct && objPtr) {
                        llvm::Value* typedObjPtr = builder->CreateBitCast(objPtr, llvm::PointerType::getUnqual(classStruct));

                        int fieldIndex = classLayouts[className].fieldIndices[fieldName];
                        llvm::Value* fieldPtr = builder->CreateStructGEP(classStruct, typedObjPtr, fieldIndex);

                        // Get the field type for proper casting
                        std::shared_ptr<Type> fieldType;
                        for (const auto& f : classLayouts[className].allFields) {
                            if (f.first == fieldName) {
                                fieldType = f.second;
                                break;
                            }
                        }

                        if (fieldType) {
                            llvm::Value* storedVal = castValue(next, getLLVMType(fieldType));
                            builder->CreateStore(storedVal, fieldPtr);
                            SPDLOG_DEBUG("visitPostfixUnary: stored value to {}.{}", className, fieldName);
                        }
                    }
                }
            }
        }
        lastValue = val; // Postfix returns old value
    }
    SPDLOG_DEBUG("visitPostfixUnaryExpression done");
}

void IRGenerator::visitAsExpression(ast::AsExpression* node) {
    visit(node->expression.get());
    
    if (node->inferredType && node->inferredType->kind == TypeKind::Any) {
        // If casting to Any, we must box the value to preserve its type info
        // especially for null/undefined which are both nullptr in IR
        lastValue = boxValue(lastValue, node->expression->inferredType);
    } else {
        lastValue = castValue(lastValue, getLLVMType(node->inferredType));
    }
}

} // namespace ts


