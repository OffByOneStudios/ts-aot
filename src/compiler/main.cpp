#include <fmt/core.h>
#include <cxxopts.hpp>
#include "ast/AstLoader.h"
#include "analysis/Analyzer.h"
#include "analysis/Monomorphizer.h"
#include "codegen/IRGenerator.h"
#include <iostream>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

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
    } else if (auto cls = dynamic_cast<const ast::ClassDeclaration*>(node)) {
        fmt::print("{}  Name: {}\n", padding, cls->name);
        for (const auto& member : cls->members) printAst(member.get(), indent + 1);
    } else if (auto prop = dynamic_cast<const ast::PropertyDefinition*>(node)) {
        fmt::print("{}  Name: {}\n", padding, prop->name);
        if (prop->initializer) printAst(prop->initializer.get(), indent + 1);
    } else if (auto method = dynamic_cast<const ast::MethodDefinition*>(node)) {
        fmt::print("{}  Name: {}\n", padding, method->name);
        for (const auto& stmt : method->body) printAst(stmt.get(), indent + 1);
    }
}

int main(int argc, char** argv) {
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif

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

        ts::Analyzer analyzer;
        analyzer.analyze(program.get());

        ts::Monomorphizer monomorphizer;
        monomorphizer.monomorphize(program.get(), analyzer);
        
        fmt::print("Generated {} specializations:\n", monomorphizer.getSpecializations().size());
    for (const auto& spec : monomorphizer.getSpecializations()) {
        fmt::print("  {} -> {}\n", spec.originalName, spec.specializedName);
    }

    ts::IRGenerator irGen;
    irGen.generate(monomorphizer.getSpecializations(), analyzer);
    
    if (result.count("debug-ast")) { // Reuse debug flag for now, or add a new one
        fmt::print("\n--- Generated IR ---\n");
        irGen.dumpIR();
    }

    if (result.count("output")) {
        std::string outputFile = result["output"].as<std::string>();
        irGen.emitObjectCode(outputFile);
        fmt::print("Emitted object code to {}\n", outputFile);
    }

    fmt::print("Successfully loaded AST from {}\n", inputFile);
    } catch (const std::exception& e) {
        std::cerr << "Error loading AST: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
