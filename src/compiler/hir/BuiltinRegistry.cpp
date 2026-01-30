#include "BuiltinRegistry.h"

namespace ts::hir {

BuiltinRegistry& BuiltinRegistry::instance() {
    static BuiltinRegistry registry;
    return registry;
}

BuiltinRegistry::BuiltinRegistry() {
    registerArrayMethods();
    registerArrayStaticMethods();
    registerStringMethods();
    registerStringStaticMethods();
    registerObjectMethods();
    registerMapMethods();
    registerSetMethods();
    registerNumberStaticMethods();
    registerMathBuiltins();
    registerConsoleBuiltins();
    registerJSONBuiltins();
}

void BuiltinRegistry::registerArrayMethods() {
    auto intType = HIRType::makeInt64();
    auto anyType = HIRType::makeAny();
    auto arrayType = HIRType::makeArray(anyType);
    auto boolType = HIRType::makeBool();
    auto stringType = HIRType::makeString();

    // Methods that become specific HIR opcodes
    methodTable_[{HIRTypeKind::Array, "push"}] =
        MethodResolution::makeOpcode(HIROpcode::ArrayPush, intType);

    methodTable_[{HIRTypeKind::Array, "length"}] =
        MethodResolution::makeOpcode(HIROpcode::ArrayLength, intType);

    // Methods that map to runtime calls
    // Note: first arg is always the array (receiver), passed implicitly

    // Mutating methods
    methodTable_[{HIRTypeKind::Array, "pop"}] =
        MethodResolution::makeRuntimeCall("ts_array_pop", 0, anyType);

    methodTable_[{HIRTypeKind::Array, "shift"}] =
        MethodResolution::makeRuntimeCall("ts_array_shift", 0, anyType);

    methodTable_[{HIRTypeKind::Array, "unshift"}] =
        MethodResolution::makeRuntimeCall("ts_array_unshift", -1, intType);

    methodTable_[{HIRTypeKind::Array, "reverse"}] =
        MethodResolution::makeRuntimeCall("ts_array_reverse", 0, arrayType);

    methodTable_[{HIRTypeKind::Array, "sort"}] =
        MethodResolution::makeRuntimeCall("ts_array_sort", -1, arrayType);

    methodTable_[{HIRTypeKind::Array, "splice"}] =
        MethodResolution::makeRuntimeCall("ts_array_splice", -1, arrayType);

    methodTable_[{HIRTypeKind::Array, "fill"}] =
        MethodResolution::makeRuntimeCall("ts_array_fill", -1, arrayType);

    methodTable_[{HIRTypeKind::Array, "copyWithin"}] =
        MethodResolution::makeRuntimeCall("ts_array_copyWithin", -1, arrayType);

    // Non-mutating methods
    methodTable_[{HIRTypeKind::Array, "join"}] =
        MethodResolution::makeRuntimeCall("ts_array_join", -1, stringType);

    methodTable_[{HIRTypeKind::Array, "indexOf"}] =
        MethodResolution::makeRuntimeCall("ts_array_indexOf", -1, intType);

    methodTable_[{HIRTypeKind::Array, "lastIndexOf"}] =
        MethodResolution::makeRuntimeCall("ts_array_lastIndexOf", -1, intType);

    methodTable_[{HIRTypeKind::Array, "includes"}] =
        MethodResolution::makeRuntimeCall("ts_array_includes", -1, boolType);

    methodTable_[{HIRTypeKind::Array, "slice"}] =
        MethodResolution::makeRuntimeCall("ts_array_slice", -1, arrayType);

    methodTable_[{HIRTypeKind::Array, "concat"}] =
        MethodResolution::makeRuntimeCall("ts_array_concat", -1, arrayType);

    methodTable_[{HIRTypeKind::Array, "flat"}] =
        MethodResolution::makeRuntimeCall("ts_array_flat", -1, arrayType);

    methodTable_[{HIRTypeKind::Array, "at"}] =
        MethodResolution::makeRuntimeCall("ts_array_at", 1, anyType);

    // ES2023 non-mutating alternatives
    methodTable_[{HIRTypeKind::Array, "toReversed"}] =
        MethodResolution::makeRuntimeCall("ts_array_toReversed", 0, arrayType);

    methodTable_[{HIRTypeKind::Array, "toSorted"}] =
        MethodResolution::makeRuntimeCall("ts_array_toSorted", -1, arrayType);

    methodTable_[{HIRTypeKind::Array, "toSpliced"}] =
        MethodResolution::makeRuntimeCall("ts_array_toSpliced", -1, arrayType);

    methodTable_[{HIRTypeKind::Array, "with"}] =
        MethodResolution::makeRuntimeCall("ts_array_with", 2, arrayType);

    // Iterator methods (return arrays for now)
    methodTable_[{HIRTypeKind::Array, "entries"}] =
        MethodResolution::makeRuntimeCall("ts_array_entries", 0, arrayType);

    methodTable_[{HIRTypeKind::Array, "keys"}] =
        MethodResolution::makeRuntimeCall("ts_array_keys", 0, arrayType);

    methodTable_[{HIRTypeKind::Array, "values"}] =
        MethodResolution::makeRuntimeCall("ts_array_values", 0, arrayType);

    // Higher-order methods (callback-based)
    auto voidType = HIRType::makeVoid();

    methodTable_[{HIRTypeKind::Array, "map"}] =
        MethodResolution::makeRuntimeCall("ts_array_map", -1, arrayType);

    methodTable_[{HIRTypeKind::Array, "filter"}] =
        MethodResolution::makeRuntimeCall("ts_array_filter", -1, arrayType);

    methodTable_[{HIRTypeKind::Array, "reduce"}] =
        MethodResolution::makeRuntimeCall("ts_array_reduce", -1, anyType);

    methodTable_[{HIRTypeKind::Array, "reduceRight"}] =
        MethodResolution::makeRuntimeCall("ts_array_reduceRight", -1, anyType);

    methodTable_[{HIRTypeKind::Array, "forEach"}] =
        MethodResolution::makeRuntimeCall("ts_array_forEach", -1, voidType);

    methodTable_[{HIRTypeKind::Array, "find"}] =
        MethodResolution::makeRuntimeCall("ts_array_find", -1, anyType);

    methodTable_[{HIRTypeKind::Array, "findIndex"}] =
        MethodResolution::makeRuntimeCall("ts_array_findIndex", -1, intType);

    methodTable_[{HIRTypeKind::Array, "findLast"}] =
        MethodResolution::makeRuntimeCall("ts_array_findLast", -1, anyType);

    methodTable_[{HIRTypeKind::Array, "findLastIndex"}] =
        MethodResolution::makeRuntimeCall("ts_array_findLastIndex", -1, intType);

    methodTable_[{HIRTypeKind::Array, "some"}] =
        MethodResolution::makeRuntimeCall("ts_array_some", -1, boolType);

    methodTable_[{HIRTypeKind::Array, "every"}] =
        MethodResolution::makeRuntimeCall("ts_array_every", -1, boolType);

    methodTable_[{HIRTypeKind::Array, "flatMap"}] =
        MethodResolution::makeRuntimeCall("ts_array_flatMap", -1, arrayType);
}

void BuiltinRegistry::registerArrayStaticMethods() {
    auto boolType = HIRType::makeBool();
    auto anyType = HIRType::makeAny();
    auto arrayType = HIRType::makeArray(anyType);

    // Array static methods
    builtinGlobals_.insert("Array");

    globalTable_[{"Array", "isArray"}] =
        BuiltinResolution::makeRuntimeCall("ts_array_isArray", boolType);

    globalTable_[{"Array", "from"}] =
        BuiltinResolution::makeRuntimeCall("ts_array_from", arrayType);

    globalTable_[{"Array", "of"}] =
        BuiltinResolution::makeRuntimeCall("ts_array_of", arrayType);
}

void BuiltinRegistry::registerStringMethods() {
    auto intType = HIRType::makeInt64();
    auto stringType = HIRType::makeString();
    auto boolType = HIRType::makeBool();
    auto arrayType = HIRType::makeArray(stringType);

    // Character access
    methodTable_[{HIRTypeKind::String, "charAt"}] =
        MethodResolution::makeRuntimeCall("ts_string_charAt", 1, stringType);

    methodTable_[{HIRTypeKind::String, "charCodeAt"}] =
        MethodResolution::makeRuntimeCall("ts_string_charCodeAt", 1, intType);

    methodTable_[{HIRTypeKind::String, "codePointAt"}] =
        MethodResolution::makeRuntimeCall("ts_string_codePointAt", 1, intType);

    methodTable_[{HIRTypeKind::String, "at"}] =
        MethodResolution::makeRuntimeCall("ts_string_at", 1, stringType);

    // Substring extraction
    methodTable_[{HIRTypeKind::String, "substring"}] =
        MethodResolution::makeRuntimeCall("ts_string_substring", -1, stringType);

    methodTable_[{HIRTypeKind::String, "substr"}] =
        MethodResolution::makeRuntimeCall("ts_string_substr", -1, stringType);

    methodTable_[{HIRTypeKind::String, "slice"}] =
        MethodResolution::makeRuntimeCall("ts_string_slice", -1, stringType);

    // Search
    methodTable_[{HIRTypeKind::String, "indexOf"}] =
        MethodResolution::makeRuntimeCall("ts_string_indexOf", -1, intType);

    methodTable_[{HIRTypeKind::String, "lastIndexOf"}] =
        MethodResolution::makeRuntimeCall("ts_string_lastIndexOf", -1, intType);

    methodTable_[{HIRTypeKind::String, "includes"}] =
        MethodResolution::makeRuntimeCall("ts_string_includes", -1, boolType);

    methodTable_[{HIRTypeKind::String, "startsWith"}] =
        MethodResolution::makeRuntimeCall("ts_string_startsWith", -1, boolType);

    methodTable_[{HIRTypeKind::String, "endsWith"}] =
        MethodResolution::makeRuntimeCall("ts_string_endsWith", -1, boolType);

    // Case conversion
    methodTable_[{HIRTypeKind::String, "toLowerCase"}] =
        MethodResolution::makeRuntimeCall("ts_string_toLowerCase", 0, stringType);

    methodTable_[{HIRTypeKind::String, "toUpperCase"}] =
        MethodResolution::makeRuntimeCall("ts_string_toUpperCase", 0, stringType);

    methodTable_[{HIRTypeKind::String, "toLocaleLowerCase"}] =
        MethodResolution::makeRuntimeCall("ts_string_toLocaleLowerCase", -1, stringType);

    methodTable_[{HIRTypeKind::String, "toLocaleUpperCase"}] =
        MethodResolution::makeRuntimeCall("ts_string_toLocaleUpperCase", -1, stringType);

    // Trimming
    methodTable_[{HIRTypeKind::String, "trim"}] =
        MethodResolution::makeRuntimeCall("ts_string_trim", 0, stringType);

    methodTable_[{HIRTypeKind::String, "trimStart"}] =
        MethodResolution::makeRuntimeCall("ts_string_trimStart", 0, stringType);

    methodTable_[{HIRTypeKind::String, "trimEnd"}] =
        MethodResolution::makeRuntimeCall("ts_string_trimEnd", 0, stringType);

    methodTable_[{HIRTypeKind::String, "trimLeft"}] =
        MethodResolution::makeRuntimeCall("ts_string_trimStart", 0, stringType);

    methodTable_[{HIRTypeKind::String, "trimRight"}] =
        MethodResolution::makeRuntimeCall("ts_string_trimEnd", 0, stringType);

    // Padding
    methodTable_[{HIRTypeKind::String, "padStart"}] =
        MethodResolution::makeRuntimeCall("ts_string_padStart", -1, stringType);

    methodTable_[{HIRTypeKind::String, "padEnd"}] =
        MethodResolution::makeRuntimeCall("ts_string_padEnd", -1, stringType);

    // Splitting and joining
    methodTable_[{HIRTypeKind::String, "split"}] =
        MethodResolution::makeRuntimeCall("ts_string_split", -1, arrayType);

    // Repeat
    methodTable_[{HIRTypeKind::String, "repeat"}] =
        MethodResolution::makeRuntimeCall("ts_string_repeat", 1, stringType);

    // Replace
    methodTable_[{HIRTypeKind::String, "replace"}] =
        MethodResolution::makeRuntimeCall("ts_string_replace", 2, stringType);

    methodTable_[{HIRTypeKind::String, "replaceAll"}] =
        MethodResolution::makeRuntimeCall("ts_string_replaceAll", 2, stringType);

    // Normalize
    methodTable_[{HIRTypeKind::String, "normalize"}] =
        MethodResolution::makeRuntimeCall("ts_string_normalize", -1, stringType);

    // Locale comparison
    methodTable_[{HIRTypeKind::String, "localeCompare"}] =
        MethodResolution::makeRuntimeCall("ts_string_localeCompare", -1, intType);

    // Well-formed checks (ES2024)
    methodTable_[{HIRTypeKind::String, "isWellFormed"}] =
        MethodResolution::makeRuntimeCall("ts_string_isWellFormed", 0, boolType);

    methodTable_[{HIRTypeKind::String, "toWellFormed"}] =
        MethodResolution::makeRuntimeCall("ts_string_toWellFormed", 0, stringType);

    // Match methods
    methodTable_[{HIRTypeKind::String, "match"}] =
        MethodResolution::makeRuntimeCall("ts_string_match", 1);

    methodTable_[{HIRTypeKind::String, "matchAll"}] =
        MethodResolution::makeRuntimeCall("ts_string_matchAll", 1);

    methodTable_[{HIRTypeKind::String, "search"}] =
        MethodResolution::makeRuntimeCall("ts_string_search", 1, intType);
}

void BuiltinRegistry::registerStringStaticMethods() {
    auto stringType = HIRType::makeString();

    // String static methods
    builtinGlobals_.insert("String");

    globalTable_[{"String", "fromCharCode"}] =
        BuiltinResolution::makeRuntimeCall("ts_string_fromCharCode", stringType);

    globalTable_[{"String", "fromCodePoint"}] =
        BuiltinResolution::makeRuntimeCall("ts_string_fromCodePoint", stringType);

    globalTable_[{"String", "raw"}] =
        BuiltinResolution::makeRuntimeCall("ts_string_raw", stringType);
}

void BuiltinRegistry::registerNumberStaticMethods() {
    auto boolType = HIRType::makeBool();
    auto floatType = HIRType::makeFloat64();
    auto intType = HIRType::makeInt64();

    // Number static methods
    builtinGlobals_.insert("Number");

    globalTable_[{"Number", "isNaN"}] =
        BuiltinResolution::makeRuntimeCall("ts_number_isNaN", boolType);

    globalTable_[{"Number", "isFinite"}] =
        BuiltinResolution::makeRuntimeCall("ts_number_isFinite", boolType);

    globalTable_[{"Number", "isInteger"}] =
        BuiltinResolution::makeRuntimeCall("ts_number_isInteger", boolType);

    globalTable_[{"Number", "isSafeInteger"}] =
        BuiltinResolution::makeRuntimeCall("ts_number_isSafeInteger", boolType);

    globalTable_[{"Number", "parseInt"}] =
        BuiltinResolution::makeRuntimeCall("ts_number_parseInt", intType);

    globalTable_[{"Number", "parseFloat"}] =
        BuiltinResolution::makeRuntimeCall("ts_number_parseFloat", floatType);

    // Global parseInt/parseFloat also
    globalTable_[{"", "parseInt"}] =
        BuiltinResolution::makeRuntimeCall("ts_number_parseInt", intType);

    globalTable_[{"", "parseFloat"}] =
        BuiltinResolution::makeRuntimeCall("ts_number_parseFloat", floatType);

    globalTable_[{"", "isNaN"}] =
        BuiltinResolution::makeRuntimeCall("ts_number_isNaN", boolType);

    globalTable_[{"", "isFinite"}] =
        BuiltinResolution::makeRuntimeCall("ts_number_isFinite", boolType);
}

void BuiltinRegistry::registerObjectMethods() {
    auto anyType = HIRType::makeAny();
    auto stringType = HIRType::makeString();
    auto boolType = HIRType::makeBool();
    auto objectType = HIRType::makeObject();
    auto arrayType = HIRType::makeArray(anyType);
    auto stringArrayType = HIRType::makeArray(stringType);

    // Object instance methods are typically handled via property access

    // Object static methods
    builtinGlobals_.insert("Object");

    globalTable_[{"Object", "keys"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_keys", stringArrayType);

    globalTable_[{"Object", "values"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_values", arrayType);

    globalTable_[{"Object", "entries"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_entries", arrayType);

    globalTable_[{"Object", "assign"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_assign", objectType);

    globalTable_[{"Object", "hasOwn"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_hasOwn", boolType);

    globalTable_[{"Object", "fromEntries"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_fromEntries", objectType);

    globalTable_[{"Object", "getOwnPropertyNames"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_getOwnPropertyNames", stringArrayType);

    globalTable_[{"Object", "getOwnPropertyDescriptor"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_getOwnPropertyDescriptor", objectType);

    globalTable_[{"Object", "create"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_create", objectType);

    globalTable_[{"Object", "freeze"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_freeze", objectType);

    globalTable_[{"Object", "seal"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_seal", objectType);

    globalTable_[{"Object", "is"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_is", boolType);

    globalTable_[{"Object", "getPrototypeOf"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_getPrototypeOf", objectType);

    globalTable_[{"Object", "setPrototypeOf"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_setPrototypeOf", objectType);

    globalTable_[{"Object", "isFrozen"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_isFrozen", boolType);

    globalTable_[{"Object", "isSealed"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_isSealed", boolType);

    globalTable_[{"Object", "isExtensible"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_isExtensible", boolType);

    globalTable_[{"Object", "preventExtensions"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_preventExtensions", objectType);

    globalTable_[{"Object", "defineProperty"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_defineProperty", objectType);

    globalTable_[{"Object", "defineProperties"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_defineProperties", objectType);

    globalTable_[{"Object", "getOwnPropertyDescriptors"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_getOwnPropertyDescriptors", objectType);

    globalTable_[{"Object", "groupBy"}] =
        BuiltinResolution::makeRuntimeCall("ts_object_groupBy", objectType);
}

void BuiltinRegistry::registerMapMethods() {
    auto anyType = HIRType::makeAny();
    auto intType = HIRType::makeInt64();
    auto boolType = HIRType::makeBool();
    auto mapType = HIRType::makeMap();
    auto voidType = HIRType::makeVoid();
    auto arrayType = HIRType::makeArray(anyType);

    // Map instance methods
    methodTable_[{HIRTypeKind::Map, "get"}] =
        MethodResolution::makeRuntimeCall("ts_map_get", 1, anyType);

    methodTable_[{HIRTypeKind::Map, "set"}] =
        MethodResolution::makeRuntimeCall("ts_map_set", 2, mapType);

    methodTable_[{HIRTypeKind::Map, "has"}] =
        MethodResolution::makeRuntimeCall("ts_map_has", 1, boolType);

    methodTable_[{HIRTypeKind::Map, "delete"}] =
        MethodResolution::makeRuntimeCall("ts_map_delete", 1, boolType);

    methodTable_[{HIRTypeKind::Map, "clear"}] =
        MethodResolution::makeRuntimeCall("ts_map_clear", 0, voidType);

    methodTable_[{HIRTypeKind::Map, "size"}] =
        MethodResolution::makeRuntimeCall("ts_map_size", 0, intType);

    methodTable_[{HIRTypeKind::Map, "forEach"}] =
        MethodResolution::makeRuntimeCall("ts_map_forEach", -1, voidType);

    // Iterator methods (return arrays for now)
    methodTable_[{HIRTypeKind::Map, "entries"}] =
        MethodResolution::makeRuntimeCall("ts_map_entries", 0, arrayType);

    methodTable_[{HIRTypeKind::Map, "keys"}] =
        MethodResolution::makeRuntimeCall("ts_map_keys", 0, arrayType);

    methodTable_[{HIRTypeKind::Map, "values"}] =
        MethodResolution::makeRuntimeCall("ts_map_values", 0, arrayType);

    // Map static methods
    builtinGlobals_.insert("Map");

    globalTable_[{"Map", "groupBy"}] =
        BuiltinResolution::makeRuntimeCall("ts_map_groupBy", mapType);
}

void BuiltinRegistry::registerSetMethods() {
    auto anyType = HIRType::makeAny();
    auto intType = HIRType::makeInt64();
    auto boolType = HIRType::makeBool();
    auto setType = HIRType::makeSet();
    auto voidType = HIRType::makeVoid();
    auto arrayType = HIRType::makeArray(anyType);

    // Set instance methods
    methodTable_[{HIRTypeKind::Set, "add"}] =
        MethodResolution::makeRuntimeCall("ts_set_add", 1, setType);

    methodTable_[{HIRTypeKind::Set, "has"}] =
        MethodResolution::makeRuntimeCall("ts_set_has", 1, boolType);

    methodTable_[{HIRTypeKind::Set, "delete"}] =
        MethodResolution::makeRuntimeCall("ts_set_delete", 1, boolType);

    methodTable_[{HIRTypeKind::Set, "clear"}] =
        MethodResolution::makeRuntimeCall("ts_set_clear", 0, voidType);

    methodTable_[{HIRTypeKind::Set, "size"}] =
        MethodResolution::makeRuntimeCall("ts_set_size", 0, intType);

    methodTable_[{HIRTypeKind::Set, "forEach"}] =
        MethodResolution::makeRuntimeCall("ts_set_forEach", -1, voidType);

    // Iterator methods (return arrays for now)
    methodTable_[{HIRTypeKind::Set, "entries"}] =
        MethodResolution::makeRuntimeCall("ts_set_entries", 0, arrayType);

    methodTable_[{HIRTypeKind::Set, "keys"}] =
        MethodResolution::makeRuntimeCall("ts_set_keys", 0, arrayType);

    methodTable_[{HIRTypeKind::Set, "values"}] =
        MethodResolution::makeRuntimeCall("ts_set_values", 0, arrayType);

    // ES2024 Set methods
    methodTable_[{HIRTypeKind::Set, "union"}] =
        MethodResolution::makeRuntimeCall("ts_set_union", 1, setType);

    methodTable_[{HIRTypeKind::Set, "intersection"}] =
        MethodResolution::makeRuntimeCall("ts_set_intersection", 1, setType);

    methodTable_[{HIRTypeKind::Set, "difference"}] =
        MethodResolution::makeRuntimeCall("ts_set_difference", 1, setType);

    methodTable_[{HIRTypeKind::Set, "symmetricDifference"}] =
        MethodResolution::makeRuntimeCall("ts_set_symmetricDifference", 1, setType);

    methodTable_[{HIRTypeKind::Set, "isSubsetOf"}] =
        MethodResolution::makeRuntimeCall("ts_set_isSubsetOf", 1, boolType);

    methodTable_[{HIRTypeKind::Set, "isSupersetOf"}] =
        MethodResolution::makeRuntimeCall("ts_set_isSupersetOf", 1, boolType);

    methodTable_[{HIRTypeKind::Set, "isDisjointFrom"}] =
        MethodResolution::makeRuntimeCall("ts_set_isDisjointFrom", 1, boolType);

    // Set static methods
    builtinGlobals_.insert("Set");
}

void BuiltinRegistry::registerMathBuiltins() {
    auto floatType = HIRType::makeFloat64();
    auto intType = HIRType::makeInt64();

    builtinGlobals_.insert("Math");

    // Single-argument functions
    globalTable_[{"Math", "abs"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_abs", floatType);

    globalTable_[{"Math", "ceil"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_ceil", floatType);

    globalTable_[{"Math", "floor"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_floor", floatType);

    globalTable_[{"Math", "round"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_round", floatType);

    globalTable_[{"Math", "trunc"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_trunc", floatType);

    globalTable_[{"Math", "sqrt"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_sqrt", floatType);

    globalTable_[{"Math", "cbrt"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_cbrt", floatType);

    globalTable_[{"Math", "exp"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_exp", floatType);

    globalTable_[{"Math", "expm1"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_expm1", floatType);

    globalTable_[{"Math", "log"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_log", floatType);

    globalTable_[{"Math", "log10"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_log10", floatType);

    globalTable_[{"Math", "log2"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_log2", floatType);

    globalTable_[{"Math", "log1p"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_log1p", floatType);

    globalTable_[{"Math", "sin"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_sin", floatType);

    globalTable_[{"Math", "cos"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_cos", floatType);

    globalTable_[{"Math", "tan"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_tan", floatType);

    globalTable_[{"Math", "asin"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_asin", floatType);

    globalTable_[{"Math", "acos"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_acos", floatType);

    globalTable_[{"Math", "atan"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_atan", floatType);

    globalTable_[{"Math", "sinh"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_sinh", floatType);

    globalTable_[{"Math", "cosh"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_cosh", floatType);

    globalTable_[{"Math", "tanh"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_tanh", floatType);

    globalTable_[{"Math", "asinh"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_asinh", floatType);

    globalTable_[{"Math", "acosh"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_acosh", floatType);

    globalTable_[{"Math", "atanh"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_atanh", floatType);

    globalTable_[{"Math", "sign"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_sign", floatType);

    globalTable_[{"Math", "fround"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_fround", floatType);

    globalTable_[{"Math", "clz32"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_clz32", intType);

    // Two-argument functions
    globalTable_[{"Math", "pow"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_pow", floatType);

    globalTable_[{"Math", "atan2"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_atan2", floatType);

    globalTable_[{"Math", "imul"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_imul", intType);

    // Variadic functions
    globalTable_[{"Math", "min"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_min", floatType);

    globalTable_[{"Math", "max"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_max", floatType);

    globalTable_[{"Math", "hypot"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_hypot", floatType);

    // Zero-argument functions
    globalTable_[{"Math", "random"}] =
        BuiltinResolution::makeRuntimeCall("ts_math_random", floatType);
}

void BuiltinRegistry::registerConsoleBuiltins() {
    auto voidType = HIRType::makeVoid();

    builtinGlobals_.insert("console");

    globalTable_[{"console", "log"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_log", voidType);

    globalTable_[{"console", "error"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_error", voidType);

    globalTable_[{"console", "warn"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_warn", voidType);

    globalTable_[{"console", "info"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_info", voidType);

    globalTable_[{"console", "debug"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_debug", voidType);

    globalTable_[{"console", "trace"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_trace", voidType);

    globalTable_[{"console", "dir"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_dir", voidType);

    globalTable_[{"console", "table"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_table", voidType);

    globalTable_[{"console", "time"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_time", voidType);

    globalTable_[{"console", "timeEnd"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_timeEnd", voidType);

    globalTable_[{"console", "timeLog"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_timeLog", voidType);

    globalTable_[{"console", "count"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_count", voidType);

    globalTable_[{"console", "countReset"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_countReset", voidType);

    globalTable_[{"console", "group"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_group", voidType);

    globalTable_[{"console", "groupEnd"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_groupEnd", voidType);

    globalTable_[{"console", "clear"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_clear", voidType);

    globalTable_[{"console", "assert"}] =
        BuiltinResolution::makeRuntimeCall("ts_console_assert", voidType);
}

void BuiltinRegistry::registerJSONBuiltins() {
    auto stringType = HIRType::makeString();
    auto anyType = HIRType::makeAny();

    builtinGlobals_.insert("JSON");

    globalTable_[{"JSON", "parse"}] =
        BuiltinResolution::makeRuntimeCall("ts_json_parse", anyType);

    globalTable_[{"JSON", "stringify"}] =
        BuiltinResolution::makeRuntimeCall("ts_json_stringify", stringType);
}

MethodResolution BuiltinRegistry::resolveMethod(HIRTypeKind receiverType,
                                                 const std::string& methodName,
                                                 int argCount) const {
    auto it = methodTable_.find({receiverType, methodName});
    if (it != methodTable_.end()) {
        const auto& resolution = it->second;
        // Check arg count if specified
        if (argCount >= 0 && resolution.expectedArgCount >= 0 &&
            argCount != resolution.expectedArgCount) {
            // Arg count mismatch - fall back to dynamic
            return MethodResolution::makeDynamic();
        }
        return resolution;
    }
    return MethodResolution::makeDynamic();
}

BuiltinResolution BuiltinRegistry::resolveGlobalBuiltin(const std::string& globalName,
                                                         const std::string& methodName,
                                                         int argCount) const {
    auto it = globalTable_.find({globalName, methodName});
    if (it != globalTable_.end()) {
        return it->second;
    }
    return BuiltinResolution::makeUnknown();
}

bool BuiltinRegistry::isBuiltinType(HIRTypeKind kind) const {
    switch (kind) {
        case HIRTypeKind::Array:
        case HIRTypeKind::String:
        case HIRTypeKind::Object:
        case HIRTypeKind::Map:
        case HIRTypeKind::Set:
            return true;
        default:
            return false;
    }
}

bool BuiltinRegistry::isBuiltinGlobal(const std::string& name) const {
    return builtinGlobals_.count(name) > 0;
}

} // namespace ts::hir
