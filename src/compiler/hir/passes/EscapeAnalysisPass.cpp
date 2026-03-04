#include "EscapeAnalysisPass.h"
#include "../HIR.h"
#include <spdlog/spdlog.h>
#include <queue>

namespace ts::hir {

PassResult EscapeAnalysisPass::runOnFunction(HIRFunction& func) {
    // Skip async functions and generators — their stack frames are captured
    // by the async state machine / generator object, so stack allocation
    // would be unsound.
    if (func.isAsync || func.isGenerator) {
        return PassResult::unchanged();
    }

    // Clear state from previous functions
    definingInst_.clear();
    useMap_.clear();
    escapingValues_.clear();

    // Phase 1: Build def-use chains
    buildDefUseChains(func);

    // Phase 2: For each allocation instruction, check if it escapes
    bool changed = false;

    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            if (!isAllocationOpcode(inst->opcode)) continue;
            if (!inst->result) continue;

            // Already marked non-escaping by AST-level analysis — validate with SSA
            // Or start from conservative (escapes = true) and try to prove non-escaping
            HIRValue* allocVal = inst->result.get();

            // Check all uses of this allocation
            bool escapes = false;
            auto it = useMap_.find(allocVal);
            if (it != useMap_.end()) {
                for (HIRInstruction* use : it->second) {
                    if (useEscapes(use, allocVal)) {
                        escapes = true;
                        break;
                    }
                }
            }
            // No uses at all means it doesn't escape (dead allocation, but DCE handles that)

            if (!escapes && inst->escapes) {
                inst->escapes = false;
                changed = true;
                SPDLOG_DEBUG("EscapeAnalysis: {} does not escape in {}",
                    inst->result->name, func.name);
            }

            // If SSA analysis proves escape but AST-level said non-escaping, correct it
            if (escapes && !inst->escapes) {
                inst->escapes = true;
                changed = true;
                SPDLOG_DEBUG("EscapeAnalysis: {} escapes in {} (corrected from non-escaping)",
                    inst->result->name, func.name);
            }

            // Check SROA eligibility: flat object where ALL uses are
            // GetPropStatic/SetPropStatic (directly or through local alloca load).
            // This runs independently of escape analysis — if SROA is possible,
            // we also mark the object as non-escaping.
            if (inst->objectShape) {
                bool canSR = true;
                auto srIt = useMap_.find(allocVal);
                if (srIt != useMap_.end()) {
                    for (HIRInstruction* use : srIt->second) {
                        // Direct SetPropStatic/GetPropStatic with alloc as operand[0]
                        if (use->opcode == HIROpcode::SetPropStatic ||
                            use->opcode == HIROpcode::GetPropStatic) {
                            if (use->operands.size() < 1) { canSR = false; break; }
                            if (auto* v = std::get_if<std::shared_ptr<HIRValue>>(&use->operands[0])) {
                                if (v->get() != allocVal) { canSR = false; break; }
                            } else { canSR = false; break; }
                            continue;
                        }
                        // Store to local alloca: follow through loads
                        if (use->opcode == HIROpcode::Store || use->opcode == HIROpcode::GCStore) {
                            if (use->operands.size() < 2) { canSR = false; break; }
                            // Check alloc is operand[0] (the value being stored)
                            bool isAllocVal = false;
                            if (auto* v = std::get_if<std::shared_ptr<HIRValue>>(&use->operands[0])) {
                                isAllocVal = (v->get() == allocVal);
                            }
                            if (!isAllocVal) { canSR = false; break; }
                            // Check dest is a local alloca
                            HIRValue* destVal = nullptr;
                            if (auto* v = std::get_if<std::shared_ptr<HIRValue>>(&use->operands[1])) {
                                destVal = v->get();
                            }
                            if (!destVal) { canSR = false; break; }
                            auto defIt = definingInst_.find(destVal);
                            if (defIt == definingInst_.end() ||
                                defIt->second->opcode != HIROpcode::Alloca) {
                                canSR = false; break;
                            }
                            // Check all uses of the alloca: must only be Load, Store(of allocVal), or the Store we're looking at
                            auto allocaUseIt = useMap_.find(destVal);
                            if (allocaUseIt != useMap_.end()) {
                                for (HIRInstruction* allocaUse : allocaUseIt->second) {
                                    if (allocaUse->opcode == HIROpcode::Load) {
                                        // Follow through: check that all uses of the loaded value
                                        // are GetPropStatic/SetPropStatic with it as operand[0]
                                        if (!allocaUse->result) { canSR = false; break; }
                                        auto loadUseIt = useMap_.find(allocaUse->result.get());
                                        if (loadUseIt != useMap_.end()) {
                                            for (HIRInstruction* loadUse : loadUseIt->second) {
                                                if (loadUse->opcode != HIROpcode::GetPropStatic &&
                                                    loadUse->opcode != HIROpcode::SetPropStatic) {
                                                    canSR = false; break;
                                                }
                                                if (loadUse->operands.size() < 1) { canSR = false; break; }
                                                if (auto* lv = std::get_if<std::shared_ptr<HIRValue>>(&loadUse->operands[0])) {
                                                    if (lv->get() != allocaUse->result.get()) { canSR = false; break; }
                                                } else { canSR = false; break; }
                                            }
                                        }
                                        if (!canSR) break;
                                    } else if (allocaUse->opcode == HIROpcode::Store || allocaUse->opcode == HIROpcode::GCStore) {
                                        // Store into alloca is fine (it's how the var is set)
                                    } else {
                                        canSR = false; break;
                                    }
                                }
                            }
                            if (!canSR) break;
                            continue;
                        }
                        // Any other use: not SROA-eligible
                        canSR = false;
                        break;
                    }
                }
                if (canSR) {
                    inst->scalarReplaceable = true;
                    inst->escapes = false;  // SROA-eligible objects don't escape
                    changed = true;
                    SPDLOG_DEBUG("EscapeAnalysis: {} is scalar-replaceable in {}",
                        inst->result->name, func.name);
                }
            }
        }
    }

    // Phase 3: Propagate escape through Phi nodes (fixpoint)
    propagateEscapeThroughPhis(func);

    return changed ? PassResult::modified() : PassResult::unchanged();
}

void EscapeAnalysisPass::buildDefUseChains(HIRFunction& func) {
    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            // Record defining instruction for the result value
            if (inst->result) {
                definingInst_[inst->result.get()] = inst.get();
            }

            // Record uses: each operand that references an HIRValue
            for (auto& operand : inst->operands) {
                if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
                    if (*valPtr) {
                        useMap_[valPtr->get()].insert(inst.get());
                    }
                }
            }

            // Phi incoming values are also uses
            for (auto& [val, phiBlock] : inst->phiIncoming) {
                if (val) {
                    useMap_[val.get()].insert(inst.get());
                }
            }
        }
    }
}

bool EscapeAnalysisPass::isAllocationOpcode(HIROpcode op) const {
    return op == HIROpcode::NewObject ||
           op == HIROpcode::NewObjectDynamic ||
           op == HIROpcode::NewArrayBoxed;
}

bool EscapeAnalysisPass::useEscapes(HIRInstruction* use, HIRValue* allocValue) const {
    switch (use->opcode) {
        // --- Definitely escapes ---

        // Returned from function
        case HIROpcode::Return:
        case HIROpcode::AsyncReturn:
            return true;

        // Stored to a closure capture (escapes the function scope)
        case HIROpcode::StoreCapture:
        case HIROpcode::StoreCaptureFromClosure:
            return true;

        // Yield from a generator (escapes to caller)
        case HIROpcode::Yield:
        case HIROpcode::YieldStar:
            return true;

        // Throw (escapes to catch handler or caller)
        case HIROpcode::Throw:
            return true;

        // --- Conservative: assume escape for calls ---
        // The allocation is passed as an argument to a function call.
        // We can't know if the callee stores it.
        case HIROpcode::Call:
        case HIROpcode::CallMethod:
        case HIROpcode::CallVirtual:
        case HIROpcode::CallIndirect: {
            // Check if the allocation is used as an argument (not just as the receiver
            // for a method call on itself where we know the method is safe).
            // For now, conservatively: if the alloc value appears in any operand
            // of a call, it escapes.
            for (size_t i = 0; i < use->operands.size(); ++i) {
                if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&use->operands[i])) {
                    if (valPtr->get() == allocValue) {
                        // For CallMethod, operand[0] is the receiver ('this').
                        // Property get/set on 'this' don't necessarily cause escape,
                        // but a call passing it as an arg does.
                        // Conservative: any appearance in a call = escape.
                        return true;
                    }
                }
            }
            return false;
        }

        // --- Stored into another object → escapes ---
        case HIROpcode::SetPropStatic:
        case HIROpcode::SetPropDynamic: {
            // SetProp has operands: [obj, key/name, val]
            // If allocValue is the VALUE being stored (operand[2]), it escapes
            // If allocValue is the OBJECT being written to (operand[0]), it does NOT escape
            if (use->operands.size() >= 3) {
                if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&use->operands[2])) {
                    if (valPtr->get() == allocValue) {
                        return true;  // Stored as a property value
                    }
                }
            }
            return false;  // Used as the target object — doesn't cause escape
        }

        case HIROpcode::SetElem:
        case HIROpcode::SetElemTyped: {
            // SetElem has operands: [arr, idx, val]
            // If allocValue is the VALUE (operand[2]), it escapes
            if (use->operands.size() >= 3) {
                if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&use->operands[2])) {
                    if (valPtr->get() == allocValue) {
                        return true;
                    }
                }
            }
            return false;
        }

        // Store to memory (GC or regular)
        case HIROpcode::Store:
        case HIROpcode::GCStore: {
            // store %val, %ptr — if the value is our alloc, it escapes
            if (use->operands.size() >= 2) {
                if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&use->operands[0])) {
                    if (valPtr->get() == allocValue) {
                        return true;
                    }
                }
            }
            return false;
        }

        // --- Phi node: handled separately in propagation ---
        case HIROpcode::Phi:
            // Will be handled by propagateEscapeThroughPhis
            return false;

        // --- Safe uses (don't cause escape) ---
        case HIROpcode::GetPropStatic:
        case HIROpcode::GetPropDynamic:
        case HIROpcode::GetElem:
        case HIROpcode::GetElemTyped:
        case HIROpcode::Load:
        case HIROpcode::GCLoad:
        case HIROpcode::TypeOf:
        case HIROpcode::InstanceOf:
        case HIROpcode::HasProp:
        case HIROpcode::DeleteProp:
        case HIROpcode::ArrayLength:
            return false;

        // Comparisons don't cause escape (typed variants)
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
            return false;

        // Boolean/conditional checks
        case HIROpcode::CondBranch:
        case HIROpcode::LogicalNot:
        case HIROpcode::LogicalAnd:
        case HIROpcode::LogicalOr:
            return false;

        // Select/Copy propagate the value — check if the result escapes
        case HIROpcode::Select:
        case HIROpcode::Copy: {
            if (!use->result) return false;
            auto resultUseIt = useMap_.find(use->result.get());
            if (resultUseIt != useMap_.end()) {
                for (HIRInstruction* resultUse : resultUseIt->second) {
                    if (useEscapes(resultUse, use->result.get())) {
                        return true;
                    }
                }
            }
            return false;
        }

        // Boxing/unboxing don't cause escape
        case HIROpcode::BoxInt:
        case HIROpcode::BoxFloat:
        case HIROpcode::BoxBool:
        case HIROpcode::BoxString:
        case HIROpcode::BoxObject:
        case HIROpcode::UnboxInt:
        case HIROpcode::UnboxFloat:
        case HIROpcode::UnboxBool:
        case HIROpcode::UnboxString:
        case HIROpcode::UnboxObject:
        case HIROpcode::TypeCheck:
            return false;

        // Cast operations don't cause escape
        case HIROpcode::CastI64ToF64:
        case HIROpcode::CastF64ToI64:
        case HIROpcode::CastBoolToI64:
            return false;

        // Default: conservatively assume escape
        default:
            return true;
    }
}

void EscapeAnalysisPass::propagateEscapeThroughPhis(HIRFunction& func) {
    // If a Phi merges an escaping value with a non-escaping allocation,
    // the allocation must also be marked as escaping.
    // Uses a worklist-based fixpoint.

    bool changed = true;
    while (changed) {
        changed = false;

        for (auto& block : func.blocks) {
            for (auto& inst : block->instructions) {
                if (inst->opcode != HIROpcode::Phi) continue;
                if (!inst->result) continue;

                // Check if the Phi result escapes
                HIRValue* phiVal = inst->result.get();
                auto phiUseIt = useMap_.find(phiVal);
                bool phiEscapes = false;

                if (phiUseIt != useMap_.end()) {
                    for (HIRInstruction* use : phiUseIt->second) {
                        if (useEscapes(use, phiVal)) {
                            phiEscapes = true;
                            break;
                        }
                    }
                }

                if (!phiEscapes) continue;

                // The Phi result escapes — propagate back to all incoming allocations.
                // Follow through pass-through instructions (BoxObject, Copy, etc.)
                // to find the original allocation.
                for (auto& [inVal, inBlock] : inst->phiIncoming) {
                    if (!inVal) continue;

                    // Walk back through pass-through instructions to find the allocation
                    HIRValue* curVal = inVal.get();
                    HIRInstruction* defInst = nullptr;
                    for (int depth = 0; depth < 8; ++depth) {
                        auto defIt = definingInst_.find(curVal);
                        if (defIt == definingInst_.end()) break;
                        defInst = defIt->second;
                        if (isAllocationOpcode(defInst->opcode)) break;
                        // Follow through pass-through opcodes
                        if (defInst->opcode == HIROpcode::BoxObject ||
                            defInst->opcode == HIROpcode::BoxInt ||
                            defInst->opcode == HIROpcode::BoxFloat ||
                            defInst->opcode == HIROpcode::BoxBool ||
                            defInst->opcode == HIROpcode::BoxString ||
                            defInst->opcode == HIROpcode::UnboxObject ||
                            defInst->opcode == HIROpcode::Copy) {
                            // These take one input operand — follow it
                            if (!defInst->operands.empty()) {
                                if (auto* v = std::get_if<std::shared_ptr<HIRValue>>(&defInst->operands[0])) {
                                    curVal = v->get();
                                    continue;
                                }
                            }
                        }
                        break;  // Not a pass-through, stop
                    }

                    if (defInst && isAllocationOpcode(defInst->opcode) && !defInst->escapes) {
                        defInst->escapes = true;
                        changed = true;
                        SPDLOG_DEBUG("EscapeAnalysis: {} escapes via phi in {}",
                            defInst->result->name, func.name);
                    }
                }
            }
        }
    }
}

} // namespace ts::hir
