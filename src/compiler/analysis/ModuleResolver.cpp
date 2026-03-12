#include "ModuleResolver.h"
#include "../extensions/ExtensionLoader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace ts {

using json = nlohmann::json;

// Extension priority: TypeScript first, then JavaScript, then ESM/CJS variants
const std::vector<std::string> ModuleResolver::EXTENSIONS = {
    ".ts", ".tsx", ".d.ts", ".mts", ".d.mts", ".cts", ".d.cts",
    ".js", ".jsx", ".mjs", ".cjs", ".json"
};

const std::vector<std::string> ModuleResolver::INDEX_FILES = {
    "index.ts", "index.tsx", "index.d.ts",
    "index.js", "index.jsx", "index.mjs", "index.cjs"
};

// Fallback builtin modules that don't have extension contracts yet
// These are kept for modules that aren't defined through the extension system.
static const std::vector<std::string> FALLBACK_BUILTIN_MODULES = {
    "buffer", "process", "https",
    "domain", "repl", "constants",
    "timers", "timers/promises", "trace_events", "worker_threads"
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

    // Check the extension registry first (covers all extension-defined modules)
    auto& registry = ext::ExtensionRegistry::instance();
    if (registry.isRegisteredModule(name)) {
        return true;
    }

    // Fallback for modules without extension contracts
    return std::find(FALLBACK_BUILTIN_MODULES.begin(), FALLBACK_BUILTIN_MODULES.end(), name)
        != FALLBACK_BUILTIN_MODULES.end();
}

const std::vector<std::string>& ModuleResolver::getBuiltinModules() {
    // This returns the fallback list for compatibility; callers that need
    // the full list should query the extension registry directly.
    return FALLBACK_BUILTIN_MODULES;
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
        std::string ext = withExt->extension().string();
        bool esm = (ext == ".mjs") || (ext != ".cjs" && isNearestPackageESM(*withExt));
        return ResolvedModule{
            .path = withExt->string(),
            .type = getModuleType(*withExt),
            .packageName = "",
            .isExternal = false,
            .isESM = esm
        };
    }

    // Try as directory (may set lastResolvedTypesPath_ via getPackageEntryPoint)
    lastResolvedTypesPath_.clear();
    auto asDir = tryDirectory(resolved);
    if (asDir) {
        std::string ext = asDir->extension().string();
        bool esm = (ext == ".mjs") || (ext != ".cjs" && isNearestPackageESM(*asDir));
        return ResolvedModule{
            .path = asDir->string(),
            .typesPath = lastResolvedTypesPath_,
            .type = getModuleType(*asDir),
            .packageName = "",
            .isExternal = false,
            .isESM = esm
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
            
            // If there's a subpath, check exports field first, then try filesystem
            if (!subpath.empty()) {
                // Check package.json exports field for subpath mapping
                fs::path pkgJsonPath = nodeModules / "package.json";
                if (fs::exists(pkgJsonPath)) {
                    auto pkg = parsePackageJson(pkgJsonPath);
                    if (pkg && (!pkg->exports.empty() || !pkg->wildcardExports.empty())) {
                        std::string exportKey = "." + subpath; // e.g., "./utils"

                        // Try exact match first
                        auto it = pkg->exports.find(exportKey);
                        if (it != pkg->exports.end()) {
                            fs::path resolved = nodeModules / it->second;
                            if (fs::exists(resolved)) {
                                return ResolvedModule{
                                    .path = resolved.string(),
                                    .type = getModuleType(resolved),
                                    .packageName = packageName,
                                    .isExternal = true
                                };
                            }
                            auto withExt = tryExtensions(resolved);
                            if (withExt) {
                                return ResolvedModule{
                                    .path = withExt->string(),
                                    .type = getModuleType(*withExt),
                                    .packageName = packageName,
                                    .isExternal = true
                                };
                            }
                        }

                        // Try wildcard patterns: "./*" -> "./dist/*.js"
                        for (const auto& [pattern, target] : pkg->wildcardExports) {
                            size_t starPos = pattern.find('*');
                            if (starPos == std::string::npos) continue;
                            std::string prefix = pattern.substr(0, starPos);
                            std::string suffix = pattern.substr(starPos + 1);

                            bool prefixMatch = exportKey.size() >= prefix.size() &&
                                               exportKey.substr(0, prefix.size()) == prefix;
                            bool suffixMatch = suffix.empty() ||
                                               (exportKey.size() >= suffix.size() &&
                                                exportKey.substr(exportKey.size() - suffix.size()) == suffix);

                            if (prefixMatch && suffixMatch &&
                                exportKey.size() >= prefix.size() + suffix.size()) {
                                std::string match = exportKey.substr(
                                    prefix.size(),
                                    exportKey.size() - prefix.size() - suffix.size());
                                std::string resolvedTarget = target;
                                size_t tStar = resolvedTarget.find('*');
                                if (tStar != std::string::npos) {
                                    resolvedTarget = resolvedTarget.substr(0, tStar) + match +
                                                     resolvedTarget.substr(tStar + 1);
                                }
                                fs::path resolved = nodeModules / resolvedTarget;
                                if (fs::exists(resolved)) {
                                    return ResolvedModule{
                                        .path = resolved.string(),
                                        .type = getModuleType(resolved),
                                        .packageName = packageName,
                                        .isExternal = true
                                    };
                                }
                                auto withExt2 = tryExtensions(resolved);
                                if (withExt2) {
                                    return ResolvedModule{
                                        .path = withExt2->string(),
                                        .type = getModuleType(*withExt2),
                                        .packageName = packageName,
                                        .isExternal = true
                                    };
                                }
                            }
                        }
                    }
                }

                // Fallback: resolve subpath directly on filesystem
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
                        std::string entryExt = entryPoint->extension().string();
                        bool pkgIsESM = (pkg->hasTypeModule && entryExt != ".cjs") || entryExt == ".mjs";
                        auto result = ResolvedModule{
                            .path = entryPoint->string(),
                            .typesPath = lastResolvedTypesPath_,
                            .type = getModuleType(*entryPoint),
                            .packageName = packageName,
                            .isExternal = true,
                            .isESM = pkgIsESM
                        };

                        // If no types found in the package itself, check @types/<package>
                        if (result.typesPath.empty() && result.type != ModuleType::Declaration) {
                            fs::path atTypesDir = searchDir / "node_modules" / "@types" / packageName;
                            if (fs::exists(atTypesDir)) {
                                fs::path atTypesPkgJson = atTypesDir / "package.json";
                                if (fs::exists(atTypesPkgJson)) {
                                    auto atTypesPkg = parsePackageJson(atTypesPkgJson);
                                    if (atTypesPkg) {
                                        auto typesEntry = getPackageEntryPoint(atTypesDir, *atTypesPkg);
                                        if (typesEntry && typesEntry->string().ends_with(".d.ts")) {
                                            result.typesPath = typesEntry->string();
                                            SPDLOG_INFO("Found @types/{}: {}", packageName, result.typesPath);
                                        }
                                    }
                                } else {
                                    // No package.json — try index.d.ts directly
                                    fs::path indexDts = atTypesDir / "index.d.ts";
                                    if (fs::exists(indexDts)) {
                                        result.typesPath = fs::absolute(indexDts).string();
                                        SPDLOG_INFO("Found @types/{}: {}", packageName, result.typesPath);
                                    }
                                }
                            }
                        }

                        return result;
                    }
                }
            }

            // Fallback: try index files (package had no main/module/types/exports)
            auto indexFile = tryDirectory(nodeModules);
            if (indexFile) {
                auto result = ResolvedModule{
                    .path = indexFile->string(),
                    .type = getModuleType(*indexFile),
                    .packageName = packageName,
                    .isExternal = true
                };

                // Check @types/<package> for types (same logic as the package.json path)
                if (result.typesPath.empty() && result.type != ModuleType::Declaration) {
                    fs::path atTypesDir = searchDir / "node_modules" / "@types" / packageName;
                    if (fs::exists(atTypesDir)) {
                        fs::path atTypesPkgJson = atTypesDir / "package.json";
                        if (fs::exists(atTypesPkgJson)) {
                            auto atTypesPkg = parsePackageJson(atTypesPkgJson);
                            if (atTypesPkg) {
                                auto typesEntry = getPackageEntryPoint(atTypesDir, *atTypesPkg);
                                if (typesEntry && typesEntry->string().ends_with(".d.ts")) {
                                    result.typesPath = typesEntry->string();
                                    SPDLOG_INFO("Found @types/{}: {}", packageName, result.typesPath);
                                }
                            }
                        } else {
                            // No package.json — try index.d.ts directly
                            fs::path indexDts = atTypesDir / "index.d.ts";
                            if (fs::exists(indexDts)) {
                                result.typesPath = fs::absolute(indexDts).string();
                                SPDLOG_INFO("Found @types/{}: {}", packageName, result.typesPath);
                            }
                        }
                    }
                }

                return result;
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

std::optional<std::string> ModuleResolver::resolveExportCondition(const nlohmann::json& val, bool preferESM) {
    if (val.is_string()) {
        return val.get<std::string>();
    }
    if (val.is_null()) {
        // Explicitly blocked export (null target)
        return std::nullopt;
    }
    if (val.is_array()) {
        // Array of fallback conditions — try each in order, return first that resolves
        // e.g., [{"import": "./index.mjs"}, {"require": "./index.cjs"}, "./fallback.js"]
        for (const auto& item : val) {
            auto result = resolveExportCondition(item, preferESM);
            if (result) return result;
        }
        return std::nullopt;
    }
    if (!val.is_object()) {
        return std::nullopt;
    }
    // "types" must be LAST because it resolves to .d.ts (declaration-only, no
    // executable code). Packages like dotenv have "types" pointing to .d.ts
    // and "require"/"default" pointing to .js — we must prefer the .js.
    // This handles arbitrarily nested conditionals like:
    // { "import": { "types": "./dist/index.d.mts", "default": "./dist/index.mjs" } }
    //
    // When preferESM is true (package has "type": "module"), prefer "import" over
    // "require" since the ESM entry points are the primary distribution format and
    // CJS versions may use patterns like `class Foo extends pkg.Bar` that our
    // parser handles less well than ESM's direct `import { Bar } from ...`.
    const char* esmOrder[] = {"node", "import", "default", "require", "types"};
    const char* cjsOrder[] = {"node", "require", "default", "import", "types"};
    const char** order = preferESM ? esmOrder : cjsOrder;
    for (int i = 0; i < 5; i++) {
        if (val.contains(order[i])) {
            auto result = resolveExportCondition(val[order[i]], preferESM);
            if (result) return result;
        }
    }
    return std::nullopt;
}

bool ModuleResolver::isNearestPackageESM(const fs::path& filePath) {
    // Walk up from the file's directory looking for the nearest package.json
    fs::path dir = filePath.parent_path();
    for (int depth = 0; depth < 20; ++depth) {
        fs::path pkgJson = dir / "package.json";
        if (fs::exists(pkgJson)) {
            auto pkg = parsePackageJson(pkgJson);
            if (pkg) {
                return pkg->hasTypeModule;
            }
        }
        auto parent = dir.parent_path();
        if (parent == dir) break;  // filesystem root
        dir = parent;
    }
    return false;
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
        
        // Parse exports field with recursive conditional resolution
        // For "type": "module" packages, prefer ESM entry points over CJS
        bool preferESM = pkg.hasTypeModule;
        if (j.contains("exports")) {
            auto& exports = j["exports"];
            if (exports.is_string()) {
                pkg.exports["."] = exports.get<std::string>();
            } else if (exports.is_array()) {
                // "exports": ["./index.mjs", "./index.cjs"] — array fallback for root
                auto resolved = resolveExportCondition(exports, preferESM);
                if (resolved) {
                    pkg.exports["."] = *resolved;
                }
            } else if (exports.is_object()) {
                // Check if this is a subpath map (keys start with ".") or a root condition map
                // Root condition: { "import": "./index.mjs", "require": "./index.cjs" }
                // Subpath map: { ".": "./index.js", "./utils": "./src/utils.js" }
                bool hasSubpathKeys = false;
                for (auto& [key, val] : exports.items()) {
                    if (!key.empty() && key[0] == '.') {
                        hasSubpathKeys = true;
                        break;
                    }
                }

                if (!hasSubpathKeys) {
                    // Root condition map — resolve the whole object as "."
                    auto resolved = resolveExportCondition(exports, preferESM);
                    if (resolved) {
                        pkg.exports["."] = *resolved;
                    }
                } else {
                    // Subpath map — resolve each key's value
                    for (auto& [key, val] : exports.items()) {
                        auto resolved = resolveExportCondition(val, preferESM);
                        if (resolved) {
                            if (key.find('*') != std::string::npos) {
                                pkg.wildcardExports.push_back({key, *resolved});
                            } else {
                                pkg.exports[key] = *resolved;
                            }
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
    // 2. types/typings → return .d.ts directly (types-only module)
    // 3. module (ESM) or main (CommonJS) → implementation file
    // Note: lastResolvedTypesPath is set as a side-effect for paired loading

    lastResolvedTypesPath_.clear();

    if (pkg.exports.count(".")) {
        fs::path entryPath = packageDir / pkg.exports.at(".");
        auto resolved = tryExtensions(entryPath);
        if (resolved) {
            // If there's also a types field, remember it for paired loading
            if (!pkg.types.empty()) {
                fs::path typesPath = packageDir / pkg.types;
                if (fs::exists(typesPath)) {
                    lastResolvedTypesPath_ = fs::absolute(typesPath).string();
                }
            }
            return resolved;
        }
    }

    // For TypeScript, prefer types field
    if (!pkg.types.empty()) {
        fs::path typesPath = packageDir / pkg.types;
        if (fs::exists(typesPath)) {
            // Try to find corresponding implementation (.js)
            // The .d.ts stem may differ from .js stem (e.g., index.d.ts → index.js)
            fs::path jsPath = typesPath;
            // Remove the .d.ts extension properly (not just .ts)
            std::string pathStr = typesPath.string();
            if (pathStr.ends_with(".d.ts")) {
                pathStr = pathStr.substr(0, pathStr.size() - 5);  // Remove ".d.ts"
            }

            // Try .js implementation
            fs::path implPath = pathStr + ".js";
            if (fs::exists(implPath)) {
                lastResolvedTypesPath_ = fs::absolute(typesPath).string();
                return fs::absolute(implPath);
            }

            // No implementation found — return .d.ts directly (types-only)
            return fs::absolute(typesPath);
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
    // Check .d.ts BEFORE .ts — std::filesystem::extension() returns ".ts" for ".d.ts"
    if (filePath.string().ends_with(".d.ts")) {
        return ModuleType::Declaration;
    }
    std::string ext = filePath.extension().string();

    if (ext == ".ts" || ext == ".tsx") {
        return ModuleType::TypeScript;
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
