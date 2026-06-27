#include "Driver.h"
#include <chrono>
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
#include "hir/passes/SpecializationPass.h"
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

// Concatenate the self-hosted-builtins prelude (src/runtime/prelude/*.ts) into
// one source string, sorted by filename so install order is deterministic. The
// prelude is parsed separately and its statements are spliced ahead of user code
// so they run first (installing spec-correct builtins) without shifting user
// source line numbers. Returns "" if no prelude dir is found (graceful: the
// runtime natives keep their pre-prelude behavior).
static std::string loadPreludeSource() {
    namespace fs = std::filesystem;
    // OPT-IN (default off). The prepend-per-compilation mechanism works
    // (validated: self-hosted filter nets +6 on the filter suite) but is too
    // invasive to enable by default: it recompiles the prelude for every program
    // (compile-time cost + fast-sweep timeout noise) and pollutes golden-IR
    // output. Shipping default-on needs the prelude PRECOMPILED once into the
    // runtime. Until then, enable explicitly with TS_PRELUDE=1.
    const char* on = std::getenv("TS_PRELUDE");
    if (!on || !on[0] || on[0] == '0') return "";
    std::vector<fs::path> candidates;
    if (const char* env = std::getenv("TS_PRELUDE_DIR")) candidates.push_back(env);
    auto exeDir = getExecutablePath().parent_path();
    if (!exeDir.empty()) {
        candidates.push_back(exeDir / "prelude");
        // Dev build-tree fallback: build/src/compiler/<cfg>/ -> repo/src/runtime/prelude
        candidates.push_back(exeDir / ".." / ".." / ".." / ".." / "src" / "runtime" / "prelude");
    }
    for (const auto& dir : candidates) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() == ".ts") files.push_back(e.path());
        }
        if (files.empty()) continue;
        std::sort(files.begin(), files.end());
        std::string out;
        for (const auto& f : files) {
            std::ifstream in(f);
            if (!in.is_open()) continue;
            out.append((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
            out.push_back('\n');
        }
        return out;
    }
    return "";
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
    // --timing accumulators (milliseconds per phase). Filled as the pipeline
    // runs; printed as a consolidated table at the end when options.timing is set.
    using Clock = std::chrono::steady_clock;
    auto MS = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    auto tRun0 = Clock::now();
    double ms_parse = 0, ms_anaCtor = 0, ms_analyze = 0, ms_mono = 0,
           ms_astHir = 0, ms_passes = 0, ms_hirLlvm = 0, ms_emit = 0, ms_link = 0;

    // Load extension contracts + register lowerings ONCE per process. These are
    // process-global singletons; in --batch mode many Driver::run() calls share
    // one process, so this fixed init must not repeat (loadDefaultExtensions
    // appends, so re-running would duplicate contracts). Guarded by a static.
    static bool s_frontendInitDone = false;
    if (!s_frontendInitDone) {
        ext::ExtensionRegistry::instance().loadDefaultExtensions();
        ::hir::LoweringRegistry::instance().registerFromExtensions();
        s_frontendInitDone = true;
    }
    auto tFrontendInit = Clock::now();  // extension/lowering registry built (once/process)

    // Consolidated --timing report. Reads the ms_* accumulators by reference at
    // call time, so it can be defined here and invoked at any success exit.
    auto printTiming = [&]() {
        if (!options.timing) return;
        double llvm_init = MS(options.tMainStart, options.tAfterLlvmInit);
        double argparse  = MS(options.tAfterLlvmInit, tRun0);      // arg parse + Driver ctor
        double frontend  = MS(tRun0, tFrontendInit);              // ext load + lowering registry
        double fixed     = llvm_init + argparse + frontend;
        double pipeline  = ms_parse + ms_anaCtor + ms_analyze + ms_mono + ms_astHir
                         + ms_passes + ms_hirLlvm + ms_emit + ms_link;
        double total     = MS(options.tMainStart, Clock::now());
        auto row = [](const char* name, double ms) {
            fprintf(stderr, "  %-22s %8.1f ms\n", name, ms);
        };
        fprintf(stderr, "\n=== ts-aot --timing (process-load before main excluded; ~35ms via --help) ===\n");
        fprintf(stderr, "[fixed per-invocation init]\n");
        row("llvm target init", llvm_init);
        row("argparse+driver ctor", argparse);
        row("frontend init", frontend);
        row("  fixed subtotal", fixed);
        fprintf(stderr, "[per-file pipeline]\n");
        row("parse", ms_parse);
        row("analyzer ctor (stdlib)", ms_anaCtor);
        row("analyze", ms_analyze);
        row("monomorphize", ms_mono);
        row("ASTToHIR", ms_astHir);
        row("HIR passes", ms_passes);
        row("HIRToLLVM", ms_hirLlvm);
        row("emit-object (LLVM be)", ms_emit);
        row("link", ms_link);
        row("  pipeline subtotal", pipeline);
        fprintf(stderr, "[total since main()]   %8.1f ms\n", total);
        fprintf(stderr, "============================================================\n");
    };

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

            auto tParse0 = Clock::now();
            parser::Parser nativeParser;
            program = nativeParser.parse(source, tsFile);
            ms_parse = MS(tParse0, Clock::now());
            if (nativeParser.getErrorCount() > 0) {
                SPDLOG_ERROR("Compilation failed with {} parse error(s).", nativeParser.getErrorCount());
                return 1;
            }
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

        // Prepend the self-hosted-builtins prelude. Parsed separately (native
        // parser) and spliced to the FRONT of the program body so its installs
        // run before user code, without shifting user source line numbers.
        if (program) {
            std::string preludeSrc = loadPreludeSource();
            if (!preludeSrc.empty()) {
                parser::Parser preludeParser;
                auto preludeProg = preludeParser.parse(preludeSrc, "<prelude>");
                if (preludeParser.getErrorCount() == 0 && preludeProg &&
                    !preludeProg->body.empty()) {
                    program->body.insert(
                        program->body.begin(),
                        std::make_move_iterator(preludeProg->body.begin()),
                        std::make_move_iterator(preludeProg->body.end()));
                } else if (preludeParser.getErrorCount() > 0) {
                    SPDLOG_WARN("Prelude parse failed ({} errors); skipping self-hosted builtins.",
                                preludeParser.getErrorCount());
                }
            }
        }

        if (options.debugAst) {
            ast::printAst(program.get());
        }

        if (options.verbose) {
            SPDLOG_INFO("Analyzing...");
        }
        auto tAnaCtor0 = Clock::now();
        ts::Analyzer analyzer;
        ms_anaCtor = MS(tAnaCtor0, Clock::now());  // Analyzer ctor = stdlib schema build
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

        auto tAnalyze0 = Clock::now();
        analyzer.analyze(program.get(), tsFile);
        ms_analyze = MS(tAnalyze0, Clock::now());

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
        auto t0 = std::chrono::steady_clock::now();
        monomorphizer.monomorphize(program.get(), analyzer);
        auto t1 = std::chrono::steady_clock::now();
        ms_mono = MS(t0, t1);
        SPDLOG_WARN("[TIMING] monomorphize: {}ms, specs={}", std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count(), monomorphizer.getSpecializations().size());

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
        auto t2 = std::chrono::steady_clock::now();
        hir::ASTToHIR astToHir;
        // Reserve a high shape-ID sub-range (top 256 of MAX_SHAPES=4096) for the
        // precompiled prelude so its shapes don't collide with the user object's.
        if (options.preludeObject) astToHir.setShapeIdBase(3840);
        auto hirModule = astToHir.lower(program.get(), monomorphizer.getSpecializations(), moduleName);
        auto t3 = std::chrono::steady_clock::now();
        ms_astHir = MS(t2, t3);
        SPDLOG_WARN("[TIMING] ASTToHIR: {}ms, functions={}", std::chrono::duration_cast<std::chrono::milliseconds>(t3-t2).count(), hirModule ? hirModule->functions.size() : 0);

        // Dump pre-pass HIR (raw output of ASTToHIR, before any optimization passes).
        // Used by Strategy B refactor work to compare ASTToHIR emission against
        // post-pass HIR for diff analysis.
        if (options.dumpHirPre) {
            std::cout << "; HIR (pre-passes, raw ASTToHIR output)\n";
            hir::HIRPrinter printer(std::cout);
            printer.print(*hirModule);
            std::cout << "; --- end pre-passes HIR ---\n";
        }

        // Run HIR optimization passes
        if (options.verbose) {
            SPDLOG_INFO("Running HIR passes...");
        }

        hir::PassManager passManager;
        passManager.addPass(std::make_unique<hir::TypePropagationPass>());
        // Strategy B Phase 2: SpecializationPass rewrites generic opcodes
        // (Add/Sub/Mul/Div/Mod/Neg, CmpEq..CmpGe, GetProp/SetProp) emitted
        // by ASTToHIR (Phase 3+) into type-specific forms based on operand
        // types. Currently a no-op until Phase 3 starts emitting generic ops.
        passManager.addPass(std::make_unique<hir::SpecializationPass>());
        passManager.addPass(std::make_unique<hir::IntegerOptimizationPass>());
        passManager.addPass(std::make_unique<hir::ConstantFoldingPass>());
        passManager.addPass(std::make_unique<hir::DeadCodeEliminationPass>());
        passManager.addPass(std::make_unique<hir::InliningPass>());
        passManager.addPass(std::make_unique<hir::EscapeAnalysisPass>());
        passManager.addPass(std::make_unique<hir::MethodResolutionPass>());
        passManager.addPass(std::make_unique<hir::BuiltinResolutionPass>());

        auto t4 = std::chrono::steady_clock::now();
        auto passResult = passManager.run(*hirModule);
        auto t5 = std::chrono::steady_clock::now();
        ms_passes = MS(t4, t5);
        SPDLOG_WARN("[TIMING] HIR passes: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(t5-t4).count());
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
        hirToLlvm.setEmitDebugInfo(options.debug || options.coverage);
        hirToLlvm.setEmitCoverage(options.coverage);
        hirToLlvm.setPreludeObject(options.preludeObject);

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

        auto t6 = std::chrono::steady_clock::now();
        hirOwnedModule = hirToLlvm.lower(hirModule.get(), moduleName);
        auto t7 = std::chrono::steady_clock::now();
        ms_hirLlvm = MS(t6, t7);
        SPDLOG_WARN("[TIMING] HIRToLLVM: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(t7-t6).count());
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
        codeGen.setEmitCoverage(options.coverage);
        auto tEmit0 = Clock::now();
        if (!codeGen.emitObjectFile(objFile, options.optLevel)) {
            return 1;
        }
        ms_emit = MS(tEmit0, Clock::now());  // LLVM backend: opt pipeline + ISel + obj emit

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

            // Link the precompiled self-hosted-builtins prelude object if present
            // (built from src/runtime/prelude/*.ts with --prelude-object). It
            // defines a strong ts_prelude_init the runtime calls before user code;
            // absent → the runtime's weak no-op default is used. Search next to the
            // compiler exe, then the dev build tree.
            {
                std::vector<std::filesystem::path> preludeObjs = {
                    compilerPath / "ts_prelude.obj",
                    compilerPath / ".." / ".." / ".." / ".." / "build" / "ts_prelude.obj",
                };
                if (const char* p = std::getenv("TS_PRELUDE_OBJ")) preludeObjs.insert(preludeObjs.begin(), p);
                for (const auto& po : preludeObjs) {
                    std::error_code ec;
                    if (std::filesystem::exists(po, ec)) {
                        linkOpts.objectFiles.push_back(po.string());
                        break;
                    }
                }
            }

            linkOpts.libraryPaths.push_back(compilerPath.string());
            linkOpts.libraryPaths.push_back((compilerPath / "lib").string());

            // Shared-runtime import library (tsruntime_shared) lives in its own
            // build subdirectory (src/sharedrt). Add it so the linker can find
            // the import lib in --shared-runtime mode.
            if (options.sharedRuntime) {
                linkOpts.libraryPaths.push_back((compilerPath / ".." / ".." / "sharedrt" / "Release").string());
                linkOpts.libraryPaths.push_back((compilerPath / ".." / ".." / "sharedrt" / "Debug").string());
#ifndef _WIN32
                linkOpts.libraryPaths.push_back((compilerPath / ".." / "sharedrt").string());
                linkOpts.libraryPaths.push_back((compilerPath / ".." / ".." / "sharedrt").string());
#endif
            }

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
                {"ts_request_",         "ts_fetch",           "fetch"},
                {"ts_response_",        "ts_fetch",           "fetch"},
                {"ts_headers_",         "ts_fetch",           "fetch"},
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
            SPDLOG_WARN("[LINK] usedBuiltinModules: {}", analyzer.getUsedBuiltinModules().size());
            for (const auto& mod : analyzer.getUsedBuiltinModules()) {
                SPDLOG_WARN("[LINK]   builtin: {}", mod);
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

            // Shared-runtime mode: the runtime and its third-party dependencies
            // live inside tsruntime_shared.dll, which exports the ts_* C ABI.
            // The executable links ONLY the import library; the DLL is copied
            // next to the output exe after linking so it is found at run time.
            // (Programs importing Node.js extension modules are not yet covered
            // by the shared DLL and should use the default static link.)
            if (options.sharedRuntime) {
#ifdef _WIN32
            linkOpts.libraries.push_back("tsruntime_shared.lib");
#else
            linkOpts.libraries.push_back("-ltsruntime_shared");
#endif
            } else {
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
            } // end static-runtime library set

            auto tLink0 = Clock::now();
            bool linkOk = ts::LinkerDriver::link(linkOpts);
            ms_link = MS(tLink0, Clock::now());
            if (!linkOk) {
                if (options.verbose) {
                    SPDLOG_ERROR("Linking failed.");
                }
                return 1;
            }

            // Clean up temporary object file
            try {
                std::filesystem::remove(objFile);
            } catch (...) {}

            // Shared-runtime mode: place tsruntime_shared.dll next to the output
            // executable so it is found at run time (Windows searches the exe's
            // own directory first; on other platforms it is the loader's rpath/
            // working dir). Copy only when missing or stale to avoid redundant
            // 3MB copies across many outputs in the same directory.
            if (options.sharedRuntime && options.copyRuntimeDll) {
#ifdef _WIN32
                const char* dllName = "tsruntime_shared.dll";
#elif defined(__APPLE__)
                const char* dllName = "libtsruntime_shared.dylib";
#else
                const char* dllName = "libtsruntime_shared.so";
#endif
                std::filesystem::path outDir = std::filesystem::path(exeOutput).parent_path();
                std::filesystem::path dest = outDir.empty() ? std::filesystem::path(dllName)
                                                            : (outDir / dllName);
                // Search the library paths we assembled for the DLL source.
                std::filesystem::path src;
                for (const auto& lp : linkOpts.libraryPaths) {
                    std::filesystem::path cand = std::filesystem::path(lp) / dllName;
                    std::error_code ec;
                    if (std::filesystem::exists(cand, ec)) { src = cand; break; }
                }
                if (src.empty()) {
                    SPDLOG_WARN("--shared-runtime: could not locate {} to copy next to {}; "
                                "the executable will need it on PATH at run time",
                                dllName, exeOutput);
                } else {
                    std::error_code ec;
                    bool needCopy = true;
                    if (std::filesystem::exists(dest, ec)) {
                        auto a = std::filesystem::file_size(src, ec);
                        auto b = std::filesystem::file_size(dest, ec);
                        if (!ec && a == b) needCopy = false;
                    }
                    if (needCopy) {
                        std::filesystem::copy_file(src, dest,
                            std::filesystem::copy_options::overwrite_existing, ec);
                        if (ec && options.verbose) {
                            SPDLOG_WARN("--shared-runtime: failed to copy {} -> {}: {}",
                                        src.string(), dest.string(), ec.message());
                        }
                    }
                }
            }

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
                printTiming();
                int runResult = std::system(runCmd.c_str());
                return runResult;
            }
        }
        printTiming();
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
