#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>

namespace ext {

// Forward declarations
struct TypeDefinition;
struct FunctionDefinition;
struct MethodDefinition;
struct PropertyDefinition;

//=============================================================================
// Lowering Specification
//=============================================================================

enum class LoweringType {
    Ptr,      // Pointer (any object)
    I64,      // 64-bit integer
    I32,      // 32-bit integer
    F64,      // 64-bit float
    I1,       // Boolean
    Boxed,    // TsValue* (boxed value)
    Void      // No return value
};

struct LoweringSpec {
    std::vector<LoweringType> args;
    LoweringType returns = LoweringType::Void;

    bool hasLowering() const { return !args.empty() || returns != LoweringType::Void; }
};

//=============================================================================
// Type Reference (can be simple string or generic)
//=============================================================================

struct TypeReference {
    std::string name;                              // e.g., "string", "Buffer", "Array"
    std::vector<TypeReference> typeArgs;           // For generics: Array<string>

    bool isGeneric() const { return !typeArgs.empty(); }

    static TypeReference simple(const std::string& name) {
        return TypeReference{name, {}};
    }
};

//=============================================================================
// Parameter Definition
//=============================================================================

struct ParameterDefinition {
    std::string name;
    TypeReference type;
    bool optional = false;
    bool rest = false;
    std::optional<std::string> defaultValue;
};

//=============================================================================
// Property Definition
//=============================================================================

struct PropertyDefinition {
    TypeReference type;
    std::optional<std::string> getter;     // Runtime function for getter
    std::optional<std::string> setter;     // Runtime function for setter
    bool readonly = false;
    std::optional<LoweringSpec> lowering;
};

//=============================================================================
// Method Definition
//=============================================================================

struct MethodDefinition {
    std::string call;                              // Runtime function name
    std::optional<std::string> hirName;            // HIR function name (if different from call)
    std::vector<ParameterDefinition> params;
    TypeReference returns;
    std::vector<std::string> typeParams;           // Generic type parameters
    std::optional<LoweringSpec> lowering;
    std::optional<int> platformArg;                // Platform constant for _ex variants (0=default, 1=win32, 2=posix)
};

//=============================================================================
// Function Definition (standalone function)
//=============================================================================

struct FunctionDefinition {
    std::string call;                              // Runtime function name
    std::vector<ParameterDefinition> params;
    TypeReference returns;
    bool async = false;
    std::vector<std::string> typeParams;
    std::optional<LoweringSpec> lowering;
    std::vector<FunctionDefinition> overloads;     // For overloaded functions
};

//=============================================================================
// Type Definition (class, interface, enum)
//=============================================================================

enum class TypeKind {
    Class,
    Interface,
    Enum
};

struct TypeDefinition {
    TypeKind kind = TypeKind::Class;
    std::vector<std::string> typeParams;                          // Generic params
    std::optional<TypeReference> extends;                         // Base class
    std::vector<TypeReference> implements;                        // Interfaces
    std::optional<MethodDefinition> constructor;
    std::unordered_map<std::string, PropertyDefinition> properties;
    std::unordered_map<std::string, MethodDefinition> methods;
    std::unordered_map<std::string, PropertyDefinition> staticProperties;
    std::unordered_map<std::string, MethodDefinition> staticMethods;
};

//=============================================================================
// Object Definition (module namespace like 'fs', 'console')
//=============================================================================

struct ObjectDefinition;  // Forward declaration for nested objects

struct ObjectDefinition {
    std::unordered_map<std::string, PropertyDefinition> properties;
    std::unordered_map<std::string, MethodDefinition> methods;
    std::unordered_map<std::string, std::shared_ptr<ObjectDefinition>> nestedObjects;
};

//=============================================================================
// Global Definition (global variable or function)
//=============================================================================

struct GlobalDefinition {
    enum class Kind { Property, Function };
    Kind kind;

    // One of these will be populated based on kind
    std::optional<PropertyDefinition> property;
    std::optional<FunctionDefinition> function;

    // Factory function to call at initialization (for property globals)
    std::optional<std::string> factory;
};

//=============================================================================
// Extension Contract (root object)
//=============================================================================

struct ExtensionContract {
    std::string name;
    std::string version;
    std::vector<std::string> modules;              // Module specifiers provided

    std::unordered_map<std::string, TypeDefinition> types;
    std::unordered_map<std::string, FunctionDefinition> functions;
    std::unordered_map<std::string, ObjectDefinition> objects;
    std::unordered_map<std::string, GlobalDefinition> globals;
};

//=============================================================================
// Helper Functions
//=============================================================================

inline LoweringType stringToLoweringType(const std::string& s) {
    if (s == "ptr") return LoweringType::Ptr;
    if (s == "i64") return LoweringType::I64;
    if (s == "i32") return LoweringType::I32;
    if (s == "f64") return LoweringType::F64;
    if (s == "i1") return LoweringType::I1;
    if (s == "boxed") return LoweringType::Boxed;
    if (s == "void") return LoweringType::Void;
    return LoweringType::Ptr;  // Default to ptr
}

inline std::string loweringTypeToString(LoweringType t) {
    switch (t) {
        case LoweringType::Ptr: return "ptr";
        case LoweringType::I64: return "i64";
        case LoweringType::I32: return "i32";
        case LoweringType::F64: return "f64";
        case LoweringType::I1: return "i1";
        case LoweringType::Boxed: return "boxed";
        case LoweringType::Void: return "void";
    }
    return "ptr";
}

} // namespace ext
