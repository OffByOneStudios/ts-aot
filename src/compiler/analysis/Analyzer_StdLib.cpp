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

} // namespace ts

