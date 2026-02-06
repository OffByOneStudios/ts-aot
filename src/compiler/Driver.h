#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <memory>

namespace ts {

struct DriverOptions {
    std::string inputFile;
    std::string outputFile;
    std::string projectFile;   // Path to tsconfig.json (or auto-detect if empty)
    std::string optLevel = "0";
    bool debugAst = false;
    bool dumpIR = false;
    bool dumpHir = false;      // Dump HIR before LLVM lowering
    bool dumpTypes = false;
    bool compileOnly = false;
    bool debug = false;
    bool debugRuntime = false;  // Link against debug version of tsruntime
    bool runAfterLink = false;
    bool smallIcu = false;
    bool verbose = false;
    std::string runtimeBitcode;
    std::vector<std::string> libraryPaths;
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
