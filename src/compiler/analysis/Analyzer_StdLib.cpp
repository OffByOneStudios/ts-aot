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

    // Register setTimeout/setInterval
    auto setTimeoutType = std::make_shared<FunctionType>();
    setTimeoutType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    setTimeoutType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setTimeoutType->returnType = std::make_shared<Type>(TypeKind::Int);
    symbols.define("setTimeout", setTimeoutType);
    symbols.define("setInterval", setTimeoutType);

    auto clearTimeoutType = std::make_shared<FunctionType>();
    clearTimeoutType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    clearTimeoutType->returnType = std::make_shared<Type>(TypeKind::Void);
    symbols.define("clearTimeout", clearTimeoutType);
    symbols.define("clearInterval", clearTimeoutType);

    auto setImmediateType = std::make_shared<FunctionType>();
    setImmediateType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    setImmediateType->returnType = std::make_shared<Type>(TypeKind::Int);
    symbols.define("setImmediate", setImmediateType);
    symbols.define("clearImmediate", clearTimeoutType);

    // Register require global
    auto requireType = std::make_shared<FunctionType>();
    requireType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    requireType->returnType = std::make_shared<Type>(TypeKind::Any);
    symbols.define("require", requireType);

    // Register ts_module_register (internal)
    auto registerType = std::make_shared<FunctionType>();
    registerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    registerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    registerType->returnType = std::make_shared<Type>(TypeKind::Void);
    symbols.define("ts_module_register", registerType);

    // Register ts_debug_marker (internal)
    auto markerType = std::make_shared<FunctionType>();
    markerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    markerType->returnType = std::make_shared<Type>(TypeKind::Any);
    symbols.define("ts_debug_marker", markerType);

    // Register undefined
    symbols.define("undefined", std::make_shared<Type>(TypeKind::Undefined));
    symbols.define("null", std::make_shared<Type>(TypeKind::Null));

    // Register Symbol global
    auto symbolType = std::make_shared<ObjectType>();
    symbolType->fields["iterator"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["asyncIterator"] = std::make_shared<Type>(TypeKind::Symbol);
    
    auto symbolFor = std::make_shared<FunctionType>();
    symbolFor->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    symbolFor->returnType = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["for"] = symbolFor;

    auto symbolKeyFor = std::make_shared<FunctionType>();
    symbolKeyFor->paramTypes.push_back(std::make_shared<Type>(TypeKind::Symbol));
    symbolKeyFor->returnType = std::make_shared<Type>(TypeKind::String);
    symbolType->fields["keyFor"] = symbolKeyFor;

    symbols.define("Symbol", symbolType);

    // Register BigInt global
    auto bigIntType = std::make_shared<FunctionType>();
    bigIntType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    bigIntType->returnType = std::make_shared<Type>(TypeKind::BigInt);
    symbols.define("BigInt", bigIntType);

    // Register ts_aot intrinsic namespace
    auto tsAotType = std::make_shared<ObjectType>();
    auto comptimeType = std::make_shared<FunctionType>();
    comptimeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    comptimeType->returnType = std::make_shared<Type>(TypeKind::Any);
    tsAotType->fields["comptime"] = comptimeType;
    symbols.define("ts_aot", tsAotType);

    // Register TypedArrays
    auto uint8ArrayClass = std::make_shared<ClassType>("Uint8Array");
    uint8ArrayClass->fields["length"] = std::make_shared<Type>(TypeKind::Int);
    uint8ArrayClass->fields["buffer"] = std::make_shared<Type>(TypeKind::Any);
    symbols.defineType("Uint8Array", uint8ArrayClass);

    // ========================================================================
    // TextEncoder (Milestone 102.11)
    // Note: encode() returns Buffer in ts-aot (not Uint8Array)
    // ========================================================================
    auto textEncoderClass = std::make_shared<ClassType>("TextEncoder");
    textEncoderClass->fields["encoding"] = std::make_shared<Type>(TypeKind::String);
    
    // We register types after registerBuffer(), so we look up Buffer class
    // For now, return Any to avoid struct lookup issues
    auto encodeMethod = std::make_shared<FunctionType>();
    encodeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    encodeMethod->returnType = std::make_shared<Type>(TypeKind::Any); // Returns TsBuffer* at runtime
    textEncoderClass->methods["encode"] = encodeMethod;
    
    auto encodeIntoResult = std::make_shared<ObjectType>();
    encodeIntoResult->fields["read"] = std::make_shared<Type>(TypeKind::Int);
    encodeIntoResult->fields["written"] = std::make_shared<Type>(TypeKind::Int);
    
    auto encodeIntoMethod = std::make_shared<FunctionType>();
    encodeIntoMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    encodeIntoMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    encodeIntoMethod->returnType = encodeIntoResult;
    textEncoderClass->methods["encodeInto"] = encodeIntoMethod;
    
    symbols.defineType("TextEncoder", textEncoderClass);
    
    // TextEncoder constructor - register the class as its own constructor
    symbols.define("TextEncoder", textEncoderClass);
    
    // ========================================================================
    // TextDecoder (Milestone 102.11)
    // ========================================================================
    auto textDecoderClass = std::make_shared<ClassType>("TextDecoder");
    textDecoderClass->fields["encoding"] = std::make_shared<Type>(TypeKind::String);
    textDecoderClass->fields["fatal"] = std::make_shared<Type>(TypeKind::Boolean);
    textDecoderClass->fields["ignoreBOM"] = std::make_shared<Type>(TypeKind::Boolean);
    
    auto decodeMethod = std::make_shared<FunctionType>();
    decodeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // Accepts TsBuffer* at runtime
    decodeMethod->returnType = std::make_shared<Type>(TypeKind::String);
    textDecoderClass->methods["decode"] = decodeMethod;
    
    symbols.defineType("TextDecoder", textDecoderClass);
    
    // TextDecoder constructor - register the class as its own constructor
    symbols.define("TextDecoder", textDecoderClass);

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

    // Register Error class
    auto errorClass = std::make_shared<ClassType>("Error");
    errorClass->fields["message"] = std::make_shared<Type>(TypeKind::String);
    errorClass->fields["stack"] = std::make_shared<Type>(TypeKind::String);
    errorClass->fields["name"] = std::make_shared<Type>(TypeKind::String);
    symbols.defineType("Error", errorClass);

    // Register console global
    auto consoleType = std::make_shared<ObjectType>();
    auto logType = std::make_shared<FunctionType>();
    logType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    logType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["log"] = logType;
    consoleType->fields["error"] = logType;
    consoleType->fields["warn"] = logType;
    consoleType->fields["info"] = logType;
    consoleType->fields["debug"] = logType;

    auto timeType = std::make_shared<FunctionType>();
    timeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    timeType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["time"] = timeType;
    consoleType->fields["timeEnd"] = timeType;

    auto traceType = std::make_shared<FunctionType>();
    traceType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["trace"] = traceType;

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

    auto promiseOfT = std::make_shared<ClassType>("Promise");
    promiseOfT->typeArguments.push_back(tParam);

    auto promiseOfAny = std::make_shared<ClassType>("Promise");
    promiseOfAny->typeArguments.push_back(std::make_shared<Type>(TypeKind::Any));

    auto thenType = std::make_shared<FunctionType>();
    auto thenCallback = std::make_shared<FunctionType>();
    thenCallback->paramTypes.push_back(tParam);
    thenCallback->returnType = std::make_shared<Type>(TypeKind::Any); // Simplified
    thenType->paramTypes.push_back(thenCallback);
    
    auto catchCallback = std::make_shared<FunctionType>();
    catchCallback->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    catchCallback->returnType = std::make_shared<Type>(TypeKind::Any);
    thenType->paramTypes.push_back(catchCallback);
    
    thenType->returnType = promiseOfAny; // Returns Promise<any> for now
    promiseClass->methods["then"] = thenType;

    auto catchType = std::make_shared<FunctionType>();
    catchType->paramTypes.push_back(catchCallback);
    catchType->returnType = promiseOfAny;
    promiseClass->methods["catch"] = catchType;

    auto finallyType = std::make_shared<FunctionType>();
    auto finallyCallback = std::make_shared<FunctionType>();
    finallyType->paramTypes.push_back(finallyCallback);
    finallyType->returnType = promiseOfAny;
    promiseClass->methods["finally"] = finallyType;

    auto resolveType = std::make_shared<FunctionType>();
    resolveType->paramTypes.push_back(tParam);
    resolveType->returnType = promiseOfT;
    promiseClass->staticMethods["resolve"] = resolveType;

    auto rejectStaticType = std::make_shared<FunctionType>();
    rejectStaticType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    rejectStaticType->returnType = promiseOfAny;
    promiseClass->staticMethods["reject"] = rejectStaticType;

    auto allType = std::make_shared<FunctionType>();
    allType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    allType->returnType = promiseOfAny;
    promiseClass->staticMethods["all"] = allType;

    auto raceType = std::make_shared<FunctionType>();
    raceType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    raceType->returnType = promiseOfAny;
    promiseClass->staticMethods["race"] = raceType;

    auto allSettledType = std::make_shared<FunctionType>();
    allSettledType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    allSettledType->returnType = promiseOfAny;
    promiseClass->staticMethods["allSettled"] = allSettledType;

    auto anyType = std::make_shared<FunctionType>();
    anyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    anyType->returnType = promiseOfAny;
    promiseClass->staticMethods["any"] = anyType;

    symbols.defineType("Promise", promiseClass);

    auto promiseCtor = std::make_shared<FunctionType>();
    promiseCtor->returnType = promiseClass;
    symbols.define("Promise", promiseCtor);

    // Register IteratorResult
    auto iteratorResultType = std::make_shared<ObjectType>();
    iteratorResultType->fields["value"] = std::make_shared<Type>(TypeKind::Any);
    iteratorResultType->fields["done"] = std::make_shared<Type>(TypeKind::Boolean);
    symbols.defineType("IteratorResult", iteratorResultType);

    // Register Generator
    auto generatorClass = std::make_shared<ClassType>("Generator");
    auto nextMethod = std::make_shared<FunctionType>();
    nextMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    nextMethod->returnType = iteratorResultType;
    generatorClass->methods["next"] = nextMethod;
    symbols.defineType("Generator", generatorClass);

    // Register AsyncGenerator
    auto asyncGeneratorClass = std::make_shared<ClassType>("AsyncGenerator");
    auto asyncNextMethod = std::make_shared<FunctionType>();
    asyncNextMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    
    auto wrappedResult = std::make_shared<ClassType>("Promise");
    wrappedResult->typeParameters = promiseClass->typeParameters;
    wrappedResult->methods = promiseClass->methods;
    wrappedResult->staticMethods = promiseClass->staticMethods;
    wrappedResult->typeArguments = { iteratorResultType };
    
    asyncNextMethod->returnType = wrappedResult;
    asyncGeneratorClass->methods["next"] = asyncNextMethod;
    symbols.defineType("AsyncGenerator", asyncGeneratorClass);

    // Register Map class
    auto mapClass = std::make_shared<ClassType>("Map");
    auto mapSet = std::make_shared<FunctionType>();
    mapSet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mapSet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mapSet->returnType = mapClass; // Returns this for chaining
    mapClass->methods["set"] = mapSet;

    auto mapGet = std::make_shared<FunctionType>();
    mapGet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mapGet->returnType = std::make_shared<Type>(TypeKind::Any);
    mapClass->methods["get"] = mapGet;

    auto mapHas = std::make_shared<FunctionType>();
    mapHas->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mapHas->returnType = std::make_shared<Type>(TypeKind::Boolean);
    mapClass->methods["has"] = mapHas;

    auto mapDelete = std::make_shared<FunctionType>();
    mapDelete->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mapDelete->returnType = std::make_shared<Type>(TypeKind::Boolean);
    mapClass->methods["delete"] = mapDelete;

    auto mapClear = std::make_shared<FunctionType>();
    mapClear->returnType = std::make_shared<Type>(TypeKind::Void);
    mapClass->methods["clear"] = mapClear;

    mapClass->fields["size"] = std::make_shared<Type>(TypeKind::Int);

    symbols.defineType("Map", mapClass);

    // Register Set class
    auto setClass = std::make_shared<ClassType>("Set");
    
    auto setAdd = std::make_shared<FunctionType>();
    setAdd->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    setAdd->returnType = setClass; // Returns this for chaining
    setClass->methods["add"] = setAdd;

    auto setHas = std::make_shared<FunctionType>();
    setHas->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    setHas->returnType = std::make_shared<Type>(TypeKind::Boolean);
    setClass->methods["has"] = setHas;

    auto setDelete = std::make_shared<FunctionType>();
    setDelete->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    setDelete->returnType = std::make_shared<Type>(TypeKind::Boolean);
    setClass->methods["delete"] = setDelete;

    auto setClear = std::make_shared<FunctionType>();
    setClear->returnType = std::make_shared<Type>(TypeKind::Void);
    setClass->methods["clear"] = setClear;

    setClass->fields["size"] = std::make_shared<Type>(TypeKind::Int);

    symbols.defineType("Set", setClass);
    
    // Define Map as a value (constructor)
    auto mapCtor = std::make_shared<FunctionType>();
    mapCtor->returnType = std::make_shared<Type>(TypeKind::Map);
    symbols.define("Map", mapCtor);

    // Define Set as a value (constructor)
    auto setCtor = std::make_shared<FunctionType>();
    setCtor->returnType = std::make_shared<Type>(TypeKind::SetType);
    symbols.define("Set", setCtor);

    // Register Object global
    auto objectType = std::make_shared<ObjectType>();
    
    // Object.keys(obj) => string[]
    auto stringArrayType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    
    auto keysType = std::make_shared<FunctionType>();
    keysType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    keysType->returnType = stringArrayType;
    objectType->fields["keys"] = keysType;
    
    // Object.values(obj) => any[]
    auto valuesType = std::make_shared<FunctionType>();
    valuesType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    valuesType->returnType = std::make_shared<Type>(TypeKind::Array);
    objectType->fields["values"] = valuesType;
    
    // Object.entries(obj) => [string, any][]
    auto entriesType = std::make_shared<FunctionType>();
    entriesType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    entriesType->returnType = std::make_shared<Type>(TypeKind::Array);
    objectType->fields["entries"] = entriesType;
    
    // Object.assign(target, ...sources) => target
    auto assignType = std::make_shared<FunctionType>();
    assignType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    assignType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    assignType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["assign"] = assignType;
    
    // Object.freeze(obj) => obj
    auto freezeType = std::make_shared<FunctionType>();
    freezeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    freezeType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["freeze"] = freezeType;
    
    // Object.seal(obj) => obj
    auto sealType = std::make_shared<FunctionType>();
    sealType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    sealType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["seal"] = sealType;
    
    // Object.isFrozen(obj) => boolean
    auto isFrozenType = std::make_shared<FunctionType>();
    isFrozenType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    isFrozenType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    objectType->fields["isFrozen"] = isFrozenType;
    
    // Object.isSealed(obj) => boolean
    auto isSealedType = std::make_shared<FunctionType>();
    isSealedType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    isSealedType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    objectType->fields["isSealed"] = isSealedType;
    
    // Object.hasOwn(obj, prop) => boolean
    auto hasOwnType = std::make_shared<FunctionType>();
    hasOwnType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    hasOwnType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    hasOwnType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    objectType->fields["hasOwn"] = hasOwnType;
    
    // Object.fromEntries(iterable) => object
    auto fromEntriesType = std::make_shared<FunctionType>();
    fromEntriesType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array));
    fromEntriesType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["fromEntries"] = fromEntriesType;
    
    symbols.define("Object", objectType);

    // Register Array global (for static methods like Array.isArray)
    auto arrayGlobalType = std::make_shared<ObjectType>();
    
    // Array.isArray(value) => boolean
    auto isArrayType = std::make_shared<FunctionType>();
    isArrayType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    isArrayType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    arrayGlobalType->fields["isArray"] = isArrayType;
    
    // Array.from(iterable) => any[] - basic support
    auto arrayFromType = std::make_shared<FunctionType>();
    arrayFromType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    arrayFromType->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
    arrayGlobalType->fields["from"] = arrayFromType;
    
    symbols.define("Array", arrayGlobalType);

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

    // Register URL class

    registerEvents();
    registerStreams();
    registerProcess();
    registerBuffer();
    registerFS();
    registerPath();
    registerNet();
    registerHTTP();
    registerHTTPS();
    registerUtil();
}
} // namespace ts
