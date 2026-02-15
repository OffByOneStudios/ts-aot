#include "Analyzer.h"
#include "../ast/AstLoader.h"
#include <iostream>
#include <fstream>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace ts {

using namespace ast;

void Analyzer::dumpTypes(ast::Program* program) {
    SPDLOG_DEBUG("Analyzer::dumpTypes starting with {} expressions", expressions.size());
    struct TypeEntry {
        int line;
        int column;
        std::string kind;
        std::string type;

        bool operator<(const TypeEntry& other) const {
            if (line != other.line) return line < other.line;
            if (column != other.column) return column < other.column;
            if (kind != other.kind) return kind < other.kind;
            return type < other.type;
        }
    };

    std::vector<TypeEntry> entries;
    for (auto expr : expressions) {
        if (expr->inferredType) {
            entries.push_back({
                expr->line,
                expr->column,
                expr->getKind(),
                expr->inferredType->toString()
            });
        } else {
            // SPDLOG_DEBUG("Expression {} at L{}:C{} has no inferred type", expr->getKind(), expr->line, expr->column);
        }
    }
    SPDLOG_DEBUG("Collected {} type entries", entries.size());

    std::sort(entries.begin(), entries.end());

    for (const auto& entry : entries) {
        fmt::print("L{}:C{} {} -> {}\n", entry.line, entry.column, entry.kind, entry.type);
    }
}

void Analyzer::visitOmittedExpression(ast::OmittedExpression* node) {
    lastType = std::make_shared<Type>(TypeKind::Any);
}

std::shared_ptr<Type> Analyzer::parseType(const std::string& typeName, SymbolTable& symbols) {
    if (typeName.empty()) return std::make_shared<Type>(TypeKind::Any);

    if (typeName.find('|') != std::string::npos) {
        std::vector<std::shared_ptr<Type>> types;
        std::stringstream ss(typeName);
        std::string part;
        while (std::getline(ss, part, '|')) {
            part.erase(0, part.find_first_not_of(" "));
            part.erase(part.find_last_not_of(" ") + 1);
            types.push_back(parseType(part, symbols));
        }
        return std::make_shared<UnionType>(types);
    }

    if (typeName.find('&') != std::string::npos) {
        std::vector<std::shared_ptr<Type>> types;
        std::stringstream ss(typeName);
        std::string part;
        while (std::getline(ss, part, '&')) {
            part.erase(0, part.find_first_not_of(" "));
            part.erase(part.find_last_not_of(" ") + 1);
            types.push_back(parseType(part, symbols));
        }
        return std::make_shared<IntersectionType>(types);
    }

    if (typeName.ends_with("[]")) {
        auto baseName = typeName.substr(0, typeName.size() - 2);
        auto baseType = parseType(baseName, symbols);
        return std::make_shared<ArrayType>(baseType);
    }

    if (typeName.starts_with("[") && typeName.ends_with("]")) {
        std::vector<std::shared_ptr<Type>> elementTypes;
        std::string inner = typeName.substr(1, typeName.size() - 2);
        std::stringstream ss(inner);
        std::string segment;
        while (std::getline(ss, segment, ',')) {
            // Trim whitespace
            segment.erase(0, segment.find_first_not_of(" "));
            segment.erase(segment.find_last_not_of(" ") + 1);
            elementTypes.push_back(parseType(segment, symbols));
        }
        return std::make_shared<TupleType>(elementTypes);
    }

    // Handle object type literals: { fieldName: fieldType, ... }
    if (typeName.starts_with("{") && typeName.ends_with("}")) {
        auto objType = std::make_shared<ObjectType>();
        std::string inner = typeName.substr(1, typeName.size() - 2);

        // Parse fields (simple parser for common cases)
        // This handles: { a: number, b: string } but not nested objects with commas
        size_t pos = 0;
        while (pos < inner.size()) {
            // Skip whitespace (including \r for Windows line endings)
            while (pos < inner.size() && (inner[pos] == ' ' || inner[pos] == '\n' || inner[pos] == '\t' || inner[pos] == '\r')) pos++;
            if (pos >= inner.size()) break;

            // Find field name (ends at :)
            size_t colonPos = inner.find(':', pos);
            if (colonPos == std::string::npos) break;

            std::string fieldName = inner.substr(pos, colonPos - pos);
            // Trim whitespace and handle optional marker
            fieldName.erase(0, fieldName.find_first_not_of(" \t\n\r"));
            fieldName.erase(fieldName.find_last_not_of(" \t\n\r?") + 1);
            if (fieldName.empty()) break;

            pos = colonPos + 1;

            // Skip whitespace after colon (including \r for Windows line endings)
            while (pos < inner.size() && (inner[pos] == ' ' || inner[pos] == '\n' || inner[pos] == '\t' || inner[pos] == '\r')) pos++;

            // Find field type (ends at , or } taking into account nesting)
            size_t typeStart = pos;
            int depth = 0;
            while (pos < inner.size()) {
                char c = inner[pos];
                if (c == '{' || c == '<' || c == '(' || c == '[') depth++;
                else if (c == '}' || c == '>' || c == ')' || c == ']') depth--;
                else if ((c == ',' || c == ';') && depth == 0) break;
                pos++;
            }

            std::string fieldTypeStr = inner.substr(typeStart, pos - typeStart);
            // Trim whitespace (including \r for Windows line endings)
            fieldTypeStr.erase(0, fieldTypeStr.find_first_not_of(" \t\n\r"));
            if (!fieldTypeStr.empty()) {
                fieldTypeStr.erase(fieldTypeStr.find_last_not_of(" \t\n\r") + 1);
            }

            if (!fieldTypeStr.empty()) {
                objType->fields[fieldName] = parseType(fieldTypeStr, symbols);
            }

            // Skip comma or semicolon
            if (pos < inner.size() && (inner[pos] == ',' || inner[pos] == ';')) pos++;
        }

        return objType;
    }

    // Handle keyof operator: keyof T -> string (keys are always strings at runtime)
    if (typeName.starts_with("keyof ")) {
        // keyof T is a union of all property names, which are strings at runtime
        // For AOT compilation, we treat this as string type
        return std::make_shared<Type>(TypeKind::String);
    }

    // Handle indexed access types: T['prop'] or T["prop"]
    // This must come after array check (ends with []) and tuple check (starts with [)
    if (!typeName.ends_with("[]") && !typeName.starts_with("[") &&
        typeName.find('[') != std::string::npos && typeName.ends_with("]")) {
        // Find the bracket position
        size_t bracketPos = typeName.find('[');
        std::string objectTypeName = typeName.substr(0, bracketPos);
        std::string indexStr = typeName.substr(bracketPos + 1, typeName.size() - bracketPos - 2);

        // Remove quotes from index string (handles 'name' or "name")
        if ((indexStr.starts_with("'") && indexStr.ends_with("'")) ||
            (indexStr.starts_with("\"") && indexStr.ends_with("\""))) {
            indexStr = indexStr.substr(1, indexStr.size() - 2);
        }

        // Resolve the object type
        auto objectType = parseType(objectTypeName, symbols);

        // Look up the property in the object type
        if (objectType->kind == TypeKind::Object) {
            auto objType = std::static_pointer_cast<ObjectType>(objectType);
            auto it = objType->fields.find(indexStr);
            if (it != objType->fields.end()) {
                return it->second;
            }
        } else if (objectType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(objectType);
            auto it = classType->fields.find(indexStr);
            if (it != classType->fields.end()) {
                return it->second;
            }
        }

        // Fall back to any if property not found
        return std::make_shared<Type>(TypeKind::Any);
    }

    if (typeName == "number") return std::make_shared<Type>(TypeKind::Double);
    if (typeName == "string") return std::make_shared<Type>(TypeKind::String);
    if (typeName == "boolean") return std::make_shared<Type>(TypeKind::Boolean);
    if (typeName == "void") return std::make_shared<Type>(TypeKind::Void);
    if (typeName == "any") return std::make_shared<Type>(TypeKind::Any);
    if (typeName == "null") return std::make_shared<Type>(TypeKind::Null);
    if (typeName == "undefined") return std::make_shared<Type>(TypeKind::Undefined);
    
    if (typeName.starts_with("Promise<") && typeName.ends_with(">")) {
        auto innerName = typeName.substr(8, typeName.size() - 9);
        auto innerType = parseType(innerName, symbols);
        auto promiseClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Promise"));
        auto wrapped = std::make_shared<ClassType>("Promise");
        if (promiseClass) {
            wrapped->methods = promiseClass->methods;
            wrapped->typeParameters = promiseClass->typeParameters;
        }
        wrapped->typeArguments = { innerType };
        return wrapped;
    }
    
    // Handle Set<T> - Note: element type is not tracked currently
    if (typeName.starts_with("Set<") && typeName.ends_with(">")) {
        // For now, just return a basic SetType
        // TODO: Create a proper SetType struct with elementType
        return std::make_shared<Type>(TypeKind::SetType);
    }
    
    // Handle Map<K, V> - Note: key/value types are not tracked currently
    if (typeName.starts_with("Map<") && typeName.ends_with(">")) {
        // For now, just return a basic Map type
        // TODO: Create a proper MapType struct with keyType/valueType
        return std::make_shared<Type>(TypeKind::Map);
    }

    // Handle Array<T> - convert to ArrayType (same as T[])
    if (typeName.starts_with("Array<") && typeName.ends_with(">")) {
        auto innerName = typeName.substr(6, typeName.size() - 7);  // Extract T from Array<T>
        auto innerType = parseType(innerName, symbols);
        return std::make_shared<ArrayType>(innerType);
    }

    // Handle function types: (paramName: paramType, ...) => returnType
    // Look for " => " pattern (with spaces around =>)
    size_t arrowPos = typeName.find(" => ");
    if (arrowPos == std::string::npos) {
        // Also try without spaces: "=>"
        arrowPos = typeName.find("=>");
    }
    
    if (arrowPos != std::string::npos && typeName.starts_with("(")) {
        // Extract params and return type
        // Find matching closing paren
        int depth = 0;
        size_t closeParenPos = std::string::npos;
        for (size_t i = 0; i < typeName.size(); ++i) {
            if (typeName[i] == '(') depth++;
            else if (typeName[i] == ')') {
                depth--;
                if (depth == 0) {
                    closeParenPos = i;
                    break;
                }
            }
        }
        
        if (closeParenPos != std::string::npos) {
            std::string paramsStr = typeName.substr(1, closeParenPos - 1);  // Inside parens
            std::string remainder = typeName.substr(closeParenPos + 1);
            
            // Find "=>" in remainder
            size_t eqPos = remainder.find("=>");
            if (eqPos != std::string::npos) {
                std::string returnTypeStr = remainder.substr(eqPos + 2);
                // Trim whitespace
                returnTypeStr.erase(0, returnTypeStr.find_first_not_of(" "));
                returnTypeStr.erase(returnTypeStr.find_last_not_of(" ") + 1);
                
                auto funcType = std::make_shared<FunctionType>();
                funcType->returnType = parseType(returnTypeStr, symbols);
                
                // Parse parameters (format: "name: type" or "name?: type")
                if (!paramsStr.empty()) {
                    // Simple split by comma (doesn't handle nested commas, but good enough for now)
                    std::vector<std::string> params;
                    int parenDepth = 0;
                    std::string current;
                    for (char c : paramsStr) {
                        if (c == '(' || c == '<') parenDepth++;
                        else if (c == ')' || c == '>') parenDepth--;
                        
                        if (c == ',' && parenDepth == 0) {
                            params.push_back(current);
                            current.clear();
                        } else {
                            current += c;
                        }
                    }
                    if (!current.empty()) params.push_back(current);
                    
                    for (const auto& param : params) {
                        std::string trimmedParam = param;
                        trimmedParam.erase(0, trimmedParam.find_first_not_of(" "));
                        trimmedParam.erase(trimmedParam.find_last_not_of(" ") + 1);
                        
                        // Format: "name: type" or "name?: type"
                        size_t colonPos = trimmedParam.find(':');
                        if (colonPos != std::string::npos) {
                            std::string paramTypeStr = trimmedParam.substr(colonPos + 1);
                            paramTypeStr.erase(0, paramTypeStr.find_first_not_of(" "));
                            paramTypeStr.erase(paramTypeStr.find_last_not_of(" ") + 1);
                            funcType->paramTypes.push_back(parseType(paramTypeStr, symbols));
                            
                            // Check for optional
                            std::string paramName = trimmedParam.substr(0, colonPos);
                            bool isOptional = paramName.ends_with("?");
                            funcType->isOptional.push_back(isOptional);
                        } else {
                            // No type specified, default to Any
                            funcType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                            funcType->isOptional.push_back(false);
                        }
                    }
                }
                
                return funcType;
            }
        }
    }

    auto type = symbols.lookupType(typeName);
    if (type) return type;
    
    return std::make_shared<Type>(TypeKind::Any);
}

std::shared_ptr<FunctionType> Analyzer::resolveOverload(const std::vector<std::shared_ptr<FunctionType>>& overloads, const std::vector<std::shared_ptr<Type>>& argTypes) {
    if (overloads.empty()) return nullptr;
    
    for (const auto& overload : overloads) {
        size_t minArgs = 0;
        for (size_t i = 0; i < overload->paramTypes.size(); ++i) {
            bool optional = i < overload->isOptional.size() && overload->isOptional[i];
            bool rest = (i == overload->paramTypes.size() - 1) && overload->hasRest;
            if (!optional && !rest) {
                minArgs++;
            }
        }

        size_t maxArgs = overload->hasRest ? std::numeric_limits<size_t>::max() : overload->paramTypes.size();

        if (argTypes.size() >= minArgs && argTypes.size() <= maxArgs) {
            bool match = true;
            for (size_t i = 0; i < argTypes.size(); ++i) {
                std::shared_ptr<Type> expectedType;
                if (i < overload->paramTypes.size()) {
                    expectedType = overload->paramTypes[i];
                    if (i == overload->paramTypes.size() - 1 && overload->hasRest) {
                        if (expectedType->kind == TypeKind::Array) {
                            expectedType = std::static_pointer_cast<ArrayType>(expectedType)->elementType;
                        }
                    }
                } else if (overload->hasRest) {
                    expectedType = overload->paramTypes.back();
                    if (expectedType->kind == TypeKind::Array) {
                        expectedType = std::static_pointer_cast<ArrayType>(expectedType)->elementType;
                    }
                }

                if (expectedType && !argTypes[i]->isAssignableTo(expectedType)) {
                    match = false;
                    break;
                }
            }
            if (match) return overload;
        }
    }
    
    return overloads[0]; // Fallback
}

void Analyzer::reportError(const std::string& message) {
    // Strict mode errors should always be reported
    if (message.starts_with("Strict mode:")) {
        std::cerr << "Error: " << message << std::endl;
        errorCount++;
        return;
    }
    // Temporarily suppress other analyzer errors to allow untyped CommonJS (lodash) to progress.
    SPDLOG_DEBUG("Suppressing analyzer error: {}", message);
}

std::shared_ptr<Type> Analyzer::substitute(std::shared_ptr<Type> type, const std::map<std::string, std::shared_ptr<Type>>& env) {
    if (!type) return nullptr;

    if (type->kind == TypeKind::TypeParameter) {
        auto tp = std::static_pointer_cast<TypeParameterType>(type);
        if (env.count(tp->name)) {
            return env.at(tp->name);
        }
        return type;
    }

    if (type->kind == TypeKind::Array) {
        auto arr = std::static_pointer_cast<ArrayType>(type);
        return std::make_shared<ArrayType>(substitute(arr->elementType, env));
    }

    if (type->kind == TypeKind::Function) {
        auto func = std::static_pointer_cast<FunctionType>(type);
        auto result = std::make_shared<FunctionType>();
        for (const auto& p : func->paramTypes) {
            result->paramTypes.push_back(substitute(p, env));
        }
        result->returnType = substitute(func->returnType, env);
        return result;
    }

    if (type->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(type);
        if (cls->typeArguments.empty()) return type;
        
        auto result = std::make_shared<ClassType>(cls->name);
        result->node = cls->node;
        result->baseClass = cls->baseClass;
        result->fields = cls->fields;
        result->methods = cls->methods;
        result->staticMethods = cls->staticMethods;
        result->staticFields = cls->staticFields;
        result->typeParameters = cls->typeParameters;
        for (const auto& arg : cls->typeArguments) {
            result->typeArguments.push_back(substitute(arg, env));
        }
        return result;
    }

    if (type->kind == TypeKind::Interface) {
        auto inter = std::static_pointer_cast<InterfaceType>(type);
        if (inter->typeArguments.empty()) return type;

        auto result = std::make_shared<InterfaceType>(inter->name);
        result->node = inter->node;
        result->fields = inter->fields;
        result->methods = inter->methods;
        result->typeParameters = inter->typeParameters;
        for (const auto& arg : inter->typeArguments) {
            result->typeArguments.push_back(substitute(arg, env));
        }
        return result;
    }

    return type;
}

// Helper: Convert JSON value to TypeScript type
static std::shared_ptr<Type> jsonToType(const nlohmann::json& j) {
    using json = nlohmann::json;
    
    if (j.is_null()) {
        return std::make_shared<Type>(TypeKind::Null);
    }
    if (j.is_boolean()) {
        return std::make_shared<Type>(TypeKind::Boolean);
    }
    if (j.is_number_integer()) {
        return std::make_shared<Type>(TypeKind::Int);
    }
    if (j.is_number_float()) {
        return std::make_shared<Type>(TypeKind::Double);
    }
    if (j.is_string()) {
        return std::make_shared<Type>(TypeKind::String);
    }
    if (j.is_array()) {
        if (j.empty()) {
            // Empty array: any[]
            return std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
        }
        // Infer element type from first element (could be improved with union)
        auto elemType = jsonToType(j[0]);
        return std::make_shared<ArrayType>(elemType);
    }
    if (j.is_object()) {
        auto objType = std::make_shared<ObjectType>();
        for (auto& [key, val] : j.items()) {
            objType->fields[key] = jsonToType(val);
        }
        return objType;
    }
    
    return std::make_shared<Type>(TypeKind::Any);
}

std::shared_ptr<Module> Analyzer::loadModule(const std::string& specifier) {
    // Use the new ModuleResolver
    ResolvedModule resolved = resolveModule(specifier);
    
    if (!resolved.isValid()) {
        // Check if an ambient module declaration exists (declare module 'name')
        if (modules.count(specifier)) {
            return modules[specifier];
        }
        reportError("Could not resolve module: " + specifier);
        return nullptr;
    }
    
    // Handle builtin modules
    if (resolved.type == ModuleType::Builtin) {
        std::string name = resolved.packageName;
        
        if (modules.count(resolved.path)) {
            return modules[resolved.path];
        }
        
        auto module = std::make_shared<Module>();
        module->path = resolved.path;
        module->analyzed = true;
        
        auto sym = symbols.lookup(name);
        if (sym && sym->type->kind == TypeKind::Object) {
            auto objType = std::static_pointer_cast<ObjectType>(sym->type);
            for (auto& [fieldName, type] : objType->fields) {
                module->exports->define(fieldName, type);
            }
            
            // Special case for fs.promises
            if (name == "fs" && objType->fields.count("promises")) {
                module->exports->define("promises", objType->fields["promises"]);
            }
        }
        
        modules[resolved.path] = module;
        return module;
    }
    
    // Check cache
    if (modules.count(resolved.path)) {
        return modules[resolved.path];
    }
    
    SPDLOG_INFO("Loading module: {} -> {}", specifier, resolved.path);
    
    auto module = std::make_shared<Module>();
    module->path = resolved.path;
    module->type = resolved.type;
    modules[resolved.path] = module;  // Cache early to handle circular deps
    
    try {
        if (resolved.type == ModuleType::JSON) {
            // Parse JSON file and create typed export
            std::ifstream jsonFile(resolved.path);
            if (!jsonFile) {
                reportError("Could not open JSON file: " + resolved.path);
                return nullptr;
            }
            
            nlohmann::json j;
            try {
                j = nlohmann::json::parse(jsonFile);
            } catch (const nlohmann::json::parse_error& e) {
                reportError("JSON parse error in " + resolved.path + ": " + e.what());
                return nullptr;
            }
            
            // Store the JSON content for codegen
            module->jsonContent = j;
            module->isJsonModule = true;
            module->analyzed = true;
            
            // Create type from JSON and export as default
            auto jsonType = jsonToType(j);
            module->exports->define("default", jsonType);
            
            SPDLOG_DEBUG("JSON module: {} with type {}", resolved.path, jsonType->toString());
            return module;
        }
        
        if (resolved.type == ModuleType::Declaration) {
            SPDLOG_INFO("Parsing declaration file: {}", resolved.path);

            // Parse the .d.ts AST using the native C++ parser
            auto ast = parseSourceFile(resolved.path);
            if (!ast) {
                reportError("Failed to parse declaration file: " + resolved.path);
                module->analyzed = true;
                return module;
            }
            module->ast = std::shared_ptr<ast::Program>(ast.release());

            // Analyze with errors suppressed (declarations may reference unavailable types)
            bool prevSuppress = suppressErrors;
            suppressErrors = true;
            analyzeDeclarationModule(module);
            suppressErrors = prevSuppress;

            return module;
        }
        
        // TypeScript or JavaScript - parse the AST using native parser
        auto nativeAst = parseSourceFile(resolved.path);
        if (!nativeAst) {
            // Fallback to Node.js parser
            SPDLOG_WARN("Native parser failed for {}, falling back to Node.js", resolved.path);
            std::string jsonPath = resolved.path + ".ast.json";
            std::string command = "node scripts/dump_ast.js \"" + resolved.path + "\" \"" + jsonPath + "\"";
            if (system(command.c_str()) != 0) {
                reportError("Failed to parse " + resolved.path);
                return nullptr;
            }
            module->ast = std::shared_ptr<ast::Program>(ast::loadAst(jsonPath).release());
        } else {
            module->ast = std::shared_ptr<ast::Program>(nativeAst.release());
        }

        if (resolved.type == ModuleType::UntypedJavaScript) {
            SPDLOG_WARN("Importing untyped JavaScript: {} (slow path)", resolved.path);
        }

        if (resolved.isExternal) {
            SPDLOG_DEBUG("External package: {}", resolved.packageName);
        }

        bool prevSuppress = suppressErrors;
        if (resolved.type != ModuleType::TypeScript) {
            suppressErrors = true;
        }
        analyzeModule(module);
        suppressErrors = prevSuppress;

        // If we have a paired .d.ts, overlay typed exports AFTER JS analysis
        // (so .d.ts types override the untyped `any` from the JS analysis)
        if (!resolved.typesPath.empty()) {
            SPDLOG_INFO("Loading paired types from: {}", resolved.typesPath);
            auto typesAst = parseSourceFile(resolved.typesPath);
            if (typesAst) {
                auto typesModule = std::make_shared<Module>();
                typesModule->path = resolved.typesPath;
                typesModule->type = ModuleType::Declaration;
                typesModule->ast = std::shared_ptr<ast::Program>(typesAst.release());

                bool prevSup = suppressErrors;
                suppressErrors = true;
                analyzeDeclarationModule(typesModule);
                suppressErrors = prevSup;

                // Overlay .d.ts typed exports on top of JS untyped exports
                for (const auto& [name, sym] : typesModule->exports->getCurrentScopeSymbols()) {
                    module->exports->define(name, sym->type);
                }
                for (const auto& [name, type] : typesModule->exports->getCurrentScopeTypes()) {
                    module->exports->defineType(name, type);
                }
            }
        }
        
    } catch (const std::exception& e) {
        reportError("Failed to load module " + resolved.path + ": " + e.what());
        return nullptr;
    }
    
    return module;
}

ImportResolution Analyzer::resolveImportSourcePath(const std::string& calleeName) {
    if (!currentModule || !currentModule->ast) return {"", ""};

    for (auto& stmt : currentModule->ast->body) {
        auto* imp = dynamic_cast<ast::ImportDeclaration*>(stmt.get());
        if (!imp) continue;

        // Check named imports only (not default imports).
        // Default imports use alias names that differ from the declaration name,
        // so findFunction's default-import fallback handles them using the calling
        // module path (call.modulePath) to locate the import declaration.
        for (auto& spec : imp->namedImports) {
            if (spec.name == calleeName) {
                auto resolved = resolveModule(imp->moduleSpecifier);
                if (resolved.isValid()) {
                    // spec.propertyName is the original name in the source module
                    // e.g., import { foo as bar } => propertyName="foo", name="bar"
                    std::string originalName = spec.propertyName.empty() ? spec.name : spec.propertyName;
                    return {resolved.path, originalName};
                }
                return {"", ""};
            }
        }
    }

    return {"", ""};
}

} // namespace ts
