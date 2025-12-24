#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <memory>

namespace ts {

struct DriverOptions {
    std::string inputFile;
    std::string outputFile;
    std::string optLevel = "0";
    bool debugAst = false;
    bool dumpIR = false;
    bool dumpTypes = false;
    bool compileOnly = false;
    bool debug = false;
    bool runAfterLink = false;
    bool smallIcu = false;
    bool verbose = false;
    std::string runtimeBitcode;
    std::vector<std::string> libraryPaths;
};

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
