#include "Driver.h"
#include "ast/AstLoader.h"
#include "analysis/Analyzer.h"
#include "analysis/Monomorphizer.h"
#include "codegen/IRGenerator.h"
#include "codegen/CodeGenerator.h"
#include "codegen/LinkerDriver.h"
#include <fmt/core.h>
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

    // If input is .ts, run the Node.js parser
    if (std::filesystem::path(tsFile).extension() == ".ts") {
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
            std::cerr << "Parsing " << tsFile << "..." << std::endl;
        }
        if (!runNodeParser(tsFile, jsonFile)) {
            return 1;
        }
    } else {
        jsonFile = tsFile;
    }

    try {
        if (options.verbose) {
            std::cerr << "Loading AST from " << jsonFile << "..." << std::endl;
        }
        auto program = ast::loadAst(jsonFile);
        
        if (isTemporaryJson) {
            std::filesystem::remove(jsonFile);
        }

        if (options.debugAst) {
            ast::printAst(program.get());
        }

        if (options.verbose) {
            std::cerr << "Analyzing..." << std::endl;
        }
        ts::Analyzer analyzer;
        analyzer.setVerbose(options.verbose);
        analyzer.analyze(program.get(), tsFile);

        if (options.dumpTypes) {
            analyzer.dumpTypes(program.get());
        }

        if (analyzer.getErrorCount() > 0) {
            fmt::print(stderr, "Compilation failed with {} errors.\n", analyzer.getErrorCount());
            return 1;
        }

        if (options.verbose) {
            std::cerr << "Monomorphizing..." << std::endl;
        }
        ts::Monomorphizer monomorphizer;
        monomorphizer.monomorphize(program.get(), analyzer);
        
        if (options.verbose) {
            std::cerr << "Generating IR..." << std::endl;
        }
        ts::IRGenerator irGen;
        irGen.setVerbose(options.verbose);
        irGen.setOptLevel(options.optLevel);
        if (!options.runtimeBitcode.empty()) {
            irGen.setRuntimeBitcode(options.runtimeBitcode);
        }
        irGen.generate(program.get(), monomorphizer.getSpecializations(), analyzer);
        
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
            std::cerr << "Emitting object code to " << objFile << "..." << std::endl;
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
                std::cerr << "Linking " << exeOutput << "..." << std::endl;
            }
            ts::LinkerDriver::Options linkOpts;
            linkOpts.outputPath = exeOutput;
            linkOpts.objectFiles.push_back(objFile);
            
            // Add compiler directory to library paths
            // We need to find where the compiler is running from
            char buffer[MAX_PATH];
            GetModuleFileNameA(NULL, buffer, MAX_PATH);
            std::filesystem::path compilerPath = std::filesystem::path(buffer).parent_path();
            
            linkOpts.libraryPaths.push_back(compilerPath.string());
            linkOpts.libraryPaths.push_back((compilerPath / "lib").string());
            
            // Development paths
            linkOpts.libraryPaths.push_back((compilerPath / ".." / ".." / "runtime" / "Release").string());
            linkOpts.libraryPaths.push_back((compilerPath / ".." / ".." / "runtime" / "Debug").string());
            
            // vcpkg paths (relative to root)
            std::filesystem::path rootPath = compilerPath / ".." / ".." / ".." / "..";
            linkOpts.libraryPaths.push_back((rootPath / "vcpkg_installed" / "x64-windows-static" / "lib").string());
            linkOpts.libraryPaths.push_back((rootPath / "vcpkg_installed" / "x64-windows" / "lib").string());

            for (const auto& path : options.libraryPaths) {
                linkOpts.libraryPaths.push_back(path);
            }

            linkOpts.libraries.push_back("tsruntime.lib");
            
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
                    std::cerr << "Linking failed." << std::endl;
                }
                return 1;
            }
            
            // Clean up temporary object file
            try {
                std::filesystem::remove(objFile);
            } catch (...) {}

            if (options.verbose) {
                std::cerr << "Successfully created " << exeOutput << std::endl;
            }

            if (options.runAfterLink) {
                if (options.verbose) {
                    std::cerr << "Running " << exeOutput << "..." << std::endl;
                }
                std::string runCmd = (std::filesystem::path(".") / exeOutput).string();
                int runResult = std::system(runCmd.c_str());
                return runResult;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

bool Driver::runNodeParser(const std::string& tsFile, const std::string& jsonFile) {
    std::string nodeExe = findNodeExecutable();
    std::string parserScript = findParserScript();

    if (nodeExe.empty()) {
        std::cerr << "Error: 'node' executable not found in PATH." << std::endl;
        return false;
    }

    if (parserScript.empty()) {
        std::cerr << "Error: Could not find 'dump_ast.js' script." << std::endl;
        return false;
    }

    std::string command = fmt::format("{} \"{}\" \"{}\" \"{}\"", nodeExe, parserScript, tsFile, jsonFile);
    if (options.verbose) {
        std::cerr << "Executing: " << command << std::endl;
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
