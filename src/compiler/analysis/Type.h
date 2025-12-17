#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include "../ast/AccessModifier.h"

namespace ts {

struct InterfaceType;

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
    Object,
    Class,
    Interface
};

struct Type : public std::enable_shared_from_this<Type> {
    TypeKind kind;

    Type(TypeKind k) : kind(k) {}
    virtual ~Type() = default;

    virtual bool isAssignableTo(std::shared_ptr<Type> other) {
        if (other->kind == TypeKind::Any || kind == TypeKind::Any) return true;
        if (kind == other->kind) return true;
        if (isNumber() && other->isNumber()) return true;
        return false;
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
        }
        return "unknown";
    }
    
    bool isNumber() const { return kind == TypeKind::Int || kind == TypeKind::Double; }
};

struct ArrayType : public Type {
    std::shared_ptr<Type> elementType;
    ArrayType(std::shared_ptr<Type> elem) : Type(TypeKind::Array), elementType(elem) {}
    std::string toString() const override { return elementType->toString() + "[]"; }
};

struct FunctionType : public Type {
    std::vector<std::shared_ptr<Type>> paramTypes;
    std::shared_ptr<Type> returnType;

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
    std::shared_ptr<ClassType> baseClass;
    std::vector<std::shared_ptr<InterfaceType>> implementsInterfaces;
    std::map<std::string, std::shared_ptr<Type>> fields;
    std::map<std::string, std::shared_ptr<FunctionType>> methods;
    std::map<std::string, AccessModifier> fieldAccess;
    std::map<std::string, AccessModifier> methodAccess;

    ClassType(std::string n) : Type(TypeKind::Class), name(n) {}

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
    std::vector<std::shared_ptr<InterfaceType>> baseInterfaces;
    std::map<std::string, std::shared_ptr<Type>> fields;
    std::map<std::string, std::shared_ptr<FunctionType>> methods;

    InterfaceType(std::string n) : Type(TypeKind::Interface), name(n) {}

    bool isAssignableTo(std::shared_ptr<Type> other) override;

    std::string toString() const override {
        return name;
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

} // namespace ts
