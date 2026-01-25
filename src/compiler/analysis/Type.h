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
    ArrayType(std::shared_ptr<Type> elem) : Type(TypeKind::Array), elementType(elem) {}
    std::string toString() const override { return elementType->toString() + "[]"; }
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
