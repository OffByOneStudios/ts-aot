#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerUtil() {
    auto utilType = std::make_shared<ObjectType>();

    // =========================================================================
    // util.promisify(fn: Function): any
    // Wraps a callback-style function to return a Promise
    // Return type is Any since we can't know the actual promisified signature
    // =========================================================================
    auto promisifyType = std::make_shared<FunctionType>();
    promisifyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    promisifyType->returnType = std::make_shared<Type>(TypeKind::Any);
    utilType->fields["promisify"] = promisifyType;

    // =========================================================================
    // util.inspect(obj: any, options?: object): string
    // Returns a string representation of an object
    // =========================================================================
    auto inspectType = std::make_shared<FunctionType>();
    inspectType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    inspectType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    inspectType->isOptional = { false, true };
    inspectType->returnType = std::make_shared<Type>(TypeKind::String);
    utilType->fields["inspect"] = inspectType;

    // =========================================================================
    // util.format(format: string, ...args: any[]): string
    // Printf-like string formatting
    // =========================================================================
    auto formatType = std::make_shared<FunctionType>();
    formatType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    formatType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    formatType->hasRest = true;
    formatType->returnType = std::make_shared<Type>(TypeKind::String);
    utilType->fields["format"] = formatType;

    // =========================================================================
    // util.inherits(constructor: Function, superConstructor: Function): void
    // Legacy prototype inheritance helper
    // =========================================================================
    auto inheritsType = std::make_shared<FunctionType>();
    inheritsType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    inheritsType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    inheritsType->returnType = std::make_shared<Type>(TypeKind::Void);
    utilType->fields["inherits"] = inheritsType;

    // =========================================================================
    // util.deprecate(fn: Function, msg: string): Function
    // Mark a function as deprecated
    // =========================================================================
    auto deprecateType = std::make_shared<FunctionType>();
    deprecateType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    deprecateType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    deprecateType->returnType = std::make_shared<Type>(TypeKind::Function);
    utilType->fields["deprecate"] = deprecateType;

    // =========================================================================
    // util.callbackify(fn: Function): any
    // Convert a Promise-returning function to callback style
    // Return type is Any since we can't know the actual callbackified signature
    // =========================================================================
    auto callbackifyType = std::make_shared<FunctionType>();
    callbackifyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    callbackifyType->returnType = std::make_shared<Type>(TypeKind::Any);
    utilType->fields["callbackify"] = callbackifyType;

    // =========================================================================
    // util.isDeepStrictEqual(val1: any, val2: any): boolean
    // Deep equality check
    // =========================================================================
    auto isDeepStrictEqualType = std::make_shared<FunctionType>();
    isDeepStrictEqualType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    isDeepStrictEqualType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    isDeepStrictEqualType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    utilType->fields["isDeepStrictEqual"] = isDeepStrictEqualType;

    // =========================================================================
    // util.stripVTControlCharacters(str: string): string
    // Removes ANSI escape codes from a string
    // =========================================================================
    auto stripVTControlCharactersType = std::make_shared<FunctionType>();
    stripVTControlCharactersType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    stripVTControlCharactersType->returnType = std::make_shared<Type>(TypeKind::String);
    utilType->fields["stripVTControlCharacters"] = stripVTControlCharactersType;

    // =========================================================================
    // util.toUSVString(str: string): string
    // Converts a string to a valid Unicode scalar value string
    // =========================================================================
    auto toUSVStringType = std::make_shared<FunctionType>();
    toUSVStringType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    toUSVStringType->returnType = std::make_shared<Type>(TypeKind::String);
    utilType->fields["toUSVString"] = toUSVStringType;

    // =========================================================================
    // util.getSystemErrorName(errno: number): string
    // Returns the string name for a numeric error code
    // =========================================================================
    auto getSystemErrorNameType = std::make_shared<FunctionType>();
    getSystemErrorNameType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    getSystemErrorNameType->returnType = std::make_shared<Type>(TypeKind::String);
    utilType->fields["getSystemErrorName"] = getSystemErrorNameType;

    // =========================================================================
    // util.getSystemErrorMap(): Map<number, [string, string]>
    // Returns a Map of all system error codes
    // =========================================================================
    auto getSystemErrorMapType = std::make_shared<FunctionType>();
    getSystemErrorMapType->returnType = std::make_shared<ClassType>("Map");
    utilType->fields["getSystemErrorMap"] = getSystemErrorMapType;

    // =========================================================================
    // util.styleText(format: string, text: string): string
    // Apply ANSI styles to text
    // =========================================================================
    auto styleTextType = std::make_shared<FunctionType>();
    styleTextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    styleTextType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    styleTextType->returnType = std::make_shared<Type>(TypeKind::String);
    utilType->fields["styleText"] = styleTextType;

    // =========================================================================
    // util.formatWithOptions(inspectOptions: object, format: string, ...args): string
    // Like util.format() but with inspect options
    // =========================================================================
    auto formatWithOptionsType = std::make_shared<FunctionType>();
    formatWithOptionsType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    formatWithOptionsType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // format
    formatWithOptionsType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // ...args
    formatWithOptionsType->hasRest = true;
    formatWithOptionsType->returnType = std::make_shared<Type>(TypeKind::String);
    utilType->fields["formatWithOptions"] = formatWithOptionsType;

    // =========================================================================
    // util.debuglog(section: string): Function
    // Returns a logging function for the given section
    // =========================================================================
    auto debuglogType = std::make_shared<FunctionType>();
    debuglogType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    debuglogType->returnType = std::make_shared<Type>(TypeKind::Function);
    utilType->fields["debuglog"] = debuglogType;

    // =========================================================================
    // util.parseArgs(config?: object): { values: object, positionals: string[] }
    // Parses command-line arguments
    // =========================================================================
    auto parseArgsType = std::make_shared<FunctionType>();
    parseArgsType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // config object
    parseArgsType->isOptional = { true };
    parseArgsType->returnType = std::make_shared<Type>(TypeKind::Any);  // Returns { values, positionals }
    utilType->fields["parseArgs"] = parseArgsType;

    // =========================================================================
    // util.types - Type checking utilities
    // =========================================================================
    auto typesType = std::make_shared<ObjectType>();
    
    // Helper to create a (value: any) => boolean type
    auto makePredicate = [this]() {
        auto pred = std::make_shared<FunctionType>();
        pred->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        pred->returnType = std::make_shared<Type>(TypeKind::Boolean);
        return pred;
    };

    typesType->fields["isPromise"] = makePredicate();
    typesType->fields["isTypedArray"] = makePredicate();
    typesType->fields["isArrayBuffer"] = makePredicate();
    typesType->fields["isArrayBufferView"] = makePredicate();
    typesType->fields["isAsyncFunction"] = makePredicate();
    typesType->fields["isBigInt64Array"] = makePredicate();
    typesType->fields["isBigUint64Array"] = makePredicate();
    typesType->fields["isBooleanObject"] = makePredicate();
    typesType->fields["isBoxedPrimitive"] = makePredicate();
    typesType->fields["isDataView"] = makePredicate();
    typesType->fields["isDate"] = makePredicate();
    typesType->fields["isFloat32Array"] = makePredicate();
    typesType->fields["isFloat64Array"] = makePredicate();
    typesType->fields["isGeneratorFunction"] = makePredicate();
    typesType->fields["isGeneratorObject"] = makePredicate();
    typesType->fields["isInt8Array"] = makePredicate();
    typesType->fields["isInt16Array"] = makePredicate();
    typesType->fields["isInt32Array"] = makePredicate();
    typesType->fields["isMap"] = makePredicate();
    typesType->fields["isMapIterator"] = makePredicate();
    typesType->fields["isNativeError"] = makePredicate();
    typesType->fields["isNumberObject"] = makePredicate();
    typesType->fields["isProxy"] = makePredicate();
    typesType->fields["isRegExp"] = makePredicate();
    typesType->fields["isSet"] = makePredicate();
    typesType->fields["isSetIterator"] = makePredicate();
    typesType->fields["isSharedArrayBuffer"] = makePredicate();
    typesType->fields["isStringObject"] = makePredicate();
    typesType->fields["isSymbolObject"] = makePredicate();
    typesType->fields["isUint8Array"] = makePredicate();
    typesType->fields["isUint8ClampedArray"] = makePredicate();
    typesType->fields["isUint16Array"] = makePredicate();
    typesType->fields["isUint32Array"] = makePredicate();
    typesType->fields["isWeakMap"] = makePredicate();
    typesType->fields["isWeakSet"] = makePredicate();
    typesType->fields["isAnyArrayBuffer"] = makePredicate();

    utilType->fields["types"] = typesType;

    // Register the util module
    symbols.define("util", utilType);
}

} // namespace ts
