#pragma once

#include "ExtensionSchema.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace ext {

//=============================================================================
// Extension Loader
//=============================================================================
// Loads extension contracts from JSON files and provides access to the
// combined type and function definitions.

class ExtensionLoader {
public:
    // Load a single extension contract from a JSON file
    std::optional<ExtensionContract> loadFromFile(const std::string& path);

    // Load all extension contracts from a directory (*.ext.json files)
    std::vector<ExtensionContract> loadFromDirectory(const std::string& dirPath);

    // Get the last error message (if any operation failed)
    const std::string& getLastError() const { return lastError_; }

private:
    std::string lastError_;

    // Parsing helpers
    TypeReference parseTypeReference(const nlohmann::json& j);
    ParameterDefinition parseParameter(const nlohmann::json& j);
    PropertyDefinition parseProperty(const nlohmann::json& j);
    MethodDefinition parseMethod(const nlohmann::json& j);
    FunctionDefinition parseFunction(const nlohmann::json& j);
    TypeDefinition parseType(const nlohmann::json& j);
    ObjectDefinition parseObject(const nlohmann::json& j);
    GlobalDefinition parseGlobal(const nlohmann::json& j);
    LoweringSpec parseLowering(const nlohmann::json& j);
};

//=============================================================================
// Extension Registry
//=============================================================================
// Central registry that holds all loaded extension contracts and provides
// lookup methods for the Analyzer and LoweringRegistry to query.

class ExtensionRegistry {
public:
    // Singleton access
    static ExtensionRegistry& instance();

    // Load extensions from the default extensions directory
    void loadDefaultExtensions();

    // Load extensions from a specific directory
    void loadExtensions(const std::string& dirPath);

    // Load a single extension file
    bool loadExtension(const std::string& filePath);

    // Clear all loaded extensions
    void clear();

    // Query methods for Analyzer
    const TypeDefinition* findType(const std::string& name) const;
    const FunctionDefinition* findFunction(const std::string& name) const;
    const ObjectDefinition* findObject(const std::string& name) const;
    const GlobalDefinition* findGlobal(const std::string& name) const;

    // Query method with type info (returns typeName for global variables)
    std::optional<std::string> getGlobalTypeName(const std::string& name) const;

    // Query methods for LoweringRegistry
    const MethodDefinition* findMethod(const std::string& typeName, const std::string& methodName) const;
    const MethodDefinition* findStaticMethod(const std::string& typeName, const std::string& methodName) const;
    const PropertyDefinition* findProperty(const std::string& typeName, const std::string& propName) const;

    // Check if a type is defined by an extension (for codegen to skip VTable)
    bool isExtensionType(const std::string& typeName) const;

    // Check if a type has kind == "class" (methods are standalone C functions)
    // vs kind == "interface" (methods are closures stored on objects)
    bool isClassKind(const std::string& typeName) const;

    // Get all globals that need initialization (for codegen)
    std::vector<std::pair<std::string, const GlobalDefinition*>> getGlobals() const;

    // Get all loaded contracts
    const std::vector<ExtensionContract>& getContracts() const { return contracts_; }

    // Find object that provides a given module specifier (e.g., "path")
    const ObjectDefinition* findObjectByModule(const std::string& moduleName) const;

    // Find a method on an object (for module namespaces like path.join)
    const MethodDefinition* findObjectMethod(const std::string& objectName, const std::string& methodName) const;

    // Find a property on an object (for module namespaces like path.sep)
    const PropertyDefinition* findObjectProperty(const std::string& objectName, const std::string& propName) const;

    // Find a nested object within an object (for path.win32, path.posix)
    const ObjectDefinition* findNestedObject(const std::string& objectName, const std::string& nestedName) const;

    // Find a method on a nested object (for path.win32.join)
    const MethodDefinition* findNestedObjectMethod(const std::string& objectName, const std::string& nestedName, const std::string& methodName) const;

    // Find a property on a nested object (for path.win32.sep)
    const PropertyDefinition* findNestedObjectProperty(const std::string& objectName, const std::string& nestedName, const std::string& propName) const;

    // Check if a name is registered as an object (console, Math, JSON, etc.)
    bool isRegisteredObject(const std::string& name) const;

    // Check if a name is registered as a module (path, fs, url, etc.)
    bool isRegisteredModule(const std::string& name) const;

    // Check if a name is registered as an object OR module (for visitIdentifier)
    bool isRegisteredGlobalOrModule(const std::string& name) const;

private:
    ExtensionRegistry() = default;
    std::vector<ExtensionContract> contracts_;

    // Cached lookups (built from contracts_)
    std::unordered_map<std::string, const TypeDefinition*> typeCache_;
    std::unordered_map<std::string, const FunctionDefinition*> functionCache_;
    std::unordered_map<std::string, const ObjectDefinition*> objectCache_;
    std::unordered_map<std::string, const GlobalDefinition*> globalCache_;
    std::unordered_map<std::string, std::string> globalTypeCache_;  // globalName -> typeName
    std::unordered_map<std::string, const ObjectDefinition*> moduleCache_;  // moduleName -> object

    void rebuildCaches();
};

} // namespace ext
