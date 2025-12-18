#include "Analyzer.h"
#include "../ast/AstLoader.h"
#include <iostream>
#include <fmt/core.h>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace ts {

using namespace ast;

Analyzer::Analyzer() {
    // Register JSON global
    auto jsonType = std::make_shared<ObjectType>();
    
    auto parseType = std::make_shared<FunctionType>();
    parseType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    parseType->returnType = std::make_shared<Type>(TypeKind::Any);
    jsonType->fields["parse"] = parseType;
    
    auto stringifyType = std::make_shared<FunctionType>();
    stringifyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    stringifyType->returnType = std::make_shared<Type>(TypeKind::String);
    jsonType->fields["stringify"] = stringifyType;
    
    symbols.define("JSON", jsonType);

    // Register Date class
    auto dateClass = std::make_shared<ClassType>("Date");
    
    auto getTime = std::make_shared<FunctionType>();
    getTime->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getTime"] = getTime;
    
    auto getFullYear = std::make_shared<FunctionType>();
    getFullYear->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getFullYear"] = getFullYear;

    auto getMonth = std::make_shared<FunctionType>();
    getMonth->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getMonth"] = getMonth;

    auto getDate = std::make_shared<FunctionType>();
    getDate->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getDate"] = getDate;
    
    auto now = std::make_shared<FunctionType>();
    now->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->staticMethods["now"] = now;
    
    symbols.defineType("Date", dateClass);

    // Register RegExp class
    auto regexpClass = std::make_shared<ClassType>("RegExp");
    
    auto testType = std::make_shared<FunctionType>();
    testType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    testType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    regexpClass->methods["test"] = testType;
    
    auto execType = std::make_shared<FunctionType>();
    execType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    auto stringArray = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    execType->returnType = stringArray; // For now, just say it returns String[]
    regexpClass->methods["exec"] = execType;
    
    symbols.defineType("RegExp", regexpClass);

    // Register console global
    auto consoleType = std::make_shared<ObjectType>();
    auto logType = std::make_shared<FunctionType>();
    logType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    logType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["log"] = logType;
    symbols.define("console", consoleType);

    // Register ts_console_log
    auto consoleLogType = std::make_shared<FunctionType>();
    consoleLogType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    consoleLogType->returnType = std::make_shared<Type>(TypeKind::Void);
    symbols.define("ts_console_log", consoleLogType);

    // Register Promise class
    auto promiseClass = std::make_shared<ClassType>("Promise");
    auto tParam = std::make_shared<TypeParameterType>("T");
    promiseClass->typeParameters.push_back(tParam);

    auto thenType = std::make_shared<FunctionType>();
    auto thenCallback = std::make_shared<FunctionType>();
    thenCallback->paramTypes.push_back(tParam);
    thenCallback->returnType = std::make_shared<Type>(TypeKind::Any); // Simplified
    thenType->paramTypes.push_back(thenCallback);
    thenType->returnType = promiseClass; // Returns Promise<any> for now
    promiseClass->methods["then"] = thenType;

    auto resolveType = std::make_shared<FunctionType>();
    resolveType->paramTypes.push_back(tParam);
    resolveType->returnType = promiseClass;
    promiseClass->staticMethods["resolve"] = resolveType;

    symbols.defineType("Promise", promiseClass);

    // Register Timers
    auto setTimeoutType = std::make_shared<FunctionType>();
    auto timerCallback = std::make_shared<FunctionType>();
    timerCallback->returnType = std::make_shared<Type>(TypeKind::Void);
    setTimeoutType->paramTypes.push_back(timerCallback);
    setTimeoutType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setTimeoutType->returnType = std::make_shared<Type>(TypeKind::Int);
    symbols.define("setTimeout", setTimeoutType);

    auto setIntervalType = std::make_shared<FunctionType>();
    setIntervalType->paramTypes.push_back(timerCallback);
    setIntervalType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setIntervalType->returnType = std::make_shared<Type>(TypeKind::Int);
    symbols.define("setInterval", setIntervalType);

    auto clearTimeoutType = std::make_shared<FunctionType>();
    clearTimeoutType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    clearTimeoutType->returnType = std::make_shared<Type>(TypeKind::Void);
    symbols.define("clearTimeout", clearTimeoutType);
    symbols.define("clearInterval", clearTimeoutType);

    // Register fs global
    auto fsType = std::make_shared<ObjectType>();
    
    auto readFileSyncType = std::make_shared<FunctionType>();
    readFileSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    readFileSyncType->returnType = std::make_shared<Type>(TypeKind::String);
    fsType->fields["readFileSync"] = readFileSyncType;
    
    auto writeFileSyncType = std::make_shared<FunctionType>();
    writeFileSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeFileSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeFileSyncType->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["writeFileSync"] = writeFileSyncType;
    
    auto promisesType = std::make_shared<ObjectType>();
    auto readFileAsyncType = std::make_shared<FunctionType>();
    readFileAsyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    auto stringPromise = std::make_shared<ClassType>("Promise");
    stringPromise->typeArguments.push_back(std::make_shared<Type>(TypeKind::String));
    readFileAsyncType->returnType = stringPromise;
    promisesType->fields["readFile"] = readFileAsyncType;
    fsType->fields["promises"] = promisesType;
    
    symbols.define("fs", fsType);

    // Register fetch global
    auto fetchType = std::make_shared<FunctionType>();
    fetchType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    auto anyPromise = std::make_shared<ClassType>("Promise");
    anyPromise->typeArguments.push_back(std::make_shared<Type>(TypeKind::Any));
    fetchType->returnType = anyPromise;
    symbols.define("fetch", fetchType);
}

void Analyzer::analyze(ast::Program* program, const std::string& path) {
    currentFilePath = fs::absolute(path).string();
    auto mainModule = std::make_shared<Module>();
    mainModule->path = currentFilePath;
    // We don't own the main program's AST, but we can wrap it in a shared_ptr with a no-op deleter
    // or just assume it lives long enough.
    mainModule->ast = std::shared_ptr<ast::Program>(program, [](ast::Program*){});
    currentModule = mainModule;
    modules[currentFilePath] = mainModule;

    symbols.enterScope();
    visitProgram(program);
    symbols.exitScope();
    
    mainModule->analyzed = true;
    moduleOrder.push_back(currentFilePath);
}

void Analyzer::analyzeModule(std::shared_ptr<Module> module) {
    auto oldModule = currentModule;
    auto oldPath = currentFilePath;

    currentModule = module;
    currentFilePath = module->path;

    symbols.enterScope();
    visitProgram(module->ast.get());
    symbols.exitScope();
    
    module->analyzed = true;
    moduleOrder.push_back(module->path);

    currentModule = oldModule;
    currentFilePath = oldPath;
}

std::string Analyzer::resolveModulePath(const std::string& specifier) {
    fs::path base = fs::path(currentFilePath).parent_path();
    fs::path resolved = base / specifier;
    
    // Try .ts, then .json (pre-compiled AST)
    if (fs::exists(resolved.string() + ".ts")) return fs::absolute(resolved.string() + ".ts").string();
    if (fs::exists(resolved.string() + ".json")) return fs::absolute(resolved.string() + ".json").string();
    
    return "";
}

void Analyzer::visitProgram(Program* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

void Analyzer::visit(Node* node) {
    if (!node) return;
    node->accept(this);

    if (auto expr = dynamic_cast<Expression*>(node)) {
        expr->inferredType = lastType;
    }
}

void Analyzer::declareBindingPattern(ast::Node* pattern, std::shared_ptr<Type> type) {
    if (auto id = dynamic_cast<Identifier*>(pattern)) {
        symbols.define(id->name, type);
    } else if (auto obp = dynamic_cast<ObjectBindingPattern*>(pattern)) {
        for (auto& elementNode : obp->elements) {
            auto element = dynamic_cast<BindingElement*>(elementNode.get());
            if (!element) continue;

            std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
            if (type) {
                if (element->isSpread) {
                    // For now, spread of an object is just Any or the same object type (simplified)
                    elementType = type; 
                } else {
                    if (type->kind == TypeKind::Class) {
                        auto classType = std::static_pointer_cast<ClassType>(type);
                        std::string fieldName;
                        if (!element->propertyName.empty()) {
                            fieldName = element->propertyName;
                        } else if (auto nameId = dynamic_cast<Identifier*>(element->name.get())) {
                            fieldName = nameId->name;
                        }
                        
                        if (!fieldName.empty()) {
                            auto current = classType;
                            while (current) {
                                if (current->fields.count(fieldName)) {
                                    elementType = current->fields[fieldName];
                                    break;
                                }
                                current = current->baseClass;
                            }
                        }
                    } else if (type->kind == TypeKind::Object) {
                        auto objType = std::static_pointer_cast<ObjectType>(type);
                        std::string fieldName;
                        if (!element->propertyName.empty()) {
                            fieldName = element->propertyName;
                        } else if (auto nameId = dynamic_cast<Identifier*>(element->name.get())) {
                            fieldName = nameId->name;
                        }
                        if (!fieldName.empty() && objType->fields.count(fieldName)) {
                            elementType = objType->fields[fieldName];
                        }
                    }
                }
            }
            declareBindingPattern(element->name.get(), elementType);
        }
    } else if (auto abp = dynamic_cast<ArrayBindingPattern*>(pattern)) {
        std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
        if (type && type->kind == TypeKind::Array) {
            elementType = std::static_pointer_cast<ArrayType>(type)->elementType;
        }
        for (auto& elementNode : abp->elements) {
            if (auto oe = dynamic_cast<OmittedExpression*>(elementNode.get())) continue;
            if (auto element = dynamic_cast<BindingElement*>(elementNode.get())) {
                if (element->isSpread) {
                    declareBindingPattern(element->name.get(), type); // Spread of array is array
                } else {
                    declareBindingPattern(element->name.get(), elementType);
                }
            }
        }
    }
}

void Analyzer::visitObjectBindingPattern(ast::ObjectBindingPattern* node) {}
void Analyzer::visitArrayBindingPattern(ast::ArrayBindingPattern* node) {}
void Analyzer::visitBindingElement(ast::BindingElement* node) {}
void Analyzer::visitSpreadElement(ast::SpreadElement* node) {
    visit(node->expression.get());
}
void Analyzer::visitOmittedExpression(ast::OmittedExpression* node) {
    lastType = std::make_shared<Type>(TypeKind::Any);
}

std::shared_ptr<Type> Analyzer::resolveType(const std::string& typeName) {
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

    return parseType(typeName, symbols);
}

std::shared_ptr<Type> Analyzer::parseType(const std::string& typeName, SymbolTable& symbols) {
    if (typeName.empty()) return std::make_shared<Type>(TypeKind::Any);

    if (typeName.ends_with("[]")) {
        auto baseName = typeName.substr(0, typeName.size() - 2);
        auto baseType = parseType(baseName, symbols);
        return std::make_shared<ArrayType>(baseType);
    }

    if (typeName == "number") return std::make_shared<Type>(TypeKind::Double);
    if (typeName == "string") return std::make_shared<Type>(TypeKind::String);
    if (typeName == "boolean") return std::make_shared<Type>(TypeKind::Boolean);
    if (typeName == "void") return std::make_shared<Type>(TypeKind::Void);
    if (typeName == "any") return std::make_shared<Type>(TypeKind::Any);
    
    auto type = symbols.lookupType(typeName);
    if (type) return type;
    
    return std::make_shared<Type>(TypeKind::Any);
}

std::shared_ptr<FunctionType> Analyzer::resolveOverload(const std::vector<std::shared_ptr<FunctionType>>& overloads, const std::vector<std::shared_ptr<Type>>& argTypes) {
    if (overloads.empty()) return nullptr;
    
    for (const auto& overload : overloads) {
        if (overload->paramTypes.size() != argTypes.size()) continue;
        
        bool match = true;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (!argTypes[i]->isAssignableTo(overload->paramTypes[i])) {
                match = false;
                break;
            }
        }
        if (match) return overload;
    }
    
    return overloads[0]; // Fallback
}

void Analyzer::reportError(const std::string& message) {
    fmt::print(stderr, "Error: {}\n", message);
    errorCount++;
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

    // For classes and interfaces, we might need to substitute their type arguments if they are generic
    // But for now, let's keep it simple.
    return type;
}

std::shared_ptr<Module> Analyzer::loadModule(const std::string& specifier) {
    std::string resolvedPath = resolveModulePath(specifier);
    if (resolvedPath.empty()) {
        reportError("Could not resolve module: " + specifier);
        return nullptr;
    }

    if (modules.count(resolvedPath)) {
        return modules[resolvedPath];
    }

    fmt::print("Loading module: {} from {}\n", specifier, resolvedPath);
    auto module = std::make_shared<Module>();
    module->path = resolvedPath;
    modules[resolvedPath] = module;

    try {
        if (resolvedPath.ends_with(".ts")) {
            std::string jsonPath = resolvedPath + ".json";
            std::string command = "node scripts/dump_ast.js \"" + resolvedPath + "\" \"" + jsonPath + "\"";
            if (system(command.c_str()) != 0) {
                reportError("Failed to run dump_ast.js for " + resolvedPath);
                return nullptr;
            }
            module->ast = std::shared_ptr<ast::Program>(ast::loadAst(jsonPath).release());
        } else {
            module->ast = std::shared_ptr<ast::Program>(ast::loadAst(resolvedPath).release());
        }
        analyzeModule(module);
    } catch (const std::exception& e) {
        reportError("Failed to load module " + resolvedPath + ": " + e.what());
        return nullptr;
    }

    return module;
}

void Analyzer::visitImportDeclaration(ast::ImportDeclaration* node) {
    auto module = loadModule(node->moduleSpecifier);
    if (!module) return;

    // Import symbols
    if (!node->defaultImport.empty()) {
        // TODO: Support default exports
    }

    if (!node->namespaceImport.empty()) {
        auto nsType = std::make_shared<NamespaceType>(module);
        symbols.define(node->namespaceImport, nsType);
    }

    for (const auto& spec : node->namedImports) {
        std::string name = spec.propertyName.empty() ? spec.name : spec.propertyName;
        auto sym = module->exports->lookup(name);
        if (sym) {
            fmt::print("Importing symbol {} as {} from {}\n", name, spec.name, node->moduleSpecifier);
            symbols.define(spec.name, sym->type);
        } else {
            auto type = module->exports->lookupType(name);
            if (type) {
                fmt::print("Importing type {} as {} from {}\n", name, spec.name, node->moduleSpecifier);
                symbols.defineType(spec.name, type);
            } else {
                reportError(fmt::format("Module {} does not export {}", node->moduleSpecifier, name));
            }
        }
    }
}

void Analyzer::visitExportDeclaration(ast::ExportDeclaration* node) {
    if (!node->moduleSpecifier.empty()) {
        auto module = loadModule(node->moduleSpecifier);
        if (!module) return;

        if (node->isStarExport) {
            // Re-export all from module
            for (auto& [name, sym] : module->exports->getGlobalSymbols()) {
                currentModule->exports->define(name, sym->type);
            }
            for (auto& [name, type] : module->exports->getGlobalTypes()) {
                currentModule->exports->defineType(name, type);
            }
            return;
        }

        for (const auto& spec : node->namedExports) {
            std::string name = spec.propertyName.empty() ? spec.name : spec.propertyName;
            auto sym = module->exports->lookup(name);
            if (sym) {
                fmt::print("Re-exporting symbol {} from {} in {}\n", name, node->moduleSpecifier, currentModule->path);
                currentModule->exports->define(spec.name, sym->type);
            } else {
                auto type = module->exports->lookupType(name);
                if (type) {
                    currentModule->exports->defineType(spec.name, type);
                } else {
                    reportError(fmt::format("Module {} does not export {}", node->moduleSpecifier, name));
                }
            }
        }
        return;
    }

    for (const auto& spec : node->namedExports) {
        std::string name = spec.propertyName.empty() ? spec.name : spec.propertyName;
        auto sym = symbols.lookup(name);
        if (sym) {
            currentModule->exports->define(spec.name, sym->type);
        } else {
            auto type = symbols.lookupType(name);
            if (type) {
                currentModule->exports->defineType(spec.name, type);
            } else {
                reportError(fmt::format("Symbol {} not found for export", name));
            }
        }
    }
}

} // namespace ts
