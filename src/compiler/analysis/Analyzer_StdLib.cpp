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

    // Register timers module (re-exports of global timer functions)
    auto timersModule = std::make_shared<ObjectType>();
    timersModule->fields["setTimeout"] = setTimeoutType;
    timersModule->fields["setInterval"] = setTimeoutType;
    timersModule->fields["clearTimeout"] = clearTimeoutType;
    timersModule->fields["clearInterval"] = clearTimeoutType;
    timersModule->fields["setImmediate"] = setImmediateType;
    timersModule->fields["clearImmediate"] = clearTimeoutType;
    symbols.define("timers", timersModule);

    // Register timers/promises module (Promise-based timer functions)
    auto promiseType = std::make_shared<ClassType>("Promise");

    // timers/promises.setTimeout(delay: number, value?: T) => Promise<T>
    auto promiseSetTimeoutType = std::make_shared<FunctionType>();
    promiseSetTimeoutType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // delay
    promiseSetTimeoutType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // value (optional)
    promiseSetTimeoutType->returnType = promiseType;

    // timers/promises.setImmediate(value?: T) => Promise<T>
    auto promiseSetImmediateType = std::make_shared<FunctionType>();
    promiseSetImmediateType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // value (optional)
    promiseSetImmediateType->returnType = promiseType;

    // timers/promises.setInterval(delay: number, value?: T) => AsyncIterable<T>
    // Returns an async iterable object that can be used with for await...of
    auto asyncIterableType = std::make_shared<ClassType>("AsyncIterable");
    auto promiseSetIntervalType = std::make_shared<FunctionType>();
    promiseSetIntervalType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // delay
    promiseSetIntervalType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // value (optional)
    promiseSetIntervalType->returnType = asyncIterableType;

    // scheduler.wait(delay: number, options?: object) => Promise<void>
    auto schedulerWaitType = std::make_shared<FunctionType>();
    schedulerWaitType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // delay
    schedulerWaitType->returnType = promiseType;

    // scheduler.yield() => Promise<void>
    auto schedulerYieldType = std::make_shared<FunctionType>();
    schedulerYieldType->returnType = promiseType;

    // Create scheduler sub-object
    auto schedulerObject = std::make_shared<ObjectType>();
    schedulerObject->fields["wait"] = schedulerWaitType;
    schedulerObject->fields["yield"] = schedulerYieldType;

    auto timersPromisesModule = std::make_shared<ObjectType>();
    timersPromisesModule->fields["setTimeout"] = promiseSetTimeoutType;
    timersPromisesModule->fields["setImmediate"] = promiseSetImmediateType;
    timersPromisesModule->fields["setInterval"] = promiseSetIntervalType;
    timersPromisesModule->fields["scheduler"] = schedulerObject;
    symbols.define("timers/promises", timersPromisesModule);

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

    // Register global Infinity and NaN
    symbols.define("Infinity", std::make_shared<Type>(TypeKind::Double));
    symbols.define("NaN", std::make_shared<Type>(TypeKind::Double));

    // Register Symbol global with all well-known symbols
    auto symbolType = std::make_shared<ObjectType>();

    // Well-known symbols (ES2015)
    symbolType->fields["iterator"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["asyncIterator"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["hasInstance"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["isConcatSpreadable"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["match"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["matchAll"] = std::make_shared<Type>(TypeKind::Symbol);  // ES2020
    symbolType->fields["replace"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["search"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["split"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["species"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["toPrimitive"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["toStringTag"] = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["unscopables"] = std::make_shared<Type>(TypeKind::Symbol);

    // Symbol.for() and Symbol.keyFor() methods
    auto symbolFor = std::make_shared<FunctionType>();
    symbolFor->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    symbolFor->returnType = std::make_shared<Type>(TypeKind::Symbol);
    symbolType->fields["for"] = symbolFor;

    auto symbolKeyFor = std::make_shared<FunctionType>();
    symbolKeyFor->paramTypes.push_back(std::make_shared<Type>(TypeKind::Symbol));
    symbolKeyFor->returnType = std::make_shared<Type>(TypeKind::Any);  // string | undefined
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

    // Register TypedArrays - helper lambda to create TypedArray class types
    auto makeTypedArrayClass = [this](const std::string& name) {
        auto cls = std::make_shared<ClassType>(name);
        cls->fields["length"] = std::make_shared<Type>(TypeKind::Int);
        cls->fields["byteLength"] = std::make_shared<Type>(TypeKind::Int);
        cls->fields["byteOffset"] = std::make_shared<Type>(TypeKind::Int);
        cls->fields["buffer"] = std::make_shared<Type>(TypeKind::Any);
        cls->fields["BYTES_PER_ELEMENT"] = std::make_shared<Type>(TypeKind::Int);

        // Note: Instance methods (set, slice, subarray, fill) not yet implemented in runtime
        // Will be added in a follow-up iteration

        // Register both as type and constructor
        symbols.defineType(name, cls);
        symbols.define(name, cls);

        return cls;
    };

    // Register all TypedArray types
    auto int8ArrayClass = makeTypedArrayClass("Int8Array");
    auto uint8ArrayClass = makeTypedArrayClass("Uint8Array");
    auto uint8ClampedArrayClass = makeTypedArrayClass("Uint8ClampedArray");
    auto int16ArrayClass = makeTypedArrayClass("Int16Array");
    auto uint16ArrayClass = makeTypedArrayClass("Uint16Array");
    auto int32ArrayClass = makeTypedArrayClass("Int32Array");
    auto uint32ArrayClass = makeTypedArrayClass("Uint32Array");
    auto float32ArrayClass = makeTypedArrayClass("Float32Array");
    auto float64ArrayClass = makeTypedArrayClass("Float64Array");
    auto bigInt64ArrayClass = makeTypedArrayClass("BigInt64Array");
    auto bigUint64ArrayClass = makeTypedArrayClass("BigUint64Array");

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

    // Note: Uint32Array and Float64Array are now registered in the TypedArray section above

    auto dataViewClass = std::make_shared<ClassType>("DataView");

    // Helper to create getter method (offset, littleEndian?) -> number
    auto makeGetter = [&]() {
        auto fn = std::make_shared<FunctionType>();
        fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
        fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // littleEndian (optional)
        fn->returnType = std::make_shared<Type>(TypeKind::Int);
        return fn;
    };

    // Helper to create float getter (returns Double)
    auto makeFloatGetter = [&]() {
        auto fn = std::make_shared<FunctionType>();
        fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
        fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // littleEndian (optional)
        fn->returnType = std::make_shared<Type>(TypeKind::Double);
        return fn;
    };

    // Helper to create setter method (offset, value, littleEndian?) -> void
    auto makeSetter = [&]() {
        auto fn = std::make_shared<FunctionType>();
        fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
        fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // value
        fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // littleEndian (optional)
        fn->returnType = std::make_shared<Type>(TypeKind::Void);
        return fn;
    };

    // Helper to create float setter
    auto makeFloatSetter = [&]() {
        auto fn = std::make_shared<FunctionType>();
        fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // offset
        fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // value
        fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Boolean)); // littleEndian (optional)
        fn->returnType = std::make_shared<Type>(TypeKind::Void);
        return fn;
    };

    // Getters
    dataViewClass->methods["getInt8"] = makeGetter();
    dataViewClass->methods["getUint8"] = makeGetter();
    dataViewClass->methods["getInt16"] = makeGetter();
    dataViewClass->methods["getUint16"] = makeGetter();
    dataViewClass->methods["getInt32"] = makeGetter();
    dataViewClass->methods["getUint32"] = makeGetter();
    dataViewClass->methods["getFloat32"] = makeFloatGetter();
    dataViewClass->methods["getFloat64"] = makeFloatGetter();

    // Setters
    dataViewClass->methods["setInt8"] = makeSetter();
    dataViewClass->methods["setUint8"] = makeSetter();
    dataViewClass->methods["setInt16"] = makeSetter();
    dataViewClass->methods["setUint16"] = makeSetter();
    dataViewClass->methods["setInt32"] = makeSetter();
    dataViewClass->methods["setUint32"] = makeSetter();
    dataViewClass->methods["setFloat32"] = makeFloatSetter();
    dataViewClass->methods["setFloat64"] = makeFloatSetter();

    // Properties
    dataViewClass->fields["buffer"] = std::make_shared<ClassType>("ArrayBuffer");
    dataViewClass->fields["byteLength"] = std::make_shared<Type>(TypeKind::Int);
    dataViewClass->fields["byteOffset"] = std::make_shared<Type>(TypeKind::Int);

    symbols.defineType("DataView", dataViewClass);
    symbols.define("DataView", dataViewClass);  // Constructor

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

    auto toJSON = std::make_shared<FunctionType>();
    toJSON->returnType = std::make_shared<Type>(TypeKind::String);
    dateClass->methods["toJSON"] = toJSON;

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

    // Register RegExpMatchArray - result type of RegExp.exec()
    auto regexpMatchArrayClass = std::make_shared<ClassType>("RegExpMatchArray");
    regexpMatchArrayClass->fields["index"] = std::make_shared<Type>(TypeKind::Int);
    regexpMatchArrayClass->fields["input"] = std::make_shared<Type>(TypeKind::String);
    regexpMatchArrayClass->fields["length"] = std::make_shared<Type>(TypeKind::Int);
    // indices is an array of [start, end] tuples (or undefined for non-participating groups)
    auto indicesElementType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Int));
    regexpMatchArrayClass->fields["indices"] = std::make_shared<ArrayType>(indicesElementType);
    symbols.defineType("RegExpMatchArray", regexpMatchArrayClass);

    // Register RegExp class
    auto regexpClass = std::make_shared<ClassType>("RegExp");

    auto testType = std::make_shared<FunctionType>();
    testType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    testType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    regexpClass->methods["test"] = testType;

    auto execType = std::make_shared<FunctionType>();
    execType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    execType->returnType = regexpMatchArrayClass;  // Returns RegExpMatchArray (or null)
    regexpClass->methods["exec"] = execType;

    regexpClass->fields["lastIndex"] = std::make_shared<Type>(TypeKind::Int);
    regexpClass->fields["source"] = std::make_shared<Type>(TypeKind::String);
    regexpClass->fields["flags"] = std::make_shared<Type>(TypeKind::String);
    regexpClass->fields["global"] = std::make_shared<Type>(TypeKind::Boolean);
    regexpClass->fields["ignoreCase"] = std::make_shared<Type>(TypeKind::Boolean);
    regexpClass->fields["multiline"] = std::make_shared<Type>(TypeKind::Boolean);
    regexpClass->fields["sticky"] = std::make_shared<Type>(TypeKind::Boolean);
    regexpClass->fields["hasIndices"] = std::make_shared<Type>(TypeKind::Boolean);  // ES2022 d flag

    symbols.defineType("RegExp", regexpClass);

    // Register Error class
    auto errorClass = std::make_shared<ClassType>("Error");
    errorClass->fields["message"] = std::make_shared<Type>(TypeKind::String);
    errorClass->fields["stack"] = std::make_shared<Type>(TypeKind::String);
    errorClass->fields["name"] = std::make_shared<Type>(TypeKind::String);
    errorClass->fields["cause"] = std::make_shared<Type>(TypeKind::Any);  // ES2022
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
    consoleType->fields["timeLog"] = timeType;

    auto traceType = std::make_shared<FunctionType>();
    traceType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["trace"] = traceType;

    // console.dir(obj) - inspect an object
    auto dirType = std::make_shared<FunctionType>();
    dirType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    dirType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["dir"] = dirType;

    // console.count(label?) and console.countReset(label?)
    auto countType = std::make_shared<FunctionType>();
    countType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    countType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["count"] = countType;
    consoleType->fields["countReset"] = countType;

    // console.group(label?) and console.groupCollapsed(label?)
    auto groupType = std::make_shared<FunctionType>();
    groupType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    groupType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["group"] = groupType;
    consoleType->fields["groupCollapsed"] = groupType;

    // console.groupEnd()
    auto groupEndType = std::make_shared<FunctionType>();
    groupEndType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["groupEnd"] = groupEndType;

    // console.clear()
    auto clearType = std::make_shared<FunctionType>();
    clearType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["clear"] = clearType;

    // console.table(tabularData, properties?)
    auto tableType = std::make_shared<FunctionType>();
    tableType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    tableType->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));
    tableType->returnType = std::make_shared<Type>(TypeKind::Void);
    consoleType->fields["table"] = tableType;

    // console.dirxml(value) - alias for console.dir in Node.js
    consoleType->fields["dirxml"] = dirType;

    symbols.define("console", consoleType);

    // Register global object (contains all global variables and functions)
    auto globalType = std::make_shared<Type>(TypeKind::Any);
    symbols.define("global", globalType);
    symbols.define("globalThis", globalType);  // ES2020: globalThis is an alias for global

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

    // ES2024 Promise.withResolvers() - returns { promise, resolve, reject }
    auto withResolversResultType = std::make_shared<ObjectType>();
    withResolversResultType->fields["promise"] = promiseOfAny;
    withResolversResultType->fields["resolve"] = std::make_shared<FunctionType>();
    withResolversResultType->fields["reject"] = std::make_shared<FunctionType>();

    auto withResolversType = std::make_shared<FunctionType>();
    withResolversType->returnType = withResolversResultType;
    promiseClass->staticMethods["withResolvers"] = withResolversType;

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

    // Register WeakMap class (keys must be objects, no iteration/size)
    auto weakMapClass = std::make_shared<ClassType>("WeakMap");

    auto weakMapSet = std::make_shared<FunctionType>();
    weakMapSet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));  // key must be object
    weakMapSet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    weakMapSet->returnType = weakMapClass;
    weakMapClass->methods["set"] = weakMapSet;

    auto weakMapGet = std::make_shared<FunctionType>();
    weakMapGet->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    weakMapGet->returnType = std::make_shared<Type>(TypeKind::Any);
    weakMapClass->methods["get"] = weakMapGet;

    auto weakMapHas = std::make_shared<FunctionType>();
    weakMapHas->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    weakMapHas->returnType = std::make_shared<Type>(TypeKind::Boolean);
    weakMapClass->methods["has"] = weakMapHas;

    auto weakMapDelete = std::make_shared<FunctionType>();
    weakMapDelete->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    weakMapDelete->returnType = std::make_shared<Type>(TypeKind::Boolean);
    weakMapClass->methods["delete"] = weakMapDelete;

    symbols.defineType("WeakMap", weakMapClass);

    // Register WeakSet class (values must be objects, no iteration/size)
    auto weakSetClass = std::make_shared<ClassType>("WeakSet");

    auto weakSetAdd = std::make_shared<FunctionType>();
    weakSetAdd->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));  // value must be object
    weakSetAdd->returnType = weakSetClass;
    weakSetClass->methods["add"] = weakSetAdd;

    auto weakSetHas = std::make_shared<FunctionType>();
    weakSetHas->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    weakSetHas->returnType = std::make_shared<Type>(TypeKind::Boolean);
    weakSetClass->methods["has"] = weakSetHas;

    auto weakSetDelete = std::make_shared<FunctionType>();
    weakSetDelete->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    weakSetDelete->returnType = std::make_shared<Type>(TypeKind::Boolean);
    weakSetClass->methods["delete"] = weakSetDelete;

    symbols.defineType("WeakSet", weakSetClass);

    // Define Map as a value (constructor + static methods)
    auto mapCtorType = std::make_shared<ObjectType>();

    // ES2024 Map.groupBy(iterable, callbackFn) => Map
    auto mapGroupByType = std::make_shared<FunctionType>();
    mapGroupByType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array));  // iterable
    mapGroupByType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // callbackFn
    mapGroupByType->returnType = std::make_shared<MapType>();  // Return MapType (TypeKind::Map)
    mapCtorType->fields["groupBy"] = mapGroupByType;

    symbols.define("Map", mapCtorType);

    // Define Set as a value (constructor)
    auto setCtor = std::make_shared<FunctionType>();
    setCtor->returnType = std::make_shared<Type>(TypeKind::SetType);
    symbols.define("Set", setCtor);

    // Define WeakMap as a value (constructor)
    auto weakMapCtor = std::make_shared<FunctionType>();
    weakMapCtor->returnType = weakMapClass;
    symbols.define("WeakMap", weakMapCtor);

    // Define WeakSet as a value (constructor)
    auto weakSetCtor = std::make_shared<FunctionType>();
    weakSetCtor->returnType = weakSetClass;
    symbols.define("WeakSet", weakSetCtor);

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
    assignType->hasRest = true;  // Accept variadic sources
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

    // Object.is(value1, value2) => boolean - ES6 SameValue comparison
    auto isType = std::make_shared<FunctionType>();
    isType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    isType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    isType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    objectType->fields["is"] = isType;

    // Object.fromEntries(iterable) => object
    auto fromEntriesType = std::make_shared<FunctionType>();
    fromEntriesType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array));
    fromEntriesType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["fromEntries"] = fromEntriesType;

    // Object.getOwnPropertyNames(obj) => string[]
    auto getOwnPropertyNamesType = std::make_shared<FunctionType>();
    getOwnPropertyNamesType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    getOwnPropertyNamesType->returnType = stringArrayType;
    objectType->fields["getOwnPropertyNames"] = getOwnPropertyNamesType;

    // Object.getPrototypeOf(obj) => object | null
    auto getPrototypeOfType = std::make_shared<FunctionType>();
    getPrototypeOfType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    getPrototypeOfType->returnType = std::make_shared<Type>(TypeKind::Any);  // Can be null
    objectType->fields["getPrototypeOf"] = getPrototypeOfType;

    // Object.create(proto) => object
    auto createType = std::make_shared<FunctionType>();
    createType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // Can be null
    createType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["create"] = createType;

    // Object.setPrototypeOf(obj, proto) => object
    auto setPrototypeOfType = std::make_shared<FunctionType>();
    setPrototypeOfType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // obj
    setPrototypeOfType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // proto (can be null)
    setPrototypeOfType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["setPrototypeOf"] = setPrototypeOfType;

    // Object.preventExtensions(obj) => object
    auto preventExtType = std::make_shared<FunctionType>();
    preventExtType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    preventExtType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["preventExtensions"] = preventExtType;

    // Object.isExtensible(obj) => boolean
    auto isExtensibleType = std::make_shared<FunctionType>();
    isExtensibleType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    isExtensibleType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    objectType->fields["isExtensible"] = isExtensibleType;

    // Object.defineProperty(obj, prop, descriptor) => obj
    auto definePropertyType = std::make_shared<FunctionType>();
    definePropertyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    definePropertyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    definePropertyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));  // descriptor
    definePropertyType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["defineProperty"] = definePropertyType;

    // Object.defineProperties(obj, descriptors) => obj
    auto definePropertiesType = std::make_shared<FunctionType>();
    definePropertiesType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    definePropertiesType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));  // descriptors
    definePropertiesType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["defineProperties"] = definePropertiesType;

    // Object.getOwnPropertyDescriptor(obj, prop) => descriptor | undefined
    auto getOwnPropertyDescriptorType = std::make_shared<FunctionType>();
    getOwnPropertyDescriptorType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    getOwnPropertyDescriptorType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    getOwnPropertyDescriptorType->returnType = std::make_shared<Type>(TypeKind::Any);  // Can be undefined
    objectType->fields["getOwnPropertyDescriptor"] = getOwnPropertyDescriptorType;

    // Object.getOwnPropertyDescriptors(obj) => { [key: string]: PropertyDescriptor }
    auto getOwnPropertyDescriptorsType = std::make_shared<FunctionType>();
    getOwnPropertyDescriptorsType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Object));
    getOwnPropertyDescriptorsType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["getOwnPropertyDescriptors"] = getOwnPropertyDescriptorsType;

    // ES2024 Object.groupBy(iterable, callbackFn) => { [key: string]: T[] }
    auto groupByType = std::make_shared<FunctionType>();
    groupByType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array));  // iterable
    groupByType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));    // callbackFn
    groupByType->returnType = std::make_shared<Type>(TypeKind::Object);
    objectType->fields["groupBy"] = groupByType;

    symbols.define("Object", objectType);

    // Register Proxy constructor - new Proxy(target, handler)
    // Returns 'any' type so property access goes through dynamic lookup (proxy traps)
    auto proxyCtorType = std::make_shared<FunctionType>();
    proxyCtorType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    proxyCtorType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // handler
    proxyCtorType->returnType = std::make_shared<Type>(TypeKind::Any);  // returns 'any' for dynamic property access

    auto proxyType = std::make_shared<ObjectType>();

    // Proxy.revocable(target, handler) => { proxy, revoke }
    auto revocableType = std::make_shared<FunctionType>();
    revocableType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    revocableType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // handler
    revocableType->returnType = std::make_shared<Type>(TypeKind::Object);  // { proxy, revoke }
    proxyType->fields["revocable"] = revocableType;

    symbols.define("Proxy", proxyCtorType);

    // Register Reflect global object
    auto reflectType = std::make_shared<ObjectType>();

    // Reflect.get(target, propertyKey [, receiver])
    auto reflectGetType = std::make_shared<FunctionType>();
    reflectGetType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectGetType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // propertyKey
    reflectGetType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // receiver (optional)
    reflectGetType->returnType = std::make_shared<Type>(TypeKind::Any);
    reflectType->fields["get"] = reflectGetType;

    // Reflect.set(target, propertyKey, value [, receiver])
    auto reflectSetType = std::make_shared<FunctionType>();
    reflectSetType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectSetType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // propertyKey
    reflectSetType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // value
    reflectSetType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // receiver (optional)
    reflectSetType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    reflectType->fields["set"] = reflectSetType;

    // Reflect.has(target, propertyKey)
    auto reflectHasType = std::make_shared<FunctionType>();
    reflectHasType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectHasType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // propertyKey
    reflectHasType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    reflectType->fields["has"] = reflectHasType;

    // Reflect.deleteProperty(target, propertyKey)
    auto reflectDeleteType = std::make_shared<FunctionType>();
    reflectDeleteType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectDeleteType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // propertyKey
    reflectDeleteType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    reflectType->fields["deleteProperty"] = reflectDeleteType;

    // Reflect.apply(target, thisArgument, argumentsList)
    auto reflectApplyType = std::make_shared<FunctionType>();
    reflectApplyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectApplyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // thisArgument
    reflectApplyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // argumentsList
    reflectApplyType->returnType = std::make_shared<Type>(TypeKind::Any);
    reflectType->fields["apply"] = reflectApplyType;

    // Reflect.construct(target, argumentsList [, newTarget])
    auto reflectConstructType = std::make_shared<FunctionType>();
    reflectConstructType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectConstructType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // argumentsList
    reflectConstructType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // newTarget (optional)
    reflectConstructType->returnType = std::make_shared<Type>(TypeKind::Any);
    reflectType->fields["construct"] = reflectConstructType;

    // Reflect.getPrototypeOf(target)
    auto reflectGetProtoType = std::make_shared<FunctionType>();
    reflectGetProtoType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectGetProtoType->returnType = std::make_shared<Type>(TypeKind::Any);
    reflectType->fields["getPrototypeOf"] = reflectGetProtoType;

    // Reflect.setPrototypeOf(target, prototype)
    auto reflectSetProtoType = std::make_shared<FunctionType>();
    reflectSetProtoType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectSetProtoType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // prototype
    reflectSetProtoType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    reflectType->fields["setPrototypeOf"] = reflectSetProtoType;

    // Reflect.isExtensible(target)
    auto reflectIsExtType = std::make_shared<FunctionType>();
    reflectIsExtType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectIsExtType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    reflectType->fields["isExtensible"] = reflectIsExtType;

    // Reflect.preventExtensions(target)
    auto reflectPreventExtType = std::make_shared<FunctionType>();
    reflectPreventExtType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectPreventExtType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    reflectType->fields["preventExtensions"] = reflectPreventExtType;

    // Reflect.getOwnPropertyDescriptor(target, propertyKey)
    auto reflectGetOwnPropDescType = std::make_shared<FunctionType>();
    reflectGetOwnPropDescType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectGetOwnPropDescType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // propertyKey
    reflectGetOwnPropDescType->returnType = std::make_shared<Type>(TypeKind::Any);
    reflectType->fields["getOwnPropertyDescriptor"] = reflectGetOwnPropDescType;

    // Reflect.defineProperty(target, propertyKey, attributes)
    auto reflectDefPropType = std::make_shared<FunctionType>();
    reflectDefPropType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectDefPropType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // propertyKey
    reflectDefPropType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // attributes
    reflectDefPropType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    reflectType->fields["defineProperty"] = reflectDefPropType;

    // Reflect.ownKeys(target)
    auto reflectOwnKeysType = std::make_shared<FunctionType>();
    reflectOwnKeysType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // target
    reflectOwnKeysType->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
    reflectType->fields["ownKeys"] = reflectOwnKeysType;

    symbols.define("Reflect", reflectType);

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

    // Array.of(...items) => any[] - creates array from arguments
    auto arrayOfType = std::make_shared<FunctionType>();
    arrayOfType->hasRest = true;
    arrayOfType->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
    arrayGlobalType->fields["of"] = arrayOfType;

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

    auto imulType = std::make_shared<FunctionType>();
    imulType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    imulType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    imulType->returnType = std::make_shared<Type>(TypeKind::Int);
    mathType->fields["imul"] = imulType;

    mathType->fields["PI"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["E"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["LN10"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["LN2"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["LOG10E"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["LOG2E"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["SQRT1_2"] = std::make_shared<Type>(TypeKind::Double);
    mathType->fields["SQRT2"] = std::make_shared<Type>(TypeKind::Double);
    
    symbols.define("Math", mathType);

    // Register Number object with static methods
    auto numberType = std::make_shared<ObjectType>();

    // Number.isFinite(value: number): boolean
    auto isFiniteType = std::make_shared<FunctionType>();
    isFiniteType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    isFiniteType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    numberType->fields["isFinite"] = isFiniteType;

    // Number.isNaN(value: number): boolean
    auto isNaNType = std::make_shared<FunctionType>();
    isNaNType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    isNaNType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    numberType->fields["isNaN"] = isNaNType;

    // Number.isInteger(value: number): boolean
    auto isIntegerType = std::make_shared<FunctionType>();
    isIntegerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    isIntegerType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    numberType->fields["isInteger"] = isIntegerType;

    // Number.isSafeInteger(value: number): boolean
    auto isSafeIntegerType = std::make_shared<FunctionType>();
    isSafeIntegerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    isSafeIntegerType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    numberType->fields["isSafeInteger"] = isSafeIntegerType;

    // Number.parseFloat(string: string): number
    auto parseFloatType = std::make_shared<FunctionType>();
    parseFloatType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    parseFloatType->returnType = std::make_shared<Type>(TypeKind::Double);
    numberType->fields["parseFloat"] = parseFloatType;

    // Number.parseInt(string: string, radix?: number): number
    auto parseIntType = std::make_shared<FunctionType>();
    parseIntType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    parseIntType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    parseIntType->returnType = std::make_shared<Type>(TypeKind::Int);
    numberType->fields["parseInt"] = parseIntType;

    // Number constants
    numberType->fields["MAX_VALUE"] = std::make_shared<Type>(TypeKind::Double);
    numberType->fields["MIN_VALUE"] = std::make_shared<Type>(TypeKind::Double);
    numberType->fields["MAX_SAFE_INTEGER"] = std::make_shared<Type>(TypeKind::Int);
    numberType->fields["MIN_SAFE_INTEGER"] = std::make_shared<Type>(TypeKind::Int);
    numberType->fields["POSITIVE_INFINITY"] = std::make_shared<Type>(TypeKind::Double);
    numberType->fields["NEGATIVE_INFINITY"] = std::make_shared<Type>(TypeKind::Double);
    numberType->fields["NaN"] = std::make_shared<Type>(TypeKind::Double);
    numberType->fields["EPSILON"] = std::make_shared<Type>(TypeKind::Double);

    symbols.define("Number", numberType);

    // Register String global object with static methods
    auto stringGlobalType = std::make_shared<ObjectType>();

    // String.fromCodePoint(...codePoints: number[]): string
    auto fromCodePointType = std::make_shared<FunctionType>();
    fromCodePointType->hasRest = true;
    fromCodePointType->returnType = std::make_shared<Type>(TypeKind::String);
    stringGlobalType->fields["fromCodePoint"] = fromCodePointType;

    // String.fromCharCode(...codes: number[]): string
    auto fromCharCodeType = std::make_shared<FunctionType>();
    fromCharCodeType->hasRest = true;
    fromCharCodeType->returnType = std::make_shared<Type>(TypeKind::String);
    stringGlobalType->fields["fromCharCode"] = fromCharCodeType;

    // String.raw(template: TemplateStringsArray, ...substitutions: any[]): string
    auto rawType = std::make_shared<FunctionType>();
    rawType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // template object with 'raw' property
    rawType->hasRest = true;  // ...substitutions
    rawType->returnType = std::make_shared<Type>(TypeKind::String);
    stringGlobalType->fields["raw"] = rawType;

    symbols.define("String", stringGlobalType);


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
    registerOS();
    registerCrypto();
    registerQueryString();
}
} // namespace ts
