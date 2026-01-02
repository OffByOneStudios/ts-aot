#include "Driver.h"
#include "ast/AstLoader.h"
#include "analysis/Analyzer.h"
#include "analysis/Monomorphizer.h"
#include "codegen/IRGenerator.h"
#include "codegen/CodeGenerator.h"
#include "codegen/LinkerDriver.h"
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
    std::string tsFile = options.inputFile;
    std::string jsonFile;
    bool isTemporaryJson = false;

    // If input is .ts or .js, run the Node.js parser
    auto ext = std::filesystem::path(tsFile).extension();
    if (ext == ".ts" || ext == ".js") {
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
        
        if (options.verbose) {
            SPDLOG_INFO("Generating IR...");
        }
        ts::IRGenerator irGen;
        irGen.setVerbose(options.verbose);
        irGen.setOptLevel(options.optLevel);
        if (!options.runtimeBitcode.empty()) {
            irGen.setRuntimeBitcode(options.runtimeBitcode);
        }
        irGen.setDebug(options.debug);
        irGen.setDebugRuntime(options.debugRuntime);
        irGen.generate(program.get(), monomorphizer.getSpecializations(), analyzer, tsFile);
        
        if (options.dumpIR) {
            irGen.dumpIR();
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
        ts::CodeGenerator codeGen(irGen.getModule());
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
