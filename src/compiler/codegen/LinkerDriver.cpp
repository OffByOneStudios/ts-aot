#include "LinkerDriver.h"
#include "lld/Common/Driver.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>

LLD_HAS_DRIVER(coff)

namespace ts {

bool LinkerDriver::link(const Options& options) {
    std::vector<const char*> args;
    args.push_back("ts-aot-linker");
    
    std::string outArg = "/out:" + options.outputPath;
    args.push_back(outArg.c_str());
    
    args.push_back("/subsystem:console");
    
    // Enable dead code elimination and identical COMDAT folding
    args.push_back("/opt:ref");
    args.push_back("/opt:icf");
    
    if (options.debug) {
        args.push_back("/debug");
    }

    // Link correct CRT based on debug/release mode
    // This must match the runtime library's CRT to avoid LNK2038 errors
    if (options.debugRuntime) {
        args.push_back("/defaultlib:libcmtd");
        args.push_back("/nodefaultlib:libcmt");
    } else {
        args.push_back("/defaultlib:libcmt");
        args.push_back("/nodefaultlib:libcmtd");
    }

    // Add library paths
    std::vector<std::string> libPathArgs;
    for (const auto& path : options.libraryPaths) {
        libPathArgs.push_back("/libpath:" + path);
    }
    for (const auto& arg : libPathArgs) {
        args.push_back(arg.c_str());
    }
    
    // Add object files
    for (const auto& obj : options.objectFiles) {
        args.push_back(obj.c_str());
    }

    // Add libraries
    for (const auto& lib : options.libraries) {
        args.push_back(lib.c_str());
    }

    // Standard Windows libraries that we almost always need
    args.push_back("kernel32.lib");
    args.push_back("user32.lib");
    args.push_back("ws2_32.lib");
    args.push_back("advapi32.lib");
    args.push_back("shell32.lib");
    args.push_back("ole32.lib");
    args.push_back("oleaut32.lib");
    args.push_back("dbghelp.lib");
    args.push_back("iphlpapi.lib");
    args.push_back("userenv.lib");

    // lld::coff::link returns true on success
    return lld::coff::link(args, llvm::outs(), llvm::errs(), false, false);
}

} // namespace ts
