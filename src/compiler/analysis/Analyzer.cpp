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

    // Register undefined
    symbols.define("undefined", std::make_shared<Type>(TypeKind::Undefined));
    symbols.define("null", std::make_shared<Type>(TypeKind::Null));

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

    auto getDay = std::make_shared<FunctionType>();
    getDay->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getDay"] = getDay;

    auto getHours = std::make_shared<FunctionType>();
    getHours->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getHours"] = getHours;

    auto getMinutes = std::make_shared<FunctionType>();
    getMinutes->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getMinutes"] = getMinutes;

    auto getSeconds = std::make_shared<FunctionType>();
    getSeconds->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getSeconds"] = getSeconds;

    auto getMilliseconds = std::make_shared<FunctionType>();
    getMilliseconds->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getMilliseconds"] = getMilliseconds;

    // UTC Getters
    auto getUTCFullYear = std::make_shared<FunctionType>();
    getUTCFullYear->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getUTCFullYear"] = getUTCFullYear;

    auto getUTCMonth = std::make_shared<FunctionType>();
    getUTCMonth->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getUTCMonth"] = getUTCMonth;

    auto getUTCDate = std::make_shared<FunctionType>();
    getUTCDate->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getUTCDate"] = getUTCDate;

    auto getUTCDay = std::make_shared<FunctionType>();
    getUTCDay->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getUTCDay"] = getUTCDay;

    auto getUTCHours = std::make_shared<FunctionType>();
    getUTCHours->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getUTCHours"] = getUTCHours;

    auto getUTCMinutes = std::make_shared<FunctionType>();
    getUTCMinutes->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getUTCMinutes"] = getUTCMinutes;

    auto getUTCSeconds = std::make_shared<FunctionType>();
    getUTCSeconds->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getUTCSeconds"] = getUTCSeconds;

    auto getUTCMilliseconds = std::make_shared<FunctionType>();
    getUTCMilliseconds->returnType = std::make_shared<Type>(TypeKind::Int);
    dateClass->methods["getUTCMilliseconds"] = getUTCMilliseconds;

    // Setters
    auto setTime = std::make_shared<FunctionType>();
    setTime->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setTime->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setTime"] = setTime;

    auto setFullYear = std::make_shared<FunctionType>();
    setFullYear->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setFullYear->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setFullYear"] = setFullYear;

    auto setMonth = std::make_shared<FunctionType>();
    setMonth->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setMonth->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setMonth"] = setMonth;

    auto setDate = std::make_shared<FunctionType>();
    setDate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setDate->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setDate"] = setDate;

    auto setHours = std::make_shared<FunctionType>();
    setHours->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setHours->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setHours"] = setHours;

    auto setMinutes = std::make_shared<FunctionType>();
    setMinutes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setMinutes->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setMinutes"] = setMinutes;

    auto setSeconds = std::make_shared<FunctionType>();
    setSeconds->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setSeconds->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setSeconds"] = setSeconds;

    auto setMilliseconds = std::make_shared<FunctionType>();
    setMilliseconds->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setMilliseconds->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setMilliseconds"] = setMilliseconds;

    // UTC Setters
    auto setUTCFullYear = std::make_shared<FunctionType>();
    setUTCFullYear->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setUTCFullYear->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setUTCFullYear"] = setUTCFullYear;

    auto setUTCMonth = std::make_shared<FunctionType>();
    setUTCMonth->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setUTCMonth->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setUTCMonth"] = setUTCMonth;

    auto setUTCDate = std::make_shared<FunctionType>();
    setUTCDate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setUTCDate->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setUTCDate"] = setUTCDate;

    auto setUTCHours = std::make_shared<FunctionType>();
    setUTCHours->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setUTCHours->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setUTCHours"] = setUTCHours;

    auto setUTCMinutes = std::make_shared<FunctionType>();
    setUTCMinutes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setUTCMinutes->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setUTCMinutes"] = setUTCMinutes;

    auto setUTCSeconds = std::make_shared<FunctionType>();
    setUTCSeconds->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setUTCSeconds->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setUTCSeconds"] = setUTCSeconds;

    auto setUTCMilliseconds = std::make_shared<FunctionType>();
    setUTCMilliseconds->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setUTCMilliseconds->returnType = std::make_shared<Type>(TypeKind::Void);
    dateClass->methods["setUTCMilliseconds"] = setUTCMilliseconds;

    // String conversions
    auto toISOString = std::make_shared<FunctionType>();
    toISOString->returnType = std::make_shared<Type>(TypeKind::String);
    dateClass->methods["toISOString"] = toISOString;

    auto toString = std::make_shared<FunctionType>();
    toString->returnType = std::make_shared<Type>(TypeKind::String);
    dateClass->methods["toString"] = toString;
    
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

    // Register Map class
    auto mapClass = std::make_shared<ClassType>("Map");
    auto mapSet = std::make_shared<FunctionType>();
    mapSet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mapSet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mapSet->returnType = std::make_shared<Type>(TypeKind::Void);
    mapClass->methods["set"] = mapSet;

    auto mapGet = std::make_shared<FunctionType>();
    mapGet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mapGet->returnType = std::make_shared<Type>(TypeKind::Any);
    mapClass->methods["get"] = mapGet;

    auto mapHas = std::make_shared<FunctionType>();
    mapHas->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mapHas->returnType = std::make_shared<Type>(TypeKind::Boolean);
    mapClass->methods["has"] = mapHas;

    symbols.defineType("Map", mapClass);
    
    // Define Map as a value (constructor)
    auto mapCtor = std::make_shared<FunctionType>();
    mapCtor->returnType = std::make_shared<Type>(TypeKind::Map);
    symbols.define("Map", mapCtor);

    // Register Math global
    auto mathType = std::make_shared<ObjectType>();
    
    auto absType = std::make_shared<FunctionType>();
    absType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    absType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["abs"] = absType;
    
    auto ceilType = std::make_shared<FunctionType>();
    ceilType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    ceilType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["ceil"] = ceilType;
    
    auto floorType = std::make_shared<FunctionType>();
    floorType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    floorType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["floor"] = floorType;
    
    auto roundType = std::make_shared<FunctionType>();
    roundType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    roundType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["round"] = roundType;
    
    auto sqrtType = std::make_shared<FunctionType>();
    sqrtType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    sqrtType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["sqrt"] = sqrtType;
    
    auto powType = std::make_shared<FunctionType>();
    powType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    powType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    powType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["pow"] = powType;
    
    auto expType = std::make_shared<FunctionType>();
    expType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    expType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["exp"] = expType;
    
    auto mathLogType = std::make_shared<FunctionType>();
    mathLogType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    mathLogType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["log"] = mathLogType;
    
    auto sinType = std::make_shared<FunctionType>();
    sinType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    sinType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["sin"] = sinType;
    
    auto cosType = std::make_shared<FunctionType>();
    cosType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    cosType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["cos"] = cosType;
    
    auto tanType = std::make_shared<FunctionType>();
    tanType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    tanType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["tan"] = tanType;
    
    auto asinType = std::make_shared<FunctionType>();
    asinType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    asinType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["asin"] = asinType;
    
    auto acosType = std::make_shared<FunctionType>();
    acosType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    acosType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["acos"] = acosType;
    
    auto atanType = std::make_shared<FunctionType>();
    atanType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    atanType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["atan"] = atanType;
    
    auto atan2Type = std::make_shared<FunctionType>();
    atan2Type->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    atan2Type->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    atan2Type->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["atan2"] = atan2Type;
    
    auto hypotType = std::make_shared<FunctionType>();
    hypotType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    hypotType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    hypotType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["hypot"] = hypotType;
    
    auto degToRadType = std::make_shared<FunctionType>();
    degToRadType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    degToRadType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["degToRad"] = degToRadType;
    
    auto radToDegType = std::make_shared<FunctionType>();
    radToDegType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    radToDegType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["radToDeg"] = radToDegType;

    auto log10Type = std::make_shared<FunctionType>();
    log10Type->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    log10Type->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["log10"] = log10Type;

    auto log2Type = std::make_shared<FunctionType>();
    log2Type->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    log2Type->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["log2"] = log2Type;

    auto truncType = std::make_shared<FunctionType>();
    truncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    truncType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["trunc"] = truncType;

    auto signType = std::make_shared<FunctionType>();
    signType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    signType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["sign"] = signType;

    auto cbrtType = std::make_shared<FunctionType>();
    cbrtType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    cbrtType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["cbrt"] = cbrtType;

    auto clz32Type = std::make_shared<FunctionType>();
    clz32Type->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    clz32Type->returnType = std::make_shared<Type>(TypeKind::Int);
    mathType->fields["clz32"] = clz32Type;

    mathType->fields["PI"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["E"] = std::make_shared<Type>(TypeKind::Double);
    
    symbols.define("Math", mathType);

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
    // Pass 1: Hoist declarations to support circular dependencies
    for (auto& stmt : node->body) {
        if (auto func = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            auto funcType = std::make_shared<FunctionType>();
            funcType->node = func;
            symbols.define(func->name, funcType);
            if (func->isExported && currentModule) {
                currentModule->exports->define(func->name, funcType);
            }
            if (func->isDefaultExport && currentModule) {
                currentModule->exports->define("default", funcType);
            }
        } else if (auto cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
            auto classType = std::make_shared<ClassType>(cls->name);
            classType->node = cls;
            symbols.defineType(cls->name, classType);
            if (cls->isExported && currentModule) {
                currentModule->exports->defineType(cls->name, classType);
            }
            if (cls->isDefaultExport && currentModule) {
                currentModule->exports->defineType("default", classType);
            }
        } else if (auto inter = dynamic_cast<ast::InterfaceDeclaration*>(stmt.get())) {
            auto interfaceType = std::make_shared<InterfaceType>(inter->name);
            symbols.defineType(inter->name, interfaceType);
            if (inter->isExported && currentModule) {
                currentModule->exports->defineType(inter->name, interfaceType);
            }
            if (inter->isDefaultExport && currentModule) {
                currentModule->exports->defineType("default", interfaceType);
            }
        } else if (auto alias = dynamic_cast<ast::TypeAliasDeclaration*>(stmt.get())) {
            // We can't fully resolve the type yet because it might depend on other hoisted types,
            // but we can register the name.
            // For now, let's just let the second pass handle it, or do a partial registration.
            // Actually, type aliases are often used in function signatures, so hoisting is good.
            auto type = parseType(alias->type, symbols);
            symbols.defineType(alias->name, type);
            if (alias->isExported && currentModule) {
                currentModule->exports->defineType(alias->name, type);
            }
        } else if (auto enm = dynamic_cast<ast::EnumDeclaration*>(stmt.get())) {
            auto enumType = std::make_shared<EnumType>(enm->name);
            int nextValue = 0;
            for (const auto& member : enm->members) {
                if (member.initializer) {
                    if (auto num = dynamic_cast<ast::NumericLiteral*>(member.initializer.get())) {
                        nextValue = (int)num->value;
                    }
                }
                enumType->members[member.name] = nextValue++;
            }
            symbols.define(enm->name, enumType);
            symbols.defineType(enm->name, enumType);
            if (enm->isExported && currentModule) {
                currentModule->exports->define(enm->name, enumType);
                currentModule->exports->defineType(enm->name, enumType);
            }
        }
    }

    // Pass 2: Full analysis
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

void Analyzer::visit(Node* node) {
    if (!node) return;
    node->accept(this);

    if (auto expr = dynamic_cast<Expression*>(node)) {
        expr->inferredType = lastType;
        if (auto id = dynamic_cast<Identifier*>(expr)) {
        }
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
        auto sym = module->exports->lookup("default");
        if (sym) {
            symbols.define(node->defaultImport, sym->type);
        } else {
            auto type = module->exports->lookupType("default");
            if (type) {
                symbols.defineType(node->defaultImport, type);
            } else {
                reportError(fmt::format("Module {} does not have a default export", node->moduleSpecifier));
            }
        }
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

void Analyzer::visitExportAssignment(ast::ExportAssignment* node) {
    visit(node->expression.get());
    if (currentModule) {
        currentModule->exports->define("default", lastType);
    }
}

void Analyzer::visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) {
    auto type = parseType(node->type, symbols);
    symbols.defineType(node->name, type);
    if (node->isExported && currentModule) {
        currentModule->exports->defineType(node->name, type);
    }
}

void Analyzer::visitEnumDeclaration(ast::EnumDeclaration* node) {
    // Already handled in hoisting pass
}

} // namespace ts
