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
    stringifyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // replacer
    stringifyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // space
    stringifyType->returnType = std::make_shared<Type>(TypeKind::String);
    jsonType->fields["stringify"] = stringifyType;
    
    symbols.define("JSON", jsonType);

    // Register undefined
    symbols.define("undefined", std::make_shared<Type>(TypeKind::Undefined));
    symbols.define("null", std::make_shared<Type>(TypeKind::Null));

    // Register TypedArrays
    auto uint8ArrayClass = std::make_shared<ClassType>("Uint8Array");
    uint8ArrayClass->fields["length"] = std::make_shared<Type>(TypeKind::Int);
    uint8ArrayClass->fields["buffer"] = std::make_shared<Type>(TypeKind::Any);
    symbols.defineType("Uint8Array", uint8ArrayClass);

    auto uint32ArrayClass = std::make_shared<ClassType>("Uint32Array");
    uint32ArrayClass->fields["length"] = std::make_shared<Type>(TypeKind::Int);
    uint32ArrayClass->fields["buffer"] = std::make_shared<Type>(TypeKind::Any);
    symbols.defineType("Uint32Array", uint32ArrayClass);

    auto float64ArrayClass = std::make_shared<ClassType>("Float64Array");
    float64ArrayClass->fields["length"] = std::make_shared<Type>(TypeKind::Int);
    float64ArrayClass->fields["buffer"] = std::make_shared<Type>(TypeKind::Any);
    symbols.defineType("Float64Array", float64ArrayClass);

    auto dataViewClass = std::make_shared<ClassType>("DataView");
    auto getUint32 = std::make_shared<FunctionType>();
    getUint32->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    getUint32->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // littleEndian
    getUint32->returnType = std::make_shared<Type>(TypeKind::Int);
    dataViewClass->methods["getUint32"] = getUint32;

    auto setUint32 = std::make_shared<FunctionType>();
    setUint32->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
    setUint32->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
    setUint32->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // littleEndian
    setUint32->returnType = std::make_shared<Type>(TypeKind::Void);
    dataViewClass->methods["setUint32"] = setUint32;
    symbols.defineType("DataView", dataViewClass);

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

    auto toDateString = std::make_shared<FunctionType>();
    toDateString->returnType = std::make_shared<Type>(TypeKind::String);
    dateClass->methods["toDateString"] = toDateString;
    
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

    regexpClass->fields["lastIndex"] = std::make_shared<Type>(TypeKind::Int);
    regexpClass->fields["source"] = std::make_shared<Type>(TypeKind::String);
    regexpClass->fields["flags"] = std::make_shared<Type>(TypeKind::String);
    regexpClass->fields["global"] = std::make_shared<Type>(TypeKind::Boolean);
    regexpClass->fields["ignoreCase"] = std::make_shared<Type>(TypeKind::Boolean);
    regexpClass->fields["multiline"] = std::make_shared<Type>(TypeKind::Boolean);
    regexpClass->fields["sticky"] = std::make_shared<Type>(TypeKind::Boolean);
    
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
    
    auto catchCallback = std::make_shared<FunctionType>();
    catchCallback->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    catchCallback->returnType = std::make_shared<Type>(TypeKind::Any);
    thenType->paramTypes.push_back(catchCallback);
    
    thenType->returnType = promiseClass; // Returns Promise<any> for now
    promiseClass->methods["then"] = thenType;

    auto catchType = std::make_shared<FunctionType>();
    catchType->paramTypes.push_back(catchCallback);
    catchType->returnType = promiseClass;
    promiseClass->methods["catch"] = catchType;

    auto finallyType = std::make_shared<FunctionType>();
    auto finallyCallback = std::make_shared<FunctionType>();
    finallyType->paramTypes.push_back(finallyCallback);
    finallyType->returnType = promiseClass;
    promiseClass->methods["finally"] = finallyType;

    auto resolveType = std::make_shared<FunctionType>();
    resolveType->paramTypes.push_back(tParam);
    resolveType->returnType = promiseClass;
    promiseClass->staticMethods["resolve"] = resolveType;

    auto rejectStaticType = std::make_shared<FunctionType>();
    rejectStaticType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    rejectStaticType->returnType = promiseClass;
    promiseClass->staticMethods["reject"] = rejectStaticType;

    auto allType = std::make_shared<FunctionType>();
    allType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    allType->returnType = promiseClass;
    promiseClass->staticMethods["all"] = allType;

    auto raceType = std::make_shared<FunctionType>();
    raceType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    raceType->returnType = promiseClass;
    promiseClass->staticMethods["race"] = raceType;

    auto allSettledType = std::make_shared<FunctionType>();
    allSettledType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    allSettledType->returnType = promiseClass;
    promiseClass->staticMethods["allSettled"] = allSettledType;

    auto anyType = std::make_shared<FunctionType>();
    anyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    anyType->returnType = promiseClass;
    promiseClass->staticMethods["any"] = anyType;

    symbols.defineType("Promise", promiseClass);

    auto promiseCtor = std::make_shared<FunctionType>();
    promiseCtor->returnType = promiseClass;
    symbols.define("Promise", promiseCtor);

    // Register fs global
    auto fsType = std::make_shared<ObjectType>();
    auto fsPromisesType = std::make_shared<ObjectType>();
    
    auto statsType = std::make_shared<ObjectType>();
    statsType->fields["size"] = std::make_shared<Type>(TypeKind::Int);
    statsType->fields["mtimeMs"] = std::make_shared<Type>(TypeKind::Double);
    
    auto isFileType = std::make_shared<FunctionType>();
    isFileType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    statsType->fields["isFile"] = isFileType;

    auto isDirectoryType = std::make_shared<FunctionType>();
    isDirectoryType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    statsType->fields["isDirectory"] = isDirectoryType;

    auto readFileType = std::make_shared<FunctionType>();
    readFileType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    readFileType->returnType = promiseClass;
    fsPromisesType->fields["readFile"] = readFileType;

    auto writeFileType = std::make_shared<FunctionType>();
    writeFileType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeFileType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    writeFileType->returnType = promiseClass;
    fsPromisesType->fields["writeFile"] = writeFileType;

    auto mkdirAsyncType = std::make_shared<FunctionType>();
    mkdirAsyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    mkdirAsyncType->returnType = promiseClass;
    fsPromisesType->fields["mkdir"] = mkdirAsyncType;

    auto statAsyncType = std::make_shared<FunctionType>();
    statAsyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    statAsyncType->returnType = promiseClass; // Promise<Stats>
    fsPromisesType->fields["stat"] = statAsyncType;

    auto readdirAsyncType = std::make_shared<FunctionType>();
    readdirAsyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    readdirAsyncType->returnType = promiseClass; // Promise<string[]>
    fsPromisesType->fields["readdir"] = readdirAsyncType;

    fsType->fields["promises"] = fsPromisesType;

    auto existsSyncType = std::make_shared<FunctionType>();
    existsSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    existsSyncType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    fsType->fields["existsSync"] = existsSyncType;

    auto mkdirSyncType = std::make_shared<FunctionType>();
    mkdirSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    mkdirSyncType->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["mkdirSync"] = mkdirSyncType;

    auto rmdirSyncType = std::make_shared<FunctionType>();
    rmdirSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    rmdirSyncType->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["rmdirSync"] = rmdirSyncType;

    auto unlinkSyncType = std::make_shared<FunctionType>();
    unlinkSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    unlinkSyncType->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["unlinkSync"] = unlinkSyncType;

    auto statSyncType = std::make_shared<FunctionType>();
    statSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    statSyncType->returnType = statsType;
    fsType->fields["statSync"] = statSyncType;

    auto readdirSyncType = std::make_shared<FunctionType>();
    readdirSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    readdirSyncType->returnType = stringArray;
    fsType->fields["readdirSync"] = readdirSyncType;

    symbols.define("fs", fsType);

    // Register path global
    auto pathType = std::make_shared<ObjectType>();
    auto joinType = std::make_shared<FunctionType>();
    joinType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    joinType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    joinType->returnType = std::make_shared<Type>(TypeKind::String);
    pathType->fields["join"] = joinType;
    symbols.define("path", pathType);

    // Register process global
    auto processType = std::make_shared<ObjectType>();
    processType->fields["argv"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    
    auto envType = std::make_shared<ObjectType>(); // Map-like object
    processType->fields["env"] = envType;

    auto exitType = std::make_shared<FunctionType>();
    exitType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    exitType->returnType = std::make_shared<Type>(TypeKind::Void);
    processType->fields["exit"] = exitType;

    auto cwdType = std::make_shared<FunctionType>();
    cwdType->returnType = std::make_shared<Type>(TypeKind::String);
    processType->fields["cwd"] = cwdType;

    symbols.define("process", processType);

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

    auto randomType = std::make_shared<FunctionType>();
    randomType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["random"] = randomType;
    
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

    auto log1pType = std::make_shared<FunctionType>();
    log1pType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    log1pType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["log1p"] = log1pType;

    auto expm1Type = std::make_shared<FunctionType>();
    expm1Type->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    expm1Type->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["expm1"] = expm1Type;

    auto coshType = std::make_shared<FunctionType>();
    coshType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    coshType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["cosh"] = coshType;

    auto sinhType = std::make_shared<FunctionType>();
    sinhType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    sinhType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["sinh"] = sinhType;

    auto tanhType = std::make_shared<FunctionType>();
    tanhType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    tanhType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["tanh"] = tanhType;

    auto acoshType = std::make_shared<FunctionType>();
    acoshType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    acoshType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["acosh"] = acoshType;

    auto asinhType = std::make_shared<FunctionType>();
    asinhType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    asinhType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["asinh"] = asinhType;

    auto atanhType = std::make_shared<FunctionType>();
    atanhType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    atanhType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["atanh"] = atanhType;

    auto froundType = std::make_shared<FunctionType>();
    froundType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    froundType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["fround"] = froundType;

    auto minType = std::make_shared<FunctionType>();
    minType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    minType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    minType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["min"] = minType;

    auto maxType = std::make_shared<FunctionType>();
    maxType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    maxType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    maxType->returnType = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["max"] = maxType;

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
    mathType->fields["LN10"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["LN2"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["LOG10E"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["LOG2E"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["SQRT1_2"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["SQRT2"] = std::make_shared<Type>(TypeKind::Double);
    
    symbols.define("Math", mathType);

    // Register Buffer class
    auto bufferClass = std::make_shared<ClassType>("Buffer");
    bufferClass->fields["length"] = std::make_shared<Type>(TypeKind::Int);
    
    auto bufferToString = std::make_shared<FunctionType>();
    bufferToString->returnType = std::make_shared<Type>(TypeKind::String);
    bufferClass->methods["toString"] = bufferToString;
    
    symbols.defineType("Buffer", bufferClass);

    auto bufferStatic = std::make_shared<ObjectType>();
    bufferStatic->fields["alloc"] = std::make_shared<FunctionType>();
    std::static_pointer_cast<FunctionType>(bufferStatic->fields["alloc"])->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    std::static_pointer_cast<FunctionType>(bufferStatic->fields["alloc"])->returnType = bufferClass;
    
    bufferStatic->fields["from"] = std::make_shared<FunctionType>();
    std::static_pointer_cast<FunctionType>(bufferStatic->fields["from"])->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    std::static_pointer_cast<FunctionType>(bufferStatic->fields["from"])->returnType = bufferClass;
    
    symbols.define("Buffer", bufferStatic);

    // Register URL class
    auto urlClass = std::make_shared<ClassType>("URL");
    urlClass->fields["href"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["protocol"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["host"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["hostname"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["port"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["pathname"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["search"] = std::make_shared<Type>(TypeKind::String);
    urlClass->fields["hash"] = std::make_shared<Type>(TypeKind::String);
    symbols.defineType("URL", urlClass);

    // fetch
    auto promiseClassRef = std::static_pointer_cast<ClassType>(symbols.lookupType("Promise"));
    auto responseClass = std::make_shared<ClassType>("Response");
    
    auto promiseResponse = std::make_shared<ClassType>("Promise");
    promiseResponse->typeArguments.push_back(responseClass);

    auto fetchType = std::make_shared<FunctionType>();
    fetchType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    fetchType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // options
    fetchType->returnType = promiseResponse;
    symbols.define("fetch", fetchType);

    // Response
    responseClass->fields["status"] = std::make_shared<Type>(TypeKind::Int);
    responseClass->fields["statusText"] = std::make_shared<Type>(TypeKind::String);
    responseClass->fields["ok"] = std::make_shared<Type>(TypeKind::Boolean);
    
    auto promiseString = std::make_shared<ClassType>("Promise");
    promiseString->typeArguments.push_back(std::make_shared<Type>(TypeKind::String));

    auto textMethod = std::make_shared<FunctionType>();
    textMethod->returnType = promiseString;
    responseClass->methods["text"] = textMethod;
    
    auto promiseAny = std::make_shared<ClassType>("Promise");
    promiseAny->typeArguments.push_back(std::make_shared<Type>(TypeKind::Any));

    auto jsonMethod = std::make_shared<FunctionType>();
    jsonMethod->returnType = promiseAny;
    responseClass->methods["json"] = jsonMethod;
    
    symbols.defineType("Response", responseClass);

    // Register http module
    auto httpType = std::make_shared<ObjectType>();
    
    auto incomingMessageClass = std::make_shared<ClassType>("IncomingMessage");
    incomingMessageClass->fields["method"] = std::make_shared<Type>(TypeKind::String);
    incomingMessageClass->fields["url"] = std::make_shared<Type>(TypeKind::String);
    incomingMessageClass->fields["headers"] = std::make_shared<Type>(TypeKind::Any);
    symbols.defineType("IncomingMessage", incomingMessageClass);

    auto serverResponseClass = std::make_shared<ClassType>("ServerResponse");
    
    auto writeHeadMethod = std::make_shared<FunctionType>();
    writeHeadMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    writeHeadMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // headers
    serverResponseClass->methods["writeHead"] = writeHeadMethod;
    
    auto writeMethod = std::make_shared<FunctionType>();
    writeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    serverResponseClass->methods["write"] = writeMethod;
    
    auto endMethod = std::make_shared<FunctionType>();
    endMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    serverResponseClass->methods["end"] = endMethod;
    symbols.defineType("ServerResponse", serverResponseClass);

    auto serverClass = std::make_shared<ClassType>("Server");
    auto listenMethod = std::make_shared<FunctionType>();
    listenMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    listenMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // callback
    serverClass->methods["listen"] = listenMethod;
    symbols.defineType("Server", serverClass);

    auto createServerType = std::make_shared<FunctionType>();
    auto requestCallback = std::make_shared<FunctionType>();
    requestCallback->paramTypes.push_back(incomingMessageClass);
    requestCallback->paramTypes.push_back(serverResponseClass);
    createServerType->paramTypes.push_back(requestCallback);
    createServerType->returnType = serverClass;
    httpType->fields["createServer"] = createServerType;
    
    symbols.define("http", httpType);
}
} // namespace ts
