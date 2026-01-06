#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>

namespace ts {
using namespace ast;
void IRGenerator::visitBinaryExpression(ast::BinaryExpression* node) {
    SPDLOG_DEBUG("visitBinaryExpression: {}", node->op);
    if (node->op == "??") {
        SPDLOG_DEBUG("Generating ?? operator");
        visit(node->left.get());
        llvm::Value* left = lastValue;
        
        // Box left if it's a primitive, because we need to check for null/undefined
        llvm::Value* boxedLeft = boxValue(left, node->left->inferredType);
        
        llvm::FunctionType* isNullishFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee isNullishFn = getRuntimeFunction("ts_value_is_nullish", isNullishFt);
        llvm::Value* isNullish = createCall(isNullishFt, isNullishFn.getCallee(), { boxedLeft });
        
        llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* rightBB = llvm::BasicBlock::Create(*context, "nullish_right", currentFunc);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "nullish_merge", currentFunc);
        
        llvm::BasicBlock* entryBB = builder->GetInsertBlock();
        builder->CreateCondBr(isNullish, rightBB, mergeBB);
        
        builder->SetInsertPoint(rightBB);
        visit(node->right.get());
        llvm::Value* rightVal = lastValue;
        llvm::Value* boxedRight = boxValue(rightVal, node->right->inferredType);
        llvm::BasicBlock* rightEndBB = builder->GetInsertBlock();
        builder->CreateBr(mergeBB);
        
        builder->SetInsertPoint(mergeBB);
        llvm::PHINode* phi = builder->CreatePHI(boxedLeft->getType(), 2, "nullish_res");
        phi->addIncoming(boxedLeft, entryBB);
        phi->addIncoming(boxedRight, rightEndBB);
        
        boxedValues.insert(phi);
        lastValue = phi;
        return;
    }

    if (node->op == "instanceof") {
        // ... existing code ...
        lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
        return;
    }

    visit(node->left.get());
    llvm::Value* left = lastValue;

    visit(node->right.get());
    llvm::Value* right = lastValue;

    if (!left || !right) return;

    if (node->op == "in") {
        llvm::Value* boxedLeft = boxValue(left, node->left->inferredType);
        llvm::Value* boxedRight = boxValue(right, node->right->inferredType);
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_object_has_prop", ft);
        lastValue = createCall(ft, fn.getCallee(), { boxedRight, boxedLeft });
        return;
    }

    // Check if either side is 'any' - use slow path for arithmetic and comparison
    // Treat null inferredType as 'any' (can happen for some JavaScript expressions)
    bool leftIsAny = !node->left->inferredType || node->left->inferredType->kind == TypeKind::Any;
    bool rightIsAny = !node->right->inferredType || node->right->inferredType->kind == TypeKind::Any;

    if (leftIsAny || rightIsAny) {
        std::string runtimeFn = "";
        if (node->op == "+") runtimeFn = "ts_value_add";
        else if (node->op == "-") runtimeFn = "ts_value_sub";
        else if (node->op == "*") runtimeFn = "ts_value_mul";
        else if (node->op == "/") runtimeFn = "ts_value_div";
        else if (node->op == "==") runtimeFn = "ts_value_eq";
        else if (node->op == "===") runtimeFn = "ts_value_strict_eq_wrapper";
        else if (node->op == "<") runtimeFn = "ts_value_lt";
        else if (node->op == ">") runtimeFn = "ts_value_gt";
        else if (node->op == "<=") runtimeFn = "ts_value_lte";
        else if (node->op == ">=") runtimeFn = "ts_value_gte";
        
        if (runtimeFn != "") {
            llvm::Value* boxedLeft = boxValue(left, node->left->inferredType);
            llvm::Value* boxedRight = boxValue(right, node->right->inferredType);
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction(runtimeFn, ft);
            lastValue = createCall(ft, fn.getCallee(), { boxedLeft, boxedRight });
            
            // If the inferred type is Boolean (for comparisons), we must unbox the result
            if (node->inferredType && node->inferredType->kind == TypeKind::Boolean) {
                lastValue = unboxValue(lastValue, node->inferredType);
            } else {
                boxedValues.insert(lastValue);
            }
            return;
        }
    }

    // Unbox if one is a pointer and the other is not, and it's not a string operation
    bool leftIsPtr = left->getType()->isPointerTy();
    bool rightIsPtr = right->getType()->isPointerTy();
    
    // CRITICAL: Only unbox primitive types (Int, Double, Boolean) - NOT Object/Array/etc!
    auto isPrimitiveType = [](std::shared_ptr<Type> t) {
        if (!t) return false;
        return t->kind == TypeKind::Int || t->kind == TypeKind::Double || t->kind == TypeKind::Boolean;
    };
    
    if (leftIsPtr && !rightIsPtr && isPrimitiveType(node->left->inferredType)) {
        if (right->getType()->isDoubleTy()) {
            left = unboxValue(left, std::make_shared<Type>(TypeKind::Double));
        } else if (right->getType()->isIntegerTy(64)) {
            left = unboxValue(left, std::make_shared<Type>(TypeKind::Int));
        } else if (right->getType()->isIntegerTy(1)) {
            left = unboxValue(left, std::make_shared<Type>(TypeKind::Boolean));
        }
    }
    if (rightIsPtr && !leftIsPtr && isPrimitiveType(node->right->inferredType)) {
        if (left->getType()->isDoubleTy()) {
            right = unboxValue(right, std::make_shared<Type>(TypeKind::Double));
        } else if (left->getType()->isIntegerTy(64)) {
            right = unboxValue(right, std::make_shared<Type>(TypeKind::Int));
        } else if (left->getType()->isIntegerTy(1)) {
            right = unboxValue(right, std::make_shared<Type>(TypeKind::Boolean));
        }
    }

    bool leftIsString = node->left->inferredType && node->left->inferredType->kind == TypeKind::String;
    bool rightIsString = node->right->inferredType && node->right->inferredType->kind == TypeKind::String;
    bool isString = leftIsString || rightIsString;

    if (leftIsPtr && rightIsPtr) {
        // Both are pointers. If it's an arithmetic or bitwise operation, we MUST unbox.
        // EXCEPT if it's a string concatenation!
        if ((node->op == "+" && !isString) || node->op == "-" || node->op == "*" || node->op == "/" || node->op == "%" ||
            node->op == "&" || node->op == "|" || node->op == "^" || node->op == "<<" || node->op == ">>" || node->op == ">>>" ||
            (node->op == "+=" && !isString) || node->op == "-=" || node->op == "*=" || node->op == "/=" || node->op == "%=" ||
            node->op == "&=" || node->op == "|=" || node->op == "^=" || node->op == "<<=" || node->op == ">>=" || node->op == ">>>=") {
             
             auto targetType = std::make_shared<Type>(TypeKind::Double);
             if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
                 targetType = std::make_shared<Type>(TypeKind::Int);
             }
             
             left = unboxValue(left, targetType);
             right = unboxValue(right, targetType);
        }
    }

    bool leftIsDouble = left->getType()->isDoubleTy();
    bool rightIsDouble = right->getType()->isDoubleTy();
    bool isDouble = leftIsDouble || rightIsDouble;
    
    if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
        isDouble = false;
    }

    if (isDouble && !isString) {
        if (!leftIsDouble) left = castValue(left, llvm::Type::getDoubleTy(*context));
        if (!rightIsDouble) right = castValue(right, llvm::Type::getDoubleTy(*context));
    } else if (!isString) {
        // Ensure both are integers if we are in integer mode
        if (left->getType()->isDoubleTy()) left = castValue(left, llvm::Type::getInt64Ty(*context));
        if (right->getType()->isDoubleTy()) right = castValue(right, llvm::Type::getInt64Ty(*context));
    }

    if (node->op == "+" || node->op == "+=") {
        if (isString) {
             // Convert non-string operands to string
             if (!leftIsString) {
                 if (left->getType()->isIntegerTy(1)) {
                     llvm::FunctionType* fromBoolFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt1Ty(*context) }, false);
                     llvm::FunctionCallee fromBool = getRuntimeFunction("ts_string_from_bool", fromBoolFt);
                     left = createCall(fromBoolFt, fromBool.getCallee(), { left });
                 } else if (left->getType()->isIntegerTy()) {
                     llvm::FunctionType* fromIntFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
                     llvm::FunctionCallee fromInt = getRuntimeFunction("ts_string_from_int", fromIntFt);
                     left = createCall(fromIntFt, fromInt.getCallee(), { left });
                 } else if (left->getType()->isDoubleTy()) {
                     llvm::FunctionType* fromDoubleFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getDoubleTy(*context) }, false);
                     llvm::FunctionCallee fromDouble = getRuntimeFunction("ts_string_from_double", fromDoubleFt);
                     left = createCall(fromDoubleFt, fromDouble.getCallee(), { left });
                 } else if (left->getType()->isPointerTy()) {
                     // Assume it's a TsValue*
                     llvm::FunctionType* fromValueFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                     llvm::FunctionCallee fromValue = getRuntimeFunction("ts_string_from_value", fromValueFt);
                     left = createCall(fromValueFt, fromValue.getCallee(), { left });
                 }
             }
             if (!rightIsString) {
                 if (right->getType()->isIntegerTy(1)) {
                     llvm::FunctionType* fromBoolFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt1Ty(*context) }, false);
                     llvm::FunctionCallee fromBool = getRuntimeFunction("ts_string_from_bool", fromBoolFt);
                     right = createCall(fromBoolFt, fromBool.getCallee(), { right });
                 } else if (right->getType()->isIntegerTy()) {
                     llvm::FunctionType* fromIntFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
                     llvm::FunctionCallee fromInt = getRuntimeFunction("ts_string_from_int", fromIntFt);
                     right = createCall(fromIntFt, fromInt.getCallee(), { right });
                 } else if (right->getType()->isDoubleTy()) {
                     llvm::FunctionType* fromDoubleFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getDoubleTy(*context) }, false);
                     llvm::FunctionCallee fromDouble = getRuntimeFunction("ts_string_from_double", fromDoubleFt);
                     right = createCall(fromDoubleFt, fromDouble.getCallee(), { right });
                 } else if (right->getType()->isPointerTy()) {
                     // Assume it's a TsValue*
                     llvm::FunctionType* fromValueFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                     llvm::FunctionCallee fromValue = getRuntimeFunction("ts_string_from_value", fromValueFt);
                     right = createCall(fromValueFt, fromValue.getCallee(), { right });
                 }
             }

             llvm::FunctionType* concatFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
             llvm::FunctionCallee concatFn = getRuntimeFunction("ts_string_concat", concatFt);
             lastValue = createCall(concatFt, concatFn.getCallee(), { left, right });
        } else if (isDouble) {
            lastValue = builder->CreateFAdd(left, right, "addtmp");
        } else {
            // SAFETY: If either operand is still a pointer, use runtime ts_value_add
            if (left->getType()->isPointerTy() || right->getType()->isPointerTy()) {
                llvm::Value* boxedLeft = boxValue(left, node->left->inferredType);
                llvm::Value* boxedRight = boxValue(right, node->right->inferredType);
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_value_add", ft);
                lastValue = createCall(ft, fn.getCallee(), { boxedLeft, boxedRight });
                boxedValues.insert(lastValue);
            } else {
                lastValue = builder->CreateAdd(left, right, "addtmp");
            }
        }
    } else if (node->op == "-" || node->op == "-=") {
        // SAFETY: If either operand is still a pointer, use runtime ts_value_sub
        if (left->getType()->isPointerTy() || right->getType()->isPointerTy()) {
            llvm::Value* boxedLeft = boxValue(left, node->left->inferredType);
            llvm::Value* boxedRight = boxValue(right, node->right->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_value_sub", ft);
            lastValue = createCall(ft, fn.getCallee(), { boxedLeft, boxedRight });
            boxedValues.insert(lastValue);
        } else if (isDouble) {
            lastValue = builder->CreateFSub(left, right, "subtmp");
        } else {
            lastValue = builder->CreateSub(left, right, "subtmp");
        }
    } else if (node->op == "*" || node->op == "*=") {
        // SAFETY: If either operand is still a pointer, use runtime
        if (left->getType()->isPointerTy() || right->getType()->isPointerTy()) {
            llvm::Value* boxedLeft = boxValue(left, node->left->inferredType);
            llvm::Value* boxedRight = boxValue(right, node->right->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_value_mul", ft);
            lastValue = createCall(ft, fn.getCallee(), { boxedLeft, boxedRight });
            boxedValues.insert(lastValue);
        } else if (isDouble) {
            lastValue = builder->CreateFMul(left, right, "multmp");
        } else {
            lastValue = builder->CreateMul(left, right, "multmp");
        }
    } else if (node->op == "/" || node->op == "/=") {
        // SAFETY: If either operand is still a pointer, use runtime
        if (left->getType()->isPointerTy() || right->getType()->isPointerTy()) {
            llvm::Value* boxedLeft = boxValue(left, node->left->inferredType);
            llvm::Value* boxedRight = boxValue(right, node->right->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_value_div", ft);
            lastValue = createCall(ft, fn.getCallee(), { boxedLeft, boxedRight });
            boxedValues.insert(lastValue);
        } else {
            // In TypeScript, division is always floating point
            if (!left->getType()->isDoubleTy()) left = castValue(left, llvm::Type::getDoubleTy(*context));
            if (!right->getType()->isDoubleTy()) right = castValue(right, llvm::Type::getDoubleTy(*context));
            lastValue = builder->CreateFDiv(left, right, "divtmp");
        }
    } else if (node->op == "%" || node->op == "%=") {
        // SAFETY: If either operand is still a pointer, use runtime
        if (left->getType()->isPointerTy() || right->getType()->isPointerTy()) {
            llvm::Value* boxedLeft = boxValue(left, node->left->inferredType);
            llvm::Value* boxedRight = boxValue(right, node->right->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_value_mod", ft);
            lastValue = createCall(ft, fn.getCallee(), { boxedLeft, boxedRight });
            boxedValues.insert(lastValue);
        } else if (isDouble) {
            lastValue = builder->CreateFRem(left, right, "remtmp");
        } else {
            // In JS, % can return double even for integers if operands are large, 
            // but for our purposes, we'll stick to SRem if both are integers.
            lastValue = builder->CreateSRem(left, right, "remtmp");
        }
    } else if (node->op == "<<" || node->op == "<<=") {
        left = toInt32(left);
        right = toInt32(right);
        lastValue = builder->CreateShl(left, right, "shltmp");
        if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
            lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), true);
        } else {
            lastValue = castValue(lastValue, builder->getDoubleTy());
        }
    } else if (node->op == ">>" || node->op == ">>=") {
        left = toInt32(left);
        right = toInt32(right);
        lastValue = builder->CreateAShr(left, right, "ashrtmp");
        if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
            lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), true);
        } else {
            lastValue = castValue(lastValue, builder->getDoubleTy());
        }
    } else if (node->op == ">>>" || node->op == ">>>=") {
        left = toUint32(left);
        right = toUint32(right);
        lastValue = builder->CreateLShr(left, right, "lshrtmp");
        if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
            lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), false);
        } else {
            lastValue = builder->CreateUIToFP(lastValue, builder->getDoubleTy());
        }
    } else if (node->op == "<") {
        if (isDouble) lastValue = builder->CreateFCmpOLT(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSLT(left, right, "cmptmp");
    } else if (node->op == "<=") {
        if (isDouble) lastValue = builder->CreateFCmpOLE(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSLE(left, right, "cmptmp");
    } else if (node->op == ">") {
        if (isDouble) lastValue = builder->CreateFCmpOGT(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSGT(left, right, "cmptmp");
    } else if (node->op == ">=") {
        if (isDouble) lastValue = builder->CreateFCmpOGE(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSGE(left, right, "cmptmp");
    } else if (node->op == "==" || node->op == "===") {
        Type* leftType = node->left->inferredType.get();
        Type* rightType = node->right->inferredType.get();

        bool leftIsAny = leftType && leftType->kind == TypeKind::Any;
        bool rightIsAny = rightType && rightType->kind == TypeKind::Any;
        bool leftIsNullish = leftType && (leftType->kind == TypeKind::Null || leftType->kind == TypeKind::Undefined);
        bool rightIsNullish = rightType && (rightType->kind == TypeKind::Null || rightType->kind == TypeKind::Undefined);

        // If types are different, or either is 'any' or nullish, use slow path
        if (leftIsAny || rightIsAny || leftIsNullish || rightIsNullish || (leftType && rightType && leftType->kind != rightType->kind)) {
            llvm::Value* boxedLeft = boxValue(left, node->left->inferredType);
            llvm::Value* boxedRight = boxValue(right, node->right->inferredType);
            
            if (node->op == "==") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_value_eq", ft);
                lastValue = createCall(ft, fn.getCallee(), { boxedLeft, boxedRight });
                
                if (node->inferredType && node->inferredType->kind == TypeKind::Boolean) {
                    lastValue = unboxValue(lastValue, node->inferredType);
                } else {
                    boxedValues.insert(lastValue);
                }
            } else {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_value_strict_eq_bool", ft);
                lastValue = createCall(ft, fn.getCallee(), { boxedLeft, boxedRight });
            }
            return;
        }

        // Same types - fast path
        if (isString) {
            if (!leftIsString) left = unboxValue(left, std::make_shared<Type>(TypeKind::String));
            if (!rightIsString) right = unboxValue(right, std::make_shared<Type>(TypeKind::String));
            
            llvm::FunctionType* eqFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee eqFn = getRuntimeFunction("ts_string_eq", eqFt);
            lastValue = createCall(eqFt, eqFn.getCallee(), { left, right });
        } else if (isDouble) {
            lastValue = builder->CreateFCmpOEQ(left, right, "cmptmp");
        } else {
            lastValue = builder->CreateICmpEQ(left, right, "cmptmp");
        }
    } else if (node->op == "!=" || node->op == "!==") {
        Type* leftType = node->left->inferredType.get();
        Type* rightType = node->right->inferredType.get();

        bool leftIsAny = leftType && leftType->kind == TypeKind::Any;
        bool rightIsAny = rightType && rightType->kind == TypeKind::Any;
        bool leftIsNullish = leftType && (leftType->kind == TypeKind::Null || leftType->kind == TypeKind::Undefined);
        bool rightIsNullish = rightType && (rightType->kind == TypeKind::Null || rightType->kind == TypeKind::Undefined);

        if (leftIsAny || rightIsAny || leftIsNullish || rightIsNullish || (leftType && rightType && leftType->kind != rightType->kind)) {
            llvm::Value* boxedLeft = boxValue(left, node->left->inferredType);
            llvm::Value* boxedRight = boxValue(right, node->right->inferredType);
            
            if (node->op == "!=") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_value_eq", ft);
                llvm::Value* res = createCall(ft, fn.getCallee(), { boxedLeft, boxedRight });
                
                // Unbox and negate
                llvm::FunctionType* getBoolFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee getBoolFn = getRuntimeFunction("ts_value_get_bool", getBoolFt);
                llvm::Value* eq = createCall(getBoolFt, getBoolFn.getCallee(), { res });
                lastValue = builder->CreateNot(eq);
            } else {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_value_strict_eq_bool", ft);
                llvm::Value* eq = createCall(ft, fn.getCallee(), { boxedLeft, boxedRight });
                lastValue = builder->CreateNot(eq);
            }
            return;
        }

        // Same types - fast path
        if (isString) {
             if (!leftIsString) left = unboxValue(left, std::make_shared<Type>(TypeKind::String));
             if (!rightIsString) right = unboxValue(right, std::make_shared<Type>(TypeKind::String));

             llvm::FunctionType* eqFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                     { builder->getPtrTy(), builder->getPtrTy() }, false);
             llvm::FunctionCallee eqFn = getRuntimeFunction("ts_string_eq", eqFt);
             llvm::Value* eq = createCall(eqFt, eqFn.getCallee(), { left, right });
             lastValue = builder->CreateNot(eq);
        } else if (isDouble) {
            lastValue = builder->CreateFCmpONE(left, right, "cmptmp");
        } else {
            lastValue = builder->CreateICmpNE(left, right, "cmptmp");
        }
    } else if (node->op == "&&" || node->op == "||") {
        // JavaScript semantics:
        //  - a && b => a if a is falsy, else b
        //  - a || b => a if a is truthy, else b
        // Implement as short-circuit control flow and return a boxed (TsValue*) result.

        llvm::Value* leftVal = left;
        llvm::Value* leftBoxed = boxValue(leftVal, node->left->inferredType);
        llvm::Value* leftCond = emitToBoolean(leftVal, node->left->inferredType);

        // CRITICAL: Capture leftBB AFTER boxing, since boxing may create new blocks
        llvm::BasicBlock* leftBB = builder->GetInsertBlock();
        
        llvm::Function* func = leftBB->getParent();
        llvm::BasicBlock* rhsBB = llvm::BasicBlock::Create(*context, node->op == "&&" ? "land.rhs" : "lor.rhs", func);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, node->op == "&&" ? "land.cont" : "lor.cont");

        if (node->op == "&&") {
            builder->CreateCondBr(leftCond, rhsBB, mergeBB);
        } else {
            builder->CreateCondBr(leftCond, mergeBB, rhsBB);
        }

        // RHS
        builder->SetInsertPoint(rhsBB);
        visit(node->right.get());
        llvm::Value* rightVal = lastValue;
        llvm::Value* rightBoxed = boxValue(rightVal, node->right->inferredType);
        llvm::BasicBlock* rhsEndBB = builder->GetInsertBlock();
        builder->CreateBr(mergeBB);

        // Merge
        func->insert(func->end(), mergeBB);
        builder->SetInsertPoint(mergeBB);

        llvm::PHINode* phi = nullptr;
        if (mergeBB->empty()) {
            phi = builder->CreatePHI(builder->getPtrTy(), 2, "condtmp");
        } else {
            phi = llvm::PHINode::Create(builder->getPtrTy(), 2, "condtmp", &mergeBB->front());
        }

        // For &&: merge gets left (falsy path) or right (truthy path)
        // For ||: merge gets left (truthy path) or right (falsy path)
        phi->addIncoming(leftBoxed, leftBB);
        phi->addIncoming(rightBoxed, rhsEndBB);
        boxedValues.insert(phi);
        lastValue = phi;
    } else if (node->op == "&" || node->op == "&=") {
        left = toInt32(left);
        right = toInt32(right);
        lastValue = builder->CreateAnd(left, right, "andtmp");
        if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
            lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), true);
        } else {
            lastValue = castValue(lastValue, builder->getDoubleTy());
        }
    } else if (node->op == "|" || node->op == "|=") {
        left = toInt32(left);
        right = toInt32(right);
        lastValue = builder->CreateOr(left, right, "ortmp");
        if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
            lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), true);
        } else {
            lastValue = castValue(lastValue, builder->getDoubleTy());
        }
    } else if (node->op == "^" || node->op == "^=") {
        left = toInt32(left);
        right = toInt32(right);
        lastValue = builder->CreateXor(left, right, "xortmp");
        if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
            lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), true);
        } else {
            lastValue = castValue(lastValue, builder->getDoubleTy());
        }
    } else if (node->op == "<<") {
        left = toInt32(left);
        right = toInt32(right);
        lastValue = builder->CreateShl(left, right, "shltmp");
        if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
            lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), true);
        } else {
            lastValue = castValue(lastValue, builder->getDoubleTy());
        }
    } else if (node->op == ">>") {
        left = toInt32(left);
        right = toInt32(right);
        lastValue = builder->CreateAShr(left, right, "ashrtmp");
        if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
            lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), true);
        } else {
            lastValue = castValue(lastValue, builder->getDoubleTy());
        }
    } else if (node->op == ">>>") {
        left = toUint32(left);
        right = toUint32(right);
        lastValue = builder->CreateLShr(left, right, "lshrtmp");
        if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
            lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), false);
        } else {
            lastValue = builder->CreateUIToFP(lastValue, builder->getDoubleTy());
        }
    }

    // Handle compound assignment store back
    if (node->op.size() > 1 && node->op.back() == '=' && node->op != "==" && node->op != "===" && node->op != "!=" && node->op != "!==" && node->op != "<=" && node->op != ">=") {
        if (auto id = dynamic_cast<ast::Identifier*>(node->left.get())) {
            llvm::Value* variable = nullptr;
            llvm::Type* varType = nullptr;

            if (namedValues.count(id->name)) {
                variable = namedValues[id->name];
                if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
                    varType = alloca->getAllocatedType();
                } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(variable)) {
                    varType = gep->getResultElementType();
                }
            } else {
                llvm::GlobalVariable* gv = module->getGlobalVariable(id->name);
                if (gv) {
                    variable = gv;
                    varType = gv->getValueType();
                }
            }

            if (variable) {
                llvm::Value* valToStore = castValue(lastValue, varType);
                builder->CreateStore(valToStore, variable);
            }
        }
    }
    SPDLOG_DEBUG("visitBinaryExpression done");
}

void IRGenerator::visitConditionalExpression(ast::ConditionalExpression* node) {
    visit(node->condition.get());
    llvm::Value* cond = lastValue;
    if (!cond) return;

    // Convert to i1 if necessary
    if (!cond->getType()->isIntegerTy(1)) {
        if (cond->getType()->isPointerTy()) {
            llvm::FunctionType* toBoolFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
            llvm::FunctionCallee toBool = getRuntimeFunction("ts_value_to_bool", toBoolFt);
            cond = createCall(toBoolFt, toBool.getCallee(), { cond });
        } else if (cond->getType()->isIntegerTy(64)) {
            cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0));
        } else if (cond->getType()->isDoubleTy()) {
            cond = builder->CreateFCmpONE(cond, llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 0.0));
        }
    }

    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "then", func);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "else");
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "ifcont");

    builder->CreateCondBr(cond, thenBB, elseBB);

    // Check if both branches have the same concrete type - if so, don't box
    bool sameType = false;
    if (node->whenTrue->inferredType && node->whenFalse->inferredType) {
        sameType = (node->whenTrue->inferredType->kind == node->whenFalse->inferredType->kind);
    }
    
    // Only avoid boxing for these concrete types that don't need boxing to be merged:
    bool avoidBoxing = sameType && node->whenTrue->inferredType && 
        (node->whenTrue->inferredType->kind == TypeKind::String ||
         node->whenTrue->inferredType->kind == TypeKind::Int ||
         node->whenTrue->inferredType->kind == TypeKind::Double ||
         node->whenTrue->inferredType->kind == TypeKind::Boolean);

    // Then
    builder->SetInsertPoint(thenBB);
    visit(node->whenTrue.get());
    llvm::Value* thenVal = lastValue;
    if (thenVal && !avoidBoxing) {
        thenVal = boxValue(thenVal, node->whenTrue->inferredType);
    }
    thenBB = builder->GetInsertBlock();
    builder->CreateBr(mergeBB);

    // Else
    func->insert(func->end(), elseBB);
    builder->SetInsertPoint(elseBB);
    visit(node->whenFalse.get());
    llvm::Value* elseVal = lastValue;
    if (elseVal && !avoidBoxing) {
        elseVal = boxValue(elseVal, node->whenFalse->inferredType);
    }
    elseBB = builder->GetInsertBlock();
    builder->CreateBr(mergeBB);

    // Merge
    func->insert(func->end(), mergeBB);
    builder->SetInsertPoint(mergeBB);

    if (thenVal && elseVal) {
        // Determine the LLVM type for the PHI node
        llvm::Type* phiType = builder->getPtrTy(); // Default to pointer
        if (avoidBoxing) {
            phiType = thenVal->getType(); // Use the unboxed type
        }
        
        llvm::PHINode* phi = nullptr;
        if (mergeBB->empty()) {
            phi = builder->CreatePHI(phiType, 2, "condtmp");
        } else {
            phi = llvm::PHINode::Create(phiType, 2, "condtmp", &mergeBB->front());
        }
        phi->addIncoming(thenVal, thenBB);
        phi->addIncoming(elseVal, elseBB);
        
        // Only mark as boxed if we actually boxed
        if (!avoidBoxing) {
            boxedValues.insert(phi);
        }
        lastValue = phi;
    } else {
        lastValue = nullptr;
    }
}

void IRGenerator::visitAssignmentExpression(ast::AssignmentExpression* node) {
    // 1. Evaluate the RHS
    visit(node->right.get());
    llvm::Value* val = lastValue;
    if (!val) return;

    // 2. Check LHS type
    if (auto id = dynamic_cast<ast::Identifier*>(node->left.get())) {
        // Check if this is a cell variable (captured and mutable)
        if (cellVariables.count(id->name) && namedValues.count(id->name)) {
            llvm::Value* cellAlloca = namedValues[id->name];
            llvm::Value* cell = builder->CreateLoad(builder->getPtrTy(), cellAlloca);
            
            // Box the value
            llvm::Value* boxedVal = boxValue(val, node->right->inferredType);
            
            // Call ts_cell_set
            llvm::FunctionType* cellSetFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee cellSetFn = getRuntimeFunction("ts_cell_set", cellSetFt);
            createCall(cellSetFt, cellSetFn.getCallee(), { cell, boxedVal });
            
            lastValue = val;
            SPDLOG_DEBUG("visitAssignmentExpression: {} is a cell variable, updated via ts_cell_set", id->name);
            return;
        }
        
        // 3. Look up the variable
        llvm::Value* variable = nullptr;
        llvm::Type* varType = nullptr;

        if (namedValues.count(id->name)) {
            variable = namedValues[id->name];
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
                varType = alloca->getAllocatedType();
            } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(variable)) {
                varType = gep->getResultElementType();
            }
        } else {
            // Check for global variable - first try mangled name for top-level vars
            llvm::GlobalVariable* gv = nullptr;
            for (const auto& symbol : analyzer->topLevelVariables) {
                if (symbol->name == id->name && !symbol->modulePath.empty()) {
                    size_t hash = std::hash<std::string>{}(symbol->modulePath);
                    std::string mangledName = symbol->name + "_" + std::to_string(hash);
                    gv = module->getGlobalVariable(mangledName);
                    if (gv) break;
                }
            }
            if (!gv) {
                // Fallback to unmangled global lookup
                gv = module->getGlobalVariable(id->name);
            }
            if (gv) {
                variable = gv;
                varType = gv->getValueType();
            }
        }

        if (!variable) {
            SPDLOG_ERROR("Error: Unknown variable name {}", id->name);
            return;
        }

        // 4. Store the value
        val = castValue(val, varType);
        builder->CreateStore(val, variable);

        if (concreteTypes.count(val)) {
            concreteTypes[variable] = concreteTypes[val];
        } else {
            concreteTypes.erase(variable);
        }

        if (!lastLengthArray.empty()) {
            lengthAliases[variable] = lastLengthArray;
            lastLengthArray = "";
        } else {
            lengthAliases.erase(variable);
        }

        // Update checkedAllocas
        if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
            if (nonNullValues.count(val)) {
                checkedAllocas.insert(alloca);
            } else {
                checkedAllocas.erase(alloca);
            }
        }
    } else if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node->left.get())) {
        // Check if this is a safe access for BCE
        bool isSafe = false;
        if (auto id = dynamic_cast<ast::Identifier*>(elem->argumentExpression.get())) {
            if (auto arrId = dynamic_cast<ast::Identifier*>(elem->expression.get())) {
                for (auto it = loopStack.rbegin(); it != loopStack.rend(); ++it) {
                    if (it->safeIndices.count(id->name) && it->safeIndices.at(id->name) == arrId->name) {
                        isSafe = true;
                        break;
                    }
                }
            }
        }

        if (elem->expression->inferredType && elem->expression->inferredType->kind == TypeKind::Object) {
            visit(elem->expression.get());
            llvm::Value* obj = lastValue;
            emitNullCheckForExpression(elem->expression.get(), obj);
            
            // Unbox object (might be boxed if loaded from global)
            if (obj->getType()->isPointerTy()) {
                llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
                obj = createCall(getObjFt, getObjFn.getCallee(), { obj });
            }
            
            visit(elem->argumentExpression.get());
            llvm::Value* key = boxValue(lastValue, elem->argumentExpression->inferredType);
            llvm::Value* boxedVal = boxValue(val, node->right->inferredType);
            
            // Use inline map set operations
            emitInlineMapSet(obj, key, boxedVal);
            lastValue = val;
            return;
        }

        // Handle 'any' type element assignment OR null inferredType (JavaScript slow-path)
        // Use dynamic set that handles both arrays and maps
        if (!elem->expression->inferredType || elem->expression->inferredType->kind == TypeKind::Any) {
            visit(elem->expression.get());
            llvm::Value* obj = boxValue(lastValue, elem->expression->inferredType ? elem->expression->inferredType : std::make_shared<Type>(TypeKind::Any));
            
            visit(elem->argumentExpression.get());
            llvm::Value* key = boxValue(lastValue, elem->argumentExpression->inferredType ? elem->argumentExpression->inferredType : std::make_shared<Type>(TypeKind::Any));
            
            llvm::Value* boxedVal = boxValue(val, node->right->inferredType ? node->right->inferredType : std::make_shared<Type>(TypeKind::Any));
            
            // Use ts_object_set_dynamic which handles both arrays and maps at runtime
            llvm::FunctionType* setFt = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*context),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee setFn = getRuntimeFunction("ts_object_set_dynamic", setFt);
            createCall(setFt, setFn.getCallee(), { obj, key, boxedVal });
            lastValue = val;
            return;
        }

        if (elem->expression->inferredType && elem->expression->inferredType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(elem->expression->inferredType);
            if (cls->name == "Buffer") {
                visit(elem->expression.get());
                llvm::Value* buf = lastValue;
                emitNullCheckForExpression(elem->expression.get(), buf);
                visit(elem->argumentExpression.get());
                llvm::Value* index = castValue(lastValue, llvm::Type::getInt64Ty(*context));
                
                // Bounds check
                if (!isSafe) {
                    llvm::StructType* tsBufferType = llvm::StructType::getTypeByName(*context, "TsBuffer");
                    llvm::Value* lengthPtr = builder->CreateStructGEP(tsBufferType, buf, 4);
                    llvm::Value* length = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthPtr);
                    emitBoundsCheck(index, length);
                }

                llvm::Value* byteVal = castValue(val, llvm::Type::getInt8Ty(*context));
                
                llvm::FunctionType* setFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt8Ty(*context) }, false);
                llvm::FunctionCallee setFn = getRuntimeFunction("ts_buffer_set", setFt);
                createCall(setFt, setFn.getCallee(), { buf, index, byteVal });
                lastValue = val;
                return;
            } else if (cls->name == "Uint8Array" || cls->name == "Uint32Array" || cls->name == "Float64Array") {
                visit(elem->expression.get());
                llvm::Value* ta = lastValue;
                emitNullCheckForExpression(elem->expression.get(), ta);
                visit(elem->argumentExpression.get());
                llvm::Value* index = castValue(lastValue, llvm::Type::getInt64Ty(*context));

                llvm::Type* llvmElemType = nullptr;
                if (cls->name == "Uint8Array") llvmElemType = llvm::Type::getInt8Ty(*context);
                else if (cls->name == "Uint32Array") llvmElemType = llvm::Type::getInt32Ty(*context);
                else if (cls->name == "Float64Array") llvmElemType = llvm::Type::getDoubleTy(*context);

                if (llvmElemType) {
                    llvm::StructType* tsTypedArrayType = llvm::StructType::getTypeByName(*context, cls->name);
                    if (!tsTypedArrayType) tsTypedArrayType = llvm::StructType::getTypeByName(*context, "TsTypedArray");

                    // Bounds check
                    if (!isSafe) {
                        llvm::Value* lengthPtr = builder->CreateStructGEP(tsTypedArrayType, ta, 3);
                        llvm::Value* length = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthPtr);
                        emitBoundsCheck(index, length);
                    }

                    llvm::Value* bufferPtrPtr = builder->CreateStructGEP(tsTypedArrayType, ta, 2);
                    llvm::Value* bufferPtr = builder->CreateLoad(builder->getPtrTy(), bufferPtrPtr);
                    
                    llvm::StructType* tsBufferType = llvm::StructType::getTypeByName(*context, "TsBuffer");
                    llvm::Value* dataPtrPtr = builder->CreateStructGEP(tsBufferType, bufferPtr, 3);
                    llvm::Value* dataPtr = builder->CreateLoad(builder->getPtrTy(), dataPtrPtr);
                    
                    llvm::Value* ptr = builder->CreateGEP(llvmElemType, dataPtr, { index });
                    
                    llvm::Value* storeVal = val;
                    if (cls->name == "Uint8Array") {
                        storeVal = toUint32(storeVal);
                        storeVal = builder->CreateTrunc(storeVal, llvm::Type::getInt8Ty(*context));
                    } else if (cls->name == "Uint32Array") {
                        storeVal = toUint32(storeVal);
                    } else if (cls->name == "Float64Array") {
                        storeVal = castValue(storeVal, llvm::Type::getDoubleTy(*context));
                    }
                    
                    builder->CreateStore(storeVal, ptr);
                }
                lastValue = val;
                return;
            }
        }

        visit(elem->expression.get());
        llvm::Value* arr = lastValue;
        emitNullCheckForExpression(elem->expression.get(), arr);
        
        // ⚠️ CRITICAL: Always unbox array for Array types before any use.
        // The array may be boxed from ts_array_create_sized or similar.
        // ts_value_get_object is idempotent for raw pointers.
        if (elem->expression->inferredType && elem->expression->inferredType->kind == TypeKind::Array) {
            llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
            arr = createCall(getObjFt, getObjFn.getCallee(), { arr });
        }
        
        visit(elem->argumentExpression.get());
        llvm::Value* index = lastValue;
        
        if (elem->expression->inferredType && elem->expression->inferredType->kind == TypeKind::Array) {
            auto arrayType = std::static_pointer_cast<ArrayType>(elem->expression->inferredType);
            auto elemType = arrayType->elementType;

            // If index is not an integer, use dynamic access
            if (index->getType()->isPointerTy() || index->getType()->isDoubleTy()) {
                llvm::Value* boxedIndex = boxValue(index, elem->argumentExpression->inferredType);
                llvm::Value* boxedVal = boxValue(val, node->right->inferredType);
                
                llvm::FunctionType* setFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                        { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee setFn = getRuntimeFunction("ts_array_set_dynamic", setFt);
                
                // arr is already unboxed above, but ts_array_set_dynamic may expect boxed - rebox it
                createCall(setFt, setFn.getCallee(), { boxValue(arr, elem->expression->inferredType), boxedIndex, boxedVal });
                lastValue = val;
                return;
            }

            index = castValue(index, llvm::Type::getInt64Ty(*context));

            bool isSpecialized = false;
            llvm::Type* llvmElemType = nullptr;

            if (elemType->kind == TypeKind::Double) {
                isSpecialized = true;
                llvmElemType = llvm::Type::getDoubleTy(*context);
            } else if (elemType->kind == TypeKind::Int) {
                isSpecialized = true;
                llvmElemType = llvm::Type::getInt64Ty(*context);
            } else if (elemType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(elemType);
                if (cls->isStruct) {
                    isSpecialized = true;
                    llvmElemType = llvm::StructType::getTypeByName(*context, cls->name);
                }
            }

            if (isSpecialized) {
                llvm::FunctionType* getPtrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee getPtrFn = getRuntimeFunction("ts_array_get_elements_ptr", getPtrFt);
                llvm::Value* elementsPtr = createCall(getPtrFt, getPtrFn.getCallee(), { arr });

                // Bounds check
                if (!isSafe) {
                    llvm::StructType* tsArrayType = llvm::StructType::getTypeByName(*context, "TsArray");
                    llvm::Value* lengthPtr = builder->CreateStructGEP(tsArrayType, arr, 2);
                    llvm::Value* length = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthPtr);
                    emitBoundsCheck(index, length);
                }

                llvm::Value* ptr = builder->CreateGEP(llvmElemType, elementsPtr, { index });
                builder->CreateStore(val, ptr);
                lastValue = val;
                return;
            }
        }
        
        index = castValue(index, llvm::Type::getInt64Ty(*context));
        
        // Box the value for inline array set
        llvm::Value* boxedVal = boxValue(val, node->right->inferredType);
        
        // ⚠️ CRITICAL: Always call ts_value_get_object to unbox the array.
        // The array may be boxed from ts_array_create_sized or similar, but
        // emitInlineArraySet expects a raw TsArray*. ts_value_get_object is
        // idempotent for raw pointers.
        llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
        llvm::Value* rawArr = createCall(getObjFt, getObjFn.getCallee(), { arr });
        
        // Use inline IR operations to avoid Windows x64 ABI issues
        // emitInlineArraySet calls __ts_array_set_inline with scalar parameters
        emitInlineArraySet(rawArr, index, boxedVal);
    } else if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->left.get())) {
        // For JavaScript slow-path: null inferredType, Any, or Function (which has dynamic properties) means dynamic object
        bool exprIsDynamic = !prop->expression->inferredType || 
                             prop->expression->inferredType->kind == TypeKind::Any ||
                             prop->expression->inferredType->kind == TypeKind::Function;
        SPDLOG_DEBUG("visitAssignmentExpression PropertyAccess: prop->name='{}' exprType={} exprIsDynamic={}", 
            prop->name, 
            prop->expression->inferredType ? (int)prop->expression->inferredType->kind : -1,
            exprIsDynamic);
        if (exprIsDynamic) {
            visit(prop->expression.get());
            // Use Any boxing here to avoid double-boxing already-boxed TsValue* values.
            llvm::Value* objBoxed = boxValue(lastValue, std::make_shared<Type>(TypeKind::Any));
            
            llvm::Value* keyStr = builder->CreateGlobalStringPtr(prop->name);
            llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
            llvm::Value* key = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
            key = boxValue(key, std::make_shared<Type>(TypeKind::String));

            llvm::Value* boxedVal = boxValue(val, node->right->inferredType);
            
            // Use object set helper so functions route through their properties map.
            emitInlineObjectSetProp(objBoxed, key, boxedVal);
            lastValue = val;
            return;
        }

        if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
            if (innerProp->name == "env") {
                if (auto id = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                    if (id->name == "process") {
                        llvm::FunctionType* setEnvFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                                { builder->getPtrTy(), builder->getPtrTy() }, false);
                        llvm::FunctionCallee setEnvFn = getRuntimeFunction("ts_process_set_env", setEnvFt);
                        
                        llvm::Value* key = builder->CreateGlobalStringPtr(prop->name);
                        llvm::Value* boxedVal = boxValue(val, node->right->inferredType);
                        createCall(setEnvFt, setEnvFn.getCallee(), { key, boxedVal });
                        lastValue = val;
                        return;
                    }
                }
            }
        }

        if (prop->expression->inferredType && (prop->expression->inferredType->kind == TypeKind::Object || prop->expression->inferredType->kind == TypeKind::Intersection)) {
            visit(prop->expression.get());
            llvm::Value* objPtr = lastValue;
            
            // ⚠️ CRITICAL: Always call ts_value_get_object for Object types.
            if (objPtr->getType()->isPointerTy()) {
                llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
                objPtr = createCall(getObjFt, getObjFn.getCallee(), { objPtr });
            }
            
            // Check if this property has a setter
            if (prop->expression->inferredType->kind == TypeKind::Object) {
                auto obj = std::static_pointer_cast<ObjectType>(prop->expression->inferredType);
                if (obj->setters.count(prop->name)) {
                    // Property has a setter - look up the setter function and call it
                    llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
                    std::string setterKey = "__setter_" + prop->name;
                    llvm::Value* keyStr = builder->CreateGlobalStringPtr(setterKey);
                    llvm::Value* keyStrPtr = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
                    llvm::Value* keyBoxed = boxValue(keyStrPtr, std::make_shared<Type>(TypeKind::String));
                    
                    // Get the setter function from the map
                    llvm::Value* setterVal = emitInlineMapGet(objPtr, keyBoxed);
                    
                    // Unbox to get raw function pointer
                    llvm::FunctionType* getFnPtrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee getFnPtrFn = getRuntimeFunction("ts_value_get_function", getFnPtrFt);
                    llvm::Value* setterFn = createCall(getFnPtrFt, getFnPtrFn.getCallee(), { setterVal });
                    
                    // For getter/setter, context is the object itself (this), not the closure context
                    llvm::Value* context = objPtr;
                    
                    // Box the value to pass to setter
                    llvm::Value* boxedVal = boxValue(val, node->right->inferredType);
                    
                    // Call the setter: (context, value) -> TsValue*
                    llvm::FunctionType* setterType = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    createCall(setterType, setterFn, { context, boxedVal });
                    
                    lastValue = val;
                    return;
                }
            }
            
            // Use Any boxing here to avoid double-boxing already-boxed TsValue* values.
            llvm::Value* objBoxed = boxValue(objPtr, std::make_shared<Type>(TypeKind::Any));
            
            llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
            llvm::Value* key = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(prop->name) });
            llvm::Value* keyBoxed = boxValue(key, std::make_shared<Type>(TypeKind::String));
            
            llvm::Value* boxedVal = boxValue(val, node->right->inferredType);
            
            // Use object set helper so functions route through their properties map.
            emitInlineObjectSetProp(objBoxed, keyBoxed, boxedVal);
            lastValue = val;
            return;
        }

        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            if (cls->name == "RegExp" && prop->name == "lastIndex") {
                visit(prop->expression.get());
                llvm::Value* re = lastValue;
                
                llvm::FunctionType* setFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_set_lastIndex", setFt);
                
                llvm::Value* indexVal = val;
                if (indexVal->getType()->isDoubleTy()) {
                    indexVal = builder->CreateFPToSI(indexVal, llvm::Type::getInt64Ty(*context));
                }
                
                createCall(setFt, fn.getCallee(), { re, indexVal });
                lastValue = val;
                return;
            }
            
            // WebSocket event handler property assignment
            if (cls->name == "WebSocket" && (prop->name == "onopen" || prop->name == "onmessage" || 
                prop->name == "onclose" || prop->name == "onerror" || prop->name == "binaryType")) {
                visit(prop->expression.get());
                llvm::Value* ws = lastValue;
                
                llvm::Value* callback = boxValue(val, node->right->inferredType);
                
                // Convert binaryType to binary_type for C API
                std::string propName = prop->name == "binaryType" ? "binary_type" : prop->name;
                std::string fnName = "ts_websocket_set_" + propName;
                llvm::FunctionType* setFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, setFt);
                
                createCall(setFt, fn.getCallee(), { ws, callback });
                lastValue = val;
                return;
            }
        }

        if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (id->name == "process" && prop->name == "exitCode") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_process_set_exit_code", ft);
                createCall(ft, fn.getCallee(), { val });
                lastValue = val;
                return;
            }
        }

        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(prop->expression->inferredType);
            if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);

                // Check if it's a static setter
                auto current = cls;
                while (current) {
                    if (current->setters.count(prop->name)) {
                        std::string implName = current->name + "_static_set_" + prop->name;
                        auto methodType = current->getters[prop->name];
                        
                        std::vector<llvm::Type*> paramTypes;
                        paramTypes.push_back(getLLVMType(methodType->returnType));
                        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), paramTypes, false);
                        
                        llvm::FunctionCallee func = module->getOrInsertFunction(implName, ft);
                        createCall(ft, func.getCallee(), { val });
                        lastValue = val;
                        return;
                    }
                    current = current->baseClass;
                }

                std::string fieldName = prop->name;
                if (fieldName.starts_with("#")) {
                    fieldName = manglePrivateName(fieldName, cls->name);
                }
                std::string mangledName = cls->name + "_" + fieldName;
                auto* gVar = module->getGlobalVariable(mangledName);
                if (gVar) {
                    // Cast if necessary
                    val = castValue(val, gVar->getValueType());
                    builder->CreateStore(val, gVar);
                    lastValue = val;
                    return;
                }
            }
        }

        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            std::string className = classType->name;
            std::string fieldName = prop->name;

            if (fieldName.starts_with("#")) {
                if (currentClass && currentClass->kind == TypeKind::Class) {
                    auto cls = std::static_pointer_cast<ClassType>(currentClass);
                    std::string baseName = cls->originalName.empty() ? cls->name : cls->originalName;
                    fieldName = manglePrivateName(fieldName, baseName);
                }
            }
            
            // Check if it's a setter
            std::string vname = "set_" + fieldName;
            if (classLayouts.count(className) && classLayouts[className].methodIndices.count(vname)) {
                visit(prop->expression.get());
                llvm::Value* objPtr = lastValue;
                
                int methodIdx = classLayouts[className].methodIndices[vname];
                
                // Load VTable pointer
                llvm::Value* vptr = builder->CreateLoad(builder->getPtrTy(), objPtr);
                
                llvm::StructType* vtableStruct = llvm::StructType::getTypeByName(*context, className + "_VTable");
                if (!vtableStruct) return;
                
                // Load function pointer from VTable
                llvm::Value* funcPtrPtr = builder->CreateStructGEP(vtableStruct, vptr, methodIdx);
                
                // Get the setter type
                std::shared_ptr<FunctionType> setterType;
                auto current = classType;
                while (current) {
                    if (current->setters.count(fieldName)) {
                        setterType = current->setters[fieldName];
                        break;
                    }
                    current = current->baseClass;
                }
                
                if (setterType) {
                    std::vector<llvm::Type*> paramTypes;
                    paramTypes.push_back(builder->getPtrTy()); // this
                    paramTypes.push_back(getLLVMType(setterType->paramTypes[0])); // value
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), paramTypes, false);
                    
                    llvm::Value* funcPtr = builder->CreateLoad(llvm::PointerType::getUnqual(ft), funcPtrPtr);

                    createCall(ft, funcPtr, { objPtr, val });
                    lastValue = val;
                    return;
                }
            }

            // Check if it's a field access
            if (classLayouts.count(className) && classLayouts[className].fieldIndices.count(fieldName)) {
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
                if (!classStruct) return;
                
                if (classType->isStruct && !objPtr->getType()->isPointerTy()) {
                    // If we have a value but need a pointer, we are in trouble for assignment
                    // unless we can find the original alloca.
                    // For now, identifier case is handled above.
                }

                llvm::Value* typedObjPtr = builder->CreateBitCast(objPtr, llvm::PointerType::getUnqual(classStruct));
                
                int fieldIndex = classLayouts[className].fieldIndices[fieldName];
                llvm::Value* fieldPtr = builder->CreateStructGEP(classStruct, typedObjPtr, fieldIndex);
                
                SPDLOG_DEBUG("Assigning to {}.{} index={}", className, fieldName, fieldIndex);

                // We need the type of the field to load it correctly
                // The field could be in a base class, so we look it up in the layout's allFields
                std::shared_ptr<Type> fieldType;
                for (const auto& f : classLayouts[className].allFields) {
                    if (f.first == fieldName) {
                        fieldType = f.second;
                        break;
                    }
                }
                
                if (fieldType) {
                    val = castValue(val, getLLVMType(fieldType));
                    builder->CreateStore(val, fieldPtr);
                    lastValue = val;
                } else {
                    lastValue = nullptr;
                }
            } else {
                SPDLOG_ERROR("Error: Field {} not found in class {}", fieldName, className);
            }
        }
    } else {
        SPDLOG_ERROR("Error: LHS of assignment must be an identifier or element access");
        return;
    }
    
    // Assignment evaluates to the value
    lastValue = val;
}

} // namespace ts


