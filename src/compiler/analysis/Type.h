#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

namespace ts {

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
    Class
};

struct Type {
    TypeKind kind;

    Type(TypeKind k) : kind(k) {}
    virtual ~Type() = default;

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
    std::map<std::string, std::shared_ptr<Type>> fields;
    std::map<std::string, std::shared_ptr<FunctionType>> methods;

    ClassType(std::string n) : Type(TypeKind::Class), name(n) {}

    std::string toString() const override {
        return name;
    }
};

} // namespace ts
