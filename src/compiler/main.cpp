#include <fmt/core.h>
#include <cxxopts.hpp>
#include "Driver.h"
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <llvm/Support/TargetSelect.h>
#include "codegen/TsAotGC.h"
#include <chrono>
#include <fstream>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

int main(int argc, char** argv) {
    // Capture the earliest point we can for --timing (process/binary load happens
    // before main, so this excludes that; --help baseline approximates it).
    auto _tMainStart = std::chrono::steady_clock::now();
#ifdef _MSC_VER
    // Disable the "Abort, Retry, Ignore" dialog and redirect to stderr
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    (void)argc; // suppress unused warnings on some compilers

    // Force-link the ts-aot-gc GC strategy registration
    ts::linkTsAotGC();

    // Initialize LLVM targets
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();
    auto _tAfterLlvmInit = std::chrono::steady_clock::now();

    try {
        cxxopts::Options options("ts-aot", "TypeScript AOT Compiler");
        options.add_options()
            ("o,output", "Output file", cxxopts::value<std::string>())
            ("c,compile", "Compile only (emit .obj)", cxxopts::value<bool>()->default_value("false"))
            ("prelude-object", "Emit a precompiled self-hosted-builtins object (ts_prelude_init, no main)", cxxopts::value<bool>()->default_value("false"))
            ("r,run", "Run the executable after linking", cxxopts::value<bool>()->default_value("false"))
            ("emit-obj", "Emit object file (legacy)", cxxopts::value<std::string>())
            ("emit-exe", "Emit executable (legacy)", cxxopts::value<std::string>())
            ("lib-path", "Additional library search path", cxxopts::value<std::vector<std::string>>())
            ("g,debug", "Generate debug information", cxxopts::value<bool>()->default_value("false"))
            ("debug-runtime", "Link against debug version of runtime (auto-detected if compiler is debug build)", cxxopts::value<bool>()->default_value("false"))
            ("shared-runtime", "Link the runtime as a shared library (tsruntime_shared.dll) instead of statically; produces a tiny executable that needs the DLL at runtime", cxxopts::value<bool>()->default_value("false"))
            ("no-copy-runtime", "In --shared-runtime mode, do NOT copy tsruntime_shared.dll next to the output executable (the exe must find it on PATH / next to itself at run time). Useful for bulk test runners that share one DLL.", cxxopts::value<bool>()->default_value("false"))
            ("d,debug-ast", "Print AST", cxxopts::value<bool>()->default_value("false"))
            ("dump-ir", "Dump LLVM IR", cxxopts::value<bool>()->default_value("false"))
            ("dump-hir", "Dump HIR after all optimization passes (final form)", cxxopts::value<bool>()->default_value("false"))
            ("dump-hir-pre", "Dump HIR before any optimization passes (raw ASTToHIR output)", cxxopts::value<bool>()->default_value("false"))
            ("dump-types", "Dump inferred types", cxxopts::value<bool>()->default_value("false"))
            ("use-hir", "[Deprecated, no-op] HIR pipeline is always used", cxxopts::value<bool>()->default_value("true"))
            ("v,verbose", "Verbose output", cxxopts::value<bool>()->default_value("false"))
            ("log-level", "Set log level (trace, debug, info, warn, error, off)", cxxopts::value<std::string>()->default_value("warning"))
            ("O,opt", "Optimization level (0, 1, 2, 3, s, z)", cxxopts::value<std::string>()->default_value("0"))
            ("p,project", "Path to tsconfig.json (or auto-detect if not specified)", cxxopts::value<std::string>())
            ("runtime-bc", "Path to runtime bitcode for LTO", cxxopts::value<std::string>())
            ("bundle-icu", "Bundle ICU data into executable (~29MB larger, for self-contained deployment)", cxxopts::value<bool>()->default_value("false"))
            ("native-parser", "Use native C++ parser (default: true)", cxxopts::value<bool>()->default_value("true"))
            ("legacy-parser", "Force legacy Node.js parser (dump_ast.js)", cxxopts::value<bool>()->default_value("false"))
            ("gc-statepoints", "Enable LLVM GC statepoint precise-root infrastructure (default: true)", cxxopts::value<bool>()->default_value("true"))
            ("no-gc-statepoints", "Disable LLVM GC statepoints (use conservative stack scan)", cxxopts::value<bool>()->default_value("false"))
            ("coverage", "Emit LLVM source-based coverage instrumentation", cxxopts::value<bool>()->default_value("false"))
            ("timing", "Print a per-phase wall-clock breakdown of the compile to stderr", cxxopts::value<bool>()->default_value("false"))
            ("batch", "Compile many files in ONE process (amortizes process load + LLVM init + extension load). Arg is a manifest: one job per line, 'INPUT<TAB>OUTPUT'. Prints 'INPUT<TAB>OUTPUT<TAB>RC' per job to stdout.", cxxopts::value<std::string>())
            ("h,help", "Print usage")
            ("input", "Input file", cxxopts::value<std::string>());

        options.parse_positional({"input"});
        auto result = options.parse(argc, argv);

        // Initialize logging on STDERR. Diagnostics/warnings belong on stderr;
        // stdout is reserved for requested output (IR/HIR dumps) and the --batch
        // machine-readable result stream, which must not be polluted by logs.
        auto console = spdlog::stderr_color_mt("console");
        spdlog::set_default_logger(console);
        
        // Set a cleaner, compiler-like pattern: [level] message
        // %g: basename of source file, %#: line number
        spdlog::set_pattern("[%^%l%$] %g:%# %v");
        
        std::string logLevel = result["log-level"].as<std::string>();
        if (result["verbose"].as<bool>()) {
            logLevel = "debug";
        }

        if (logLevel == "trace") spdlog::set_level(spdlog::level::trace);
        else if (logLevel == "debug") spdlog::set_level(spdlog::level::debug);
        else if (logLevel == "info") spdlog::set_level(spdlog::level::info);
        else if (logLevel == "warn" || logLevel == "warning") spdlog::set_level(spdlog::level::warn);
        else if (logLevel == "error") spdlog::set_level(spdlog::level::err);
        else if (logLevel == "off") spdlog::set_level(spdlog::level::off);
        else {
            // Default to warn if unknown level
            spdlog::set_level(spdlog::level::warn);
        }

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (!result.count("input") && !result.count("batch")) {
            std::cerr << "Error: No input file specified." << std::endl;
            return 1;
        }

        ts::DriverOptions driverOpts;
        if (result.count("input")) {
            driverOpts.inputFile = result["input"].as<std::string>();
        }
        if (result.count("batch")) {
            driverOpts.batchManifest = result["batch"].as<std::string>();
        }
        driverOpts.debug = result["debug"].as<bool>();
        
        // Auto-detect debug runtime if compiler was built in debug mode, or if explicitly requested
        driverOpts.debugRuntime = result["debug-runtime"].as<bool>() || ts::isDebugBuild();
        
        if (driverOpts.debugRuntime && !result["debug-runtime"].as<bool>()) {
            SPDLOG_INFO("Compiler built in Debug mode - using debug runtime");
        }
        
        if (result.count("output")) {
            driverOpts.outputFile = result["output"].as<std::string>();
        } else if (result.count("emit-exe")) {
            driverOpts.outputFile = result["emit-exe"].as<std::string>();
        } else if (result.count("emit-obj")) {
            driverOpts.outputFile = result["emit-obj"].as<std::string>();
            driverOpts.compileOnly = true;
        }

        if (result["compile"].as<bool>()) {
            driverOpts.compileOnly = true;
        }

        if (result["prelude-object"].as<bool>()) {
            driverOpts.preludeObject = true;
            driverOpts.compileOnly = true;  // object-only emission
        }

        if (result["run"].as<bool>()) {
            driverOpts.runAfterLink = true;
        }

        // Handle optimization level
        // If user specified an opt level, use it. Otherwise:
        // - Debug runtime: force O0 (MSVC debug CRT compatibility)
        // - Release runtime: default to O2 for performance
        if (result.count("opt")) {
            driverOpts.optLevel = result["opt"].as<std::string>();
            // Warn if using optimizations with debug runtime
            if (driverOpts.debugRuntime && driverOpts.optLevel != "0") {
                SPDLOG_WARN("Using optimization level -{} with debug runtime may cause issues", driverOpts.optLevel);
            }
        } else {
            // Auto-select optimization level based on runtime mode
            driverOpts.optLevel = driverOpts.debugRuntime ? "0" : "2";
        }
        
        driverOpts.debugAst = result["debug-ast"].as<bool>();
        driverOpts.dumpIR = result["dump-ir"].as<bool>();
        driverOpts.dumpHir = result["dump-hir"].as<bool>();
        driverOpts.dumpHirPre = result["dump-hir-pre"].as<bool>();
        driverOpts.dumpTypes = result["dump-types"].as<bool>();
        driverOpts.bundleIcu = result["bundle-icu"].as<bool>();
        driverOpts.sharedRuntime = result["shared-runtime"].as<bool>();
        driverOpts.copyRuntimeDll = !result["no-copy-runtime"].as<bool>();
        // Precise GC statepoints are ON by default; --no-gc-statepoints opts out.
        driverOpts.enableGCStatepoints = result["gc-statepoints"].as<bool>()
                                         && !result["no-gc-statepoints"].as<bool>();
        driverOpts.coverage = result["coverage"].as<bool>();
        driverOpts.timing = result["timing"].as<bool>();
        driverOpts.tMainStart = _tMainStart;
        driverOpts.tAfterLlvmInit = _tAfterLlvmInit;
        driverOpts.verbose = result["verbose"].as<bool>();

        // Parser selection: --native-parser enables, --legacy-parser disables
        if (result["native-parser"].as<bool>()) {
            driverOpts.useNativeParser = true;
        }
        if (result["legacy-parser"].as<bool>()) {
            driverOpts.useNativeParser = false;
        }
        
        if (result.count("runtime-bc")) {
            driverOpts.runtimeBitcode = result["runtime-bc"].as<std::string>();
        }

        if (result.count("project")) {
            driverOpts.projectFile = result["project"].as<std::string>();
        }

        if (result.count("lib-path")) {
            driverOpts.libraryPaths = result["lib-path"].as<std::vector<std::string>>();
        }

        // --batch: compile every job in the manifest within THIS process, so the
        // process load (~35ms for the 77MB LLVM-heavy binary), LLVM target init,
        // and extension/lowering registry load are paid ONCE instead of per file.
        // Each job gets a fresh Driver (cheap) -> fresh LLVMContext/Module/Analyzer
        // per file (correct isolation); the once-per-process init is guarded inside
        // Driver::run(). A recoverable error in one job does not abort the batch
        // (a hard crash still does -- callers should chunk the manifest).
        if (!driverOpts.batchManifest.empty()) {
            std::ifstream mf(driverOpts.batchManifest);
            if (!mf.is_open()) {
                std::cerr << "Error: cannot open batch manifest: " << driverOpts.batchManifest << std::endl;
                return 1;
            }
            auto tBatch0 = std::chrono::steady_clock::now();
            std::string line;
            int count = 0, failures = 0;
            while (std::getline(mf, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF manifests
                if (line.empty() || line[0] == '#') continue;
                auto tab = line.find('\t');
                std::string inFile = (tab == std::string::npos) ? line : line.substr(0, tab);
                std::string outFile = (tab == std::string::npos) ? std::string() : line.substr(tab + 1);
                if (inFile.empty()) continue;

                ts::DriverOptions jobOpts = driverOpts;
                jobOpts.batchManifest.clear();
                jobOpts.inputFile = inFile;
                jobOpts.outputFile = outFile;

                int rc = 1;
                try {
                    ts::Driver driver(jobOpts);
                    rc = driver.run();
                } catch (const std::exception& e) {
                    std::cerr << "batch: " << inFile << ": " << e.what() << std::endl;
                    rc = 1;
                }
                // Per-job result line for the caller to map outcomes.
                std::cout << inFile << "\t" << outFile << "\t" << rc << "\n";
                std::cout.flush();
                ++count;
                if (rc != 0) ++failures;
            }
            if (driverOpts.timing) {
                double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - tBatch0).count();
                std::cerr << "[batch] " << count << " files, " << failures << " failures, "
                          << ms << " ms total ("
                          << (count ? ms / count : 0.0) << " ms/file)\n";
            }
            return failures > 0 ? 1 : 0;
        }

        ts::Driver driver(driverOpts);
        return driver.run();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
