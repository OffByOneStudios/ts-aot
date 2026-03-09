#include "LinkerDriver.h"
#include "lld/Common/Driver.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <filesystem>

#ifdef _WIN32
LLD_HAS_DRIVER(coff)
#else
LLD_HAS_DRIVER(elf)
#endif

namespace ts {

bool LinkerDriver::link(const Options& options) {
    std::vector<const char*> args;
    args.push_back("ts-aot-linker");

#ifdef _WIN32
    // ---- Windows: COFF/PE linking via lld-link ----

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

    // Add whole-archive libraries (force all symbols to be included,
    // needed for static registrars that don't have external references)
    std::vector<std::string> wholeArchiveArgs;
    for (const auto& lib : options.wholeArchiveLibs) {
        wholeArchiveArgs.push_back("/wholearchive:" + lib);
    }
    for (const auto& arg : wholeArchiveArgs) {
        args.push_back(arg.c_str());
    }

    // Standard Windows libraries
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

    return lld::coff::link(args, llvm::outs(), llvm::errs(), false, false);

#else
    // ---- Linux: ELF linking via ld.lld ----

    std::string outArg = "-o";
    args.push_back(outArg.c_str());
    args.push_back(options.outputPath.c_str());

    // Static linking
    args.push_back("-static");

    // Dead code elimination
    args.push_back("--gc-sections");

    if (options.debug) {
        // Keep debug info (default for ELF, but be explicit)
    }

    // System library paths - needed for static libc, libpthread, etc.
    // Standard locations for x86_64 Linux
    args.push_back("-L/usr/lib/x86_64-linux-gnu");
    args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/12");
    args.push_back("-L/usr/lib");
    args.push_back("-L/lib/x86_64-linux-gnu");

    // Add user library search paths (canonicalize to resolve ../ components)
    std::vector<std::string> libPathArgs;
    for (const auto& path : options.libraryPaths) {
        std::error_code ec;
        auto canonical = std::filesystem::canonical(path, ec);
        if (!ec) {
            libPathArgs.push_back("-L" + canonical.string());
        } else {
            libPathArgs.push_back("-L" + path);
        }
    }
    for (const auto& arg : libPathArgs) {
        args.push_back(arg.c_str());
    }

    // CRT startup files for static linking
    args.push_back("/usr/lib/x86_64-linux-gnu/crt1.o");
    args.push_back("/usr/lib/x86_64-linux-gnu/crti.o");

    // Add object files
    for (const auto& obj : options.objectFiles) {
        args.push_back(obj.c_str());
    }

    // Helper: resolve -l flag to full archive path
    auto resolveLib = [&](const std::string& lib) -> std::string {
        if (lib.substr(0, 2) == "-l") {
            std::string libName = lib.substr(2);
            for (const auto& lp : libPathArgs) {
                std::string dir = lp.substr(2);  // strip "-L"
                std::string candidate = dir + "/lib" + libName + ".a";
                if (std::filesystem::exists(candidate)) {
                    std::error_code ec;
                    auto canon = std::filesystem::canonical(candidate, ec);
                    return ec ? candidate : canon.string();
                }
            }
        }
        return lib;  // Return as-is if not -l or not found
    };

    // Resolve -l flags to full paths and pass archives directly
    // This avoids LLD search path issues with embedded invocation
    std::vector<std::string> resolvedLibs;
    for (const auto& lib : options.libraries) {
        resolvedLibs.push_back(resolveLib(lib));
    }

    // Whole-archive libraries (force all symbols to be included,
    // needed for static registrars that don't have external references)
    std::vector<std::string> resolvedWholeArchiveLibs;
    for (const auto& lib : options.wholeArchiveLibs) {
        resolvedWholeArchiveLibs.push_back(resolveLib(lib));
    }
    if (!resolvedWholeArchiveLibs.empty()) {
        args.push_back("--whole-archive");
        for (const auto& lib : resolvedWholeArchiveLibs) {
            args.push_back(lib.c_str());
        }
        args.push_back("--no-whole-archive");
    }

    // Pass all archives directly (no -l flags) in a group for circular dep resolution
    args.push_back("--start-group");

    for (const auto& lib : resolvedLibs) {
        args.push_back(lib.c_str());
    }

    // Linux system libraries (use -l for system libs)
    args.push_back("-lstdc++");
    args.push_back("-lpthread");
    args.push_back("-ldl");
    args.push_back("-lm");
    args.push_back("-lrt");
    args.push_back("-lc");
    args.push_back("-lgcc");
    args.push_back("-lgcc_eh");

    args.push_back("--end-group");

    // CRT finalization
    args.push_back("/usr/lib/x86_64-linux-gnu/crtn.o");

    return lld::elf::link(args, llvm::outs(), llvm::errs(), false, false);
#endif
}

} // namespace ts
