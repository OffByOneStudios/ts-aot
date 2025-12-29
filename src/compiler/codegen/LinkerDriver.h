#pragma once

#include <string>
#include <vector>

namespace ts {

class LinkerDriver {
public:
    struct Options {
        std::string outputPath;
        std::vector<std::string> objectFiles;
        std::vector<std::string> libraryPaths;
        std::vector<std::string> libraries;
        bool debug = false;
        bool debugRuntime = false;  // Use debug CRT (libcmtd vs libcmt)
    };

    static bool link(const Options& options);
};

} // namespace ts
