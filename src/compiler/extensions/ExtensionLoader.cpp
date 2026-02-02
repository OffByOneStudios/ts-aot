#include "ExtensionLoader.h"
#include <fstream>
#include <spdlog/spdlog.h>

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
    param.type = parseTypeReference(j.value("type", "any"));
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
    prop.type = parseTypeReference(j.value("type", "any"));
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
    // Look for extensions directory relative to executable or working directory
    std::vector<std::string> searchPaths = {
        "extensions",
        "../extensions",
        "../../extensions"
    };

    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
            loadExtensions(path);
            return;
        }
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
}

void ExtensionRegistry::rebuildCaches() {
    typeCache_.clear();
    functionCache_.clear();
    objectCache_.clear();
    globalCache_.clear();
    globalTypeCache_.clear();

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
    auto typeIt = typeCache_.find(typeName);
    if (typeIt == typeCache_.end()) return nullptr;

    const TypeDefinition* type = typeIt->second;
    auto methodIt = type->methods.find(methodName);
    return methodIt != type->methods.end() ? &methodIt->second : nullptr;
}

const PropertyDefinition* ExtensionRegistry::findProperty(const std::string& typeName, const std::string& propName) const {
    auto typeIt = typeCache_.find(typeName);
    if (typeIt == typeCache_.end()) return nullptr;

    const TypeDefinition* type = typeIt->second;
    auto propIt = type->properties.find(propName);
    return propIt != type->properties.end() ? &propIt->second : nullptr;
}

} // namespace ext
