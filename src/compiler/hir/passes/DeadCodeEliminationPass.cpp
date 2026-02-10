#include "DeadCodeEliminationPass.h"
#include "../HIR.h"
#include <spdlog/spdlog.h>
#include <queue>
#include <algorithm>

namespace ts::hir {

PassResult DeadCodeEliminationPass::runOnFunction(HIRFunction& func) {
    bool changed = false;

    // Clear state from previous functions
    uses_.clear();
    liveInstructions_.clear();
    reachableBlocks_.clear();

    // Phase 0: Simplify constant conditional branches to unconditional branches
    if (simplifyConstantCondBranches(func)) {
        changed = true;
    }

    // Phase 1: Build use-def chains
    buildUseMap(func);

    // Phase 2: Find reachable blocks (for unreachable block elimination)
    findReachableBlocks(func);

    // Phase 3: Mark live instructions
    markLiveInstructions(func);

    // Phase 4: Remove unreachable blocks
    if (removeUnreachableBlocks(func)) {
        changed = true;
    }

    // Phase 5: Remove dead instructions
    if (removeDeadInstructions(func)) {
        changed = true;
    }

    return changed ? PassResult::modified() : PassResult::unchanged();
}

bool DeadCodeEliminationPass::simplifyConstantCondBranches(HIRFunction& func) {
    bool changed = false;

    for (auto& block : func.blocks) {
        if (block->instructions.empty()) continue;

        HIRInstruction* term = block->instructions.back().get();
        if (term->opcode != HIROpcode::CondBranch) continue;
        if (term->operands.size() < 3) continue;

        // Check if the condition is a constant boolean
        auto* condVal = std::get_if<std::shared_ptr<HIRValue>>(&term->operands[0]);
        if (!condVal || !*condVal) continue;

        // Find the defining instruction for the condition
        HIRInstruction* condDef = nullptr;
        for (auto& b : func.blocks) {
            for (auto& inst : b->instructions) {
                if (inst->result == *condVal) {
                    condDef = inst.get();
                    break;
                }
            }
            if (condDef) break;
        }

        if (!condDef) continue;

        // Check if it's a ConstBool instruction
        if (condDef->opcode != HIROpcode::ConstBool) continue;
        if (condDef->operands.empty()) continue;

        auto* boolVal = std::get_if<bool>(&condDef->operands[0]);
        if (!boolVal) continue;

        // Get target blocks
        auto* thenBlk = std::get_if<HIRBlock*>(&term->operands[1]);
        auto* elseBlk = std::get_if<HIRBlock*>(&term->operands[2]);
        if (!thenBlk || !elseBlk) continue;

        // Replace condbr with unconditional branch
        HIRBlock* targetBlock = *boolVal ? *thenBlk : *elseBlk;
        HIRBlock* deadBlock = *boolVal ? *elseBlk : *thenBlk;

        SPDLOG_DEBUG("DCE: Simplifying constant condbr to br: {} -> {}",
            *boolVal ? "true" : "false", targetBlock->label);

        // Replace the instruction in-place
        term->opcode = HIROpcode::Branch;
        term->operands.clear();
        term->operands.push_back(targetBlock);

        // Update block's successors list
        block->successors.clear();
        block->successors.push_back(targetBlock);

        // Remove this block from the dead block's predecessors
        auto& deadPreds = deadBlock->predecessors;
        deadPreds.erase(std::remove(deadPreds.begin(), deadPreds.end(), block.get()), deadPreds.end());

        changed = true;
    }

    return changed;
}

void DeadCodeEliminationPass::buildUseMap(HIRFunction& func) {
    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            // Check each operand for HIRValue references
            for (auto& operand : inst->operands) {
                if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
                    if (*valPtr) {
                        uses_[*valPtr].insert(inst.get());
                    }
                }
            }

            // Check phi incoming values
            for (auto& [val, block] : inst->phiIncoming) {
                if (val) {
                    uses_[val].insert(inst.get());
                }
            }
        }
    }
}

void DeadCodeEliminationPass::findReachableBlocks(HIRFunction& func) {
    if (func.blocks.empty()) return;

    // BFS from entry block
    std::queue<HIRBlock*> worklist;
    worklist.push(func.blocks[0].get());
    reachableBlocks_.insert(func.blocks[0].get());

    while (!worklist.empty()) {
        HIRBlock* block = worklist.front();
        worklist.pop();

        // Find successors from terminator
        if (block->instructions.empty()) continue;

        HIRInstruction* term = block->instructions.back().get();
        std::vector<HIRBlock*> successors;

        switch (term->opcode) {
            case HIROpcode::Branch:
                if (!term->operands.empty()) {
                    if (auto* blk = std::get_if<HIRBlock*>(&term->operands[0])) {
                        successors.push_back(*blk);
                    }
                }
                break;

            case HIROpcode::CondBranch:
                if (term->operands.size() >= 3) {
                    if (auto* thenBlk = std::get_if<HIRBlock*>(&term->operands[1])) {
                        successors.push_back(*thenBlk);
                    }
                    if (auto* elseBlk = std::get_if<HIRBlock*>(&term->operands[2])) {
                        successors.push_back(*elseBlk);
                    }
                }
                break;

            case HIROpcode::Switch:
                if (term->switchDefault) {
                    successors.push_back(term->switchDefault);
                }
                for (auto& [val, target] : term->switchCases) {
                    successors.push_back(target);
                }
                break;

            default:
                // Return, ReturnVoid, Unreachable, Throw - no successors
                break;
        }

        for (HIRBlock* succ : successors) {
            if (succ && reachableBlocks_.find(succ) == reachableBlocks_.end()) {
                reachableBlocks_.insert(succ);
                worklist.push(succ);
            }
        }
    }
}

void DeadCodeEliminationPass::markLiveInstructions(HIRFunction& func) {
    std::queue<HIRInstruction*> worklist;

    // Mark initial live instructions (terminators and side-effecting)
    for (auto& block : func.blocks) {
        // Skip unreachable blocks
        if (reachableBlocks_.find(block.get()) == reachableBlocks_.end()) {
            continue;
        }

        for (auto& inst : block->instructions) {
            if (isTerminator(inst->opcode) || hasSideEffects(inst.get())) {
                if (liveInstructions_.find(inst.get()) == liveInstructions_.end()) {
                    liveInstructions_.insert(inst.get());
                    worklist.push(inst.get());
                }
            }
        }
    }

    // Propagate liveness backwards through uses
    while (!worklist.empty()) {
        HIRInstruction* inst = worklist.front();
        worklist.pop();

        // Mark all operands as live
        for (auto& operand : inst->operands) {
            if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
                if (*valPtr) {
                    // Find the defining instruction
                    HIRInstruction* defInst = getDefiningInstruction(*valPtr, func);
                    if (defInst && liveInstructions_.find(defInst) == liveInstructions_.end()) {
                        liveInstructions_.insert(defInst);
                        worklist.push(defInst);
                    }
                }
            }
        }

        // Mark phi incoming values as live
        for (auto& [val, block] : inst->phiIncoming) {
            if (val) {
                HIRInstruction* defInst = getDefiningInstruction(val, func);
                if (defInst && liveInstructions_.find(defInst) == liveInstructions_.end()) {
                    liveInstructions_.insert(defInst);
                    worklist.push(defInst);
                }
            }
        }
    }
}

bool DeadCodeEliminationPass::hasSideEffects(HIRInstruction* inst) const {
    switch (inst->opcode) {
        // Calls may have arbitrary side effects
        case HIROpcode::Call:
        case HIROpcode::CallMethod:
        case HIROpcode::CallVirtual:
        case HIROpcode::CallIndirect:
            return true;

        // Memory writes
        case HIROpcode::Store:
        case HIROpcode::GCStore:
        case HIROpcode::SetPropStatic:
        case HIROpcode::SetPropDynamic:
        case HIROpcode::SetElem:
        case HIROpcode::SetElemTyped:
        case HIROpcode::StoreCapture:
        case HIROpcode::StoreCaptureFromClosure:
        case HIROpcode::StoreGlobal:
            return true;

        // Array mutations
        case HIROpcode::ArrayPush:
            return true;

        // Exception handling
        case HIROpcode::SetupTry:
        case HIROpcode::Throw:
        case HIROpcode::PopHandler:
        case HIROpcode::ClearException:
            return true;

        // Property deletion
        case HIROpcode::DeleteProp:
            return true;

        // GC operations
        case HIROpcode::Safepoint:
        case HIROpcode::SafepointPoll:
            return true;

        // Generator operations - yield suspends and returns a value
        case HIROpcode::Yield:
        case HIROpcode::YieldStar:
            return true;

        // Await suspends and waits for a promise
        case HIROpcode::Await:
            return true;

        default:
            return false;
    }
}

bool DeadCodeEliminationPass::isTerminator(HIROpcode opcode) const {
    switch (opcode) {
        case HIROpcode::Branch:
        case HIROpcode::CondBranch:
        case HIROpcode::Switch:
        case HIROpcode::Return:
        case HIROpcode::ReturnVoid:
        case HIROpcode::Unreachable:
        case HIROpcode::Throw:
            return true;
        default:
            return false;
    }
}

void DeadCodeEliminationPass::markLive(HIRInstruction* inst) {
    if (liveInstructions_.find(inst) != liveInstructions_.end()) {
        return;  // Already marked
    }
    liveInstructions_.insert(inst);
}

bool DeadCodeEliminationPass::removeDeadInstructions(HIRFunction& func) {
    bool changed = false;

    for (auto& block : func.blocks) {
        // Skip unreachable blocks (they'll be removed)
        if (reachableBlocks_.find(block.get()) == reachableBlocks_.end()) {
            continue;
        }

        // Remove dead instructions
        auto it = block->instructions.begin();
        while (it != block->instructions.end()) {
            HIRInstruction* inst = it->get();
            if (liveInstructions_.find(inst) == liveInstructions_.end()) {
                SPDLOG_DEBUG("DCE: Removing dead instruction: {}", inst->toString());
                it = block->instructions.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }

    return changed;
}

bool DeadCodeEliminationPass::removeUnreachableBlocks(HIRFunction& func) {
    bool changed = false;

    // First, collect all unreachable blocks
    std::set<HIRBlock*> unreachableBlocks;
    for (auto& block : func.blocks) {
        if (reachableBlocks_.find(block.get()) == reachableBlocks_.end()) {
            unreachableBlocks.insert(block.get());
        }
    }

    // Update predecessors lists of all remaining blocks to remove references to unreachable blocks
    for (auto& block : func.blocks) {
        if (unreachableBlocks.find(block.get()) != unreachableBlocks.end()) {
            continue;  // Skip unreachable blocks
        }
        auto& preds = block->predecessors;
        preds.erase(std::remove_if(preds.begin(), preds.end(),
            [&unreachableBlocks](HIRBlock* pred) {
                return unreachableBlocks.find(pred) != unreachableBlocks.end();
            }), preds.end());
    }

    // Remove unreachable blocks
    auto it = func.blocks.begin();
    while (it != func.blocks.end()) {
        HIRBlock* block = it->get();
        if (unreachableBlocks.find(block) != unreachableBlocks.end()) {
            SPDLOG_DEBUG("DCE: Removing unreachable block: {}", block->label);
            it = func.blocks.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    return changed;
}

HIRInstruction* DeadCodeEliminationPass::getDefiningInstruction(
    std::shared_ptr<HIRValue> val, HIRFunction& func) {

    // Linear search through all instructions to find the one that defines this value
    // Note: A more efficient implementation would maintain a def map
    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            if (inst->result == val) {
                return inst.get();
            }
        }
    }

    // Value might be a function parameter
    return nullptr;
}

} // namespace ts::hir
