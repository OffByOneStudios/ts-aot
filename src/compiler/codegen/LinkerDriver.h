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
    };

    static bool link(const Options& options);
};

} // namespace ts
