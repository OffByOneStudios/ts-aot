#include "TypePropagationPass.h"
#include "../HIR.h"
#include <spdlog/spdlog.h>

namespace ts::hir {

PassResult TypePropagationPass::runOnFunction(HIRFunction& func) {
    bool changed = false;

    // Clear any previous narrowing info
    narrowedTypes_.clear();

    // Run propagation until fixpoint
    int iterations = 0;
    const int maxIterations = 100;  // Prevent infinite loops

    while (iterations < maxIterations) {
        bool iterChanged = propagateTypes(func);
        if (!iterChanged) {
            break;
        }
        changed = true;
        iterations++;
    }

    if (iterations >= maxIterations) {
        SPDLOG_WARN("TypePropagation: Hit max iterations for function {}", func.name);
    } else {
        SPDLOG_DEBUG("TypePropagation: {} converged in {} iterations",
                     func.name, iterations);
    }

    return changed ? PassResult::modified() : PassResult::unchanged();
}

bool TypePropagationPass::propagateTypes(HIRFunction& func) {
    bool changed = false;

    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            // Skip instructions without results
            if (!inst->result) {
                continue;
            }

            // Handle phi nodes specially
            if (inst->opcode == HIROpcode::Phi) {
                auto mergedType = mergePhiTypes(inst.get());
                if (mergedType && (!inst->result->type ||
                    !typesCompatible(inst->result->type.get(), mergedType.get()))) {
                    SPDLOG_DEBUG("TypePropagation: Phi {} type refined to {}",
                                 inst->result->toString(), mergedType->toString());
                    inst->result->type = mergedType;
                    changed = true;
                }
                continue;
            }

            // Infer type from the instruction
            auto inferredType = inferResultType(inst.get());
            if (inferredType) {
                // Check if this is a more specific type
                bool shouldUpdate = false;

                if (!inst->result->type) {
                    shouldUpdate = true;
                } else if (inst->result->type->kind == HIRTypeKind::Any &&
                           inferredType->kind != HIRTypeKind::Any) {
                    // Refining from Any to something more specific
                    shouldUpdate = true;
                } else if (!typesCompatible(inst->result->type.get(), inferredType.get())) {
                    // Type changed
                    shouldUpdate = true;
                }

                if (shouldUpdate) {
                    SPDLOG_DEBUG("TypePropagation: {} type set to {}",
                                 inst->result->toString(), inferredType->toString());
                    inst->result->type = inferredType;
                    changed = true;
                }
            }
        }

        // Apply type narrowing for conditional branches
        if (auto* term = block->getTerminator()) {
            if (term->opcode == HIROpcode::CondBranch && term->operands.size() >= 3) {
                auto* trueBBPtr = std::get_if<HIRBlock*>(&term->operands[1]);
                auto* falseBBPtr = std::get_if<HIRBlock*>(&term->operands[2]);

                if (trueBBPtr && *trueBBPtr && falseBBPtr && *falseBBPtr) {
                    applyTypeNarrowing(term, *trueBBPtr, *falseBBPtr);
                }
            }
        }
    }

    return changed;
}

std::shared_ptr<HIRType> TypePropagationPass::inferResultType(HIRInstruction* inst) {
    switch (inst->opcode) {
        // Constants - we know the exact type
        case HIROpcode::ConstInt:
            return HIRType::makeInt64();
        case HIROpcode::ConstFloat:
            return HIRType::makeFloat64();
        case HIROpcode::ConstBool:
            return HIRType::makeBool();
        case HIROpcode::ConstString:
            return HIRType::makeString();
        case HIROpcode::ConstNull:
        case HIROpcode::ConstUndefined:
            return HIRType::makeAny();

        // Integer arithmetic
        case HIROpcode::AddI64:
        case HIROpcode::SubI64:
        case HIROpcode::MulI64:
        case HIROpcode::DivI64:
        case HIROpcode::ModI64:
        case HIROpcode::NegI64:
        case HIROpcode::AndI64:
        case HIROpcode::OrI64:
        case HIROpcode::XorI64:
        case HIROpcode::ShlI64:
        case HIROpcode::ShrI64:
        case HIROpcode::UShrI64:
        case HIROpcode::NotI64:
            return HIRType::makeInt64();

        // Float arithmetic
        case HIROpcode::AddF64:
        case HIROpcode::SubF64:
        case HIROpcode::MulF64:
        case HIROpcode::DivF64:
        case HIROpcode::ModF64:
        case HIROpcode::NegF64:
            return HIRType::makeFloat64();

        // String operations
        case HIROpcode::StringConcat:
            return HIRType::makeString();

        // Comparisons
        case HIROpcode::CmpEqI64:
        case HIROpcode::CmpNeI64:
        case HIROpcode::CmpLtI64:
        case HIROpcode::CmpLeI64:
        case HIROpcode::CmpGtI64:
        case HIROpcode::CmpGeI64:
        case HIROpcode::CmpEqF64:
        case HIROpcode::CmpNeF64:
        case HIROpcode::CmpLtF64:
        case HIROpcode::CmpLeF64:
        case HIROpcode::CmpGtF64:
        case HIROpcode::CmpGeF64:
        case HIROpcode::CmpEqPtr:
        case HIROpcode::CmpNePtr:
        case HIROpcode::LogicalAnd:
        case HIROpcode::LogicalOr:
        case HIROpcode::LogicalNot:
        case HIROpcode::TypeCheck:
        case HIROpcode::InstanceOf:
            return HIRType::makeBool();

        // Type conversions
        case HIROpcode::CastI64ToF64:
            return HIRType::makeFloat64();
        case HIROpcode::CastF64ToI64:
        case HIROpcode::CastBoolToI64:
            return HIRType::makeInt64();

        // Boxing - result is always Any (boxed TsValue*)
        case HIROpcode::BoxInt:
        case HIROpcode::BoxFloat:
        case HIROpcode::BoxBool:
        case HIROpcode::BoxString:
        case HIROpcode::BoxObject:
            return HIRType::makeAny();

        // Unboxing - result is the unboxed type
        case HIROpcode::UnboxInt:
            return HIRType::makeInt64();
        case HIROpcode::UnboxFloat:
            return HIRType::makeFloat64();
        case HIROpcode::UnboxBool:
            return HIRType::makeBool();
        case HIROpcode::UnboxString:
            return HIRType::makeString();
        case HIROpcode::UnboxObject:
            return HIRType::makeObject();

        // TypeOf returns a string
        case HIROpcode::TypeOf:
            return HIRType::makeString();

        // Memory operations
        case HIROpcode::Alloca:
            return HIRType::makePtr();
        case HIROpcode::GetElementPtr:
            return HIRType::makePtr();
        case HIROpcode::Load:
            // Load type comes from the instruction's type operand
            if (inst->operands.size() >= 1) {
                auto* typePtr = std::get_if<std::shared_ptr<HIRType>>(&inst->operands[0]);
                if (typePtr && *typePtr) {
                    return *typePtr;
                }
            }
            return nullptr;

        // Object operations
        case HIROpcode::NewObject:
        case HIROpcode::NewObjectDynamic:
            return HIRType::makeObject();
        case HIROpcode::GetPropStatic:
        case HIROpcode::GetPropDynamic:
            // After Strategy B Phase 4a, ASTToHIR sets result->type from the
            // receiver's class shape when the receiver is a typed class
            // instance. Phase 4a also ensures the type annotation operand[2]
            // matches, so lowerGetPropStatic emits the inline unbox and the
            // LLVM value at the SSA name is actually the typed thing — no
            // longer a lie.
            //
            // Preserve the existing result type set at emission time.
            // Returning nullptr from inferResultType means "no change",
            // which is the correct behavior for both:
            //   - Typed receivers (result->type is concrete from emission)
            //   - Untyped receivers (result->type is Any from emission)
            return inst->result ? inst->result->type : HIRType::makeAny();
        case HIROpcode::HasProp:
        case HIROpcode::DeleteProp:
            return HIRType::makeBool();

        // Array operations
        case HIROpcode::NewArrayBoxed:
            return HIRType::makeArray(HIRType::makeAny(), false);
        case HIROpcode::NewArrayTyped:
            // Type comes from operand
            if (inst->operands.size() >= 1) {
                auto* typePtr = std::get_if<std::shared_ptr<HIRType>>(&inst->operands[0]);
                if (typePtr && *typePtr) {
                    return HIRType::makeArray(*typePtr, true);
                }
            }
            return HIRType::makeArray(HIRType::makeAny(), false);
        case HIROpcode::GetElem:
            // Element type from array
            if (inst->operands.size() >= 1) {
                auto* arrPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0]);
                if (arrPtr && *arrPtr && (*arrPtr)->type &&
                    (*arrPtr)->type->kind == HIRTypeKind::Array &&
                    (*arrPtr)->type->elementType) {
                    return (*arrPtr)->type->elementType;
                }
            }
            return HIRType::makeAny();
        case HIROpcode::ArrayLength:
            return HIRType::makeInt64();
        case HIROpcode::ArrayPush:
            return HIRType::makeInt64();  // Returns new length

        // Closures
        case HIROpcode::MakeClosure:
            return HIRType::makePtr();  // Closure pointer
        case HIROpcode::LoadCapture:
            // Type is already set correctly in ASTToHIR based on the captured variable's type
            // Return nullptr to preserve the existing type
            return nullptr;

        // Select
        case HIROpcode::Select:
            // Type is join of true and false values
            if (inst->operands.size() >= 3) {
                auto* truePtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[1]);
                auto* falsePtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[2]);
                if (truePtr && *truePtr && falsePtr && *falsePtr) {
                    return joinTypes((*truePtr)->type, (*falsePtr)->type);
                }
            }
            return nullptr;

        // Copy
        case HIROpcode::Copy:
            if (inst->operands.size() >= 1) {
                auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0]);
                if (valPtr && *valPtr && (*valPtr)->type) {
                    return (*valPtr)->type;
                }
            }
            return nullptr;

        // GC operations
        case HIROpcode::GCAlloc:
        case HIROpcode::GCAllocArray:
            return HIRType::makePtr();

        // Calls - refine from the called function's return type when possible
        case HIROpcode::Call: {
            // Operand layout for Call:
            //   [0] = function name (string)
            //   [1..N] = arguments (HIRValue*)
            //
            // If the called function exists in the current module and has a
            // concrete return type, use it. This refines runtime helpers
            // (ts_array_length, ts_string_concat, etc.) that ASTToHIR may
            // have emitted with a generic Any return type.
            if (inst->operands.size() >= 1 && currentModule_) {
                auto* namePtr = std::get_if<std::string>(&inst->operands[0]);
                if (namePtr) {
                    HIRFunction* callee = currentModule_->getFunction(*namePtr);
                    if (callee && callee->returnType &&
                        callee->returnType->kind != HIRTypeKind::Any) {
                        return callee->returnType;
                    }
                }
            }
            // Preserve existing type set at emission time
            return inst->result ? inst->result->type : HIRType::makeAny();
        }
        case HIROpcode::CallMethod:
        case HIROpcode::CallVirtual:
        case HIROpcode::CallIndirect:
            // These have more complex resolution paths. For now keep existing
            // emission-time type. Could be enhanced in a follow-up.
            return inst->result ? inst->result->type : HIRType::makeAny();

        // Globals
        case HIROpcode::LoadGlobal:
            // Preserve the existing type set by createLoadGlobalTyped (e.g., Any for module vars)
            // rather than overriding to Ptr, which loses type info needed for string concat etc.
            return inst->result ? inst->result->type : HIRType::makeAny();
        case HIROpcode::LoadFunction:
            return HIRType::makePtr();

        // Exception handling
        case HIROpcode::SetupTry:
            return HIRType::makeBool();
        case HIROpcode::GetException:
            return HIRType::makeAny();

        default:
            return nullptr;
    }
}

std::shared_ptr<HIRType> TypePropagationPass::mergePhiTypes(HIRInstruction* phi) {
    if (phi->phiIncoming.empty()) {
        return HIRType::makeAny();
    }

    std::shared_ptr<HIRType> merged = nullptr;

    for (const auto& [val, block] : phi->phiIncoming) {
        if (!val || !val->type) {
            // Unknown type - must use Any
            return HIRType::makeAny();
        }

        // Get effective type considering narrowing
        auto effectiveType = getEffectiveType(val, block);

        if (!merged) {
            merged = effectiveType;
        } else {
            merged = joinTypes(merged, effectiveType);
        }
    }

    return merged ? merged : HIRType::makeAny();
}

bool TypePropagationPass::typesCompatible(const HIRType* a, const HIRType* b) const {
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    // For arrays, also check element type
    if (a->kind == HIRTypeKind::Array) {
        if (!a->elementType && !b->elementType) return true;
        if (!a->elementType || !b->elementType) return false;
        return typesCompatible(a->elementType.get(), b->elementType.get());
    }

    // For classes, check class name
    if (a->kind == HIRTypeKind::Class) {
        return a->className == b->className;
    }

    return true;
}

std::shared_ptr<HIRType> TypePropagationPass::joinTypes(
    std::shared_ptr<HIRType> a, std::shared_ptr<HIRType> b) {

    if (!a) return b;
    if (!b) return a;

    // Same type kind - return that type
    if (a->kind == b->kind) {
        // For arrays, join element types
        if (a->kind == HIRTypeKind::Array) {
            auto elemJoin = joinTypes(a->elementType, b->elementType);
            return HIRType::makeArray(elemJoin, a->isTypedArray && b->isTypedArray);
        }

        // For classes, if same class return it, else widen to Object
        if (a->kind == HIRTypeKind::Class) {
            if (a->className == b->className) {
                return a;
            }
            // TODO: Could check for common base class
            return HIRType::makeObject();
        }

        return a;  // Same type
    }

    // Numeric types can merge to a number type
    if ((a->kind == HIRTypeKind::Int64 || a->kind == HIRTypeKind::Float64) &&
        (b->kind == HIRTypeKind::Int64 || b->kind == HIRTypeKind::Float64)) {
        // Float64 subsumes Int64
        return HIRType::makeFloat64();
    }

    // Object and Class can merge to Object
    if ((a->kind == HIRTypeKind::Object && b->kind == HIRTypeKind::Class) ||
        (a->kind == HIRTypeKind::Class && b->kind == HIRTypeKind::Object)) {
        return HIRType::makeObject();
    }

    // Different types - must use Any
    return HIRType::makeAny();
}

void TypePropagationPass::applyTypeNarrowing(
    HIRInstruction* condBr, HIRBlock* trueBB, HIRBlock* falseBB) {

    if (condBr->operands.empty()) return;

    auto* condPtr = std::get_if<std::shared_ptr<HIRValue>>(&condBr->operands[0]);
    if (!condPtr || !*condPtr) return;

    auto condValue = *condPtr;

    // Find the instruction that defines the condition
    // We need to search through the parent block to find the defining instruction
    HIRInstruction* condDef = nullptr;
    if (condBr->operands.size() > 0) {
        // Walk through the current block to find the instruction that produced condValue
        HIRBlock* currentBlock = trueBB->parent ? trueBB->parent->blocks[0].get() : nullptr;
        // This is a simplified search - ideally we'd have a use-def chain
        // For now, we leave condDef as nullptr and skip narrowing
    }

    // Look for typeof or instanceof patterns
    // This is a simplified implementation - a full version would track
    // the definition chain more carefully

    // Try to extract type guard info
    auto guard = extractTypeGuard(condDef);
    if (guard) {
        // Apply narrowing in the true branch
        if (!guard->isNegation) {
            narrowedTypes_[trueBB][guard->value] = guard->narrowedType;
        } else {
            // Negated guard - narrow in the false branch instead
            narrowedTypes_[falseBB][guard->value] = guard->narrowedType;
        }
    }
}

std::optional<TypePropagationPass::TypeGuard> TypePropagationPass::extractTypeGuard(
    HIRInstruction* condInst) {

    if (!condInst) return std::nullopt;

    // TypeCheck instruction: %r = typecheck %val, <type>
    if (condInst->opcode == HIROpcode::TypeCheck && condInst->operands.size() >= 2) {
        auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&condInst->operands[0]);
        auto* typePtr = std::get_if<std::shared_ptr<HIRType>>(&condInst->operands[1]);

        if (valPtr && *valPtr && typePtr && *typePtr) {
            return TypeGuard{*valPtr, *typePtr, false};
        }
    }

    // InstanceOf instruction: %r = instanceof %val, %ctor
    if (condInst->opcode == HIROpcode::InstanceOf && condInst->operands.size() >= 2) {
        auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&condInst->operands[0]);
        auto* ctorPtr = std::get_if<std::shared_ptr<HIRValue>>(&condInst->operands[1]);

        if (valPtr && *valPtr && ctorPtr && *ctorPtr) {
            // Could extract class name from constructor if available
            // For now, return Object type
            return TypeGuard{*valPtr, HIRType::makeObject(), false};
        }
    }

    return std::nullopt;
}

std::shared_ptr<HIRType> TypePropagationPass::getEffectiveType(
    std::shared_ptr<HIRValue> val, HIRBlock* block) {

    // Check if there's a narrowed type for this value in this block
    auto blockIt = narrowedTypes_.find(block);
    if (blockIt != narrowedTypes_.end()) {
        auto valIt = blockIt->second.find(val);
        if (valIt != blockIt->second.end()) {
            return valIt->second;
        }
    }

    // Return the value's declared type
    return val ? val->type : nullptr;
}

} // namespace ts::hir
