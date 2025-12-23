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
        fmt::print("{}  Name: {}{}\n", padding, func->name, func->isExported ? " (exported)" : "");
        for (const auto& stmt : func->body) printAst(stmt.get(), indent + 1);
    } else if (auto var = dynamic_cast<const ast::VariableDeclaration*>(node)) {
        if (auto id = dynamic_cast<ast::Identifier*>(var->name.get())) {
            fmt::print("{}  Name: {}{}\n", padding, id->name, var->isExported ? " (exported)" : "");
        } else {
            fmt::print("{}  Name: <pattern>{}\n", padding, var->isExported ? " (exported)" : "");
            printAst(var->name.get(), indent + 1);
        }
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
        fmt::print("{}  Name: {}{}\n", padding, cls->name, cls->isExported ? " (exported)" : "");
        for (const auto& member : cls->members) printAst(member.get(), indent + 1);
    } else if (auto prop = dynamic_cast<const ast::PropertyDefinition*>(node)) {
        fmt::print("{}  Name: {}\n", padding, prop->name);
        if (prop->initializer) printAst(prop->initializer.get(), indent + 1);
    } else if (auto method = dynamic_cast<const ast::MethodDefinition*>(node)) {
        fmt::print("{}  Name: {}\n", padding, method->name);
        for (const auto& stmt : method->body) printAst(stmt.get(), indent + 1);
    } else if (auto imp = dynamic_cast<const ast::ImportDeclaration*>(node)) {
        fmt::print("{}  Module: {}\n", padding, imp->moduleSpecifier);
        if (!imp->defaultImport.empty()) fmt::print("{}  Default: {}\n", padding, imp->defaultImport);
        if (!imp->namespaceImport.empty()) fmt::print("{}  Namespace: {}\n", padding, imp->namespaceImport);
        for (const auto& spec : imp->namedImports) {
            fmt::print("{}  Named: {} (as {})\n", padding, spec.propertyName.empty() ? spec.name : spec.propertyName, spec.name);
        }
    } else if (auto exp = dynamic_cast<const ast::ExportDeclaration*>(node)) {
        if (!exp->moduleSpecifier.empty()) fmt::print("{}  Module: {}\n", padding, exp->moduleSpecifier);
        if (exp->isStarExport) fmt::print("{}  Star Export\n", padding);
        for (const auto& spec : exp->namedExports) {
            fmt::print("{}  Named: {} (as {})\n", padding, spec.propertyName.empty() ? spec.name : spec.propertyName, spec.name);
        }
    } else if (auto iface = dynamic_cast<const ast::InterfaceDeclaration*>(node)) {
        fmt::print("{}  Name: {}{}\n", padding, iface->name, iface->isExported ? " (exported)" : "");
        for (const auto& member : iface->members) printAst(member.get(), indent + 1);
    }
}

int main(int argc, char** argv) {
#ifdef _MSC_VER
    // Disable the "Abort, Retry, Ignore" dialog and redirect to stderr
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    std::cerr << "Compiler starting..." << std::endl;

    try {
        cxxopts::Options options("ts-aot", "TypeScript AOT Compiler");
        options.add_options()
            ("o,output", "Output file", cxxopts::value<std::string>())
            ("d,debug-ast", "Print AST", cxxopts::value<bool>()->default_value("false"))
            ("dump-ir", "Dump LLVM IR", cxxopts::value<bool>()->default_value("false"))
            ("O,opt", "Optimization level (0, 1, 2, 3, s, z)", cxxopts::value<std::string>()->default_value("0"))
            ("runtime-bc", "Path to runtime bitcode for LTO", cxxopts::value<std::string>())
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
        
        std::cerr << "Loading AST from " << inputFile << "..." << std::endl;
        auto program = ast::loadAst(inputFile);
        if (result["debug-ast"].as<bool>()) {
            printAst(program.get());
        }

        std::cerr << "Analyzing..." << std::endl;
        ts::Analyzer analyzer;
        analyzer.analyze(program.get(), inputFile);

        if (analyzer.getErrorCount() > 0) {
            fmt::print(stderr, "Compilation failed with {} errors.\n", analyzer.getErrorCount());
            return 1;
        }

        std::cerr << "Monomorphizing..." << std::endl;
        ts::Monomorphizer monomorphizer;
        monomorphizer.monomorphize(program.get(), analyzer);
        
        std::cerr << "Generating IR..." << std::endl;
        ts::IRGenerator irGen;
        irGen.setOptLevel(result["opt"].as<std::string>());
        if (result.count("runtime-bc")) {
            irGen.setRuntimeBitcode(result["runtime-bc"].as<std::string>());
        }
        irGen.generate(program.get(), monomorphizer.getSpecializations(), analyzer);
        
        if (result["dump-ir"].as<bool>()) {
            irGen.dumpIR();
        }

        if (result.count("output")) {
            std::string outputFile = result["output"].as<std::string>();
            irGen.emitObjectCode(outputFile);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
