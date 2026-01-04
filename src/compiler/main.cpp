#include <fmt/core.h>
#include <cxxopts.hpp>
#include "Driver.h"
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <llvm/Support/TargetSelect.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

int main(int argc, char** argv) {
#ifdef _MSC_VER
    // Disable the "Abort, Retry, Ignore" dialog and redirect to stderr
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    // Initialize LLVM targets
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    try {
        cxxopts::Options options("ts-aot", "TypeScript AOT Compiler");
        options.add_options()
            ("o,output", "Output file", cxxopts::value<std::string>())
            ("c,compile", "Compile only (emit .obj)", cxxopts::value<bool>()->default_value("false"))
            ("r,run", "Run the executable after linking", cxxopts::value<bool>()->default_value("false"))
            ("emit-obj", "Emit object file (legacy)", cxxopts::value<std::string>())
            ("emit-exe", "Emit executable (legacy)", cxxopts::value<std::string>())
            ("lib-path", "Additional library search path", cxxopts::value<std::vector<std::string>>())
            ("g,debug", "Generate debug information", cxxopts::value<bool>()->default_value("false"))
            ("debug-runtime", "Link against debug version of runtime (auto-detected if compiler is debug build)", cxxopts::value<bool>()->default_value("false"))
            ("d,debug-ast", "Print AST", cxxopts::value<bool>()->default_value("false"))
            ("dump-ir", "Dump LLVM IR", cxxopts::value<bool>()->default_value("false"))
            ("dump-types", "Dump inferred types", cxxopts::value<bool>()->default_value("false"))
            ("v,verbose", "Verbose output", cxxopts::value<bool>()->default_value("false"))
            ("log-level", "Set log level (trace, debug, info, warn, error, off)", cxxopts::value<std::string>()->default_value("warning"))
            ("O,opt", "Optimization level (0, 1, 2, 3, s, z)", cxxopts::value<std::string>()->default_value("0"))
            ("p,project", "Path to tsconfig.json (or auto-detect if not specified)", cxxopts::value<std::string>())
            ("runtime-bc", "Path to runtime bitcode for LTO", cxxopts::value<std::string>())
            ("small-icu", "Use a smaller ICU data set (English only)", cxxopts::value<bool>()->default_value("false"))
            ("h,help", "Print usage")
            ("input", "Input file", cxxopts::value<std::string>());

        options.parse_positional({"input"});
        auto result = options.parse(argc, argv);

        // Initialize logging
        auto console = spdlog::stdout_color_mt("console");
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

        if (!result.count("input")) {
            std::cerr << "Error: No input file specified." << std::endl;
            return 1;
        }

        ts::DriverOptions driverOpts;
        driverOpts.inputFile = result["input"].as<std::string>();
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
        driverOpts.dumpTypes = result["dump-types"].as<bool>();
        driverOpts.smallIcu = result["small-icu"].as<bool>();
        driverOpts.verbose = result["verbose"].as<bool>();
        
        if (result.count("runtime-bc")) {
            driverOpts.runtimeBitcode = result["runtime-bc"].as<std::string>();
        }

        if (result.count("project")) {
            driverOpts.projectFile = result["project"].as<std::string>();
        }

        if (result.count("lib-path")) {
            driverOpts.libraryPaths = result["lib-path"].as<std::vector<std::string>>();
        }

        ts::Driver driver(driverOpts);
        return driver.run();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
