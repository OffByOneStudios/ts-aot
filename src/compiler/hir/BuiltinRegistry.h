#pragma once

#include "HIR.h"
#include <string>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace ts::hir {

/// Describes how a method call should be resolved
struct MethodResolution {
    enum class Kind {
        /// Replace with a specific HIR opcode (e.g., ArrayPush)
        HIROpcode,
        /// Replace with a runtime function call
        RuntimeCall,
        /// Leave as CallMethod (dynamic dispatch needed)
        Dynamic
    };

    Kind kind = Kind::Dynamic;

    /// For HIROpcode: the opcode to use
    HIROpcode opcode = HIROpcode::Call;

    /// For RuntimeCall: the runtime function name
    std::string runtimeFunction;

    /// Expected argument count (-1 = variadic)
    int expectedArgCount = -1;

    /// Return type of the method (if known)
    std::shared_ptr<HIRType> returnType;

    // Factory methods
    static MethodResolution makeOpcode(HIROpcode op, std::shared_ptr<HIRType> retType = nullptr) {
        MethodResolution r;
        r.kind = Kind::HIROpcode;
        r.opcode = op;
        r.returnType = retType;
        return r;
    }

    static MethodResolution makeRuntimeCall(const std::string& fn, int argCount = -1,
                                            std::shared_ptr<HIRType> retType = nullptr) {
        MethodResolution r;
        r.kind = Kind::RuntimeCall;
        r.runtimeFunction = fn;
        r.expectedArgCount = argCount;
        r.returnType = retType;
        return r;
    }

    static MethodResolution makeDynamic() {
        MethodResolution r;
        r.kind = Kind::Dynamic;
        return r;
    }
};

/// Describes how a global builtin call should be resolved
struct BuiltinResolution {
    enum class Kind {
        /// Replace with a runtime function call
        RuntimeCall,
        /// Inline the operation (e.g., console.log)
        Inline,
        /// Not a known builtin
        Unknown
    };

    Kind kind = Kind::Unknown;
    std::string runtimeFunction;
    std::shared_ptr<HIRType> returnType;

    static BuiltinResolution makeRuntimeCall(const std::string& fn,
                                             std::shared_ptr<HIRType> retType = nullptr) {
        BuiltinResolution r;
        r.kind = Kind::RuntimeCall;
        r.runtimeFunction = fn;
        r.returnType = retType;
        return r;
    }

    static BuiltinResolution makeInline() {
        BuiltinResolution r;
        r.kind = Kind::Inline;
        return r;
    }

    static BuiltinResolution makeUnknown() {
        return BuiltinResolution{};
    }
};

/// Centralized registry for builtin method and function resolution
///
/// This provides a single source of truth for:
/// - Array methods (push, pop, map, filter, etc.)
/// - String methods (charAt, substring, etc.)
/// - Object methods (keys, values, entries, etc.)
/// - Global builtins (console.log, Math.floor, JSON.parse, etc.)
///
/// Passes should query this registry rather than containing their own
/// hardcoded string checks.
class BuiltinRegistry {
public:
    /// Get the singleton instance
    static BuiltinRegistry& instance();

    /// Resolve a method call on a typed receiver
    /// @param receiverType The type of the receiver (e.g., Array, String)
    /// @param methodName The method name being called
    /// @param argCount Number of arguments (-1 if unknown)
    /// @return Resolution describing how to handle the call
    MethodResolution resolveMethod(HIRTypeKind receiverType,
                                   const std::string& methodName,
                                   int argCount = -1) const;

    /// Resolve a global builtin call (e.g., console.log, Math.floor)
    /// @param globalName The global object (e.g., "console", "Math")
    /// @param methodName The method name (e.g., "log", "floor")
    /// @param argCount Number of arguments (-1 if unknown)
    /// @return Resolution describing how to handle the call
    BuiltinResolution resolveGlobalBuiltin(const std::string& globalName,
                                           const std::string& methodName,
                                           int argCount = -1) const;

    /// Check if a type is a known builtin type with special method handling
    bool isBuiltinType(HIRTypeKind kind) const;

    /// Check if a global name is a known builtin object
    bool isBuiltinGlobal(const std::string& name) const;

private:
    BuiltinRegistry();

    // Prevent copying
    BuiltinRegistry(const BuiltinRegistry&) = delete;
    BuiltinRegistry& operator=(const BuiltinRegistry&) = delete;

    void registerArrayMethods();
    void registerArrayStaticMethods();
    void registerStringMethods();
    void registerStringStaticMethods();
    void registerObjectMethods();
    void registerNumberStaticMethods();
    void registerMathBuiltins();
    void registerConsoleBuiltins();
    void registerJSONBuiltins();

    // Method tables: (type, methodName) → resolution
    using MethodKey = std::pair<HIRTypeKind, std::string>;
    struct MethodKeyHash {
        size_t operator()(const MethodKey& k) const {
            return std::hash<int>()(static_cast<int>(k.first)) ^
                   (std::hash<std::string>()(k.second) << 1);
        }
    };
    std::unordered_map<MethodKey, MethodResolution, MethodKeyHash> methodTable_;

    // Global builtin tables: (globalName, methodName) → resolution
    using GlobalKey = std::pair<std::string, std::string>;
    struct GlobalKeyHash {
        size_t operator()(const GlobalKey& k) const {
            return std::hash<std::string>()(k.first) ^
                   (std::hash<std::string>()(k.second) << 1);
        }
    };
    std::unordered_map<GlobalKey, BuiltinResolution, GlobalKeyHash> globalTable_;

    // Known builtin global objects
    std::unordered_set<std::string> builtinGlobals_;
};

} // namespace ts::hir
