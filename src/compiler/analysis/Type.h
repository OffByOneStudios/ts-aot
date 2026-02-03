#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include <algorithm>
#include <variant>
#include "../ast/AccessModifier.h"

namespace ast {
    struct Node;
    struct ClassDeclaration;
    struct ClassExpression;
    struct InterfaceDeclaration;
    struct FunctionDeclaration;
    struct StaticBlock;
}

namespace ts {
struct InterfaceType;
struct UnionType;
struct IntersectionType;

enum class TypeKind {
    Void,
    Boolean,
    Int,
    Double,
    String,
    Any,
    Function,
    Array,
    Map,
    SetType,
    Object,
    Class,
    Interface,
    Union,
    Intersection,
    TypeParameter,
    Namespace,
    Enum,
    Tuple,
    BigInt,
    Symbol,
    Null,
    Undefined,
    Unknown,
    Never
};

/// Element kinds for V8-style monomorphic array optimizations
/// Based on V8's PACKED_*_ELEMENTS and HOLEY_*_ELEMENTS
/// See: https://v8.dev/blog/elements-kinds
enum class ElementKind {
    Unknown,           ///< Type not yet determined (uninitialized array)
    PackedSmi,         ///< Small integers that fit in 31 bits (-2^30 to 2^30-1)
    PackedDouble,      ///< IEEE 754 doubles (includes all numbers)
    PackedString,      ///< TsString* pointers only
    PackedObject,      ///< Homogeneous object type (same class)
    PackedAny,         ///< Mixed types (current generic path)
    HoleySmi,          ///< SMI with holes (sparse array)
    HoleyDouble,       ///< Double with holes
    HoleyAny           ///< Mixed with holes
};

/// Check if an element kind has holes (sparse array)
inline bool hasHoles(ElementKind kind) {
    return kind == ElementKind::HoleySmi ||
           kind == ElementKind::HoleyDouble ||
           kind == ElementKind::HoleyAny;
}

/// Check if an element kind is a "packed" (dense) kind
inline bool isPacked(ElementKind kind) {
    return kind == ElementKind::PackedSmi ||
           kind == ElementKind::PackedDouble ||
           kind == ElementKind::PackedString ||
           kind == ElementKind::PackedObject ||
           kind == ElementKind::PackedAny;
}

/// Get the "holey" version of a packed element kind
inline ElementKind toHoley(ElementKind kind) {
    switch (kind) {
        case ElementKind::PackedSmi: return ElementKind::HoleySmi;
        case ElementKind::PackedDouble: return ElementKind::HoleyDouble;
        case ElementKind::PackedString:
        case ElementKind::PackedObject:
        case ElementKind::PackedAny: return ElementKind::HoleyAny;
        default: return kind; // Already holey or unknown
    }
}

/// Get the more general element kind when two kinds meet
/// Element kind transitions are one-way: more specific -> more general
inline ElementKind meetElementKinds(ElementKind a, ElementKind b) {
    if (a == b) return a;
    if (a == ElementKind::Unknown) return b;
    if (b == ElementKind::Unknown) return a;

    // If either has holes, result has holes
    bool holes = hasHoles(a) || hasHoles(b);

    // Determine base kind (ignoring holes)
    auto baseA = a;
    auto baseB = b;
    if (a == ElementKind::HoleySmi) baseA = ElementKind::PackedSmi;
    else if (a == ElementKind::HoleyDouble) baseA = ElementKind::PackedDouble;
    else if (a == ElementKind::HoleyAny) baseA = ElementKind::PackedAny;
    if (b == ElementKind::HoleySmi) baseB = ElementKind::PackedSmi;
    else if (b == ElementKind::HoleyDouble) baseB = ElementKind::PackedDouble;
    else if (b == ElementKind::HoleyAny) baseB = ElementKind::PackedAny;

    // SMI + Double = Double (SMI is subset of Double)
    ElementKind resultBase;
    if (baseA == ElementKind::PackedSmi && baseB == ElementKind::PackedDouble) {
        resultBase = ElementKind::PackedDouble;
    } else if (baseA == ElementKind::PackedDouble && baseB == ElementKind::PackedSmi) {
        resultBase = ElementKind::PackedDouble;
    } else {
        // Any other combination goes to PackedAny
        resultBase = ElementKind::PackedAny;
    }

    return holes ? toHoley(resultBase) : resultBase;
}

/// Convert element kind to string for debugging
inline const char* elementKindToString(ElementKind kind) {
    switch (kind) {
        case ElementKind::Unknown: return "Unknown";
        case ElementKind::PackedSmi: return "PackedSmi";
        case ElementKind::PackedDouble: return "PackedDouble";
        case ElementKind::PackedString: return "PackedString";
        case ElementKind::PackedObject: return "PackedObject";
        case ElementKind::PackedAny: return "PackedAny";
        case ElementKind::HoleySmi: return "HoleySmi";
        case ElementKind::HoleyDouble: return "HoleyDouble";
        case ElementKind::HoleyAny: return "HoleyAny";
        default: return "Unknown";
    }
}

struct Type : public std::enable_shared_from_this<Type> {
    TypeKind kind;

    Type(TypeKind k) : kind(k) {}
    virtual ~Type() = default;

    virtual bool isAssignableTo(std::shared_ptr<Type> other);

    bool isNumber() const {
        return kind == TypeKind::Int || kind == TypeKind::Double;
    }

    virtual std::string toString() const {
        switch (kind) {
            case TypeKind::Void: return "void";
            case TypeKind::Boolean: return "boolean";
            case TypeKind::Int: return "int";
            case TypeKind::Double: return "double";
            case TypeKind::String: return "string";
            case TypeKind::Any: return "any";
            case TypeKind::Function: return "function";
            case TypeKind::Array: return "array";
            case TypeKind::Map: return "Map";
            case TypeKind::Object: return "object";
            case TypeKind::Class: return "class";
            case TypeKind::Interface: return "interface";
            case TypeKind::Union: return "union";
            case TypeKind::Intersection: return "intersection";
            case TypeKind::TypeParameter: return "type-parameter";
            case TypeKind::Namespace: return "namespace";
            case TypeKind::Enum: return "enum";
            case TypeKind::Tuple: return "tuple";
            default: return "unknown";
        }
    }
};

struct TypeParameterType : Type {
    std::string name;
    std::shared_ptr<Type> constraint;
    std::shared_ptr<Type> defaultType;  // Default type if not specified (e.g., T = string)

    TypeParameterType(const std::string& n) : Type(TypeKind::TypeParameter), name(n) {}
    std::string toString() const override { return name; }
};

struct Module; // Forward declaration

struct NamespaceType : Type {
    std::shared_ptr<Module> module;
    NamespaceType(std::shared_ptr<Module> m) : Type(TypeKind::Namespace), module(m) {}
    std::string toString() const override { return "namespace"; }
};

struct ArrayType : public Type {
    std::shared_ptr<Type> elementType;
    ElementKind elementKind = ElementKind::Unknown;  ///< V8-style element kind for optimization
    bool mayHaveHoles = false;  ///< True if array may be sparse (has holes)

    ArrayType(std::shared_ptr<Type> elem) : Type(TypeKind::Array), elementType(elem) {}
    ArrayType(std::shared_ptr<Type> elem, ElementKind kind)
        : Type(TypeKind::Array), elementType(elem), elementKind(kind) {}

    /// Create an array type with inferred element kind from element type
    static std::shared_ptr<ArrayType> createWithKind(std::shared_ptr<Type> elem) {
        auto arr = std::make_shared<ArrayType>(elem);
        arr->elementKind = inferElementKind(elem);
        return arr;
    }

    /// Infer the element kind from the element type
    static ElementKind inferElementKind(std::shared_ptr<Type> elemType) {
        if (!elemType) return ElementKind::PackedAny;
        switch (elemType->kind) {
            case TypeKind::Int:
                return ElementKind::PackedSmi;  // Integers use SMI path
            case TypeKind::Double:
                return ElementKind::PackedDouble;
            case TypeKind::String:
                return ElementKind::PackedString;
            case TypeKind::Class:
            case TypeKind::Interface:
                return ElementKind::PackedObject;
            case TypeKind::Any:
            case TypeKind::Unknown:
                return ElementKind::PackedAny;
            default:
                return ElementKind::PackedAny;
        }
    }

    std::string toString() const override {
        std::string result = elementType->toString() + "[]";
        if (elementKind != ElementKind::Unknown) {
            result += " [" + std::string(elementKindToString(elementKind)) + "]";
        }
        return result;
    }
};

struct MapType : public Type {
    std::shared_ptr<Type> keyType;
    std::shared_ptr<Type> valueType;
    MapType(std::shared_ptr<Type> k, std::shared_ptr<Type> v) : Type(TypeKind::Map), keyType(k), valueType(v) {}
    MapType() : Type(TypeKind::Map), 
        keyType(std::make_shared<Type>(TypeKind::Any)), 
        valueType(std::make_shared<Type>(TypeKind::Any)) {}
    std::string toString() const override { 
        return "Map<" + keyType->toString() + ", " + valueType->toString() + ">"; 
    }
};

struct SetTypeStruct : public Type {
    std::shared_ptr<Type> elementType;
    SetTypeStruct(std::shared_ptr<Type> elem) : Type(TypeKind::SetType), elementType(elem) {}
    SetTypeStruct() : Type(TypeKind::SetType), elementType(std::make_shared<Type>(TypeKind::Any)) {}
    std::string toString() const override { return "Set<" + elementType->toString() + ">"; }
};

struct FunctionType : public Type {
    ast::Node* node = nullptr;
    std::vector<std::shared_ptr<Type>> paramTypes;
    std::vector<bool> isOptional;
    bool hasRest = false;
    std::shared_ptr<Type> returnType;
    std::vector<std::shared_ptr<TypeParameterType>> typeParameters;
    bool isComptime = false;

    FunctionType() : Type(TypeKind::Function) {}
    
    std::string toString() const override {
        std::string s = "(";
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            s += paramTypes[i]->toString();
            if (i < paramTypes.size() - 1) s += ", ";
        }
        s += ") => " + (returnType ? returnType->toString() : "void");
        return s;
    }
};

struct ObjectType : public Type {
    std::map<std::string, std::shared_ptr<Type>> fields;
    std::map<std::string, std::shared_ptr<FunctionType>> getters;
    std::map<std::string, std::shared_ptr<FunctionType>> setters;
    ObjectType() : Type(TypeKind::Object) {}
    
    std::string toString() const override {
        std::string s = "{ ";
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            s += it->first + ": " + it->second->toString();
            if (std::next(it) != fields.end()) s += ", ";
        }
        s += " }";
        return s;
    }
};

struct ClassType : public Type {
    std::string name;
    std::string originalName;
    ast::ClassDeclaration* node = nullptr;
    ast::ClassExpression* exprNode = nullptr;  // For class expressions
    bool isAbstract = false;
    bool isStruct = false;
    std::shared_ptr<ClassType> baseClass;
    std::vector<std::shared_ptr<InterfaceType>> implementsInterfaces;
    std::vector<std::shared_ptr<TypeParameterType>> typeParameters;
    std::map<std::string, std::shared_ptr<Type>> fields;
    std::map<std::string, std::shared_ptr<Type>> staticFields;
    std::map<std::string, std::shared_ptr<FunctionType>> methods;
    std::map<std::string, std::shared_ptr<FunctionType>> staticMethods;
    std::map<std::string, std::vector<std::shared_ptr<FunctionType>>> methodOverloads;
    std::map<std::string, std::vector<std::shared_ptr<FunctionType>>> staticMethodOverloads;
    std::vector<std::shared_ptr<FunctionType>> constructorOverloads;
    std::map<std::string, std::shared_ptr<FunctionType>> getters;
    std::map<std::string, std::shared_ptr<FunctionType>> setters;
    std::map<std::string, AccessModifier> fieldAccess;
    std::map<std::string, AccessModifier> staticFieldAccess;
    std::map<std::string, AccessModifier> methodAccess;
    std::map<std::string, AccessModifier> staticMethodAccess;
    std::set<std::string> readonlyFields;
    std::set<std::string> staticReadonlyFields;
    std::set<std::string> abstractMethods;
    std::set<std::string> comptimeFields;
    std::set<std::string> staticComptimeFields;
    std::set<std::string> comptimeMethods;
    std::set<std::string> staticComptimeMethods;
    std::vector<ast::StaticBlock*> staticBlocks;
    std::vector<std::shared_ptr<Type>> typeArguments;

    // Index signature: [key: string]: valueType
    std::shared_ptr<Type> indexSignatureValueType;  // null if no index signature
    bool indexSignatureReadonly = false;

    ClassType() : Type(TypeKind::Class) {}
    ClassType(const std::string& name) : Type(TypeKind::Class), name(name) {}

    bool isSubclassOf(std::shared_ptr<ClassType> other) {
        if (this == other.get()) return true;
        if (baseClass) return baseClass->isSubclassOf(other);
        return false;
    }

    bool isAssignableTo(std::shared_ptr<Type> other) override;

    std::string toString() const override {
        return name;
    }
};

struct InterfaceType : public Type {
    std::string name;
    ast::InterfaceDeclaration* node = nullptr;
    std::vector<std::shared_ptr<InterfaceType>> baseInterfaces;
    std::vector<std::shared_ptr<TypeParameterType>> typeParameters;
    std::map<std::string, std::shared_ptr<Type>> fields;
    std::map<std::string, std::shared_ptr<FunctionType>> methods;
    std::map<std::string, std::vector<std::shared_ptr<FunctionType>>> methodOverloads;
    std::vector<std::shared_ptr<Type>> typeArguments;

    // Call signatures for hybrid types (callable interfaces)
    std::vector<std::shared_ptr<FunctionType>> callSignatures;
    std::vector<std::shared_ptr<FunctionType>> constructSignatures;

    InterfaceType(std::string n) : Type(TypeKind::Interface), name(n) {}

    // Check if this interface is callable (has call signatures)
    bool isCallable() const { return !callSignatures.empty(); }
    bool isConstructable() const { return !constructSignatures.empty(); }

    // Get the primary call signature (for single-signature cases)
    std::shared_ptr<FunctionType> getCallSignature() const {
        return callSignatures.empty() ? nullptr : callSignatures[0];
    }

    bool isAssignableTo(std::shared_ptr<Type> other) override;

    std::string toString() const override {
        return name;
    }
};

struct UnionType : public Type {
    std::vector<std::shared_ptr<Type>> types;
    UnionType(std::vector<std::shared_ptr<Type>> t) : Type(TypeKind::Union), types(t) {}
    
    std::string toString() const override {
        std::string s;
        for (size_t i = 0; i < types.size(); ++i) {
            s += types[i]->toString();
            if (i < types.size() - 1) s += " | ";
        }
        return s;
    }
};

struct IntersectionType : public Type {
    std::vector<std::shared_ptr<Type>> types;
    IntersectionType(std::vector<std::shared_ptr<Type>> t) : Type(TypeKind::Intersection), types(t) {}
    
    std::string toString() const override {
        std::string s;
        for (size_t i = 0; i < types.size(); ++i) {
            s += types[i]->toString();
            if (i < types.size() - 1) s += " & ";
        }
        return s;
    }
};

// Enum member value can be either int or string
using EnumMemberValue = std::variant<int, std::string>;

struct EnumType : Type {
    std::string name;
    std::map<std::string, EnumMemberValue> members;
    EnumType(const std::string& n) : Type(TypeKind::Enum), name(n) {}
    std::string toString() const override { return "enum " + name; }

    // Helper to check if enum has any string values
    bool hasStringMembers() const {
        for (const auto& [key, val] : members) {
            if (std::holds_alternative<std::string>(val)) return true;
        }
        return false;
    }
};

struct TupleType : Type {
    std::vector<std::shared_ptr<Type>> elementTypes;
    TupleType(const std::vector<std::shared_ptr<Type>>& types) : Type(TypeKind::Tuple), elementTypes(types) {}
    std::string toString() const override {
        std::string s = "[";
        for (size_t i = 0; i < elementTypes.size(); ++i) {
            if (i > 0) s += ", ";
            s += elementTypes[i]->toString();
        }
        s += "]";
        return s;
    }
};

inline bool ClassType::isAssignableTo(std::shared_ptr<Type> other) {
    if (other->kind == TypeKind::Any) return true;
    if (other->kind == TypeKind::Class) {
        auto otherClass = std::static_pointer_cast<ClassType>(other);
        auto current = std::static_pointer_cast<ClassType>(shared_from_this());
        while (current) {
            if (current->name == otherClass->name) return true;
            current = current->baseClass;
        }
        return false;
    }
    if (other->kind == TypeKind::Interface) {
        auto otherInter = std::static_pointer_cast<InterfaceType>(other);
        for (const auto& [name, type] : otherInter->fields) {
            bool found = false;
            auto current = std::static_pointer_cast<ClassType>(shared_from_this());
            while (current) {
                if (current->fields.count(name)) { found = true; break; }
                current = current->baseClass;
            }
            if (!found) return false;
        }
        for (const auto& [name, type] : otherInter->methods) {
            bool found = false;
            auto current = std::static_pointer_cast<ClassType>(shared_from_this());
            while (current) {
                if (current->methods.count(name)) { found = true; break; }
                current = current->baseClass;
            }
            if (!found) return false;
        }
        return true;
    }
    return false;
}

inline bool InterfaceType::isAssignableTo(std::shared_ptr<Type> other) {
    if (other->kind == TypeKind::Any) return true;
    if (other->kind == TypeKind::Interface) {
        auto otherInter = std::static_pointer_cast<InterfaceType>(other);
        for (const auto& [name, type] : otherInter->fields) {
            if (!fields.count(name)) return false;
        }
        for (const auto& [name, type] : otherInter->methods) {
            if (!methods.count(name)) return false;
        }
        return true;
    }
    return false;
}

inline bool Type::isAssignableTo(std::shared_ptr<Type> other) {
    if (other->kind == TypeKind::Any || kind == TypeKind::Any) return true;

    // Handle Tuple
    if (kind == TypeKind::Tuple) {
        auto tupleThis = std::static_pointer_cast<TupleType>(shared_from_this());
        if (other->kind == TypeKind::Tuple) {
            auto tupleOther = std::static_pointer_cast<TupleType>(other);
            if (tupleThis->elementTypes.size() != tupleOther->elementTypes.size()) return false;
            for (size_t i = 0; i < tupleThis->elementTypes.size(); ++i) {
                if (!tupleThis->elementTypes[i]->isAssignableTo(tupleOther->elementTypes[i])) return false;
            }
            return true;
        }
        if (other->kind == TypeKind::Array) {
            auto arrayOther = std::static_pointer_cast<ArrayType>(other);
            for (auto& t : tupleThis->elementTypes) {
                if (!t->isAssignableTo(arrayOther->elementType)) return false;
            }
            return true;
        }
    }

    // Handle TypeParameter
    if (kind == TypeKind::TypeParameter) {
        if (other->kind == TypeKind::TypeParameter) {
            if (static_cast<const TypeParameterType*>(this)->name == static_cast<const TypeParameterType*>(other.get())->name) {
                return true;
            }
        }
        auto constraint = static_cast<const TypeParameterType*>(this)->constraint;
        if (constraint) return constraint->isAssignableTo(other);
        return false;
    }

    // Handle Interface (Target)
    if (other->kind == TypeKind::Interface) {
        auto inter = std::static_pointer_cast<InterfaceType>(other);
        
        // Special case for string/array length
        if (kind == TypeKind::String || kind == TypeKind::Array) {
            for (const auto& [name, type] : inter->fields) {
                if (name == "length") {
                    if (!type->isNumber()) return false;
                } else {
                    return false;
                }
            }
            return inter->methods.empty();
        }
    }

    // Handle Union (Target)
    if (other->kind == TypeKind::Union) {
        auto unionOther = std::static_pointer_cast<UnionType>(other);
        for (auto& t : unionOther->types) {
            if (this->isAssignableTo(t)) return true;
        }
        return false;
    }

    // Handle Union (Source)
    if (kind == TypeKind::Union) {
        auto unionThis = std::static_pointer_cast<UnionType>(shared_from_this());
        for (auto& t : unionThis->types) {
            if (!t->isAssignableTo(other)) return false;
        }
        return true;
    }

    // Handle Intersection (Target)
    if (other->kind == TypeKind::Intersection) {
        auto interOther = std::static_pointer_cast<IntersectionType>(other);
        for (auto& t : interOther->types) {
            if (!this->isAssignableTo(t)) return false;
        }
        return true;
    }

    // Handle Intersection (Source)
    if (kind == TypeKind::Intersection) {
        auto interThis = std::static_pointer_cast<IntersectionType>(shared_from_this());
        for (auto& t : interThis->types) {
            if (t->isAssignableTo(other)) return true;
        }
        return false;
    }

    if (kind == other->kind) return true;
    if (isNumber() && other->isNumber()) return true;
    return false;
}

} // namespace ts
