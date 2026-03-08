#include "Driver.h"
#include "ast/AstLoader.h"
#include "parser/Parser.h"
#include "analysis/Analyzer.h"
#include "analysis/Monomorphizer.h"
#include "codegen/CodeGenerator.h"
#include "codegen/LinkerDriver.h"
#include "extensions/ExtensionLoader.h"
#include "hir/ASTToHIR.h"
#include "hir/HIRPrinter.h"
#include "hir/HIRToLLVM.h"
#include "hir/LoweringRegistry.h"
#include "hir/passes/PassManager.h"
#include "hir/passes/TypePropagationPass.h"
#include "hir/passes/IntegerOptimizationPass.h"
#include "hir/passes/ConstantFoldingPass.h"
#include "hir/passes/MethodResolutionPass.h"
#include "hir/passes/BuiltinResolutionPass.h"
#include "hir/passes/DeadCodeEliminationPass.h"
#include "hir/passes/InliningPass.h"
#include "hir/passes/EscapeAnalysisPass.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <set>
#include <unordered_map>

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
    bool useNativeParser = false;

    // If input is .ts, .tsx, .js, or .jsx, we need to parse it
    auto ext = std::filesystem::path(tsFile).extension();
    if (ext == ".ts" || ext == ".tsx" || ext == ".js" || ext == ".jsx") {
        useNativeParser = options.useNativeParser;

        if (!useNativeParser) {
            // Legacy path: use Node.js dump_ast.js
            jsonFile = std::filesystem::path(tsFile).replace_extension(".json").string();
            char tempPath[MAX_PATH];
            char tempFile[MAX_PATH];
            if (GetTempPathA(MAX_PATH, tempPath)) {
                if (GetTempFileNameA(tempPath, "tsaot", 0, tempFile)) {
                    jsonFile = tempFile;
                    isTemporaryJson = true;
                }
            }

            if (options.verbose) {
                SPDLOG_INFO("Parsing {} (Node.js parser)...", tsFile);
            }
            if (!runNodeParser(tsFile, jsonFile)) {
                return 1;
            }
        }
    } else {
        jsonFile = tsFile;
    }

    try {
        std::unique_ptr<ast::Program> program;

        if (useNativeParser) {
            // Native C++ parser path
            if (options.verbose) {
                SPDLOG_INFO("Parsing {} (native parser)...", tsFile);
            }
            std::ifstream srcFile(tsFile);
            if (!srcFile.is_open()) {
                SPDLOG_ERROR("Could not open input file: {}", tsFile);
                return 1;
            }
            std::string source((std::istreambuf_iterator<char>(srcFile)),
                                std::istreambuf_iterator<char>());
            srcFile.close();

            parser::Parser nativeParser;
            program = nativeParser.parse(source, tsFile);
        } else {
            // Legacy path: load from JSON
            if (options.verbose) {
                SPDLOG_INFO("Loading AST from {}...", jsonFile);
            }
            program = ast::loadAst(jsonFile);

            if (isTemporaryJson) {
                std::filesystem::remove(jsonFile);
            }
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

        // IMPORTANT: Declaration order matters for destruction!
        // Context must be declared BEFORE Module so Module is destroyed first.
        llvm::Module* modulePtr = nullptr;
        std::unique_ptr<llvm::LLVMContext> hirContext; // LLVM context (destroyed LAST)
        std::unique_ptr<llvm::Module> hirOwnedModule;  // LLVM module (destroyed BEFORE context)

        // HIR Pipeline: AST -> HIR -> LLVM IR
        if (options.verbose) {
            SPDLOG_INFO("Lowering AST to HIR...");
        }

        std::string moduleName = std::filesystem::path(tsFile).stem().string();
        hir::ASTToHIR astToHir;
        auto hirModule = astToHir.lower(program.get(), monomorphizer.getSpecializations(), moduleName);

        // Run HIR optimization passes
        if (options.verbose) {
            SPDLOG_INFO("Running HIR passes...");
        }

        hir::PassManager passManager;
        passManager.addPass(std::make_unique<hir::TypePropagationPass>());
        passManager.addPass(std::make_unique<hir::IntegerOptimizationPass>());
        passManager.addPass(std::make_unique<hir::ConstantFoldingPass>());
        passManager.addPass(std::make_unique<hir::DeadCodeEliminationPass>());
        passManager.addPass(std::make_unique<hir::InliningPass>());
        passManager.addPass(std::make_unique<hir::EscapeAnalysisPass>());
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

        // Create LLVM context (must outlive the module)
        hirContext = std::make_unique<llvm::LLVMContext>();
        hir::HIRToLLVM hirToLlvm(*hirContext);
        hirToLlvm.setEnableGCStatepoints(options.enableGCStatepoints);
        hirToLlvm.setEmitDebugInfo(options.debug);

        // Embed ICU data path so compiled executables can find icudt74l.dat
        // next to the compiler instead of needing a local copy
        if (!options.bundleIcu) {
            char compBuf[MAX_PATH];
            GetModuleFileNameA(NULL, compBuf, MAX_PATH);
            auto datPath = std::filesystem::path(compBuf).parent_path() / "icudt74l.dat";
            hirToLlvm.setIcuDataPath(datPath.string());
        }

        hirOwnedModule = hirToLlvm.lower(hirModule.get(), moduleName);
        modulePtr = hirOwnedModule.get();

        if (options.dumpIR) {
            modulePtr->print(llvm::outs(), nullptr);
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
        codeGen.setEnableGCStatepoints(options.enableGCStatepoints);
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

            // Extension library paths - only add paths for extensions the program imports.
            // This reduces binary size significantly (e.g., hello world: ~7MB -> ~3.5MB).
            std::filesystem::path extensionsPath = compilerPath / ".." / ".." / ".." / "extensions" / "node";

            // Map from normalized module name to {libName, dirName}
            struct ExtLibInfo { const char* libName; const char* dirName; };
            static const std::unordered_map<std::string, ExtLibInfo> MODULE_TO_LIB = {
                {"fs",              {"ts_fs.lib",              "fs"}},
                {"path",            {"ts_path.lib",            "path"}},
                {"os",              {"ts_os.lib",              "os"}},
                {"http",            {"ts_http.lib",            "http"}},
                {"https",           {"ts_http.lib",            "http"}},
                {"http2",           {"ts_http2.lib",           "http2"}},
                {"net",             {"ts_net.lib",             "net"}},
                {"dns",             {"ts_dns.lib",             "dns"}},
                {"dgram",           {"ts_dgram.lib",           "dgram"}},
                {"crypto",          {"ts_crypto.lib",          "crypto"}},
                {"zlib",            {"ts_zlib.lib",            "zlib"}},
                {"url",             {"ts_url.lib",             "url"}},
                {"util",            {"ts_util.lib",            "util"}},
                {"events",          {"ts_events.lib",          "events"}},
                {"stream",          {"ts_stream.lib",          "stream"}},
                {"readline",        {"ts_readline.lib",        "readline"}},
                {"child_process",   {"ts_child_process.lib",   "child_process"}},
                {"cluster",         {"ts_cluster.lib",         "cluster"}},
                {"assert",          {"ts_assert.lib",          "assert"}},
                {"async_hooks",     {"ts_async_hooks.lib",     "async_hooks"}},
                {"perf_hooks",      {"ts_perf_hooks.lib",      "perf_hooks"}},
                {"string_decoder",  {"ts_string_decoder.lib",  "string_decoder"}},
                {"tty",             {"ts_tty.lib",             "tty"}},
                {"v8",              {"ts_v8.lib",              "v8"}},
                {"vm",              {"ts_vm.lib",              "vm"}},
                {"inspector",       {"ts_inspector.lib",       "inspector"}},
                {"module",          {"ts_module.lib",          "module"}},
            };

            // Determine which extension libraries to link by scanning the LLVM IR
            // for external symbol references with known extension prefixes.
            // This is more reliable than import tracking because the codegen
            // generates calls to extension functions transitively (e.g., fs
            // operations that return streams generate ts_stream_* calls).
            struct SymbolPrefix { const char* prefix; const char* libName; const char* dirName; };
            static const SymbolPrefix SYMBOL_PREFIXES[] = {
                {"ts_fs_",              "ts_fs.lib",              "fs"},
                {"ts_path_",            "ts_path.lib",            "path"},
                {"ts_os_",              "ts_os.lib",              "os"},
                {"ts_http2_",           "ts_http2.lib",           "http2"},
                {"ts_http_",            "ts_http.lib",            "http"},
                {"ts_https_",           "ts_http.lib",            "http"},
                {"ts_net_",             "ts_net.lib",             "net"},
                {"ts_dns_",             "ts_dns.lib",             "dns"},
                {"ts_dgram_",           "ts_dgram.lib",           "dgram"},
                {"ts_crypto_",          "ts_crypto.lib",          "crypto"},
                {"ts_zlib_",            "ts_zlib.lib",            "zlib"},
                {"ts_url_",             "ts_url.lib",             "url"},
                {"ts_querystring_",     "ts_url.lib",             "url"},
                {"ts_util_",            "ts_util.lib",            "util"},
                {"ts_event_emitter_",   "ts_events.lib",          "events"},
                {"ts_events_",          "ts_events.lib",          "events"},
                {"ts_stream_",          "ts_stream.lib",          "stream"},
                {"ts_readable_",        "ts_stream.lib",          "stream"},
                {"ts_writable_",        "ts_stream.lib",          "stream"},
                {"ts_duplex_",          "ts_stream.lib",          "stream"},
                {"ts_transform_",       "ts_stream.lib",          "stream"},
                {"ts_readline_",        "ts_readline.lib",        "readline"},
                {"ts_child_process_",   "ts_child_process.lib",   "child_process"},
                {"ts_cluster_",         "ts_cluster.lib",         "cluster"},
                {"ts_assert_",          "ts_assert.lib",          "assert"},
                {"ts_async_hooks_",     "ts_async_hooks.lib",     "async_hooks"},
                {"ts_perf_hooks_",      "ts_perf_hooks.lib",      "perf_hooks"},
                {"ts_performance_",     "ts_perf_hooks.lib",      "perf_hooks"},
                {"ts_string_decoder_",  "ts_string_decoder.lib",  "string_decoder"},
                {"ts_tty_",             "ts_tty.lib",             "tty"},
                {"ts_v8_",              "ts_v8.lib",              "v8"},
                {"ts_vm_",              "ts_vm.lib",              "vm"},
                {"ts_inspector_",       "ts_inspector.lib",       "inspector"},
                {"ts_module_",          "ts_module.lib",          "module"},
                {"ts_fetch",            "ts_fetch.lib",           "fetch"},
                {"ts_socket_",          "ts_net.lib",             "net"},
                {"ts_server_",          "ts_net.lib",             "net"},
            };

            std::set<std::string> requiredLibs;
            std::set<std::string> requiredDirs;

            // Always include core (console, buffer, process, querystring, tls, etc.)
            requiredDirs.insert("core");

            // Scan LLVM module for external function references
            for (const auto& fn : modulePtr->functions()) {
                if (fn.isDeclaration()) {
                    std::string name = fn.getName().str();
                    for (const auto& sp : SYMBOL_PREFIXES) {
                        if (name.starts_with(sp.prefix)) {
                            requiredLibs.insert(sp.libName);
                            requiredDirs.insert(sp.dirName);
                            break;
                        }
                    }
                }
            }

            // Also include extensions based on analyzer import tracking
            // (catches cases where extension init functions are needed)
            for (const auto& mod : analyzer.getUsedBuiltinModules()) {
                std::string normalized = mod;
                if (normalized.starts_with("node:")) normalized = normalized.substr(5);
                auto it = MODULE_TO_LIB.find(normalized);
                if (it != MODULE_TO_LIB.end()) {
                    requiredLibs.insert(it->second.libName);
                    requiredDirs.insert(it->second.dirName);
                }
            }

            // Transitive dependencies: extensions that create EventEmitter
            // objects need ts_events.lib for the runtime .on() property
            // access to work (ts_builtin_lookup_special("event_emitter_on")).
            static const std::unordered_map<std::string, std::vector<std::pair<const char*, const char*>>> EXT_DEPS = {
                {"ts_net.lib",           {{"ts_events.lib", "events"}, {"ts_stream.lib", "stream"}}},
                {"ts_http.lib",          {{"ts_events.lib", "events"}, {"ts_stream.lib", "stream"}, {"ts_net.lib", "net"}, {"ts_fetch.lib", "fetch"}}},
                {"ts_http2.lib",         {{"ts_events.lib", "events"}, {"ts_stream.lib", "stream"}, {"ts_net.lib", "net"}}},
                {"ts_fetch.lib",         {{"ts_url.lib", "url"}}},
                {"ts_fs.lib",            {{"ts_events.lib", "events"}, {"ts_stream.lib", "stream"}}},
                {"ts_child_process.lib", {{"ts_events.lib", "events"}, {"ts_stream.lib", "stream"}}},
                {"ts_cluster.lib",       {{"ts_events.lib", "events"}, {"ts_child_process.lib", "child_process"}, {"ts_net.lib", "net"}}},
                {"ts_dgram.lib",         {{"ts_events.lib", "events"}}},
                {"ts_readline.lib",      {{"ts_events.lib", "events"}, {"ts_stream.lib", "stream"}}},
                {"ts_stream.lib",        {{"ts_events.lib", "events"}}},
                {"ts_tty.lib",           {{"ts_events.lib", "events"}, {"ts_stream.lib", "stream"}, {"ts_net.lib", "net"}}},
            };
            // Resolve transitive deps (iterate until stable)
            bool changed = true;
            while (changed) {
                changed = false;
                for (const auto& [lib, deps] : EXT_DEPS) {
                    if (requiredLibs.count(lib)) {
                        for (const auto& [depLib, depDir] : deps) {
                            if (!requiredLibs.count(depLib)) {
                                requiredLibs.insert(depLib);
                                requiredDirs.insert(depDir);
                                changed = true;
                            }
                        }
                    }
                }
            }

            // Add library paths only for required extension directories
            for (const auto& dir : requiredDirs) {
                if (options.debugRuntime) {
                    linkOpts.libraryPaths.push_back((extensionsPath / dir / "Debug").string());
                    linkOpts.libraryPaths.push_back((extensionsPath / dir / "Release").string());
                } else {
                    linkOpts.libraryPaths.push_back((extensionsPath / dir / "Release").string());
                    linkOpts.libraryPaths.push_back((extensionsPath / dir / "Debug").string());
                }
            }
            
            // vcpkg paths (relative to root) - debug vs release
            // Priority: x64-windows-static-md (static libs, dynamic CRT) > x64-windows-static > x64-windows
            std::filesystem::path rootPath = compilerPath / ".." / ".." / ".." / "..";
            std::filesystem::path vcpkgPath = rootPath / "vcpkg_installed";
            if (options.debugRuntime) {
                linkOpts.libraryPaths.push_back((vcpkgPath / "x64-windows-static-md" / "debug" / "lib").string());
                linkOpts.libraryPaths.push_back((vcpkgPath / "x64-windows-static" / "debug" / "lib").string());
            }
            linkOpts.libraryPaths.push_back((vcpkgPath / "x64-windows-static-md" / "lib").string());
            linkOpts.libraryPaths.push_back((vcpkgPath / "x64-windows-static" / "lib").string());
            linkOpts.libraryPaths.push_back((vcpkgPath / "x64-windows" / "lib").string());

            for (const auto& path : options.libraryPaths) {
                linkOpts.libraryPaths.push_back(path);
            }

            // Core runtime libraries
            linkOpts.libraries.push_back("tsruntime.lib");
            linkOpts.libraries.push_back("nodecore.lib");

            // Extensions with static registrars need /WHOLEARCHIVE to ensure
            // their constructors run (linker won't pull them in otherwise
            // since nothing directly references the registrar symbols).
            // /OPT:REF still strips unused functions after loading.
            static const std::set<std::string> REGISTRAR_LIBS = {
                "ts_events.lib", "ts_fs.lib", "ts_path.lib",
                "ts_os.lib", "ts_crypto.lib",
            };

            // Extension libraries - only link what the program imports
            for (const auto& lib : requiredLibs) {
                if (REGISTRAR_LIBS.count(lib)) {
                    linkOpts.wholeArchiveLibs.push_back(lib);
                } else {
                    linkOpts.libraries.push_back(lib);
                }
            }

            linkOpts.libraries.push_back("tommath.lib");

            // Runtime depends on spdlog/fmt when SPDLOG_COMPILED_LIB is enabled.
            if (options.debugRuntime) {
                linkOpts.libraries.push_back("spdlogd.lib");
                linkOpts.libraries.push_back("fmtd.lib");
            } else {
                linkOpts.libraries.push_back("spdlog.lib");
                linkOpts.libraries.push_back("fmt.lib");
            }

            // vcpkg dependencies
            linkOpts.libraries.push_back("libuv.lib");        // libuv

            linkOpts.libraries.push_back("icuuc.lib");        // ICU Unicode
            linkOpts.libraries.push_back("icuin.lib");        // ICU i18n
            if (options.bundleIcu) {
                linkOpts.libraries.push_back("icudt.lib");        // Full ICU data (~29MB, self-contained)
            } else {
                linkOpts.libraries.push_back("icudt_stub.lib");   // Stub (~0KB, loads data from external file)
            }
            linkOpts.libraries.push_back("libsodium.lib");    // libsodium
            linkOpts.libraries.push_back("llhttp.lib");       // llhttp
            linkOpts.libraries.push_back("libssl.lib");       // OpenSSL SSL
            linkOpts.libraries.push_back("libcrypto.lib");    // OpenSSL Crypto
            linkOpts.libraries.push_back("cares.lib");        // c-ares DNS
            linkOpts.libraries.push_back("nghttp2.lib");     // nghttp2 (HTTP/2)
            linkOpts.libraries.push_back("zlib.lib");         // zlib
            linkOpts.libraries.push_back("brotlicommon.lib"); // Brotli common
            linkOpts.libraries.push_back("brotlidec.lib");    // Brotli decoder
            linkOpts.libraries.push_back("brotlienc.lib");    // Brotli encoder

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
