#include "Analyzer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>

namespace ts {
using namespace ast;
void Analyzer::visitIdentifier(ast::Identifier* node) {
    if (node->name == "null") {
        lastType = std::make_shared<Type>(TypeKind::Null);
        node->inferredType = lastType;
        return;
    }
    if (node->name == "undefined") {
        lastType = std::make_shared<Type>(TypeKind::Undefined);
        node->inferredType = lastType;
        return;
    }

    auto sym = symbols.lookup(node->name);
    if (sym) {
        lastType = sym->type;
        SPDLOG_DEBUG("  Lookup {}: {}", node->name, lastType->toString());
    } else {
        // Check if it's a class name
        auto type = symbols.lookupType(node->name);
        if (type && type->kind == TypeKind::Class) {
            auto ctorType = std::make_shared<FunctionType>();
            ctorType->returnType = type;
            lastType = ctorType;
        } else if (type) {
            lastType = type;
        } else {
            SPDLOG_ERROR("Failed to find identifier: {}", node->name);
            reportError("Undefined variable " + node->name);
            lastType = std::make_shared<Type>(TypeKind::Any);
        }
    }
    node->inferredType = lastType;
}

void Analyzer::visitElementAccessExpression(ast::ElementAccessExpression* node) {
    visit(node->expression.get());
    auto objType = lastType;

    if (objType->kind == TypeKind::Null || objType->kind == TypeKind::Undefined) {
        if (node->isOptional) {
            lastType = std::make_shared<Type>(TypeKind::Undefined);
            node->inferredType = lastType;
            return;
        }
        reportError(fmt::format("Object is possibly '{}'.", objType->kind == TypeKind::Null ? "null" : "undefined"));
        lastType = std::make_shared<Type>(TypeKind::Any);
        return;
    }
    if (objType->kind == TypeKind::Unknown) {
        reportError("Object is of type 'unknown'.");
        lastType = std::make_shared<Type>(TypeKind::Any);
        return;
    }
    
    visit(node->argumentExpression.get());
    auto indexType = lastType;
    
    if (objType->kind == TypeKind::Tuple) {
        auto tupleType = std::static_pointer_cast<TupleType>(objType);
        if (auto lit = dynamic_cast<NumericLiteral*>(node->argumentExpression.get())) {
            size_t index = (size_t)lit->value;
            if (index < tupleType->elementTypes.size()) {
                lastType = tupleType->elementTypes[index];
                return;
            }
        }
        // If index is not a literal, we have to return a union of all element types
        if (tupleType->elementTypes.empty()) {
            lastType = std::make_shared<Type>(TypeKind::Any);
        } else {
            lastType = std::make_shared<UnionType>(tupleType->elementTypes);
        }
        return;
    }

    auto arrayType = std::dynamic_pointer_cast<ArrayType>(objType);
    if (arrayType) {
        lastType = arrayType->elementType;
    } else if (objType->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(objType);
        if (cls->name == "Uint8Array" || cls->name == "Uint32Array") {
            lastType = std::make_shared<Type>(TypeKind::Int);
        } else if (cls->name == "Float64Array") {
            lastType = std::make_shared<Type>(TypeKind::Double);
        } else {
            lastType = std::make_shared<Type>(TypeKind::Any);
        }
    } else {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitPropertyAccessExpression(ast::PropertyAccessExpression* node) {
    visit(node->expression.get());
    auto objType = lastType;

    if (objType->kind == TypeKind::Null || objType->kind == TypeKind::Undefined) {
        if (node->isOptional) {
            lastType = std::make_shared<Type>(TypeKind::Undefined);
            node->inferredType = lastType;
            return;
        }
        reportError(fmt::format("Object is possibly '{}'.", objType->kind == TypeKind::Null ? "null" : "undefined"));
        lastType = std::make_shared<Type>(TypeKind::Any);
        return;
    }
    if (objType->kind == TypeKind::Unknown) {
        reportError("Object is of type 'unknown'.");
        lastType = std::make_shared<Type>(TypeKind::Any);
        return;
    }

    while (objType && objType->kind == TypeKind::TypeParameter) {
        auto tp = std::static_pointer_cast<TypeParameterType>(objType);
        if (tp->constraint) {
            objType = tp->constraint;
        } else {
            objType = std::make_shared<Type>(TypeKind::Any);
            break;
        }
    }
    
    if (node->name == "length") {
        SPDLOG_DEBUG("  Accessing .length on type: {}", objType->toString());
        if (objType->kind == TypeKind::String || objType->kind == TypeKind::Array || objType->kind == TypeKind::Tuple || objType->kind == TypeKind::Any) {
            lastType = std::make_shared<Type>(TypeKind::Int);
            return;
        }
    }

    if (objType->kind == TypeKind::Int || objType->kind == TypeKind::Double) {
        if (node->name == "toString") {
            auto fn = std::make_shared<FunctionType>();
            fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // radix
            fn->returnType = std::make_shared<Type>(TypeKind::String);
            lastType = fn;
            return;
        } else if (node->name == "toFixed") {
            auto fn = std::make_shared<FunctionType>();
            fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // digits
            fn->returnType = std::make_shared<Type>(TypeKind::String);
            lastType = fn;
            return;
        }
    }

    if (objType->kind == TypeKind::Array || objType->kind == TypeKind::Tuple) {
        if (node->name == "push") {
            auto pushFn = std::make_shared<FunctionType>();
            pushFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            pushFn->returnType = std::make_shared<Type>(TypeKind::Void);
            lastType = pushFn;
            return;
        } else if (node->name == "pop" || node->name == "shift") {
            auto popFn = std::make_shared<FunctionType>();
            popFn->returnType = std::make_shared<Type>(TypeKind::Any);
            lastType = popFn;
            return;
        } else if (node->name == "unshift") {
            auto unshiftFn = std::make_shared<FunctionType>();
            unshiftFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            unshiftFn->returnType = std::make_shared<Type>(TypeKind::Void);
            lastType = unshiftFn;
            return;
        } else if (node->name == "sort") {
            auto sortFn = std::make_shared<FunctionType>();
            sortFn->returnType = std::make_shared<Type>(TypeKind::Void);
            lastType = sortFn;
            return;
        } else if (node->name == "indexOf") {
            auto idxFn = std::make_shared<FunctionType>();
            idxFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            idxFn->returnType = std::make_shared<Type>(TypeKind::Int);
            lastType = idxFn;
            return;
        } else if (node->name == "includes") {
            auto incFn = std::make_shared<FunctionType>();
            incFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            incFn->returnType = std::make_shared<Type>(TypeKind::Boolean);
            lastType = incFn;
            return;
        } else if (node->name == "at") {
            auto atFn = std::make_shared<FunctionType>();
            atFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
            atFn->returnType = std::make_shared<Type>(TypeKind::Any);
            lastType = atFn;
            return;
        } else if (node->name == "join") {
            auto joinFn = std::make_shared<FunctionType>();
            joinFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
            joinFn->returnType = std::make_shared<Type>(TypeKind::String);
            lastType = joinFn;
            return;
        } else if (node->name == "slice") {
            auto sliceFn = std::make_shared<FunctionType>();
            sliceFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
            sliceFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
            sliceFn->returnType = objType;
            lastType = sliceFn;
            return;
        } else if (node->name == "forEach" || node->name == "map" || node->name == "filter" || 
                   node->name == "reduce" || node->name == "some" || node->name == "every" || 
                   node->name == "find" || node->name == "findIndex") {
            auto fn = std::make_shared<FunctionType>();
            fn->returnType = std::make_shared<Type>(TypeKind::Any);
            lastType = fn;
            return;
        }
    }

    if (objType->kind == TypeKind::String) {
        if (node->name == "charCodeAt") {
            auto charAtFn = std::make_shared<FunctionType>();
            charAtFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
            charAtFn->returnType = std::make_shared<Type>(TypeKind::Int);
            lastType = charAtFn;
            return;
        } else if (node->name == "charAt") {
            auto charAtFn = std::make_shared<FunctionType>();
            charAtFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
            charAtFn->returnType = std::make_shared<Type>(TypeKind::String);
            lastType = charAtFn;
            return;
        } else if (node->name == "match") {
            auto matchFn = std::make_shared<FunctionType>();
            auto regExpType = symbols.lookupType("RegExp");
            if (regExpType) {
                matchFn->paramTypes.push_back(regExpType);
            } else {
                matchFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            }
            matchFn->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
            lastType = matchFn;
            return;
        } else if (node->name == "search") {
            auto searchFn = std::make_shared<FunctionType>();
            auto regExpType = symbols.lookupType("RegExp");
            if (regExpType) {
                searchFn->paramTypes.push_back(regExpType);
            } else {
                searchFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            }
            searchFn->returnType = std::make_shared<Type>(TypeKind::Int);
            lastType = searchFn;
            return;
        } else if (node->name == "split") {
            auto splitFn = std::make_shared<FunctionType>();
            auto regExpType = symbols.lookupType("RegExp");
            auto stringType = std::make_shared<Type>(TypeKind::String);
            auto unionType = std::make_shared<UnionType>(std::vector<std::shared_ptr<Type>>{stringType});
            if (regExpType) {
                unionType->types.push_back(regExpType);
            } else {
                unionType->types.push_back(std::make_shared<Type>(TypeKind::Any));
            }
            splitFn->paramTypes.push_back(unionType);
            splitFn->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
            lastType = splitFn;
            return;
        } else if (node->name == "trim" || node->name == "toLowerCase" || node->name == "toUpperCase") {
            auto strFn = std::make_shared<FunctionType>();
            strFn->returnType = std::make_shared<Type>(TypeKind::String);
            lastType = strFn;
            return;
        } else if (node->name == "substring" || node->name == "slice") {
            auto subFn = std::make_shared<FunctionType>();
            subFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
            subFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
            subFn->returnType = std::make_shared<Type>(TypeKind::String);
            lastType = subFn;
            return;
        } else if (node->name == "repeat") {
            auto repeatFn = std::make_shared<FunctionType>();
            repeatFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
            repeatFn->returnType = std::make_shared<Type>(TypeKind::String);
            lastType = repeatFn;
            return;
        } else if (node->name == "padStart" || node->name == "padEnd") {
            auto padFn = std::make_shared<FunctionType>();
            padFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
            padFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
            padFn->returnType = std::make_shared<Type>(TypeKind::String);
            lastType = padFn;
            return;
        } else if (node->name == "replace" || node->name == "replaceAll") {
            auto repFn = std::make_shared<FunctionType>();
            auto regExpType = symbols.lookupType("RegExp");
            auto stringType = std::make_shared<Type>(TypeKind::String);
            auto unionType = std::make_shared<UnionType>(std::vector<std::shared_ptr<Type>>{stringType});
            if (regExpType) {
                unionType->types.push_back(regExpType);
            } else {
                unionType->types.push_back(std::make_shared<Type>(TypeKind::Any));
            }
            repFn->paramTypes.push_back(unionType);
            repFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
            repFn->returnType = std::make_shared<Type>(TypeKind::String);
            lastType = repFn;
            return;
        }
    }

    if (node->name == "size" && (objType->kind == TypeKind::Map || objType->kind == TypeKind::SetType)) {
        lastType = std::make_shared<Type>(TypeKind::Int);
        return;
    }

    if (objType->kind == TypeKind::SetType) {
        if (node->name == "add") {
            auto setAdd = std::make_shared<FunctionType>();
            setAdd->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            setAdd->returnType = std::make_shared<Type>(TypeKind::Void);
            lastType = setAdd;
            return;
        } else if (node->name == "has") {
            auto setHas = std::make_shared<FunctionType>();
            setHas->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            setHas->returnType = std::make_shared<Type>(TypeKind::Boolean);
            lastType = setHas;
            return;
        } else if (node->name == "delete") {
            auto setDel = std::make_shared<FunctionType>();
            setDel->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            setDel->returnType = std::make_shared<Type>(TypeKind::Boolean);
            lastType = setDel;
            return;
        } else if (node->name == "clear") {
            auto setClear = std::make_shared<FunctionType>();
            setClear->returnType = std::make_shared<Type>(TypeKind::Void);
            lastType = setClear;
            return;
        }
    }

    if (objType->kind == TypeKind::Map) {
        if (node->name == "set") {
            auto mapSet = std::make_shared<FunctionType>();
            mapSet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            mapSet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            mapSet->returnType = std::make_shared<Type>(TypeKind::Void);
            lastType = mapSet;
            return;
        } else if (node->name == "get") {
            auto mapGet = std::make_shared<FunctionType>();
            mapGet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            mapGet->returnType = std::make_shared<Type>(TypeKind::Any);
            lastType = mapGet;
            return;
        } else if (node->name == "has") {
            auto mapHas = std::make_shared<FunctionType>();
            mapHas->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            mapHas->returnType = std::make_shared<Type>(TypeKind::Boolean);
            lastType = mapHas;
            return;
        } else if (node->name == "delete") {
            auto mapDel = std::make_shared<FunctionType>();
            mapDel->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            mapDel->returnType = std::make_shared<Type>(TypeKind::Boolean);
            lastType = mapDel;
            return;
        } else if (node->name == "clear") {
            auto mapClear = std::make_shared<FunctionType>();
            mapClear->returnType = std::make_shared<Type>(TypeKind::Void);
            lastType = mapClear;
            return;
        } else if (node->name == "forEach") {
            auto mapForEach = std::make_shared<FunctionType>();
            mapForEach->returnType = std::make_shared<Type>(TypeKind::Void);
            lastType = mapForEach;
            return;
        } else if (node->name == "keys" || node->name == "values" || node->name == "entries") {
            auto mapIter = std::make_shared<FunctionType>();
            mapIter->returnType = std::make_shared<Type>(TypeKind::Any);
            lastType = mapIter;
            return;
        }
    }

    if (objType->kind == TypeKind::Namespace) {
        auto ns = std::static_pointer_cast<NamespaceType>(objType);
        auto sym = ns->module->exports->lookup(node->name);
        if (sym) {
            lastType = sym->type;
            return;
        }
        auto type = ns->module->exports->lookupType(node->name);
        if (type) {
            lastType = type;
            return;
        }
        reportError(fmt::format("Module does not export {}", node->name));
        lastType = std::make_shared<Type>(TypeKind::Any);
        return;
    }

    if (objType->kind == TypeKind::Enum) {
        auto enumType = std::static_pointer_cast<EnumType>(objType);
        if (enumType->members.count(node->name)) {
            lastType = std::make_shared<Type>(TypeKind::Int);
            return;
        }
        reportError("Unknown property " + node->name + " on enum " + enumType->name);
        lastType = std::make_shared<Type>(TypeKind::Any);
        return;
    }

    if (objType->kind == TypeKind::Union) {
            auto unionType = std::static_pointer_cast<UnionType>(objType);
            std::vector<std::shared_ptr<Type>> memberTypes;
            for (auto& t : unionType->types) {
                // Recursively check each type in the union
                lastType = t;
                // We need a way to check property access without side effects or with a way to restore state
                // For now, let's assume we can just check if it's an object/class/interface
                std::shared_ptr<Type> foundType = nullptr;
                if (t->kind == TypeKind::Object) {
                    auto obj = std::static_pointer_cast<ObjectType>(t);
                    if (obj->fields.count(node->name)) foundType = obj->fields[node->name];
                } else if (t->kind == TypeKind::Class) {
                    auto cls = std::static_pointer_cast<ClassType>(t);
                    auto current = cls;
                    while (current) {
                        if (current->fields.count(node->name)) { foundType = current->fields[node->name]; break; }
                        if (current->methods.count(node->name)) { foundType = current->methods[node->name]; break; }
                        current = current->baseClass;
                    }
                } else if (t->kind == TypeKind::Interface) {
                    auto inter = std::static_pointer_cast<InterfaceType>(t);
                    if (inter->fields.count(node->name)) foundType = inter->fields[node->name];
                    else if (inter->methods.count(node->name)) foundType = inter->methods[node->name];
                }

                if (!foundType) {
                    // Property not found in one of the union members
                    lastType = std::make_shared<Type>(TypeKind::Any);
                    return;
                }
                memberTypes.push_back(foundType);
            }
            if (memberTypes.size() == 1) lastType = memberTypes[0];
            else lastType = std::make_shared<UnionType>(memberTypes);
            return;
        } else if (objType->kind == TypeKind::Intersection) {
            auto interType = std::static_pointer_cast<IntersectionType>(objType);
            std::vector<std::shared_ptr<Type>> memberTypes;
            for (auto& t : interType->types) {
                std::shared_ptr<Type> foundType = nullptr;
                if (t->kind == TypeKind::Object) {
                    auto obj = std::static_pointer_cast<ObjectType>(t);
                    if (obj->fields.count(node->name)) foundType = obj->fields[node->name];
                } else if (t->kind == TypeKind::Class) {
                    auto cls = std::static_pointer_cast<ClassType>(t);
                    auto current = cls;
                    while (current) {
                        if (current->fields.count(node->name)) { foundType = current->fields[node->name]; break; }
                        if (current->methods.count(node->name)) { foundType = current->methods[node->name]; break; }
                        current = current->baseClass;
                    }
                } else if (t->kind == TypeKind::Interface) {
                    auto inter = std::static_pointer_cast<InterfaceType>(t);
                    if (inter->fields.count(node->name)) foundType = inter->fields[node->name];
                    else if (inter->methods.count(node->name)) foundType = inter->methods[node->name];
                }
                if (foundType) memberTypes.push_back(foundType);
            }
            if (memberTypes.empty()) lastType = std::make_shared<Type>(TypeKind::Any);
            else if (memberTypes.size() == 1) lastType = memberTypes[0];
            else lastType = std::make_shared<IntersectionType>(memberTypes);
            return;
        }

        if (objType->kind == TypeKind::Object) {
            auto obj = std::static_pointer_cast<ObjectType>(objType);
            fmt::print("DEBUG: Accessing property {} on ObjectType. Available fields: ", node->name);
            for (auto const& [name, type] : obj->fields) fmt::print("{} ", name);
            fmt::print("\n");
            if (obj->fields.count(node->name)) {
                lastType = obj->fields[node->name];
                return;
            }
        } else if (objType->kind == TypeKind::Interface) {
            auto inter = std::static_pointer_cast<InterfaceType>(objType);
            if (inter->fields.count(node->name)) {
                lastType = inter->fields[node->name];
                fmt::print("Found property {} on interface {} with type {}\n", node->name, inter->name, lastType->toString());
                return;
            } else if (inter->methods.count(node->name)) {
                lastType = inter->methods[node->name];
                return;
            }
            // TODO: Check base interfaces
        } else if (objType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(objType);
            
            // Find defining class and access modifier
            std::shared_ptr<ClassType> definingClass = nullptr;
            AccessModifier access = AccessModifier::Public;
            std::shared_ptr<Type> memberType = nullptr;

            std::string name = node->name;
            if (name.starts_with("#")) {
                if (!currentClass) {
                    reportError(fmt::format("Private field {} can only be accessed within a class.", name));
                    lastType = std::make_shared<Type>(TypeKind::Any);
                    return;
                }
                // Private fields are strictly scoped to the class they are defined in.
                // In TS, #x in class A is only accessible in A.
                std::string className = currentClass->originalName.empty() ? currentClass->name : currentClass->originalName;
                name = manglePrivateName(name, className);
            }

            auto current = cls;
            while (current) {
                if (current->fields.count(name)) {
                    definingClass = current;
                    access = current->fieldAccess[name];
                    memberType = current->fields[name];
                    break;
                } else if (current->methods.count(name)) {
                    definingClass = current;
                    access = current->methodAccess[name];
                    memberType = current->methods[name];
                    break;
                } else if (current->getters.count(name)) {
                    definingClass = current;
                    access = current->methodAccess[name];
                    memberType = current->getters[name]->returnType;
                    break;
                }
                current = current->baseClass;
            }

            if (definingClass) {
                // Visibility check
                bool allowed = true;
                if (access == AccessModifier::Private) {
                    if (currentClass != definingClass) {
                        reportError(fmt::format("Property {} is private and not accessible from here", node->name));
                    }
                } else if (access == AccessModifier::Protected) {
                    if (!currentClass || !currentClass->isSubclassOf(definingClass)) {
                        reportError(fmt::format("Property {} is protected and not accessible from here", node->name));
                    }
                }
                lastType = memberType;
                return;
            } else {
                fmt::print("DEBUG: Property {} not found on class {}. Available fields: ", node->name, cls->name);
                for (auto const& [name, type] : cls->fields) fmt::print("{} ", name);
                fmt::print("\n");
            }
        } else if (objType->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(objType);
            if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
                
                // Static access
                std::shared_ptr<ClassType> definingClass = nullptr;
                AccessModifier access = AccessModifier::Public;
                std::shared_ptr<Type> memberType = nullptr;

                std::string name = node->name;
                if (name.starts_with("#")) {
                    if (!currentClass) {
                        reportError(fmt::format("Private static field {} can only be accessed within a class.", name));
                        lastType = std::make_shared<Type>(TypeKind::Any);
                        return;
                    }
                    name = manglePrivateName(name, cls->name);
                }

                auto current = cls;
                while (current) {
                    if (current->staticFields.count(name)) {
                        definingClass = current;
                        access = current->staticFieldAccess[name];
                        memberType = current->staticFields[name];
                        break;
                    } else if (current->staticMethods.count(name)) {
                        definingClass = current;
                        access = current->staticMethodAccess[name];
                        memberType = current->staticMethods[name];
                        break;
                    }
                    current = current->baseClass;
                }

                if (definingClass) {
                    if (access == AccessModifier::Private) {
                        if (currentClass != definingClass) {
                            reportError(fmt::format("Static property {} is private and not accessible from here", node->name));
                        }
                    } else if (access == AccessModifier::Protected) {
                        if (!currentClass || !currentClass->isSubclassOf(definingClass)) {
                            reportError(fmt::format("Static property {} is protected and not accessible from here", node->name));
                        }
                    }
                    lastType = memberType;
                    return;
                }
            }
        } else if (objType->kind == TypeKind::Any) {
            lastType = std::make_shared<Type>(TypeKind::Any);
            node->inferredType = lastType;
            return;
        }

        // Built-in methods fallback
        if (node->name == "includes" || node->name == "indexOf" || node->name == "toLowerCase" || node->name == "toUpperCase" || node->name == "slice" || node->name == "join") {
            lastType = std::make_shared<Type>(TypeKind::Any); // Or more specific function type
            return;
        }

        reportError(fmt::format("Unknown property {}", node->name));
        lastType = std::make_shared<Type>(TypeKind::Any);
        node->inferredType = lastType;
}

void Analyzer::visitSuperExpression(ast::SuperExpression* node) {
    if (currentClass && currentClass->baseClass) {
        lastType = currentClass->baseClass;
    } else {
        reportError("'super' used outside of a derived class");
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

} // namespace ts


