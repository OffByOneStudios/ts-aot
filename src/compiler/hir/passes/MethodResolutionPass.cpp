#include "MethodResolutionPass.h"
#include "../HIR.h"
#include "../BuiltinRegistry.h"
#include <spdlog/spdlog.h>

namespace ts::hir {

PassResult MethodResolutionPass::runOnFunction(HIRFunction& func) {
    bool changed = false;

    for (auto& block : func.blocks) {
        // We need to iterate carefully since we're modifying the vector
        for (size_t i = 0; i < block->instructions.size(); ++i) {
            auto* inst = block->instructions[i].get();

            if (inst->opcode != HIROpcode::CallMethod) {
                continue;
            }

            auto replacement = tryResolveMethod(inst, func);
            if (replacement) {
                SPDLOG_DEBUG("MethodResolution: Resolved CallMethod to {}",
                    replacement->toString());

                // Replace the instruction
                block->instructions[i] = std::move(replacement);
                changed = true;
            }
        }
    }

    return changed ? PassResult::modified() : PassResult::unchanged();
}

std::unique_ptr<HIRInstruction> MethodResolutionPass::tryResolveMethod(
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

    // Get method name
    auto* methodNamePtr = std::get_if<std::string>(&inst->operands[1]);
    if (!methodNamePtr) {
        return nullptr;
    }
    const std::string& methodName = *methodNamePtr;

    // Collect arguments
    std::vector<std::shared_ptr<HIRValue>> args;
    for (size_t i = 2; i < inst->operands.size(); ++i) {
        auto* argPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[i]);
        if (argPtr && *argPtr) {
            args.push_back(*argPtr);
        }
    }

    // Check receiver type and query the registry
    if (!receiver->type) {
        return nullptr;
    }

    auto& registry = BuiltinRegistry::instance();
    auto resolution = registry.resolveMethod(receiver->type->kind, methodName,
                                             static_cast<int>(args.size()));

    switch (resolution.kind) {
        case MethodResolution::Kind::HIROpcode:
            return createOpcodeInstruction(inst, resolution.opcode, receiver, args);

        case MethodResolution::Kind::RuntimeCall:
            return createRuntimeCallInstruction(inst, resolution.runtimeFunction,
                                                receiver, args, resolution.returnType);

        case MethodResolution::Kind::Dynamic:
        default:
            // Leave as CallMethod
            return nullptr;
    }
}

std::unique_ptr<HIRInstruction> MethodResolutionPass::createOpcodeInstruction(
    HIRInstruction* original,
    HIROpcode opcode,
    std::shared_ptr<HIRValue> receiver,
    const std::vector<std::shared_ptr<HIRValue>>& args) {

    auto newInst = std::make_unique<HIRInstruction>(opcode);
    newInst->result = original->result;
    newInst->sourceLocation = original->sourceLocation;

    // Add receiver as first operand
    newInst->operands.push_back(receiver);

    // Add remaining arguments
    for (const auto& arg : args) {
        newInst->operands.push_back(arg);
    }

    return newInst;
}

std::unique_ptr<HIRInstruction> MethodResolutionPass::createRuntimeCallInstruction(
    HIRInstruction* original,
    const std::string& runtimeFunction,
    std::shared_ptr<HIRValue> receiver,
    const std::vector<std::shared_ptr<HIRValue>>& args,
    std::shared_ptr<HIRType> returnType) {

    auto newInst = std::make_unique<HIRInstruction>(HIROpcode::Call);
    newInst->result = original->result;
    newInst->sourceLocation = original->sourceLocation;

    // First operand is the function name
    newInst->operands.push_back(runtimeFunction);

    // Second operand is the receiver (first argument to the runtime function)
    newInst->operands.push_back(receiver);

    // Remaining operands are the method arguments
    for (const auto& arg : args) {
        newInst->operands.push_back(arg);
    }

    return newInst;
}

} // namespace ts::hir
