#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <chrono>

namespace ts {

struct DriverOptions {
    std::string inputFile;
    std::string outputFile;
    std::string projectFile;   // Path to tsconfig.json (or auto-detect if empty)
    std::string optLevel = "0";
    bool debugAst = false;
    bool dumpIR = false;
    bool dumpHir = false;       // Dump HIR after all optimization passes (final form)
    bool dumpHirPre = false;    // Dump HIR before any optimization passes (raw ASTToHIR output)
    bool dumpTypes = false;
    bool compileOnly = false;
    bool preludeObject = false; // --prelude-object: emit ts_prelude_init (no main/ts_main), for the precompiled self-hosted-builtins object
    bool debug = false;
    bool debugRuntime = false;  // Link against debug version of tsruntime
    bool sharedRuntime = false; // --shared-runtime: link the runtime as a shared DLL (tsruntime_shared) instead of statically
    bool copyRuntimeDll = true; // shared mode: copy tsruntime_shared.dll next to the exe (off via --no-copy-runtime for bulk runners using PATH discovery)
    bool runAfterLink = false;
    bool bundleIcu = false;    // --bundle-icu: embed ICU data (~29MB larger, self-contained)
    bool verbose = false;
    bool useNativeParser = true;   // Use native C++ parser instead of Node.js dump_ast.js
    bool enableGCStatepoints = false;  // --gc-statepoints: enable LLVM GC statepoint infrastructure
    bool fastChecks = false;           // --fast-checks: dev-mode NativeArray bounds/dispose checks ("use fast")
    bool coverage = false;             // --coverage: emit LLVM source-based coverage instrumentation
    bool timing = false;               // --timing: print a per-phase wall-clock breakdown to stderr
    std::string batchManifest;         // --batch <file>: compile many inputs in one process (amortizes startup)
    std::string runtimeBitcode;
    std::vector<std::string> libraryPaths;
    // Process-level timestamps captured in main() so run() can report the fixed
    // pre-pipeline cost (process load is before main; LLVM target init is in main).
    std::chrono::steady_clock::time_point tMainStart{};
    std::chrono::steady_clock::time_point tAfterLlvmInit{};
};

// Helper to detect if the compiler was built in debug mode
inline bool isDebugBuild() {
#ifdef _DEBUG
    return true;
#else
    return false;
#endif
}

class Driver {
public:
    Driver(const DriverOptions& options);
    ~Driver();

    int run();

private:
    bool runNodeParser(const std::string& tsFile, const std::string& jsonFile);
    std::string findNodeExecutable();
    std::string findParserScript();

    DriverOptions options;
};

} // namespace ts
