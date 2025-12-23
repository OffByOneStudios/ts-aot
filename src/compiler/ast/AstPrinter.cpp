#include "AstLoader.h"
#include <fmt/core.h>

namespace ast {

void printAst(const Node* node, int indent) {
    if (!node) return;
    
    std::string padding(indent * 2, ' ');
    fmt::print("{}{}\n", padding, node->getKind());
    
    if (auto prog = dynamic_cast<const Program*>(node)) {
        for (const auto& stmt : prog->body) printAst(stmt.get(), indent + 1);
    } else if (auto func = dynamic_cast<const FunctionDeclaration*>(node)) {
        fmt::print("{}  Name: {}{}\n", padding, func->name, func->isExported ? " (exported)" : "");
        for (const auto& stmt : func->body) printAst(stmt.get(), indent + 1);
    } else if (auto var = dynamic_cast<const VariableDeclaration*>(node)) {
        if (auto id = dynamic_cast<Identifier*>(var->name.get())) {
            fmt::print("{}  Name: {}{}\n", padding, id->name, var->isExported ? " (exported)" : "");
        } else {
            fmt::print("{}  Name: <pattern>{}\n", padding, var->isExported ? " (exported)" : "");
            printAst(var->name.get(), indent + 1);
        }
        if (var->initializer) printAst(var->initializer.get(), indent + 1);
    } else if (auto exprStmt = dynamic_cast<const ExpressionStatement*>(node)) {
        printAst(exprStmt->expression.get(), indent + 1);
    } else if (auto bin = dynamic_cast<const BinaryExpression*>(node)) {
        fmt::print("{}  Op: {}\n", padding, bin->op);
        printAst(bin->left.get(), indent + 1);
        printAst(bin->right.get(), indent + 1);
    } else if (auto call = dynamic_cast<const CallExpression*>(node)) {
        printAst(call->callee.get(), indent + 1);
        for (const auto& arg : call->arguments) printAst(arg.get(), indent + 1);
    } else if (auto cls = dynamic_cast<const ClassDeclaration*>(node)) {
        fmt::print("{}  Name: {}{}\n", padding, cls->name, cls->isExported ? " (exported)" : "");
        for (const auto& member : cls->members) printAst(member.get(), indent + 1);
    } else if (auto prop = dynamic_cast<const PropertyDefinition*>(node)) {
        fmt::print("{}  Name: {}\n", padding, prop->name);
        if (prop->initializer) printAst(prop->initializer.get(), indent + 1);
    } else if (auto method = dynamic_cast<const MethodDefinition*>(node)) {
        fmt::print("{}  Name: {}\n", padding, method->name);
        for (const auto& stmt : method->body) printAst(stmt.get(), indent + 1);
    } else if (auto imp = dynamic_cast<const ImportDeclaration*>(node)) {
        fmt::print("{}  Module: {}\n", padding, imp->moduleSpecifier);
        if (!imp->defaultImport.empty()) fmt::print("{}  Default: {}\n", padding, imp->defaultImport);
        if (!imp->namespaceImport.empty()) fmt::print("{}  Namespace: {}\n", padding, imp->namespaceImport);
        for (const auto& spec : imp->namedImports) {
            fmt::print("{}  Named: {} (as {})\n", padding, spec.propertyName.empty() ? spec.name : spec.propertyName, spec.name);
        }
    } else if (auto exp = dynamic_cast<const ExportDeclaration*>(node)) {
        if (!exp->moduleSpecifier.empty()) fmt::print("{}  Module: {}\n", padding, exp->moduleSpecifier);
        if (exp->isStarExport) fmt::print("{}  Star Export\n", padding);
        for (const auto& spec : exp->namedExports) {
            fmt::print("{}  Named: {} (as {})\n", padding, spec.propertyName.empty() ? spec.name : spec.propertyName, spec.name);
        }
    } else if (auto iface = dynamic_cast<const InterfaceDeclaration*>(node)) {
        fmt::print("{}  Name: {}{}\n", padding, iface->name, iface->isExported ? " (exported)" : "");
        for (const auto& member : iface->members) printAst(member.get(), indent + 1);
    }
}

} // namespace ast
