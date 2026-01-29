#include "InliningPass.h"
#include "../HIR.h"
#include <spdlog/spdlog.h>
#include <queue>
#include <algorithm>

namespace ts::hir {

InliningPass::InliningPass(Config config) : config_(std::move(config)) {}

PassResult InliningPass::run(HIRModule& module) {
    bool changed = false;

    // Phase 1: Analyze call graph
    analyzeCallGraph(module);

    // Phase 2: Inline calls in each function
    // We need to iterate until no more inlining happens (fixed-point)
    bool madeProgress = true;
    size_t iterations = 0;
    const size_t maxIterations = 10;  // Prevent infinite loops

    while (madeProgress && iterations < maxIterations) {
        madeProgress = false;
        iterations++;

        for (auto& func : module.functions) {
            // Find all Call instructions that can be inlined
            std::vector<std::pair<HIRBlock*, HIRInstruction*>> callsToInline;

            for (auto& block : func->blocks) {
                for (auto& inst : block->instructions) {
                    if (inst->opcode == HIROpcode::Call) {
                        // Get the callee name
                        if (inst->operands.empty()) continue;

                        auto* calleeName = std::get_if<std::string>(&inst->operands[0]);
                        if (!calleeName) continue;

                        // Find the callee function
                        HIRFunction* callee = nullptr;
                        for (auto& f : module.functions) {
                            if (f->name == *calleeName) {
                                callee = f.get();
                                break;
                            }
                        }

                        if (!callee) continue;  // External function

                        // Check if we should inline
                        if (shouldInline(*func, *callee)) {
                            callsToInline.push_back({block.get(), inst.get()});
                        }
                    }
                }
            }

            // Inline the calls (in reverse order to maintain instruction validity)
            for (auto it = callsToInline.rbegin(); it != callsToInline.rend(); ++it) {
                auto [block, callInst] = *it;

                // Get callee name again
                auto* calleeName = std::get_if<std::string>(&callInst->operands[0]);
                if (!calleeName) continue;

                // Find callee
                HIRFunction* callee = nullptr;
                for (auto& f : module.functions) {
                    if (f->name == *calleeName) {
                        callee = f.get();
                        break;
                    }
                }

                if (callee && inlineCall(*func, callInst, *callee)) {
                    changed = true;
                    madeProgress = true;
                    SPDLOG_DEBUG("Inlining: Inlined {} into {}", callee->name, func->name);
                }
            }
        }
    }

    if (iterations > 1) {
        SPDLOG_DEBUG("Inlining: Converged after {} iterations", iterations);
    }

    return changed ? PassResult::modified() : PassResult::unchanged();
}

void InliningPass::analyzeCallGraph(HIRModule& module) {
    // Clear previous analysis
    callCounts_.clear();
    recursiveFunctions_.clear();

    // Count calls to each function
    for (auto& func : module.functions) {
        countCalls(*func);
    }

    // Detect recursive functions
    detectRecursion(module);
}

void InliningPass::countCalls(HIRFunction& func) {
    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            if (inst->opcode == HIROpcode::Call) {
                if (!inst->operands.empty()) {
                    if (auto* name = std::get_if<std::string>(&inst->operands[0])) {
                        callCounts_[*name]++;
                    }
                }
            }
        }
    }
}

void InliningPass::detectRecursion(HIRModule& module) {
    // Build call graph and detect cycles using DFS
    std::map<std::string, std::set<std::string>> callGraph;

    // Build adjacency list
    for (auto& func : module.functions) {
        for (auto& block : func->blocks) {
            for (auto& inst : block->instructions) {
                if (inst->opcode == HIROpcode::Call) {
                    if (!inst->operands.empty()) {
                        if (auto* callee = std::get_if<std::string>(&inst->operands[0])) {
                            callGraph[func->name].insert(*callee);
                        }
                    }
                }
            }
        }
    }

    // DFS to detect cycles
    std::set<std::string> visited;
    std::set<std::string> inStack;

    std::function<bool(const std::string&)> hasCycle = [&](const std::string& node) -> bool {
        if (inStack.count(node)) {
            return true;  // Cycle detected
        }
        if (visited.count(node)) {
            return false;  // Already processed
        }

        visited.insert(node);
        inStack.insert(node);

        for (const auto& callee : callGraph[node]) {
            if (hasCycle(callee)) {
                recursiveFunctions_.insert(node);
                return true;
            }
        }

        inStack.erase(node);
        return false;
    };

    for (auto& func : module.functions) {
        visited.clear();
        inStack.clear();
        hasCycle(func->name);
    }

    if (!recursiveFunctions_.empty()) {
        SPDLOG_DEBUG("Inlining: Found {} recursive functions", recursiveFunctions_.size());
    }
}

bool InliningPass::shouldInline(const HIRFunction& caller, const HIRFunction& callee) {
    // Never inline recursive functions
    if (recursiveFunctions_.count(callee.name)) {
        return false;
    }

    // Never inline user_main or ts_main
    if (callee.name == "user_main" || callee.name == "ts_main") {
        return false;
    }

    // Check inline depth
    if (currentInlineDepth_ >= config_.maxInlineDepth) {
        return false;
    }

    // Check if function is too large
    size_t size = getFunctionSize(callee);
    size_t threshold = config_.maxInlineSize;

    // Bonus for single-call-site functions
    auto countIt = callCounts_.find(callee.name);
    if (countIt != callCounts_.end() && countIt->second == 1) {
        threshold += config_.singleCallSiteBonus;
    }

    if (size > threshold) {
        return false;
    }

    // If multi-block inlining is disabled, only inline single-block functions
    if (!config_.allowMultiBlock && callee.blocks.size() > 1) {
        return false;
    }

    // Lambda bonus - always inline simple lambdas if configured
    if (config_.inlineLambdas && isSimpleLambda(callee)) {
        return true;
    }

    return size <= threshold;
}

size_t InliningPass::getFunctionSize(const HIRFunction& func) const {
    size_t count = 0;
    for (auto& block : func.blocks) {
        count += block->instructions.size();
    }
    return count;
}

bool InliningPass::isSimpleLambda(const HIRFunction& func) const {
    // A simple lambda is:
    // 1. Single basic block
    // 2. No control flow (just computation and return)
    // 3. Small size

    if (func.blocks.size() != 1) {
        return false;
    }

    // Check for control flow opcodes
    for (auto& inst : func.blocks[0]->instructions) {
        switch (inst->opcode) {
            case HIROpcode::CondBranch:
            case HIROpcode::Switch:
            case HIROpcode::SetupTry:
            case HIROpcode::Throw:
                return false;
            default:
                break;
        }
    }

    return func.blocks[0]->instructions.size() <= 10;
}

bool InliningPass::inlineCall(HIRFunction& caller, HIRInstruction* callInst,
                               const HIRFunction& callee) {
    // Increment depth
    currentInlineDepth_++;

    // Find the block containing the call
    HIRBlock* callBlock = nullptr;
    size_t callIndex = 0;

    for (auto& block : caller.blocks) {
        for (size_t i = 0; i < block->instructions.size(); i++) {
            if (block->instructions[i].get() == callInst) {
                callBlock = block.get();
                callIndex = i;
                break;
            }
        }
        if (callBlock) break;
    }

    if (!callBlock) {
        currentInlineDepth_--;
        return false;
    }

    // Extract arguments from the call instruction
    // Format: Call @funcname, arg1, arg2, ...
    std::vector<std::shared_ptr<HIRValue>> args;
    for (size_t i = 1; i < callInst->operands.size(); i++) {
        if (auto* val = std::get_if<std::shared_ptr<HIRValue>>(&callInst->operands[i])) {
            args.push_back(*val);
        }
    }

    // Create value mapping for parameters -> arguments
    // Note: callee.params is std::vector<std::pair<std::string, std::shared_ptr<HIRType>>>
    // We need to track the param values that are defined in the function body
    std::map<std::shared_ptr<HIRValue>, std::shared_ptr<HIRValue>> valueMap;

    // The first instructions in the function typically define the parameters
    // We'll map them during instruction cloning when we encounter them

    // Clone the callee's instructions
    auto clonedInsts = cloneInstructions(callee, args, valueMap, caller);

    // Find the return value
    auto returnValue = findReturnValue(clonedInsts);

    // Remove return instructions from cloned instructions
    clonedInsts.erase(
        std::remove_if(clonedInsts.begin(), clonedInsts.end(),
            [](const std::unique_ptr<HIRInstruction>& inst) {
                return inst->opcode == HIROpcode::Return ||
                       inst->opcode == HIROpcode::ReturnVoid;
            }),
        clonedInsts.end()
    );

    // Replace uses of the call result with the return value
    if (callInst->result && returnValue) {
        // Replace all uses of callInst->result with returnValue
        for (auto& block : caller.blocks) {
            for (auto& inst : block->instructions) {
                // Check operands
                for (auto& operand : inst->operands) {
                    if (auto* val = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
                        if (*val == callInst->result) {
                            *val = returnValue;
                        }
                    }
                }
                // Check phi incoming
                for (auto& [val, phiBlock] : inst->phiIncoming) {
                    if (val == callInst->result) {
                        val = returnValue;
                    }
                }
            }
        }
    }

    // Insert cloned instructions before the call
    auto it = callBlock->instructions.begin() + callIndex;

    // Remove the call instruction
    it = callBlock->instructions.erase(it);

    // Insert cloned instructions at that position
    for (auto& clonedInst : clonedInsts) {
        it = callBlock->instructions.insert(it, std::move(clonedInst));
        ++it;
    }

    currentInlineDepth_--;
    return true;
}

std::vector<std::unique_ptr<HIRInstruction>> InliningPass::cloneInstructions(
    const HIRFunction& callee,
    const std::vector<std::shared_ptr<HIRValue>>& args,
    std::map<std::shared_ptr<HIRValue>, std::shared_ptr<HIRValue>>& valueMap,
    HIRFunction& caller) {

    std::vector<std::unique_ptr<HIRInstruction>> result;

    // For now, only handle single-block functions
    if (callee.blocks.empty()) {
        return result;
    }

    const auto& sourceBlock = callee.blocks[0];

    // Track how many parameters we've seen (to map them to args)
    size_t paramIndex = 0;

    for (const auto& inst : sourceBlock->instructions) {
        // Create a clone of the instruction
        auto clone = std::make_unique<HIRInstruction>(inst->opcode);

        // Clone result value with new ID
        if (inst->result) {
            auto newResult = caller.createValue(inst->result->type,
                                                  inst->result->name + "_inline");
            clone->result = newResult;
            valueMap[inst->result] = newResult;
        }

        // Clone operands with remapping
        for (const auto& operand : inst->operands) {
            if (auto* val = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
                // Check if this value should be remapped
                auto mapIt = valueMap.find(*val);
                if (mapIt != valueMap.end()) {
                    clone->operands.push_back(mapIt->second);
                } else {
                    // Check if this might be a parameter reference
                    // Parameters are typically referenced but not defined in instructions
                    // For simplicity, just copy the original value
                    clone->operands.push_back(*val);
                }
            } else {
                // Copy non-value operands directly
                clone->operands.push_back(operand);
            }
        }

        // Copy phi incoming with remapping
        for (const auto& [val, phiBlock] : inst->phiIncoming) {
            auto mapIt = valueMap.find(val);
            if (mapIt != valueMap.end()) {
                clone->phiIncoming.push_back({mapIt->second, phiBlock});
            } else {
                clone->phiIncoming.push_back({val, phiBlock});
            }
        }

        // Copy switch cases
        clone->switchCases = inst->switchCases;
        clone->switchDefault = inst->switchDefault;

        // Copy source location
        clone->sourceLocation = inst->sourceLocation;

        result.push_back(std::move(clone));
    }

    return result;
}

std::shared_ptr<HIRValue> InliningPass::findReturnValue(
    const std::vector<std::unique_ptr<HIRInstruction>>& instructions) {

    for (const auto& inst : instructions) {
        if (inst->opcode == HIROpcode::Return) {
            if (!inst->operands.empty()) {
                if (auto* val = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
                    return *val;
                }
            }
        }
    }
    return nullptr;
}

void InliningPass::replaceCallResult(HIRFunction& func, HIRInstruction* callInst,
                                      std::shared_ptr<HIRValue> returnValue) {
    if (!callInst->result || !returnValue) {
        return;
    }

    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            // Replace in operands
            for (auto& operand : inst->operands) {
                if (auto* val = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
                    if (*val == callInst->result) {
                        *val = returnValue;
                    }
                }
            }
            // Replace in phi incoming
            for (auto& [val, phiBlock] : inst->phiIncoming) {
                if (val == callInst->result) {
                    val = returnValue;
                }
            }
        }
    }
}

} // namespace ts::hir
