#include "HIR.h"
#include <sstream>

namespace ts::hir {

//==============================================================================
// HIRType implementation
//==============================================================================

std::string HIRType::toString() const {
    switch (kind) {
        case HIRTypeKind::Void: return "void";
        case HIRTypeKind::Bool: return "bool";
        case HIRTypeKind::Int64: return "i64";
        case HIRTypeKind::Float64: return "f64";
        case HIRTypeKind::String: return "string";
        case HIRTypeKind::Object: return "object";
        case HIRTypeKind::Any: return "any";
        case HIRTypeKind::Ptr: return "ptr";
        case HIRTypeKind::Array: {
            std::string prefix = isTypedArray ? "typed_array" : "array";
            if (elementType) {
                return prefix + "<" + elementType->toString() + ">";
            }
            return prefix + "<any>";
        }
        case HIRTypeKind::Class: {
            if (shapeId > 0) {
                return "class(" + className + ", shape=" + std::to_string(shapeId) + ")";
            }
            return "class(" + className + ")";
        }
        case HIRTypeKind::Function: {
            std::string s = "(";
            for (size_t i = 0; i < paramTypes.size(); ++i) {
                if (i > 0) s += ", ";
                s += paramTypes[i]->toString();
            }
            s += ") -> ";
            s += returnType ? returnType->toString() : "void";
            return s;
        }
    }
    return "unknown";
}

bool HIRType::operator==(const HIRType& other) const {
    if (kind != other.kind) return false;
    switch (kind) {
        case HIRTypeKind::Array:
            return isTypedArray == other.isTypedArray &&
                   elementType && other.elementType &&
                   *elementType == *other.elementType;
        case HIRTypeKind::Class:
            return className == other.className && shapeId == other.shapeId;
        case HIRTypeKind::Function:
            if (paramTypes.size() != other.paramTypes.size()) return false;
            for (size_t i = 0; i < paramTypes.size(); ++i) {
                if (!(*paramTypes[i] == *other.paramTypes[i])) return false;
            }
            if (returnType && other.returnType) {
                return *returnType == *other.returnType;
            }
            return !returnType && !other.returnType;
        default:
            return true;
    }
}

//==============================================================================
// HIRInstruction implementation
//==============================================================================

static const char* opcodeToString(HIROpcode op) {
    switch (op) {
        // Constants
        case HIROpcode::ConstInt: return "const.i64";
        case HIROpcode::ConstFloat: return "const.f64";
        case HIROpcode::ConstBool: return "const.bool";
        case HIROpcode::ConstString: return "const.string";
        case HIROpcode::ConstNull: return "const.null";
        case HIROpcode::ConstUndefined: return "const.undefined";

        // Integer arithmetic
        case HIROpcode::AddI64: return "add.i64";
        case HIROpcode::SubI64: return "sub.i64";
        case HIROpcode::MulI64: return "mul.i64";
        case HIROpcode::DivI64: return "div.i64";
        case HIROpcode::ModI64: return "mod.i64";
        case HIROpcode::NegI64: return "neg.i64";

        // Float arithmetic
        case HIROpcode::AddF64: return "add.f64";
        case HIROpcode::SubF64: return "sub.f64";
        case HIROpcode::MulF64: return "mul.f64";
        case HIROpcode::DivF64: return "div.f64";
        case HIROpcode::ModF64: return "mod.f64";
        case HIROpcode::NegF64: return "neg.f64";

        // Checked arithmetic
        case HIROpcode::AddI64Checked: return "add.i64.checked";
        case HIROpcode::SubI64Checked: return "sub.i64.checked";
        case HIROpcode::MulI64Checked: return "mul.i64.checked";

        // Bitwise
        case HIROpcode::AndI64: return "and.i64";
        case HIROpcode::OrI64: return "or.i64";
        case HIROpcode::XorI64: return "xor.i64";
        case HIROpcode::ShlI64: return "shl.i64";
        case HIROpcode::ShrI64: return "shr.i64";
        case HIROpcode::UShrI64: return "ushr.i64";
        case HIROpcode::NotI64: return "not.i64";

        // Integer comparison
        case HIROpcode::CmpEqI64: return "cmp.eq.i64";
        case HIROpcode::CmpNeI64: return "cmp.ne.i64";
        case HIROpcode::CmpLtI64: return "cmp.lt.i64";
        case HIROpcode::CmpLeI64: return "cmp.le.i64";
        case HIROpcode::CmpGtI64: return "cmp.gt.i64";
        case HIROpcode::CmpGeI64: return "cmp.ge.i64";

        // Float comparison
        case HIROpcode::CmpEqF64: return "cmp.eq.f64";
        case HIROpcode::CmpNeF64: return "cmp.ne.f64";
        case HIROpcode::CmpLtF64: return "cmp.lt.f64";
        case HIROpcode::CmpLeF64: return "cmp.le.f64";
        case HIROpcode::CmpGtF64: return "cmp.gt.f64";
        case HIROpcode::CmpGeF64: return "cmp.ge.f64";

        // Pointer comparison
        case HIROpcode::CmpEqPtr: return "cmp.eq.ptr";
        case HIROpcode::CmpNePtr: return "cmp.ne.ptr";

        // Boolean
        case HIROpcode::LogicalAnd: return "and.bool";
        case HIROpcode::LogicalOr: return "or.bool";
        case HIROpcode::LogicalNot: return "not.bool";

        // Conversions
        case HIROpcode::CastI64ToF64: return "cast.i64_to_f64";
        case HIROpcode::CastF64ToI64: return "cast.f64_to_i64";
        case HIROpcode::CastBoolToI64: return "cast.bool_to_i64";

        // Boxing
        case HIROpcode::BoxInt: return "box.int";
        case HIROpcode::BoxFloat: return "box.float";
        case HIROpcode::BoxBool: return "box.bool";
        case HIROpcode::BoxString: return "box.string";
        case HIROpcode::BoxObject: return "box.object";

        // Unboxing
        case HIROpcode::UnboxInt: return "unbox.int";
        case HIROpcode::UnboxFloat: return "unbox.float";
        case HIROpcode::UnboxBool: return "unbox.bool";
        case HIROpcode::UnboxString: return "unbox.string";
        case HIROpcode::UnboxObject: return "unbox.object";

        // Type checking
        case HIROpcode::TypeCheck: return "typecheck";
        case HIROpcode::TypeOf: return "typeof";
        case HIROpcode::InstanceOf: return "instanceof";

        // GC operations
        case HIROpcode::GCAlloc: return "gc.alloc";
        case HIROpcode::GCAllocArray: return "gc.alloc.array";
        case HIROpcode::GCStore: return "gc.store";
        case HIROpcode::GCLoad: return "gc.load";
        case HIROpcode::Safepoint: return "safepoint";
        case HIROpcode::SafepointPoll: return "safepoint.poll";

        // Memory
        case HIROpcode::Load: return "load";
        case HIROpcode::Store: return "store";
        case HIROpcode::GetElementPtr: return "gep";

        // Objects
        case HIROpcode::NewObject: return "new_object";
        case HIROpcode::NewObjectDynamic: return "new_object_dynamic";
        case HIROpcode::GetPropStatic: return "get_prop.static";
        case HIROpcode::GetPropDynamic: return "get_prop.dynamic";
        case HIROpcode::SetPropStatic: return "set_prop.static";
        case HIROpcode::SetPropDynamic: return "set_prop.dynamic";
        case HIROpcode::HasProp: return "has_prop";
        case HIROpcode::DeleteProp: return "delete_prop";

        // Arrays
        case HIROpcode::NewArrayBoxed: return "new_array.boxed";
        case HIROpcode::NewArrayTyped: return "new_array.typed";
        case HIROpcode::GetElem: return "get_elem";
        case HIROpcode::SetElem: return "set_elem";
        case HIROpcode::GetElemTyped: return "get_elem.typed";
        case HIROpcode::SetElemTyped: return "set_elem.typed";
        case HIROpcode::ArrayLength: return "array.length";
        case HIROpcode::ArrayPush: return "array.push";

        // Calls
        case HIROpcode::Call: return "call";
        case HIROpcode::CallMethod: return "call_method";
        case HIROpcode::CallVirtual: return "call_virtual";
        case HIROpcode::CallIndirect: return "call_indirect";

        // Globals
        case HIROpcode::LoadGlobal: return "load_global";
        case HIROpcode::LoadFunction: return "load_function";

        // Control flow
        case HIROpcode::Branch: return "br";
        case HIROpcode::CondBranch: return "condbr";
        case HIROpcode::Switch: return "switch";
        case HIROpcode::Return: return "ret";
        case HIROpcode::ReturnVoid: return "ret void";
        case HIROpcode::Unreachable: return "unreachable";

        // Phi
        case HIROpcode::Phi: return "phi";

        // Misc
        case HIROpcode::Select: return "select";
        case HIROpcode::Copy: return "copy";
    }
    return "unknown";
}

static std::string operandToString(const HIROperand& op) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::shared_ptr<HIRValue>>) {
            return arg ? arg->toString() : "null";
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return "\"" + arg + "\"";
        } else if constexpr (std::is_same_v<T, HIRBlock*>) {
            return arg ? "%" + arg->label : "%null";
        } else if constexpr (std::is_same_v<T, std::shared_ptr<HIRType>>) {
            return arg ? arg->toString() : "type?";
        }
        return "?";
    }, op);
}

std::string HIRInstruction::toString() const {
    std::ostringstream ss;

    // Print result if present
    if (result) {
        ss << result->toString() << " = ";
    } else {
        ss << "    ";
    }

    // Print opcode
    ss << opcodeToString(opcode);

    // Print operands
    for (size_t i = 0; i < operands.size(); ++i) {
        ss << (i == 0 ? " " : ", ");
        ss << operandToString(operands[i]);
    }

    // Print phi incoming edges
    if (opcode == HIROpcode::Phi) {
        for (auto& [val, block] : phiIncoming) {
            ss << " [" << val->toString() << ", %" << block->label << "]";
        }
    }

    // Print switch cases
    if (opcode == HIROpcode::Switch) {
        ss << ", default: %" << (switchDefault ? switchDefault->label : "null");
        for (auto& [val, block] : switchCases) {
            ss << ", " << val << ": %" << block->label;
        }
    }

    return ss.str();
}

} // namespace ts::hir
