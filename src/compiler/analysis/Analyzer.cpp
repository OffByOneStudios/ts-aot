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
    else if (auto br = dynamic_cast<BreakStatement*>(node)) visitBreakStatement(br);
    else if (auto cont = dynamic_cast<ContinueStatement*>(node)) visitContinueStatement(cont);
    else if (auto block = dynamic_cast<BlockStatement*>(node)) visitBlockStatement(block);
    else if (auto id = dynamic_cast<Identifier*>(node)) visitIdentifier(id);
    else if (auto sl = dynamic_cast<StringLiteral*>(node)) visitStringLiteral(sl);
    else if (auto nl = dynamic_cast<NumericLiteral*>(node)) visitNumericLiteral(nl);
    else if (auto pre = dynamic_cast<PrefixUnaryExpression*>(node)) visitPrefixUnaryExpression(pre);

    if (auto expr = dynamic_cast<Expression*>(node)) {
        expr->inferredType = lastType;
    }
}

void Analyzer::visitProgram(Program* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

std::shared_ptr<Type> parseType(const std::string& typeName) {
    if (typeName == "number") return std::make_shared<Type>(TypeKind::Double);
    if (typeName == "string") return std::make_shared<Type>(TypeKind::String);
    if (typeName == "boolean") return std::make_shared<Type>(TypeKind::Boolean);
    if (typeName == "void") return std::make_shared<Type>(TypeKind::Void);
    return std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitFunctionDeclaration(FunctionDeclaration* node) {
    auto funcType = std::make_shared<FunctionType>();
    if (!node->returnType.empty()) {
        funcType->returnType = parseType(node->returnType);
    } else {
        funcType->returnType = std::make_shared<Type>(TypeKind::Void); 
    }
    
    for (const auto& param : node->parameters) {
        if (!param->type.empty()) {
            funcType->paramTypes.push_back(parseType(param->type));
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
    }
    
    std::string calleeName;
    if (auto id = dynamic_cast<Identifier*>(node->callee.get())) {
        calleeName = id->name;
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
        }
    }
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
        }
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

void Analyzer::visitReturnStatement(ReturnStatement* node) {
    if (node->expression) {
        visit(node->expression.get());
        currentReturnType = lastType;
        fmt::print("Return statement inferred type: {}\n", currentReturnType->toString());
    } else {
        currentReturnType = std::make_shared<Type>(TypeKind::Void);
    }
}

void Analyzer::visitPrefixUnaryExpression(PrefixUnaryExpression* node) {
    visit(node->operand.get());
    if (node->op == "!") {
         lastType = std::make_shared<Type>(TypeKind::Boolean);
    }
}

std::shared_ptr<Type> Analyzer::analyzeFunctionBody(FunctionDeclaration* node, const std::vector<std::shared_ptr<Type>>& argTypes) {
    // If return type is annotated, use it (unless it's explicit 'any', which we want to refine if possible)
    if (!node->returnType.empty() && node->returnType != "any") {
        return parseType(node->returnType);
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

} // namespace ts
