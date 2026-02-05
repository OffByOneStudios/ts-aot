#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace ts::hir {

//==============================================================================
// MapSetHandler - Handles Map and Set builtin functions
//
// This handler extracts the Map and Set function lowering from the monolithic
// lowerCall function. Map and Set operations require boxing of keys/values:
// - Map: set, get, has, delete, clear, size
// - Set: add, has, delete, clear, size
//==============================================================================
class MapSetHandler : public BuiltinHandler {
public:
    const char* name() const override { return "MapSetHandler"; }

    bool canHandle(const std::string& funcName, HIRInstruction* inst) const override {
        static const std::unordered_set<std::string> mapSetFuncs = {
            // Map functions
            "ts_map_set",
            "ts_map_get",
            "ts_map_has",
            "ts_map_delete",
            "ts_map_clear",
            "ts_map_size",
            // Set functions
            "ts_set_add",
            "ts_set_has",
            "ts_set_delete",
            "ts_set_clear",
            "ts_set_size"
        };
        return mapSetFuncs.count(funcName) > 0;
    }

    llvm::Value* lower(const std::string& funcName, HIRInstruction* inst,
                       HIRToLLVM& lowerer) override {
        // Map functions
        if (funcName == "ts_map_set") {
            return lowerMapSet(inst, lowerer);
        }
        if (funcName == "ts_map_get") {
            return lowerMapGet(inst, lowerer);
        }
        if (funcName == "ts_map_has") {
            return lowerMapHas(inst, lowerer);
        }
        if (funcName == "ts_map_delete") {
            return lowerMapDelete(inst, lowerer);
        }
        if (funcName == "ts_map_clear") {
            return lowerMapClear(inst, lowerer);
        }
        if (funcName == "ts_map_size") {
            return lowerMapSize(inst, lowerer);
        }

        // Set functions
        if (funcName == "ts_set_add") {
            return lowerSetAdd(inst, lowerer);
        }
        if (funcName == "ts_set_has") {
            return lowerSetHas(inst, lowerer);
        }
        if (funcName == "ts_set_delete") {
            return lowerSetDelete(inst, lowerer);
        }
        if (funcName == "ts_set_clear") {
            return lowerSetClear(inst, lowerer);
        }
        if (funcName == "ts_set_size") {
            return lowerSetSize(inst, lowerer);
        }

        return nullptr;
    }

private:
    //==========================================================================
    // Boxing helper for Map/Set operations
    // These need TsValue* so we must box primitives AND strings
    //==========================================================================
    llvm::Value* boxForMapSet(llvm::Value* val, const HIROperand& operand, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // Get HIR type from operand
        std::shared_ptr<HIRType> hirType = nullptr;
        if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
            if (*valPtr) hirType = (*valPtr)->type;
        }

        if (!val->getType()->isPointerTy()) {
            // Primitive types - box based on LLVM type
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = lowerer.getTsValueMakeInt();
                return builder.CreateCall(boxFn, {val});
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = lowerer.getTsValueMakeDouble();
                return builder.CreateCall(boxFn, {val});
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = lowerer.getTsValueMakeBool();
                llvm::Value* extended = builder.CreateZExt(val, builder.getInt32Ty());
                return builder.CreateCall(boxFn, {extended});
            }
        } else {
            // Pointer type - check HIR type to determine boxing method
            if (hirType && hirType->kind == HIRTypeKind::String) {
                // Box string with ts_value_make_string
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder.getPtrTy(), { builder.getPtrTy() }, false);
                llvm::FunctionCallee fn = module.getOrInsertFunction("ts_value_make_string", ft);
                return builder.CreateCall(ft, fn.getCallee(), { val });
            } else if (hirType && (hirType->kind == HIRTypeKind::Object ||
                                   hirType->kind == HIRTypeKind::Class ||
                                   hirType->kind == HIRTypeKind::Array)) {
                // Box object with ts_value_make_object
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder.getPtrTy(), { builder.getPtrTy() }, false);
                llvm::FunctionCallee fn = module.getOrInsertFunction("ts_value_make_object", ft);
                return builder.CreateCall(ft, fn.getCallee(), { val });
            }
            // If no HIR type info, assume it's already boxed or an object pointer
        }
        return val;
    }

    //==========================================================================
    // Map functions
    //==========================================================================

    // ts_map_set(map, key, value) -> TsValue* (returns the map)
    llvm::Value* lowerMapSet(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* map = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* key = lowerer.getOperandValue(inst->operands[2]);
        llvm::Value* value = lowerer.getOperandValue(inst->operands[3]);

        // Box key and value
        key = boxForMapSet(key, inst->operands[2], lowerer);
        value = boxForMapSet(value, inst->operands[3], lowerer);

        // Call ts_map_set_wrapper(void* map, TsValue* key, TsValue* value) -> TsValue*
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_map_set_wrapper", ft);
        return builder.CreateCall(ft, fn.getCallee(), { map, key, value });
    }

    // ts_map_get(map, key) -> TsValue*
    llvm::Value* lowerMapGet(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* map = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* key = lowerer.getOperandValue(inst->operands[2]);

        // Box key
        key = boxForMapSet(key, inst->operands[2], lowerer);

        // Call ts_map_get_wrapper(void* map, TsValue* key) -> TsValue*
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_map_get_wrapper", ft);
        return builder.CreateCall(ft, fn.getCallee(), { map, key });
    }

    // ts_map_has(map, key) -> bool (unboxed)
    llvm::Value* lowerMapHas(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* map = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* key = lowerer.getOperandValue(inst->operands[2]);

        // Box key
        key = boxForMapSet(key, inst->operands[2], lowerer);

        // Call ts_map_has_wrapper(void* map, TsValue* key) -> TsValue* (boxed bool)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_map_has_wrapper", ft);
        llvm::Value* boxedResult = builder.CreateCall(ft, fn.getCallee(), { map, key });

        // Unbox the result to bool
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(
            builder.getInt1Ty(), { builder.getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module.getOrInsertFunction("ts_value_get_bool", unboxFt);
        return builder.CreateCall(unboxFt, unboxFn.getCallee(), { boxedResult });
    }

    // ts_map_delete(map, key) -> bool (unboxed)
    llvm::Value* lowerMapDelete(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* map = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* key = lowerer.getOperandValue(inst->operands[2]);

        // Box key
        key = boxForMapSet(key, inst->operands[2], lowerer);

        // Call ts_map_delete_wrapper(void* map, TsValue* key) -> TsValue* (boxed bool)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_map_delete_wrapper", ft);
        llvm::Value* boxedResult = builder.CreateCall(ft, fn.getCallee(), { map, key });

        // Unbox the result to bool
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(
            builder.getInt1Ty(), { builder.getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module.getOrInsertFunction("ts_value_get_bool", unboxFt);
        return builder.CreateCall(unboxFt, unboxFn.getCallee(), { boxedResult });
    }

    // ts_map_clear(map) -> void (returns null pointer)
    llvm::Value* lowerMapClear(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* map = lowerer.getOperandValue(inst->operands[1]);

        // Call ts_map_clear_wrapper(void* map) -> void
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getVoidTy(),
            { builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_map_clear_wrapper", ft);
        builder.CreateCall(ft, fn.getCallee(), { map });

        return llvm::ConstantPointerNull::get(builder.getPtrTy());
    }

    // ts_map_size(map) -> int64_t
    llvm::Value* lowerMapSize(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* map = lowerer.getOperandValue(inst->operands[1]);

        // Call ts_map_size(void* map) -> int64_t
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt64Ty(),
            { builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_map_size", ft);
        return builder.CreateCall(ft, fn.getCallee(), { map });
    }

    //==========================================================================
    // Set functions
    //==========================================================================

    // ts_set_add(set, value) -> TsValue* (returns the set)
    llvm::Value* lowerSetAdd(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* set = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* value = lowerer.getOperandValue(inst->operands[2]);

        // Box value
        value = boxForMapSet(value, inst->operands[2], lowerer);

        // Call ts_set_add_wrapper(void* set, TsValue* value) -> TsValue*
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_set_add_wrapper", ft);
        return builder.CreateCall(ft, fn.getCallee(), { set, value });
    }

    // ts_set_has(set, value) -> bool (unboxed)
    llvm::Value* lowerSetHas(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* set = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* value = lowerer.getOperandValue(inst->operands[2]);

        // Box value
        value = boxForMapSet(value, inst->operands[2], lowerer);

        // Call ts_set_has_wrapper(void* set, TsValue* value) -> TsValue* (boxed bool)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_set_has_wrapper", ft);
        llvm::Value* boxedResult = builder.CreateCall(ft, fn.getCallee(), { set, value });

        // Unbox the result to bool
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(
            builder.getInt1Ty(), { builder.getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module.getOrInsertFunction("ts_value_get_bool", unboxFt);
        return builder.CreateCall(unboxFt, unboxFn.getCallee(), { boxedResult });
    }

    // ts_set_delete(set, value) -> bool (unboxed)
    llvm::Value* lowerSetDelete(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* set = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* value = lowerer.getOperandValue(inst->operands[2]);

        // Box value
        value = boxForMapSet(value, inst->operands[2], lowerer);

        // Call ts_set_delete_wrapper(void* set, TsValue* value) -> TsValue* (boxed bool)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_set_delete_wrapper", ft);
        llvm::Value* boxedResult = builder.CreateCall(ft, fn.getCallee(), { set, value });

        // Unbox the result to bool
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(
            builder.getInt1Ty(), { builder.getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module.getOrInsertFunction("ts_value_get_bool", unboxFt);
        return builder.CreateCall(unboxFt, unboxFn.getCallee(), { boxedResult });
    }

    // ts_set_clear(set) -> void (returns null pointer)
    llvm::Value* lowerSetClear(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* set = lowerer.getOperandValue(inst->operands[1]);

        // Call ts_set_clear_wrapper(void* set) -> void
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getVoidTy(),
            { builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_set_clear_wrapper", ft);
        builder.CreateCall(ft, fn.getCallee(), { set });

        return llvm::ConstantPointerNull::get(builder.getPtrTy());
    }

    // ts_set_size(set) -> int64_t
    llvm::Value* lowerSetSize(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* set = lowerer.getOperandValue(inst->operands[1]);

        // Call ts_set_size(void* set) -> int64_t
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt64Ty(),
            { builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_set_size", ft);
        return builder.CreateCall(ft, fn.getCallee(), { set });
    }
};

} // namespace ts::hir

// Factory function to create MapSetHandler - called from HandlerRegistry
namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createMapSetHandler() {
        return std::make_unique<MapSetHandler>();
    }
}
