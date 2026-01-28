#include "BuiltinResolutionPass.h"
#include "../HIR.h"
#include "../BuiltinRegistry.h"
#include <spdlog/spdlog.h>

namespace ts::hir {

PassResult BuiltinResolutionPass::runOnFunction(HIRFunction& func) {
    bool changed = false;

    for (auto& block : func.blocks) {
        for (size_t i = 0; i < block->instructions.size(); ++i) {
            auto* inst = block->instructions[i].get();

            if (inst->opcode != HIROpcode::CallMethod) {
                continue;
            }

            auto replacement = tryResolveBuiltin(inst, func);
            if (replacement) {
                SPDLOG_DEBUG("BuiltinResolution: Resolved CallMethod to {}",
                    replacement->toString());

                block->instructions[i] = std::move(replacement);
                changed = true;
            }
        }
    }

    return changed ? PassResult::modified() : PassResult::unchanged();
}

std::unique_ptr<HIRInstruction> BuiltinResolutionPass::tryResolveBuiltin(
    HIRInstruction* inst, HIRFunction& func) {

    // CallMethod operands: [0] = receiver, [1] = methodName, [2...] = args
    if (inst->operands.size() < 2) {
        return nullptr;
    }

    // Get receiver value
    auto* receiverPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0]);
    if (!receiverPtr || !*receiverPtr) {
        return nullptr;
    }
    auto receiver = *receiverPtr;

    // Check if the receiver is a builtin global
    std::string globalName = getBuiltinGlobalName(receiver, func);
    if (globalName.empty()) {
        return nullptr;
    }

    // Get method name
    auto* methodNamePtr = std::get_if<std::string>(&inst->operands[1]);
    if (!methodNamePtr) {
        return nullptr;
    }
    const std::string& methodName = *methodNamePtr;

    // Collect arguments (skip receiver and method name)
    std::vector<std::shared_ptr<HIRValue>> args;
    for (size_t i = 2; i < inst->operands.size(); ++i) {
        auto* argPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[i]);
        if (argPtr && *argPtr) {
            args.push_back(*argPtr);
        }
    }

    // Query the registry
    auto& registry = BuiltinRegistry::instance();
    auto resolution = registry.resolveGlobalBuiltin(globalName, methodName,
                                                     static_cast<int>(args.size()));

    if (resolution.kind == BuiltinResolution::Kind::Unknown) {
        // Not a known builtin
        return nullptr;
    }

    // Create a direct Call instruction
    auto newInst = std::make_unique<HIRInstruction>(HIROpcode::Call);
    newInst->result = inst->result;
    newInst->sourceLocation = inst->sourceLocation;

    // First operand is the runtime function name
    newInst->operands.push_back(resolution.runtimeFunction);

    // Remaining operands are the arguments
    for (const auto& arg : args) {
        newInst->operands.push_back(arg);
    }

    SPDLOG_DEBUG("BuiltinResolution: {}.{}() -> {}()",
        globalName, methodName, resolution.runtimeFunction);

    return newInst;
}

std::string BuiltinResolutionPass::getBuiltinGlobalName(
    std::shared_ptr<HIRValue> value, HIRFunction& func) {

    // Find the instruction that defines this value
    HIRInstruction* defInst = findDefiningInstruction(value, func);
    if (!defInst) {
        return "";
    }

    // Check if it's a LoadGlobal instruction
    if (defInst->opcode != HIROpcode::LoadGlobal) {
        return "";
    }

    // Get the global name from the operand
    if (defInst->operands.empty()) {
        return "";
    }

    auto* namePtr = std::get_if<std::string>(&defInst->operands[0]);
    if (!namePtr) {
        return "";
    }

    const std::string& globalName = *namePtr;

    // Check if it's a known builtin global
    auto& registry = BuiltinRegistry::instance();
    if (registry.isBuiltinGlobal(globalName)) {
        return globalName;
    }

    return "";
}

HIRInstruction* BuiltinResolutionPass::findDefiningInstruction(
    std::shared_ptr<HIRValue> value, HIRFunction& func) {

    if (!value) {
        return nullptr;
    }

    // Search all blocks for an instruction that produces this value
    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            if (inst->result && inst->result->id == value->id) {
                return inst.get();
            }
        }
    }

    return nullptr;
}

} // namespace ts::hir
