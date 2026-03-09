#include "Driver.h"
#include <unicode/uvernum.h>
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
#else
#include <unistd.h>
#include <climits>
#endif

namespace ts {

// Get the path to the currently running executable
static std::filesystem::path getExecutablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer);
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        return std::filesystem::path(buffer);
    }
    // Fallback: use argv[0] or empty
    return std::filesystem::path();
#endif
}

// Create a temporary file path
static std::string createTempFile(const std::string& prefix, const std::string& suffix) {
#ifdef _WIN32
    char tempPath[MAX_PATH];
    char tempFile[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tempPath)) {
        if (GetTempFileNameA(tempPath, prefix.c_str(), 0, tempFile)) {
            return tempFile;
        }
    }
    return "";
#else
    std::string tmpl = "/tmp/" + prefix + "XXXXXX" + suffix;
    // mkstemp modifies the template in place
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = mkstemps(buf.data(), (int)suffix.size());
    if (fd >= 0) {
        close(fd);
        return std::string(buf.data());
    }
    // Fallback without suffix
    tmpl = "/tmp/" + prefix + "XXXXXX";
    buf.assign(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    fd = mkstemp(buf.data());
    if (fd >= 0) {
        close(fd);
        return std::string(buf.data());
    }
    return "";
#endif
}

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
            std::string tempFile = createTempFile("tsaot", ".json");
            if (!tempFile.empty()) {
                jsonFile = tempFile;
                isTemporaryJson = true;
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

        // Embed ICU data path so compiled executables can find icudtXXl.dat
        // next to the compiler instead of needing a local copy
        if (!options.bundleIcu) {
            auto compilerExe = getExecutablePath();
            if (!compilerExe.empty()) {
                // Build version-specific ICU data filename
                std::string datName = "icudt" + std::to_string(U_ICU_VERSION_MAJOR_NUM) + "l.dat";
                auto datPath = compilerExe.parent_path() / datName;
                hirToLlvm.setIcuDataPath(datPath.string());
            }
        }

        hirOwnedModule = hirToLlvm.lower(hirModule.get(), moduleName);
        modulePtr = hirOwnedModule.get();

        if (options.dumpIR) {
            modulePtr->print(llvm::outs(), nullptr);
        }

        // Object file extension is platform-specific
#ifdef _WIN32
        std::string objExt = ".obj";
#else
        std::string objExt = ".o";
#endif

        std::string objFile;
        if (options.compileOnly) {
            objFile = options.outputFile.empty() ?
                std::filesystem::path(tsFile).replace_extension(objExt).string() :
                options.outputFile;
        } else {
            objFile = std::filesystem::path(tsFile).replace_extension(objExt).string();
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
            // Default output extension is platform-specific
#ifdef _WIN32
            std::string defaultExeExt = ".exe";
#else
            std::string defaultExeExt = "";
#endif
            std::string exeOutput = options.outputFile.empty() ?
                std::filesystem::path(tsFile).replace_extension(defaultExeExt).string() :
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
            auto compilerExe = getExecutablePath();
            std::filesystem::path compilerPath;
            if (!compilerExe.empty()) {
                compilerPath = compilerExe.parent_path();
            } else {
                compilerPath = std::filesystem::current_path();
            }

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

#ifndef _WIN32
            // On Linux with single-config generators, libs are directly under runtime/
            linkOpts.libraryPaths.push_back((compilerPath / ".." / "runtime").string());
            linkOpts.libraryPaths.push_back((compilerPath / ".." / ".." / "runtime").string());
#endif

            // Extension library paths - only add paths for extensions the program imports.
            // This reduces binary size significantly (e.g., hello world: ~7MB -> ~3.5MB).
#ifdef _WIN32
            // Windows multi-config: compiler is at build/src/compiler/Release/ts-aot.exe
            std::filesystem::path extensionsPath = compilerPath / ".." / ".." / ".." / "extensions" / "node";
#else
            // Linux single-config: compiler is at build/src/compiler/ts-aot
            std::filesystem::path extensionsPath = compilerPath / ".." / ".." / "extensions" / "node";
#endif

            // Map from normalized module name to {libName, dirName}
            struct ExtLibInfo { const char* libName; const char* dirName; };
            static const std::unordered_map<std::string, ExtLibInfo> MODULE_TO_LIB = {
                {"fs",              {"ts_fs",              "fs"}},
                {"path",            {"ts_path",            "path"}},
                {"os",              {"ts_os",              "os"}},
                {"http",            {"ts_http",            "http"}},
                {"https",           {"ts_http",            "http"}},
                {"http2",           {"ts_http2",           "http2"}},
                {"net",             {"ts_net",             "net"}},
                {"dns",             {"ts_dns",             "dns"}},
                {"dgram",           {"ts_dgram",           "dgram"}},
                {"crypto",          {"ts_crypto",          "crypto"}},
                {"zlib",            {"ts_zlib",            "zlib"}},
                {"url",             {"ts_url",             "url"}},
                {"util",            {"ts_util",            "util"}},
                {"events",          {"ts_events",          "events"}},
                {"stream",          {"ts_stream",          "stream"}},
                {"readline",        {"ts_readline",        "readline"}},
                {"child_process",   {"ts_child_process",   "child_process"}},
                {"cluster",         {"ts_cluster",         "cluster"}},
                {"assert",          {"ts_assert",          "assert"}},
                {"async_hooks",     {"ts_async_hooks",     "async_hooks"}},
                {"perf_hooks",      {"ts_perf_hooks",      "perf_hooks"}},
                {"string_decoder",  {"ts_string_decoder",  "string_decoder"}},
                {"tty",             {"ts_tty",             "tty"}},
                {"v8",              {"ts_v8",              "v8"}},
                {"vm",              {"ts_vm",              "vm"}},
                {"inspector",       {"ts_inspector",       "inspector"}},
                {"module",          {"ts_module",          "module"}},
            };

            // Determine which extension libraries to link by scanning the LLVM IR
            // for external symbol references with known extension prefixes.
            struct SymbolPrefix { const char* prefix; const char* libName; const char* dirName; };
            static const SymbolPrefix SYMBOL_PREFIXES[] = {
                {"ts_fs_",              "ts_fs",              "fs"},
                {"ts_path_",            "ts_path",            "path"},
                {"ts_os_",              "ts_os",              "os"},
                {"ts_http2_",           "ts_http2",           "http2"},
                {"ts_http_",            "ts_http",            "http"},
                {"ts_https_",           "ts_http",            "http"},
                {"ts_net_",             "ts_net",             "net"},
                {"ts_dns_",             "ts_dns",             "dns"},
                {"ts_dgram_",           "ts_dgram",           "dgram"},
                {"ts_crypto_",          "ts_crypto",          "crypto"},
                {"ts_zlib_",            "ts_zlib",            "zlib"},
                {"ts_url_",             "ts_url",             "url"},
                {"ts_querystring_",     "ts_url",             "url"},
                {"ts_util_",            "ts_util",            "util"},
                {"ts_event_emitter_",   "ts_events",          "events"},
                {"ts_events_",          "ts_events",          "events"},
                {"ts_stream_",          "ts_stream",          "stream"},
                {"ts_readable_",        "ts_stream",          "stream"},
                {"ts_writable_",        "ts_stream",          "stream"},
                {"ts_duplex_",          "ts_stream",          "stream"},
                {"ts_transform_",       "ts_stream",          "stream"},
                {"ts_readline_",        "ts_readline",        "readline"},
                {"ts_child_process_",   "ts_child_process",   "child_process"},
                {"ts_cluster_",         "ts_cluster",         "cluster"},
                {"ts_assert_",          "ts_assert",          "assert"},
                {"ts_async_hooks_",     "ts_async_hooks",     "async_hooks"},
                {"ts_perf_hooks_",      "ts_perf_hooks",      "perf_hooks"},
                {"ts_performance_",     "ts_perf_hooks",      "perf_hooks"},
                {"ts_string_decoder_",  "ts_string_decoder",  "string_decoder"},
                {"ts_tty_",             "ts_tty",             "tty"},
                {"ts_v8_",              "ts_v8",              "v8"},
                {"ts_vm_",              "ts_vm",              "vm"},
                {"ts_inspector_",       "ts_inspector",       "inspector"},
                {"ts_module_",          "ts_module",          "module"},
                {"ts_fetch",            "ts_fetch",           "fetch"},
                {"ts_socket_",          "ts_net",             "net"},
                {"ts_server_",          "ts_net",             "net"},
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
            for (const auto& mod : analyzer.getUsedBuiltinModules()) {
                std::string normalized = mod;
                if (normalized.starts_with("node:")) normalized = normalized.substr(5);
                auto it = MODULE_TO_LIB.find(normalized);
                if (it != MODULE_TO_LIB.end()) {
                    requiredLibs.insert(it->second.libName);
                    requiredDirs.insert(it->second.dirName);
                }
            }

            // Transitive dependencies
            static const std::unordered_map<std::string, std::vector<std::pair<const char*, const char*>>> EXT_DEPS = {
                {"ts_net",           {{"ts_events", "events"}, {"ts_stream", "stream"}}},
                {"ts_http",          {{"ts_events", "events"}, {"ts_stream", "stream"}, {"ts_net", "net"}, {"ts_fetch", "fetch"}}},
                {"ts_http2",         {{"ts_events", "events"}, {"ts_stream", "stream"}, {"ts_net", "net"}}},
                {"ts_fetch",         {{"ts_url", "url"}}},
                {"ts_fs",            {{"ts_events", "events"}, {"ts_stream", "stream"}}},
                {"ts_child_process", {{"ts_events", "events"}, {"ts_stream", "stream"}}},
                {"ts_cluster",       {{"ts_events", "events"}, {"ts_child_process", "child_process"}, {"ts_net", "net"}}},
                {"ts_dgram",         {{"ts_events", "events"}}},
                {"ts_readline",      {{"ts_events", "events"}, {"ts_stream", "stream"}}},
                {"ts_stream",        {{"ts_events", "events"}}},
                {"ts_tty",           {{"ts_events", "events"}, {"ts_stream", "stream"}, {"ts_net", "net"}}},
            };
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
#ifdef _WIN32
                if (options.debugRuntime) {
                    linkOpts.libraryPaths.push_back((extensionsPath / dir / "Debug").string());
                    linkOpts.libraryPaths.push_back((extensionsPath / dir / "Release").string());
                } else {
                    linkOpts.libraryPaths.push_back((extensionsPath / dir / "Release").string());
                    linkOpts.libraryPaths.push_back((extensionsPath / dir / "Debug").string());
                }
#else
                linkOpts.libraryPaths.push_back((extensionsPath / dir).string());
#endif
            }

            // vcpkg paths
#ifdef _WIN32
            // Windows multi-config: 4 levels up from build/src/compiler/Release/
            std::filesystem::path rootPath = compilerPath / ".." / ".." / ".." / "..";
#else
            // Linux single-config: 3 levels up from build/src/compiler/
            std::filesystem::path rootPath = compilerPath / ".." / ".." / "..";
#endif
            std::filesystem::path vcpkgPath = rootPath / "vcpkg_installed";
#ifdef _WIN32
            if (options.debugRuntime) {
                linkOpts.libraryPaths.push_back((vcpkgPath / "x64-windows-static-md" / "debug" / "lib").string());
                linkOpts.libraryPaths.push_back((vcpkgPath / "x64-windows-static" / "debug" / "lib").string());
            }
            linkOpts.libraryPaths.push_back((vcpkgPath / "x64-windows-static-md" / "lib").string());
            linkOpts.libraryPaths.push_back((vcpkgPath / "x64-windows-static" / "lib").string());
            linkOpts.libraryPaths.push_back((vcpkgPath / "x64-windows" / "lib").string());
#else
            if (options.debugRuntime) {
                linkOpts.libraryPaths.push_back((vcpkgPath / "x64-linux" / "debug" / "lib").string());
            }
            linkOpts.libraryPaths.push_back((vcpkgPath / "x64-linux" / "lib").string());
#endif

            for (const auto& path : options.libraryPaths) {
                linkOpts.libraryPaths.push_back(path);
            }

            // Core runtime libraries
#ifdef _WIN32
            linkOpts.libraries.push_back("tsruntime.lib");
            linkOpts.libraries.push_back("nodecore.lib");
#else
            linkOpts.libraries.push_back("-ltsruntime");
            linkOpts.libraries.push_back("-lnodecore");
#endif

            // Extensions with static registrars need whole-archive to ensure
            // their constructors run (linker won't pull them in otherwise).
            static const std::set<std::string> REGISTRAR_LIBS = {
                "ts_events", "ts_fs", "ts_path",
                "ts_os", "ts_crypto",
            };

            // Extension libraries - only link what the program imports
            for (const auto& lib : requiredLibs) {
#ifdef _WIN32
                std::string winLib = lib + ".lib";
                if (REGISTRAR_LIBS.count(lib)) {
                    linkOpts.wholeArchiveLibs.push_back(winLib);
                } else {
                    linkOpts.libraries.push_back(winLib);
                }
#else
                if (REGISTRAR_LIBS.count(lib)) {
                    linkOpts.wholeArchiveLibs.push_back("-l" + lib);
                } else {
                    linkOpts.libraries.push_back("-l" + lib);
                }
#endif
            }

            // Third-party libraries
#ifdef _WIN32
            linkOpts.libraries.push_back("tommath.lib");
#else
            linkOpts.libraries.push_back("-ltommath");
#endif

            if (options.debugRuntime) {
#ifdef _WIN32
                linkOpts.libraries.push_back("spdlogd.lib");
                linkOpts.libraries.push_back("fmtd.lib");
#else
                linkOpts.libraries.push_back("-lspdlogd");
                linkOpts.libraries.push_back("-lfmtd");
#endif
            } else {
#ifdef _WIN32
                linkOpts.libraries.push_back("spdlog.lib");
                linkOpts.libraries.push_back("fmt.lib");
#else
                linkOpts.libraries.push_back("-lspdlog");
                linkOpts.libraries.push_back("-lfmt");
#endif
            }

            // vcpkg dependencies
#ifdef _WIN32
            linkOpts.libraries.push_back("libuv.lib");
            linkOpts.libraries.push_back("icuuc.lib");
            linkOpts.libraries.push_back("icuin.lib");
            if (options.bundleIcu) {
                linkOpts.libraries.push_back("icudt.lib");
            } else {
                linkOpts.libraries.push_back("icudt_stub.lib");
            }
            linkOpts.libraries.push_back("libsodium.lib");
            linkOpts.libraries.push_back("llhttp.lib");
            linkOpts.libraries.push_back("libssl.lib");
            linkOpts.libraries.push_back("libcrypto.lib");
            linkOpts.libraries.push_back("cares.lib");
            linkOpts.libraries.push_back("nghttp2.lib");
            linkOpts.libraries.push_back("zlib.lib");
            linkOpts.libraries.push_back("brotlicommon.lib");
            linkOpts.libraries.push_back("brotlidec.lib");
            linkOpts.libraries.push_back("brotlienc.lib");

            // Windows system libraries
            linkOpts.libraries.push_back("ws2_32.lib");
            linkOpts.libraries.push_back("user32.lib");
            linkOpts.libraries.push_back("advapi32.lib");
            linkOpts.libraries.push_back("iphlpapi.lib");
            linkOpts.libraries.push_back("shell32.lib");
            linkOpts.libraries.push_back("crypt32.lib");
            linkOpts.libraries.push_back("bcrypt.lib");
#else
            linkOpts.libraries.push_back("-luv");
            linkOpts.libraries.push_back("-licuuc");
            linkOpts.libraries.push_back("-licui18n");
            if (options.bundleIcu) {
                linkOpts.libraries.push_back("-licudata");
            } else {
                linkOpts.libraries.push_back("-licudt_stub");
            }
            linkOpts.libraries.push_back("-lsodium");
            linkOpts.libraries.push_back("-lllhttp");
            linkOpts.libraries.push_back("-lssl");
            linkOpts.libraries.push_back("-lcrypto");
            linkOpts.libraries.push_back("-lcares");
            linkOpts.libraries.push_back("-lnghttp2");
            linkOpts.libraries.push_back("-lz");
            linkOpts.libraries.push_back("-lbrotlicommon");
            linkOpts.libraries.push_back("-lbrotlidec");
            linkOpts.libraries.push_back("-lbrotlienc");
#endif

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
#ifdef _WIN32
                std::string runCmd = (std::filesystem::path(".") / exeOutput).string();
#else
                // On Linux, use ./ prefix for local executables
                std::string runCmd = "./" + exeOutput;
#endif
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
    return "node";
}

std::string Driver::findParserScript() {
    auto compilerExe = getExecutablePath();
    std::filesystem::path compilerPath;
    if (!compilerExe.empty()) {
        compilerPath = compilerExe.parent_path();
    } else {
        compilerPath = std::filesystem::current_path();
    }

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
