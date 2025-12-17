#include "Analyzer.h"
#include <iostream>
#include <fmt/core.h>

namespace ts {

using namespace ast;

Analyzer::Analyzer() {}

void Analyzer::analyze(Program* program) {
    visitProgram(program);
}

void Analyzer::visit(Node* node) {
    if (!node) return;

    if (auto p = dynamic_cast<Program*>(node)) visitProgram(p);
    else if (auto f = dynamic_cast<FunctionDeclaration*>(node)) visitFunctionDeclaration(f);
    else if (auto v = dynamic_cast<VariableDeclaration*>(node)) visitVariableDeclaration(v);
    else if (auto e = dynamic_cast<ExpressionStatement*>(node)) visitExpressionStatement(e);
    else if (auto r = dynamic_cast<ReturnStatement*>(node)) visitReturnStatement(r);
    else if (auto c = dynamic_cast<CallExpression*>(node)) visitCallExpression(c);
    else if (auto n = dynamic_cast<NewExpression*>(node)) visitNewExpression(n);
    else if (auto obj = dynamic_cast<ObjectLiteralExpression*>(node)) visitObjectLiteralExpression(obj);
    else if (auto arr = dynamic_cast<ArrayLiteralExpression*>(node)) visitArrayLiteralExpression(arr);
    else if (auto elem = dynamic_cast<ElementAccessExpression*>(node)) visitElementAccessExpression(elem);
    else if (auto pa = dynamic_cast<PropertyAccessExpression*>(node)) visitPropertyAccessExpression(pa);
    else if (auto bin = dynamic_cast<BinaryExpression*>(node)) visitBinaryExpression(bin);
    else if (auto assign = dynamic_cast<AssignmentExpression*>(node)) visitAssignmentExpression(assign);
    else if (auto ifStmt = dynamic_cast<IfStatement*>(node)) visitIfStatement(ifStmt);
    else if (auto whileStmt = dynamic_cast<WhileStatement*>(node)) visitWhileStatement(whileStmt);
    else if (auto forStmt = dynamic_cast<ForStatement*>(node)) visitForStatement(forStmt);
    else if (auto forOfStmt = dynamic_cast<ForOfStatement*>(node)) visitForOfStatement(forOfStmt);
    else if (auto sw = dynamic_cast<SwitchStatement*>(node)) visitSwitchStatement(sw);
    else if (auto br = dynamic_cast<BreakStatement*>(node)) visitBreakStatement(br);
    else if (auto cont = dynamic_cast<ContinueStatement*>(node)) visitContinueStatement(cont);
    else if (auto block = dynamic_cast<BlockStatement*>(node)) visitBlockStatement(block);
    else if (auto id = dynamic_cast<Identifier*>(node)) visitIdentifier(id);
    else if (auto sl = dynamic_cast<StringLiteral*>(node)) visitStringLiteral(sl);
    else if (auto nl = dynamic_cast<NumericLiteral*>(node)) visitNumericLiteral(nl);
    else if (auto bl = dynamic_cast<BooleanLiteral*>(node)) visitBooleanLiteral(bl);
    else if (auto arrow = dynamic_cast<ArrowFunction*>(node)) visitArrowFunction(arrow);
    else if (auto tmpl = dynamic_cast<TemplateExpression*>(node)) visitTemplateExpression(tmpl);
    else if (auto pre = dynamic_cast<PrefixUnaryExpression*>(node)) visitPrefixUnaryExpression(pre);
    else if (auto sup = dynamic_cast<SuperExpression*>(node)) visitSuperExpression(sup);
    else if (auto cls = dynamic_cast<ClassDeclaration*>(node)) visitClassDeclaration(cls);
    else if (auto inter = dynamic_cast<InterfaceDeclaration*>(node)) visitInterfaceDeclaration(inter);

    if (auto expr = dynamic_cast<Expression*>(node)) {
        expr->inferredType = lastType;
    }
}

void Analyzer::visitProgram(Program* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

std::shared_ptr<Type> parseType(const std::string& typeName, SymbolTable& symbols) {
    if (typeName == "number") return std::make_shared<Type>(TypeKind::Double);
    if (typeName == "string") return std::make_shared<Type>(TypeKind::String);
    if (typeName == "boolean") return std::make_shared<Type>(TypeKind::Boolean);
    if (typeName == "void") return std::make_shared<Type>(TypeKind::Void);
    if (typeName == "any") return std::make_shared<Type>(TypeKind::Any);
    
    // Look up in symbol table for classes
    auto type = symbols.lookupType(typeName);
    if (type) return type;

    return std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitFunctionDeclaration(FunctionDeclaration* node) {
    auto funcType = std::make_shared<FunctionType>();
    if (!node->returnType.empty()) {
        funcType->returnType = parseType(node->returnType, symbols);
    } else {
        funcType->returnType = std::make_shared<Type>(TypeKind::Void); 
    }
    
    for (const auto& param : node->parameters) {
        if (!param->type.empty()) {
            funcType->paramTypes.push_back(parseType(param->type, symbols));
        } else {
            funcType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        }
    }

    symbols.define(node->name, funcType);

    symbols.enterScope();
    // Define parameters in scope
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        symbols.define(node->parameters[i]->name, funcType->paramTypes[i]);
    }

    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
    symbols.exitScope();
}

void Analyzer::visitVariableDeclaration(VariableDeclaration* node) {
    auto type = std::make_shared<Type>(TypeKind::Any);
    if (node->initializer) {
        visit(node->initializer.get());
        if (lastType) {
            type = lastType;
        }
    }
    symbols.define(node->name, type);
}

void Analyzer::visitExpressionStatement(ExpressionStatement* node) {
    visit(node->expression.get());
}

void Analyzer::visitCallExpression(CallExpression* node) {
    visit(node->callee.get());
    
    // Check for property access calls (methods)
    if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->callee.get())) {
        if (prop->name == "charCodeAt") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<Type>(TypeKind::Int);
             return;
        } else if (prop->name == "split") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
             return;
        } else if (prop->name == "trim") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "substring") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "startsWith") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<Type>(TypeKind::Boolean);
             return;
        } else if (prop->name == "sort") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<Type>(TypeKind::Void);
             return;
        } else if (prop->name == "set") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<Type>(TypeKind::Void);
             return;
        } else if (prop->name == "get") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<Type>(TypeKind::Int);
             return;
        } else if (prop->name == "has") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<Type>(TypeKind::Boolean);
             return;
        }
        
        // Check for Math methods
        if (auto obj = dynamic_cast<Identifier*>(prop->expression.get())) {
            if (obj->name == "Math") {
                if (prop->name == "min" || prop->name == "max" || prop->name == "abs" || prop->name == "floor") {
                    for (auto& arg : node->arguments) visit(arg.get());
                    lastType = std::make_shared<Type>(TypeKind::Int);
                    return;
                }
            } else if (obj->name == "fs") {
                if (prop->name == "readFileSync") {
                    for (auto& arg : node->arguments) visit(arg.get());
                    lastType = std::make_shared<Type>(TypeKind::String);
                    return;
                }
            } else if (obj->name == "crypto") {
                if (prop->name == "md5") {
                    for (auto& arg : node->arguments) visit(arg.get());
                    lastType = std::make_shared<Type>(TypeKind::String);
                    return;
                }
            }
        }

        // Check for class methods
        visit(prop->expression.get());
        auto objType = lastType;
        if (objType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(objType);
            if (cls->methods.count(prop->name)) {
                auto methodType = cls->methods[prop->name];
                lastType = methodType->returnType;
                for (auto& arg : node->arguments) visit(arg.get());
                return;
            }
        }
    }
    
    std::string calleeName;
    if (auto id = dynamic_cast<Identifier*>(node->callee.get())) {
        calleeName = id->name;
        
        auto sym = symbols.lookup(calleeName);
        if (sym) {
             if (sym->type->kind == TypeKind::Function) {
                 auto funcType = std::static_pointer_cast<FunctionType>(sym->type);
                 lastType = funcType->returnType;
             }
        }

        if (calleeName == "parseInt") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<Type>(TypeKind::Int);
             return;
        }
    }

    std::vector<std::shared_ptr<Type>> argTypes;
    for (auto& arg : node->arguments) {
        visit(arg.get());
        if (lastType) {
            argTypes.push_back(lastType);
        } else {
            argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        }
    }

    if (!calleeName.empty()) {
        functionUsages[calleeName].push_back(argTypes);
    }
    
    // For now, assume calls return Any unless we know better
    lastType = std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitNewExpression(NewExpression* node) {
    visit(node->expression.get());
    for (auto& arg : node->arguments) {
        visit(arg.get());
    }
    
    if (auto id = dynamic_cast<Identifier*>(node->expression.get())) {
        if (id->name == "Map") {
            lastType = std::make_shared<Type>(TypeKind::Map);
            return;
        } else if (id->name == "Array") {
            lastType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
            return;
        }

        // Check for user-defined classes
        auto sym = symbols.lookup(id->name);
        if (sym && sym->type->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(sym->type);
            if (funcType->returnType->kind == TypeKind::Class) {
                lastType = funcType->returnType;
                // TODO: Validate arguments against funcType->paramTypes
                return;
            }
        }
    }
    
    lastType = std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitObjectLiteralExpression(ObjectLiteralExpression* node) {
    auto objType = std::make_shared<ObjectType>();
    for (auto& prop : node->properties) {
        visit(prop->initializer.get());
        objType->fields[prop->name] = lastType;
    }
    lastType = objType;
}

void Analyzer::visitArrayLiteralExpression(ArrayLiteralExpression* node) {
    std::shared_ptr<Type> elemType = nullptr;
    for (auto& el : node->elements) {
        visit(el.get());
        if (!elemType) {
            elemType = lastType;
        }
        // TODO: Check for mixed types and upgrade to Any
    }
    if (!elemType) elemType = std::make_shared<Type>(TypeKind::Any);
    lastType = std::make_shared<ArrayType>(elemType);
}

void Analyzer::visitElementAccessExpression(ElementAccessExpression* node) {
    visit(node->expression.get());
    auto arrayType = std::dynamic_pointer_cast<ArrayType>(lastType);
    
    visit(node->argumentExpression.get());
    // TODO: Verify index is integer
    
    if (arrayType) {
        lastType = arrayType->elementType;
    } else {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitPropertyAccessExpression(PropertyAccessExpression* node) {
    visit(node->expression.get());
    auto objType = lastType;
    
    if (node->name == "length") {
        if (objType->kind == TypeKind::String || objType->kind == TypeKind::Array) {
            lastType = std::make_shared<Type>(TypeKind::Int);
        } else {
            lastType = std::make_shared<Type>(TypeKind::Any);
        }
    } else if (node->name == "size") {
        if (objType->kind == TypeKind::Map) {
            lastType = std::make_shared<Type>(TypeKind::Int);
        } else {
            lastType = std::make_shared<Type>(TypeKind::Any);
        }
    } else {
        if (objType->kind == TypeKind::Object) {
            auto obj = std::static_pointer_cast<ObjectType>(objType);
            if (obj->fields.count(node->name)) {
                lastType = obj->fields[node->name];
                return;
            }
        } else if (objType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(objType);
            
            // Find defining class and access modifier
            std::shared_ptr<ClassType> definingClass = nullptr;
            AccessModifier access = AccessModifier::Public;
            std::shared_ptr<Type> memberType = nullptr;

            auto current = cls;
            while (current) {
                if (current->fields.count(node->name)) {
                    definingClass = current;
                    access = current->fieldAccess[node->name];
                    memberType = current->fields[node->name];
                    break;
                } else if (current->methods.count(node->name)) {
                    definingClass = current;
                    access = current->methodAccess[node->name];
                    memberType = current->methods[node->name];
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
            }
        }
        reportError(fmt::format("Unknown property {}", node->name));
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitBinaryExpression(BinaryExpression* node) {
    visit(node->left.get());
    auto leftType = lastType;
    visit(node->right.get());
    auto rightType = lastType;

    // Simple type inference for binary ops
    if (leftType && rightType) {
        if (leftType->kind == TypeKind::Int && rightType->kind == TypeKind::Int) {
            lastType = std::make_shared<Type>(TypeKind::Int);
        } else if (leftType->isNumber() && rightType->isNumber()) {
            lastType = std::make_shared<Type>(TypeKind::Double);
        } else if (leftType->kind == TypeKind::String || rightType->kind == TypeKind::String) {
            lastType = std::make_shared<Type>(TypeKind::String);
        } else {
            lastType = std::make_shared<Type>(TypeKind::Any);
        }
    } else {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitAssignmentExpression(AssignmentExpression* node) {
    visit(node->left.get());
    visit(node->right.get());
    // In a real compiler, we'd check if left is assignable from right
    // For now, assignment evaluates to the right side type
}

void Analyzer::visitIfStatement(IfStatement* node) {
    visit(node->condition.get());
    visit(node->thenStatement.get());
    if (node->elseStatement) {
        visit(node->elseStatement.get());
    }
}

void Analyzer::visitWhileStatement(WhileStatement* node) {
    visit(node->condition.get());
    visit(node->body.get());
}

void Analyzer::visitForStatement(ForStatement* node) {
    symbols.enterScope();
    if (node->initializer) visit(node->initializer.get());
    if (node->condition) visit(node->condition.get());
    if (node->incrementor) visit(node->incrementor.get());
    visit(node->body.get());
    symbols.exitScope();
}

void Analyzer::visitForOfStatement(ForOfStatement* node) {
    symbols.enterScope();
    
    visit(node->expression.get());
    auto iterableType = lastType;
    
    std::shared_ptr<Type> elemType;
    if (iterableType->kind == TypeKind::Array) {
        elemType = std::static_pointer_cast<ArrayType>(iterableType)->elementType;
    } else if (iterableType->kind == TypeKind::String) {
        elemType = std::make_shared<Type>(TypeKind::String); // Iterating string yields strings
    } else {
        elemType = std::make_shared<Type>(TypeKind::Any);
    }

    // Handle initializer (VariableDeclaration)
    if (auto varDecl = dynamic_cast<VariableDeclaration*>(node->initializer.get())) {
        symbols.define(varDecl->name, elemType);
    }

    visit(node->body.get());
    symbols.exitScope();
}

void Analyzer::visitSwitchStatement(SwitchStatement* node) {
    visit(node->expression.get());
    
    symbols.enterScope();
    for (auto& clause : node->clauses) {
        if (auto caseClause = dynamic_cast<CaseClause*>(clause.get())) {
            visit(caseClause->expression.get());
            for (auto& stmt : caseClause->statements) {
                visit(stmt.get());
            }
        } else if (auto defaultClause = dynamic_cast<DefaultClause*>(clause.get())) {
            for (auto& stmt : defaultClause->statements) {
                visit(stmt.get());
            }
        }
    }
    symbols.exitScope();
}

void Analyzer::visitBreakStatement(BreakStatement* node) {
    // TODO: Check if inside loop
}

void Analyzer::visitContinueStatement(ContinueStatement* node) {
    // TODO: Check if inside loop
}

void Analyzer::visitBlockStatement(BlockStatement* node) {
    symbols.enterScope();
    for (auto& stmt : node->statements) {
        visit(stmt.get());
    }
    symbols.exitScope();
}

void Analyzer::visitIdentifier(Identifier* node) {
    auto symbol = symbols.lookup(node->name);
    if (!symbol) {
        std::cerr << "Warning: Undefined symbol " << node->name << std::endl;
        lastType = std::make_shared<Type>(TypeKind::Any);
    } else {
        lastType = symbol->type;
    }
}

void Analyzer::visitStringLiteral(StringLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::String);
}

void Analyzer::visitNumericLiteral(NumericLiteral* node) {
    // Heuristic: if it has a decimal point, it's a double.
    // But for now, let's just say everything is Double unless we add IntLiteral support in AST
    // Actually, let's check if it's an integer
    if (node->value == (int64_t)node->value) {
        lastType = std::make_shared<Type>(TypeKind::Int);
    } else {
        lastType = std::make_shared<Type>(TypeKind::Double);
    }
}

void Analyzer::visitBooleanLiteral(BooleanLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Boolean);
}

void Analyzer::visitReturnStatement(ReturnStatement* node) {
    if (node->expression) {
        visit(node->expression.get());
        currentReturnType = lastType;
        fmt::print("Return statement inferred type: {}\n", currentReturnType->toString());
    } else {
        currentReturnType = std::make_shared<Type>(TypeKind::Void);
    }
}

void Analyzer::visitArrowFunction(ArrowFunction* node) {
    symbols.enterScope();
    for (auto& param : node->parameters) {
        std::shared_ptr<Type> type = std::make_shared<Type>(TypeKind::Any);
        if (!param->type.empty()) {
            type = parseType(param->type, symbols);
        }
        symbols.define(param->name, type);
    }
    
    visit(node->body.get());
    
    symbols.exitScope();
    
    // TODO: Construct proper FunctionType
    lastType = std::make_shared<Type>(TypeKind::Function);
}

void Analyzer::visitTemplateExpression(TemplateExpression* node) {
    for (auto& span : node->spans) {
        visit(span.expression.get());
    }
    lastType = std::make_shared<Type>(TypeKind::String);
}

void Analyzer::visitPrefixUnaryExpression(PrefixUnaryExpression* node) {
    visit(node->operand.get());
    if (node->op == "!") {
         lastType = std::make_shared<Type>(TypeKind::Boolean);
    }
}

void Analyzer::visitSuperExpression(SuperExpression* node) {
    if (currentClass && currentClass->baseClass) {
        lastType = currentClass->baseClass;
    } else {
        reportError("'super' used outside of a derived class");
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitClassDeclaration(ClassDeclaration* node) {
    auto classType = std::make_shared<ClassType>(node->name);
    
    // 1. Resolve base class
    if (!node->baseClass.empty()) {
        auto base = symbols.lookupType(node->baseClass);
        if (!base || base->kind != TypeKind::Class) {
            reportError(fmt::format("Base class {} not found or not a class", node->baseClass));
        } else {
            classType->baseClass = std::static_pointer_cast<ClassType>(base);
        }
    }

    // 2. Register class type
    symbols.defineType(node->name, classType);

    // 2.5 Resolve implemented interfaces
            for (const auto& interfaceName : node->implementsInterfaces) {
        auto interfaceType = symbols.lookupType(interfaceName);
        if (!interfaceType || interfaceType->kind != TypeKind::Interface) {
            reportError(fmt::format("Interface {} not found or not an interface", interfaceName));
        } else {
            classType->implementsInterfaces.push_back(std::static_pointer_cast<InterfaceType>(interfaceType));
        }
    }

    // 3. Scan members to populate fields and methods
    for (const auto& member : node->members) {
        if (auto prop = dynamic_cast<PropertyDefinition*>(member.get())) {
            classType->fields[prop->name] = parseType(prop->type, symbols);
            classType->fieldAccess[prop->name] = prop->access;
        } else if (auto method = dynamic_cast<MethodDefinition*>(member.get())) {
            auto methodType = std::make_shared<FunctionType>();
            if (!method->returnType.empty()) {
                methodType->returnType = parseType(method->returnType, symbols);
            } else {
                methodType->returnType = std::make_shared<Type>(TypeKind::Void);
            }
            for (const auto& param : method->parameters) {
                if (!param->type.empty()) {
                    methodType->paramTypes.push_back(parseType(param->type, symbols));
                } else {
                    methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                }
            }
            classType->methods[method->name] = methodType;
            classType->methodAccess[method->name] = method->access;
        }
    }

    // 4. Register constructor (if any) or default constructor
    auto ctorType = std::make_shared<FunctionType>();
    ctorType->returnType = classType;
    
    if (classType->methods.count("constructor")) {
        auto ctorMethod = classType->methods["constructor"];
        ctorType->paramTypes = ctorMethod->paramTypes;
    }
    
    symbols.define(node->name, ctorType);

    // 5. Visit method bodies
    auto oldClass = currentClass;
    currentClass = classType;
    for (const auto& member : node->members) {
        if (auto method = dynamic_cast<MethodDefinition*>(member.get())) {
            visitMethodDefinition(method, classType);
        } else if (auto prop = dynamic_cast<PropertyDefinition*>(member.get())) {
            visitPropertyDefinition(prop, classType);
        }
    }
    currentClass = oldClass;

    // 6. Check interface implementations
    for (const auto& inter : classType->implementsInterfaces) {
        checkInterfaceImplementation(classType, inter);
    }
}

void Analyzer::checkInterfaceImplementation(std::shared_ptr<ClassType> classType, std::shared_ptr<InterfaceType> interfaceType) {
    // Check fields
    for (const auto& [name, type] : interfaceType->fields) {
        bool found = false;
        auto current = classType;
        while (current) {
            if (current->fields.count(name)) {
                found = true;
                break;
            }
            current = current->baseClass;
        }
        if (!found) {
            reportError(fmt::format("Class {} does not implement property {} from interface {}", classType->name, name, interfaceType->name));
        }
    }

    // Check methods
    for (const auto& [name, methodType] : interfaceType->methods) {
        if (classType->methods.find(name) == classType->methods.end()) {
            reportError(fmt::format("Class {} does not implement method {} from interface {}", classType->name, name, interfaceType->name));
        }
    }

    // Check base interfaces recursively
    for (const auto& base : interfaceType->baseInterfaces) {
        checkInterfaceImplementation(classType, base);
    }
}

void Analyzer::visitMethodDefinition(MethodDefinition* node, std::shared_ptr<ClassType> classType) {
    symbols.enterScope();
    
    // Define 'this'
    symbols.define("this", classType);

    // Define parameters
    auto methodType = classType->methods[node->name];
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        symbols.define(node->parameters[i]->name, methodType->paramTypes[i]);
    }

    for (auto& stmt : node->body) {
        visit(stmt.get());
    }

    symbols.exitScope();
}

void Analyzer::visitPropertyDefinition(PropertyDefinition* node, std::shared_ptr<ClassType> classType) {
    if (node->initializer) {
        visit(node->initializer.get());
        // TODO: Check type compatibility
    }
}

void Analyzer::visitInterfaceDeclaration(InterfaceDeclaration* node) {
    auto interfaceType = std::make_shared<InterfaceType>(node->name);
    symbols.defineType(node->name, interfaceType);

    for (const auto& baseName : node->baseInterfaces) {
        auto base = symbols.lookupType(baseName);
        if (base && base->kind == TypeKind::Interface) {
            interfaceType->baseInterfaces.push_back(std::static_pointer_cast<InterfaceType>(base));
        }
    }

    for (const auto& member : node->members) {
        if (auto prop = dynamic_cast<PropertyDefinition*>(member.get())) {
            interfaceType->fields[prop->name] = parseType(prop->type, symbols);
        } else if (auto method = dynamic_cast<MethodDefinition*>(member.get())) {
            auto methodType = std::make_shared<FunctionType>();
            methodType->returnType = parseType(method->returnType, symbols);
            for (const auto& param : method->parameters) {
                methodType->paramTypes.push_back(parseType(param->type, symbols));
            }
            interfaceType->methods[method->name] = methodType;
        }
    }
}

std::shared_ptr<Type> Analyzer::analyzeFunctionBody(FunctionDeclaration* node, const std::vector<std::shared_ptr<Type>>& argTypes) {
    // If return type is annotated, use it (unless it's explicit 'any', which we want to refine if possible)
    if (!node->returnType.empty() && node->returnType != "any") {
        return parseType(node->returnType, symbols);
    }

    // Create a new scope for the function body
    symbols.enterScope();
    
    // Bind parameters to the provided types
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        if (i < argTypes.size()) {
            symbols.define(node->parameters[i]->name, argTypes[i]);
        }
    }

    currentReturnType = std::make_shared<Type>(TypeKind::Void); // Default to Void

    for (auto& stmt : node->body) {
        visit(stmt.get());
    }

    // Debug
    fmt::print("Analyzed function {} return type: {}\n", node->name, currentReturnType->toString());

    symbols.exitScope();
    return currentReturnType;
}

void Analyzer::reportError(const std::string& message) {
    fmt::print(stderr, "Error: {}\n", message);
    errorCount++;
}

} // namespace ts
