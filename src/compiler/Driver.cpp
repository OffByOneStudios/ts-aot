#include "Driver.h"
#include "ast/AstLoader.h"
#include "analysis/Analyzer.h"
#include "analysis/Monomorphizer.h"
#include "codegen/IRGenerator.h"
#include "codegen/CodeGenerator.h"
#include "codegen/LinkerDriver.h"
#include "extensions/ExtensionLoader.h"
#include "hir/ASTToHIR.h"
#include "hir/HIRPrinter.h"
#include "hir/HIRToLLVM.h"
#include "hir/LoweringRegistry.h"
#include "hir/passes/PassManager.h"
#include "hir/passes/TypePropagationPass.h"
#include "hir/passes/ConstantFoldingPass.h"
#include "hir/passes/MethodResolutionPass.h"
#include "hir/passes/BuiltinResolutionPass.h"
#include "hir/passes/DeadCodeEliminationPass.h"
#include "hir/passes/InliningPass.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ts {

Driver::Driver(const DriverOptions& opts) : options(opts) {}
Driver::~Driver() {}

int Driver::run() {
    // Load extension contracts
    auto& extRegistry = ext::ExtensionRegistry::instance();
    extRegistry.loadDefaultExtensions();

    // Register lowerings from extension contracts
    ::hir::LoweringRegistry::instance().registerFromExtensions();

    std::string tsFile = options.inputFile;
    std::string jsonFile;
    bool isTemporaryJson = false;

    // If input is .ts, .tsx, .js, or .jsx, run the Node.js parser
    auto ext = std::filesystem::path(tsFile).extension();
    if (ext == ".ts" || ext == ".tsx" || ext == ".js" || ext == ".jsx") {
        jsonFile = std::filesystem::path(tsFile).replace_extension(".json").string();
        // If we are in a "driver" mode, we might want to use a temp file instead of overwriting user's .json
        // but for now, let's just use the same name but in a temp directory if possible, or just next to it.
        // Actually, let's use a temp file.
        char tempPath[MAX_PATH];
        char tempFile[MAX_PATH];
        if (GetTempPathA(MAX_PATH, tempPath)) {
            if (GetTempFileNameA(tempPath, "tsaot", 0, tempFile)) {
                jsonFile = tempFile;
                isTemporaryJson = true;
            }
        }

        if (options.verbose) {
            SPDLOG_INFO("Parsing {}...", tsFile);
        }
        if (!runNodeParser(tsFile, jsonFile)) {
            return 1;
        }
    } else {
        jsonFile = tsFile;
    }

    try {
        if (options.verbose) {
            SPDLOG_INFO("Loading AST from {}...", jsonFile);
        }
        auto program = ast::loadAst(jsonFile);

        if (isTemporaryJson) {
            std::filesystem::remove(jsonFile);
        }

        if (options.debugAst) {
            ast::printAst(program.get());
        }

        if (options.verbose) {
            SPDLOG_INFO("Analyzing...");
        }
        ts::Analyzer analyzer;
        analyzer.setVerbose(options.verbose);

        // Set project root and load tsconfig.json if available
        std::filesystem::path tsFilePath = std::filesystem::absolute(tsFile);
        std::filesystem::path projectRoot = tsFilePath.parent_path();
        analyzer.setProjectRoot(projectRoot.string());

        // Load tsconfig.json - either from explicit path or auto-detect
        if (!options.projectFile.empty()) {
            // Explicit --project path specified
            if (!analyzer.loadTsConfig(options.projectFile)) {
                SPDLOG_WARN("Could not load tsconfig.json from {}", options.projectFile);
            }
        } else {
            // Auto-detect: search upward from input file for tsconfig.json
            std::filesystem::path searchPath = projectRoot;
            while (!searchPath.empty()) {
                std::filesystem::path tsconfigPath = searchPath / "tsconfig.json";
                if (std::filesystem::exists(tsconfigPath)) {
                    analyzer.loadTsConfig(tsconfigPath.string());
                    break;
                }
                auto parent = searchPath.parent_path();
                if (parent == searchPath) break;
                searchPath = parent;
            }
        }

        analyzer.analyze(program.get(), tsFile);

        if (options.dumpTypes) {
            analyzer.dumpTypes(program.get());
        }

        if (analyzer.getErrorCount() > 0) {
            SPDLOG_ERROR("Compilation failed with {} errors.", analyzer.getErrorCount());
            return 1;
        }

        if (options.verbose) {
            SPDLOG_INFO("Monomorphizing...");
        }
        ts::Monomorphizer monomorphizer;
        monomorphizer.monomorphize(program.get(), analyzer);

        // Choose between HIR pipeline and traditional IRGenerator
        llvm::Module* modulePtr = nullptr;
        std::unique_ptr<llvm::Module> hirOwnedModule;  // Only used for HIR pipeline
        std::unique_ptr<ts::IRGenerator> irGen;        // Only used for traditional pipeline

        if (options.useHir) {
            // HIR Pipeline: AST -> HIR -> LLVM IR
            if (options.verbose) {
                SPDLOG_INFO("Using HIR pipeline...");
                SPDLOG_INFO("Lowering AST to HIR...");
            }

            std::string moduleName = std::filesystem::path(tsFile).stem().string();
            hir::ASTToHIR astToHir;
            auto hirModule = astToHir.lower(program.get(), moduleName);

            // Run HIR optimization passes
            if (options.verbose) {
                SPDLOG_INFO("Running HIR passes...");
            }

            hir::PassManager passManager;
            passManager.addPass(std::make_unique<hir::TypePropagationPass>());
            passManager.addPass(std::make_unique<hir::ConstantFoldingPass>());
            passManager.addPass(std::make_unique<hir::DeadCodeEliminationPass>());
            passManager.addPass(std::make_unique<hir::InliningPass>());
            passManager.addPass(std::make_unique<hir::MethodResolutionPass>());
            passManager.addPass(std::make_unique<hir::BuiltinResolutionPass>());

            auto passResult = passManager.run(*hirModule);
            if (!passResult.success()) {
                SPDLOG_ERROR("HIR pass failed: {}", passResult.error);
                return 1;
            }

            // Dump final HIR (after all optimization passes)
            if (options.dumpHir) {
                hir::HIRPrinter printer(std::cout);
                printer.print(*hirModule);
            }

            if (options.verbose) {
                SPDLOG_INFO("Lowering HIR to LLVM IR...");
            }

            // Create LLVM context for HIR pipeline (owned by this scope)
            static llvm::LLVMContext hirContext;
            hir::HIRToLLVM hirToLlvm(hirContext);
            hirOwnedModule = hirToLlvm.lower(hirModule.get(), moduleName);
            modulePtr = hirOwnedModule.get();

            if (options.dumpIR) {
                modulePtr->print(llvm::outs(), nullptr);
            }
        } else {
            // Traditional Pipeline: AST -> LLVM IR directly
            if (options.verbose) {
                SPDLOG_INFO("Generating IR...");
            }
            irGen = std::make_unique<ts::IRGenerator>();
            irGen->setVerbose(options.verbose);
            irGen->setOptLevel(options.optLevel);
            if (!options.runtimeBitcode.empty()) {
                irGen->setRuntimeBitcode(options.runtimeBitcode);
            }
            irGen->setDebug(options.debug);
            irGen->setDebugRuntime(options.debugRuntime);
            irGen->generate(program.get(), monomorphizer.getSpecializations(), analyzer, tsFile);

            if (options.dumpIR) {
                irGen->dumpIR();
            }

            modulePtr = irGen->getModule();
        }

        std::string objFile;
        if (options.compileOnly) {
            objFile = options.outputFile.empty() ? 
                std::filesystem::path(tsFile).replace_extension(".obj").string() : 
                options.outputFile;
        } else {
            objFile = std::filesystem::path(tsFile).replace_extension(".obj").string();
        }

        if (options.verbose) {
            SPDLOG_INFO("Emitting object code to {}...", objFile);
        }
        ts::CodeGenerator codeGen(modulePtr);
        if (!codeGen.emitObjectFile(objFile, options.optLevel)) {
            return 1;
        }

        if (!options.compileOnly) {
            std::string exeOutput = options.outputFile.empty() ? 
                std::filesystem::path(tsFile).replace_extension(".exe").string() : 
                options.outputFile;

            if (options.verbose) {
                SPDLOG_INFO("Linking {}...", exeOutput);
            }
            ts::LinkerDriver::Options linkOpts;
            linkOpts.outputPath = exeOutput;
            linkOpts.objectFiles.push_back(objFile);
            linkOpts.debug = options.debug;
            linkOpts.debugRuntime = options.debugRuntime;
            
            // Add compiler directory to library paths
            // We need to find where the compiler is running from
            char buffer[MAX_PATH];
            GetModuleFileNameA(NULL, buffer, MAX_PATH);
            std::filesystem::path compilerPath = std::filesystem::path(buffer).parent_path();
            
            linkOpts.libraryPaths.push_back(compilerPath.string());
            linkOpts.libraryPaths.push_back((compilerPath / "lib").string());
            
            // Development paths - order matters! Put the appropriate one first
            if (options.debugRuntime) {
                SPDLOG_INFO("Using DEBUG runtime library");
                linkOpts.libraryPaths.push_back((compilerPath / ".." / ".." / "runtime" / "Debug").string());
                linkOpts.libraryPaths.push_back((compilerPath / ".." / ".." / "runtime" / "Release").string());
            } else {
                linkOpts.libraryPaths.push_back((compilerPath / ".." / ".." / "runtime" / "Release").string());
                linkOpts.libraryPaths.push_back((compilerPath / ".." / ".." / "runtime" / "Debug").string());
            }
            
            // vcpkg paths (relative to root) - debug vs release
            std::filesystem::path rootPath = compilerPath / ".." / ".." / ".." / "..";
            if (options.debugRuntime) {
                linkOpts.libraryPaths.push_back((rootPath / "vcpkg_installed" / "x64-windows-static" / "debug" / "lib").string());
            }
            linkOpts.libraryPaths.push_back((rootPath / "vcpkg_installed" / "x64-windows-static" / "lib").string());
            linkOpts.libraryPaths.push_back((rootPath / "vcpkg_installed" / "x64-windows" / "lib").string());

            for (const auto& path : options.libraryPaths) {
                linkOpts.libraryPaths.push_back(path);
            }

            linkOpts.libraries.push_back("tsruntime.lib");
            linkOpts.libraries.push_back("tommath.lib");

            // Runtime depends on spdlog/fmt when SPDLOG_COMPILED_LIB is enabled.
            if (options.debugRuntime) {
                linkOpts.libraries.push_back("spdlogd.lib");
                linkOpts.libraries.push_back("fmtd.lib");
            } else {
                linkOpts.libraries.push_back("spdlog.lib");
                linkOpts.libraries.push_back("fmt.lib");
            }
            
            // Windows system libraries
            linkOpts.libraries.push_back("ws2_32.lib");
            linkOpts.libraries.push_back("user32.lib");
            linkOpts.libraries.push_back("advapi32.lib");
            linkOpts.libraries.push_back("iphlpapi.lib");
            linkOpts.libraries.push_back("shell32.lib");
            linkOpts.libraries.push_back("crypt32.lib");
            linkOpts.libraries.push_back("bcrypt.lib");

            if (!ts::LinkerDriver::link(linkOpts)) {
                if (options.verbose) {
                    SPDLOG_ERROR("Linking failed.");
                }
                return 1;
            }
            
            // Clean up temporary object file
            try {
                std::filesystem::remove(objFile);
            } catch (...) {}

            if (options.verbose) {
                SPDLOG_INFO("Successfully created {}", exeOutput);
            }

            if (options.runAfterLink) {
                if (options.verbose) {
                    SPDLOG_INFO("Running {}...", exeOutput);
                }
                std::string runCmd = (std::filesystem::path(".") / exeOutput).string();
                int runResult = std::system(runCmd.c_str());
                return runResult;
            }
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error: {}", e.what());
        return 1;
    }

    return 0;
}

bool Driver::runNodeParser(const std::string& tsFile, const std::string& jsonFile) {
    std::string nodeExe = findNodeExecutable();
    std::string parserScript = findParserScript();

    if (nodeExe.empty()) {
        SPDLOG_ERROR("Error: 'node' executable not found in PATH.");
        return false;
    }

    if (parserScript.empty()) {
        SPDLOG_ERROR("Error: Could not find 'dump_ast.js' script.");
        return false;
    }

    std::string command = fmt::format("{} \"{}\" \"{}\" \"{}\"", nodeExe, parserScript, tsFile, jsonFile);
    if (options.verbose) {
        SPDLOG_DEBUG("Executing: {}", command);
    }
    
    int result = std::system(command.c_str());
    return result == 0;
}

std::string Driver::findNodeExecutable() {
    // Simple check for node in PATH
    // On Windows, we can use 'where node' or just rely on system() finding it.
    // But for absolute path, we might want to be more careful.
    return "node"; 
}

std::string Driver::findParserScript() {
    // 1. Check relative to compiler executable
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::filesystem::path compilerPath = std::filesystem::path(buffer).parent_path();
    
    std::vector<std::filesystem::path> searchPaths = {
        compilerPath / "scripts" / "dump_ast.js",
        compilerPath / ".." / "scripts" / "dump_ast.js",
        compilerPath / ".." / ".." / "scripts" / "dump_ast.js",
        compilerPath / ".." / ".." / ".." / "scripts" / "dump_ast.js",
        compilerPath / ".." / ".." / ".." / ".." / "scripts" / "dump_ast.js"
    };

    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return std::filesystem::absolute(path).string();
        }
    }

    return "";
}

} // namespace ts
