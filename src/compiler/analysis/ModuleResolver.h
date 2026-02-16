#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace ts {

namespace fs = std::filesystem;

// Represents the type of module being resolved
enum class ModuleType {
    TypeScript,      // .ts, .tsx - full type inference, AOT optimization
    TypedJavaScript, // .js with JSDoc annotations - extracted types
    UntypedJavaScript, // .js without types - slow path (boxed values)
    JSON,            // .json - compile-time embedded
    Declaration,     // .d.ts - types only, no codegen
    Builtin          // fs, path, http, etc. - handled specially
};

// Result of module resolution
struct ResolvedModule {
    std::string path;           // Absolute file path (implementation: .ts or .js)
    std::string typesPath;      // Optional .d.ts path for type info (when paired with .js)
    ModuleType type;            // Type of module
    std::string packageName;    // Package name if from node_modules (e.g., "lodash")
    bool isExternal = false;    // True if from node_modules
    bool isESM = false;         // True if package.json "type": "module" or .mjs

    bool isValid() const { return !path.empty(); }
};

// Parsed package.json fields
struct PackageJson {
    std::string name;
    std::string version;
    std::string main;           // CommonJS entry point
    std::string module;         // ESM entry point (unofficial but common)
    std::string types;          // TypeScript declarations
    bool hasTypeModule = false; // "type": "module"
    
    // "exports" field (conditional exports) - simplified for now
    std::map<std::string, std::string> exports;

    // Wildcard export patterns: "./*" -> "./dist/*.js"
    std::vector<std::pair<std::string, std::string>> wildcardExports;

    bool isValid() const { return !name.empty(); }
};

// Parsed tsconfig.json fields
struct TsConfig {
    std::string baseUrl;
    std::map<std::string, std::vector<std::string>> paths; // Path aliases
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    std::string rootDir;
    std::string outDir;
    
    bool isValid() const { return !baseUrl.empty() || !paths.empty(); }
};

class ModuleResolver {
public:
    ModuleResolver();
    
    // Set the directory of the file doing the importing
    void setCurrentDirectory(const fs::path& dir);
    
    // Set project root (for tsconfig.json resolution)
    void setProjectRoot(const fs::path& root);
    
    // Load tsconfig.json from project root
    bool loadTsConfig(const fs::path& tsconfigPath);
    
    // Main resolution function
    // specifier: the import string (e.g., "./foo", "lodash", "@org/pkg")
    // fromFile: the file doing the importing
    ResolvedModule resolve(const std::string& specifier, const fs::path& fromFile);
    
    // Check if a specifier is a builtin module
    static bool isBuiltinModule(const std::string& specifier);
    
    // Get list of builtin modules
    static const std::vector<std::string>& getBuiltinModules();
    
    // Determine module type from file extension
    static ModuleType getModuleType(const fs::path& filePath);
    
private:
    // Resolution strategies
    ResolvedModule resolveRelative(const std::string& specifier, const fs::path& fromDir);
    ResolvedModule resolveNodeModules(const std::string& specifier, const fs::path& fromDir);
    ResolvedModule resolveTsConfigPaths(const std::string& specifier);
    
    // Try to resolve a path with various extensions
    std::optional<fs::path> tryExtensions(const fs::path& basePath);
    
    // Try to resolve a directory (index.ts, index.js, etc.)
    std::optional<fs::path> tryDirectory(const fs::path& dirPath);
    
    // Parse package.json
    std::optional<PackageJson> parsePackageJson(const fs::path& packageJsonPath);

    // Get the entry point from a package
    std::optional<fs::path> getPackageEntryPoint(const fs::path& packageDir, const PackageJson& pkg);

    // Recursively resolve conditional exports (handles nested { "import": { "types": ..., "default": ... } })
    static std::optional<std::string> resolveExportCondition(const nlohmann::json& val);
    
    fs::path currentDir;
    fs::path projectRoot;
    TsConfig tsConfig;
    
    // Cache for package.json files
    std::map<std::string, PackageJson> packageJsonCache;

    // Side-effect from getPackageEntryPoint: .d.ts path for paired loading
    std::string lastResolvedTypesPath_;

    // Extension priority order
    static const std::vector<std::string> EXTENSIONS;
    static const std::vector<std::string> INDEX_FILES;
};

} // namespace ts
