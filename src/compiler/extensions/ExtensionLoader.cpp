#include "ExtensionLoader.h"
#include <fstream>
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <climits>
#endif

namespace ext {

using json = nlohmann::json;

//=============================================================================
// ExtensionLoader Implementation
//=============================================================================

std::optional<ExtensionContract> ExtensionLoader::loadFromFile(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            lastError_ = "Failed to open file: " + path;
            SPDLOG_ERROR("{}", lastError_);
            return std::nullopt;
        }

        json j = json::parse(file);
        ExtensionContract contract;

        // Parse basic info
        contract.name = j.value("name", "");
        contract.version = j.value("version", "1.0.0");

        if (j.contains("modules") && j["modules"].is_array()) {
            for (const auto& m : j["modules"]) {
                contract.modules.push_back(m.get<std::string>());
            }
        }

        // Parse types
        if (j.contains("types") && j["types"].is_object()) {
            for (auto& [name, typeDef] : j["types"].items()) {
                contract.types[name] = parseType(typeDef);
            }
        }

        // Parse functions
        if (j.contains("functions") && j["functions"].is_object()) {
            for (auto& [name, funcDef] : j["functions"].items()) {
                contract.functions[name] = parseFunction(funcDef);
            }
        }

        // Parse objects (module namespaces)
        if (j.contains("objects") && j["objects"].is_object()) {
            for (auto& [name, objDef] : j["objects"].items()) {
                contract.objects[name] = parseObject(objDef);
            }
        }

        // Parse globals
        if (j.contains("globals") && j["globals"].is_object()) {
            for (auto& [name, globalDef] : j["globals"].items()) {
                contract.globals[name] = parseGlobal(globalDef);
            }
        }

        SPDLOG_INFO("Loaded extension contract: {} v{}", contract.name, contract.version);
        return contract;

    } catch (const json::exception& e) {
        lastError_ = "JSON parse error in " + path + ": " + e.what();
        SPDLOG_ERROR("{}", lastError_);
        return std::nullopt;
    } catch (const std::exception& e) {
        lastError_ = "Error loading " + path + ": " + e.what();
        SPDLOG_ERROR("{}", lastError_);
        return std::nullopt;
    }
}

std::vector<ExtensionContract> ExtensionLoader::loadFromDirectory(const std::string& dirPath) {
    std::vector<ExtensionContract> contracts;

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                // Look for *.ext.json files
                if (filename.size() > 9 && filename.substr(filename.size() - 9) == ".ext.json") {
                    auto contract = loadFromFile(entry.path().string());
                    if (contract) {
                        contracts.push_back(std::move(*contract));
                    }
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        lastError_ = "Filesystem error: " + std::string(e.what());
        SPDLOG_ERROR("{}", lastError_);
    }

    return contracts;
}

TypeReference ExtensionLoader::parseTypeReference(const json& j) {
    TypeReference ref;

    if (j.is_string()) {
        ref.name = j.get<std::string>();
    } else if (j.is_object()) {
        ref.name = j.value("name", "any");
        if (j.contains("typeArgs") && j["typeArgs"].is_array()) {
            for (const auto& arg : j["typeArgs"]) {
                ref.typeArgs.push_back(parseTypeReference(arg));
            }
        }
    }

    return ref;
}

ParameterDefinition ExtensionLoader::parseParameter(const json& j) {
    ParameterDefinition param;
    param.name = j.value("name", "");
    param.type = j.contains("type") ? parseTypeReference(j["type"]) : parseTypeReference("any");
    param.optional = j.value("optional", false);
    param.rest = j.value("rest", false);
    if (j.contains("default") && !j["default"].is_null()) {
        param.defaultValue = j["default"].get<std::string>();
    }
    return param;
}

LoweringSpec ExtensionLoader::parseLowering(const json& j) {
    LoweringSpec spec;

    if (j.contains("args") && j["args"].is_array()) {
        for (const auto& arg : j["args"]) {
            spec.args.push_back(stringToLoweringType(arg.get<std::string>()));
        }
    }

    if (j.contains("returns")) {
        spec.returns = stringToLoweringType(j["returns"].get<std::string>());
    }

    return spec;
}

PropertyDefinition ExtensionLoader::parseProperty(const json& j) {
    PropertyDefinition prop;
    prop.type = j.contains("type") ? parseTypeReference(j["type"]) : parseTypeReference("any");
    if (j.contains("getter") && !j["getter"].is_null()) {
        prop.getter = j["getter"].get<std::string>();
    }
    if (j.contains("setter") && !j["setter"].is_null()) {
        prop.setter = j["setter"].get<std::string>();
    }
    prop.readonly = j.value("readonly", false);
    if (j.contains("lowering")) {
        prop.lowering = parseLowering(j["lowering"]);
    }
    return prop;
}

MethodDefinition ExtensionLoader::parseMethod(const json& j) {
    MethodDefinition method;
    method.call = j.value("call", "");

    // Parse optional HIR name (if different from call)
    if (j.contains("hirName") && !j["hirName"].is_null()) {
        method.hirName = j["hirName"].get<std::string>();
    }

    if (j.contains("params") && j["params"].is_array()) {
        for (const auto& p : j["params"]) {
            method.params.push_back(parseParameter(p));
        }
    }

    method.returns = parseTypeReference(j.value("returns", "void"));

    if (j.contains("typeParams") && j["typeParams"].is_array()) {
        for (const auto& tp : j["typeParams"]) {
            method.typeParams.push_back(tp.get<std::string>());
        }
    }

    if (j.contains("lowering")) {
        method.lowering = parseLowering(j["lowering"]);
    }

    if (j.contains("platformArg") && j["platformArg"].is_number_integer()) {
        method.platformArg = j["platformArg"].get<int>();
    }

    return method;
}

FunctionDefinition ExtensionLoader::parseFunction(const json& j) {
    FunctionDefinition func;
    func.call = j.value("call", "");
    func.async = j.value("async", false);

    if (j.contains("params") && j["params"].is_array()) {
        for (const auto& p : j["params"]) {
            func.params.push_back(parseParameter(p));
        }
    }

    func.returns = parseTypeReference(j.value("returns", "void"));

    if (j.contains("typeParams") && j["typeParams"].is_array()) {
        for (const auto& tp : j["typeParams"]) {
            func.typeParams.push_back(tp.get<std::string>());
        }
    }

    if (j.contains("lowering")) {
        func.lowering = parseLowering(j["lowering"]);
    }

    if (j.contains("overloads") && j["overloads"].is_array()) {
        for (const auto& o : j["overloads"]) {
            func.overloads.push_back(parseFunction(o));
        }
    }

    return func;
}

TypeDefinition ExtensionLoader::parseType(const json& j) {
    TypeDefinition type;

    std::string kindStr = j.value("kind", "class");
    if (kindStr == "interface") {
        type.kind = TypeKind::Interface;
    } else if (kindStr == "enum") {
        type.kind = TypeKind::Enum;
    } else {
        type.kind = TypeKind::Class;
    }

    if (j.contains("typeParams") && j["typeParams"].is_array()) {
        for (const auto& tp : j["typeParams"]) {
            type.typeParams.push_back(tp.get<std::string>());
        }
    }

    if (j.contains("extends") && !j["extends"].is_null()) {
        type.extends = parseTypeReference(j["extends"]);
    }

    if (j.contains("implements") && j["implements"].is_array()) {
        for (const auto& impl : j["implements"]) {
            type.implements.push_back(parseTypeReference(impl));
        }
    }

    if (j.contains("constructor") && !j["constructor"].is_null()) {
        type.constructor = parseMethod(j["constructor"]);
    }

    if (j.contains("properties") && j["properties"].is_object()) {
        for (auto& [name, propDef] : j["properties"].items()) {
            type.properties[name] = parseProperty(propDef);
        }
    }

    if (j.contains("methods") && j["methods"].is_object()) {
        for (auto& [name, methodDef] : j["methods"].items()) {
            type.methods[name] = parseMethod(methodDef);
        }
    }

    if (j.contains("staticProperties") && j["staticProperties"].is_object()) {
        for (auto& [name, propDef] : j["staticProperties"].items()) {
            type.staticProperties[name] = parseProperty(propDef);
        }
    }

    if (j.contains("staticMethods") && j["staticMethods"].is_object()) {
        for (auto& [name, methodDef] : j["staticMethods"].items()) {
            type.staticMethods[name] = parseMethod(methodDef);
        }
    }

    return type;
}

ObjectDefinition ExtensionLoader::parseObject(const json& j) {
    ObjectDefinition obj;

    if (j.contains("properties") && j["properties"].is_object()) {
        for (auto& [name, propDef] : j["properties"].items()) {
            obj.properties[name] = parseProperty(propDef);
        }
    }

    if (j.contains("methods") && j["methods"].is_object()) {
        for (auto& [name, methodDef] : j["methods"].items()) {
            obj.methods[name] = parseMethod(methodDef);
        }
    }

    if (j.contains("nestedObjects") && j["nestedObjects"].is_object()) {
        for (auto& [name, nestedDef] : j["nestedObjects"].items()) {
            obj.nestedObjects[name] = std::make_shared<ObjectDefinition>(parseObject(nestedDef));
        }
    }

    return obj;
}

GlobalDefinition ExtensionLoader::parseGlobal(const json& j) {
    GlobalDefinition global;

    // Check if it's a function or property
    if (j.contains("call")) {
        global.kind = GlobalDefinition::Kind::Function;
        global.function = parseFunction(j);
    } else {
        global.kind = GlobalDefinition::Kind::Property;
        global.property = parseProperty(j);
    }

    // Parse factory function if specified (for property globals)
    if (j.contains("factory") && !j["factory"].is_null()) {
        global.factory = j["factory"].get<std::string>();
    }

    return global;
}

//=============================================================================
// ExtensionRegistry Implementation
//=============================================================================

ExtensionRegistry& ExtensionRegistry::instance() {
    static ExtensionRegistry instance;
    return instance;
}

void ExtensionRegistry::loadDefaultExtensions() {
    // Search relative to compiler executable first, then CWD fallbacks
    std::vector<std::string> searchPaths;

#ifdef _WIN32
    char exeBuf[MAX_PATH];
    if (GetModuleFileNameA(NULL, exeBuf, MAX_PATH)) {
        auto exeDir = std::filesystem::path(exeBuf).parent_path();
        searchPaths.push_back((exeDir / "extensions").string());
    }
#elif defined(__linux__)
    char exeBuf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
    if (len != -1) {
        exeBuf[len] = '\0';
        auto exeDir = std::filesystem::path(exeBuf).parent_path();
        searchPaths.push_back((exeDir / "extensions").string());
    }
#endif

    // CWD-relative fallbacks (development layout)
    searchPaths.push_back("extensions");
    searchPaths.push_back("../extensions");
    searchPaths.push_back("../../extensions");

    // Find the directory with the most ext.json files (best coverage)
    std::string bestPath;
    size_t bestCount = 0;
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
            // Count ext.json files recursively
            size_t count = 0;
            for (auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    auto fname = entry.path().filename().string();
                    if (fname.size() > 9 && fname.substr(fname.size() - 9) == ".ext.json") {
                        count++;
                    }
                }
            }
            if (count > bestCount) {
                bestCount = count;
                bestPath = path;
            }
        }
    }
    if (!bestPath.empty()) {
        loadExtensions(bestPath);
        return;
    }

    SPDLOG_DEBUG("No extensions directory found in default locations");
}

void ExtensionRegistry::loadExtensions(const std::string& dirPath) {
    ExtensionLoader loader;
    auto newContracts = loader.loadFromDirectory(dirPath);

    for (auto& contract : newContracts) {
        contracts_.push_back(std::move(contract));
    }

    rebuildCaches();
    SPDLOG_INFO("Loaded {} extension contracts from {}", newContracts.size(), dirPath);
}

bool ExtensionRegistry::loadExtension(const std::string& filePath) {
    ExtensionLoader loader;
    auto contract = loader.loadFromFile(filePath);

    if (contract) {
        contracts_.push_back(std::move(*contract));
        rebuildCaches();
        return true;
    }

    return false;
}

void ExtensionRegistry::clear() {
    contracts_.clear();
    typeCache_.clear();
    functionCache_.clear();
    objectCache_.clear();
    globalCache_.clear();
    globalTypeCache_.clear();
    moduleCache_.clear();
}

void ExtensionRegistry::rebuildCaches() {
    typeCache_.clear();
    functionCache_.clear();
    objectCache_.clear();
    globalCache_.clear();
    globalTypeCache_.clear();
    moduleCache_.clear();

    for (const auto& contract : contracts_) {
        for (const auto& [name, type] : contract.types) {
            typeCache_[name] = &type;
        }
        for (const auto& [name, func] : contract.functions) {
            functionCache_[name] = &func;
        }
        for (const auto& [name, obj] : contract.objects) {
            objectCache_[name] = &obj;
        }
        for (const auto& [name, global] : contract.globals) {
            globalCache_[name] = &global;
            // For property globals, extract the type name
            if (global.kind == GlobalDefinition::Kind::Property && global.property) {
                globalTypeCache_[name] = global.property->type.name;
            }
        }
        // Map module specifiers to their providing objects
        for (const auto& moduleName : contract.modules) {
            // Find the object that matches this module name
            auto objIt = contract.objects.find(moduleName);
            if (objIt != contract.objects.end()) {
                moduleCache_[moduleName] = &objIt->second;
            }
        }
    }
}

const TypeDefinition* ExtensionRegistry::findType(const std::string& name) const {
    auto it = typeCache_.find(name);
    return it != typeCache_.end() ? it->second : nullptr;
}

const FunctionDefinition* ExtensionRegistry::findFunction(const std::string& name) const {
    auto it = functionCache_.find(name);
    return it != functionCache_.end() ? it->second : nullptr;
}

const ObjectDefinition* ExtensionRegistry::findObject(const std::string& name) const {
    auto it = objectCache_.find(name);
    return it != objectCache_.end() ? it->second : nullptr;
}

const GlobalDefinition* ExtensionRegistry::findGlobal(const std::string& name) const {
    auto it = globalCache_.find(name);
    return it != globalCache_.end() ? it->second : nullptr;
}

std::optional<std::string> ExtensionRegistry::getGlobalTypeName(const std::string& name) const {
    auto it = globalTypeCache_.find(name);
    return it != globalTypeCache_.end() ? std::make_optional(it->second) : std::nullopt;
}

const MethodDefinition* ExtensionRegistry::findMethod(const std::string& typeName, const std::string& methodName) const {
    std::string current = typeName;
    int depth = 0;
    while (depth < 10) {  // Guard against infinite loops
        auto typeIt = typeCache_.find(current);
        if (typeIt == typeCache_.end()) return nullptr;

        const TypeDefinition* type = typeIt->second;
        auto methodIt = type->methods.find(methodName);
        if (methodIt != type->methods.end()) return &methodIt->second;

        // Walk up inheritance chain
        if (type->extends) {
            current = type->extends->name;
            depth++;
        } else {
            return nullptr;
        }
    }
    return nullptr;
}

const MethodDefinition* ExtensionRegistry::findStaticMethod(const std::string& typeName, const std::string& methodName) const {
    std::string current = typeName;
    int depth = 0;
    while (depth < 10) {  // Guard against infinite loops
        auto typeIt = typeCache_.find(current);
        if (typeIt == typeCache_.end()) return nullptr;

        const TypeDefinition* type = typeIt->second;
        auto methodIt = type->staticMethods.find(methodName);
        if (methodIt != type->staticMethods.end()) return &methodIt->second;

        // Walk up inheritance chain
        if (type->extends) {
            current = type->extends->name;
            depth++;
        } else {
            return nullptr;
        }
    }
    return nullptr;
}

const PropertyDefinition* ExtensionRegistry::findProperty(const std::string& typeName, const std::string& propName) const {
    std::string current = typeName;
    int depth = 0;
    while (depth < 10) {  // Guard against infinite loops
        auto typeIt = typeCache_.find(current);
        if (typeIt == typeCache_.end()) return nullptr;

        const TypeDefinition* type = typeIt->second;
        auto propIt = type->properties.find(propName);
        if (propIt != type->properties.end()) return &propIt->second;

        // Walk up inheritance chain
        if (type->extends) {
            current = type->extends->name;
            depth++;
        } else {
            return nullptr;
        }
    }
    return nullptr;
}

bool ExtensionRegistry::isExtensionType(const std::string& typeName) const {
    return typeCache_.find(typeName) != typeCache_.end();
}

bool ExtensionRegistry::isClassKind(const std::string& typeName) const {
    auto it = typeCache_.find(typeName);
    if (it == typeCache_.end()) return false;
    return it->second->kind == ext::TypeKind::Class;
}

std::vector<std::pair<std::string, const GlobalDefinition*>> ExtensionRegistry::getGlobals() const {
    std::vector<std::pair<std::string, const GlobalDefinition*>> result;
    for (const auto& [name, global] : globalCache_) {
        result.emplace_back(name, global);
    }
    return result;
}

const ObjectDefinition* ExtensionRegistry::findObjectByModule(const std::string& moduleName) const {
    auto it = moduleCache_.find(moduleName);
    return it != moduleCache_.end() ? it->second : nullptr;
}

const MethodDefinition* ExtensionRegistry::findObjectMethod(const std::string& objectName, const std::string& methodName) const {
    auto objIt = objectCache_.find(objectName);
    if (objIt == objectCache_.end()) return nullptr;

    const ObjectDefinition* obj = objIt->second;
    auto methodIt = obj->methods.find(methodName);
    return methodIt != obj->methods.end() ? &methodIt->second : nullptr;
}

const PropertyDefinition* ExtensionRegistry::findObjectProperty(const std::string& objectName, const std::string& propName) const {
    auto objIt = objectCache_.find(objectName);
    if (objIt == objectCache_.end()) return nullptr;

    const ObjectDefinition* obj = objIt->second;
    auto propIt = obj->properties.find(propName);
    return propIt != obj->properties.end() ? &propIt->second : nullptr;
}

const ObjectDefinition* ExtensionRegistry::findNestedObject(const std::string& objectName, const std::string& nestedName) const {
    auto objIt = objectCache_.find(objectName);
    if (objIt == objectCache_.end()) return nullptr;

    const ObjectDefinition* obj = objIt->second;
    auto nestedIt = obj->nestedObjects.find(nestedName);
    return nestedIt != obj->nestedObjects.end() ? nestedIt->second.get() : nullptr;
}

const MethodDefinition* ExtensionRegistry::findNestedObjectMethod(const std::string& objectName, const std::string& nestedName, const std::string& methodName) const {
    const ObjectDefinition* nested = findNestedObject(objectName, nestedName);
    if (!nested) return nullptr;

    auto methodIt = nested->methods.find(methodName);
    return methodIt != nested->methods.end() ? &methodIt->second : nullptr;
}

const PropertyDefinition* ExtensionRegistry::findNestedObjectProperty(const std::string& objectName, const std::string& nestedName, const std::string& propName) const {
    const ObjectDefinition* nested = findNestedObject(objectName, nestedName);
    if (!nested) return nullptr;

    auto propIt = nested->properties.find(propName);
    return propIt != nested->properties.end() ? &propIt->second : nullptr;
}

bool ExtensionRegistry::isRegisteredObject(const std::string& name) const {
    return objectCache_.find(name) != objectCache_.end();
}

bool ExtensionRegistry::isRegisteredModule(const std::string& name) const {
    return moduleCache_.find(name) != moduleCache_.end();
}

bool ExtensionRegistry::isRegisteredGlobalOrModule(const std::string& name) const {
    // Check if it's a registered object (console, Math, JSON, Buffer, etc.)
    if (objectCache_.find(name) != objectCache_.end()) return true;
    // Check if it's a registered module (path, fs, url, etc.)
    if (moduleCache_.find(name) != moduleCache_.end()) return true;
    // Check if it's a registered global (process, global, globalThis, etc.)
    if (globalCache_.find(name) != globalCache_.end()) return true;
    return false;
}

} // namespace ext
