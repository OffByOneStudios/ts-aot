#include "ModuleResolver.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace ts {

using json = nlohmann::json;

// Extension priority: TypeScript first, then JavaScript
const std::vector<std::string> ModuleResolver::EXTENSIONS = {
    ".ts", ".tsx", ".d.ts", ".js", ".jsx", ".json"
};

const std::vector<std::string> ModuleResolver::INDEX_FILES = {
    "index.ts", "index.tsx", "index.d.ts", "index.js", "index.jsx"
};

// Builtin modules that don't need resolution
static const std::vector<std::string> BUILTIN_MODULES = {
    "fs", "path", "crypto", "os", "http", "https", "events", "net", 
    "stream", "util", "url", "buffer", "process", "child_process",
    "cluster", "dgram", "dns", "domain", "readline", "repl", "tls",
    "tty", "v8", "vm", "zlib", "assert", "async_hooks", "console",
    "constants", "module", "perf_hooks", "querystring", "string_decoder",
    "timers", "trace_events", "worker_threads"
};

ModuleResolver::ModuleResolver() {}

void ModuleResolver::setCurrentDirectory(const fs::path& dir) {
    currentDir = fs::absolute(dir);
}

void ModuleResolver::setProjectRoot(const fs::path& root) {
    projectRoot = fs::absolute(root);
}

bool ModuleResolver::isBuiltinModule(const std::string& specifier) {
    std::string name = specifier;
    if (name.starts_with("node:")) {
        name = name.substr(5);
    }
    return std::find(BUILTIN_MODULES.begin(), BUILTIN_MODULES.end(), name) != BUILTIN_MODULES.end();
}

const std::vector<std::string>& ModuleResolver::getBuiltinModules() {
    return BUILTIN_MODULES;
}

ResolvedModule ModuleResolver::resolve(const std::string& specifier, const fs::path& fromFile) {
    SPDLOG_DEBUG("Resolving module '{}' from '{}'", specifier, fromFile.string());
    
    fs::path fromDir = fromFile.parent_path();
    
    // 1. Check for builtin modules
    if (isBuiltinModule(specifier)) {
        std::string name = specifier;
        if (name.starts_with("node:")) {
            name = name.substr(5);
        }
        return ResolvedModule{
            .path = "builtin:" + name,
            .type = ModuleType::Builtin,
            .packageName = name,
            .isExternal = false
        };
    }
    
    // 2. Check tsconfig.json paths first (if configured)
    if (tsConfig.isValid()) {
        auto resolved = resolveTsConfigPaths(specifier);
        if (resolved.isValid()) {
            SPDLOG_DEBUG("Resolved via tsconfig paths: {}", resolved.path);
            return resolved;
        }
    }
    
    // 3. Relative imports (start with ./ or ../)
    if (specifier.starts_with("./") || specifier.starts_with("../") ||
        specifier.starts_with(".\\") || specifier.starts_with("..\\")) {
        return resolveRelative(specifier, fromDir);
    }
    
    // 4. Absolute paths
    if (fs::path(specifier).is_absolute()) {
        auto resolved = tryExtensions(specifier);
        if (resolved) {
            return ResolvedModule{
                .path = resolved->string(),
                .type = getModuleType(*resolved),
                .packageName = "",
                .isExternal = false
            };
        }
    }
    
    // 5. node_modules resolution
    return resolveNodeModules(specifier, fromDir);
}

ResolvedModule ModuleResolver::resolveRelative(const std::string& specifier, const fs::path& fromDir) {
    fs::path resolved = fromDir / specifier;
    
    // Try with extensions
    auto withExt = tryExtensions(resolved);
    if (withExt) {
        return ResolvedModule{
            .path = withExt->string(),
            .type = getModuleType(*withExt),
            .packageName = "",
            .isExternal = false
        };
    }
    
    // Try as directory
    auto asDir = tryDirectory(resolved);
    if (asDir) {
        return ResolvedModule{
            .path = asDir->string(),
            .type = getModuleType(*asDir),
            .packageName = "",
            .isExternal = false
        };
    }
    
    SPDLOG_WARN("Could not resolve relative import: {}", specifier);
    return ResolvedModule{};
}

ResolvedModule ModuleResolver::resolveNodeModules(const std::string& specifier, const fs::path& fromDir) {
    // Parse package name - handle scoped packages (@org/package)
    std::string packageName;
    std::string subpath;
    
    if (specifier.starts_with("@")) {
        // Scoped package: @org/package or @org/package/path
        auto firstSlash = specifier.find('/');
        if (firstSlash == std::string::npos) {
            SPDLOG_WARN("Invalid scoped package: {}", specifier);
            return ResolvedModule{};
        }
        auto secondSlash = specifier.find('/', firstSlash + 1);
        if (secondSlash == std::string::npos) {
            packageName = specifier;
        } else {
            packageName = specifier.substr(0, secondSlash);
            subpath = specifier.substr(secondSlash);
        }
    } else {
        // Regular package: package or package/path
        auto slash = specifier.find('/');
        if (slash == std::string::npos) {
            packageName = specifier;
        } else {
            packageName = specifier.substr(0, slash);
            subpath = specifier.substr(slash);
        }
    }
    
    // Walk up directories looking for node_modules
    fs::path searchDir = fromDir;
    while (true) {
        fs::path nodeModules = searchDir / "node_modules" / packageName;
        
        if (fs::exists(nodeModules) && fs::is_directory(nodeModules)) {
            SPDLOG_DEBUG("Found package at: {}", nodeModules.string());
            
            // If there's a subpath, resolve it directly
            if (!subpath.empty()) {
                fs::path subpathResolved = nodeModules / subpath.substr(1); // Remove leading /
                auto withExt = tryExtensions(subpathResolved);
                if (withExt) {
                    return ResolvedModule{
                        .path = withExt->string(),
                        .type = getModuleType(*withExt),
                        .packageName = packageName,
                        .isExternal = true
                    };
                }
                auto asDir = tryDirectory(subpathResolved);
                if (asDir) {
                    return ResolvedModule{
                        .path = asDir->string(),
                        .type = getModuleType(*asDir),
                        .packageName = packageName,
                        .isExternal = true
                    };
                }
            }
            
            // Otherwise, use package.json to find entry point
            fs::path pkgJsonPath = nodeModules / "package.json";
            if (fs::exists(pkgJsonPath)) {
                auto pkg = parsePackageJson(pkgJsonPath);
                if (pkg) {
                    auto entryPoint = getPackageEntryPoint(nodeModules, *pkg);
                    if (entryPoint) {
                        return ResolvedModule{
                            .path = entryPoint->string(),
                            .type = getModuleType(*entryPoint),
                            .packageName = packageName,
                            .isExternal = true
                        };
                    }
                }
            }
            
            // Fallback: try index files
            auto indexFile = tryDirectory(nodeModules);
            if (indexFile) {
                return ResolvedModule{
                    .path = indexFile->string(),
                    .type = getModuleType(*indexFile),
                    .packageName = packageName,
                    .isExternal = true
                };
            }
        }
        
        // Move up one directory
        fs::path parent = searchDir.parent_path();
        if (parent == searchDir) {
            // Reached filesystem root
            break;
        }
        searchDir = parent;
    }
    
    SPDLOG_WARN("Could not find package '{}' in node_modules", packageName);
    return ResolvedModule{};
}

ResolvedModule ModuleResolver::resolveTsConfigPaths(const std::string& specifier) {
    for (const auto& [pattern, targets] : tsConfig.paths) {
        // Handle wildcard patterns like "@lib/*"
        if (pattern.ends_with("*")) {
            std::string prefix = pattern.substr(0, pattern.length() - 1);
            if (specifier.starts_with(prefix)) {
                std::string suffix = specifier.substr(prefix.length());
                
                for (const auto& target : targets) {
                    std::string targetPath = target;
                    if (targetPath.ends_with("*")) {
                        targetPath = targetPath.substr(0, targetPath.length() - 1) + suffix;
                    }
                    
                    fs::path fullPath = projectRoot;
                    if (!tsConfig.baseUrl.empty()) {
                        fullPath = projectRoot / tsConfig.baseUrl;
                    }
                    fullPath = fullPath / targetPath;
                    
                    auto resolved = tryExtensions(fullPath);
                    if (resolved) {
                        return ResolvedModule{
                            .path = resolved->string(),
                            .type = getModuleType(*resolved),
                            .packageName = "",
                            .isExternal = false
                        };
                    }
                    
                    auto asDir = tryDirectory(fullPath);
                    if (asDir) {
                        return ResolvedModule{
                            .path = asDir->string(),
                            .type = getModuleType(*asDir),
                            .packageName = "",
                            .isExternal = false
                        };
                    }
                }
            }
        } else {
            // Exact match
            if (specifier == pattern) {
                for (const auto& target : targets) {
                    fs::path fullPath = projectRoot;
                    if (!tsConfig.baseUrl.empty()) {
                        fullPath = projectRoot / tsConfig.baseUrl;
                    }
                    fullPath = fullPath / target;
                    
                    auto resolved = tryExtensions(fullPath);
                    if (resolved) {
                        return ResolvedModule{
                            .path = resolved->string(),
                            .type = getModuleType(*resolved),
                            .packageName = "",
                            .isExternal = false
                        };
                    }
                }
            }
        }
    }
    
    // Try baseUrl resolution
    if (!tsConfig.baseUrl.empty()) {
        fs::path fullPath = projectRoot / tsConfig.baseUrl / specifier;
        auto resolved = tryExtensions(fullPath);
        if (resolved) {
            return ResolvedModule{
                .path = resolved->string(),
                .type = getModuleType(*resolved),
                .packageName = "",
                .isExternal = false
            };
        }
    }
    
    return ResolvedModule{};
}

std::optional<fs::path> ModuleResolver::tryExtensions(const fs::path& basePath) {
    // If the path already has a valid extension, check it directly
    std::string ext = basePath.extension().string();
    if (!ext.empty()) {
        if (fs::exists(basePath) && fs::is_regular_file(basePath)) {
            return fs::absolute(basePath);
        }
    }
    
    // Try adding extensions
    for (const auto& extension : EXTENSIONS) {
        fs::path withExt = basePath;
        withExt += extension;
        if (fs::exists(withExt) && fs::is_regular_file(withExt)) {
            return fs::absolute(withExt);
        }
    }
    
    return std::nullopt;
}

std::optional<fs::path> ModuleResolver::tryDirectory(const fs::path& dirPath) {
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        return std::nullopt;
    }
    
    // Check for package.json first
    fs::path pkgJson = dirPath / "package.json";
    if (fs::exists(pkgJson)) {
        auto pkg = parsePackageJson(pkgJson);
        if (pkg) {
            auto entryPoint = getPackageEntryPoint(dirPath, *pkg);
            if (entryPoint) {
                return entryPoint;
            }
        }
    }
    
    // Try index files
    for (const auto& indexFile : INDEX_FILES) {
        fs::path indexPath = dirPath / indexFile;
        if (fs::exists(indexPath) && fs::is_regular_file(indexPath)) {
            return fs::absolute(indexPath);
        }
    }
    
    return std::nullopt;
}

std::optional<PackageJson> ModuleResolver::parsePackageJson(const fs::path& packageJsonPath) {
    std::string pathStr = packageJsonPath.string();
    
    // Check cache
    auto it = packageJsonCache.find(pathStr);
    if (it != packageJsonCache.end()) {
        return it->second;
    }
    
    try {
        std::ifstream file(packageJsonPath);
        if (!file.is_open()) {
            return std::nullopt;
        }
        
        json j;
        file >> j;
        
        PackageJson pkg;
        
        if (j.contains("name") && j["name"].is_string()) {
            pkg.name = j["name"].get<std::string>();
        }
        if (j.contains("version") && j["version"].is_string()) {
            pkg.version = j["version"].get<std::string>();
        }
        if (j.contains("main") && j["main"].is_string()) {
            pkg.main = j["main"].get<std::string>();
        }
        if (j.contains("module") && j["module"].is_string()) {
            pkg.module = j["module"].get<std::string>();
        }
        if (j.contains("types") && j["types"].is_string()) {
            pkg.types = j["types"].get<std::string>();
        }
        if (j.contains("typings") && j["typings"].is_string()) {
            pkg.types = j["typings"].get<std::string>();
        }
        if (j.contains("type") && j["type"].is_string()) {
            pkg.hasTypeModule = (j["type"].get<std::string>() == "module");
        }
        
        // Parse exports field (simplified - only string values for now)
        if (j.contains("exports")) {
            auto& exports = j["exports"];
            if (exports.is_string()) {
                pkg.exports["."] = exports.get<std::string>();
            } else if (exports.is_object()) {
                for (auto& [key, val] : exports.items()) {
                    if (val.is_string()) {
                        pkg.exports[key] = val.get<std::string>();
                    } else if (val.is_object()) {
                        // Handle conditional exports like { "import": "./esm.js", "require": "./cjs.js" }
                        // Prefer TypeScript types, then import, then require, then default
                        if (val.contains("types") && val["types"].is_string()) {
                            pkg.exports[key] = val["types"].get<std::string>();
                        } else if (val.contains("import") && val["import"].is_string()) {
                            pkg.exports[key] = val["import"].get<std::string>();
                        } else if (val.contains("require") && val["require"].is_string()) {
                            pkg.exports[key] = val["require"].get<std::string>();
                        } else if (val.contains("default") && val["default"].is_string()) {
                            pkg.exports[key] = val["default"].get<std::string>();
                        }
                    }
                }
            }
        }
        
        packageJsonCache[pathStr] = pkg;
        return pkg;
        
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to parse {}: {}", pathStr, e.what());
        return std::nullopt;
    }
}

std::optional<fs::path> ModuleResolver::getPackageEntryPoint(const fs::path& packageDir, const PackageJson& pkg) {
    // Priority order:
    // 1. exports["."] (modern packages)
    // 2. types/typings (for TypeScript)
    // 3. module (ESM)
    // 4. main (CommonJS)
    
    if (pkg.exports.count(".")) {
        fs::path entryPath = packageDir / pkg.exports.at(".");
        auto resolved = tryExtensions(entryPath);
        if (resolved) return resolved;
    }
    
    // For TypeScript, prefer types field
    if (!pkg.types.empty()) {
        fs::path typesPath = packageDir / pkg.types;
        if (fs::exists(typesPath)) {
            // But we need the actual implementation, not just types
            // Try to find corresponding .ts or .js
            fs::path tsPath = typesPath;
            tsPath.replace_extension(".ts");
            if (fs::exists(tsPath)) return fs::absolute(tsPath);
            
            tsPath.replace_extension(".js");
            if (fs::exists(tsPath)) return fs::absolute(tsPath);
        }
    }
    
    if (!pkg.module.empty()) {
        fs::path modulePath = packageDir / pkg.module;
        auto resolved = tryExtensions(modulePath);
        if (resolved) return resolved;
    }
    
    if (!pkg.main.empty()) {
        fs::path mainPath = packageDir / pkg.main;
        auto resolved = tryExtensions(mainPath);
        if (resolved) return resolved;
    }
    
    return std::nullopt;
}

bool ModuleResolver::loadTsConfig(const fs::path& tsconfigPath) {
    try {
        std::ifstream file(tsconfigPath);
        if (!file.is_open()) {
            return false;
        }
        
        json j;
        file >> j;
        
        projectRoot = tsconfigPath.parent_path();
        
        if (j.contains("compilerOptions")) {
            auto& opts = j["compilerOptions"];
            
            if (opts.contains("baseUrl") && opts["baseUrl"].is_string()) {
                tsConfig.baseUrl = opts["baseUrl"].get<std::string>();
            }
            
            if (opts.contains("paths") && opts["paths"].is_object()) {
                for (auto& [key, val] : opts["paths"].items()) {
                    if (val.is_array()) {
                        std::vector<std::string> paths;
                        for (auto& p : val) {
                            if (p.is_string()) {
                                paths.push_back(p.get<std::string>());
                            }
                        }
                        tsConfig.paths[key] = paths;
                    }
                }
            }
            
            if (opts.contains("rootDir") && opts["rootDir"].is_string()) {
                tsConfig.rootDir = opts["rootDir"].get<std::string>();
            }
            
            if (opts.contains("outDir") && opts["outDir"].is_string()) {
                tsConfig.outDir = opts["outDir"].get<std::string>();
            }
        }
        
        if (j.contains("include") && j["include"].is_array()) {
            for (auto& p : j["include"]) {
                if (p.is_string()) {
                    tsConfig.include.push_back(p.get<std::string>());
                }
            }
        }
        
        if (j.contains("exclude") && j["exclude"].is_array()) {
            for (auto& p : j["exclude"]) {
                if (p.is_string()) {
                    tsConfig.exclude.push_back(p.get<std::string>());
                }
            }
        }
        
        SPDLOG_INFO("Loaded tsconfig.json from {}", tsconfigPath.string());
        if (!tsConfig.baseUrl.empty()) {
            SPDLOG_DEBUG("  baseUrl: {}", tsConfig.baseUrl);
        }
        if (!tsConfig.paths.empty()) {
            SPDLOG_DEBUG("  paths: {} aliases", tsConfig.paths.size());
        }
        
        return true;
        
    } catch (const std::exception& e) {
        SPDLOG_WARN("Failed to parse {}: {}", tsconfigPath.string(), e.what());
        return false;
    }
}

ModuleType ModuleResolver::getModuleType(const fs::path& filePath) {
    std::string ext = filePath.extension().string();
    
    if (ext == ".ts" || ext == ".tsx") {
        return ModuleType::TypeScript;
    }
    if (ext == ".d.ts" || filePath.string().ends_with(".d.ts")) {
        return ModuleType::Declaration;
    }
    if (ext == ".js" || ext == ".jsx" || ext == ".mjs" || ext == ".cjs") {
        // TODO: Check for JSDoc annotations to distinguish typed/untyped
        return ModuleType::UntypedJavaScript;
    }
    if (ext == ".json") {
        return ModuleType::JSON;
    }
    
    return ModuleType::UntypedJavaScript;
}

} // namespace ts
