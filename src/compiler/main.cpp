#include <fmt/core.h>
#include <cxxopts.hpp>
#include "ast/AstLoader.h"
#include <iostream>

void printAst(const ast::Node* node, int indent = 0) {
    std::string padding(indent * 2, ' ');
    fmt::print("{}{}\n", padding, node->getKind());
    
    if (auto prog = dynamic_cast<const ast::Program*>(node)) {
        for (const auto& stmt : prog->body) printAst(stmt.get(), indent + 1);
    } else if (auto func = dynamic_cast<const ast::FunctionDeclaration*>(node)) {
        fmt::print("{}  Name: {}\n", padding, func->name);
        for (const auto& stmt : func->body) printAst(stmt.get(), indent + 1);
    } else if (auto var = dynamic_cast<const ast::VariableDeclaration*>(node)) {
        fmt::print("{}  Name: {}\n", padding, var->name);
        if (var->initializer) printAst(var->initializer.get(), indent + 1);
    } else if (auto exprStmt = dynamic_cast<const ast::ExpressionStatement*>(node)) {
        printAst(exprStmt->expression.get(), indent + 1);
    } else if (auto bin = dynamic_cast<const ast::BinaryExpression*>(node)) {
        fmt::print("{}  Op: {}\n", padding, bin->op);
        printAst(bin->left.get(), indent + 1);
        printAst(bin->right.get(), indent + 1);
    } else if (auto call = dynamic_cast<const ast::CallExpression*>(node)) {
        printAst(call->callee.get(), indent + 1);
        for (const auto& arg : call->arguments) printAst(arg.get(), indent + 1);
    }
}

int main(int argc, char** argv) {
    cxxopts::Options options("ts-aot", "TypeScript AOT Compiler");
    options.add_options()
        ("o,output", "Output file", cxxopts::value<std::string>())
        ("d,debug-ast", "Print AST", cxxopts::value<bool>()->default_value("false"))
        ("h,help", "Print usage")
        ("input", "Input file", cxxopts::value<std::string>());

    options.parse_positional({"input"});
    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    if (!result.count("input")) {
        std::cerr << "Error: No input file specified." << std::endl;
        return 1;
    }

    std::string inputFile = result["input"].as<std::string>();
    
    try {
        auto program = ast::loadAst(inputFile);
        if (result["debug-ast"].as<bool>()) {
            printAst(program.get());
        }
        fmt::print("Successfully loaded AST from {}\n", inputFile);
    } catch (const std::exception& e) {
        std::cerr << "Error loading AST: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
