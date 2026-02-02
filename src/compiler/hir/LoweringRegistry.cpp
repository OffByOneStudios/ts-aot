#include "LoweringRegistry.h"
#include "../extensions/ExtensionLoader.h"
#include <spdlog/spdlog.h>

namespace hir {

bool LoweringRegistry::builtinsRegistered_ = false;
bool LoweringRegistry::extensionsRegistered_ = false;

LoweringRegistry& LoweringRegistry::instance() {
    static LoweringRegistry instance;
    if (!builtinsRegistered_) {
        builtinsRegistered_ = true;  // Set flag BEFORE calling registerBuiltins to prevent recursion
        instance.registerBuiltinsImpl();
    }
    return instance;
}

void LoweringRegistry::registerLowering(const std::string& hirFuncName, LoweringSpec spec) {
    registry_[hirFuncName] = std::move(spec);
}

const LoweringSpec* LoweringRegistry::lookup(const std::string& hirFuncName) const {
    auto it = registry_.find(hirFuncName);
    if (it != registry_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool LoweringRegistry::hasLowering(const std::string& hirFuncName) const {
    return registry_.find(hirFuncName) != registry_.end();
}

void LoweringRegistry::clear() {
    registry_.clear();
    builtinsRegistered_ = false;  // Allow re-registration after clear
    extensionsRegistered_ = false;
}

void LoweringRegistry::registerBuiltins() {
    // Public API - get the instance and register
    // Note: This is safe because instance() sets builtinsRegistered_ = true BEFORE
    // calling registerBuiltinsImpl(), so we won't recurse.
    auto& inst = instance();
    inst.registerBuiltinsImpl();
}

void LoweringRegistry::registerBuiltinsImpl() {
    // Use 'this' directly - we're called from instance() on the static instance
    auto& reg = *this;

    // ========================================
    // Console functions
    // ========================================
    reg.registerLowering("ts_console_log",
        lowering("ts_console_log")
            .returnsVoid()
            .boxedArg()
            .build());

    reg.registerLowering("ts_console_error",
        lowering("ts_console_error")
            .returnsVoid()
            .boxedArg()
            .build());

    reg.registerLowering("ts_console_warn",
        lowering("ts_console_warn")
            .returnsVoid()
            .boxedArg()
            .build());

    reg.registerLowering("ts_console_info",
        lowering("ts_console_info")
            .returnsVoid()
            .boxedArg()
            .build());

    reg.registerLowering("ts_console_debug",
        lowering("ts_console_debug")
            .returnsVoid()
            .boxedArg()
            .build());

    reg.registerLowering("ts_console_dir",
        lowering("ts_console_dir")
            .returnsVoid()
            .boxedArg()
            .build());

    reg.registerLowering("ts_console_table",
        lowering("ts_console_table")
            .returnsVoid()
            .boxedArg()
            .build());

    reg.registerLowering("ts_console_trace",
        lowering("ts_console_trace")
            .returnsVoid()
            .boxedArg()
            .build());

    reg.registerLowering("ts_console_assert",
        lowering("ts_console_assert")
            .returnsVoid()
            .boxedArg()    // condition
            .boxedArg()    // message
            .build());

    reg.registerLowering("ts_console_count",
        lowering("ts_console_count")
            .returnsVoid()
            .ptrArg()      // label string
            .build());

    reg.registerLowering("ts_console_count_reset",
        lowering("ts_console_count_reset")
            .returnsVoid()
            .ptrArg()      // label string
            .build());

    reg.registerLowering("ts_console_time",
        lowering("ts_console_time")
            .returnsVoid()
            .ptrArg()      // label string
            .build());

    reg.registerLowering("ts_console_time_end",
        lowering("ts_console_time_end")
            .returnsVoid()
            .ptrArg()      // label string
            .build());

    reg.registerLowering("ts_console_time_log",
        lowering("ts_console_time_log")
            .returnsVoid()
            .ptrArg()      // label string
            .boxedArg()    // data
            .build());

    reg.registerLowering("ts_console_group",
        lowering("ts_console_group")
            .returnsVoid()
            .ptrArg()      // label string
            .build());

    reg.registerLowering("ts_console_group_collapsed",
        lowering("ts_console_group_collapsed")
            .returnsVoid()
            .ptrArg()      // label string
            .build());

    reg.registerLowering("ts_console_group_end",
        lowering("ts_console_group_end")
            .returnsVoid()
            .build());

    reg.registerLowering("ts_console_clear",
        lowering("ts_console_clear")
            .returnsVoid()
            .build());

    // ========================================
    // Math functions (using libm directly where possible)
    // ========================================
    reg.registerLowering("ts_math_abs",
        lowering("ts_math_abs")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_floor",
        lowering("floor")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_ceil",
        lowering("ceil")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_round",
        lowering("round")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_trunc",
        lowering("trunc")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_sqrt",
        lowering("sqrt")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_cbrt",
        lowering("cbrt")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_sin",
        lowering("sin")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_cos",
        lowering("cos")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_tan",
        lowering("tan")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_asin",
        lowering("asin")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_acos",
        lowering("acos")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_atan",
        lowering("atan")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_atan2",
        lowering("atan2")
            .returnsF64()
            .f64Arg()   // y
            .f64Arg()   // x
            .build());

    reg.registerLowering("ts_math_sinh",
        lowering("sinh")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_cosh",
        lowering("cosh")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_tanh",
        lowering("tanh")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_asinh",
        lowering("asinh")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_acosh",
        lowering("acosh")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_atanh",
        lowering("atanh")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_exp",
        lowering("exp")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_expm1",
        lowering("expm1")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_log",
        lowering("log")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_log10",
        lowering("log10")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_log2",
        lowering("log2")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_log1p",
        lowering("log1p")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_pow",
        lowering("pow")
            .returnsF64()
            .f64Arg()   // base
            .f64Arg()   // exponent
            .build());

    reg.registerLowering("ts_math_hypot",
        lowering("hypot")
            .returnsF64()
            .f64Arg()   // x
            .f64Arg()   // y
            .build());

    reg.registerLowering("ts_math_random",
        lowering("ts_math_random")
            .returnsF64()
            .build());

    reg.registerLowering("ts_math_sign",
        lowering("ts_math_sign")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_fround",
        lowering("ts_math_fround")
            .returnsF64()
            .f64Arg()
            .build());

    reg.registerLowering("ts_math_clz32",
        lowering("ts_math_clz32")
            .returnsI64()
            .i64Arg()
            .build());

    reg.registerLowering("ts_math_imul",
        lowering("ts_math_imul")
            .returnsI64()
            .i64Arg()
            .i64Arg()
            .build());

    // ts_math_min and ts_math_max need variadic or array handling - skip for now

    // ========================================
    // Array methods
    // ========================================
    reg.registerLowering("ts_array_create",
        lowering("ts_array_create")
            .returnsPtr()
            .build());

    reg.registerLowering("ts_array_push",
        lowering("ts_array_push")
            .returnsI64()
            .ptrArg()      // array
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_array_pop",
        lowering("ts_array_pop")
            .returnsBoxed()
            .ptrArg()      // array
            .build());

    reg.registerLowering("ts_array_shift",
        lowering("ts_array_shift")
            .returnsBoxed()
            .ptrArg()      // array
            .build());

    reg.registerLowering("ts_array_unshift",
        lowering("ts_array_unshift")
            .returnsI64()
            .ptrArg()      // array
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_array_get",
        lowering("ts_array_get")
            .returnsBoxed()
            .ptrArg()      // array
            .i64Arg()      // index
            .build());

    reg.registerLowering("ts_array_set",
        lowering("ts_array_set")
            .returnsVoid()
            .ptrArg()      // array
            .i64Arg()      // index
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_array_length",
        lowering("ts_array_length")
            .returnsI64()
            .ptrArg()      // array
            .build());

    reg.registerLowering("ts_array_join",
        lowering("ts_array_join")
            .returnsPtr()
            .ptrArg()      // array
            .ptrArg()      // separator
            .build());

    reg.registerLowering("ts_array_reverse",
        lowering("ts_array_reverse")
            .returnsPtr()
            .ptrArg()      // array
            .build());

    reg.registerLowering("ts_array_slice",
        lowering("ts_array_slice")
            .returnsPtr()
            .ptrArg()      // array
            .i64Arg()      // start
            .i64Arg()      // end
            .build());

    reg.registerLowering("ts_array_concat",
        lowering("ts_array_concat")
            .returnsPtr()
            .ptrArg()      // array1
            .ptrArg()      // array2
            .build());

    reg.registerLowering("ts_array_includes",
        lowering("ts_array_includes")
            .returnsBool()
            .ptrArg()      // array
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_array_indexOf",
        lowering("ts_array_indexOf")
            .returnsI64()
            .ptrArg()      // array
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_array_lastIndexOf",
        lowering("ts_array_lastIndexOf")
            .returnsI64()
            .ptrArg()      // array
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_array_find",
        lowering("ts_array_find")
            .returnsBoxed()
            .ptrArg()      // array
            .ptrArg()      // callback
            .build());

    reg.registerLowering("ts_array_findIndex",
        lowering("ts_array_findIndex")
            .returnsI64()
            .ptrArg()      // array
            .ptrArg()      // callback
            .build());

    reg.registerLowering("ts_array_findLast",
        lowering("ts_array_findLast")
            .returnsBoxed()
            .ptrArg()      // array
            .ptrArg()      // callback
            .build());

    reg.registerLowering("ts_array_findLastIndex",
        lowering("ts_array_findLastIndex")
            .returnsI64()
            .ptrArg()      // array
            .ptrArg()      // callback
            .build());

    reg.registerLowering("ts_array_every",
        lowering("ts_array_every")
            .returnsBool()
            .ptrArg()      // array
            .ptrArg()      // callback
            .build());

    reg.registerLowering("ts_array_some",
        lowering("ts_array_some")
            .returnsBool()
            .ptrArg()      // array
            .ptrArg()      // callback
            .build());

    reg.registerLowering("ts_array_filter",
        lowering("ts_array_filter")
            .returnsPtr()
            .ptrArg()      // array
            .ptrArg()      // callback
            .build());

    reg.registerLowering("ts_array_map",
        lowering("ts_array_map")
            .returnsPtr()
            .ptrArg()      // array
            .ptrArg()      // callback
            .build());

    reg.registerLowering("ts_array_forEach",
        lowering("ts_array_forEach")
            .returnsVoid()
            .ptrArg()      // array
            .ptrArg()      // callback
            .build());

    reg.registerLowering("ts_array_reduce",
        lowering("ts_array_reduce")
            .returnsBoxed()
            .ptrArg()      // array
            .ptrArg()      // callback
            .boxedArg()    // initial value
            .build());

    reg.registerLowering("ts_array_reduceRight",
        lowering("ts_array_reduceRight")
            .returnsBoxed()
            .ptrArg()      // array
            .ptrArg()      // callback
            .boxedArg()    // initial value
            .build());

    reg.registerLowering("ts_array_flat",
        lowering("ts_array_flat")
            .returnsPtr()
            .ptrArg()      // array
            .i64Arg()      // depth
            .build());

    reg.registerLowering("ts_array_flatMap",
        lowering("ts_array_flatMap")
            .returnsPtr()
            .ptrArg()      // array
            .ptrArg()      // callback
            .build());

    reg.registerLowering("ts_array_fill",
        lowering("ts_array_fill")
            .returnsPtr()
            .ptrArg()      // array
            .boxedArg()    // value
            .i64Arg()      // start
            .i64Arg()      // end
            .build());

    reg.registerLowering("ts_array_copyWithin",
        lowering("ts_array_copyWithin")
            .returnsPtr()
            .ptrArg()      // array
            .i64Arg()      // target
            .i64Arg()      // start
            .i64Arg()      // end
            .build());

    reg.registerLowering("ts_array_sort",
        lowering("ts_array_sort")
            .returnsPtr()
            .ptrArg()      // array
            .ptrArg()      // compareFn (optional, can be null)
            .build());

    reg.registerLowering("ts_array_splice",
        lowering("ts_array_splice")
            .returnsPtr()
            .ptrArg()      // array
            .i64Arg()      // start
            .i64Arg()      // deleteCount
            .ptrArg()      // items array (or null)
            .build());

    // ES2023 non-mutating methods
    reg.registerLowering("ts_array_toReversed",
        lowering("ts_array_toReversed")
            .returnsPtr()
            .ptrArg()      // array
            .build());

    reg.registerLowering("ts_array_toSorted",
        lowering("ts_array_toSorted")
            .returnsPtr()
            .ptrArg()      // array
            .ptrArg()      // compareFn
            .build());

    reg.registerLowering("ts_array_toSpliced",
        lowering("ts_array_toSpliced")
            .returnsPtr()
            .ptrArg()      // array
            .i64Arg()      // start
            .i64Arg()      // deleteCount
            .ptrArg()      // items array
            .build());

    reg.registerLowering("ts_array_with",
        lowering("ts_array_with")
            .returnsPtr()
            .ptrArg()      // array
            .i64Arg()      // index
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_array_at",
        lowering("ts_array_at")
            .returnsBoxed()
            .ptrArg()      // array
            .i64Arg()      // index
            .build());

    // ========================================
    // String methods
    // ========================================
    reg.registerLowering("ts_string_length",
        lowering("ts_string_length")
            .returnsI64()
            .ptrArg()      // string
            .build());

    reg.registerLowering("ts_string_charAt",
        lowering("ts_string_charAt")
            .returnsPtr()
            .ptrArg()      // string
            .i64Arg()      // index
            .build());

    reg.registerLowering("ts_string_charCodeAt",
        lowering("ts_string_charCodeAt")
            .returnsI64()
            .ptrArg()      // string
            .i64Arg()      // index
            .build());

    reg.registerLowering("ts_string_codePointAt",
        lowering("ts_string_codePointAt")
            .returnsI64()
            .ptrArg()      // string
            .i64Arg()      // index
            .build());

    reg.registerLowering("ts_string_concat",
        lowering("ts_string_concat")
            .returnsPtr()
            .ptrArg()      // string1
            .ptrArg()      // string2
            .build());

    reg.registerLowering("ts_string_includes",
        lowering("ts_string_includes")
            .returnsBool()
            .ptrArg()      // string
            .ptrArg()      // searchString
            .build());

    reg.registerLowering("ts_string_startsWith",
        lowering("ts_string_startsWith")
            .returnsBool()
            .ptrArg()      // string
            .ptrArg()      // searchString
            .build());

    reg.registerLowering("ts_string_endsWith",
        lowering("ts_string_endsWith")
            .returnsBool()
            .ptrArg()      // string
            .ptrArg()      // searchString
            .build());

    reg.registerLowering("ts_string_indexOf",
        lowering("ts_string_indexOf")
            .returnsI64()
            .ptrArg()      // string
            .ptrArg()      // searchString
            .build());

    reg.registerLowering("ts_string_lastIndexOf",
        lowering("ts_string_lastIndexOf")
            .returnsI64()
            .ptrArg()      // string
            .ptrArg()      // searchString
            .build());

    reg.registerLowering("ts_string_slice",
        lowering("ts_string_slice")
            .returnsPtr()
            .ptrArg()      // string
            .i64Arg()      // start
            .i64Arg()      // end
            .build());

    reg.registerLowering("ts_string_substring",
        lowering("ts_string_substring")
            .returnsPtr()
            .ptrArg()      // string
            .i64Arg()      // start
            .i64Arg()      // end
            .build());

    reg.registerLowering("ts_string_substr",
        lowering("ts_string_substr")
            .returnsPtr()
            .ptrArg()      // string
            .i64Arg()      // start
            .i64Arg()      // length
            .build());

    reg.registerLowering("ts_string_toLowerCase",
        lowering("ts_string_toLowerCase")
            .returnsPtr()
            .ptrArg()      // string
            .build());

    reg.registerLowering("ts_string_toUpperCase",
        lowering("ts_string_toUpperCase")
            .returnsPtr()
            .ptrArg()      // string
            .build());

    reg.registerLowering("ts_string_trim",
        lowering("ts_string_trim")
            .returnsPtr()
            .ptrArg()      // string
            .build());

    reg.registerLowering("ts_string_trimStart",
        lowering("ts_string_trimStart")
            .returnsPtr()
            .ptrArg()      // string
            .build());

    reg.registerLowering("ts_string_trimEnd",
        lowering("ts_string_trimEnd")
            .returnsPtr()
            .ptrArg()      // string
            .build());

    reg.registerLowering("ts_string_padStart",
        lowering("ts_string_padStart")
            .returnsPtr()
            .ptrArg()      // string
            .i64Arg()      // targetLength
            .ptrArg()      // padString
            .build());

    reg.registerLowering("ts_string_padEnd",
        lowering("ts_string_padEnd")
            .returnsPtr()
            .ptrArg()      // string
            .i64Arg()      // targetLength
            .ptrArg()      // padString
            .build());

    reg.registerLowering("ts_string_repeat",
        lowering("ts_string_repeat")
            .returnsPtr()
            .ptrArg()      // string
            .i64Arg()      // count
            .build());

    reg.registerLowering("ts_string_replace",
        lowering("ts_string_replace")
            .returnsPtr()
            .ptrArg()      // string
            .ptrArg()      // searchValue
            .ptrArg()      // replaceValue
            .build());

    reg.registerLowering("ts_string_replaceAll",
        lowering("ts_string_replaceAll")
            .returnsPtr()
            .ptrArg()      // string
            .ptrArg()      // searchValue
            .ptrArg()      // replaceValue
            .build());

    reg.registerLowering("ts_string_split",
        lowering("ts_string_split")
            .returnsPtr()
            .ptrArg()      // string
            .ptrArg()      // separator
            .build());

    reg.registerLowering("ts_string_normalize",
        lowering("ts_string_normalize")
            .returnsPtr()
            .ptrArg()      // string
            .ptrArg()      // form
            .build());

    reg.registerLowering("ts_string_at",
        lowering("ts_string_at")
            .returnsPtr()
            .ptrArg()      // string
            .i64Arg()      // index
            .build());

    // ========================================
    // Map methods
    // ========================================
    reg.registerLowering("ts_map_create",
        lowering("ts_map_create")
            .returnsPtr()
            .build());

    reg.registerLowering("ts_map_get",
        lowering("ts_map_get")
            .returnsBoxed()
            .ptrArg()      // map
            .boxedArg()    // key
            .build());

    reg.registerLowering("ts_map_set",
        lowering("ts_map_set")
            .returnsPtr()
            .ptrArg()      // map
            .boxedArg()    // key
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_map_has",
        lowering("ts_map_has")
            .returnsBool()
            .ptrArg()      // map
            .boxedArg()    // key
            .build());

    reg.registerLowering("ts_map_delete",
        lowering("ts_map_delete")
            .returnsBool()
            .ptrArg()      // map
            .boxedArg()    // key
            .build());

    reg.registerLowering("ts_map_clear",
        lowering("ts_map_clear")
            .returnsVoid()
            .ptrArg()      // map
            .build());

    reg.registerLowering("ts_map_size",
        lowering("ts_map_size")
            .returnsI64()
            .ptrArg()      // map
            .build());

    reg.registerLowering("ts_map_keys",
        lowering("ts_map_keys")
            .returnsPtr()
            .ptrArg()      // map
            .build());

    reg.registerLowering("ts_map_values",
        lowering("ts_map_values")
            .returnsPtr()
            .ptrArg()      // map
            .build());

    reg.registerLowering("ts_map_entries",
        lowering("ts_map_entries")
            .returnsPtr()
            .ptrArg()      // map
            .build());

    reg.registerLowering("ts_map_forEach",
        lowering("ts_map_forEach")
            .returnsVoid()
            .ptrArg()      // map
            .ptrArg()      // callback
            .build());

    // ========================================
    // Set methods
    // ========================================
    reg.registerLowering("ts_set_create",
        lowering("ts_set_create")
            .returnsPtr()
            .build());

    reg.registerLowering("ts_set_add",
        lowering("ts_set_add")
            .returnsPtr()
            .ptrArg()      // set
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_set_has",
        lowering("ts_set_has")
            .returnsBool()
            .ptrArg()      // set
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_set_delete",
        lowering("ts_set_delete")
            .returnsBool()
            .ptrArg()      // set
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_set_clear",
        lowering("ts_set_clear")
            .returnsVoid()
            .ptrArg()      // set
            .build());

    reg.registerLowering("ts_set_size",
        lowering("ts_set_size")
            .returnsI64()
            .ptrArg()      // set
            .build());

    reg.registerLowering("ts_set_keys",
        lowering("ts_set_keys")
            .returnsPtr()
            .ptrArg()      // set
            .build());

    reg.registerLowering("ts_set_values",
        lowering("ts_set_values")
            .returnsPtr()
            .ptrArg()      // set
            .build());

    reg.registerLowering("ts_set_entries",
        lowering("ts_set_entries")
            .returnsPtr()
            .ptrArg()      // set
            .build());

    reg.registerLowering("ts_set_forEach",
        lowering("ts_set_forEach")
            .returnsVoid()
            .ptrArg()      // set
            .ptrArg()      // callback
            .build());

    // ========================================
    // JSON methods
    // ========================================
    reg.registerLowering("ts_json_parse",
        lowering("ts_json_parse")
            .returnsBoxed()
            .ptrArg()      // json string
            .build());

    reg.registerLowering("ts_json_stringify",
        lowering("ts_json_stringify")
            .returnsPtr()
            .ptrArg()      // object
            .ptrArg()      // replacer (null)
            .ptrArg()      // space (null)
            .build());

    // ========================================
    // Object methods
    // ========================================
    reg.registerLowering("ts_object_keys",
        lowering("ts_object_keys")
            .returnsPtr()
            .ptrArg()      // object
            .build());

    reg.registerLowering("ts_object_values",
        lowering("ts_object_values")
            .returnsPtr()
            .ptrArg()      // object
            .build());

    reg.registerLowering("ts_object_entries",
        lowering("ts_object_entries")
            .returnsPtr()
            .ptrArg()      // object
            .build());

    reg.registerLowering("ts_object_assign",
        lowering("ts_object_assign")
            .returnsPtr()
            .ptrArg()      // target
            .ptrArg()      // source
            .build());

    reg.registerLowering("ts_object_create",
        lowering("ts_object_create")
            .returnsPtr()
            .ptrArg()      // prototype
            .build());

    reg.registerLowering("ts_object_freeze",
        lowering("ts_object_freeze")
            .returnsPtr()
            .ptrArg()      // object
            .build());

    reg.registerLowering("ts_object_seal",
        lowering("ts_object_seal")
            .returnsPtr()
            .ptrArg()      // object
            .build());

    reg.registerLowering("ts_object_is_frozen",
        lowering("ts_object_is_frozen")
            .returnsBool()
            .ptrArg()      // object
            .build());

    reg.registerLowering("ts_object_is_sealed",
        lowering("ts_object_is_sealed")
            .returnsBool()
            .ptrArg()      // object
            .build());

    reg.registerLowering("ts_object_is_extensible",
        lowering("ts_object_is_extensible")
            .returnsBool()
            .ptrArg()      // object
            .build());

    reg.registerLowering("ts_object_prevent_extensions",
        lowering("ts_object_prevent_extensions")
            .returnsPtr()
            .ptrArg()      // object
            .build());

    reg.registerLowering("ts_object_from_entries",
        lowering("ts_object_from_entries")
            .returnsPtr()
            .ptrArg()      // entries array
            .build());

    reg.registerLowering("ts_object_has_own",
        lowering("ts_object_has_own")
            .returnsBool()
            .ptrArg()      // object
            .ptrArg()      // key string
            .build());

    // ========================================
    // BigInt operations
    // ========================================
    reg.registerLowering("ts_bigint_create_str",
        lowering("ts_bigint_create_str")
            .returnsPtr()
            .ptrArg()      // string representation
            .build());

    reg.registerLowering("ts_bigint_add",
        lowering("ts_bigint_add")
            .returnsPtr()
            .ptrArg()      // a
            .ptrArg()      // b
            .build());

    reg.registerLowering("ts_bigint_sub",
        lowering("ts_bigint_sub")
            .returnsPtr()
            .ptrArg()      // a
            .ptrArg()      // b
            .build());

    reg.registerLowering("ts_bigint_mul",
        lowering("ts_bigint_mul")
            .returnsPtr()
            .ptrArg()      // a
            .ptrArg()      // b
            .build());

    reg.registerLowering("ts_bigint_div",
        lowering("ts_bigint_div")
            .returnsPtr()
            .ptrArg()      // a
            .ptrArg()      // b
            .build());

    reg.registerLowering("ts_bigint_mod",
        lowering("ts_bigint_mod")
            .returnsPtr()
            .ptrArg()      // a
            .ptrArg()      // b
            .build());

    // ========================================
    // Symbol
    // ========================================
    reg.registerLowering("ts_symbol_create",
        lowering("ts_symbol_create")
            .returnsPtr()
            .ptrArg()      // description (optional)
            .build());

    reg.registerLowering("ts_symbol_for",
        lowering("ts_symbol_for")
            .returnsPtr()
            .ptrArg()      // key
            .build());

    reg.registerLowering("ts_symbol_keyFor",
        lowering("ts_symbol_keyFor")
            .returnsPtr()
            .ptrArg()      // symbol
            .build());

    // ========================================
    // Error
    // ========================================
    reg.registerLowering("ts_error_create",
        lowering("ts_error_create")
            .returnsBoxed()  // Already boxed TsValue*
            .ptrArg()        // message string
            .build());

    // ========================================
    // typeof / instanceof
    // ========================================
    reg.registerLowering("ts_typeof",
        lowering("ts_typeof")
            .returnsPtr()
            .boxedArg()    // value
            .build());

    reg.registerLowering("ts_instanceof",
        lowering("ts_instanceof")
            .returnsBool()
            .ptrArg()      // object
            .ptrArg()      // constructor
            .build());
}

void LoweringRegistry::registerFromExtensions() {
    if (extensionsRegistered_) return;
    extensionsRegistered_ = true;

    const auto& extRegistry = ext::ExtensionRegistry::instance();
    const auto& contracts = extRegistry.getContracts();

    int registeredCount = 0;

    for (const auto& contract : contracts) {
        // Register lowerings from type methods
        for (const auto& [typeName, typeDef] : contract.types) {
            for (const auto& [methodName, method] : typeDef.methods) {
                if (!method.lowering || method.call.empty()) continue;

                // Skip if already registered (builtins take precedence)
                if (hasLowering(method.call)) continue;

                LoweringSpecBuilder builder(method.call);

                // Set return type
                switch (method.lowering->returns) {
                    case ext::LoweringType::Void:
                        builder.returnsVoid();
                        break;
                    case ext::LoweringType::Ptr:
                        builder.returnsPtr();
                        break;
                    case ext::LoweringType::I64:
                        builder.returnsI64();
                        break;
                    case ext::LoweringType::I32:
                        builder.returns([](llvm::LLVMContext& ctx) {
                            return llvm::Type::getInt32Ty(ctx);
                        });
                        break;
                    case ext::LoweringType::F64:
                        builder.returnsF64();
                        break;
                    case ext::LoweringType::I1:
                        builder.returnsBool();
                        break;
                    case ext::LoweringType::Boxed:
                        builder.returnsBoxed();
                        break;
                }

                // Set argument types
                for (const auto& argType : method.lowering->args) {
                    switch (argType) {
                        case ext::LoweringType::Ptr:
                            builder.ptrArg();
                            break;
                        case ext::LoweringType::I64:
                            builder.i64Arg();
                            break;
                        case ext::LoweringType::I32:
                            builder.i32Arg();
                            break;
                        case ext::LoweringType::F64:
                            builder.f64Arg();
                            break;
                        case ext::LoweringType::I1:
                            builder.boolArg();
                            break;
                        case ext::LoweringType::Boxed:
                            builder.boxedArg();
                            break;
                        case ext::LoweringType::Void:
                            // Void doesn't make sense for args, ignore
                            break;
                    }
                }

                registerLowering(method.call, builder.build());
                registeredCount++;
                SPDLOG_DEBUG("Registered lowering from extension: {} -> {}", method.call, typeName + "." + methodName);
            }
        }

        // Register lowerings from standalone functions
        for (const auto& [funcName, func] : contract.functions) {
            if (!func.lowering || func.call.empty()) continue;

            if (hasLowering(func.call)) continue;

            LoweringSpecBuilder builder(func.call);

            switch (func.lowering->returns) {
                case ext::LoweringType::Void:
                    builder.returnsVoid();
                    break;
                case ext::LoweringType::Ptr:
                    builder.returnsPtr();
                    break;
                case ext::LoweringType::I64:
                    builder.returnsI64();
                    break;
                case ext::LoweringType::I32:
                    builder.returns([](llvm::LLVMContext& ctx) {
                        return llvm::Type::getInt32Ty(ctx);
                    });
                    break;
                case ext::LoweringType::F64:
                    builder.returnsF64();
                    break;
                case ext::LoweringType::I1:
                    builder.returnsBool();
                    break;
                case ext::LoweringType::Boxed:
                    builder.returnsBoxed();
                    break;
            }

            for (const auto& argType : func.lowering->args) {
                switch (argType) {
                    case ext::LoweringType::Ptr:
                        builder.ptrArg();
                        break;
                    case ext::LoweringType::I64:
                        builder.i64Arg();
                        break;
                    case ext::LoweringType::I32:
                        builder.i32Arg();
                        break;
                    case ext::LoweringType::F64:
                        builder.f64Arg();
                        break;
                    case ext::LoweringType::I1:
                        builder.boolArg();
                        break;
                    case ext::LoweringType::Boxed:
                        builder.boxedArg();
                        break;
                    case ext::LoweringType::Void:
                        break;
                }
            }

            registerLowering(func.call, builder.build());
            registeredCount++;
            SPDLOG_DEBUG("Registered lowering from extension: {} (function {})", func.call, funcName);
        }
    }

    if (registeredCount > 0) {
        SPDLOG_INFO("Registered {} lowerings from extension contracts", registeredCount);
    }
}

} // namespace hir
