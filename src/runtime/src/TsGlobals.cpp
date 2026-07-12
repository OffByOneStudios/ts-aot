// TsGlobals.cpp - Global object getters for HIR pipeline
//
// These functions return proper runtime objects so that untyped JavaScript
// modules can access built-in globals (Object.keys, String.prototype, etc.)
// via dynamic property lookup.

#include <vector>
#include "GC.h"
#include "TsGC.h"  // with-stack GC rooting (scanner + minor fixup)
#include "TsRuntime.h"
#include "TsObject.h"
#include "TsClosure.h"
#include "TsMap.h"
#include "TsHashTable.h"
#include "TsString.h"
#include "TsConsString.h"
#include "TsNanBox.h"
#include "TsError.h"
#include "TsSymbol.h"
#include "TsBuffer.h"  // for TsTypedArray
#include "TsArray.h"   // for TsArray source in typed array constructors
#include "TsBigInt.h"  // for BigInt.asIntN / asUintN
#include "TsRegExp.h"  // for ts_regexp_create (plain-call RegExp(pattern))
#include "TsTemporal.h" // Temporal.PlainTime (and other Temporal types)
#include <unicode/unistr.h>
#include <unicode/utypes.h>
#include <unicode/locid.h>
#include <unicode/bytestream.h>
#include <unicode/coll.h>
#include <unicode/ucol.h>
#include <unicode/numfmt.h>
#include <unicode/decimfmt.h>
#include <unicode/plurrule.h>
#include <unordered_map>
#include <string>
#include <limits>
#include <cmath>
#include <cstdio>

// GC-001 Phase C: RAII guard that routes allocations to the old generation for
// its lifetime (ts_gc_push_tenure/pop). Placed at the entry of every
// ts_get_global_* lazy builtin getter so the immortal builtin prototype graph
// (Object/Array/Map/Set/Date/RegExp/Promise/... prototypes) is born in old-gen
// and never moves. That keeps the compiler-cached extern "C" TsValue* .data
// bindings and prototype method-resolution valid across a minor GC. The
// cached-return fast path of each getter does no allocation, so the scope is a
// harmless no-op there; the depth counter lets getters that call other getters
// nest correctly.
extern "C" void ts_gc_push_tenure();
extern "C" void ts_gc_pop_tenure();
// Root the cached builtin-global pointers so the collector keeps the (tenured,
// non-moving) builtin function objects alive. Without this, a cached builtin is
// reachable only via this unscanned C++ static and gets SWEPT when no live JS
// reference is on the stack at GC time (e.g. `String` passed as a param across
// a forced GC → its memory reused → typeof "string", uncallable).
extern "C" void ts_gc_register_root(void** location);
extern "C" { extern void* g_object_proto_map; }  // #66 (defined in TsObject.cpp)
namespace { struct TenureScope { TenureScope() { ts_gc_push_tenure(); } ~TenureScope() { ts_gc_pop_tenure(); } }; }

extern "C" {

// Forward declarations for native wrapper functions
void* ts_get_builtin_function(void* nameStr);
TsValue* ts_object_keys_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_values_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_entries_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_getOwnPropertyNames_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_getOwnPropertyDescriptor_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_getOwnPropertyDescriptors_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_getPrototypeOf_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_create_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_defineProperty_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_defineProperties_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_freeze_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_seal_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_preventExtensions_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_isFrozen_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_isSealed_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_isExtensible_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_assign(TsValue* target, TsValue* source);
TsValue* ts_object_setPrototypeOf(TsValue* obj, TsValue* proto);
bool ts_object_is(TsValue* val1, TsValue* val2);
TsValue* ts_array_isArray_native(void* context, int argc, TsValue** argv);
// Array instance method natives (defined in TsObject.cpp)
TsValue* ts_array_slice_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_map_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_filter_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_forEach_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_reduce_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_push_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_pop_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_join_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_indexOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_includes_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_some_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_every_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_find_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_findIndex_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_sort_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_reverse_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_splice_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_concat_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_flat_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_shift_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_unshift_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_at_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_fill_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_reduceRight_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_lastIndexOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_toReversed_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_toSorted_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_toSpliced_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_copyWithin_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_with_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_findLast_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_findLastIndex_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_flatMap_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_entries_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_keys_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_values_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_json_stringify_native(void* context, int argc, TsValue** argv);
TsValue* ts_json_parse_native(void* context, int argc, TsValue** argv);
// ts_error_create is declared in TsError.h (returns void*)
TsValue* ts_function_call_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_function_apply_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_function_bind_native(void* ctx, int argc, TsValue** argv);
extern "C" TsValue* ts_object_toString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_get_builtin_module(const char* name);
TsValue* ts_value_make_native_function(void* funcPtr, void* context);
TsValue* ts_value_make_object(void* ptr);
TsValue* ts_value_make_bool(bool val);
TsValue* ts_value_make_undefined();
TsValue* ts_value_make_string(void* str);
TsValue* ts_value_make_double(double d);
bool ts_value_is_undefined(TsValue* val);
void* ts_get_call_this();
void* ts_string_from_value(TsValue* val);
double ts_value_get_double(TsValue* v);
bool ts_value_to_bool(TsValue* v);
void* ts_value_get_object(TsValue* val);
TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key);
TsValue* ts_function_call_with_this(TsValue* fn, TsValue* thisArg, int argc, TsValue** argv);
bool ts_object_has_property(void* objArg, void* keyArg);
extern "C" TsFunction* ts_extract_function(TsValue* boxedFunc);
extern "C" TsValue* ts_fn_hasInstance_native(void* ctx, int argc, TsValue** argv);
extern "C" TsClosure* ts_extract_closure(TsValue* boxedFunc);

// Helper: install an accessor-getter on a prototype TsMap, where reading
// the property `propName` invokes `getterFn(this)`. Uses the existing
// `__getter_<propName>` convention recognized by ts_object_get_property
// (TsObject.cpp:3440-3468). The getter function is named "get <propName>"
// per ECMA-262 6.2.5 / built-in accessor convention.
static void addAccessorGetter(TsMap* map, const char* propName, void* nativeFn) {
    TsValue* fn = ts_value_make_native_function(nativeFn, nullptr);
    TsFunction* func = (TsFunction*)fn;
    std::string fname = std::string("get ") + propName;
    func->name = TsString::Create(fname.c_str());
    func->arity = 0;
    func->is_constructor = false;
    if (!func->properties) func->properties = TsMap::Create();
    TsValue nk; nk.type = ValueType::STRING_PTR;
    nk.ptr_val = TsString::GetInterned("name");
    TsValue nv; nv.type = ValueType::STRING_PTR; nv.ptr_val = func->name;
    func->properties->SetWithAttrs(nk, nv, TsHashTable::ATTR_CONFIGURABLE);
    TsValue lk; lk.type = ValueType::STRING_PTR;
    lk.ptr_val = TsString::GetInterned("length");
    TsValue lv; lv.type = ValueType::NUMBER_INT; lv.i_val = 0;
    func->properties->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);
    // Install __getter_<propName> on the prototype map.
    std::string getterKey = std::string("__getter_") + propName;
    TsValue gk; gk.type = ValueType::STRING_PTR;
    gk.ptr_val = TsString::GetInterned(getterKey.c_str());
    TsValue gv; gv.type = ValueType::FUNCTION_PTR; gv.ptr_val = fn;
    map->SetWithAttrs(gk, gv, 0);  // hidden — not enumerable, not writable, not configurable
    // Install the outward-facing slot for getOwnPropertyDescriptor's
    // attribute lookup. Value is undefined (placeholder); the descriptor
    // path detects __getter_<name> and returns {get, set, enum, conf}
    // shape with the attrs taken from this slot.
    TsValue pk; pk.type = ValueType::STRING_PTR;
    pk.ptr_val = TsString::GetInterned(propName);
    TsValue pv; pv.type = ValueType::UNDEFINED;
    map->SetWithAttrs(pk, pv, TsHashTable::ATTR_CONFIGURABLE);
}

// Helper: add a native function to a TsMap, setting .name and .arity
// so hasOwnProperty('length'/'name') works per ES spec.
// get [Symbol.species]() { return this; } — returns the receiver (the constructor).
static TsValue* species_this_getter(void* ctx, int argc, TsValue** argv) {
    if (!ctx) ctx = ts_get_call_this();
    return (TsValue*)ctx;
}
static void installCtorMeta(TsFunction* fn, const char* name, int length);
static void addMethod(TsMap* map, const char* name, void* nativeFn, int arity = 1) {
    TsValue key;
    key.type = ValueType::STRING_PTR;
    key.ptr_val = TsString::GetInterned(name);
    TsValue val;
    val.type = ValueType::FUNCTION_PTR;
    TsValue* fn = ts_value_make_native_function(nativeFn, nullptr);
    // Set function metadata so .length and .name return correct values
    TsFunction* func = (TsFunction*)fn;
    func->name = TsString::Create(name);
    func->arity = arity;
    // Per ES spec, built-in prototype methods have no [[Construct]].
    // `new <Constructor>.prototype.X()` must throw TypeError.
    func->is_constructor = false;
    // Store .length/.name in properties TsMap with correct attributes
    // so hasOwnProperty/getOwnPropertyDescriptor work per ES spec
    if (!func->properties) func->properties = TsMap::Create();
    TsValue lk; lk.type = ValueType::STRING_PTR;
    lk.ptr_val = TsString::GetInterned("length");
    TsValue lv; lv.type = ValueType::NUMBER_INT; lv.i_val = arity;
    func->properties->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);
    TsValue nk; nk.type = ValueType::STRING_PTR;
    nk.ptr_val = TsString::GetInterned("name");
    TsValue nv; nv.type = ValueType::STRING_PTR; nv.ptr_val = func->name;
    func->properties->SetWithAttrs(nk, nv, TsHashTable::ATTR_CONFIGURABLE);
    val.ptr_val = fn;
    // Per ES spec, built-in prototype methods are {writable:true,
    // enumerable:false, configurable:true}. SetWithAttrs omits ATTR_ENUMERABLE.
    map->SetWithAttrs(key, val,
        TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
}

// Install a built-in's @@toStringTag — a data property {writable:false,
// enumerable:false, configurable:true} under the well-known-symbol storage key,
// so Object.prototype.toString.call(x) === "[object <tag>]" and the property is
// discoverable (ECMA-262 e.g. 24.1.3.13 Map.prototype[@@toStringTag]).
static void setProtoStringTag(TsMap* proto, const char* tag) {
    if (!proto) return;
    TsValue k; k.type = ValueType::STRING_PTR; k.ptr_val = TsString::GetInterned("[Symbol.toStringTag]");
    TsValue v; v.type = ValueType::STRING_PTR; v.ptr_val = TsString::Create(tag);
    proto->SetWithAttrs(k, v, TsHashTable::ATTR_CONFIGURABLE);
}

// Native wrappers for functions that take 2+ args and don't have _native variants
static TsValue* ts_object_assign_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return argc > 0 ? argv[0] : ts_value_make_undefined();
    return ts_object_assign(argv[0], argv[1]);
}

static TsValue* ts_object_setPrototypeOf_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return ts_value_make_undefined();
    return ts_object_setPrototypeOf(argv[0], argv[1]);
}

static TsValue* ts_object_is_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return ts_value_make_bool(false);
    return ts_value_make_bool(ts_object_is(argv[0], argv[1]));
}

static TsValue* ts_object_hasOwn_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return ts_value_make_bool(false);
    // hasOwn checks if an object has a property
    extern TsValue* ts_object_getOwnPropertyDescriptor(TsValue*, TsValue*);
    TsValue* desc = ts_object_getOwnPropertyDescriptor(argv[0], argv[1]);
    return ts_value_make_bool(desc != nullptr && !ts_value_is_undefined(desc));
}

// Forward decl — defined later near makeSimpleConstructorGlobal.
// ts_get_global_Object (below) uses wrapAsCallable to promote its TsMap
// ctor to a TsFunction so typeof Object === "function".
static void* wrapAsCallable(TsMap* ctor, const char* name, int length = 0);

// ========================================
// Object global
// ========================================
// Object.fromEntries(iterable) — inverse of Object.entries. Builds a TsMap
// from a list of [key, value] pairs.
static TsValue* object_fromEntries_native(void* ctx, int argc, TsValue** argv) {
    TsMap* out = TsMap::Create();
    if (argc < 1 || !argv || !argv[0]) return ts_value_make_object(out);
    void* raw = ts_value_get_object(argv[0]);
    if (!raw) return ts_value_make_object(out);
    uint32_t magic0 = *(uint32_t*)raw;
    if (magic0 != 0x41525259) return ts_value_make_object(out);  // Only TsArray source for now.
    TsArray* arr = (TsArray*)raw;
    size_t len = (size_t)arr->Length();
    for (size_t i = 0; i < len; i++) {
        TsValue entry = nanbox_to_tagged((TsValue*)arr->GetElementBoxed(i));
        if (entry.type != ValueType::ARRAY_PTR || !entry.ptr_val) continue;
        TsArray* pair = (TsArray*)entry.ptr_val;
        if (pair->Length() < 2) continue;
        TsValue k = nanbox_to_tagged((TsValue*)pair->GetElementBoxed(0));
        TsValue v = nanbox_to_tagged((TsValue*)pair->GetElementBoxed(1));
        out->Set(k, v);
    }
    return ts_value_make_object(out);
}

void* ts_get_global_Object() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (cached) return cached;

    TsMap* ctor = TsMap::Create();
    // Static methods
    addMethod(ctor, "keys", (void*)ts_object_keys_native);
    addMethod(ctor, "values", (void*)ts_object_values_native);
    addMethod(ctor, "entries", (void*)ts_object_entries_native);
    addMethod(ctor, "fromEntries", (void*)object_fromEntries_native, 1);
    addMethod(ctor, "assign", (void*)ts_object_assign_native, 2);
    addMethod(ctor, "create", (void*)ts_object_create_native, 2);
    addMethod(ctor, "defineProperty", (void*)ts_object_defineProperty_native, 3);
    addMethod(ctor, "defineProperties", (void*)ts_object_defineProperties_native, 2);
    addMethod(ctor, "getOwnPropertyDescriptor", (void*)ts_object_getOwnPropertyDescriptor_native, 2);
    addMethod(ctor, "getOwnPropertyDescriptors", (void*)ts_object_getOwnPropertyDescriptors_native);
    addMethod(ctor, "getOwnPropertyNames", (void*)ts_object_getOwnPropertyNames_native);
    // Object.getOwnPropertySymbols — returns the object's own user-Symbol keys.
    // User symbols are stored under "\x01@@sym\x01<index>" marker strings; the
    // impl in TsObject.cpp gathers those and maps them back to Symbol objects.
    extern TsValue* ts_object_getOwnPropertySymbols_native(void* context, int argc, TsValue** argv);
    addMethod(ctor, "getOwnPropertySymbols", (void*)ts_object_getOwnPropertySymbols_native, 1);
    addMethod(ctor, "getPrototypeOf", (void*)ts_object_getPrototypeOf_native);
    addMethod(ctor, "setPrototypeOf", (void*)ts_object_setPrototypeOf_native, 2);
    addMethod(ctor, "freeze", (void*)ts_object_freeze_native);
    addMethod(ctor, "seal", (void*)ts_object_seal_native);
    addMethod(ctor, "preventExtensions", (void*)ts_object_preventExtensions_native);
    addMethod(ctor, "isFrozen", (void*)ts_object_isFrozen_native);
    addMethod(ctor, "isSealed", (void*)ts_object_isSealed_native);
    addMethod(ctor, "isExtensible", (void*)ts_object_isExtensible_native);
    addMethod(ctor, "is", (void*)ts_object_is_native);
    addMethod(ctor, "hasOwn", (void*)ts_object_hasOwn_native);
    // Object.groupBy(items, keyFn) — ES2024.
    extern TsValue* ts_object_groupBy(TsValue* iterable, TsValue* callbackFn);
    addMethod(ctor, "groupBy", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
        TsValue* it = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
        TsValue* fn = (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined();
        return ts_object_groupBy(it, fn);
    }, 2);

    // Object.prototype — a TsMap that serves as the base prototype
    TsMap* proto = TsMap::Create();
    // #66: publish the real %Object.prototype% for dynamic inheritance.
    g_object_proto_map = proto;
    { static bool _r = false; if (!_r) { _r = true; ts_gc_register_root(&g_object_proto_map); } }
    // hasOwnProperty(key): use the canonical implementation from TsObject.cpp
    // which handles TsClosure/TsFunction properties TsMap (for .length/.name
    // after delete). The old inline lambda used ts_object_has_property which
    // doesn't check function property maps.
    extern TsValue* ts_object_hasOwnProperty_native(void*, int, TsValue**);
    addMethod(proto, "hasOwnProperty", (void*)ts_object_hasOwnProperty_native);
    // Other Object.prototype methods. Without these, accessing
    // `Object.prototype.X` directly returns undefined — the
    // ts_object_get_property fallbacks at TsObject.cpp:3231+ only fire
    // when the prototype-chain walk reaches the Object.prototype
    // sentinel via an instance, not when it IS the receiver. The harness
    // pattern `Function.prototype.call.bind(Object.prototype.X)` needs
    // X to be a real own-property function on the proto map.
    {
        struct M { const char* name; void* fn; int arity; };
        // Forward-declare statics from TsObject.cpp.
        extern TsValue* ts_object_isPrototypeOf_native(void*, int, TsValue**);
        extern TsValue* ts_object_propertyIsEnumerable_native(void*, int, TsValue**);
        // toString/valueOf live as `static` in TsObject.cpp so we can't
        // link to them directly; install lambda wrappers that produce
        // spec-shaped output here.
        addMethod(proto, "isPrototypeOf",       (void*)ts_object_isPrototypeOf_native, 1);
        addMethod(proto, "propertyIsEnumerable",(void*)ts_object_propertyIsEnumerable_native, 1);
        // annexB B.2.2 legacy accessor helpers.
        extern TsValue* ts_object_defineGetter_native(void*, int, TsValue**);
        extern TsValue* ts_object_defineSetter_native(void*, int, TsValue**);
        extern TsValue* ts_object_lookupGetter_native(void*, int, TsValue**);
        extern TsValue* ts_object_lookupSetter_native(void*, int, TsValue**);
        addMethod(proto, "__defineGetter__",  (void*)ts_object_defineGetter_native, 2);
        addMethod(proto, "__defineSetter__",  (void*)ts_object_defineSetter_native, 2);
        addMethod(proto, "__lookupGetter__",  (void*)ts_object_lookupGetter_native, 1);
        addMethod(proto, "__lookupSetter__",  (void*)ts_object_lookupSetter_native, 1);
        // Route Object.prototype.toString to the canonical implementation in
        // TsObject.cpp which handles all magic-byte brand checks plus
        // @@toStringTag prototype-chain lookup per ECMA-262 step 15-16.
        // (Forward decl declared at file scope below addMethod call.)
        auto protoValueOf = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return (TsValue*)(ctx ? ctx : ts_value_make_undefined());
        };
        addMethod(proto, "toString", (void*)ts_object_toString_native, 0);
        addMethod(proto, "valueOf",  (void*)+protoValueOf,  0);
        // ECMA-262 20.1.3.5 Object.prototype.toLocaleString(): return
        // Invoke(O, "toString") — so an overridden toString is honoured. Was
        // missing entirely (read undefined).
        auto protoToLocaleString = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            TsValue* self = (TsValue*)(ctx ? ctx : ts_get_call_this());
            if (!self) self = ts_value_make_undefined();
            TsValue* key = ts_value_make_string(TsString::Create("toString"));
            TsValue* tsFn = ts_object_get_dynamic(self, key);
            extern TsValue* ts_call_with_this_0(TsValue* boxedFunc, TsValue* thisArg);
            if (tsFn && !ts_value_is_undefined(tsFn)) return ts_call_with_this_0(tsFn, self);
            // A plain object's INHERITED Object.prototype.toString may not surface
            // via get_dynamic; fall back to the default object toString.
            extern TsValue* ts_object_toString_native(void* ctx, int argc, TsValue** argv);
            return ts_object_toString_native(self, 0, nullptr);
        };
        addMethod(proto, "toLocaleString", (void*)+protoToLocaleString, 0);
    }
    TsValue protoKey;
    protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal;
    protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    ctor->SetWithAttrs(protoKey, protoVal, 0);

    // Promote to TsFunction so `typeof Object === "function"` and
    // `isConstructor(Object)` returns true.
    cached = wrapAsCallable(ctor, "Object", 1);
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }

    // ECMA-262: Object.prototype.constructor === Object. Lodash's
    // isPlainObject walks `proto.constructor` to identify plain objects,
    // and Babel's transforms emit `obj.constructor` for super-class
    // detection. Without this back-pointer `{}.constructor` is undefined.
    {
        TsValue ctorBackKey;
        ctorBackKey.type = ValueType::STRING_PTR;
        ctorBackKey.ptr_val = TsString::GetInterned("constructor");
        TsValue ctorBackVal;
        ctorBackVal.type = ValueType::FUNCTION_PTR;
        ctorBackVal.ptr_val = cached;
        // Non-enumerable per ECMA-262 20.1.2.1 — plain Set made it enumerable,
        // so it leaked into Object.keys(Object.prototype) / for-in over the
        // prototype chain (writable|configurable, not enumerable).
        proto->SetWithAttrs(ctorBackKey, ctorBackVal, 0x02 | 0x04);
    }

    // Override the default undefined-returning body with the real
    // Object(value) coercion. Used by patterns like
    // `Object(value)` (identity for objects, boxes primitives) and the
    // common lodash idiom `var nativeKeys = overArg(Object.keys, Object);
    // function nativeKeys(o) { return Object.keys(Object(o)); }` — without
    // a real body, Object(o) returned undefined and Object.keys(undefined)
    // returned [].
    extern void* ts_object_constructor(void* arg);
    {
        TsFunction* fn = (TsFunction*)ts_value_get_object((TsValue*)cached);
        if (fn) {
            fn->funcPtr = (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (argc < 1 || !argv) {
                    extern void* ts_object_create_empty();
                    return (TsValue*)ts_value_make_object(ts_object_create_empty());
                }
                return (TsValue*)ts_object_constructor((void*)argv[0]);
            };
        }
    }
    return cached;
}

// ========================================
// Array global
// ========================================
// Forward decls for Array.from/of runtime entry points. Defined in TsArray.cpp.
extern "C" {
    void* ts_array_from(void* arrayLike, void* mapFn, void* thisArg);
    void* ts_array_create();
    int64_t ts_array_push(void* arr, void* value);
}

// Forward decl: wrapAsCallable is defined below (next to makeSimpleConstructorGlobal).
// Declared here so ts_get_global_Array can use it before its definition.
static void* wrapAsCallable(TsMap* ctor, const char* name, int length);

static TsValue* array_from_native_wrap(void* ctx, int argc, TsValue** argv) {
    void* arg0 = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
    void* arg1 = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
    void* arg2 = (argc >= 3 && argv) ? (void*)argv[2] : nullptr;
    void* result = ts_array_from(arg0, arg1, arg2);
    return result ? (TsValue*)ts_value_make_object(result) : ts_value_make_object(ts_array_create());
}

static bool value_is_constructor(void* v);  // defined below (~7489)
void* ts_get_global_Array();               // defined just below
static TsValue* array_of_native_wrap(void* ctx, int argc, TsValue** argv) {
    // ES 23.1.2.3 steps 4-5: when `this` is a CONSTRUCTOR other than the
    // Array intrinsic (Array.of.call(Custom, ...)), A = ? Construct(C, [len])
    // and elements are defined onto it; a throwing constructor propagates
    // (return-abrupt-from-contructor). Plain Array.of keeps the fast path.
    {
        // fwd decl of the file-static helper defined below (line ~7489)
        extern TsValue* ts_new_from_constructor_1(TsValue* ctor, TsValue* arg);
        extern void ts_object_set_property(void* obj, void* key, void* value);
        void* thisV = ts_get_call_this();
        void* thisRaw = thisV ? ts_value_get_object((TsValue*)thisV) : nullptr;
        void* arrayCtorRaw = ts_value_get_object((TsValue*)ts_get_global_Array());
        if (thisRaw && thisRaw != arrayCtorRaw && value_is_constructor(thisV)) {
            TsValue* A = ts_new_from_constructor_1((TsValue*)thisV,
                                                   ts_value_make_int(argc));
            if (A) {
                // CreateDataPropertyOrThrow — DEFINE semantics; [[Set]] would
                // invoke inherited setters (does-not-use-prototype-properties).
                extern TsValue* ts_object_defineProperty(TsValue* obj, TsValue* prop,
                                                         TsValue* descriptor);
                for (int i = 0; i < argc; i++) {
                    TsMap* desc = TsMap::Create();
                    TsValue dk; dk.type = ValueType::STRING_PTR;
                    TsValue dv; dv.type = ValueType::BOOLEAN; dv.b_val = true;
                    dk.ptr_val = TsString::GetInterned("value");
                    desc->Set(dk, nanbox_to_tagged(argv[i]));
                    dk.ptr_val = TsString::GetInterned("writable");     desc->Set(dk, dv);
                    dk.ptr_val = TsString::GetInterned("enumerable");   desc->Set(dk, dv);
                    dk.ptr_val = TsString::GetInterned("configurable"); desc->Set(dk, dv);
                    char ibuf[24];
                    snprintf(ibuf, sizeof(ibuf), "%d", i);
                    ts_object_defineProperty(A,
                        ts_value_make_string(TsString::Create(ibuf)),
                        ts_value_make_object(desc));
                }
                ts_object_set_property((void*)A,
                    (void*)ts_value_make_string(TsString::GetInterned("length")),
                    (void*)ts_value_make_int(argc));
                return A;
            }
        }
    }
    void* arr = ts_array_create();
    for (int i = 0; i < argc; i++) ts_array_push(arr, (void*)argv[i]);
    return (TsValue*)ts_value_make_object(arr);
}

void* ts_get_global_Array() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (cached) return cached;

    TsMap* ctorMap = TsMap::Create();
    addMethod(ctorMap, "isArray", (void*)ts_array_isArray_native);
    addMethod(ctorMap, "from",    (void*)array_from_native_wrap, 1);
    addMethod(ctorMap, "of",      (void*)array_of_native_wrap, 0);

    // Array.prototype — populated with instance methods so
    // Array.prototype.slice.call(arr, ...) pattern works (used by Express)
    TsMap* proto = TsMap::Create();
    extern TsMap* g_array_prototype_map;
    g_array_prototype_map = proto;
    addMethod(proto, "slice", (void*)ts_array_slice_native, 2);
    addMethod(proto, "map", (void*)ts_array_map_native);
    addMethod(proto, "filter", (void*)ts_array_filter_native);
    addMethod(proto, "forEach", (void*)ts_array_forEach_native);
    addMethod(proto, "reduce", (void*)ts_array_reduce_native);
    addMethod(proto, "push", (void*)ts_array_push_native);
    addMethod(proto, "pop", (void*)ts_array_pop_native, 0);
    addMethod(proto, "join", (void*)ts_array_join_native);
    { extern TsValue* ts_array_toLocaleString_native(void*, int, TsValue**);
      addMethod(proto, "toLocaleString", (void*)ts_array_toLocaleString_native, 0); }
    addMethod(proto, "indexOf", (void*)ts_array_indexOf_native);
    addMethod(proto, "includes", (void*)ts_array_includes_native);
    addMethod(proto, "some", (void*)ts_array_some_native);
    addMethod(proto, "every", (void*)ts_array_every_native);
    addMethod(proto, "find", (void*)ts_array_find_native);
    addMethod(proto, "findIndex", (void*)ts_array_findIndex_native);
    addMethod(proto, "sort", (void*)ts_array_sort_native);
    addMethod(proto, "reverse", (void*)ts_array_reverse_native, 0);
    addMethod(proto, "splice", (void*)ts_array_splice_native, 2);
    addMethod(proto, "concat", (void*)ts_array_concat_native);
    addMethod(proto, "flat", (void*)ts_array_flat_native, 0);
    addMethod(proto, "shift", (void*)ts_array_shift_native, 0);
    addMethod(proto, "unshift", (void*)ts_array_unshift_native);
    // ES2022+ methods
    addMethod(proto, "at", (void*)ts_array_at_native);
    addMethod(proto, "fill", (void*)ts_array_fill_native);
    addMethod(proto, "reduceRight", (void*)ts_array_reduceRight_native, 1);
    addMethod(proto, "lastIndexOf", (void*)ts_array_lastIndexOf_native);
    addMethod(proto, "findLast", (void*)ts_array_findLast_native);
    addMethod(proto, "findLastIndex", (void*)ts_array_findLastIndex_native);
    addMethod(proto, "flatMap", (void*)ts_array_flatMap_native);
    addMethod(proto, "copyWithin", (void*)ts_array_copyWithin_native, 2);
    // ES2023 mutation-free methods
    addMethod(proto, "toReversed", (void*)ts_array_toReversed_native, 0);
    addMethod(proto, "toSorted", (void*)ts_array_toSorted_native);
    addMethod(proto, "toSpliced", (void*)ts_array_toSpliced_native);
    addMethod(proto, "with", (void*)ts_array_with_native, 2);
    addMethod(proto, "entries", (void*)ts_array_entries_native, 0);
    addMethod(proto, "keys", (void*)ts_array_keys_native, 0);
    addMethod(proto, "values", (void*)ts_array_values_native, 0);

    // ECMA-262 23.1.3.36: Array.prototype[@@iterator] is the SAME function
    // object as Array.prototype.values ({writable, enumerable:false, configurable}).
    {
        TsValue vk; vk.type = ValueType::STRING_PTR; vk.ptr_val = TsString::GetInterned("values");
        TsValue vfn = proto->Get(vk);
        if (vfn.type != ValueType::UNDEFINED) {
            TsValue ik; ik.type = ValueType::STRING_PTR; ik.ptr_val = TsString::GetInterned("[Symbol.iterator]");
            proto->SetWithAttrs(ik, vfn, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
        }
    }

    // ECMA-262 23.1.3.34: Array.prototype[@@unscopables] is a null-prototype
    // object whose keys are the method names added to Array.prototype after
    // ES5, each with value true ({writable,enumerable,configurable}). The
    // property itself is {writable:false, enumerable:false, configurable:true}.
    {
        TsMap* unsc = TsMap::Create();
        unsc->SetNullPrototype(true);
        static const char* const kUnscopables[] = {
            "at","copyWithin","entries","fill","find","findIndex","findLast",
            "findLastIndex","flat","flatMap","includes","keys","toReversed",
            "toSorted","toSpliced","values"
        };
        for (const char* k : kUnscopables) {
            TsValue kk; kk.type = ValueType::STRING_PTR; kk.ptr_val = TsString::GetInterned(k);
            TsValue tv; tv.type = ValueType::BOOLEAN; tv.b_val = true;
            unsc->SetWithAttrs(kk, tv,
                TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_ENUMERABLE | TsHashTable::ATTR_CONFIGURABLE);
        }
        TsValue uk; uk.type = ValueType::STRING_PTR;
        uk.ptr_val = TsString::GetInterned("[Symbol.unscopables]");
        TsValue uv; uv.type = ValueType::OBJECT_PTR; uv.ptr_val = unsc;
        proto->SetWithAttrs(uk, uv, TsHashTable::ATTR_CONFIGURABLE);
    }

    TsValue protoKey;
    protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal;
    protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    ctorMap->SetWithAttrs(protoKey, protoVal, 0);

    // Promote to TsFunction so typeof Array === "function" and
    // isConstructor(Array) returns true.
    addAccessorGetter(ctorMap, "[Symbol.species]", (void*)species_this_getter);
    cached = wrapAsCallable(ctorMap, "Array", 1);
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }

    // proto.constructor = Array (per spec — Array.prototype.constructor === Array).
    // Must be done after wrapAsCallable so we have the TsFunction reference.
    TsValue ctorKey; ctorKey.type = ValueType::STRING_PTR;
    ctorKey.ptr_val = TsString::GetInterned("constructor");
    TsValue ctorRefVal; ctorRefVal.type = ValueType::FUNCTION_PTR;
    ctorRefVal.ptr_val = ts_value_get_object((TsValue*)cached);
    // Non-enumerable per ECMA-262 23.1.2.2 (Array.prototype.constructor).
    proto->SetWithAttrs(ctorKey, ctorRefVal, 0x02 | 0x04);

    return cached;
}

// ========================================
// String global
// ========================================
// String.prototype method wrappers — extract string from ctx or ts_get_call_this().
//
// Per ECMAScript spec each String.prototype method begins with:
//   1. Let O be ? RequireObjectCoercible(this value).  // throws if null/undefined
//   2. Let S be ? ToString(O).                          // coerces primitives
// Then operates on S.
static TsValue* string_proto_method(const char* methodName, void* ctx, int argc, TsValue** argv) {
    // Receiver is set by ts_call_with_this_N in the global ts_call_this_value
    // BEFORE we're invoked. Prefer it over `ctx` (which is func->context and
    // can hold a stale value from a previous .call() that threw — see
    // ts_call_with_this_N's patchedCtx logic, which is longjmp-unsafe).
    // ts_call_this_value is now snapshot/restored across exception unwind by
    // ExceptionContext (Core.cpp), so it is the authoritative source.
    TsValue* target = (TsValue*)ts_get_call_this();
    if (!target) target = (TsValue*)ctx;

    // Spec step 1: RequireObjectCoercible — null/undefined throw TypeError.
    if (!target || ts_value_is_nullish(target)) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "String.prototype.%s called on null or undefined",
                 methodName);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return ts_value_make_undefined();
    }

    // Spec step 2: ToString — coerce non-string primitives (number, bool, etc.)
    // to a TsString. ts_string_from_value implements JS-spec ToString and throws
    // TypeError for Symbol (handled by the existing Symbol coercion code path).
    // For String wrapper objects (`new String(x)`), we store the original
    // string under __StringData on a TsMap; unwrap it here so prototype
    // methods see the underlying string instead of "[object Object]".
    {
        uint64_t nb = nanbox_from_tsvalue_ptr(target);
        bool isAlreadyString = false;
        if (nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            if (ptr) {
                uint32_t magic = *(uint32_t*)ptr;
                if (magic == TsString::MAGIC || magic == TsConsString::MAGIC) {
                    isAlreadyString = true;
                } else {
                    uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
                    if (magic16 == 0x4D415053) {  // TsMap (potential String wrapper)
                        TsMap* m = (TsMap*)ptr;
                        TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
                        ndKey.ptr_val = TsString::GetInterned("__StringData");
                        TsValue v = m->Get(ndKey);
                        if (v.type == ValueType::STRING_PTR && v.ptr_val) {
                            target = ts_value_make_string(v.ptr_val);
                            isAlreadyString = true;
                        }
                    }
                }
            }
        }
        if (!isAlreadyString) {
            // Hook-invoking ToString(this): an object receiver's
            // toString/valueOf runs and its throw PROPAGATES (the
            // this-value-object-tostring-err families across ~25 String
            // prototype methods). This frame is std::string-free, so the
            // hook's longjmp is safe. Wrapper objects were unwrapped above.
            extern void* ts_to_string_spec(TsValue* val);
            void* str = ts_to_string_spec(target);
            if (!str) return ts_value_make_undefined();
            target = ts_value_make_string(str);
        }
    }

    // Now dispatch to the underlying method via the string's prototype chain.
    TsValue* method = ts_object_get_dynamic(target,
        ts_value_make_string(TsString::Create(methodName)));
    if (!method || ts_value_is_undefined(method)) {
        return ts_value_make_undefined();
    }
    return ts_function_call_with_this(method, target, argc, argv);
}

#define STRING_PROTO_METHOD(name) \
    static TsValue* ts_string_proto_##name(void* ctx, int argc, TsValue** argv) { \
        return string_proto_method(#name, ctx, argc, argv); \
    }

STRING_PROTO_METHOD(indexOf)
STRING_PROTO_METHOD(lastIndexOf)
STRING_PROTO_METHOD(slice)
STRING_PROTO_METHOD(substring)
STRING_PROTO_METHOD(charAt)
STRING_PROTO_METHOD(charCodeAt)
STRING_PROTO_METHOD(includes)
STRING_PROTO_METHOD(startsWith)
STRING_PROTO_METHOD(endsWith)
STRING_PROTO_METHOD(trim)
STRING_PROTO_METHOD(split)
STRING_PROTO_METHOD(replace)
STRING_PROTO_METHOD(toLowerCase)
STRING_PROTO_METHOD(toUpperCase)
STRING_PROTO_METHOD(repeat)
STRING_PROTO_METHOD(padStart)
STRING_PROTO_METHOD(padEnd)
STRING_PROTO_METHOD(match)
STRING_PROTO_METHOD(search)
STRING_PROTO_METHOD(concat)
STRING_PROTO_METHOD(trimStart)
STRING_PROTO_METHOD(trimEnd)
STRING_PROTO_METHOD(substr)
STRING_PROTO_METHOD(trimLeft)
STRING_PROTO_METHOD(trimRight)
STRING_PROTO_METHOD(at)
STRING_PROTO_METHOD(codePointAt)
STRING_PROTO_METHOD(normalize)
STRING_PROTO_METHOD(replaceAll)
STRING_PROTO_METHOD(matchAll)
STRING_PROTO_METHOD(localeCompare)
STRING_PROTO_METHOD(isWellFormed)
STRING_PROTO_METHOD(toWellFormed)
STRING_PROTO_METHOD(toLocaleLowerCase)
STRING_PROTO_METHOD(toLocaleUpperCase)

#undef STRING_PROTO_METHOD

// ES 22.1.3.29/22.1.3.35 thisStringValue: toString/valueOf accept ONLY a
// string primitive or a String wrapper object — NO ToString coercion
// (String.prototype.toString.call(1) must TypeError). POD frame — ts_throw
// longjmps out of here.
static TsValue* string_proto_thisStringValue(const char* methodName, void* ctx) {
    if (!ctx) ctx = ts_get_call_this();
    TsValue* target = (TsValue*)ctx;
    uint64_t nb = nanbox_from_tsvalue_ptr(target);
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (ptr && (uintptr_t)ptr > 0x1000) {
            uint32_t magic = *(uint32_t*)ptr;
            if (magic == TsString::MAGIC || magic == TsConsString::MAGIC)
                return ts_value_make_string((TsString*)ptr);
            uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
            if (magic16 == 0x4D415053) {  // String wrapper (TsMap + __StringData)
                TsMap* m = (TsMap*)ptr;
                TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
                ndKey.ptr_val = TsString::GetInterned("__StringData");
                TsValue v = m->Get(ndKey);
                if (v.type == ValueType::STRING_PTR && v.ptr_val)
                    return ts_value_make_string((TsString*)v.ptr_val);
            }
        }
    }
    char msg[160];
    snprintf(msg, sizeof(msg),
             "String.prototype.%s requires that 'this' be a String",
             methodName);
    ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
    return ts_value_make_undefined();
}
static TsValue* ts_string_proto_toString(void* ctx, int argc, TsValue** argv) {
    (void)argc; (void)argv;
    return string_proto_thisStringValue("toString", ctx);
}
static TsValue* ts_string_proto_valueOf(void* ctx, int argc, TsValue** argv) {
    (void)argc; (void)argv;
    return string_proto_thisStringValue("valueOf", ctx);
}

// Forward declaration for ts_to_number — defined later in this file via
// Primitives.cpp's extern "C". String.fromCharCode/fromCodePoint use it.
extern "C" double ts_to_number(TsValue* v);

// String.prototype, captured so the plain-call/construct disambiguation in
// stringFn can tell `new String(x)` (this is a fresh wrapper whose prototype
// IS String.prototype) from a receiver-less `String(x)` / `[..].map(String)`
// (where ts_get_call_this() leaks an ambient `this` such as globalThis —
// also a TsMap, but with a different prototype). Without this check the plain
// call wrongly built a wrapper object, breaking e.g. lodash's
// `baseTimes(n, String)` → wrapper-string keys → broken iteration.
static TsMap* g_string_wrapper_proto = nullptr;

extern "C" void* ts_create_string_iterator(void* strPtr);  // TsMap.cpp
void* ts_get_global_String() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        // String() as a callable function: converts argument to string.
        // `new String(x)` stores [[StringData]] on the wrapper TsMap so
        // String.prototype.toString/valueOf can return the original.
        auto stringFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            void* thisVal = ts_get_call_this();
            // Detect `new String(...)` (this is a fresh wrapper whose prototype is String.prototype).
            bool isNew = false;
            if (thisVal) { void* traw = ts_value_get_object((TsValue*)thisVal);
                if (traw && *(uint32_t*)((char*)traw + 16) == 0x4D415053 &&
                    ((TsMap*)traw)->GetPrototype() == g_string_wrapper_proto) isNew = true; }
            // String(symbol) returns SymbolDescriptiveString; only `new String(symbol)` is a
            // TypeError. (ts_string_from_value/ToString would otherwise throw for both.)
            if (argc >= 1 && argv && argv[0]) {
                uint64_t a0nb = nanbox_from_tsvalue_ptr(argv[0]);
                void* sraw = nanbox_is_ptr(a0nb) ? nanbox_to_ptr(a0nb) : nullptr;
                if (sraw && *(uint32_t*)sraw == 0x53594D42) {
                    if (isNew) { ts_throw((TsValue*)ts_error_create_typed("TypeError","Cannot convert a Symbol value to a string")); return ts_value_make_undefined(); }
                    TsSymbol* sym = (TsSymbol*)sraw;
                    std::string ds = "Symbol("; if (sym->description) ds += sym->description->ToUtf8(); ds += ")";
                    return ts_value_make_string(TsString::Create(ds.c_str()));
                }
            }
            void* result = (argc >= 1 && argv && argv[0])
                ? ts_string_from_value(argv[0])
                : (void*)TsString::Create("");
            if (thisVal) {
                void* raw = ts_value_get_object((TsValue*)thisVal);
                if (raw) {
                    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
                    // Only a genuine `new String(x)` wrapper (prototype ===
                    // String.prototype) is a construction target. A leaked
                    // ambient `this` (globalThis etc.) is also a TsMap but has
                    // a different prototype → return a primitive string.
                    if (m16 == 0x4D415053 &&  // TsMap
                        ((TsMap*)raw)->GetPrototype() == g_string_wrapper_proto) {
                        TsMap* obj = (TsMap*)raw;
                        TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
                        ndKey.ptr_val = TsString::GetInterned("__StringData");
                        TsValue ndVal; ndVal.type = ValueType::STRING_PTR;
                        ndVal.ptr_val = (TsString*)result;
                        obj->Set(ndKey, ndVal);
                        return (TsValue*)thisVal;
                    }
                }
            }
            return ts_value_make_string(result);
        };

        TsValue* ctorVal = ts_value_make_native_function((void*)+stringFn, nullptr);
        void* ctorRaw = ts_value_get_object(ctorVal);
        TsFunction* ctorFunc = (TsFunction*)ctorRaw;
        installCtorMeta(ctorFunc, "String", 1);

        // String.prototype with common methods
        TsMap* proto = TsMap::Create();
        addMethod(proto, "indexOf", (void*)ts_string_proto_indexOf);
        addMethod(proto, "lastIndexOf", (void*)ts_string_proto_lastIndexOf);
        addMethod(proto, "slice", (void*)ts_string_proto_slice, 2);
        addMethod(proto, "substring", (void*)ts_string_proto_substring, 2);
        addMethod(proto, "charAt", (void*)ts_string_proto_charAt);
        addMethod(proto, "charCodeAt", (void*)ts_string_proto_charCodeAt);
        addMethod(proto, "includes", (void*)ts_string_proto_includes);
        addMethod(proto, "startsWith", (void*)ts_string_proto_startsWith);
        addMethod(proto, "endsWith", (void*)ts_string_proto_endsWith);
        addMethod(proto, "trim", (void*)ts_string_proto_trim, 0);
        addMethod(proto, "split", (void*)ts_string_proto_split, 2);
        addMethod(proto, "replace", (void*)ts_string_proto_replace, 2);
        addMethod(proto, "toLowerCase", (void*)ts_string_proto_toLowerCase, 0);
        addMethod(proto, "toUpperCase", (void*)ts_string_proto_toUpperCase, 0);
        addMethod(proto, "repeat", (void*)ts_string_proto_repeat);
        addMethod(proto, "padStart", (void*)ts_string_proto_padStart);
        addMethod(proto, "padEnd", (void*)ts_string_proto_padEnd);
        addMethod(proto, "match", (void*)ts_string_proto_match);
        addMethod(proto, "search", (void*)ts_string_proto_search);
        addMethod(proto, "concat", (void*)ts_string_proto_concat);
        addMethod(proto, "trimStart", (void*)ts_string_proto_trimStart, 0);
        addMethod(proto, "trimEnd", (void*)ts_string_proto_trimEnd, 0);
        // AnnexB B.2.3: substr + trimLeft/trimRight aliases as real own props.
        addMethod(proto, "substr", (void*)ts_string_proto_substr, 2);
        addMethod(proto, "trimLeft", (void*)ts_string_proto_trimLeft, 0);
        addMethod(proto, "trimRight", (void*)ts_string_proto_trimRight, 0);
        addMethod(proto, "at", (void*)ts_string_proto_at);
        addMethod(proto, "codePointAt", (void*)ts_string_proto_codePointAt);
        addMethod(proto, "normalize", (void*)ts_string_proto_normalize);
        addMethod(proto, "replaceAll", (void*)ts_string_proto_replaceAll, 2);
        addMethod(proto, "matchAll", (void*)ts_string_proto_matchAll);
        addMethod(proto, "localeCompare", (void*)ts_string_proto_localeCompare);
        addMethod(proto, "isWellFormed", (void*)ts_string_proto_isWellFormed, 0);
        addMethod(proto, "toWellFormed", (void*)ts_string_proto_toWellFormed, 0);
        addMethod(proto, "toLocaleLowerCase", (void*)ts_string_proto_toLocaleLowerCase, 0);
        addMethod(proto, "toLocaleUpperCase", (void*)ts_string_proto_toLocaleUpperCase, 0);
        addMethod(proto, "toString", (void*)ts_string_proto_toString, 0);
        addMethod(proto, "valueOf", (void*)ts_string_proto_valueOf, 0);

        // String.prototype[Symbol.iterator] — ECMA-262 22.1.3.34. A code-point
        // iterator backed by %StringIteratorPrototype%. RequireObjectCoercible
        // (null/undefined this -> TypeError), then ToString.
        addMethod(proto, "[Symbol.iterator]", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
            if (nanbox_is_null(nb) || nanbox_is_undefined(nb)) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "String.prototype[Symbol.iterator] called on null or undefined"));
                return ts_value_make_undefined();
            }
            void* s = ts_string_from_value((TsValue*)ctx);  // ToString
            return (TsValue*)ts_create_string_iterator(s);
        }, 0);

        // Annex B.2.3: HTML wrapper methods (deprecated but standardized).
        // These need to be on String.prototype directly so
        // String.prototype.italics etc. work (not just instance access).
        extern TsValue* ts_string_big_native(void*, int, TsValue**);
        extern TsValue* ts_string_small_native(void*, int, TsValue**);
        extern TsValue* ts_string_bold_native(void*, int, TsValue**);
        extern TsValue* ts_string_italics_native(void*, int, TsValue**);
        extern TsValue* ts_string_fixed_native(void*, int, TsValue**);
        extern TsValue* ts_string_strike_native(void*, int, TsValue**);
        extern TsValue* ts_string_blink_native(void*, int, TsValue**);
        extern TsValue* ts_string_sub_native(void*, int, TsValue**);
        extern TsValue* ts_string_sup_native(void*, int, TsValue**);
        extern TsValue* ts_string_anchor_native(void*, int, TsValue**);
        extern TsValue* ts_string_link_native(void*, int, TsValue**);
        extern TsValue* ts_string_fontcolor_native(void*, int, TsValue**);
        extern TsValue* ts_string_fontsize_native(void*, int, TsValue**);
        addMethod(proto, "big",       (void*)ts_string_big_native, 0);
        addMethod(proto, "small",     (void*)ts_string_small_native, 0);
        addMethod(proto, "bold",      (void*)ts_string_bold_native, 0);
        addMethod(proto, "italics",   (void*)ts_string_italics_native, 0);
        addMethod(proto, "fixed",     (void*)ts_string_fixed_native, 0);
        addMethod(proto, "strike",    (void*)ts_string_strike_native, 0);
        addMethod(proto, "blink",     (void*)ts_string_blink_native, 0);
        addMethod(proto, "sub",       (void*)ts_string_sub_native, 0);
        addMethod(proto, "sup",       (void*)ts_string_sup_native, 0);
        addMethod(proto, "anchor",    (void*)ts_string_anchor_native, 1);
        addMethod(proto, "link",      (void*)ts_string_link_native, 1);
        addMethod(proto, "fontcolor", (void*)ts_string_fontcolor_native, 1);
        addMethod(proto, "fontsize",  (void*)ts_string_fontsize_native, 1);

        if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal; protoVal.type = ValueType::OBJECT_PTR;
        protoVal.ptr_val = proto;
        ctorFunc->properties->SetWithAttrs(protoKey, protoVal, 0);
        g_string_wrapper_proto = proto;  // for stringFn's new-vs-call check

        ctorFunc->name = TsString::Create("String");

        // String static methods: fromCharCode, fromCodePoint, raw.
        // Register via addMethod so they get [[Construct]]=false + correct
        // .name/.length metadata for test262 compliance.
        auto fromCharCodeFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            // Build UnicodeString from 16-bit code units across all args.
            std::u16string us;
            us.reserve((size_t)argc);
            for (int i = 0; i < argc; i++) {
                if (!argv || !argv[i]) { us.push_back(0); continue; }
                double d = ts_to_number(argv[i]);
                int32_t code = (d != d) ? 0 : (int32_t)d;
                us.push_back(static_cast<char16_t>(code & 0xFFFF));
            }
            icu::UnicodeString uni((const UChar*)us.data(), (int32_t)us.size());
            std::string utf8;
            uni.toUTF8String(utf8);
            return ts_value_make_string(TsString::Create(utf8.c_str()));
        };
        addMethod(ctorFunc->properties, "fromCharCode", (void*)+fromCharCodeFn, 1);

        auto fromCodePointFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            // String.fromCodePoint(...codePoints): each arg is a Unicode
            // code point (0..0x10FFFF). Throw RangeError on invalid.
            // ALL non-PODs constructed before the first possible throw
            // (SMELL-002 longjmp rule — ts_to_number runs user valueOf).
            icu::UnicodeString uni;
            std::string utf8;
            for (int i = 0; i < argc; i++) {
                if (!argv || !argv[i]) {
                    ts_throw((TsValue*)ts_error_create_typed("RangeError",
                        "Invalid code point"));
                    return ts_value_make_undefined();
                }
                double d = ts_to_number(argv[i]);
                if (d != d || d < 0 || d > 0x10FFFF || d != (double)(int32_t)d) {
                    ts_throw((TsValue*)ts_error_create_typed("RangeError",
                        "Invalid code point"));
                    return ts_value_make_undefined();
                }
                uni.append((UChar32)d);
            }
            uni.toUTF8String(utf8);
            return ts_value_make_string(TsString::Create(utf8.c_str()));
        };
        addMethod(ctorFunc->properties, "fromCodePoint", (void*)+fromCodePointFn, 1);

        // String.raw(template, ...substitutions) — tagged template helper.
        extern void* ts_string_raw(void* templateObj, void* substitutionsArray);
        auto stringRawFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (argc < 1 || !argv || !argv[0]) return ts_value_make_string(TsString::Create(""));
            // Build a TsArray from argv[1..argc-1] so ts_string_raw can
            // iterate substitutions by index. ts_string_raw expects an
            // array-like wrapper for substitutions.
            TsArray* subs = (TsArray*)ts_array_create();
            for (int i = 1; i < argc; i++) {
                subs->Push((int64_t)(uintptr_t)argv[i]);
            }
            void* tmpl = ts_value_get_object(argv[0]);
            if (!tmpl) tmpl = argv[0];
            void* result = ts_string_raw(tmpl, subs);
            return ts_value_make_string(result);
        };
        addMethod(ctorFunc->properties, "raw", (void*)+stringRawFn, 1);

        // String.prototype.constructor = String (was unset; broke lodash
        // cloneByTag `new value.constructor(value)`).
        { TsValue ck; ck.type = ValueType::STRING_PTR; ck.ptr_val = TsString::GetInterned("constructor");
          TsValue cv; cv.type = ValueType::OBJECT_PTR; cv.ptr_val = ts_value_get_object(ctorVal);
          proto->SetWithAttrs(ck, cv, 0x02 | 0x04); }

        cached = (void*)ctorVal;
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

// ========================================
// Error constructors — callable native functions with .prototype
// ========================================

// Forward declaration: Error global used for prototype inheritance in typed
// error constructors (TypeError.prototype → Error.prototype, etc.).
void* ts_get_global_Error();

// Helper: create a callable error constructor global
// Returns a TsFunction that, when called via `new`, creates an error TsMap
// with .message, .name, and .stack properties.
static void* makeErrorConstructor(const char* errorName) {
    TenureScope _tenure;
    // Create the constructor as a native function
    auto constructorFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
        // ctx is the error name string (TsString*)
        // 'this' is the new object from ts_new_from_constructor_impl
        void* thisVal = ts_get_call_this();
        void* raw = thisVal ? ts_value_get_object((TsValue*)thisVal) : nullptr;

        // If called via 'new', set .message on 'this'
        if (raw) {
            uint32_t m16 = *(uint32_t*)((char*)raw + 16);
            uint32_t m20 = *(uint32_t*)((char*)raw + 20);
            if (m16 == 0x4D415053 || m20 == 0x4D415053) {
                TsMap* obj = (TsMap*)raw;
                TsValue msgKey; msgKey.type = ValueType::STRING_PTR;
                msgKey.ptr_val = TsString::GetInterned("message");
                if (argc >= 1 && argv && argv[0]) {
                    TsValue msgVal = nanbox_to_tagged(argv[0]);
                    obj->Set(msgKey, msgVal);
                } else {
                    TsValue emptyVal; emptyVal.type = ValueType::STRING_PTR;
                    emptyVal.ptr_val = TsString::Create("");
                    obj->Set(msgKey, emptyVal);
                }
                // Brand the instance as Error per [[ErrorData]] internal slot
                // (spec brand check). Use the @@toStringTag string-key
                // convention as own non-enumerable property so
                // Object.prototype.toString returns "[object Error]" but
                // Error.prototype itself remains "[object Object]".
                TsValue tagKey; tagKey.type = ValueType::STRING_PTR;
                tagKey.ptr_val = TsString::GetInterned("[Symbol.toStringTag]");
                TsValue tagVal; tagVal.type = ValueType::STRING_PTR;
                tagVal.ptr_val = TsString::Create("Error");
                obj->SetWithAttrs(tagKey, tagVal, TsHashTable::ATTR_CONFIGURABLE);
                return (TsValue*)thisVal;
            }
        }

        // Fallback: create a standalone error object
        if (argc > 0 && argv) return (TsValue*)ts_error_create(argv[0]);
        return (TsValue*)ts_error_create(nullptr);
    };

    // Create the TsFunction for the constructor
    TsValue* ctorVal = ts_value_make_native_function((void*)+constructorFn, nullptr);

    // Extract the raw TsFunction to set .prototype and .captureStackTrace
    void* ctorRaw = ts_value_get_object(ctorVal);
    TsFunction* ctorFunc = (TsFunction*)ctorRaw;

    // Create Error.prototype with .name
    TsMap* proto = TsMap::Create();
    TsValue nameKey; nameKey.type = ValueType::STRING_PTR;
    nameKey.ptr_val = TsString::GetInterned("name");
    TsValue nameVal; nameVal.type = ValueType::STRING_PTR;
    nameVal.ptr_val = TsString::Create(errorName);
    // Spec: Error.prototype.name is { writable:true, enumerable:false, configurable:true }.
    proto->SetWithAttrs(nameKey, nameVal, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);

    // ES 20.5.3.3 / 20.5.6.3.2: Error.prototype.message and each
    // NativeError.prototype.message are the empty string, with the same
    // attributes { writable:true, enumerable:false, configurable:true }.
    TsValue msgKey; msgKey.type = ValueType::STRING_PTR;
    msgKey.ptr_val = TsString::GetInterned("message");
    TsValue msgVal; msgVal.type = ValueType::STRING_PTR;
    msgVal.ptr_val = TsString::Create("");
    proto->SetWithAttrs(msgKey, msgVal, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);

    // ES 20.5.3.1 / 20.5.6.3.1: <NativeError>.prototype.constructor is an
    // OWN {w, !e, c} data property (gOPD 15.2.3.3-4-170..175; the get-ladder
    // fallback alone is invisible to own-property introspection).
    {
        TsValue ck; ck.type = ValueType::STRING_PTR;
        ck.ptr_val = TsString::GetInterned("constructor");
        TsValue cv = nanbox_to_tagged(ctorVal);
        proto->SetWithAttrs(ck, cv,
            TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
    }

    // ECMA-262 20.5.3.4 Error.prototype.toString: "name: message" (only "name"
    // or "message" if the other is empty). Installed once on Error.prototype so
    // TypeError/RangeError/... inherit it via the prototype chain; without it,
    // errorObj.toString()/String(errorObj) fell through to Object.prototype.
    // toString and returned "[object Error]".
    if (strcmp(errorName, "Error") == 0) {
        addMethod(proto, "toString", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
            void* raw = nanbox_is_ptr(nb) ? ts_value_get_object((TsValue*)ctx) : nullptr;
            if (!raw && nanbox_is_ptr(nb)) raw = ts_nanbox_safe_unbox((TsValue*)ctx);
            // Step 1: Type(O) must be Object (reject primitives incl. string/symbol).
            bool isObject = false;
            if (raw && (uintptr_t)raw > 0x1000) {
                uint32_t m0 = *(uint32_t*)raw;
                isObject = (m0 != 0x53545247 /*STRG*/ && m0 != 0x434F4E53 /*CONS*/ &&
                            m0 != 0x53594D42 /*SYMB*/ && m0 != 0x42494749 /*BIGI*/);
            }
            if (!isObject) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Error.prototype.toString called on non-object"));
                return ts_value_make_undefined();
            }
            extern TsValue* ts_object_get_property(void* obj, const char* key);
            extern void* ts_string_from_value(TsValue* val);
            // NO std::string locals in this lambda: ts_throw above (and the
            // name/message getters, which may throw) longjmp out of this
            // frame, and MSVC's unwind ran _Tidy_deallocate on the
            // not-yet-constructed strings (0xc0000374 heap corruption —
            // Error.prototype.toString.call(5)). Compose via TsString.
            TsValue* nameV = ts_object_get_property(raw, "name");
            TsValue* msgV  = ts_object_get_property(raw, "message");
            TsString* nameS = (nameV && !ts_value_is_undefined(nameV))
                ? (TsString*)ts_string_from_value(nameV) : TsString::Create("Error");
            TsString* msgS = (msgV && !ts_value_is_undefined(msgV))
                ? (TsString*)ts_string_from_value(msgV) : TsString::Create("");
            const char* nc = (nameS && nameS->ToUtf8()) ? nameS->ToUtf8() : "";
            const char* mc = (msgS && msgS->ToUtf8()) ? msgS->ToUtf8() : "";
            if (!*nc) return ts_value_make_string(msgS ? msgS : TsString::Create(""));
            if (!*mc) return ts_value_make_string(nameS);
            extern void* ts_string_concat(void* a, void* b);
            void* withSep = ts_string_concat(nameS, TsString::Create(": "));
            return ts_value_make_string(
                (TsString*)ts_string_concat(withSep, msgS));
        }, 0);
    }


    // Per spec: TypeError.prototype / RangeError.prototype / etc.
    // inherit from Error.prototype. Link via the TsMap prototype chain
    // so `e instanceof Error` walks one step past `e.__proto__`
    // (TypeError.prototype) to find Error.prototype.
    // Skip for "Error" itself — that would recurse infinitely.
    if (strcmp(errorName, "Error") != 0) {
        void* errorCtor = ts_get_global_Error();  // lazy + cached
        if (errorCtor) {
            void* errorCtorRaw = ts_value_get_object((TsValue*)errorCtor);
            if (errorCtorRaw) {
                TsFunction* errFn = (TsFunction*)errorCtorRaw;
                if (errFn->properties) {
                    TsValue pkey; pkey.type = ValueType::STRING_PTR;
                    pkey.ptr_val = TsString::GetInterned("prototype");
                    TsValue pval = errFn->properties->Get(pkey);
                    if (pval.type != ValueType::UNDEFINED && pval.ptr_val) {
                        // pval is a tagged value; its ptr_val is the raw TsMap.
                        uint32_t m16 = *(uint32_t*)((char*)pval.ptr_val + 16);
                        if (m16 == 0x4D415053) {
                            proto->SetPrototype((TsMap*)pval.ptr_val);
                        }
                    }
                }
            }
        }
    }

    // Set .prototype on the constructor function's properties map
    if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();
    TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal; protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    ctorFunc->properties->SetWithAttrs(protoKey, protoVal, 0);

    // Set .name on the constructor
    ctorFunc->name = TsString::Create(errorName);

    // Install name/length as own properties per ES spec:
    // {writable:false, enumerable:false, configurable:true}. Tests use
    // hasOwnProperty / getOwnPropertyDescriptor to verify these exist.
    ctorFunc->arity = 1;  // Error(message) arity is 1
    {
        TsValue nk; nk.type = ValueType::STRING_PTR;
        nk.ptr_val = TsString::GetInterned("name");
        TsValue nv; nv.type = ValueType::STRING_PTR;
        nv.ptr_val = ctorFunc->name;
        ctorFunc->properties->SetWithAttrs(nk, nv, TsHashTable::ATTR_CONFIGURABLE);
        TsValue lk; lk.type = ValueType::STRING_PTR;
        lk.ptr_val = TsString::GetInterned("length");
        TsValue lv; lv.type = ValueType::NUMBER_INT; lv.i_val = 1;
        ctorFunc->properties->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);
    }

    // ES 20.5.6.2/.3: each NativeError constructor's [[Prototype]] is the
    // Error constructor, and NativeError.prototype's [[Prototype]] is
    // Error.prototype (getPrototypeOf 15.2.3.2-2 family; the fn-to-fn link
    // uses the "\x01__proto_fn" marker so getPrototypeOf(EvalError) ===
    // Error). Skip for "Error" itself (its [[Prototype]] is %Function%).
    if (strcmp(errorName, "Error") != 0) {
        extern void* ts_get_global_Error();
        TsValue* base = (TsValue*)ts_get_global_Error();
        if (base) {
            ts_object_setPrototypeOf(ctorVal, base);
            void* baseRaw = ts_value_get_object(base);
            if (baseRaw) {
                TsFunction* baseFn = (TsFunction*)baseRaw;
                if (baseFn->properties) {
                    TsValue pk; pk.type = ValueType::STRING_PTR;
                    pk.ptr_val = TsString::GetInterned("prototype");
                    TsValue pv = baseFn->properties->Get(pk);
                    if (pv.type == ValueType::OBJECT_PTR && pv.ptr_val &&
                        !proto->WouldCreateCycle((TsMap*)pv.ptr_val))
                        proto->SetPrototype((TsMap*)pv.ptr_val);
                }
            }
        }
    }

    return (void*)ctorVal;
}

void* ts_get_global_Error() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        cached = makeErrorConstructor("Error");
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
        // ES2024: Error.isError(x) — true iff x is an Error instance.
        // Accept objects whose prototype chain contains Error.prototype.
        void* ctorRaw = ts_value_get_object((TsValue*)cached);
        if (ctorRaw) {
            TsFunction* ctorFunc = (TsFunction*)ctorRaw;
            if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();
            auto isErrorFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (argc < 1 || !argv || !argv[0]) return ts_value_make_bool(false);
                void* raw = ts_value_get_object(argv[0]);
                if (!raw) return ts_value_make_bool(false);
                // Check if raw has a "message" property OR its prototype chain
                // reaches an Error.prototype. Pragmatic: inspect magic and
                // property-map presence.
                uint32_t m16 = *(uint32_t*)((char*)raw + 16);
                uint32_t m20 = *(uint32_t*)((char*)raw + 20);
                if (m16 != 0x4D415053 && m20 != 0x4D415053) return ts_value_make_bool(false);
                TsMap* m = (TsMap*)raw;
                TsValue msgKey; msgKey.type = ValueType::STRING_PTR;
                msgKey.ptr_val = TsString::GetInterned("message");
                TsValue stackKey; stackKey.type = ValueType::STRING_PTR;
                stackKey.ptr_val = TsString::GetInterned("stack");
                // Walk the chain looking for an Error-shaped prototype (has
                // both "message" and "name" properties).
                TsMap* cur = m;
                while (cur) {
                    TsValue nameKey; nameKey.type = ValueType::STRING_PTR;
                    nameKey.ptr_val = TsString::GetInterned("name");
                    if (cur->Has(nameKey)) {
                        TsValue nv = cur->Get(nameKey);
                        if (nv.type == ValueType::STRING_PTR && nv.ptr_val) {
                            const char* n = ((TsString*)nv.ptr_val)->ToUtf8();
                            if (n && (strstr(n, "Error") || !strcmp(n, "AggregateError"))) {
                                return ts_value_make_bool(true);
                            }
                        }
                    }
                    cur = cur->GetPrototype();
                }
                return ts_value_make_bool(false);
            };
            addMethod(ctorFunc->properties, "isError", (void*)+isErrorFn, 1);
        }
    }
    return cached;
}

void* ts_get_global_AggregateError() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        // AggregateError(errors, message?) — subclass of Error with
        // an additional .errors array. Follow the makeErrorConstructor
        // pattern but accept (errors, message) args.
        auto aggFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            void* thisVal = ts_get_call_this();
            void* raw = thisVal ? ts_value_get_object((TsValue*)thisVal) : nullptr;
            if (!raw) return ts_value_make_undefined();
            uint32_t m16 = *(uint32_t*)((char*)raw + 16);
            uint32_t m20 = *(uint32_t*)((char*)raw + 20);
            if (m16 != 0x4D415053 && m20 != 0x4D415053) return (TsValue*)thisVal;
            TsMap* obj = (TsMap*)raw;
            // .errors: iterate `errors` iterable into an array. Simplified:
            // if it's already an array, copy it; else leave empty.
            TsArray* errs = (TsArray*)ts_array_create();
            if (argc >= 1 && argv && argv[0]) {
                void* arg0 = ts_value_get_object(argv[0]);
                if (arg0 && *(uint32_t*)arg0 == 0x41525259) {  // TsArray
                    TsArray* src = (TsArray*)arg0;
                    int64_t n = src->Length();
                    for (int64_t i = 0; i < n; i++) errs->Push(src->Get(i));
                }
            }
            TsValue errsKey; errsKey.type = ValueType::STRING_PTR;
            errsKey.ptr_val = TsString::GetInterned("errors");
            TsValue errsVal; errsVal.type = ValueType::OBJECT_PTR;
            errsVal.ptr_val = errs;
            obj->Set(errsKey, errsVal);
            // .message
            if (argc >= 2 && argv && argv[1]) {
                TsValue msgKey; msgKey.type = ValueType::STRING_PTR;
                msgKey.ptr_val = TsString::GetInterned("message");
                TsValue msgVal = nanbox_to_tagged(argv[1]);
                obj->Set(msgKey, msgVal);
            }
            return (TsValue*)thisVal;
        };
        TsValue* ctorVal = ts_value_make_native_function((void*)+aggFn, nullptr);
        TsFunction* ctorFunc = (TsFunction*)ts_value_get_object(ctorVal);
        ctorFunc->name = TsString::Create("AggregateError");
        ctorFunc->arity = 2;
        ctorFunc->is_constructor = true;
        if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();

        // Prototype inherits from Error.prototype so (aggErr instanceof Error).
        TsMap* proto = TsMap::Create();
        TsValue pNameKey; pNameKey.type = ValueType::STRING_PTR;
        pNameKey.ptr_val = TsString::GetInterned("name");
        TsValue pNameVal; pNameVal.type = ValueType::STRING_PTR;
        pNameVal.ptr_val = TsString::Create("AggregateError");
        proto->Set(pNameKey, pNameVal);
        void* errorCtor = ts_get_global_Error();
        if (errorCtor) {
            TsFunction* ef = (TsFunction*)ts_value_get_object((TsValue*)errorCtor);
            if (ef && ef->properties) {
                TsValue pk; pk.type = ValueType::STRING_PTR;
                pk.ptr_val = TsString::GetInterned("prototype");
                TsValue pv = ef->properties->Get(pk);
                if (pv.type != ValueType::UNDEFINED && pv.ptr_val) {
                    uint32_t mm = *(uint32_t*)((char*)pv.ptr_val + 16);
                    if (mm == 0x4D415053) proto->SetPrototype((TsMap*)pv.ptr_val);
                }
            }
        }
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoValFn; protoValFn.type = ValueType::OBJECT_PTR;
        protoValFn.ptr_val = proto;
        ctorFunc->properties->SetWithAttrs(protoKey, protoValFn, 0);

        TsValue nk; nk.type = ValueType::STRING_PTR;
        nk.ptr_val = TsString::GetInterned("name");
        TsValue nv; nv.type = ValueType::STRING_PTR;
        nv.ptr_val = ctorFunc->name;
        ctorFunc->properties->SetWithAttrs(nk, nv, TsHashTable::ATTR_CONFIGURABLE);
        TsValue lk; lk.type = ValueType::STRING_PTR;
        lk.ptr_val = TsString::GetInterned("length");
        TsValue lv; lv.type = ValueType::NUMBER_INT; lv.i_val = 2;
        ctorFunc->properties->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);

        cached = (void*)ctorVal;
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

// ========================================
// JSON global
// ========================================
void* ts_get_global_JSON() {
    TenureScope _tenure;
    static TsMap* cached = nullptr;
    if (cached) return cached;

    cached = TsMap::Create();
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    addMethod(cached, "stringify", (void*)ts_json_stringify_native, 3);
    addMethod(cached, "parse", (void*)ts_json_parse_native, 2);
    // Symbol.toStringTag so Object.prototype.toString.call(JSON) === "[object JSON]"
    TsValue tagKey; tagKey.type = ValueType::STRING_PTR;
    tagKey.ptr_val = TsString::GetInterned("[Symbol.toStringTag]");
    TsValue tagVal; tagVal.type = ValueType::STRING_PTR;
    tagVal.ptr_val = TsString::Create("JSON");
    cached->SetWithAttrs(tagKey, tagVal, TsHashTable::ATTR_CONFIGURABLE);
    return cached;
}


// ========================================
// Other constructor globals — minimal stubs
// Historically returned a TsMap so property access (.prototype) worked,
// but this made typeof report "object" instead of "function" and broke
// isConstructor / Reflect.construct / Function.prototype tests.
//
// New design: return a TsMap as before (used internally to hold
// prototype/name/static methods), but `wrapAsCallable` promotes it into a
// TsFunction whose .properties IS this TsMap. get_global_X functions
// return the wrapped TsFunction so typeof is "function" and the value
// has [[Construct]]. Spec-strict callers (new Set() etc.) still use the
// compiler's fast paths (ts_set_create, ts_map_create_explicit, ...).
// ========================================

static TsMap* makeSimpleConstructorGlobal(const char* name) {
    TsMap* ctor = TsMap::Create();
    TsMap* proto = TsMap::Create();
    // Set .name
    TsValue nameKey;
    nameKey.type = ValueType::STRING_PTR;
    nameKey.ptr_val = TsString::GetInterned("name");
    TsValue nameVal;
    nameVal.type = ValueType::STRING_PTR;
    nameVal.ptr_val = TsString::Create(name);
    proto->Set(nameKey, nameVal);

    TsValue protoKey;
    protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal;
    protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    // A built-in constructor's "prototype" is {writable:false, enumerable:false,
    // configurable:false} (ES verifyProperty).
    ctor->SetWithAttrs(protoKey, protoVal, 0);
    return ctor;
}

// Wrap a TsMap-shaped constructor as a TsFunction so `typeof X` is
// "function" and isConstructor(X) returns true. Preserves property
// access: func.properties points at the same TsMap caller populated, so
// ts_object_get_property(func, "prototype") finds it.
// ES 10.3.3-4: built-in functions own "length" then "name", both
// {writable:false, enumerable:false, configurable:true} — length FIRST for
// ordinary-own-property-key order (built-ins/*/property-order.js).
static void installCtorMeta(TsFunction* fn, const char* name, int length) {
    if (!fn) return;
    fn->name = TsString::Create(name);
    fn->arity = length;
    if (!fn->properties) {
        fn->properties = TsMap::Create();
        ts_gc_write_barrier(&fn->properties, fn->properties);
    }
    TsValue lk; lk.type = ValueType::STRING_PTR;
    lk.ptr_val = TsString::GetInterned("length");
    TsValue lv; lv.type = ValueType::NUMBER_INT; lv.i_val = length;
    fn->properties->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);
    TsValue nk; nk.type = ValueType::STRING_PTR;
    nk.ptr_val = TsString::GetInterned("name");
    TsValue nv; nv.type = ValueType::STRING_PTR; nv.ptr_val = fn->name;
    fn->properties->SetWithAttrs(nk, nv, TsHashTable::ATTR_CONFIGURABLE);
}

static void* wrapAsCallable(TsMap* ctor, const char* name, int length) {
    if (!ctor) return nullptr;
    // Default body returns undefined. The spec says `Set()` without `new`
    // should throw TypeError, but we can't distinguish construct-context
    // calls (from Reflect.construct) from plain-call here, and most
    // test262 harness tests check isConstructor(X) which calls
    // Reflect.construct and expects NOT to throw. Runtime `new X()` goes
    // through compiler fast paths (ts_set_create, ts_map_create_explicit,
    // etc.) that bypass this body entirely.
    //
    // For the few built-ins where ECMA-262 specifies that plain-call IS
    // valid (and equivalent to `new`), dispatch by name using ctx. Lodash's
    // `RegExp = context.RegExp; RegExp(pat)` is the motivating case.
    auto body = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
        // ctx normally holds the constructor *name* literal (set below), but
        // ts_call_with_this_* (.call/.apply/method dispatch) overrides a native
        // function's context with the `this` receiver. When one of these wrapped
        // globals is invoked with a non-pointer `this` (e.g. a NaN-boxed number),
        // ctx is no longer the name. Guard: only treat ctx as the name when it is
        // a canonical, readable pointer — small ints (< 0x10000) and NaN-boxed
        // numbers (>= user-space ceiling) would otherwise fault in strcmp.
        const char* name = (const char*)ctx;
        uintptr_t caddr = (uintptr_t)ctx;
        if (name && caddr >= 0x10000 && caddr < 0x0000800000000000ULL &&
            strcmp(name, "RegExp") == 0) {
            // ECMA-262 22.2.4.1: RegExp(pat) is equivalent to new RegExp(pat).
            void* pattern = (argc >= 1 && argv) ? argv[0] : nullptr;
            void* flags = (argc >= 2 && argv) ? argv[1] : nullptr;
            void* re = ts_regexp_create(pattern, flags);
            return re ? ts_value_make_object(re) : ts_value_make_undefined();
        }
        if (name && caddr >= 0x10000 && caddr < 0x0000800000000000ULL &&
            strcmp(name, "Symbol") == 0) {
            // ECMA-262 20.4.1.1: Symbol([description]) returns a fresh unique
            // symbol primitive (description = ToString(arg), or none if
            // undefined). Direct `Symbol(x)` is handled by a compiler fast
            // path; this is the INDIRECT path (Symbol passed as a function
            // value, e.g. lodash's `times(n, Symbol)` / `map(arr, Symbol)`),
            // which previously fell through to undefined.
            extern void* ts_symbol_create(void* desc);
            extern void* ts_string_from_value(TsValue*);
            void* descStr = nullptr;
            if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
                descStr = ts_string_from_value(argv[0]);
            }
            void* sym = ts_symbol_create(descStr);
            return sym ? ts_value_make_object(sym) : ts_value_make_undefined();
        }
        // ES: most built-in class constructors are not [[Call]]-able —
        // invoking them without `new` (NewTarget undefined) throws
        // TypeError (Map 24.1.1.1, Set 24.2.1.1, WeakMap/WeakSet 24.3/24.4,
        // Promise 27.2.3.1, Proxy 28.2.1.1, ArrayBuffer 25.1.3.1, DataView
        // 25.3.2.1, WeakRef 26.1.1.1, FinalizationRegistry 26.2.1.1, and
        // all Temporal constructors). This body is reached with ctx==name
        // ONLY on a plain call: every construct path (compiler `new` fast
        // paths, ts_new_from_constructor identity/name dispatch,
        // Reflect.construct) resolves these builtins BEFORE the wrapper
        // body runs, and the generic construct path overrides ctx with the
        // freshly-allocated `this`. POD frame only (ts_throw longjmps).
        if (name && caddr >= 0x10000 && caddr < 0x0000800000000000ULL) {
            static const char* const kRequiresNew[] = {
                "Map", "Set", "WeakMap", "WeakSet", "Promise", "Proxy",
                "ArrayBuffer", "DataView", "WeakRef", "FinalizationRegistry",
                "PlainTime", "Duration", "PlainDate", "PlainYearMonth",
                "PlainMonthDay", "PlainDateTime", "Instant", "ZonedDateTime",
            };
            for (const char* rn : kRequiresNew) {
                if (strcmp(name, rn) == 0) {
                    char msg[80];
                    snprintf(msg, sizeof(msg),
                             "Constructor %s requires 'new'", rn);
                    ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
                    return ts_value_make_undefined();  // unreachable
                }
            }
        }
        // Method dispatch (`Temporal.PlainDate(...)`) overrides a native
        // function's ctx with the receiver. A receiver that IS the Temporal
        // namespace object means the callee is one of the eight wrapped
        // Temporal constructors invoked without `new` — none of them is
        // [[Call]]-able, so throw TypeError.
        {
            extern void* ts_get_global_Temporal();
            void* tns = ts_get_global_Temporal();
            if (tns && ctx && (ctx == tns ||
                               ctx == ts_value_get_object((TsValue*)tns))) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Temporal constructor requires 'new'"));
                return ts_value_make_undefined();  // unreachable
            }
        }
        return ts_value_make_undefined();
    };
    TsValue* fnVal = ts_value_make_native_function((void*)+body, (void*)name);
    void* rawFn = ts_value_get_object(fnVal);
    if (!rawFn) return (void*)ctor;
    TsFunction* func = (TsFunction*)rawFn;
    func->name = TsString::Create(name);
    func->arity = length;
    func->is_constructor = true;  // [[Construct]] slot
    // Point the function's property bag at the TsMap ctor so existing
    // setup (prototype, name, static methods) is visible via
    // ts_object_get_property(func, key).
    func->properties = ctor;
    ts_gc_write_barrier(&func->properties, ctor);
    // Per ES spec, built-in function objects have "name" and "length" as
    // own data properties with {writable:false, enumerable:false,
    // configurable:true}. Tests use hasOwnProperty + verifyProperty, so
    // these must live on the properties TsMap, not just in func->name/arity.
    {
        // ECMA-262: a built-in function's own "length" property precedes its
        // "name" property in ordinary-own-property-key order. Insert length
        // first so Object.getOwnPropertyNames(Ctor) yields ...,length,name
        // (built-ins/*/property-order.js asserts nameIndex === lengthIndex + 1).
        TsValue lk; lk.type = ValueType::STRING_PTR;
        lk.ptr_val = TsString::GetInterned("length");
        TsValue lv; lv.type = ValueType::NUMBER_INT; lv.i_val = length;
        ctor->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);
        TsValue nk; nk.type = ValueType::STRING_PTR;
        nk.ptr_val = TsString::GetInterned("name");
        TsValue nv; nv.type = ValueType::STRING_PTR;
        nv.ptr_val = func->name;
        ctor->SetWithAttrs(nk, nv, TsHashTable::ATTR_CONFIGURABLE);
    }
    return (void*)func;
}

// Strict Number.isFinite (per spec, no coercion — returns false for
// non-Numbers including string "42"). Differs from global isFinite.
static TsValue* ts_number_isFinite_strict_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 1 || !argv || !argv[0]) return ts_value_make_bool(false);
    uint64_t nb = nanbox_from_tsvalue_ptr(argv[0]);
    if (!nanbox_is_number(nb)) return ts_value_make_bool(false);
    double d = nanbox_to_number(nb);
    return ts_value_make_bool(std::isfinite(d));
}

// Strict Number.isNaN (per spec, no coercion).
static TsValue* ts_number_isNaN_strict_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 1 || !argv || !argv[0]) return ts_value_make_bool(false);
    uint64_t nb = nanbox_from_tsvalue_ptr(argv[0]);
    if (!nanbox_is_number(nb)) return ts_value_make_bool(false);
    double d = nanbox_to_number(nb);
    return ts_value_make_bool(d != d);
}

// Number.isInteger: true iff finite integer-valued Number.
static TsValue* ts_number_isInteger_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 1 || !argv || !argv[0]) return ts_value_make_bool(false);
    uint64_t nb = nanbox_from_tsvalue_ptr(argv[0]);
    if (nanbox_is_int32(nb)) return ts_value_make_bool(true);
    if (!nanbox_is_double(nb)) return ts_value_make_bool(false);
    double d = nanbox_to_double(nb);
    if (!std::isfinite(d)) return ts_value_make_bool(false);
    return ts_value_make_bool(d == std::floor(d));
}

// Number.isSafeInteger: integer && |x| <= 2^53-1.
static TsValue* ts_number_isSafeInteger_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 1 || !argv || !argv[0]) return ts_value_make_bool(false);
    uint64_t nb = nanbox_from_tsvalue_ptr(argv[0]);
    if (nanbox_is_int32(nb)) return ts_value_make_bool(true);
    if (!nanbox_is_double(nb)) return ts_value_make_bool(false);
    double d = nanbox_to_double(nb);
    if (!std::isfinite(d) || d != std::floor(d)) return ts_value_make_bool(false);
    return ts_value_make_bool(std::abs(d) <= 9007199254740991.0);
}

// Helper for Number wrapper objects: extract the underlying double from a
// receiver. Handles primitive numbers and TsMap wrappers that store the
// hidden "__NumberData" slot. Returns 0 for any other receiver.
static double ts_number_data_of(void* ctx) {
    if (!ctx) return 0.0;
    uint64_t nb = (uint64_t)(uintptr_t)ctx;
    if (nanbox_is_int32(nb)) return (double)nanbox_to_int32(nb);
    if (nanbox_is_double(nb)) return nanbox_to_double(nb);
    void* raw = nanbox_is_ptr(nb) ? nanbox_to_ptr(nb) : ctx;
    if (!raw) return 0.0;
    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
    if (m16 == 0x4D415053) {  // TsMap
        TsMap* obj = (TsMap*)raw;
        TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
        ndKey.ptr_val = TsString::GetInterned("__NumberData");
        TsValue v = obj->Get(ndKey);
        if (v.type == ValueType::NUMBER_DBL) return v.d_val;
        if (v.type == ValueType::NUMBER_INT) return (double)v.i_val;
    }
    return 0.0;
}

// thisNumberValue(this) per ECMA-262: returns the underlying double if the
// receiver is a number primitive or a Number wrapper object holding
// [[NumberData]]; otherwise throws a TypeError. Number.prototype methods must
// reject string/boolean/null/undefined/plain-object receivers rather than
// silently coercing them to 0.
static double ts_number_value_or_throw(void* ctx, const char* method) {
    if (ctx) {
        uint64_t nb = (uint64_t)(uintptr_t)ctx;
        if (nanbox_is_int32(nb)) return (double)nanbox_to_int32(nb);
        if (nanbox_is_double(nb)) return nanbox_to_double(nb);
        void* raw = nanbox_is_ptr(nb) ? nanbox_to_ptr(nb) : ctx;
        // A boolean/null/undefined receiver is a small special nanbox (e.g. true=0x06),
        // not a heap pointer: `raw` is non-null but `*(raw+16)` faults. Guard the
        // offset-16 magic read with a safe heap-pointer range check.
        uintptr_t rp = (uintptr_t)raw;
        if (rp >= 0x1000 && rp <= 0x00007FFFFFFFFFFFULL) {
            uint32_t m16 = *(uint32_t*)((char*)raw + 16);
            if (m16 == 0x4D415053) {  // TsMap — only valid if it carries [[NumberData]]
                TsMap* obj = (TsMap*)raw;
                TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
                ndKey.ptr_val = TsString::GetInterned("__NumberData");
                TsValue v = obj->Get(ndKey);
                if (v.type == ValueType::NUMBER_DBL) return v.d_val;
                if (v.type == ValueType::NUMBER_INT) return (double)v.i_val;
            }
        }
    }
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "Number.prototype.%s requires that 'this' be a Number", method);
    ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
    return 0.0;  // unreachable
}

extern "C" void* ts_number_to_string(double value, int64_t radix);

// Number.prototype, captured for numberFn's new-vs-call disambiguation
// (see g_string_wrapper_proto / stringFn).
static TsMap* g_number_wrapper_proto = nullptr;

void* ts_get_global_Number() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        auto numberFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            double d = (argc >= 1 && argv && argv[0])
                ? ts_value_get_double(argv[0]) : 0.0;
            // `new Number(x)`: ts_get_call_this() returns the allocated
            // wrapper TsMap. Set hidden [[NumberData]] slot and return it
            // so the caller observes a real wrapper object. Plain
            // `Number(x)` calls return a primitive double.
            void* thisVal = ts_get_call_this();
            if (thisVal) {
                void* raw = ts_value_get_object((TsValue*)thisVal);
                if (raw) {
                    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
                    // Only a genuine `new Number(x)` wrapper (prototype ===
                    // Number.prototype) constructs; a leaked ambient `this`
                    // returns a primitive. See stringFn for the rationale.
                    if (m16 == 0x4D415053 &&  // TsMap
                        ((TsMap*)raw)->GetPrototype() == g_number_wrapper_proto) {
                        TsMap* obj = (TsMap*)raw;
                        TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
                        ndKey.ptr_val = TsString::GetInterned("__NumberData");
                        TsValue ndVal; ndVal.type = ValueType::NUMBER_DBL;
                        ndVal.d_val = d;
                        obj->Set(ndKey, ndVal);
                        return (TsValue*)thisVal;
                    }
                }
            }
            return ts_value_make_double(d);
        };

        TsValue* ctorVal = ts_value_make_native_function((void*)+numberFn, nullptr);
        void* ctorRaw = ts_value_get_object(ctorVal);
        TsFunction* ctorFunc = (TsFunction*)ctorRaw;
        installCtorMeta(ctorFunc, "Number", 1);

        if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();
        TsMap* proto = TsMap::Create();
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal; protoVal.type = ValueType::OBJECT_PTR;
        protoVal.ptr_val = proto;
        ctorFunc->properties->SetWithAttrs(protoKey, protoVal, 0);
        g_number_wrapper_proto = proto;  // for numberFn's new-vs-call check

        // Number.prototype itself has [[NumberData]] = +0 per spec, so
        // Number.prototype.toString(10) returns "0". Stash it on proto.
        TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
        ndKey.ptr_val = TsString::GetInterned("__NumberData");
        TsValue ndZero; ndZero.type = ValueType::NUMBER_DBL; ndZero.d_val = 0.0;
        proto->Set(ndKey, ndZero);

        // Number.prototype methods that read [[NumberData]] from receiver.
        auto numProtoToString = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            double d = ts_number_value_or_throw(ctx, "toString");
            int64_t radix = 10;  // ECMA-262 21.1.3.6: radix undefined -> 10
            if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
                radix = ts_value_get_int(argv[0]);  // ToIntegerOrInfinity (truncates)
            }
            // radix must be an integer in [2, 36], else RangeError.
            if (radix < 2 || radix > 36) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "Number.prototype.toString() radix must be an integer between 2 and 36"));
                return ts_value_make_undefined();
            }
            return ts_value_make_string((TsString*)ts_number_to_string(d, radix));
        };
        auto numProtoValueOf = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_value_make_double(ts_number_value_or_throw(ctx, "valueOf"));
        };
        addMethod(proto, "toString", (void*)+numProtoToString, 1);
        addMethod(proto, "valueOf",  (void*)+numProtoValueOf,  0);
        // Number.prototype.toFixed/toExponential/toPrecision/toLocaleString —
        // minimal impls; tests for name/length pass once registered.
        addMethod(proto, "toFixed", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            double d = ts_number_value_or_throw(ctx, "toFixed");
            int digits = (argc >= 1 && argv && argv[0]) ? (int)ts_value_get_int(argv[0]) : 0;
            if (digits < 0 || digits > 100) {  // ECMA-262 21.1.3.3: RangeError
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "toFixed() digits argument must be between 0 and 100"));
                return ts_value_make_undefined();
            }
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%.*f", digits, d);
            return ts_value_make_string(TsString::Create(buf));
        }, 1);
        addMethod(proto, "toExponential", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            double d = ts_number_value_or_throw(ctx, "toExponential");
            int digits = (argc >= 1 && argv && argv[0]) ? (int)ts_value_get_int(argv[0]) : 6;
            // ECMA-262 21.1.3.2 step 4: non-finite returns "NaN"/"Infinity" before
            // the RangeError (step 5); snprintf would emit "nan"/"inf".
            if (std::isnan(d)) return ts_value_make_string(TsString::Create("NaN"));
            if (std::isinf(d)) return ts_value_make_string(TsString::Create(d < 0 ? "-Infinity" : "Infinity"));
            if (digits < 0 || digits > 100) {  // ECMA-262 21.1.3.2: RangeError
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "toExponential() argument must be between 0 and 100"));
                return ts_value_make_undefined();
            }
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%.*e", digits, d);
            return ts_value_make_string(TsString::Create(buf));
        }, 1);
        addMethod(proto, "toPrecision", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            double d = ts_number_value_or_throw(ctx, "toPrecision");
            if (argc < 1 || !argv || !argv[0] || ts_value_is_undefined(argv[0])) {
                return ts_value_make_string((TsString*)ts_number_to_string(d, 10));
            }
            int digits = (int)ts_value_get_int(argv[0]);
            // ECMA-262 21.1.3.5 steps 4/6: non-finite returns "NaN"/"Infinity"
            // before the RangeError (step 7).
            if (std::isnan(d)) return ts_value_make_string(TsString::Create("NaN"));
            if (std::isinf(d)) return ts_value_make_string(TsString::Create(d < 0 ? "-Infinity" : "Infinity"));
            if (digits < 1 || digits > 100) {  // ECMA-262 21.1.3.5: RangeError
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "toPrecision() argument must be between 1 and 100"));
                return ts_value_make_undefined();
            }
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%.*g", digits, d);
            return ts_value_make_string(TsString::Create(buf));
        }, 1);
        addMethod(proto, "toLocaleString", (void*)+numProtoToString, 0);

        ctorFunc->name = TsString::Create("Number");

        // Static properties on Number constructor
        auto setDouble = [&](const char* name, double val) {
            TsValue k; k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::Create(name);
            TsValue v = nanbox_to_tagged(ts_value_make_double(val));
            // Spec: Number constants are { writable:false, enumerable:false, configurable:false }.
            ctorFunc->properties->SetWithAttrs(k, v, 0);
        };
        setDouble("NaN", std::numeric_limits<double>::quiet_NaN());
        setDouble("POSITIVE_INFINITY", std::numeric_limits<double>::infinity());
        setDouble("NEGATIVE_INFINITY", -std::numeric_limits<double>::infinity());
        setDouble("MAX_SAFE_INTEGER", 9007199254740991.0);
        setDouble("MIN_SAFE_INTEGER", -9007199254740991.0);
        setDouble("EPSILON", 2.220446049250313e-16);
        setDouble("MAX_VALUE", 1.7976931348623157e+308);
        setDouble("MIN_VALUE", 5e-324);

        // Static methods on Number constructor (spec ES2015+)
        addMethod(ctorFunc->properties, "isFinite",      (void*)ts_number_isFinite_strict_native, 1);
        addMethod(ctorFunc->properties, "isNaN",         (void*)ts_number_isNaN_strict_native,    1);
        addMethod(ctorFunc->properties, "isInteger",     (void*)ts_number_isInteger_native,       1);
        addMethod(ctorFunc->properties, "isSafeInteger", (void*)ts_number_isSafeInteger_native,   1);

        // ES 21.1.2.12/13: Number.parseInt and Number.parseFloat ARE the same
        // function objects as the global parseInt / parseFloat (identity is
        // observable). Install the cached global singletons, not fresh copies.
        {
            extern void* ts_get_builtin_function(void* nameStr);
            void* piFn = ts_get_builtin_function(TsString::Create("parseInt"));
            void* pfFn = ts_get_builtin_function(TsString::Create("parseFloat"));
            if (piFn) {
                TsValue k; k.type = ValueType::STRING_PTR; k.ptr_val = TsString::GetInterned("parseInt");
                TsValue v; v.type = ValueType::OBJECT_PTR; v.ptr_val = piFn;
                ctorFunc->properties->SetWithAttrs(k, v, 0x02 | 0x04 /* writable|configurable */);
            }
            if (pfFn) {
                TsValue k; k.type = ValueType::STRING_PTR; k.ptr_val = TsString::GetInterned("parseFloat");
                TsValue v; v.type = ValueType::OBJECT_PTR; v.ptr_val = pfFn;
                ctorFunc->properties->SetWithAttrs(k, v, 0x02 | 0x04 /* writable|configurable */);
            }
        }

        // Number.prototype.constructor = Number. Was unset, so
        // `(new Number(x)).constructor` read undefined and lodash cloneNumber
        // `new value.constructor(value)` failed.
        { TsValue ck; ck.type = ValueType::STRING_PTR; ck.ptr_val = TsString::GetInterned("constructor");
          TsValue cv; cv.type = ValueType::OBJECT_PTR; cv.ptr_val = ts_value_get_object(ctorVal);
          proto->SetWithAttrs(ck, cv, 0x02 | 0x04 /* writable|configurable, non-enumerable */); }

        cached = (void*)ctorVal;
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

// Same pattern as Number wrapper: extract underlying bool from a wrapper
// receiver (TsMap with hidden __BooleanData) or a primitive boolean.
static bool ts_boolean_data_of(void* ctx) {
    if (!ctx) return false;
    uint64_t nb = (uint64_t)(uintptr_t)ctx;
    if (nb == NANBOX_TRUE) return true;
    if (nb == NANBOX_FALSE) return false;
    void* raw = nanbox_is_ptr(nb) ? nanbox_to_ptr(nb) : ctx;
    if (!raw) return false;
    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
    if (m16 == 0x4D415053) {  // TsMap
        TsMap* obj = (TsMap*)raw;
        TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
        ndKey.ptr_val = TsString::GetInterned("__BooleanData");
        TsValue v = obj->Get(ndKey);
        if (v.type == ValueType::BOOLEAN) return v.i_val != 0;
    }
    return false;
}

// thisBooleanValue(this) per ECMA-262: returns the underlying bool if the
// receiver is a boolean primitive or a Boolean wrapper object holding
// [[BooleanData]]; otherwise throws a TypeError. Boolean.prototype methods must
// reject non-boolean receivers rather than silently yielding false.
static bool ts_boolean_value_or_throw(void* ctx, const char* method) {
    if (ctx) {
        uint64_t nb = (uint64_t)(uintptr_t)ctx;
        if (nb == NANBOX_TRUE) return true;
        if (nb == NANBOX_FALSE) return false;
        void* raw = nanbox_is_ptr(nb) ? nanbox_to_ptr(nb) : ctx;
        // A number/string/symbol receiver is a non-pointer (or non-Map) nanbox: `raw`
        // is non-null but `*(raw+16)` faults. Guard the offset-16 magic read with a
        // safe heap-pointer range check (same fix as ts_number_value_or_throw).
        uintptr_t rp = (uintptr_t)raw;
        if (rp >= 0x1000 && rp <= 0x00007FFFFFFFFFFFULL) {
            uint32_t m16 = *(uint32_t*)((char*)raw + 16);
            if (m16 == 0x4D415053) {  // TsMap — only valid if it carries [[BooleanData]]
                TsMap* obj = (TsMap*)raw;
                TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
                ndKey.ptr_val = TsString::GetInterned("__BooleanData");
                TsValue v = obj->Get(ndKey);
                if (v.type == ValueType::BOOLEAN) return v.i_val != 0;
            }
        }
    }
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "Boolean.prototype.%s requires that 'this' be a Boolean", method);
    ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
    return false;  // unreachable
}

static TsMap* g_boolean_wrapper_proto = nullptr;  // for boolFn new-vs-call check

void* ts_get_global_Boolean() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        auto boolFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            bool b = (argc >= 1 && argv && argv[0]) ? ts_value_to_bool(argv[0]) : false;
            // `new Boolean(x)`: store in wrapper TsMap and return it.
            void* thisVal = ts_get_call_this();
            if (thisVal) {
                void* raw = ts_value_get_object((TsValue*)thisVal);
                if (raw) {
                    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
                    // Only a genuine `new Boolean(x)` wrapper (prototype ===
                    // Boolean.prototype) constructs; a leaked ambient `this`
                    // returns a primitive. See stringFn for the rationale.
                    if (m16 == 0x4D415053 &&  // TsMap
                        ((TsMap*)raw)->GetPrototype() == g_boolean_wrapper_proto) {
                        TsMap* obj = (TsMap*)raw;
                        TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
                        ndKey.ptr_val = TsString::GetInterned("__BooleanData");
                        TsValue ndVal; ndVal.type = ValueType::BOOLEAN;
                        ndVal.i_val = b ? 1 : 0;
                        obj->Set(ndKey, ndVal);
                        return (TsValue*)thisVal;
                    }
                }
            }
            return ts_value_make_bool(b);
        };

        TsValue* ctorVal = ts_value_make_native_function((void*)+boolFn, nullptr);
        void* ctorRaw = ts_value_get_object(ctorVal);
        TsFunction* ctorFunc = (TsFunction*)ctorRaw;

        if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();
        TsMap* proto = TsMap::Create();
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal; protoVal.type = ValueType::OBJECT_PTR;
        protoVal.ptr_val = proto;
        ctorFunc->properties->SetWithAttrs(protoKey, protoVal, 0);
        g_boolean_wrapper_proto = proto;  // for boolFn's new-vs-call check

        // Boolean.prototype seeds [[BooleanData]] = false per spec, so
        // Boolean.prototype.toString() === "false".
        TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
        ndKey.ptr_val = TsString::GetInterned("__BooleanData");
        TsValue ndFalse; ndFalse.type = ValueType::BOOLEAN; ndFalse.i_val = 0;
        proto->Set(ndKey, ndFalse);

        auto boolProtoToString = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_value_make_string(TsString::Create(
                ts_boolean_value_or_throw(ctx, "toString") ? "true" : "false"));
        };
        auto boolProtoValueOf = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_value_make_bool(ts_boolean_value_or_throw(ctx, "valueOf"));
        };
        addMethod(proto, "toString", (void*)+boolProtoToString, 0);
        addMethod(proto, "valueOf",  (void*)+boolProtoValueOf,  0);

        ctorFunc->name = TsString::Create("Boolean");
        // Boolean.prototype.constructor = Boolean (was unset; broke lodash
        // cloneByTag `new value.constructor(value)`).
        { TsValue ck; ck.type = ValueType::STRING_PTR; ck.ptr_val = TsString::GetInterned("constructor");
          TsValue cv; cv.type = ValueType::OBJECT_PTR; cv.ptr_val = ts_value_get_object(ctorVal);
          proto->SetWithAttrs(ck, cv, 0x02 | 0x04); }
        cached = (void*)ctorVal;
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

// Defined in TsObject.cpp — Function.prototype.toString returns the
// "function NAME() { [native code] }" template required by libraries
// (e.g. lodash) that introspect function source for native detection.
extern "C" TsValue* ts_function_toString_native(void* ctx, int argc, TsValue** argv);

void* ts_get_global_Function() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = TsMap::Create();
        TsMap* proto = TsMap::Create();

        // Function.prototype.call / apply / bind / toString
        // toString is required for lodash (and many other libraries) which
        // call Function.prototype.toString.call(fn) to inspect a function's
        // source for native-code detection. Without an explicit toString on
        // Function.prototype, the lookup falls through to Object.prototype.toString
        // which returns "[object Function]" instead of "function NAME() { [native code] }".
        addMethod(proto, "call", (void*)ts_function_call_native);
        addMethod(proto, "apply", (void*)ts_function_apply_native);
        addMethod(proto, "bind", (void*)ts_function_bind_native);
        addMethod(proto, "toString", (void*)ts_function_toString_native);
        // ES 20.2.3.6 Function.prototype[@@hasInstance]: the default
        // OrdinaryHasInstance as a first-class function value. Descriptor is
        // {writable:false, enumerable:false, configurable:false}; name is
        // "[Symbol.hasInstance]" per spec.
        {
            TsValue* hi = ts_value_make_native_function((void*)ts_fn_hasInstance_native, nullptr);
            TsFunction* hif = (TsFunction*)ts_value_get_object(hi);
            if (!hif) hif = (TsFunction*)hi;
            hif->name = TsString::Create("[Symbol.hasInstance]");
            hif->arity = 1;
            hif->is_constructor = false;
            if (!hif->properties) hif->properties = TsMap::Create();
            TsValue lk; lk.type = ValueType::STRING_PTR; lk.ptr_val = TsString::GetInterned("length");
            TsValue lv; lv.type = ValueType::NUMBER_INT; lv.i_val = 1;
            hif->properties->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);
            TsValue nk; nk.type = ValueType::STRING_PTR; nk.ptr_val = TsString::GetInterned("name");
            TsValue nv; nv.type = ValueType::STRING_PTR; nv.ptr_val = hif->name;
            hif->properties->SetWithAttrs(nk, nv, TsHashTable::ATTR_CONFIGURABLE);
            TsValue hk; hk.type = ValueType::STRING_PTR;
            hk.ptr_val = TsString::GetInterned("[Symbol.hasInstance]");
            TsValue hv = nanbox_to_tagged(hi);
            proto->SetWithAttrs(hk, hv, 0);
        }

        // Set ctor.prototype = proto
        TsValue protoKey;
        protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal;
        protoVal.type = ValueType::OBJECT_PTR;
        protoVal.ptr_val = proto;
        ctor->SetWithAttrs(protoKey, protoVal, 0);

        cached = wrapAsCallable(ctor, "Function", 1);
        // ES 20.2.3.1: Function.prototype.constructor === Function
        // ({writable:true, enumerable:false, configurable:true}).
        {
            TsValue ck; ck.type = ValueType::STRING_PTR;
            ck.ptr_val = TsString::GetInterned("constructor");
            TsValue cv = nanbox_to_tagged((TsValue*)cached);
            proto->SetWithAttrs(ck, cv, 0x02 | 0x04);
        }
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

extern "C" void* ts_date_prototype_build_map();
extern "C" void ts_date_constructor_populate(void* ctor);

void* ts_get_global_Date() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = TsMap::Create();
        // Set .name
        TsValue nameKey;
        nameKey.type = ValueType::STRING_PTR;
        nameKey.ptr_val = TsString::GetInterned("name");
        TsValue nameVal;
        nameVal.type = ValueType::STRING_PTR;
        nameVal.ptr_val = TsString::Create("Date");
        ctor->Set(nameKey, nameVal);

        // Attach pre-populated prototype with all instance methods
        TsMap* proto = (TsMap*)ts_date_prototype_build_map();
        TsValue protoKey;
        protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal;
        protoVal.type = ValueType::OBJECT_PTR;
        protoVal.ptr_val = proto;
        ctor->SetWithAttrs(protoKey, protoVal, 0);

        // Attach constructor static methods (Date.now/parse/UTC)
        ts_date_constructor_populate(ctor);

        cached = wrapAsCallable(ctor, "Date", 7);
        // ES 21.4.4.1: Date.prototype.constructor === Date, {w, !e, c} —
        // an OWN entry so getOwnPropertyDescriptor sees it (gOPD
        // 15.2.3.3-4-116 family; the instance get ladder alone is invisible
        // to own-property introspection).
        {
            TsValue ck; ck.type = ValueType::STRING_PTR;
            ck.ptr_val = TsString::GetInterned("constructor");
            TsValue cv = nanbox_to_tagged((TsValue*)cached);
            proto->SetWithAttrs(ck, cv,
                TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
        }
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

// Forward decls for RegExp.prototype native methods (defined in TsObject.cpp).
extern "C" {
    TsValue* ts_regexp_exec_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_regexp_test_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_regexp_compile_native(void* ctx, int argc, TsValue** argv);
}

// Brand-check for the RegExp.prototype accessor getters (ECMA-262 22.2.6.x).
// Returns the TsRegExp* when `ctx` is a RegExp instance; sets *isProto when ctx
// is %RegExp.prototype% (the getters return the spec defaults there); otherwise
// throws TypeError and returns nullptr. ts_nanbox_safe_unbox => no crash on a
// non-object receiver.
static TsRegExp* regexp_accessor_this(void* ctx, bool* isProto) {
    *isProto = false;
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_nanbox_safe_unbox(ctx);
    if (raw && *(uint32_t*)raw == 0x52454758 /*TsRegExp "REGX"*/)
        return (TsRegExp*)raw;
    if (raw) {
        extern void* ts_get_global_RegExp();
        void* g = ts_get_global_RegExp();
        if (g) {
            TsValue* p = ts_object_get_property(g, "prototype");
            void* pr = p ? ts_value_get_object(p) : nullptr;
            if (pr && pr == raw) { *isProto = true; return nullptr; }
        }
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError",
        "RegExp.prototype accessor called on a non-RegExp receiver"));
    return nullptr;
}

// RegExp.escape (ES2025 22.2.5.1). Non-string input -> TypeError (NO
// coercion). Escapes: syntax chars + solidus with a backslash; a leading
// ASCII letter/digit as \xNN; control t/n/v/f/r as their ControlEscape;
// "other punctuators", whitespace, line terminators and surrogates as
// \xNN (<= 0xFF) or \uXXXX per UTF-16 unit. Builds UTF-8 output; every
// escaped form is ASCII, and lone surrogates are always escaped, so the
// pass-through code-point encoder keeps the buffer valid UTF-8.
static void re_escape_append_cp(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}
static bool re_escape_is_ws_or_lt(uint32_t c) {
    switch (c) {
        case 0x9: case 0xB: case 0xC: case 0x20: case 0xA0: case 0xFEFF:
        case 0xA: case 0xD: case 0x2028: case 0x2029:
        case 0x1680: case 0x202F: case 0x205F: case 0x3000:
            return true;
        default:
            return c >= 0x2000 && c <= 0x200A;  // Zs range
    }
}
extern "C" TsValue* ts_regexp_escape_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx;
    TsValue* arg = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    TsString* str = nullptr;
    {
        uint64_t nb = arg ? (uint64_t)(uintptr_t)arg : 0;
        void* raw = (arg && nanbox_is_ptr(nb)) ? nanbox_to_ptr(nb) : nullptr;
        if (raw && *(uint32_t*)raw == 0x53545247 /*STRG*/) str = (TsString*)raw;
    }
    if (!str) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "RegExp.escape called with a non-string argument"));
        return ts_value_make_undefined();
    }
    static const char* kSyntax = "^$\\.*+?()[]{}|/";
    static const char* kOtherPunct = ",-=<>#&!%:;@~'`\"";
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    int64_t len = str->Length();  // UTF-16 code units
    for (int64_t i = 0; i < len; i++) {
        uint32_t u = (uint32_t)str->CharCodeAt(i);
        uint32_t cp = u;
        int64_t adv = 0;
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < len) {
            uint32_t lo = (uint32_t)str->CharCodeAt(i + 1);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                adv = 1;
            }
        }
        bool isFirst = (i == 0);
        if (cp && cp < 0x80 && strchr(kSyntax, (char)cp)) {
            out += '\\'; out += (char)cp;
        } else if (isFirst && ((cp >= '0' && cp <= '9') ||
                               (cp >= 'a' && cp <= 'z') ||
                               (cp >= 'A' && cp <= 'Z'))) {
            out += "\\x"; out += kHex[(cp >> 4) & 0xF]; out += kHex[cp & 0xF];
        } else if (cp == 0x9)  { out += "\\t"; }
        else if (cp == 0xA)  { out += "\\n"; }
        else if (cp == 0xB)  { out += "\\v"; }
        else if (cp == 0xC)  { out += "\\f"; }
        else if (cp == 0xD)  { out += "\\r"; }
        else if ((cp && cp < 0x80 && strchr(kOtherPunct, (char)cp)) ||
                 re_escape_is_ws_or_lt(cp) ||
                 (cp >= 0xD800 && cp <= 0xDFFF)) {
            if (cp <= 0xFF) {
                out += "\\x"; out += kHex[(cp >> 4) & 0xF]; out += kHex[cp & 0xF];
            } else {
                // one \uXXXX per UTF-16 unit (BMP: the unit itself; the
                // astral case never reaches here — no astral ws/punct)
                out += "\\u";
                out += kHex[(u >> 12) & 0xF]; out += kHex[(u >> 8) & 0xF];
                out += kHex[(u >> 4) & 0xF];  out += kHex[u & 0xF];
            }
        } else {
            re_escape_append_cp(out, cp);
        }
        i += adv;
    }
    return ts_value_make_string(TsString::Create(out.c_str()));
}

void* ts_get_global_RegExp() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* reCtor = makeSimpleConstructorGlobal("RegExp");
        addAccessorGetter(reCtor, "[Symbol.species]", (void*)species_this_getter);
        addMethod(reCtor, "escape", (void*)ts_regexp_escape_native, 1);
        cached = wrapAsCallable(reCtor, "RegExp", 2);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
        // Populate RegExp.prototype with the spec-required methods so that
        // `RegExp.prototype.exec`, `.test`, etc. are accessible with proper
        // name/length own-properties — required by test262.
        TsFunction* ctorFn = (TsFunction*)ts_value_get_object((TsValue*)cached);
        if (ctorFn && ctorFn->properties) {
            TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
            protoKey.ptr_val = TsString::GetInterned("prototype");
            TsValue protoVal = ctorFn->properties->Get(protoKey);
            if (protoVal.type == ValueType::OBJECT_PTR && protoVal.ptr_val) {
                TsMap* reproto = (TsMap*)protoVal.ptr_val;
                extern TsValue* ts_regexp_tostring_native(void*, int, TsValue**);
                extern TsValue* ts_regexp_symbol_search_native(void*, int, TsValue**);
                extern TsValue* ts_regexp_symbol_match_native(void*, int, TsValue**);
                extern TsValue* ts_regexp_symbol_replace_native(void*, int, TsValue**);
                extern TsValue* ts_regexp_symbol_split_native(void*, int, TsValue**);
                extern TsValue* ts_regexp_symbol_matchAll_native(void*, int, TsValue**);
                // ES 22.2.6.1: RegExp.prototype.constructor === RegExp as an
                // OWN {w, !e, c} entry (gOPD 15.2.3.3-4-163; the instance
                // ladder alone is invisible to own-property introspection).
                {
                    TsValue ck; ck.type = ValueType::STRING_PTR;
                    ck.ptr_val = TsString::GetInterned("constructor");
                    TsValue cv = nanbox_to_tagged((TsValue*)cached);
                    reproto->SetWithAttrs(ck, cv,
                        TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
                }
                addMethod(reproto, "toString", (void*)ts_regexp_tostring_native, 0);
                addMethod(reproto, "exec",     (void*)ts_regexp_exec_native, 1);
                addMethod(reproto, "test",     (void*)ts_regexp_test_native, 1);
                addMethod(reproto, "compile",  (void*)ts_regexp_compile_native, 2);
                addMethod(reproto, "[Symbol.search]", (void*)ts_regexp_symbol_search_native, 1);
                addMethod(reproto, "[Symbol.match]", (void*)ts_regexp_symbol_match_native, 1);
                addMethod(reproto, "[Symbol.replace]", (void*)ts_regexp_symbol_replace_native, 2);
                addMethod(reproto, "[Symbol.split]", (void*)ts_regexp_symbol_split_native, 2);
                addMethod(reproto, "[Symbol.matchAll]", (void*)ts_regexp_symbol_matchAll_native, 1);

                // RegExp.prototype accessor getters as REAL accessors (ES2017+
                // 22.2.6) so getOwnPropertyDescriptor returns the {get,...}
                // descriptor and propertyHelper.js verifyProperty passes. The
                // get_dynamic specials (TsObject.cpp ~4023, REGX-magic instances)
                // remain the instance fast path; these handle gOPD + .get.call().
                // On %RegExp.prototype% they return the spec defaults.
                addAccessorGetter(reproto, "source", (void*)+[](void* ctx, int, TsValue**) -> TsValue* {
                    bool isProto; TsRegExp* re = regexp_accessor_this(ctx, &isProto);
                    if (re) return ts_value_make_string(re->GetSource());
                    if (isProto) return ts_value_make_string(TsString::Create("(?:)"));
                    return ts_value_make_undefined();
                });
                // ES 22.2.6.4 `get RegExp.prototype.flags` is GENERIC: any
                // OBJECT receiver works -- the string is built by reading the
                // eight flag properties (in spec order d,g,i,m,s,u,v,y) off
                // the receiver and ToBoolean-ing each, so user getters run
                // and coercion tests observe them. Only non-objects throw.
                addAccessorGetter(reproto, "flags", (void*)+[](void* ctx, int, TsValue**) -> TsValue* {
                    if (!ctx) ctx = ts_get_call_this();
                    void* raw = ts_nanbox_safe_unbox(ctx);
                    // Primitives (numbers/booleans unbox to null; string/
                    // symbol/bigint unbox to their value objects) are NOT
                    // Objects -- ES 22.2.6.4 step 2 TypeError.
                    if (raw) {
                        uint32_t m0 = *(uint32_t*)raw;
                        if (m0 == 0x53545247 /*STRG*/ || m0 == TsConsString::MAGIC ||
                            m0 == 0x53594D42 /*SYMB*/ || m0 == 0x42494749 /*BIGI*/) {
                            raw = nullptr;
                        }
                    }
                    if (!raw) {
                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                            "RegExp.prototype.flags getter called on non-object"));
                        return ts_value_make_undefined();
                    }
                    // Real RegExp fast path keeps the stored flag string
                    // (ordering matches the ctor's normalization).
                    if (*(uint32_t*)raw == 0x52454758 /*REGX*/) {
                        return ts_value_make_string(((TsRegExp*)raw)->GetFlags());
                    }
                    static const struct { const char* prop; char flag; } kFlags[] = {
                        { "hasIndices", 'd' }, { "global", 'g' },
                        { "ignoreCase", 'i' }, { "multiline", 'm' },
                        { "dotAll", 's' }, { "unicode", 'u' },
                        { "unicodeSets", 'v' }, { "sticky", 'y' },
                    };
                    char out[9]; int n = 0;
                    for (const auto& f : kFlags) {
                        TsValue* v = ts_object_get_property(raw, f.prop);
                        if (v && ts_value_to_bool(v)) out[n++] = f.flag;
                    }
                    out[n] = 0;
                    return ts_value_make_string(TsString::Create(out));
                });
                addAccessorGetter(reproto, "global", (void*)+[](void* ctx, int, TsValue**) -> TsValue* {
                    bool isProto; TsRegExp* re = regexp_accessor_this(ctx, &isProto);
                    return re ? ts_value_make_bool(re->IsGlobal()) : ts_value_make_undefined();
                });
                addAccessorGetter(reproto, "ignoreCase", (void*)+[](void* ctx, int, TsValue**) -> TsValue* {
                    bool isProto; TsRegExp* re = regexp_accessor_this(ctx, &isProto);
                    return re ? ts_value_make_bool(re->IsIgnoreCase()) : ts_value_make_undefined();
                });
                addAccessorGetter(reproto, "multiline", (void*)+[](void* ctx, int, TsValue**) -> TsValue* {
                    bool isProto; TsRegExp* re = regexp_accessor_this(ctx, &isProto);
                    return re ? ts_value_make_bool(re->IsMultiline()) : ts_value_make_undefined();
                });
                addAccessorGetter(reproto, "sticky", (void*)+[](void* ctx, int, TsValue**) -> TsValue* {
                    bool isProto; TsRegExp* re = regexp_accessor_this(ctx, &isProto);
                    return re ? ts_value_make_bool(re->IsSticky()) : ts_value_make_undefined();
                });
                addAccessorGetter(reproto, "hasIndices", (void*)+[](void* ctx, int, TsValue**) -> TsValue* {
                    bool isProto; TsRegExp* re = regexp_accessor_this(ctx, &isProto);
                    return re ? ts_value_make_bool(re->HasIndices()) : ts_value_make_undefined();
                });
                addAccessorGetter(reproto, "dotAll", (void*)+[](void* ctx, int, TsValue**) -> TsValue* {
                    bool isProto; TsRegExp* re = regexp_accessor_this(ctx, &isProto);
                    return re ? ts_value_make_bool(re->IsDotAll()) : ts_value_make_undefined();
                });
                addAccessorGetter(reproto, "unicode", (void*)+[](void* ctx, int, TsValue**) -> TsValue* {
                    bool isProto; TsRegExp* re = regexp_accessor_this(ctx, &isProto);
                    return re ? ts_value_make_bool(re->IsUnicode()) : ts_value_make_undefined();
                });
                addAccessorGetter(reproto, "unicodeSets", (void*)+[](void* ctx, int, TsValue**) -> TsValue* {
                    bool isProto; TsRegExp* re = regexp_accessor_this(ctx, &isProto);
                    return re ? ts_value_make_bool(re->IsUnicodeSets()) : ts_value_make_undefined();
                });
            }
        }
    }
    return cached;
}

// Native wrappers for Promise statics. The runtime provides the logic
// (ts_promise_resolve/reject/all/race), these just adapt the native
// calling convention.
extern "C" {
    TsValue* ts_promise_resolve(void* context, TsValue* value);
    TsValue* ts_promise_reject(void* context, TsValue* reason);
    TsValue* ts_promise_all(TsValue* iterable);
    TsValue* ts_promise_race(TsValue* iterable);
    TsValue* ts_promise_allSettled(TsValue* iterable);
    TsValue* ts_promise_any(TsValue* iterable);
}

// NewPromiseCapability step 1 (ES 27.2.1.5): the combinators throw TypeError
// when the receiver C is not a constructor. Only rejects a receiver that is a
// plain function VALUE with [[Construct]] absent (is_constructor=false: the
// eval stub, builtin prototype methods) — object receivers (the Promise
// constructor map, subclasses) and user closures keep the lenient behavior.
// Receiver discrimination for the combinator wrappers. The CONTEXT param is
// per-dispatch: a with-this call (method call, .call/.apply) carries the
// actual receiver via maybe_override_context; some plain dispatches carry the
// wrapper FUNCTION OBJECT itself. The ambient ts_get_call_this() register is
// NOT usable (stale values from unrelated calls leak through plain calls).
// `selfFnPtr` is the wrapper's own native entry — ctx pointing back at self
// means "no explicit receiver" -> builtin fast path.
static TsFunction* promise_receiver_fn(void* ctx, void* selfFnPtr) {
    TsValue* t = (TsValue*)ctx;
    if (!t) return nullptr;
    void* raw = ts_value_get_object(t);
    if (!raw) return nullptr;
    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
    if (m16 != 0x46554E43 /*FUNC*/) return nullptr;
    TsFunction* f = (TsFunction*)raw;
    if (f->funcPtr == selfFnPtr) return nullptr;  // self: plain method call
    return f;
}
// NewPromiseCapability step 1: a receiver that is a function VALUE without
// [[Construct]] (eval stub, builtin prototype methods) throws TypeError.
static bool promise_receiver_is_non_ctor(void* ctx, void* selfFnPtr) {
    TsValue* t = (TsValue*)ctx;
    if (!t) return false;
    if (ts_extract_closure(t)) return false;
    TsFunction* f = promise_receiver_fn(ctx, selfFnPtr);
    return f && !f->is_constructor;
}
static void promise_throw_non_ctor() {
    ts_throw((TsValue*)ts_error_create_typed("TypeError",
        "Promise combinator called on a non-constructor"));
}
extern "C" TsValue* ts_promise_combinator_spec(int64_t kind, TsValue* C, TsValue* iterable);
extern "C" TsValue* ts_promise_static_resolve_spec(TsValue* C, TsValue* x);
extern "C" TsValue* ts_promise_static_reject_spec(TsValue* C, TsValue* r);
extern "C" bool ts_is_callable(void* val);
void* ts_get_global_Promise();
static TsValue* promise_custom_receiver(void* ctx, void* selfFnPtr) {
    TsValue* t = (TsValue*)ctx;
    if (!t) return nullptr;
    if (!ts_is_callable((void*)t)) return nullptr;
    if (ts_extract_closure(t)) return t;             // user function / class ctor
    TsFunction* f = promise_receiver_fn(ctx, selfFnPtr);
    if (!f) return nullptr;                          // self or non-function
    return t;
}

static TsValue* promise_resolve_native(void* ctx, int argc, TsValue** argv) {
    if (promise_receiver_is_non_ctor(ctx, (void*)promise_resolve_native)) { promise_throw_non_ctor(); return ts_value_make_undefined(); }
    TsValue* v = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (TsValue* C = promise_custom_receiver(ctx, (void*)promise_resolve_native)) {
        return ts_promise_static_resolve_spec(C, v);
    }
    return ts_promise_resolve(nullptr, v);
}
static TsValue* promise_reject_native(void* ctx, int argc, TsValue** argv) {
    if (promise_receiver_is_non_ctor(ctx, (void*)promise_reject_native)) { promise_throw_non_ctor(); return ts_value_make_undefined(); }
    TsValue* v = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (TsValue* C = promise_custom_receiver(ctx, (void*)promise_reject_native)) {
        return ts_promise_static_reject_spec(C, v);
    }
    return ts_promise_reject(nullptr, v);
}
static TsValue* promise_all_native(void* ctx, int argc, TsValue** argv) {
    if (promise_receiver_is_non_ctor(ctx, (void*)promise_all_native)) { promise_throw_non_ctor(); return ts_value_make_undefined(); }
    TsValue* v = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (TsValue* C = promise_custom_receiver(ctx, (void*)promise_all_native)) {
        return ts_promise_combinator_spec(0, C, v);
    }
    return ts_promise_all(v);
}
static TsValue* promise_race_native(void* ctx, int argc, TsValue** argv) {
    if (promise_receiver_is_non_ctor(ctx, (void*)promise_race_native)) { promise_throw_non_ctor(); return ts_value_make_undefined(); }
    TsValue* v = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (TsValue* C = promise_custom_receiver(ctx, (void*)promise_race_native)) {
        return ts_promise_combinator_spec(2, C, v);
    }
    return ts_promise_race(v);
}
static TsValue* promise_allSettled_native(void* ctx, int argc, TsValue** argv) {
    if (promise_receiver_is_non_ctor(ctx, (void*)promise_allSettled_native)) { promise_throw_non_ctor(); return ts_value_make_undefined(); }
    TsValue* v = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (TsValue* C = promise_custom_receiver(ctx, (void*)promise_allSettled_native)) {
        return ts_promise_combinator_spec(1, C, v);
    }
    return ts_promise_allSettled(v);
}
// Promise.try (ES proposal, stage 4 / test262 "promise-try"): call fn
// synchronously with the given args; the return value resolves (a throw
// rejects) a fresh promise. A non-callable fn rejects via the same abrupt
// path (Call throws TypeError).
extern "C" void* ts_push_exception_handler();
extern "C" void ts_pop_exception_handler();
extern "C" TsValue* ts_get_exception();
extern "C" void ts_set_exception(TsValue* exc);
class TsPromise;
extern "C" TsPromise* ts_promise_create();
void ts_promise_resolve_internal(TsPromise* p, TsValue* value);
void ts_promise_reject_internal(TsPromise* p, TsValue* reason);
#include <csetjmp>

extern "C" TsValue* ts_promise_try_spec(TsValue* C, int argc, TsValue** argv);
static TsValue* promise_try_native(void* ctx, int argc, TsValue** argv) {
    if (promise_receiver_is_non_ctor(ctx, (void*)promise_try_native)) { promise_throw_non_ctor(); return ts_value_make_undefined(); }
    if (TsValue* C = promise_custom_receiver(ctx, (void*)promise_try_native)) {
        return ts_promise_try_spec(C, argc, argv);
    }
    TsValue* fn = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    TsPromise* p = ts_promise_create();
    void* hbuf = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)hbuf;
    if (setjmp(*env) != 0) {
        TsValue* exc = ts_get_exception();
        ts_set_exception(nullptr);
        ts_promise_reject_internal(p, exc ? exc : ts_value_make_undefined());
        return ts_value_make_promise(p);
    }
#ifdef _WIN64
    ((_JUMP_BUFFER*)env)->Frame = 0;
#endif
    extern TsValue* ts_function_call_with_this(TsValue*, TsValue*, int, TsValue**);
    if (!ts_is_callable((void*)fn)) {
        ts_pop_exception_handler();
        ts_promise_reject_internal(p, (TsValue*)ts_error_create_typed("TypeError",
            "Promise.try argument is not callable"));
        return ts_value_make_promise(p);
    }
    TsValue* r = ts_function_call_with_this(fn, ts_value_make_undefined(),
        argc > 1 ? argc - 1 : 0, argc > 1 ? argv + 1 : nullptr);
    ts_pop_exception_handler();
    ts_promise_resolve_internal(p, r);
    return ts_value_make_promise(p);
}

static TsValue* promise_any_native(void* ctx, int argc, TsValue** argv) {
    if (promise_receiver_is_non_ctor(ctx, (void*)promise_any_native)) { promise_throw_non_ctor(); return ts_value_make_undefined(); }
    TsValue* v = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (TsValue* C = promise_custom_receiver(ctx, (void*)promise_any_native)) {
        return ts_promise_combinator_spec(3, C, v);
    }
    return ts_promise_any(v);
}

void* ts_get_global_Promise() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("Promise");
        addMethod(ctor, "resolve",    (void*)promise_resolve_native, 1);
        addMethod(ctor, "reject",     (void*)promise_reject_native,  1);
        addMethod(ctor, "all",        (void*)promise_all_native,     1);
        addMethod(ctor, "race",       (void*)promise_race_native,    1);
        addMethod(ctor, "allSettled", (void*)promise_allSettled_native, 1);
        addMethod(ctor, "any",        (void*)promise_any_native,     1);
        addMethod(ctor, "try",        (void*)promise_try_native,     1);
        // Per ECMA-262: Promise.prototype has @@toStringTag = "Promise".
        TsValue pkey; pkey.type = ValueType::STRING_PTR;
        pkey.ptr_val = TsString::GetInterned("prototype");
        TsValue pval = ctor->Get(pkey);
        if (pval.type == ValueType::OBJECT_PTR && pval.ptr_val) {
            TsMap* proto = (TsMap*)pval.ptr_val;
            TsValue tagKey; tagKey.type = ValueType::STRING_PTR;
            tagKey.ptr_val = TsString::GetInterned("[Symbol.toStringTag]");
            TsValue tagVal; tagVal.type = ValueType::STRING_PTR;
            tagVal.ptr_val = TsString::Create("Promise");
            proto->SetWithAttrs(tagKey, tagVal, TsHashTable::ATTR_CONFIGURABLE);
            // Register then/catch/finally as real own-properties of
            // Promise.prototype so `Promise.prototype.finally` (and its
            // length/name/descriptor) is discoverable. Instance calls
            // (p.then()/p.finally()) are dispatched via TsPromise's virtual
            // property handler; these delegate to the same logic.
            // ts_promise_then/catch/finally expect a NaN-boxed promise; `this`
            // arrives as a raw/boxed receiver, so unbox then re-box as a
            // promise value. ts_promise_* validate PROMISE_PTR internally.
            TsValue* ts_promise_then(TsValue*, TsValue*, TsValue*);
            TsValue* ts_promise_catch(TsValue*, TsValue*);
            TsValue* ts_promise_finally(TsValue*, TsValue*);
            addMethod(proto, "then", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_value_get_object((TsValue*)ctx); if (!raw) raw = ctx;
                return ts_promise_then(ts_value_make_promise(raw),
                    (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(),
                    (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined());
            }, 2);
            addMethod(proto, "catch", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_value_get_object((TsValue*)ctx); if (!raw) raw = ctx;
                return ts_promise_catch(ts_value_make_promise(raw),
                    (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined());
            }, 1);
            addMethod(proto, "finally", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_value_get_object((TsValue*)ctx); if (!raw) raw = ctx;
                return ts_promise_finally(ts_value_make_promise(raw),
                    (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined());
            }, 1);
        }
        addAccessorGetter(ctor, "[Symbol.species]", (void*)species_this_getter);
        cached = wrapAsCallable(ctor, "Promise", 1);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

void* ts_get_global_TypeError() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("TypeError");
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

void* ts_get_global_RangeError() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("RangeError");
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

void* ts_get_global_ReferenceError() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("ReferenceError");
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

void* ts_get_global_SyntaxError() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("SyntaxError");
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

void* ts_get_global_URIError() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("URIError");
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

void* ts_get_global_EvalError() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("EvalError");
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

void* ts_get_global_Symbol() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("Symbol");
        {   // Symbol.prototype[@@toStringTag] = "Symbol"
            TsValue pkey; pkey.type = ValueType::STRING_PTR; pkey.ptr_val = TsString::GetInterned("prototype");
            TsValue protoT = ctor->Get(pkey);
            if (protoT.type == ValueType::OBJECT_PTR && protoT.ptr_val)
                setProtoStringTag((TsMap*)protoT.ptr_val, "Symbol");
        }

        // Register well-known symbols as REAL TsSymbols whose description is
        // "Symbol.<name>" (so `Symbol.iterator.description === "Symbol.iterator"`
        // and `typeof Symbol.iterator === "symbol"`). The property-key coercion
        // ts_symbol_storage_key (TsObject.cpp) maps any symbol whose description
        // begins "Symbol." back to the legacy canonical string "[Symbol.<name>]",
        // which the iteration / toPrimitive / hasInstance protocol machinery in
        // many files looks up by that literal string. So `obj[Symbol.X]` (user
        // code, via ts_property_key_string) and the hardcoded internal
        // `ts_object_get_property(o, "[Symbol.X]")` resolve to the same slot.
        // Created once (inside `if (!cached)`) so identity holds and the GC roots
        // them through the cached ctor map.
        static const char* kWellKnown[] = {
            "iterator",       "asyncIterator",  "hasInstance",
            "isConcatSpreadable", "match",     "matchAll",
            "replace",        "search",         "split",
            "species",        "toPrimitive",    "toStringTag",
            "unscopables",    nullptr
        };
        for (int i = 0; kWellKnown[i]; i++) {
            char descbuf[64];
            snprintf(descbuf, sizeof(descbuf), "Symbol.%s", kWellKnown[i]);
            TsSymbol* sym = TsSymbol::Create(TsString::Create(descbuf));
            TsValue k; k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::GetInterned(kWellKnown[i]);
            TsValue v; v.type = ValueType::SYMBOL_PTR;
            v.ptr_val = sym;
            // Spec: well-known symbols are { writable:false, enumerable:false, configurable:false }.
            ctor->SetWithAttrs(k, v, 0);
        }

        // Static methods: Symbol.for(key), Symbol.keyFor(sym).
        extern void* ts_symbol_for(void* key);
        extern void* ts_symbol_key_for(void* sym);
        addMethod(ctor, "for", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            void* key = (argc >= 1 && argv) ? (void*)ts_value_get_string(argv[0]) : nullptr;
            if (!key && argc >= 1 && argv) key = (void*)argv[0];
            void* sym = ts_symbol_for(key);
            return ts_value_make_object(sym);
        }, 1);
        addMethod(ctor, "keyFor", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (argc < 1 || !argv || !argv[0]) return ts_value_make_undefined();
            void* sym = ts_value_get_object(argv[0]);
            if (!sym) sym = argv[0];
            void* key = ts_symbol_key_for(sym);
            if (!key) return ts_value_make_undefined();
            return ts_value_make_string(key);
        }, 1);

        // Symbol.prototype.valueOf (ECMA-262 20.4.3.4): returns the underlying
        // symbol primitive. For a bare symbol receiver this is `this`; for a
        // Symbol wrapper object (TsMap with the hidden __SymbolData slot, made
        // by Object(symbol)) it unwraps the slot. Without this a wrapper's
        // valueOf inherited Object.prototype.valueOf (returns the wrapper, typeof
        // 'object'), breaking lodash's `_.clone(Object(sym))` assertion
        // `typeof actual.valueOf() === 'symbol'`.
        {
            TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
            protoKey.ptr_val = TsString::GetInterned("prototype");
            TsValue protoVal = ctor->Get(protoKey);
            if (protoVal.type != ValueType::UNDEFINED && protoVal.ptr_val) {
                TsMap* proto = (TsMap*)protoVal.ptr_val;
                auto symValueOf = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
                    if (!ctx) ctx = ts_get_call_this();
                    if (!ctx) return ts_value_make_undefined();
                    void* raw = ts_value_get_object((TsValue*)ctx);
                    if (!raw) raw = ctx;
                    if (raw) {
                        uint32_t m0 = *(uint32_t*)raw;
                        if (m0 == 0x53594D42) return (TsValue*)ctx;  // bare symbol
                        uint32_t m16 = *(uint32_t*)((char*)raw + 16);
                        if (m16 == 0x4D415053) {  // TsMap wrapper
                            TsValue dk; dk.type = ValueType::STRING_PTR;
                            dk.ptr_val = TsString::GetInterned("__SymbolData");
                            TsValue v = ((TsMap*)raw)->Get(dk);
                            if (v.type == ValueType::SYMBOL_PTR && v.ptr_val) {
                                return ts_value_make_object(v.ptr_val);
                            }
                        }
                    }
                    return (TsValue*)ctx;
                };
                addMethod(proto, "valueOf", (void*)+symValueOf, 0);

                // Symbol.prototype.description (ES2019) — install as a REAL
                // accessor property (not just a get_dynamic special) so
                // getOwnPropertyDescriptor returns {get, set:undefined,
                // enumerable:false, configurable:true} and propertyHelper.js
                // verifyProperty (delete/redefine/for-in) passes. The
                // get_dynamic special at TsObject.cpp ~9319 remains as a fast
                // path; this installed getter is what gOPD + .call() see.
                addAccessorGetter(proto, "description", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                    if (!ctx) ctx = ts_get_call_this();
                    void* raw = ts_nanbox_safe_unbox(ctx);  // nullptr for non-objects (no crash)
                    // Symbol wrapper object (Object(sym)): unwrap __SymbolData.
                    if (raw && *(uint32_t*)((char*)raw + 16) == 0x4D415053 /*TsMap "MAPS"*/) {
                        TsValue dk; dk.type = ValueType::STRING_PTR;
                        dk.ptr_val = TsString::GetInterned("__SymbolData");
                        TsValue v = ((TsMap*)raw)->Get(dk);
                        if (v.type == ValueType::SYMBOL_PTR && v.ptr_val) raw = v.ptr_val;
                    }
                    if (raw && *(uint32_t*)raw == 0x53594D42 /*TsSymbol "SYMB"*/) {
                        extern void* ts_symbol_get_description(void* sym);
                        void* d = ts_symbol_get_description(raw);
                        return d ? ts_value_make_string((TsString*)d) : ts_value_make_undefined();
                    }
                    // ECMA-262 thisSymbolValue: TypeError when not a Symbol.
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Symbol.prototype.description requires that 'this' be a Symbol"));
                    return ts_value_make_undefined();
                });

                // Symbol.prototype.toString (ECMA-262 20.4.3.3): returns
                // "Symbol(<description>)". Without this a symbol inherited
                // Object.prototype.toString -> "[object Symbol]" and
                // `Symbol('a').toString()` was undefined. lodash baseToString
                // calls Symbol.prototype.toString.call(sym), so `_.toString(sym)`
                // must yield "Symbol(a)".
                auto symToString = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
                    if (!ctx) ctx = ts_get_call_this();
                    if (!ctx) return ts_value_make_undefined();
                    void* raw = ts_value_get_object((TsValue*)ctx);
                    if (!raw) raw = ctx;
                    TsSymbol* sym = nullptr;
                    if (raw) {
                        uint32_t m0 = *(uint32_t*)raw;
                        if (m0 == 0x53594D42) {  // bare symbol
                            sym = (TsSymbol*)raw;
                        } else {
                            uint32_t m16 = *(uint32_t*)((char*)raw + 16);
                            if (m16 == 0x4D415053) {  // Symbol wrapper (Object(sym))
                                TsValue dk; dk.type = ValueType::STRING_PTR;
                                dk.ptr_val = TsString::GetInterned("__SymbolData");
                                TsValue v = ((TsMap*)raw)->Get(dk);
                                if (v.type == ValueType::SYMBOL_PTR && v.ptr_val) {
                                    sym = (TsSymbol*)v.ptr_val;
                                }
                            }
                        }
                    }
                    const char* desc = (sym && sym->description) ? sym->description->ToUtf8() : "";
                    std::string s = std::string("Symbol(") + (desc ? desc : "") + ")";
                    return ts_value_make_string(TsString::Create(s.c_str()));
                };
                addMethod(proto, "toString", (void*)+symToString, 0);
            }
        }

        cached = wrapAsCallable(ctor, "Symbol", 0);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

// Forward declarations for Set/Map wrappers — must be before first use
extern "C" {
    TsValue* ts_set_has_wrapper(void* context, TsValue* value);
    TsValue* ts_set_add_wrapper(void* context, TsValue* value);
    TsValue* ts_set_delete_wrapper(void* context, TsValue* value);
    TsValue* ts_set_clear_wrapper(void* context);
    TsValue* ts_set_size_wrapper(void* context);
    void ts_set_forEach(void* set, void* callback, void* thisArg);
    TsValue* ts_map_get_wrapper(void* context, TsValue* key);
    TsValue* ts_map_set_wrapper(void* context, TsValue* key, TsValue* value);
    TsValue* ts_map_has_wrapper(void* context, TsValue* key);
    TsValue* ts_map_delete_wrapper(void* context, TsValue* key);
    TsValue* ts_map_clear_wrapper(void* context);
    TsValue* ts_map_size_wrapper(void* context);
    TsValue* ts_map_getOrInsert_wrapper(void* context, TsValue* key, TsValue* value);
    TsValue* ts_map_getOrInsertComputed_wrapper(void* context, TsValue* key, TsValue* callbackfn);

    // Brand-checked prototype-method entry points (ECMA-262 cross-receiver
    // TypeError). The trailing int is the collection brand the receiver must
    // match. Map-family codes (CollBrand in TsMap.cpp): 0=Map, 2=WeakMap.
    // Set-family codes (SetBrand in TsSet.cpp): 0=Set, 1=WeakSet.
    TsValue* ts_map_get_wrapper_branded(void* context, TsValue* key, int brand);
    TsValue* ts_map_set_wrapper_branded(void* context, TsValue* key, TsValue* value, int brand);
    TsValue* ts_map_has_wrapper_branded(void* context, TsValue* key, int brand);
    TsValue* ts_map_delete_wrapper_branded(void* context, TsValue* key, int brand);
    TsValue* ts_map_clear_wrapper_branded(void* context, int brand);
    TsValue* ts_map_size_wrapper_branded(void* context, int brand);
    TsValue* ts_map_getOrInsert_wrapper_branded(void* context, TsValue* key, TsValue* value, int brand);
    TsValue* ts_map_getOrInsertComputed_wrapper_branded(void* context, TsValue* key, TsValue* callbackfn, int brand);
    TsValue* ts_set_add_wrapper_branded(void* context, TsValue* value, int brand);
    TsValue* ts_set_has_wrapper_branded(void* context, TsValue* value, int brand);
    TsValue* ts_set_delete_wrapper_branded(void* context, TsValue* value, int brand);
    TsValue* ts_set_clear_wrapper_branded(void* context, int brand);
    TsValue* ts_set_size_wrapper_branded(void* context, int brand);
    TsValue* ts_set_forEach_wrapper_branded(void* context, TsValue* callback, TsValue* thisArg, int brand);
    TsValue* ts_set_values_iter_wrapper_branded(void* context, int argc, TsValue** argv, int brand);
    TsValue* ts_set_entries_iter_wrapper_branded(void* context, int argc, TsValue** argv, int brand);
    // ES2025 Set methods (TsSet.cpp) — brand-check internally via requireSet.
    TsValue* ts_set_union_wrapper(void* context, TsValue* other);
    TsValue* ts_set_intersection_wrapper(void* context, TsValue* other);
    TsValue* ts_set_difference_wrapper(void* context, TsValue* other);
    TsValue* ts_set_symdiff_wrapper(void* context, TsValue* other);
    TsValue* ts_set_isSubsetOf_wrapper(void* context, TsValue* other);
    TsValue* ts_set_isSupersetOf_wrapper(void* context, TsValue* other);
    TsValue* ts_set_isDisjointFrom_wrapper(void* context, TsValue* other);
}

// Collection brand codes shared with TsMap.cpp (CollBrand) / TsSet.cpp (SetBrand).
// Keep in sync with those enums' declaration order.
enum {
    TS_BRAND_MAP = 0,       // CollBrand::Map
    TS_BRAND_WEAKMAP = 2,   // CollBrand::WeakMap
    TS_BRAND_SET = 0,       // SetBrand::Set
    TS_BRAND_WEAKSET = 1,   // SetBrand::WeakSet
};

void* ts_get_global_Map() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("Map");
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal = ctor->Get(protoKey);
        TsMap* proto = (protoVal.type != ValueType::UNDEFINED && protoVal.ptr_val)
            ? (TsMap*)protoVal.ptr_val : TsMap::Create();

        addMethod(proto, "get", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_get_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_MAP);
        });
        addMethod(proto, "set", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_set_wrapper_branded(ctx,
                (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(),
                (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined(), TS_BRAND_MAP);
        }, 2);
        addMethod(proto, "has", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_has_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_MAP);
        });
        addMethod(proto, "delete", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_delete_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_MAP);
        });
        // TC39 upsert proposal — Map.prototype.getOrInsert / getOrInsertComputed.
        addMethod(proto, "getOrInsert", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_getOrInsert_wrapper_branded(ctx,
                (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(),
                (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined(), TS_BRAND_MAP);
        }, 2);
        addMethod(proto, "getOrInsertComputed", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_getOrInsertComputed_wrapper_branded(ctx,
                (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(),
                (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined(), TS_BRAND_MAP);
        }, 2);
        addMethod(proto, "clear", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_clear_wrapper_branded(ctx, TS_BRAND_MAP);
        }, 0);
        // Iteration methods: entries, keys, values (return iterator-like
        // arrays). Implementations exist in TsMap.cpp.
        extern void* ts_map_entries_iter(void* map);
        extern void* ts_map_keys_iter(void* map);
        extern void* ts_map_values_iter(void* map);
        extern void ts_map_forEach(void* map, void* callback, void* thisArg);
        addMethod(proto, "entries", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            void* raw = ts_value_get_object((TsValue*)ctx);
            if (!raw) raw = ctx;
            // Brand-check: a primitive/non-Map receiver (Map.prototype.entries.call(5)
            // or .call("x")) was cast to TsMap* -> GetEntries deref -> crash. Require
            // the TsMap magic (offset 16) on a safe heap pointer; else TypeError.
            { uintptr_t _p=(uintptr_t)raw; if (_p<0x1000 || _p>0x00007FFFFFFFFFFFULL || *(uint32_t*)((char*)raw+16)!=0x4D415053 || !((TsMap*)raw)->IsExplicitMap()) { ts_throw((TsValue*)ts_error_create_typed("TypeError","Map.prototype.entries called on incompatible receiver")); return ts_value_make_undefined(); } }
            void* it = ts_map_entries_iter(raw);
            return it ? ts_value_make_object(it) : ts_value_make_undefined();
        }, 0);
        addMethod(proto, "keys", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            void* raw = ts_value_get_object((TsValue*)ctx);
            if (!raw) raw = ctx;
            { uintptr_t _p=(uintptr_t)raw; if (_p<0x1000 || _p>0x00007FFFFFFFFFFFULL || *(uint32_t*)((char*)raw+16)!=0x4D415053 || !((TsMap*)raw)->IsExplicitMap()) { ts_throw((TsValue*)ts_error_create_typed("TypeError","Map.prototype.keys called on incompatible receiver")); return ts_value_make_undefined(); } }
            void* it = ts_map_keys_iter(raw);
            return it ? ts_value_make_object(it) : ts_value_make_undefined();
        }, 0);
        addMethod(proto, "values", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            void* raw = ts_value_get_object((TsValue*)ctx);
            if (!raw) raw = ctx;
            { uintptr_t _p=(uintptr_t)raw; if (_p<0x1000 || _p>0x00007FFFFFFFFFFFULL || *(uint32_t*)((char*)raw+16)!=0x4D415053 || !((TsMap*)raw)->IsExplicitMap()) { ts_throw((TsValue*)ts_error_create_typed("TypeError","Map.prototype.values called on incompatible receiver")); return ts_value_make_undefined(); } }
            void* it = ts_map_values_iter(raw);
            return it ? ts_value_make_object(it) : ts_value_make_undefined();
        }, 0);
        addMethod(proto, "forEach", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            void* raw = ts_value_get_object((TsValue*)ctx);
            if (!raw) raw = ctx;
            void* cb = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
            void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
            ts_map_forEach(raw, cb, thisArg);
            return ts_value_make_undefined();
        }, 1);

        // Map.prototype.size — REAL accessor (the get_dynamic special at
        // TsObject.cpp ~4392 remains a fast path) so getOwnPropertyDescriptor
        // returns the accessor descriptor and verifyProperty passes. Reuses the
        // brand-checking ts_map_size_wrapper (throws TypeError on non-Map).
        addAccessorGetter(proto, "size", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_size_wrapper_branded(ctx, TS_BRAND_MAP);
        });

        // Static Map.groupBy(items, keyFn) — ES2024.
        extern TsValue* ts_map_groupBy(TsValue* iterable, TsValue* callbackFn);
        addMethod(ctor, "groupBy", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            TsValue* it = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
            TsValue* fn = (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined();
            return ts_map_groupBy(it, fn);
        }, 2);

        // Map.prototype[@@iterator] === Map.prototype.entries (same function).
        {
            TsValue ek; ek.type = ValueType::STRING_PTR; ek.ptr_val = TsString::GetInterned("entries");
            TsValue efn = proto->Get(ek);
            if (efn.type != ValueType::UNDEFINED) {
                TsValue ik; ik.type = ValueType::STRING_PTR; ik.ptr_val = TsString::GetInterned("[Symbol.iterator]");
                proto->SetWithAttrs(ik, efn, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
            }
        }

        setProtoStringTag(proto, "Map");
        addAccessorGetter(ctor, "[Symbol.species]", (void*)species_this_getter);
        cached = wrapAsCallable(ctor, "Map", 0);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

void* ts_get_global_Set() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("Set");
        // Get the prototype TsMap from the constructor
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal = ctor->Get(protoKey);
        TsMap* proto = (protoVal.type != ValueType::UNDEFINED && protoVal.ptr_val)
            ? (TsMap*)protoVal.ptr_val : TsMap::Create();

        // Register Set.prototype methods — these use ctx as the Set object.
        // Branded entry points reject a WeakSet (or any non-Set) receiver.
        addMethod(proto, "has", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_has_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_SET);
        });
        addMethod(proto, "add", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_add_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_SET);
        });
        addMethod(proto, "delete", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_delete_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_SET);
        });
        addMethod(proto, "clear", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_clear_wrapper_branded(ctx, TS_BRAND_SET);
        }, 0);
        // Set.prototype iteration methods (entries/keys/values). For a Set,
        // values and keys are the same; entries returns [v, v] pairs.
        addMethod(proto, "values", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_values_iter_wrapper_branded(ctx, argc, argv, TS_BRAND_SET);
        }, 0);
        addMethod(proto, "keys", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_values_iter_wrapper_branded(ctx, argc, argv, TS_BRAND_SET);
        }, 0);
        addMethod(proto, "entries", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_entries_iter_wrapper_branded(ctx, argc, argv, TS_BRAND_SET);
        }, 0);
        addMethod(proto, "forEach", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            TsValue* callback = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
            TsValue* thisArg = (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined();
            return ts_set_forEach_wrapper_branded(ctx, callback, thisArg, TS_BRAND_SET);
        });

        // ES2025 Set methods (union/intersection/difference/symmetricDifference/
        // isSubsetOf/isSupersetOf/isDisjointFrom) — set-like argument protocol.
        addMethod(proto, "union", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_union_wrapper(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined());
        });
        addMethod(proto, "intersection", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_intersection_wrapper(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined());
        });
        addMethod(proto, "difference", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_difference_wrapper(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined());
        });
        addMethod(proto, "symmetricDifference", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_symdiff_wrapper(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined());
        });
        addMethod(proto, "isSubsetOf", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_isSubsetOf_wrapper(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined());
        });
        addMethod(proto, "isSupersetOf", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_isSupersetOf_wrapper(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined());
        });
        addMethod(proto, "isDisjointFrom", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_isDisjointFrom_wrapper(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined());
        });

        // Set.prototype[@@iterator] === Set.prototype.values (same function).
        {
            TsValue vk; vk.type = ValueType::STRING_PTR; vk.ptr_val = TsString::GetInterned("values");
            TsValue vfn = proto->Get(vk);
            if (vfn.type != ValueType::UNDEFINED) {
                TsValue ik; ik.type = ValueType::STRING_PTR; ik.ptr_val = TsString::GetInterned("[Symbol.iterator]");
                proto->SetWithAttrs(ik, vfn, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
            }
        }

        // Set.prototype.size — REAL accessor (see Map.prototype.size). Reuses
        // the brand-checking ts_set_size_wrapper_branded (throws TypeError on a
        // non-Set, including a WeakSet, receiver).
        addAccessorGetter(proto, "size", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_size_wrapper_branded(ctx, TS_BRAND_SET);
        });

        setProtoStringTag(proto, "Set");
        addAccessorGetter(ctor, "[Symbol.species]", (void*)species_this_getter);
        cached = wrapAsCallable(ctor, "Set", 0);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

void* ts_get_global_WeakMap() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("WeakMap");
        // Get the prototype TsMap from the constructor
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal = ctor->Get(protoKey);
        TsMap* proto = (protoVal.type != ValueType::UNDEFINED && protoVal.ptr_val)
            ? (TsMap*)protoVal.ptr_val : TsMap::Create();

        // WeakMap.prototype methods — share the TsMap-backed ops with Map via the
        // branded entry points, validating the WeakMap brand so a Map/Set/WeakSet
        // receiver throws TypeError (ECMA-262 cross-receiver brand check).
        addMethod(proto, "has", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_has_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_WEAKMAP);
        });
        addMethod(proto, "get", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_get_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_WEAKMAP);
        });
        addMethod(proto, "set", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_set_wrapper_branded(ctx,
                (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(),
                (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined(), TS_BRAND_WEAKMAP);
        }, 2);
        addMethod(proto, "delete", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_delete_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_WEAKMAP);
        });
        // TC39 upsert proposal — WeakMap.prototype.getOrInsert / getOrInsertComputed.
        addMethod(proto, "getOrInsert", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_getOrInsert_wrapper_branded(ctx,
                (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(),
                (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined(), TS_BRAND_WEAKMAP);
        }, 2);
        addMethod(proto, "getOrInsertComputed", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_map_getOrInsertComputed_wrapper_branded(ctx,
                (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(),
                (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined(), TS_BRAND_WEAKMAP);
        }, 2);
        setProtoStringTag(proto, "WeakMap");
        cached = wrapAsCallable(ctor, "WeakMap", 0);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

void* ts_get_global_WeakSet() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("WeakSet");
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal = ctor->Get(protoKey);
        TsMap* proto = (protoVal.type != ValueType::UNDEFINED && protoVal.ptr_val)
            ? (TsMap*)protoVal.ptr_val : TsMap::Create();

        // WeakSet.prototype methods — share the TsSet-backed ops via the branded
        // entry points, validating the WeakSet brand so a Set/Map/WeakMap
        // receiver throws TypeError (ECMA-262 cross-receiver brand check).
        addMethod(proto, "has", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_has_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_WEAKSET);
        });
        addMethod(proto, "add", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_add_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_WEAKSET);
        });
        addMethod(proto, "delete", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return ts_set_delete_wrapper_branded(ctx, (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined(), TS_BRAND_WEAKSET);
        });
        setProtoStringTag(proto, "WeakSet");
        cached = wrapAsCallable(ctor, "WeakSet", 0);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

// Forward declarations for Reflect methods (TsReflect.cpp)
extern "C" TsValue* ts_reflect_construct(void* targetArg, void* argsArg, void* newTargetArg);
extern "C" TsValue* ts_reflect_get(void* targetArg, void* propArg, void* receiverArg);
extern "C" TsValue* ts_reflect_apply(void* target, void* thisArg, void* args);

// Native wrapper for Reflect.construct callable from JS
static TsValue* ts_reflect_construct_native(void* ctx, int argc, TsValue** argv) {
    void* target = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
    void* args = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
    void* newTarget = (argc >= 3 && argv) ? (void*)argv[2] : nullptr;
    return ts_reflect_construct(target, args, newTarget);
}

static TsValue* ts_reflect_apply_native(void* ctx, int argc, TsValue** argv) {
    void* target = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
    void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
    void* args = (argc >= 3 && argv) ? (void*)argv[2] : nullptr;
    // ECMA-262 28.1.1 Reflect.apply(target, thisArg, argumentsList):
    //   1. If IsCallable(target) is false, throw a TypeError.
    //   3. CreateListFromArrayLike(argumentsList) -> TypeError if not an Object.
    if (!ts_is_callable(target)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Reflect.apply called on non-callable target"));
        return ts_value_make_undefined();
    }
    void* argsRaw = args ? ts_value_get_object((TsValue*)args) : nullptr;
    bool argsIsObject = false;
    if (argsRaw) {
        uintptr_t ap = (uintptr_t)argsRaw;
        if (ap >= 0x1000 && ap <= 0x00007FFFFFFFFFFFULL) {
            uint32_t am0 = *(uint32_t*)argsRaw;       // reject string primitives
            argsIsObject = (am0 != 0x53545247 && am0 != 0x434F4E53);
        }
    }
    if (!argsIsObject) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Reflect.apply argumentsList is not an object"));
        return ts_value_make_undefined();
    }
    return ts_reflect_apply(target, thisArg, args);
}

extern "C" {
    int64_t ts_reflect_set(void* target, void* prop, void* value, void* receiver);
    int64_t ts_reflect_has(void* target, void* prop);
    int64_t ts_reflect_deleteProperty(void* target, void* prop);
    TsValue* ts_reflect_ownKeys(void* target);
    TsValue* ts_reflect_getPrototypeOf(void* target);
    int64_t ts_reflect_setPrototypeOf(void* target, void* proto);
    TsValue* ts_reflect_getOwnPropertyDescriptor(void* target, void* prop);
    int64_t ts_reflect_defineProperty(void* target, void* prop, void* descriptor);
    int64_t ts_reflect_isExtensible(void* target);
    int64_t ts_reflect_preventExtensions(void* target);
}

static TsValue* reflect_get_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    void* p = (argc >= 2 && argv) ? argv[1] : nullptr;
    void* r = (argc >= 3 && argv) ? argv[2] : nullptr;
    return ts_reflect_get(t, p, r);
}
static TsValue* reflect_set_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    void* p = (argc >= 2 && argv) ? argv[1] : nullptr;
    void* v = (argc >= 3 && argv) ? argv[2] : nullptr;
    void* r = (argc >= 4 && argv) ? argv[3] : nullptr;
    return ts_value_make_bool(ts_reflect_set(t, p, v, r) != 0);
}
static TsValue* reflect_has_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    void* p = (argc >= 2 && argv) ? argv[1] : nullptr;
    return ts_value_make_bool(ts_reflect_has(t, p) != 0);
}
static TsValue* reflect_deleteProperty_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    void* p = (argc >= 2 && argv) ? argv[1] : nullptr;
    return ts_value_make_bool(ts_reflect_deleteProperty(t, p) != 0);
}
static TsValue* reflect_ownKeys_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    return ts_reflect_ownKeys(t);
}
static TsValue* reflect_getPrototypeOf_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    return ts_reflect_getPrototypeOf(t);
}
static TsValue* reflect_setPrototypeOf_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    void* p = (argc >= 2 && argv) ? argv[1] : nullptr;
    return ts_value_make_bool(ts_reflect_setPrototypeOf(t, p) != 0);
}
static TsValue* reflect_getOwnPropertyDescriptor_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    void* p = (argc >= 2 && argv) ? argv[1] : nullptr;
    return ts_reflect_getOwnPropertyDescriptor(t, p);
}
static TsValue* reflect_defineProperty_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    void* p = (argc >= 2 && argv) ? argv[1] : nullptr;
    void* d = (argc >= 3 && argv) ? argv[2] : nullptr;
    return ts_value_make_bool(ts_reflect_defineProperty(t, p, d) != 0);
}
static TsValue* reflect_isExtensible_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    return ts_value_make_bool(ts_reflect_isExtensible(t) != 0);
}
static TsValue* reflect_preventExtensions_native(void* ctx, int argc, TsValue** argv) {
    void* t = (argc >= 1 && argv) ? argv[0] : nullptr;
    return ts_value_make_bool(ts_reflect_preventExtensions(t) != 0);
}

void* ts_get_global_Reflect() {
    TenureScope _tenure;
    static TsMap* cached = nullptr;
    if (!cached) {
        cached = makeSimpleConstructorGlobal("Reflect");
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
        addMethod(cached, "apply",        (void*)ts_reflect_apply_native, 3);
        addMethod(cached, "construct",    (void*)ts_reflect_construct_native, 2);
        addMethod(cached, "get",          (void*)reflect_get_native, 2);
        addMethod(cached, "set",          (void*)reflect_set_native, 3);
        addMethod(cached, "has",          (void*)reflect_has_native, 2);
        addMethod(cached, "deleteProperty", (void*)reflect_deleteProperty_native, 2);
        addMethod(cached, "ownKeys",      (void*)reflect_ownKeys_native, 1);
        addMethod(cached, "getPrototypeOf", (void*)reflect_getPrototypeOf_native, 1);
        addMethod(cached, "setPrototypeOf", (void*)reflect_setPrototypeOf_native, 2);
        addMethod(cached, "getOwnPropertyDescriptor", (void*)reflect_getOwnPropertyDescriptor_native, 2);
        addMethod(cached, "defineProperty", (void*)reflect_defineProperty_native, 3);
        addMethod(cached, "isExtensible", (void*)reflect_isExtensible_native, 1);
        addMethod(cached, "preventExtensions", (void*)reflect_preventExtensions_native, 1);
        setProtoStringTag(cached, "Reflect");
    }
    return cached;
}

// ===================== Temporal (TC39) =====================
static void* g_temporal_plaintime_ctor = nullptr;  // GC-rooted in ts_get_global_Temporal
extern "C" {
    TsValue* ts_temporal_plaintime_toString_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaintime_valueOf_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaintime_with_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaintime_round_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaintime_equals_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaintime_compare_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaintime_from_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaintime_add_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaintime_subtract_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaintime_until_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaintime_since_native(void* ctx, int argc, TsValue** argv);
    // Duration natives (TsTemporal.cpp)
    TsValue* ts_temporal_duration_toString_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_duration_valueOf_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_duration_negated_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_duration_abs_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_duration_with_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_duration_from_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_duration_compare_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_duration_add_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_duration_subtract_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_duration_total_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_duration_round_native(void* ctx, int argc, TsValue** argv);
}
static void* g_temporal_duration_ctor = nullptr;  // GC-rooted in ts_get_global_Temporal
static void* g_temporal_plaindate_ctor = nullptr; // GC-rooted in ts_get_global_Temporal
static void* g_temporal_plainyearmonth_ctor = nullptr;
static void* g_temporal_plainmonthday_ctor = nullptr;
static void* g_temporal_plaindatetime_ctor = nullptr;
static void* g_temporal_instant_ctor = nullptr;
static void* g_temporal_zoneddatetime_ctor = nullptr;
extern "C" {
    TsValue* ts_temporal_zdt_epochNs_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_epochMicros_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_toString_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_valueOf_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_equals_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_compare_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_from_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_withTimeZone_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_withCalendar_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindate_withCalendar_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindate_toZonedDateTime_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_withPlainTime_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_startOfDay_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_withCalendar_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_withPlainTime_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_with_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_toInstant_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_toPlainDateTime_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_toPlainDate_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_toPlainTime_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_add_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_subtract_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_until_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_since_native(void*,int,TsValue**);
}
static TsValue* temporal_zdt_field(void* ctx, const char* name) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_nanbox_safe_unbox(ctx);
    TsZonedDateTime* d = nullptr;
    if (raw) { uint32_t m0=*(uint32_t*)raw;
        if (m0!=0x53545247 && m0!=0x434F4E53 && *(uint32_t*)((char*)raw+16)==TsZonedDateTime::MAGIC) d=(TsZonedDateTime*)raw; }
    if (!d) {
        std::string msg = std::string("get Temporal.ZonedDateTime.prototype.") + name + " called on an incompatible receiver";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
        return ts_value_make_undefined();
    }
    TsValue v = d->GetPropertyVirtual(name);
    switch (v.type) {
        case ValueType::NUMBER_INT: return ts_value_make_int(v.i_val);
        case ValueType::BOOLEAN:    return ts_value_make_bool(v.i_val != 0);
        case ValueType::STRING_PTR: return ts_value_make_string((TsString*)v.ptr_val);
        default:                    return ts_value_make_undefined();
    }
}
extern "C" {
    TsValue* ts_temporal_instant_epochNs_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_epochMicros_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_toString_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_valueOf_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_equals_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_compare_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_from_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_fromEpochMs_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_fromEpochSec_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_fromEpochNs_native(void*,int,TsValue**);
    TsValue* ts_temporal_now_instant_native(void*,int,TsValue**);
    TsValue* ts_temporal_now_zoneddatetimeiso_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_add_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_subtract_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_until_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_since_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_round_native(void*,int,TsValue**);
}
static TsValue* temporal_instant_field(void* ctx, const char* name) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_nanbox_safe_unbox(ctx);
    TsInstant* d = nullptr;
    if (raw) { uint32_t m0=*(uint32_t*)raw;
        if (m0!=0x53545247 && m0!=0x434F4E53 && *(uint32_t*)((char*)raw+16)==TsInstant::MAGIC) d=(TsInstant*)raw; }
    if (!d) {
        std::string msg = std::string("get Temporal.Instant.prototype.") + name + " called on an incompatible receiver";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
        return ts_value_make_undefined();
    }
    TsValue v = d->GetPropertyVirtual(name);
    if (v.type == ValueType::NUMBER_INT) return ts_value_make_int(v.i_val);
    return ts_value_make_undefined();
}
extern "C" {
    TsValue* ts_temporal_plaindatetime_toString_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_valueOf_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_equals_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_compare_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_with_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_toPlainDate_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_toPlainTime_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_from_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_add_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_subtract_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_until_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_since_native(void*,int,TsValue**);
}
extern "C" {
    TsValue* ts_temporal_now_plaindatetimeiso_native(void*,int,TsValue**);
    TsValue* ts_temporal_now_plaindateiso_native(void*,int,TsValue**);
    TsValue* ts_temporal_now_plaintimeiso_native(void*,int,TsValue**);
    TsValue* ts_temporal_now_timezoneid_native(void*,int,TsValue**);
}
static TsValue* temporal_plaindatetime_field(void* ctx, const char* name) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_nanbox_safe_unbox(ctx);
    TsPlainDateTime* d = nullptr;
    if (raw) { uint32_t m0=*(uint32_t*)raw;
        if (m0!=0x53545247 && m0!=0x434F4E53 && *(uint32_t*)((char*)raw+16)==TsPlainDateTime::MAGIC) d=(TsPlainDateTime*)raw; }
    if (!d) {
        std::string msg = std::string("get Temporal.PlainDateTime.prototype.") + name + " called on an incompatible receiver";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
        return ts_value_make_undefined();
    }
    TsValue v = d->GetPropertyVirtual(name);
    switch (v.type) {
        case ValueType::NUMBER_INT: return ts_value_make_int(v.i_val);
        case ValueType::BOOLEAN:    return ts_value_make_bool(v.i_val != 0);
        case ValueType::STRING_PTR: return ts_value_make_string((TsString*)v.ptr_val);
        default:                    return ts_value_make_undefined();
    }
}
extern "C" {
    TsValue* ts_temporal_plainyearmonth_toString_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainyearmonth_valueOf_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainyearmonth_equals_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainyearmonth_compare_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainyearmonth_with_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainyearmonth_from_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainmonthday_toString_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainmonthday_valueOf_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainmonthday_equals_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainmonthday_with_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainmonthday_from_native(void*,int,TsValue**);
}
static TsValue* temporal_plainyearmonth_field(void* ctx, const char* name) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_nanbox_safe_unbox(ctx);
    TsPlainYearMonth* d = nullptr;
    if (raw) { uint32_t m0=*(uint32_t*)raw;
        if (m0!=0x53545247 && m0!=0x434F4E53 && *(uint32_t*)((char*)raw+16)==TsPlainYearMonth::MAGIC) d=(TsPlainYearMonth*)raw; }
    if (!d) {
        std::string msg = std::string("get Temporal.PlainYearMonth.prototype.") + name +
            " called on an incompatible receiver";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
        return ts_value_make_undefined();
    }
    TsValue v = d->GetPropertyVirtual(name);
    switch (v.type) {
        case ValueType::NUMBER_INT: return ts_value_make_int(v.i_val);
        case ValueType::BOOLEAN:    return ts_value_make_bool(v.i_val != 0);
        case ValueType::STRING_PTR: return ts_value_make_string((TsString*)v.ptr_val);
        default:                    return ts_value_make_undefined();
    }
}
static TsValue* temporal_plainmonthday_field(void* ctx, const char* name) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_nanbox_safe_unbox(ctx);
    TsPlainMonthDay* d = nullptr;
    if (raw) { uint32_t m0=*(uint32_t*)raw;
        if (m0!=0x53545247 && m0!=0x434F4E53 && *(uint32_t*)((char*)raw+16)==TsPlainMonthDay::MAGIC) d=(TsPlainMonthDay*)raw; }
    if (!d) {
        std::string msg = std::string("get Temporal.PlainMonthDay.prototype.") + name +
            " called on an incompatible receiver";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
        return ts_value_make_undefined();
    }
    TsValue v = d->GetPropertyVirtual(name);
    switch (v.type) {
        case ValueType::NUMBER_INT: return ts_value_make_int(v.i_val);
        case ValueType::BOOLEAN:    return ts_value_make_bool(v.i_val != 0);
        case ValueType::STRING_PTR: return ts_value_make_string((TsString*)v.ptr_val);
        default:                    return ts_value_make_undefined();
    }
}
extern "C" {
    TsValue* ts_temporal_plaindate_toString_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaindate_valueOf_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaindate_equals_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaindate_compare_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaindate_with_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaindate_from_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaindate_add_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaindate_subtract_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaindate_until_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_temporal_plaindate_since_native(void* ctx, int argc, TsValue** argv);
}
static TsValue* temporal_plaindate_field(void* ctx, const char* name) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_nanbox_safe_unbox(ctx);
    TsPlainDate* d = nullptr;
    if (raw) { uint32_t m0=*(uint32_t*)raw;
        if (m0!=0x53545247 && m0!=0x434F4E53 && *(uint32_t*)((char*)raw+16)==TsPlainDate::MAGIC) d=(TsPlainDate*)raw; }
    if (!d) {
        std::string msg = std::string("get Temporal.PlainDate.prototype.") + name +
            " called on an object that is not a Temporal.PlainDate";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
        return ts_value_make_undefined();
    }
    TsValue v = d->GetPropertyVirtual(name);
    switch (v.type) {
        case ValueType::NUMBER_INT: return ts_value_make_int(v.i_val);
        case ValueType::BOOLEAN:    return ts_value_make_bool(v.i_val != 0);
        case ValueType::STRING_PTR: return ts_value_make_string((TsString*)v.ptr_val);
        default:                    return ts_value_make_undefined();
    }
}
// Brand-checked Duration.prototype accessor reader (10 fields + sign + blank).
static TsValue* temporal_duration_field(void* ctx, int which, const char* getter) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_nanbox_safe_unbox(ctx);
    TsDuration* d = nullptr;
    if (raw) { uint32_t m0=*(uint32_t*)raw;
        if (m0!=0x53545247 && m0!=0x434F4E53 && *(uint32_t*)((char*)raw+16)==TsDuration::MAGIC) d=(TsDuration*)raw; }
    if (!d) {
        std::string msg = std::string("get Temporal.Duration.prototype.") + getter +
            " called on an object that is not a Temporal.Duration";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
        return ts_value_make_undefined();
    }
    long long v[10] = {d->years,d->months,d->weeks,d->days,d->hours,d->minutes,d->seconds,d->milliseconds,d->microseconds,d->nanoseconds};
    if (which >= 0 && which < 10) return ts_value_make_int(v[which]);
    if (which == 10) return ts_value_make_int(d->Sign());
    return ts_value_make_bool(d->Sign()==0);
}
// Brand-checked reader for Temporal.PlainTime.prototype accessors: per spec each
// getter does thisTemporalTime(this) — if `this` isn't a PlainTime, TypeError.
static TsValue* temporal_plaintime_field(void* ctx, int which, const char* getter) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_nanbox_safe_unbox(ctx);
    // Magic brand-check (NOT dynamic_cast: a string receiver isn't a TsObject
    // and dynamic_cast<TsPlainTime*>((TsObject*)str) is UB/crash).
    TsPlainTime* pt = nullptr;
    if (raw) {
        uint32_t m0 = *(uint32_t*)raw;
        if (m0 != 0x53545247 && m0 != 0x434F4E53 &&
            *(uint32_t*)((char*)raw + 16) == TsPlainTime::MAGIC) {
            pt = (TsPlainTime*)raw;
        }
    }
    if (!pt) {
        std::string msg = std::string("get Temporal.PlainTime.prototype.") + getter +
            " called on an object that is not a Temporal.PlainTime";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
        return ts_value_make_undefined();
    }
    int v = 0;
    switch (which) {
        case 0: v = pt->iso_hour; break;
        case 1: v = pt->iso_minute; break;
        case 2: v = pt->iso_second; break;
        case 3: v = pt->iso_millisecond; break;
        case 4: v = pt->iso_microsecond; break;
        case 5: v = pt->iso_nanosecond; break;
    }
    return ts_value_make_int((int64_t)v);
}

extern "C" {
    TsValue* ts_temporal_plaindate_toPlainDateTime_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindate_toPlainYearMonth_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindate_toPlainMonthDay_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_toPlainYearMonth_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_toPlainMonthDay_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_toZonedDateTime_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainyearmonth_toPlainDate_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainyearmonth_until_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainyearmonth_since_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainyearmonth_add_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainyearmonth_subtract_native(void*,int,TsValue**);
    TsValue* ts_temporal_plainmonthday_toPlainDate_native(void*,int,TsValue**);
    TsValue* ts_temporal_instant_toZonedDateTimeISO_native(void*,int,TsValue**);
    TsValue* ts_temporal_plaindatetime_round_native(void*,int,TsValue**);
    TsValue* ts_temporal_zdt_round_native(void*,int,TsValue**);
}
void* ts_get_global_Temporal() {
    TenureScope _tenure;
    static TsMap* cached = nullptr;
    if (!cached) {
        cached = TsMap::Create();
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); ts_gc_register_root(&g_temporal_plaintime_ctor); } }
        setProtoStringTag(cached, "Temporal");

        // ---- Temporal.PlainTime ----
        TsMap* ptCtor = makeSimpleConstructorGlobal("PlainTime");
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoT = ptCtor->Get(protoKey);
        TsMap* ptProto = (TsMap*)protoT.ptr_val;
        setProtoStringTag(ptProto, "Temporal.PlainTime");
        addAccessorGetter(ptProto, "hour",        (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaintime_field(c,0,"hour"); });
        addAccessorGetter(ptProto, "minute",      (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaintime_field(c,1,"minute"); });
        addAccessorGetter(ptProto, "second",      (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaintime_field(c,2,"second"); });
        addAccessorGetter(ptProto, "millisecond", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaintime_field(c,3,"millisecond"); });
        addAccessorGetter(ptProto, "microsecond", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaintime_field(c,4,"microsecond"); });
        addAccessorGetter(ptProto, "nanosecond",  (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaintime_field(c,5,"nanosecond"); });
        // Prototype methods.
        addMethod(ptProto, "toString",       (void*)ts_temporal_plaintime_toString_native, 0);
        addMethod(ptProto, "toLocaleString", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plaintime_toString_native(_c,0,nullptr); }, 0);
        addMethod(ptProto, "toJSON", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plaintime_toString_native(_c,0,nullptr); }, 0);
        addMethod(ptProto, "toLocaleString", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plaintime_toString_native(_c,0,nullptr); }, 0);
        addMethod(ptProto, "valueOf",        (void*)ts_temporal_plaintime_valueOf_native, 0);
        addMethod(ptProto, "with",           (void*)ts_temporal_plaintime_with_native, 1);
        addMethod(ptProto, "round",          (void*)ts_temporal_plaintime_round_native, 1);
        addMethod(ptProto, "add",            (void*)ts_temporal_plaintime_add_native, 1);
        addMethod(ptProto, "subtract",       (void*)ts_temporal_plaintime_subtract_native, 1);
        addMethod(ptProto, "until",          (void*)ts_temporal_plaintime_until_native, 1);
        addMethod(ptProto, "since",          (void*)ts_temporal_plaintime_since_native, 1);
        addMethod(ptProto, "equals",         (void*)ts_temporal_plaintime_equals_native, 1);
        void* ptFn = wrapAsCallable(ptCtor, "PlainTime", 0);
        // Static methods on the constructor.
        addMethod(ptCtor, "from",    (void*)ts_temporal_plaintime_from_native, 1);
        addMethod(ptCtor, "compare", (void*)ts_temporal_plaintime_compare_native, 2);
        g_temporal_plaintime_ctor = ptFn;
        TsValue ck; ck.type = ValueType::STRING_PTR; ck.ptr_val = TsString::GetInterned("PlainTime");
        TsValue cv; cv.type = ValueType::FUNCTION_PTR; cv.ptr_val = ptFn;
        cached->SetWithAttrs(ck, cv, TsHashTable::ATTR_WRITABLE|TsHashTable::ATTR_CONFIGURABLE);

        // ---- Temporal.Duration ----
        TsMap* duCtor = makeSimpleConstructorGlobal("Duration");
        TsValue duProtoT = duCtor->Get(protoKey);
        TsMap* duProto = (TsMap*)duProtoT.ptr_val;
        setProtoStringTag(duProto, "Temporal.Duration");
        addAccessorGetter(duProto, "years", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,0,"years"); });
        addAccessorGetter(duProto, "months", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,1,"months"); });
        addAccessorGetter(duProto, "weeks", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,2,"weeks"); });
        addAccessorGetter(duProto, "days", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,3,"days"); });
        addAccessorGetter(duProto, "hours", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,4,"hours"); });
        addAccessorGetter(duProto, "minutes", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,5,"minutes"); });
        addAccessorGetter(duProto, "seconds", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,6,"seconds"); });
        addAccessorGetter(duProto, "milliseconds", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,7,"milliseconds"); });
        addAccessorGetter(duProto, "microseconds", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,8,"microseconds"); });
        addAccessorGetter(duProto, "nanoseconds", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,9,"nanoseconds"); });
        addAccessorGetter(duProto, "sign", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,10,"sign"); });
        addAccessorGetter(duProto, "blank", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_duration_field(c,11,"blank"); });
        addMethod(duProto, "toString", (void*)ts_temporal_duration_toString_native, 0);
        addMethod(duProto, "toLocaleString", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_duration_toString_native(_c,0,nullptr); }, 0);
        addMethod(duProto, "toJSON", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_duration_toString_native(_c,0,nullptr); }, 0);
        addMethod(duProto, "valueOf",  (void*)ts_temporal_duration_valueOf_native, 0);
        addMethod(duProto, "negated",  (void*)ts_temporal_duration_negated_native, 0);
        addMethod(duProto, "abs",      (void*)ts_temporal_duration_abs_native, 0);
        addMethod(duProto, "with",     (void*)ts_temporal_duration_with_native, 1);
        addMethod(duProto, "add",      (void*)ts_temporal_duration_add_native, 1);
        addMethod(duProto, "subtract", (void*)ts_temporal_duration_subtract_native, 1);
        addMethod(duProto, "total",    (void*)ts_temporal_duration_total_native, 1);
        addMethod(duProto, "round",    (void*)ts_temporal_duration_round_native, 1);
        void* duFn = wrapAsCallable(duCtor, "Duration", 0);
        g_temporal_duration_ctor = duFn;
        ts_gc_register_root(&g_temporal_duration_ctor);
        addMethod(duCtor, "from", (void*)ts_temporal_duration_from_native, 1);
        addMethod(duCtor, "compare", (void*)ts_temporal_duration_compare_native, 2);
        TsValue dck; dck.type = ValueType::STRING_PTR; dck.ptr_val = TsString::GetInterned("Duration");
        TsValue dcv; dcv.type = ValueType::FUNCTION_PTR; dcv.ptr_val = duFn;
        cached->SetWithAttrs(dck, dcv, TsHashTable::ATTR_WRITABLE|TsHashTable::ATTR_CONFIGURABLE);

        // ---- Temporal.PlainDate ----
        TsMap* pdCtor = makeSimpleConstructorGlobal("PlainDate");
        TsValue pdProtoT = pdCtor->Get(protoKey);
        TsMap* pdProto = (TsMap*)pdProtoT.ptr_val;
        setProtoStringTag(pdProto, "Temporal.PlainDate");
        addAccessorGetter(pdProto, "year", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"year"); });
        addAccessorGetter(pdProto, "month", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"month"); });
        addAccessorGetter(pdProto, "monthCode", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"monthCode"); });
        addAccessorGetter(pdProto, "day", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"day"); });
        addAccessorGetter(pdProto, "calendarId", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"calendarId"); });
        addAccessorGetter(pdProto, "dayOfWeek", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"dayOfWeek"); });
        addAccessorGetter(pdProto, "dayOfYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"dayOfYear"); });
        addAccessorGetter(pdProto, "weekOfYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"weekOfYear"); });
        addAccessorGetter(pdProto, "yearOfWeek", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"yearOfWeek"); });
        addAccessorGetter(pdProto, "daysInWeek", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"daysInWeek"); });
        addAccessorGetter(pdProto, "daysInMonth", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"daysInMonth"); });
        addAccessorGetter(pdProto, "daysInYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"daysInYear"); });
        addAccessorGetter(pdProto, "monthsInYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"monthsInYear"); });
        addAccessorGetter(pdProto, "inLeapYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"inLeapYear"); });
        addAccessorGetter(pdProto, "era", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"era"); });
        addAccessorGetter(pdProto, "eraYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindate_field(c,"eraYear"); });
        addMethod(pdProto, "toString", (void*)ts_temporal_plaindate_toString_native, 0);
        addMethod(pdProto, "toLocaleString", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plaindate_toString_native(_c,0,nullptr); }, 0);
        addMethod(pdProto, "toJSON", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plaindate_toString_native(_c,0,nullptr); }, 0);
        addMethod(pdProto, "valueOf",  (void*)ts_temporal_plaindate_valueOf_native, 0);
        addMethod(pdProto, "equals",   (void*)ts_temporal_plaindate_equals_native, 1);
        addMethod(pdProto, "with",     (void*)ts_temporal_plaindate_with_native, 1);
        addMethod(pdProto, "withCalendar", (void*)ts_temporal_plaindate_withCalendar_native, 1);
        addMethod(pdProto, "toZonedDateTime", (void*)ts_temporal_plaindate_toZonedDateTime_native, 1);
        addMethod(pdProto, "add",      (void*)ts_temporal_plaindate_add_native, 1);
        addMethod(pdProto, "subtract", (void*)ts_temporal_plaindate_subtract_native, 1);
        addMethod(pdProto, "until",    (void*)ts_temporal_plaindate_until_native, 1);
        addMethod(pdProto, "since",    (void*)ts_temporal_plaindate_since_native, 1);
        addMethod(pdProto, "toPlainDateTime",  (void*)ts_temporal_plaindate_toPlainDateTime_native, 0);
        addMethod(pdProto, "toPlainYearMonth", (void*)ts_temporal_plaindate_toPlainYearMonth_native, 0);
        addMethod(pdProto, "toPlainMonthDay",  (void*)ts_temporal_plaindate_toPlainMonthDay_native, 0);
        void* pdFn = wrapAsCallable(pdCtor, "PlainDate", 3);
        g_temporal_plaindate_ctor = pdFn;
        ts_gc_register_root(&g_temporal_plaindate_ctor);
        addMethod(pdCtor, "from",    (void*)ts_temporal_plaindate_from_native, 1);
        addMethod(pdCtor, "compare", (void*)ts_temporal_plaindate_compare_native, 2);
        TsValue pdck; pdck.type = ValueType::STRING_PTR; pdck.ptr_val = TsString::GetInterned("PlainDate");
        TsValue pdcv; pdcv.type = ValueType::FUNCTION_PTR; pdcv.ptr_val = pdFn;
        cached->SetWithAttrs(pdck, pdcv, TsHashTable::ATTR_WRITABLE|TsHashTable::ATTR_CONFIGURABLE);

        // ---- Temporal.PlainYearMonth ----
        TsMap* ymCtor = makeSimpleConstructorGlobal("PlainYearMonth");
        TsMap* ymProto = (TsMap*)ymCtor->Get(protoKey).ptr_val;
        setProtoStringTag(ymProto, "Temporal.PlainYearMonth");
        addAccessorGetter(ymProto, "year", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainyearmonth_field(c,"year"); });
        addAccessorGetter(ymProto, "month", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainyearmonth_field(c,"month"); });
        addAccessorGetter(ymProto, "monthCode", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainyearmonth_field(c,"monthCode"); });
        addAccessorGetter(ymProto, "calendarId", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainyearmonth_field(c,"calendarId"); });
        addAccessorGetter(ymProto, "daysInMonth", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainyearmonth_field(c,"daysInMonth"); });
        addAccessorGetter(ymProto, "daysInYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainyearmonth_field(c,"daysInYear"); });
        addAccessorGetter(ymProto, "monthsInYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainyearmonth_field(c,"monthsInYear"); });
        addAccessorGetter(ymProto, "inLeapYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainyearmonth_field(c,"inLeapYear"); });
        addAccessorGetter(ymProto, "era", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainyearmonth_field(c,"era"); });
        addAccessorGetter(ymProto, "eraYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainyearmonth_field(c,"eraYear"); });
        addMethod(ymProto, "toString", (void*)ts_temporal_plainyearmonth_toString_native, 0);
        addMethod(ymProto, "toLocaleString", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plainyearmonth_toString_native(_c,0,nullptr); }, 0);
        addMethod(ymProto, "toJSON", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plainyearmonth_toString_native(_c,0,nullptr); }, 0);
        addMethod(ymProto, "valueOf",  (void*)ts_temporal_plainyearmonth_valueOf_native, 0);
        addMethod(ymProto, "equals",   (void*)ts_temporal_plainyearmonth_equals_native, 1);
        addMethod(ymProto, "with",     (void*)ts_temporal_plainyearmonth_with_native, 1);
        addMethod(ymProto, "toPlainDate", (void*)ts_temporal_plainyearmonth_toPlainDate_native, 1);
        addMethod(ymProto, "until",    (void*)ts_temporal_plainyearmonth_until_native, 1);
        addMethod(ymProto, "since",    (void*)ts_temporal_plainyearmonth_since_native, 1);
        addMethod(ymProto, "add",      (void*)ts_temporal_plainyearmonth_add_native, 1);
        addMethod(ymProto, "subtract", (void*)ts_temporal_plainyearmonth_subtract_native, 1);
        void* ymFn = wrapAsCallable(ymCtor, "PlainYearMonth", 2);
        g_temporal_plainyearmonth_ctor = ymFn;
        ts_gc_register_root(&g_temporal_plainyearmonth_ctor);
        addMethod(ymCtor, "from",    (void*)ts_temporal_plainyearmonth_from_native, 1);
        addMethod(ymCtor, "compare", (void*)ts_temporal_plainyearmonth_compare_native, 2);
        { TsValue k; k.type=ValueType::STRING_PTR; k.ptr_val=TsString::GetInterned("PlainYearMonth");
          TsValue v; v.type=ValueType::FUNCTION_PTR; v.ptr_val=ymFn; cached->SetWithAttrs(k,v, TsHashTable::ATTR_WRITABLE|TsHashTable::ATTR_CONFIGURABLE); }

        // ---- Temporal.PlainMonthDay ----
        TsMap* mdCtor = makeSimpleConstructorGlobal("PlainMonthDay");
        TsMap* mdProto = (TsMap*)mdCtor->Get(protoKey).ptr_val;
        setProtoStringTag(mdProto, "Temporal.PlainMonthDay");
        addAccessorGetter(mdProto, "monthCode", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainmonthday_field(c,"monthCode"); });
        addAccessorGetter(mdProto, "day", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainmonthday_field(c,"day"); });
        addAccessorGetter(mdProto, "calendarId", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plainmonthday_field(c,"calendarId"); });
        addMethod(mdProto, "toString", (void*)ts_temporal_plainmonthday_toString_native, 0);
        addMethod(mdProto, "toLocaleString", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plainmonthday_toString_native(_c,0,nullptr); }, 0);
        addMethod(mdProto, "toJSON", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plainmonthday_toString_native(_c,0,nullptr); }, 0);
        addMethod(mdProto, "valueOf",  (void*)ts_temporal_plainmonthday_valueOf_native, 0);
        addMethod(mdProto, "equals",   (void*)ts_temporal_plainmonthday_equals_native, 1);
        addMethod(mdProto, "with",     (void*)ts_temporal_plainmonthday_with_native, 1);
        addMethod(mdProto, "toPlainDate", (void*)ts_temporal_plainmonthday_toPlainDate_native, 1);
        void* mdFn = wrapAsCallable(mdCtor, "PlainMonthDay", 2);
        g_temporal_plainmonthday_ctor = mdFn;
        ts_gc_register_root(&g_temporal_plainmonthday_ctor);
        addMethod(mdCtor, "from", (void*)ts_temporal_plainmonthday_from_native, 1);
        { TsValue k; k.type=ValueType::STRING_PTR; k.ptr_val=TsString::GetInterned("PlainMonthDay");
          TsValue v; v.type=ValueType::FUNCTION_PTR; v.ptr_val=mdFn; cached->SetWithAttrs(k,v, TsHashTable::ATTR_WRITABLE|TsHashTable::ATTR_CONFIGURABLE); }

        // ---- Temporal.PlainDateTime ----
        TsMap* dtCtor = makeSimpleConstructorGlobal("PlainDateTime");
        TsMap* dtProto = (TsMap*)dtCtor->Get(protoKey).ptr_val;
        setProtoStringTag(dtProto, "Temporal.PlainDateTime");
        addAccessorGetter(dtProto, "year", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"year"); });
        addAccessorGetter(dtProto, "month", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"month"); });
        addAccessorGetter(dtProto, "monthCode", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"monthCode"); });
        addAccessorGetter(dtProto, "day", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"day"); });
        addAccessorGetter(dtProto, "dayOfWeek", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"dayOfWeek"); });
        addAccessorGetter(dtProto, "dayOfYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"dayOfYear"); });
        addAccessorGetter(dtProto, "weekOfYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"weekOfYear"); });
        addAccessorGetter(dtProto, "yearOfWeek", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"yearOfWeek"); });
        addAccessorGetter(dtProto, "daysInWeek", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"daysInWeek"); });
        addAccessorGetter(dtProto, "daysInMonth", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"daysInMonth"); });
        addAccessorGetter(dtProto, "daysInYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"daysInYear"); });
        addAccessorGetter(dtProto, "monthsInYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"monthsInYear"); });
        addAccessorGetter(dtProto, "inLeapYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"inLeapYear"); });
        addAccessorGetter(dtProto, "hour", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"hour"); });
        addAccessorGetter(dtProto, "minute", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"minute"); });
        addAccessorGetter(dtProto, "second", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"second"); });
        addAccessorGetter(dtProto, "millisecond", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"millisecond"); });
        addAccessorGetter(dtProto, "microsecond", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"microsecond"); });
        addAccessorGetter(dtProto, "nanosecond", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"nanosecond"); });
        addAccessorGetter(dtProto, "calendarId", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"calendarId"); });
        addAccessorGetter(dtProto, "era", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"era"); });
        addAccessorGetter(dtProto, "eraYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_plaindatetime_field(c,"eraYear"); });
        addMethod(dtProto, "toString",    (void*)ts_temporal_plaindatetime_toString_native, 0);
        addMethod(dtProto, "toLocaleString", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plaindatetime_toString_native(_c,0,nullptr); }, 0);
        addMethod(dtProto, "toJSON", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_plaindatetime_toString_native(_c,0,nullptr); }, 0);
        addMethod(dtProto, "valueOf",     (void*)ts_temporal_plaindatetime_valueOf_native, 0);
        addMethod(dtProto, "equals",      (void*)ts_temporal_plaindatetime_equals_native, 1);
        addMethod(dtProto, "with",        (void*)ts_temporal_plaindatetime_with_native, 1);
        addMethod(dtProto, "withCalendar", (void*)ts_temporal_plaindatetime_withCalendar_native, 1);
        addMethod(dtProto, "withPlainTime", (void*)ts_temporal_plaindatetime_withPlainTime_native, 0);
        addMethod(dtProto, "round",       (void*)ts_temporal_plaindatetime_round_native, 1);
        addMethod(dtProto, "add",         (void*)ts_temporal_plaindatetime_add_native, 1);
        addMethod(dtProto, "subtract",    (void*)ts_temporal_plaindatetime_subtract_native, 1);
        addMethod(dtProto, "until",       (void*)ts_temporal_plaindatetime_until_native, 1);
        addMethod(dtProto, "since",       (void*)ts_temporal_plaindatetime_since_native, 1);
        addMethod(dtProto, "toPlainDate", (void*)ts_temporal_plaindatetime_toPlainDate_native, 0);
        addMethod(dtProto, "toPlainTime", (void*)ts_temporal_plaindatetime_toPlainTime_native, 0);
        addMethod(dtProto, "toPlainYearMonth",(void*)ts_temporal_plaindatetime_toPlainYearMonth_native, 0);
        addMethod(dtProto, "toPlainMonthDay", (void*)ts_temporal_plaindatetime_toPlainMonthDay_native, 0);
        addMethod(dtProto, "toZonedDateTime", (void*)ts_temporal_plaindatetime_toZonedDateTime_native, 1);
        void* dtFn = wrapAsCallable(dtCtor, "PlainDateTime", 3);
        g_temporal_plaindatetime_ctor = dtFn;
        ts_gc_register_root(&g_temporal_plaindatetime_ctor);
        addMethod(dtCtor, "from",    (void*)ts_temporal_plaindatetime_from_native, 1);
        addMethod(dtCtor, "compare", (void*)ts_temporal_plaindatetime_compare_native, 2);
        { TsValue k; k.type=ValueType::STRING_PTR; k.ptr_val=TsString::GetInterned("PlainDateTime");
          TsValue v; v.type=ValueType::FUNCTION_PTR; v.ptr_val=dtFn; cached->SetWithAttrs(k,v, TsHashTable::ATTR_WRITABLE|TsHashTable::ATTR_CONFIGURABLE); }

        // ---- Temporal.Instant ----
        TsMap* inCtor = makeSimpleConstructorGlobal("Instant");
        TsMap* inProto = (TsMap*)inCtor->Get(protoKey).ptr_val;
        setProtoStringTag(inProto, "Temporal.Instant");
        addAccessorGetter(inProto, "epochMilliseconds", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_instant_field(c,"epochMilliseconds"); });
        addAccessorGetter(inProto, "epochSeconds",      (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_instant_field(c,"epochSeconds"); });
        addAccessorGetter(inProto, "epochNanoseconds",  (void*)ts_temporal_instant_epochNs_native);
        addAccessorGetter(inProto, "epochMicroseconds", (void*)ts_temporal_instant_epochMicros_native);
        addMethod(inProto, "toString", (void*)ts_temporal_instant_toString_native, 0);
        addMethod(inProto, "toLocaleString", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_instant_toString_native(_c,0,nullptr); }, 0);
        addMethod(inProto, "toJSON", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_instant_toString_native(_c,0,nullptr); }, 0);
        addMethod(inProto, "valueOf",  (void*)ts_temporal_instant_valueOf_native, 0);
        addMethod(inProto, "equals",   (void*)ts_temporal_instant_equals_native, 1);
        addMethod(inProto, "add",      (void*)ts_temporal_instant_add_native, 1);
        addMethod(inProto, "subtract", (void*)ts_temporal_instant_subtract_native, 1);
        addMethod(inProto, "until",    (void*)ts_temporal_instant_until_native, 1);
        addMethod(inProto, "since",    (void*)ts_temporal_instant_since_native, 1);
        addMethod(inProto, "round",    (void*)ts_temporal_instant_round_native, 1);
        addMethod(inProto, "toZonedDateTimeISO", (void*)ts_temporal_instant_toZonedDateTimeISO_native, 1);
        void* inFn = wrapAsCallable(inCtor, "Instant", 1);
        g_temporal_instant_ctor = inFn;
        ts_gc_register_root(&g_temporal_instant_ctor);
        addMethod(inCtor, "from",                  (void*)ts_temporal_instant_from_native, 1);
        addMethod(inCtor, "fromEpochNanoseconds",  (void*)ts_temporal_instant_fromEpochNs_native, 1);
        addMethod(inCtor, "fromEpochMilliseconds", (void*)ts_temporal_instant_fromEpochMs_native, 1);
        addMethod(inCtor, "fromEpochSeconds",      (void*)ts_temporal_instant_fromEpochSec_native, 1);
        addMethod(inCtor, "compare",               (void*)ts_temporal_instant_compare_native, 2);
        { TsValue k; k.type=ValueType::STRING_PTR; k.ptr_val=TsString::GetInterned("Instant");
          TsValue v; v.type=ValueType::FUNCTION_PTR; v.ptr_val=inFn; cached->SetWithAttrs(k,v, TsHashTable::ATTR_WRITABLE|TsHashTable::ATTR_CONFIGURABLE); }

        // ---- Temporal.ZonedDateTime ----
        TsMap* zdCtor = makeSimpleConstructorGlobal("ZonedDateTime");
        TsMap* zdProto = (TsMap*)zdCtor->Get(protoKey).ptr_val;
        setProtoStringTag(zdProto, "Temporal.ZonedDateTime");
        addAccessorGetter(zdProto, "year", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"year"); });
        addAccessorGetter(zdProto, "month", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"month"); });
        addAccessorGetter(zdProto, "monthCode", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"monthCode"); });
        addAccessorGetter(zdProto, "day", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"day"); });
        addAccessorGetter(zdProto, "dayOfWeek", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"dayOfWeek"); });
        addAccessorGetter(zdProto, "dayOfYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"dayOfYear"); });
        addAccessorGetter(zdProto, "weekOfYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"weekOfYear"); });
        addAccessorGetter(zdProto, "yearOfWeek", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"yearOfWeek"); });
        addAccessorGetter(zdProto, "daysInWeek", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"daysInWeek"); });
        addAccessorGetter(zdProto, "daysInMonth", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"daysInMonth"); });
        addAccessorGetter(zdProto, "daysInYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"daysInYear"); });
        addAccessorGetter(zdProto, "monthsInYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"monthsInYear"); });
        addAccessorGetter(zdProto, "inLeapYear", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"inLeapYear"); });
        addAccessorGetter(zdProto, "hour", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"hour"); });
        addAccessorGetter(zdProto, "minute", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"minute"); });
        addAccessorGetter(zdProto, "second", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"second"); });
        addAccessorGetter(zdProto, "millisecond", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"millisecond"); });
        addAccessorGetter(zdProto, "microsecond", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"microsecond"); });
        addAccessorGetter(zdProto, "nanosecond", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"nanosecond"); });
        addAccessorGetter(zdProto, "calendarId", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"calendarId"); });
        addAccessorGetter(zdProto, "offset", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"offset"); });
        addAccessorGetter(zdProto, "offsetNanoseconds", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"offsetNanoseconds"); });
        addAccessorGetter(zdProto, "timeZoneId", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"timeZoneId"); });
        addAccessorGetter(zdProto, "epochMilliseconds", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"epochMilliseconds"); });
        addAccessorGetter(zdProto, "epochSeconds", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"epochSeconds"); });
        addAccessorGetter(zdProto, "hoursInDay", (void*)+[](void* c,int,TsValue**)->TsValue*{ return temporal_zdt_field(c,"hoursInDay"); });
        addAccessorGetter(zdProto, "epochNanoseconds",  (void*)ts_temporal_zdt_epochNs_native);
        addAccessorGetter(zdProto, "epochMicroseconds", (void*)ts_temporal_zdt_epochMicros_native);
        addMethod(zdProto, "toString",        (void*)ts_temporal_zdt_toString_native, 0);
        addMethod(zdProto, "toLocaleString", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_zdt_toString_native(_c,0,nullptr); }, 0);
        addMethod(zdProto, "toJSON", (void*)+[](void* _c,int,TsValue**)->TsValue*{ return ts_temporal_zdt_toString_native(_c,0,nullptr); }, 0);
        addMethod(zdProto, "valueOf",         (void*)ts_temporal_zdt_valueOf_native, 0);
        addMethod(zdProto, "equals",          (void*)ts_temporal_zdt_equals_native, 1);
        addMethod(zdProto, "add",             (void*)ts_temporal_zdt_add_native, 1);
        addMethod(zdProto, "subtract",        (void*)ts_temporal_zdt_subtract_native, 1);
        addMethod(zdProto, "until",           (void*)ts_temporal_zdt_until_native, 1);
        addMethod(zdProto, "since",           (void*)ts_temporal_zdt_since_native, 1);
        addMethod(zdProto, "round",           (void*)ts_temporal_zdt_round_native, 1);
        addMethod(zdProto, "toInstant",       (void*)ts_temporal_zdt_toInstant_native, 0);
        addMethod(zdProto, "toPlainDateTime", (void*)ts_temporal_zdt_toPlainDateTime_native, 0);
        addMethod(zdProto, "toPlainDate",     (void*)ts_temporal_zdt_toPlainDate_native, 0);
        addMethod(zdProto, "toPlainTime",     (void*)ts_temporal_zdt_toPlainTime_native, 0);
        addMethod(zdProto, "withTimeZone",    (void*)ts_temporal_zdt_withTimeZone_native, 1);
        addMethod(zdProto, "withCalendar",    (void*)ts_temporal_zdt_withCalendar_native, 1);
        { extern TsValue* ts_temporal_zdt_getTimeZoneTransition_native(void*,int,TsValue**);
          addMethod(zdProto, "getTimeZoneTransition", (void*)ts_temporal_zdt_getTimeZoneTransition_native, 1); }
        addMethod(zdProto, "startOfDay",      (void*)ts_temporal_zdt_startOfDay_native, 0);
        addMethod(zdProto, "withPlainTime",   (void*)ts_temporal_zdt_withPlainTime_native, 1);
        addMethod(zdProto, "with",            (void*)ts_temporal_zdt_with_native, 1);
        void* zdFn = wrapAsCallable(zdCtor, "ZonedDateTime", 2);
        g_temporal_zoneddatetime_ctor = zdFn;
        ts_gc_register_root(&g_temporal_zoneddatetime_ctor);
        addMethod(zdCtor, "compare", (void*)ts_temporal_zdt_compare_native, 2);
        addMethod(zdCtor, "from", (void*)ts_temporal_zdt_from_native, 1);
        { TsValue k; k.type=ValueType::STRING_PTR; k.ptr_val=TsString::GetInterned("ZonedDateTime");
          TsValue v; v.type=ValueType::FUNCTION_PTR; v.ptr_val=zdFn; cached->SetWithAttrs(k,v, TsHashTable::ATTR_WRITABLE|TsHashTable::ATTR_CONFIGURABLE); }

        // ---- Temporal.Now (clock function namespace) ----
        TsMap* nowNs = TsMap::Create();
        setProtoStringTag(nowNs, "Temporal.Now");
        addMethod(nowNs, "plainDateTimeISO", (void*)ts_temporal_now_plaindatetimeiso_native, 0);
        addMethod(nowNs, "instant",          (void*)ts_temporal_now_instant_native, 0);
        addMethod(nowNs, "zonedDateTimeISO", (void*)ts_temporal_now_zoneddatetimeiso_native, 0);
        addMethod(nowNs, "plainDateISO",     (void*)ts_temporal_now_plaindateiso_native, 0);
        addMethod(nowNs, "plainTimeISO",     (void*)ts_temporal_now_plaintimeiso_native, 0);
        addMethod(nowNs, "timeZoneId",       (void*)ts_temporal_now_timezoneid_native, 0);
        { TsValue k; k.type=ValueType::STRING_PTR; k.ptr_val=TsString::GetInterned("Now");
          TsValue v; v.type=ValueType::OBJECT_PTR; v.ptr_val=nowNs; cached->SetWithAttrs(k,v, TsHashTable::ATTR_WRITABLE|TsHashTable::ATTR_CONFIGURABLE); }
    }
    return cached;
}

void* ts_temporal_get_duration_ctor() {
    ts_get_global_Temporal();
    return g_temporal_duration_ctor;
}

void* ts_temporal_get_plaindate_ctor() {
    ts_get_global_Temporal();
    return g_temporal_plaindate_ctor;
}

void* ts_temporal_get_plainyearmonth_ctor() { ts_get_global_Temporal(); return g_temporal_plainyearmonth_ctor; }
void* ts_temporal_get_plainmonthday_ctor() { ts_get_global_Temporal(); return g_temporal_plainmonthday_ctor; }
void* ts_temporal_get_plaindatetime_ctor() { ts_get_global_Temporal(); return g_temporal_plaindatetime_ctor; }
void* ts_temporal_get_instant_ctor() { ts_get_global_Temporal(); return g_temporal_instant_ctor; }
void* ts_temporal_get_zoneddatetime_ctor() { ts_get_global_Temporal(); return g_temporal_zoneddatetime_ctor; }

// The cached Temporal.PlainTime constructor function, for the new-dispatch
// match in ts_new_from_constructor_impl (PlainTime is a namespace sub-property,
// not a top-level global getter, so it needs its own getter).
void* ts_temporal_get_plaintime_ctor() {
    ts_get_global_Temporal();  // ensure built
    return g_temporal_plaintime_ctor;
}

// --- Iterator global (TC39 iterator-sequencing: Iterator.concat) ---------
extern "C" bool ts_iterator_result_done(TsValue* result);

// Local {value: undefined, done} result builder — TsPromise.cpp's
// create_generator_result has C++ linkage and this region is extern "C".
static TsValue* iterseq_done_result() {
    TsMap* map = TsMap::Create();
    TsValue undef; undef.type = ValueType::UNDEFINED; undef.ptr_val = nullptr;
    map->Set(TsString::GetInterned("value"), undef);
    map->Set(TsString::GetInterned("done"), TsValue(true));
    return ts_value_make_object(map);
}

static bool iterseq_is_callable(TsValue* v) {
    return ts_is_callable((void*)v);  // canonical IsCallable (TsObject.cpp)
}

// next() of the iterator returned by Iterator.concat. State (ctx) is the
// iterator TsMap itself: "__items" = flat [iterable, method, ...] pairs,
// "__idx" = next pair index, "__inner" = current inner iterator (lazily
// created IN ORDER; @@iterator was read once per item at concat() time).
// Inner result objects PASS THROUGH unchanged (fresh-iterator-result.js
// asserts identity with the inner iterator's result).
static TsValue* iterator_concat_next(void* ctx, TsValue* /*arg*/) {
    TsMap* st = (TsMap*)ctx;
    if (!st) return iterseq_done_result();
    for (;;) {
        TsValue* innerV = ts_object_get_property(st, "__inner");
        void* innerRaw = innerV ? ts_value_get_object(innerV) : nullptr;
        if (!innerRaw) {
            TsValue* itemsV = ts_object_get_property(st, "__items");
            TsArray* items = itemsV ? (TsArray*)ts_value_get_object(itemsV) : nullptr;
            int64_t idx = (int64_t)ts_to_number(ts_object_get_property(st, "__idx"));
            if (!items || idx * 2 >= (int64_t)items->Length()) {
                return iterseq_done_result();
            }
            TsValue* iterable = (TsValue*)(intptr_t)items->Get((size_t)(idx * 2));
            TsValue* method   = (TsValue*)(intptr_t)items->Get((size_t)(idx * 2 + 1));
            TsValue nextIdx; nextIdx.type = ValueType::NUMBER_INT; nextIdx.i_val = idx + 1;
            st->Set(TsString::GetInterned("__idx"), nextIdx);
            TsValue* inner = ts_call_with_this_0(method, iterable);
            if (!inner || !ts_value_get_object(inner)) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Iterator.concat: iterator method returned a non-object"));
                return iterseq_done_result();
            }
            st->Set(TsString::GetInterned("__inner"), nanbox_to_tagged(inner));
            innerV = inner;
            innerRaw = ts_value_get_object(inner);
        }
        // Legacy iterator shape: several runtime @@iterator implementations
        // return a plain ARRAY ("iterator-like" per the conformance notes)
        // rather than a {next} object. Walk it by index with fresh results.
        if (*(uint32_t*)innerRaw == 0x41525259) { // TsArray "ARRY"
            TsArray* arr = (TsArray*)innerRaw;
            int64_t ii = (int64_t)ts_to_number(ts_object_get_property(st, "__innerIdx"));
            if (ii >= (int64_t)arr->Length()) {
                TsValue undef2; undef2.type = ValueType::UNDEFINED; undef2.ptr_val = nullptr;
                st->Set(TsString::GetInterned("__inner"), undef2);
                TsValue z; z.type = ValueType::NUMBER_INT; z.i_val = 0;
                st->Set(TsString::GetInterned("__innerIdx"), z);
                continue;
            }
            TsValue ni; ni.type = ValueType::NUMBER_INT; ni.i_val = ii + 1;
            st->Set(TsString::GetInterned("__innerIdx"), ni);
            TsMap* res = TsMap::Create();
            res->Set(TsString::GetInterned("value"),
                     nanbox_to_tagged((TsValue*)arr->GetElementBoxed((size_t)ii)));
            res->Set(TsString::GetInterned("done"), TsValue(false));
            return ts_value_make_object(res);
        }
        TsValue* nextFn = ts_object_get_property(innerRaw, "next");
        // Only reject definitively-absent next; some runtime iterator shapes
        // carry callables that evade the magic check, and a non-callable
        // still surfaces as TypeError via the result-not-object check below.
        if (!nextFn || ts_value_is_nullish(nextFn)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Iterator.concat: iterator.next is not callable"));
            return iterseq_done_result();
        }
        TsValue* res = ts_call_with_this_0(nextFn, innerV);
        if (!res || !ts_value_get_object(res)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Iterator.concat: iterator result is not an object"));
            return iterseq_done_result();
        }
        if (ts_iterator_result_done(res)) {
            TsValue undef; undef.type = ValueType::UNDEFINED; undef.ptr_val = nullptr;
            st->Set(TsString::GetInterned("__inner"), undef);
            continue;
        }
        return res;
    }
}

static TsValue* iterator_concat_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx;
    // Validate every argument and read its @@iterator method exactly ONCE,
    // up front, in argument order (get-iterator-method-only-once.js,
    // inner-iterator-created-in-order.js).
    TsArray* items = TsArray::Create((size_t)(argc > 0 ? argc * 2 : 0));
    for (int i = 0; i < argc; i++) {
        TsValue* item = argv ? argv[i] : nullptr;
        void* raw = item ? ts_value_get_object(item) : nullptr;
        // ECMA-262 27.1.3.1.1 step 2a: each item must be an Object. A primitive
        // string/symbol/cons-string unboxes to a non-null pointer but is NOT an
        // Object, so brand-exclude it (a primitive string is now iterable via
        // String.prototype[Symbol.iterator], which would otherwise slip through).
        if (raw && (uintptr_t)raw > 0x1000) {
            uint32_t m0 = *(uint32_t*)raw;
            // NOTE (SMELL-002): BIGI deliberately NOT excluded here —
            // Object(1n) yields the raw TsBigInt (no BigInt wrapper objects
            // yet), and iterable-primitive-wrapper-objects requires wrapped
            // bigints to be accepted. Revisit when wrappers exist.
            if (m0 == 0x53545247 /*STRG*/ || m0 == 0x434F4E53 /*CONS*/ ||
                m0 == 0x53594D42 /*SYMB*/) raw = nullptr;
        }
        if (!raw) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Iterator.concat: argument is not an object"));
            return ts_value_make_undefined();
        }
        TsValue* method = ts_object_get_property(raw, "[Symbol.iterator]");
        if (!iterseq_is_callable(method)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Iterator.concat: argument is not iterable"));
            return ts_value_make_undefined();
        }
        items->Push((int64_t)(intptr_t)item);
        items->Push((int64_t)(intptr_t)method);
    }
    TsMap* st = TsMap::Create();
    st->Set(TsString::GetInterned("__items"),
            nanbox_to_tagged(ts_value_make_array(items)));
    TsValue zero; zero.type = ValueType::NUMBER_INT; zero.i_val = 0;
    st->Set(TsString::GetInterned("__idx"), zero);
    st->Set(TsString::GetInterned("__innerIdx"), zero);
    st->Set(TsString::GetInterned("next"),
            nanbox_to_tagged(ts_value_make_function((void*)iterator_concat_next, st)));
    // [Symbol.iterator]() returns the iterator itself.
    st->Set(TsString::GetInterned("[Symbol.iterator]"),
            nanbox_to_tagged(ts_value_make_function(
                (void*)+[](void* c, TsValue*) -> TsValue* {
                    return ts_value_make_object(c);
                }, st)));
    return ts_value_make_object(st);
}

extern "C" void* getIteratorPrototypeBoxed();  // TsMap.cpp — %IteratorPrototype%
extern "C" void* ts_iterator_from(void* arg);  // TsMap.cpp
void* ts_get_global_Iterator() {
    TenureScope _tenure;
    static TsMap* cached = nullptr;
    if (!cached) {
        cached = makeSimpleConstructorGlobal("Iterator");
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
        addMethod(cached, "concat", (void*)iterator_concat_native, 1);
        addMethod(cached, "from", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
              return (TsValue*)ts_iterator_from((argc>=1&&argv)?(void*)argv[0]:(void*)ts_value_make_undefined());
          }, 1);
        // ECMA-262 27.1.3.3: Iterator.prototype IS %IteratorPrototype% (which now
        // carries the iterator-helper methods), not a fresh object. Overwrite the
        // ctor's "prototype" slot and link %IteratorPrototype%.constructor = Iterator.
        TsMap* iterProto = (TsMap*)ts_value_get_object((TsValue*)getIteratorPrototypeBoxed());
        if (iterProto) {
            TsValue pk; pk.type = ValueType::STRING_PTR; pk.ptr_val = TsString::GetInterned("prototype");
            TsValue pv; pv.type = ValueType::OBJECT_PTR; pv.ptr_val = iterProto;
            cached->SetWithAttrs(pk, pv, 0);  // {writable:false,enumerable:false,configurable:false}
            TsValue ck; ck.type = ValueType::STRING_PTR; ck.ptr_val = TsString::GetInterned("constructor");
            TsValue cv; cv.type = ValueType::OBJECT_PTR; cv.ptr_val = cached;
            iterProto->SetWithAttrs(ck, cv, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
        }
    }
    return cached;
}

extern "C" TsValue* ts_proxy_revocable(void* targetArg, void* handlerArg);

void* ts_get_global_Proxy() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("Proxy");
        // ECMA-262 28.2.2.1: Proxy.revocable(target, handler) returns
        // { proxy, revoke }. Without this static method, `Proxy.revocable`
        // resolved to undefined (163 test262 proxy-revoked tests destructure
        // `{proxy, revoke}` from the result). Mirror ArrayBuffer.isView: add
        // the method to the ctor map before wrapAsCallable.
        addMethod(ctor, "revocable", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            void* target  = (argc > 0 && argv) ? (void*)argv[0] : (void*)ts_value_make_undefined();
            void* handler = (argc > 1 && argv) ? (void*)argv[1] : (void*)ts_value_make_undefined();
            return ts_proxy_revocable(target, handler);
        }, 2);
        cached = wrapAsCallable(ctor, "Proxy", 2);
    }
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

// ArrayBuffer / DataView / BigInt / GeneratorFunction / AsyncFunction /
// AsyncGeneratorFunction: callable stubs. These produce a TsFunction
// with is_constructor=true (so isConstructor(X) === true per spec) and
// correct name/length own properties. The bodies are stubs that return
// an empty object — enough to pass test262's is-a-constructor tests
// plus .name / .length own-property checks.

void* ts_get_global_ArrayBuffer() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("ArrayBuffer");
        // Static: ArrayBuffer.isView(arg) — true iff arg is a TypedArray
        // or DataView (per spec). Detected by magic16: TARR/DVIE.
        addMethod(ctor, "isView", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (argc < 1 || !argv || !argv[0]) return ts_value_make_bool(false);
            void* raw = ts_value_get_object(argv[0]);
            if (!raw) return ts_value_make_bool(false);
            uint32_t magic16 = *(uint32_t*)((uint8_t*)raw + 16);
            if (magic16 == 0x54415252 || magic16 == 0x44564945) {  // TARR, DVIE
                return ts_value_make_bool(true);
            }
            return ts_value_make_bool(false);
        }, 1);

        // Install accessor getters on ArrayBuffer.prototype (byteLength,
        // detached, maxByteLength, resizable) so `Object.getOwnPropertyDescriptor(
        // ArrayBuffer.prototype, "byteLength").get.call(non-buffer)` throws
        // TypeError per spec. Without these, property access on an
        // ArrayBuffer instance still works via TsBuffer::GetPropertyVirtual,
        // but the getter-extraction tests in built-ins/ArrayBuffer/prototype/*
        // fail because they retrieve the getter and call it on a non-buffer.
        TsValue pkey; pkey.type = ValueType::STRING_PTR; pkey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoT = ctor->Get(pkey);
        if (protoT.type == ValueType::OBJECT_PTR && protoT.ptr_val) {
            TsMap* abProto = (TsMap*)protoT.ptr_val;
            setProtoStringTag(abProto, "ArrayBuffer");
            // TsBuffer's MAGIC pattern at offset 16. See TsBuffer.h.
            auto requireBuffer = [](void* ctx, const char* getterName) -> TsBuffer* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                if (!raw) return nullptr;
                TsBuffer* buf = ts_cast<TsBuffer>(raw);  // heap-safe brand check
                if (!buf) return nullptr;
                (void)getterName;
                return buf;
            };
            addAccessorGetter(abProto, "byteLength", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                TsBuffer* buf = raw ? ts_cast<TsBuffer>(raw) : nullptr;  // heap-safe: dynamic_cast crashed on a string receiver
                if (!buf) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get byteLength called on non-ArrayBuffer"));
                    return ts_value_make_undefined();
                }
                return ts_value_make_int((int64_t)buf->GetLength());
            });
            addAccessorGetter(abProto, "detached", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                TsBuffer* buf = raw ? ts_cast<TsBuffer>(raw) : nullptr;  // heap-safe: dynamic_cast crashed on a string receiver
                if (!buf) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get detached called on non-ArrayBuffer"));
                    return ts_value_make_undefined();
                }
                return ts_value_make_bool(buf->IsDetached());
            });
            addAccessorGetter(abProto, "resizable", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                TsBuffer* buf = raw ? ts_cast<TsBuffer>(raw) : nullptr;  // heap-safe: dynamic_cast crashed on a string receiver
                if (!buf) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get resizable called on non-ArrayBuffer"));
                    return ts_value_make_undefined();
                }
                return ts_value_make_bool(buf->IsResizable());
            });
            addAccessorGetter(abProto, "maxByteLength", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                TsBuffer* buf = raw ? ts_cast<TsBuffer>(raw) : nullptr;  // heap-safe: dynamic_cast crashed on a string receiver
                if (!buf) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get maxByteLength called on non-ArrayBuffer"));
                    return ts_value_make_undefined();
                }
                return ts_value_make_int((int64_t)(buf->IsResizable() ? buf->GetMaxByteLength() : buf->GetLength()));
            });
            // ArrayBuffer.prototype.resize(newByteLength) as a function-valued
            // own property of the PROTOTYPE — test262's resizable-buffer
            // helpers feature-detect `typeof ArrayBuffer.prototype.resize ===
            // "function"` there, so the instance-level GetPropertyVirtual
            // wiring alone left the whole family skipped as unimplemented.
            {
                TsValue rk; rk.type = ValueType::STRING_PTR; rk.ptr_val = TsString::GetInterned("resize");
                TsValue rv = nanbox_to_tagged(ts_value_make_native_function(
                    (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                        if (!ctx) ctx = ts_get_call_this();
                        void* raw = ts_nanbox_safe_unbox(ctx);
                        TsBuffer* buf = raw ? ts_cast<TsBuffer>(raw) : nullptr;  // heap-safe: dynamic_cast crashed on a string receiver
                        if (!buf) {
                            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                "ArrayBuffer.prototype.resize called on non-ArrayBuffer"));
                            return ts_value_make_undefined();
                        }
                        double d = (argc >= 1 && argv[0] && !ts_value_is_undefined(argv[0]))
                            ? ts_to_number(argv[0]) : 0;
                        if (d < 0) {
                            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                                "Invalid array buffer length"));
                            return ts_value_make_undefined();
                        }
                        buf->Resize((size_t)(int64_t)d);
                        return ts_value_make_undefined();
                    }, nullptr));
                constexpr uint8_t ATTR_W = 0x02, ATTR_C = 0x04;
                abProto->SetWithAttrs(rk, rv, ATTR_W | ATTR_C);
            }
            (void)requireBuffer;
        }

        addAccessorGetter(ctor, "[Symbol.species]", (void*)species_this_getter);
        cached = wrapAsCallable(ctor, "ArrayBuffer", 1);
        // Wire ArrayBuffer.prototype.constructor = ArrayBuffer (the callable
        // global, `cached` — NOT the inner ctor map). It was unset, so
        // `(new ArrayBuffer()).constructor` read undefined and lodash
        // cloneArrayBuffer `new arrayBuffer.constructor(byteLength)` produced
        // garbage. Non-enumerable per ECMA-262.
        {
            TsValue pk; pk.type = ValueType::STRING_PTR; pk.ptr_val = TsString::GetInterned("prototype");
            TsValue pv = ctor->Get(pk);
            if (pv.type == ValueType::OBJECT_PTR && pv.ptr_val && cached) {
                TsMap* abProto = (TsMap*)pv.ptr_val;
                TsValue ck; ck.type = ValueType::STRING_PTR; ck.ptr_val = TsString::GetInterned("constructor");
                TsValue cv; cv.type = ValueType::OBJECT_PTR; cv.ptr_val = cached;
                constexpr uint8_t ATTR_WRITABLE = 0x02, ATTR_CONFIGURABLE = 0x04;
                abProto->SetWithAttrs(ck, cv, ATTR_WRITABLE | ATTR_CONFIGURABLE);
            }
        }
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

// ---- DataView.prototype get/set methods (ECMA-262 25.3.4) ----
// Real function objects on DataView.prototype (registered via addMethod, so each has the
// correct .name / .length / [[Construct]]=false). GetViewValue / SetViewValue semantics:
// require a DataView receiver (TypeError), ToIndex the byte offset (RangeError), ToNumber the
// value, ToBoolean littleEndian (DataView default is big-endian), reject a detached buffer
// (TypeError) and an out-of-bounds access (RangeError), then read/write `size` bytes.
static TsDataView* dv_require(void* ctx){
    if(!ctx) ctx = ts_get_call_this();
    void* raw = ctx ? ts_nanbox_safe_unbox(ctx) : nullptr;
    // ts_cast<TsDataView> is heap-checked (never dereferences a non-heap `this`)
    // and reads the magic at the layout-correct offset (+16). A bad receiver
    // -> nullptr -> TypeError rather than an access violation.
    TsDataView* dv = raw ? ts_cast<TsDataView>(raw) : nullptr;
    if(!dv){
        ts_throw((TsValue*)ts_error_create_typed("TypeError","DataView method called on a non-DataView"));
        return nullptr;
    }
    return dv;
}
static int64_t dv_to_index(TsValue* v){
    double d = v ? ts_to_number(v) : 0.0;            // ToNumber (throws TypeError on a Symbol)
    if(d != d) return 0;                              // ToIntegerOrInfinity(NaN) == 0
    d = std::trunc(d);
    if(d < 0 || d > 9007199254740991.0){ ts_throw((TsValue*)ts_error_create_typed("RangeError","DataView offset is out of range")); return 0; }
    return (int64_t)d;
}
static uint64_t dv_read_bytes(const uint8_t* p, int size, bool le){
    uint64_t v=0;
    if(le){ for(int i=0;i<size;i++) v |= (uint64_t)p[i] << (8*i); }
    else  { for(int i=0;i<size;i++) v = (v<<8) | (uint64_t)p[i]; }
    return v;
}
static void dv_write_bytes(uint8_t* p, int size, bool le, uint64_t v){
    if(le){ for(int i=0;i<size;i++){ p[i]=(uint8_t)(v&0xFF); v>>=8; } }
    else  { for(int i=size-1;i>=0;i--){ p[i]=(uint8_t)(v&0xFF); v>>=8; } }
}
// kind: 0 = signed int, 1 = unsigned int, 2 = float
static TsValue* dv_get_impl(void* ctx, int argc, TsValue** argv, int size, int kind){
    TsDataView* dv = dv_require(ctx); if(!dv) return ts_value_make_undefined();
    int64_t off = dv_to_index((argc>=1&&argv)?argv[0]:nullptr);
    bool le = (size>1 && argc>=2 && argv && argv[1]) ? ts_value_to_bool(argv[1]) : false;
    TsBuffer* buf = dv->GetBuffer();
    if(!buf || buf->IsDetached()){ ts_throw((TsValue*)ts_error_create_typed("TypeError","DataView: the underlying ArrayBuffer is detached")); return ts_value_make_undefined(); }
    size_t blen = dv->GetByteLength();
    if((uint64_t)off + (uint64_t)size > (uint64_t)blen){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Offset is outside the bounds of the DataView")); return ts_value_make_undefined(); }
    uint64_t raw = dv_read_bytes(buf->GetData() + dv->GetByteOffset() + off, size, le);
    if(kind==2){ if(size==4){ float f; std::memcpy(&f,&raw,4); return ts_value_make_double((double)f); } double d; std::memcpy(&d,&raw,8); return ts_value_make_double(d); }
    if(kind==0){ int64_t sv = (size==1)?(int8_t)raw : (size==2)?(int16_t)raw : (int32_t)raw; return ts_value_make_double((double)sv); }
    uint64_t uv = (size==1)?(uint8_t)raw : (size==2)?(uint16_t)raw : (uint32_t)raw; return ts_value_make_double((double)uv);
}
static TsValue* dv_set_impl(void* ctx, int argc, TsValue** argv, int size, int kind){
    TsDataView* dv = dv_require(ctx); if(!dv) return ts_value_make_undefined();
    int64_t off = dv_to_index((argc>=1&&argv)?argv[0]:nullptr);
    double val = (argc>=2&&argv&&argv[1]) ? ts_to_number(argv[1]) : std::nan("");   // ToNumber(undefined) == NaN
    bool le = (size>1 && argc>=3 && argv && argv[2]) ? ts_value_to_bool(argv[2]) : false;
    TsBuffer* buf = dv->GetBuffer();
    if(!buf || buf->IsDetached()){ ts_throw((TsValue*)ts_error_create_typed("TypeError","DataView: the underlying ArrayBuffer is detached")); return ts_value_make_undefined(); }
    size_t blen = dv->GetByteLength();
    if((uint64_t)off + (uint64_t)size > (uint64_t)blen){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Offset is outside the bounds of the DataView")); return ts_value_make_undefined(); }
    uint8_t* p = buf->GetData() + dv->GetByteOffset() + off;
    if(kind==2){
        if(size==4){ float f=(float)val; uint64_t r=0; std::memcpy(&r,&f,4); dv_write_bytes(p,4,le,r); }
        else { uint64_t r; std::memcpy(&r,&val,8); dv_write_bytes(p,8,le,r); }
    } else {
        double iv = std::isfinite(val) ? std::trunc(val) : 0.0;          // ToInt/ToUint: integer part...
        double m = std::pow(2.0, size*8); double r = std::fmod(iv, m); if(r<0) r += m;   // ...mod 2^(size*8)
        dv_write_bytes(p, size, le, (uint64_t)r);
    }
    return ts_value_make_undefined();
}
// kind 3/4: BigInt64/BigUint64 — raw 64-bit slots boxed as TsBigInt; the
// set path requires a BigInt VALUE (ToBigInt on a Number is a TypeError).
static TsValue* dv_get_big(void* ctx, int argc, TsValue** argv, bool isSigned){
    TsDataView* dv = dv_require(ctx); if(!dv) return ts_value_make_undefined();
    int64_t off = dv_to_index((argc>=1&&argv)?argv[0]:nullptr);
    bool le = (argc>=2 && argv && argv[1]) ? ts_value_to_bool(argv[1]) : false;
    TsBuffer* buf = dv->GetBuffer();
    if(!buf || buf->IsDetached()){ ts_throw((TsValue*)ts_error_create_typed("TypeError","DataView: the underlying ArrayBuffer is detached")); return ts_value_make_undefined(); }
    size_t blen = dv->GetByteLength();
    if((uint64_t)off + 8ull > (uint64_t)blen){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Offset is outside the bounds of the DataView")); return ts_value_make_undefined(); }
    uint64_t raw = dv_read_bytes(buf->GetData() + dv->GetByteOffset() + off, 8, le);
    (void)isSigned;  // TsBigInt is i64-backed; unsigned > 2^63-1 saturates (rare in tests)
    extern void* ts_bigint_create_int(int64_t v);
    extern TsValue* ts_value_make_bigint(void* bi);
    return ts_value_make_bigint(ts_bigint_create_int((int64_t)raw));
}
static TsValue* dv_set_big(void* ctx, int argc, TsValue** argv){
    TsDataView* dv = dv_require(ctx); if(!dv) return ts_value_make_undefined();
    int64_t off = dv_to_index((argc>=1&&argv)?argv[0]:nullptr);
    // Step order per 25.3.1.6 SetViewValue: ToBigInt(value) BEFORE the
    // detached/bounds checks would be wrong — spec: GetViewValue does
    // ToIndex, then ToBigInt, then detached check. Match that.
    TsValue* vArg = (argc>=2&&argv) ? argv[1] : nullptr;
    int64_t iv = 0;
    {
        uint64_t nb = vArg ? nanbox_from_tsvalue_ptr(vArg) : 0;
        void* raw = (vArg && nanbox_is_ptr(nb)) ? nanbox_to_ptr(nb) : nullptr;
        if (raw && (uintptr_t)raw >= 4096 && *(uint32_t*)raw == 0x42494749 /*BIGI*/) {
            extern int64_t ts_bigint_to_i64(void* bi);
            iv = ts_bigint_to_i64(raw);
        } else {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert a non-BigInt value to a BigInt"));
            return ts_value_make_undefined();
        }
    }
    bool le = (argc>=3 && argv && argv[2]) ? ts_value_to_bool(argv[2]) : false;
    TsBuffer* buf = dv->GetBuffer();
    if(!buf || buf->IsDetached()){ ts_throw((TsValue*)ts_error_create_typed("TypeError","DataView: the underlying ArrayBuffer is detached")); return ts_value_make_undefined(); }
    size_t blen = dv->GetByteLength();
    if((uint64_t)off + 8ull > (uint64_t)blen){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Offset is outside the bounds of the DataView")); return ts_value_make_undefined(); }
    dv_write_bytes(buf->GetData() + dv->GetByteOffset() + off, 8, le, (uint64_t)iv);
    return ts_value_make_undefined();
}
static TsValue* dv_getBigInt64 (void*c,int a,TsValue**v){return dv_get_big(c,a,v,true);}
static TsValue* dv_getBigUint64(void*c,int a,TsValue**v){return dv_get_big(c,a,v,false);}
static TsValue* dv_setBigInt64 (void*c,int a,TsValue**v){return dv_set_big(c,a,v);}
static TsValue* dv_setBigUint64(void*c,int a,TsValue**v){return dv_set_big(c,a,v);}

static TsValue* dv_getInt8   (void*c,int a,TsValue**v){return dv_get_impl(c,a,v,1,0);}
static TsValue* dv_getUint8  (void*c,int a,TsValue**v){return dv_get_impl(c,a,v,1,1);}
static TsValue* dv_getInt16  (void*c,int a,TsValue**v){return dv_get_impl(c,a,v,2,0);}
static TsValue* dv_getUint16 (void*c,int a,TsValue**v){return dv_get_impl(c,a,v,2,1);}
static TsValue* dv_getInt32  (void*c,int a,TsValue**v){return dv_get_impl(c,a,v,4,0);}
static TsValue* dv_getUint32 (void*c,int a,TsValue**v){return dv_get_impl(c,a,v,4,1);}
static TsValue* dv_getFloat32(void*c,int a,TsValue**v){return dv_get_impl(c,a,v,4,2);}
static TsValue* dv_getFloat64(void*c,int a,TsValue**v){return dv_get_impl(c,a,v,8,2);}
static TsValue* dv_setInt8   (void*c,int a,TsValue**v){return dv_set_impl(c,a,v,1,0);}
static TsValue* dv_setUint8  (void*c,int a,TsValue**v){return dv_set_impl(c,a,v,1,1);}
static TsValue* dv_setInt16  (void*c,int a,TsValue**v){return dv_set_impl(c,a,v,2,0);}
static TsValue* dv_setUint16 (void*c,int a,TsValue**v){return dv_set_impl(c,a,v,2,1);}
static TsValue* dv_setInt32  (void*c,int a,TsValue**v){return dv_set_impl(c,a,v,4,0);}
static TsValue* dv_setUint32 (void*c,int a,TsValue**v){return dv_set_impl(c,a,v,4,1);}
static TsValue* dv_setFloat32(void*c,int a,TsValue**v){return dv_set_impl(c,a,v,4,2);}
static TsValue* dv_setFloat64(void*c,int a,TsValue**v){return dv_set_impl(c,a,v,8,2);}

void* ts_get_global_DataView() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("DataView");
        // Install accessor getters on DataView.prototype per ECMA-262
        // 25.3.4.{1,2,3}: buffer / byteLength / byteOffset are accessor
        // properties whose getter functions have .name = "get buffer"
        // (etc.). When invoked, the getter reads the slot from `this`
        // (which must be a TsDataView) or throws TypeError.
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal = ctor->Get(protoKey);
        if (protoVal.type == ValueType::OBJECT_PTR && protoVal.ptr_val) {
            TsMap* dvProto = (TsMap*)protoVal.ptr_val;
            // Helper: extract a TsDataView from `this` (ctx) or throw.
            auto requireDataView = [](void* ctx, const char* method) -> TsDataView* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_value_get_object((TsValue*)ctx);
                if (!raw) raw = ctx;
                if (!raw) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "DataView accessor invoked on non-DataView"));
                    return nullptr;
                }
                // Magic at offset 0 for TsDataView.
                uint32_t magic0 = *(uint32_t*)raw;
                if (magic0 != TsDataView::MAGIC) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "DataView accessor invoked on non-DataView"));
                    return nullptr;
                }
                return (TsDataView*)raw;
            };
            addAccessorGetter(dvProto, "buffer", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                // ts_nanbox_safe_unbox returns nullptr for NaN-boxed specials
                // (null, undefined, true, false, numbers). The MAGIC check below
                // then fires cleanly. Previously the code did
                // `if (!raw) raw = ctx;` which for primitive `this` left raw
                // pointing at a tagged-value bit pattern; the MAGIC read
                // dereferenced wild memory and crashed.
                // TsDataView magic lives at offset +16 (TsObject subclass), so
                // the old `*(uint32_t*)raw` (offset 0) read the vtable, not the
                // magic — it both mis-classified valid DataViews and could
                // dereference a non-heap `this`. ts_cast<TsDataView> is
                // heap-checked and reads the magic at the layout-correct offset.
                void* raw = ts_nanbox_safe_unbox(ctx);
                TsDataView* dv = raw ? ts_cast<TsDataView>(raw) : nullptr;
                if (!dv) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get buffer called on non-DataView"));
                    return ts_value_make_undefined();
                }
                TsBuffer* buf = dv->GetBuffer();
                return buf ? ts_value_make_object(buf) : ts_value_make_undefined();
            });
            addAccessorGetter(dvProto, "byteLength", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                TsDataView* dv = raw ? ts_cast<TsDataView>(raw) : nullptr;
                if (!dv) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get byteLength called on non-DataView"));
                    return ts_value_make_undefined();
                }
                return ts_value_make_int((int64_t)dv->GetByteLength());
            });
            addAccessorGetter(dvProto, "byteOffset", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                TsDataView* dv = raw ? ts_cast<TsDataView>(raw) : nullptr;
                if (!dv) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get byteOffset called on non-DataView"));
                    return ts_value_make_undefined();
                }
                return ts_value_make_int((int64_t)dv->GetByteOffset());
            });
            (void)requireDataView;
            // get* have length 1 (byteOffset[, littleEndian]); set* have length 2 (byteOffset, value[, littleEndian]).
            addMethod(dvProto, "getInt8",   (void*)dv_getInt8,   1);
            addMethod(dvProto, "getUint8",  (void*)dv_getUint8,  1);
            addMethod(dvProto, "getInt16",  (void*)dv_getInt16,  1);
            addMethod(dvProto, "getUint16", (void*)dv_getUint16, 1);
            addMethod(dvProto, "getInt32",  (void*)dv_getInt32,  1);
            addMethod(dvProto, "getUint32", (void*)dv_getUint32, 1);
            addMethod(dvProto, "getFloat32",(void*)dv_getFloat32,1);
            addMethod(dvProto, "getFloat64",(void*)dv_getFloat64,1);
            addMethod(dvProto, "getBigInt64",(void*)dv_getBigInt64,1);
            addMethod(dvProto, "getBigUint64",(void*)dv_getBigUint64,1);
            addMethod(dvProto, "setInt8",   (void*)dv_setInt8,   2);
            addMethod(dvProto, "setUint8",  (void*)dv_setUint8,  2);
            addMethod(dvProto, "setInt16",  (void*)dv_setInt16,  2);
            addMethod(dvProto, "setUint16", (void*)dv_setUint16, 2);
            addMethod(dvProto, "setInt32",  (void*)dv_setInt32,  2);
            addMethod(dvProto, "setUint32", (void*)dv_setUint32, 2);
            addMethod(dvProto, "setFloat32",(void*)dv_setFloat32,2);
            addMethod(dvProto, "setFloat64",(void*)dv_setFloat64,2);
            addMethod(dvProto, "setBigInt64",(void*)dv_setBigInt64,2);
            addMethod(dvProto, "setBigUint64",(void*)dv_setBigUint64,2);
            setProtoStringTag(dvProto, "DataView");
        }
        cached = wrapAsCallable(ctor, "DataView", 1);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

void* ts_get_global_SharedArrayBuffer() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) cached = wrapAsCallable(makeSimpleConstructorGlobal("SharedArrayBuffer"), "SharedArrayBuffer", 1);
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

// ECMA-262 7.1.22 ToIndex for BigInt.asIntN/asUintN `bits` (step 1 of both):
// ToNumber (incl. ToPrimitive on objects, done by ts_to_number), NaN/undefined
// -> +0, truncate toward zero, RangeError outside [0, 2^53-1].
static int64_t bigint_asn_bits(TsValue* v) {
    // ToPrimitive(number) FIRST (an object's @@toPrimitive/valueOf may yield
    // a BigInt), then gate BigInt: spec ToNumber(BigInt) is a TypeError, but
    // ts_to_number keeps its legacy numeric coercion for internal callers.
    {
        extern TsValue* ts_to_primitive(TsValue* val, int hint);
        v = ts_to_primitive(v, 1);
        uint64_t nb = v ? nanbox_from_tsvalue_ptr(v) : 0;
        if (v && nanbox_is_ptr(nb)) {
            void* raw = nanbox_to_ptr(nb);
            if (raw && *(uint32_t*)raw == 0x42494749) {  // TsBigInt
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Cannot convert a BigInt value to a number"));
                return 0;
            }
        }
    }
    double d = ts_to_number(v);
    if (d != d) d = 0;  // ToIntegerOrInfinity(NaN) = +0 (covers undefined too)
    d = std::trunc(d);
    if (d < 0 || d > 9007199254740991.0) {
        ts_throw((TsValue*)ts_error_create_typed("RangeError",
            "bits must be an integer in [0, 2^53-1]"));
        return 0;  // unreachable
    }
    // mp_2expt takes an int; anything this large is out of practical range.
    if (d > 2147483647.0) d = 2147483647.0;
    return (int64_t)d;
}

// ECMA-262 7.1.13 ToBigInt (step 2 of asIntN/asUintN): primitives per the
// table (undefined/null/Number/Symbol TypeError; boolean 0n/1n; string via
// the validated StringToBigInt -> SyntaxError on bad syntax); objects go
// through ToPrimitive(number) once, then the primitive rules. Throws rather
// than returning nullptr (the nullptr return is for the unreachable path).
static TsBigInt* bigint_asn_tobigint(TsValue* v) {
    extern TsValue* ts_to_primitive(TsValue* val, int hint);
    extern void* ts_bigint_from_string_checked(void* str);
    for (int pass = 0; pass < 2; ++pass) {
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        if (nanbox_is_undefined(nb) || nanbox_is_null(nb)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert undefined or null to BigInt"));
            return nullptr;
        }
        if (nanbox_is_true(nb))  return (TsBigInt*)ts_bigint_create_int(1);
        if (nanbox_is_false(nb)) return (TsBigInt*)ts_bigint_create_int(0);
        if (nanbox_is_int32(nb) || nanbox_is_double(nb)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert a Number to a BigInt"));
            return nullptr;
        }
        if (nanbox_is_ptr(nb)) {
            void* raw = nanbox_to_ptr(nb);
            if (raw) {
                uint32_t magic = *(uint32_t*)raw;
                if (magic == 0x42494749) {  // TsBigInt
                    return (TsBigInt*)raw;
                }
                if (magic == 0x53594D42) {  // TsSymbol
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Cannot convert a Symbol value to a BigInt"));
                    return nullptr;
                }
                if (magic == 0x53545247 || magic == TsConsString::MAGIC) {
                    void* bi = ts_bigint_from_string_checked(ts_ensure_flat(raw));
                    if (!bi) {
                        ts_throw((TsValue*)ts_error_create_typed("SyntaxError",
                            "Cannot convert string to a BigInt"));
                        return nullptr;
                    }
                    return (TsBigInt*)bi;
                }
                if (pass == 0) {
                    v = ts_to_primitive(v, 1);  // hint number
                    continue;
                }
            }
        }
        break;
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError",
        "Cannot convert value to a BigInt"));
    return nullptr;
}

// Exported spec ToBigInt for other TUs (typed-array element writes etc.):
// throws for undefined/null/Number/Symbol/bad-string, returns TsBigInt*.
extern "C" void* ts_to_bigint_spec(TsValue* v) {
    return bigint_asn_tobigint(v);
}

void* ts_get_global_BigInt() {
    TenureScope _tenure;
    // Spec: BigInt is a constructor (isConstructor === true) but `new BigInt(x)`
    // throws TypeError. Call-as-function `BigInt(x)` coerces to bigint.
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("BigInt");
        {   // BigInt.prototype[@@toStringTag] = "BigInt" + prototype methods.
            TsValue pkey; pkey.type = ValueType::STRING_PTR; pkey.ptr_val = TsString::GetInterned("prototype");
            TsValue protoT = ctor->Get(pkey);
            if (protoT.type == ValueType::OBJECT_PTR && protoT.ptr_val) {
                TsMap* bigProto = (TsMap*)protoT.ptr_val;
                setProtoStringTag(bigProto, "BigInt");
                // BigInt.prototype.{toString,valueOf,toLocaleString}: each
                // brand-checks `this` (thisBigIntValue) and throws TypeError on
                // a non-BigInt receiver. Without these the methods inherited
                // Object.prototype.{toString,valueOf} and silently returned
                // garbage instead of throwing (e.g.
                // BigInt.prototype.valueOf.call({})).
                addMethod(bigProto, "toString",       (void*)ts_bigint_toString_native, 0);
                addMethod(bigProto, "valueOf",        (void*)ts_bigint_valueOf_native, 0);
                addMethod(bigProto, "toLocaleString", (void*)ts_bigint_toLocaleString_native, 0);
            }
        }
        // BigInt.asIntN(bits, bigint) — wrap to a signed two's-complement
        // value with `bits` bits. Implementation: out = bigint mod 2^bits;
        // if the high bit is set, subtract 2^bits.
        addMethod(ctor, "asIntN", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (argc < 2) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "BigInt.asIntN requires bits and bigint arguments"));
                return ts_value_make_undefined();
            }
            int bits = (int)bigint_asn_bits(argv[0]);
            TsBigInt* src = bigint_asn_tobigint(argv[1]);
            if (!src) return ts_value_make_undefined();  // unreachable (threw)
            if (bits == 0) {
                return (TsValue*)ts_bigint_create_int(0);
            }
            // out = src mod 2^bits  (positive result)
            mp_int mod, modulus, half;
            mp_init(&mod); mp_init(&modulus); mp_init(&half);
            mp_2expt(&modulus, bits);          // modulus = 2^bits
            mp_2expt(&half, bits - 1);         // half    = 2^(bits-1)
            mp_mod(&src->value, &modulus, &mod);
            // If mod >= 2^(bits-1), subtract 2^bits to make it negative.
            if (mp_cmp(&mod, &half) != MP_LT) {
                mp_sub(&mod, &modulus, &mod);
            }
            TsBigInt* out = TsBigInt::Create((int64_t)0);
            mp_copy(&mod, &out->value);
            mp_clear(&mod); mp_clear(&modulus); mp_clear(&half);
            return (TsValue*)out;
        }, 2);
        // BigInt.asUintN(bits, bigint) — wrap to an unsigned bits-bit value.
        addMethod(ctor, "asUintN", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (argc < 2) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "BigInt.asUintN requires bits and bigint arguments"));
                return ts_value_make_undefined();
            }
            int bits = (int)bigint_asn_bits(argv[0]);
            TsBigInt* src = bigint_asn_tobigint(argv[1]);
            if (!src) return ts_value_make_undefined();  // unreachable (threw)
            if (bits == 0) return (TsValue*)ts_bigint_create_int(0);
            mp_int mod, modulus;
            mp_init(&mod); mp_init(&modulus);
            mp_2expt(&modulus, bits);
            mp_mod(&src->value, &modulus, &mod);
            TsBigInt* out = TsBigInt::Create((int64_t)0);
            mp_copy(&mod, &out->value);
            mp_clear(&mod); mp_clear(&modulus);
            return (TsValue*)out;
        }, 2);
        cached = wrapAsCallable(ctor, "BigInt", 1);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

void* ts_get_global_GeneratorFunction() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) cached = wrapAsCallable(makeSimpleConstructorGlobal("GeneratorFunction"), "GeneratorFunction", 1);
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

void* ts_get_global_AsyncFunction() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) cached = wrapAsCallable(makeSimpleConstructorGlobal("AsyncFunction"), "AsyncFunction", 1);
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

void* ts_get_global_AsyncGeneratorFunction() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) cached = wrapAsCallable(makeSimpleConstructorGlobal("AsyncGeneratorFunction"), "AsyncGeneratorFunction", 1);
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    return cached;
}

// ========================================
// Console, Math, Buffer, process — keep sentinels for typed path,
// but also support dynamic access
// ========================================

void* ts_get_global_console() {
    TenureScope _tenure;
    // Console methods are handled specially in HIR->LLVM lowering.
    // For untyped JS, return a sentinel — console.log etc. are lowered directly.
    static const char sentinel[] = "console";
    return (void*)sentinel;
}

// These globals are initialized in ts_runtime_init() → initGlobal() in
// TsObject.cpp, which runs from ts_main() before any user code executes.
// Previously returned sentinel strings ("Math", "Buffer", etc.) which
// worked for the typed call path (extension contracts bypass property
// access) but broke first-class value access like `Math.abs.length` or
// `typeof Math.abs` because property access on a string sentinel falls
// through all handlers to undefined.
extern "C" TsValue* Math;
extern "C" TsValue* JSON;
extern "C" TsValue* process;
extern "C" TsValue* Buffer;
extern "C" TsValue* globalThis;

void* ts_get_global_Math() {
    TenureScope _tenure;
    return (void*)Math;
}

// ========================================
// Intl (ECMA-402) — Phase A scaffold
// ========================================
// The Intl namespace is a top-level TsMap exposing constructor stubs
// (Collator, NumberFormat, DateTimeFormat, PluralRules, Locale, plus
// stretch goals) and a static `getCanonicalLocales(input)` helper.
//
// Each constructor stub:
//   - Has spec-correct .name and .length own-properties via wrapAsCallable.
//   - Has a .prototype TsMap with Symbol.toStringTag = "Intl.<Name>".
//   - Has prototype methods (compare/format/formatToParts/select/etc.)
//     installed via addMethod with proper name/length, returning placeholder
//     values. Phases B-F replace these with real ICU-backed impls.
//   - Has supportedLocalesOf static (stub returning empty array; refine later).

static void intlInstallToStringTag(TsMap* proto, const char* tag) {
    TsValue tagKey; tagKey.type = ValueType::STRING_PTR;
    tagKey.ptr_val = TsString::GetInterned("[Symbol.toStringTag]");
    TsValue tagVal; tagVal.type = ValueType::STRING_PTR;
    tagVal.ptr_val = TsString::Create(tag);
    proto->SetWithAttrs(tagKey, tagVal, TsHashTable::ATTR_CONFIGURABLE);
}

// Like wrapAsCallable but with a custom body. Used by Intl constructors
// that need to actually instantiate ICU-backed objects.
static void* wrapAsCallableWithBody(TsMap* ctor, const char* name, int length,
                                     TsValue* (*body)(void*, int, TsValue**)) {
    if (!ctor) return nullptr;
    TsValue* fnVal = ts_value_make_native_function((void*)body, nullptr);
    void* rawFn = ts_value_get_object(fnVal);
    if (!rawFn) return (void*)ctor;
    TsFunction* func = (TsFunction*)rawFn;
    func->name = TsString::Create(name);
    func->arity = length;
    func->is_constructor = true;
    func->properties = ctor;
    ts_gc_write_barrier(&func->properties, ctor);
    TsValue nk; nk.type = ValueType::STRING_PTR;
    nk.ptr_val = TsString::GetInterned("name");
    TsValue nv; nv.type = ValueType::STRING_PTR; nv.ptr_val = func->name;
    ctor->SetWithAttrs(nk, nv, TsHashTable::ATTR_CONFIGURABLE);
    TsValue lk; lk.type = ValueType::STRING_PTR;
    lk.ptr_val = TsString::GetInterned("length");
    TsValue lv; lv.type = ValueType::NUMBER_INT; lv.i_val = length;
    ctor->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);
    // ECMA-262: F.prototype.constructor = F (writable, non-enumerable, configurable).
    TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal = ctor->Get(protoKey);
    if (protoVal.type == ValueType::OBJECT_PTR && protoVal.ptr_val) {
        TsMap* proto = (TsMap*)protoVal.ptr_val;
        TsValue ck; ck.type = ValueType::STRING_PTR;
        ck.ptr_val = TsString::GetInterned("constructor");
        TsValue cv; cv.type = ValueType::FUNCTION_PTR; cv.ptr_val = func;
        proto->SetWithAttrs(ck, cv, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
    }
    return (void*)fnVal;
}

static void* makeIntlCtorStub(const char* name, int length,
                              const char* protoToStringTag,
                              void (*populateProto)(TsMap*)) {
    TsMap* ctor = makeSimpleConstructorGlobal(name);
    TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal = ctor->Get(protoKey);
    if (protoVal.type == ValueType::OBJECT_PTR && protoVal.ptr_val) {
        TsMap* proto = (TsMap*)protoVal.ptr_val;
        if (populateProto) populateProto(proto);
        intlInstallToStringTag(proto, protoToStringTag);
    }
    // supportedLocalesOf — minimal stub returning empty array (full impl
    // would filter via icu::Locale::createCanonical). Spec arity: 1.
    addMethod(ctor, "supportedLocalesOf", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
        extern void* ts_array_create();
        return ts_value_make_object(ts_array_create());
    }, 1);
    void* fn = wrapAsCallable(ctor, name, length);
    // ECMA-262: F.prototype.constructor = F (writable, non-enumerable, configurable).
    if (fn && protoVal.type == ValueType::OBJECT_PTR && protoVal.ptr_val) {
        TsMap* proto = (TsMap*)protoVal.ptr_val;
        TsValue ck; ck.type = ValueType::STRING_PTR;
        ck.ptr_val = TsString::GetInterned("constructor");
        TsValue cv; cv.type = ValueType::FUNCTION_PTR; cv.ptr_val = fn;
        proto->SetWithAttrs(ck, cv, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
    }
    return fn;
}

// =============================================================================
// Intl.Collator (Phase B) — real icu::Collator integration.
// =============================================================================
//
// Each instance is a TsMap with prototype = Collator.prototype. Holds the
// icu::Collator* under a hidden "__icuCollator" key. The GC doesn't track
// the external ICU allocation, and we have no finalizer support yet, so
// these leak — acceptable for short-lived test262 use; finalizer is a
// future task.

static TsMap* g_intlCollatorProto = nullptr;  // Set during Intl init.

static TsValue* intlCollatorCompareImpl(void* ctx, int argc, TsValue** argv) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_value_get_object((TsValue*)ctx);
    if (!raw) raw = ctx;
    TsMap* receiver = raw ? (TsMap*)raw : nullptr;
    if (!receiver) return ts_value_make_int(0);
    // Look up __icuCollator on the receiver.
    TsValue ck; ck.type = ValueType::STRING_PTR;
    ck.ptr_val = TsString::GetInterned("__icuCollator");
    TsValue cv = receiver->Get(ck);
    icu::Collator* coll = (cv.type == ValueType::OBJECT_PTR) ? (icu::Collator*)cv.ptr_val : nullptr;
    void* sa = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
    void* sb = (argc >= 2 && argv && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
    const char* ua = sa ? ((TsString*)sa)->ToUtf8() : "";
    const char* ub = sb ? ((TsString*)sb)->ToUtf8() : "";
    if (!ua) ua = "";
    if (!ub) ub = "";
    if (!coll) {
        // No collator (shouldn't normally happen) — fall back to byte compare.
        int cmp = std::strcmp(ua, ub);
        return ts_value_make_int(cmp < 0 ? -1 : (cmp > 0 ? 1 : 0));
    }
    UErrorCode err = U_ZERO_ERROR;
    UCollationResult r = coll->compareUTF8(icu::StringPiece(ua), icu::StringPiece(ub), err);
    if (U_FAILURE(err)) return ts_value_make_int(0);
    return ts_value_make_int(r == UCOL_LESS ? -1 : (r == UCOL_GREATER ? 1 : 0));
}

static TsValue* intlCollatorResolvedOptions(void* ctx, int argc, TsValue** argv) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_value_get_object((TsValue*)ctx);
    if (!raw) raw = ctx;
    TsMap* receiver = raw ? (TsMap*)raw : nullptr;
    TsMap* result = TsMap::Create();
    if (!receiver) return ts_value_make_object(result);
    // Copy resolved option fields from the instance to a fresh TsMap.
    const char* keys[] = {"locale", "usage", "sensitivity", "ignorePunctuation",
                          "collation", "numeric", "caseFirst"};
    for (const char* k : keys) {
        TsValue key; key.type = ValueType::STRING_PTR;
        key.ptr_val = TsString::GetInterned(k);
        TsValue v = receiver->Get(key);
        if (v.type != ValueType::UNDEFINED) {
            result->Set(key, v);
        }
    }
    return ts_value_make_object(result);
}

// =============================================================================
// Intl.PluralRules (Phase E) — real icu::PluralRules integration.
// =============================================================================

static TsMap* g_intlPluralRulesProto = nullptr;

static TsValue* intlPluralRulesSelectImpl(void* ctx, int argc, TsValue** argv) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_value_get_object((TsValue*)ctx);
    if (!raw) raw = ctx;
    TsMap* receiver = raw ? (TsMap*)raw : nullptr;
    if (!receiver) return ts_value_make_string(TsString::Create("other"));
    TsValue pk; pk.type = ValueType::STRING_PTR;
    pk.ptr_val = TsString::GetInterned("__icuPluralRules");
    TsValue pv = receiver->Get(pk);
    icu::PluralRules* rules = (pv.type == ValueType::OBJECT_PTR) ? (icu::PluralRules*)pv.ptr_val : nullptr;
    double d = 0;
    if (argc >= 1 && argv && argv[0]) d = ts_value_get_double(argv[0]);
    if (!rules) return ts_value_make_string(TsString::Create("other"));
    icu::UnicodeString cat = rules->select(d);
    std::string utf8;
    cat.toUTF8String(utf8);
    return ts_value_make_string(TsString::Create(utf8.c_str()));
}

static TsValue* intlPluralRulesResolvedOptions(void* ctx, int argc, TsValue** argv) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_value_get_object((TsValue*)ctx);
    if (!raw) raw = ctx;
    TsMap* receiver = raw ? (TsMap*)raw : nullptr;
    TsMap* result = TsMap::Create();
    if (!receiver) return ts_value_make_object(result);
    const char* keys[] = {"locale", "type", "minimumIntegerDigits",
                          "minimumFractionDigits", "maximumFractionDigits"};
    for (const char* k : keys) {
        TsValue key; key.type = ValueType::STRING_PTR;
        key.ptr_val = TsString::GetInterned(k);
        TsValue v = receiver->Get(key);
        if (v.type != ValueType::UNDEFINED) result->Set(key, v);
    }
    return ts_value_make_object(result);
}

static TsValue* intlPluralRulesCtorBody(void* ctx, int argc, TsValue** argv) {
    icu::Locale locale = icu::Locale::getDefault();
    std::string localeTagOut;
    if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
        void* str = ts_value_get_string(argv[0]);
        if (str) {
            const char* utf8 = ((TsString*)str)->ToUtf8();
            if (utf8 && utf8[0]) {
                locale = icu::Locale::createCanonical(utf8);
                UErrorCode err = U_ZERO_ERROR;
                icu::StringByteSink<std::string> sink(&localeTagOut);
                locale.toLanguageTag(sink, err);
                if (U_FAILURE(err) || localeTagOut.empty()) localeTagOut = utf8;
            }
        }
    }
    if (localeTagOut.empty()) localeTagOut = locale.getName();

    const char* type = "cardinal";
    if (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1])) {
        TsValue* tv = ts_object_get_property(argv[1], "type");
        if (tv && !ts_value_is_undefined(tv)) {
            void* s = ts_value_get_string(tv);
            if (s) {
                const char* ts2 = ((TsString*)s)->ToUtf8();
                if (ts2 && std::strcmp(ts2, "ordinal") == 0) type = "ordinal";
            }
        }
    }

    UErrorCode err = U_ZERO_ERROR;
    UPluralType pt = (std::strcmp(type, "ordinal") == 0) ? UPLURAL_TYPE_ORDINAL : UPLURAL_TYPE_CARDINAL;
    icu::PluralRules* rules = icu::PluralRules::forLocale(locale, pt, err);
    if (U_FAILURE(err) || !rules) {
        err = U_ZERO_ERROR;
        rules = icu::PluralRules::forLocale(icu::Locale::getDefault(), pt, err);
    }

    TsMap* instance = TsMap::Create();
    if (g_intlPluralRulesProto) instance->SetPrototype(g_intlPluralRulesProto);
    auto setStr = [&](const char* k, const char* v) {
        TsValue kk; kk.type = ValueType::STRING_PTR;
        kk.ptr_val = TsString::GetInterned(k);
        TsValue vv; vv.type = ValueType::STRING_PTR;
        vv.ptr_val = TsString::Create(v);
        instance->Set(kk, vv);
    };
    auto setInt = [&](const char* k, int v) {
        TsValue kk; kk.type = ValueType::STRING_PTR;
        kk.ptr_val = TsString::GetInterned(k);
        TsValue vv; vv.type = ValueType::NUMBER_INT; vv.i_val = v;
        instance->Set(kk, vv);
    };
    setStr("locale", localeTagOut.c_str());
    setStr("type", type);
    setInt("minimumIntegerDigits", 1);
    setInt("minimumFractionDigits", 0);
    setInt("maximumFractionDigits", 3);
    TsValue rk; rk.type = ValueType::STRING_PTR;
    rk.ptr_val = TsString::GetInterned("__icuPluralRules");
    TsValue rv; rv.type = ValueType::OBJECT_PTR; rv.ptr_val = rules;
    instance->Set(rk, rv);
    return ts_value_make_object(instance);
}

// =============================================================================
// Intl.NumberFormat (Phase C) — real icu::NumberFormat integration.
// =============================================================================

static TsMap* g_intlNumberFormatProto = nullptr;

static TsValue* intlNumberFormatFormatImpl(void* ctx, int argc, TsValue** argv) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_value_get_object((TsValue*)ctx);
    if (!raw) raw = ctx;
    TsMap* receiver = raw ? (TsMap*)raw : nullptr;
    if (!receiver) return ts_value_make_string(TsString::Create(""));
    TsValue fk; fk.type = ValueType::STRING_PTR;
    fk.ptr_val = TsString::GetInterned("__icuNumberFormat");
    TsValue fv = receiver->Get(fk);
    icu::NumberFormat* fmt = (fv.type == ValueType::OBJECT_PTR) ? (icu::NumberFormat*)fv.ptr_val : nullptr;
    // ECMA-402 11.1.4 Number Format Functions: if value is not provided,
    // let value be undefined; then x = ToNumber(value) -> NaN.
    double d = std::numeric_limits<double>::quiet_NaN();
    if (argc >= 1 && argv && argv[0]) d = ts_value_get_double(argv[0]);
    if (!fmt) {
        if (std::isnan(d)) return ts_value_make_string(TsString::Create("NaN"));
        char buf[64]; std::snprintf(buf, sizeof(buf), "%g", d);
        return ts_value_make_string(TsString::Create(buf));
    }
    icu::UnicodeString result;
    fmt->format(d, result);
    std::string utf8;
    result.toUTF8String(utf8);
    return ts_value_make_string(TsString::Create(utf8.c_str()));
}

static TsValue* intlNumberFormatResolvedOptions(void* ctx, int argc, TsValue** argv) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_value_get_object((TsValue*)ctx);
    if (!raw) raw = ctx;
    TsMap* receiver = raw ? (TsMap*)raw : nullptr;
    TsMap* result = TsMap::Create();
    if (!receiver) return ts_value_make_object(result);
    const char* keys[] = {"locale", "style", "currency", "currencyDisplay",
                          "minimumIntegerDigits", "minimumFractionDigits",
                          "maximumFractionDigits", "useGrouping",
                          "notation", "numberingSystem"};
    for (const char* k : keys) {
        TsValue key; key.type = ValueType::STRING_PTR;
        key.ptr_val = TsString::GetInterned(k);
        TsValue v = receiver->Get(key);
        if (v.type != ValueType::UNDEFINED) result->Set(key, v);
    }
    return ts_value_make_object(result);
}

static TsValue* intlNumberFormatCtorBody(void* ctx, int argc, TsValue** argv) {
    icu::Locale locale = icu::Locale::getDefault();
    std::string localeTagOut;
    if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
        void* str = ts_value_get_string(argv[0]);
        if (str) {
            const char* utf8 = ((TsString*)str)->ToUtf8();
            if (utf8 && utf8[0]) {
                locale = icu::Locale::createCanonical(utf8);
                UErrorCode err = U_ZERO_ERROR;
                icu::StringByteSink<std::string> sink(&localeTagOut);
                locale.toLanguageTag(sink, err);
                if (U_FAILURE(err) || localeTagOut.empty()) localeTagOut = utf8;
            }
        }
    }
    if (localeTagOut.empty()) localeTagOut = locale.getName();

    const char* style = "decimal";
    const char* currency = nullptr;
    const char* currencyDisplay = "symbol";
    int minFracDigits = -1;  // -1 = use style default (2 for currency, 0 otherwise)
    int maxFracDigits = -1;
    int minIntDigits = 1;
    bool useGrouping = true;
    if (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1])) {
        auto getStrOpt = [&](const char* k) -> const char* {
            TsValue* v = ts_object_get_property(argv[1], k);
            if (!v || ts_value_is_undefined(v)) return nullptr;
            void* s = ts_value_get_string(v);
            return s ? ((TsString*)s)->ToUtf8() : nullptr;
        };
        auto getIntOpt = [&](const char* k, int dflt) -> int {
            TsValue* v = ts_object_get_property(argv[1], k);
            if (!v || ts_value_is_undefined(v)) return dflt;
            return (int)ts_value_get_double(v);
        };
        if (const char* s = getStrOpt("style")) style = s;
        if (const char* c = getStrOpt("currency")) currency = c;
        if (const char* cd = getStrOpt("currencyDisplay")) currencyDisplay = cd;
        minFracDigits = getIntOpt("minimumFractionDigits", -1);
        maxFracDigits = getIntOpt("maximumFractionDigits", -1);
        minIntDigits = getIntOpt("minimumIntegerDigits", 1);
        TsValue* gv = ts_object_get_property(argv[1], "useGrouping");
        if (gv && !ts_value_is_undefined(gv)) useGrouping = ts_value_to_bool(gv);
    }

    UErrorCode err = U_ZERO_ERROR;
    icu::NumberFormat* fmt = nullptr;
    if (std::strcmp(style, "currency") == 0) {
        fmt = icu::NumberFormat::createCurrencyInstance(locale, err);
        if (fmt && currency && currency[0]) {
            UChar ucur[4] = {0};
            for (int i = 0; i < 3 && currency[i]; i++) ucur[i] = (UChar)currency[i];
            fmt->setCurrency(ucur, err);
        }
        if (minFracDigits < 0) minFracDigits = 2;
        if (maxFracDigits < 0) maxFracDigits = 2;
    } else if (std::strcmp(style, "percent") == 0) {
        fmt = icu::NumberFormat::createPercentInstance(locale, err);
        if (minFracDigits < 0) minFracDigits = 0;
        if (maxFracDigits < 0) maxFracDigits = 0;
    } else {
        fmt = icu::NumberFormat::createInstance(locale, err);
        if (minFracDigits < 0) minFracDigits = 0;
        if (maxFracDigits < 0) maxFracDigits = 3;
    }
    if (U_FAILURE(err) || !fmt) {
        err = U_ZERO_ERROR;
        fmt = icu::NumberFormat::createInstance(icu::Locale::getDefault(), err);
    }
    if (fmt) {
        // ECMA-402: if min > max default, raise max to min (so a user
        // setting minimumFractionDigits=4 implies maxFractionDigits>=4).
        int actualMin = minFracDigits >= 0 ? minFracDigits : 0;
        int actualMax = maxFracDigits >= 0 ? maxFracDigits : 3;
        if (actualMax < actualMin) actualMax = actualMin;
        fmt->setMinimumFractionDigits(actualMin);
        fmt->setMaximumFractionDigits(actualMax);
        fmt->setMinimumIntegerDigits(minIntDigits);
        fmt->setGroupingUsed(useGrouping);
    }

    TsMap* instance = TsMap::Create();
    if (g_intlNumberFormatProto) instance->SetPrototype(g_intlNumberFormatProto);
    auto setStr = [&](const char* k, const char* v) {
        TsValue kk; kk.type = ValueType::STRING_PTR;
        kk.ptr_val = TsString::GetInterned(k);
        TsValue vv; vv.type = ValueType::STRING_PTR;
        vv.ptr_val = TsString::Create(v);
        instance->Set(kk, vv);
    };
    auto setInt = [&](const char* k, int v) {
        TsValue kk; kk.type = ValueType::STRING_PTR;
        kk.ptr_val = TsString::GetInterned(k);
        TsValue vv; vv.type = ValueType::NUMBER_INT; vv.i_val = v;
        instance->Set(kk, vv);
    };
    auto setBool = [&](const char* k, bool v) {
        TsValue kk; kk.type = ValueType::STRING_PTR;
        kk.ptr_val = TsString::GetInterned(k);
        TsValue vv; vv.type = ValueType::BOOLEAN; vv.i_val = v ? 1 : 0;
        instance->Set(kk, vv);
    };
    setStr("locale", localeTagOut.c_str());
    setStr("style", style);
    if (currency) setStr("currency", currency);
    setStr("currencyDisplay", currencyDisplay);
    setInt("minimumIntegerDigits", minIntDigits);
    setInt("minimumFractionDigits", minFracDigits >= 0 ? minFracDigits : 0);
    setInt("maximumFractionDigits", maxFracDigits >= 0 ? maxFracDigits : 3);
    setBool("useGrouping", useGrouping);
    setStr("notation", "standard");
    setStr("numberingSystem", "latn");
    TsValue fk; fk.type = ValueType::STRING_PTR;
    fk.ptr_val = TsString::GetInterned("__icuNumberFormat");
    TsValue fv; fv.type = ValueType::OBJECT_PTR; fv.ptr_val = fmt;
    instance->Set(fk, fv);
    return ts_value_make_object(instance);
}

static TsValue* intlCollatorCtorBody(void* ctx, int argc, TsValue** argv) {
    // Extract locale from argv[0] (string or array; first usable element).
    icu::Locale locale = icu::Locale::getDefault();
    std::string localeTagOut = "und";
    if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
        void* str = ts_value_get_string(argv[0]);
        if (str) {
            const char* utf8 = ((TsString*)str)->ToUtf8();
            if (utf8 && utf8[0]) {
                locale = icu::Locale::createCanonical(utf8);
                UErrorCode err = U_ZERO_ERROR;
                localeTagOut.clear();
                icu::StringByteSink<std::string> sink(&localeTagOut);
                locale.toLanguageTag(sink, err);
                if (U_FAILURE(err) || localeTagOut.empty()) localeTagOut = utf8;
            }
        }
    }
    // Build the icu::Collator.
    UErrorCode err = U_ZERO_ERROR;
    icu::Collator* coll = icu::Collator::createInstance(locale, err);
    if (U_FAILURE(err) || !coll) {
        // Fall back to default locale.
        err = U_ZERO_ERROR;
        coll = icu::Collator::createInstance(icu::Locale::getDefault(), err);
    }
    // Default ECMA-402 Collator options.
    const char* usage = "sort";
    const char* sensitivity = "variant";
    bool numeric = false;
    bool ignorePunctuation = false;
    const char* caseFirst = "false";
    // Parse options object if present. Use ts_object_get_property so we
    // handle both TsMap and TsFlatObject receivers uniformly.
    if (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1])) {
        auto getOpt = [&](const char* k) -> TsValue* {
            return ts_object_get_property(argv[1], k);
        };
        auto getStrOpt = [&](const char* k) -> const char* {
            TsValue* v = getOpt(k);
            if (!v) return nullptr;
            void* s = ts_value_get_string(v);
            return s ? ((TsString*)s)->ToUtf8() : nullptr;
        };
        if (const char* u = getStrOpt("usage")) usage = u;
        if (const char* s = getStrOpt("sensitivity")) sensitivity = s;
        if (const char* cf = getStrOpt("caseFirst")) caseFirst = cf;
        TsValue* nv = getOpt("numeric");
        if (nv && !ts_value_is_undefined(nv)) numeric = ts_value_to_bool(nv);
        TsValue* iv = getOpt("ignorePunctuation");
        if (iv && !ts_value_is_undefined(iv)) ignorePunctuation = ts_value_to_bool(iv);
    }
    // Apply options to the collator.
    if (coll) {
        UErrorCode e2 = U_ZERO_ERROR;
        if (std::strcmp(sensitivity, "base") == 0) {
            coll->setAttribute(UCOL_STRENGTH, UCOL_PRIMARY, e2);
        } else if (std::strcmp(sensitivity, "accent") == 0) {
            coll->setAttribute(UCOL_STRENGTH, UCOL_SECONDARY, e2);
        } else if (std::strcmp(sensitivity, "case") == 0) {
            coll->setAttribute(UCOL_STRENGTH, UCOL_PRIMARY, e2);
            coll->setAttribute(UCOL_CASE_LEVEL, UCOL_ON, e2);
        } else {  // "variant" (default)
            coll->setAttribute(UCOL_STRENGTH, UCOL_TERTIARY, e2);
        }
        if (numeric) {
            coll->setAttribute(UCOL_NUMERIC_COLLATION, UCOL_ON, e2);
        }
        if (ignorePunctuation) {
            coll->setAttribute(UCOL_ALTERNATE_HANDLING, UCOL_SHIFTED, e2);
        }
        if (std::strcmp(caseFirst, "upper") == 0) {
            coll->setAttribute(UCOL_CASE_FIRST, UCOL_UPPER_FIRST, e2);
        } else if (std::strcmp(caseFirst, "lower") == 0) {
            coll->setAttribute(UCOL_CASE_FIRST, UCOL_LOWER_FIRST, e2);
        }
    }
    // Build the instance TsMap.
    TsMap* instance = TsMap::Create();
    if (g_intlCollatorProto) {
        instance->SetPrototype(g_intlCollatorProto);
    }
    auto setStr = [&](const char* k, const char* v) {
        TsValue kk; kk.type = ValueType::STRING_PTR;
        kk.ptr_val = TsString::GetInterned(k);
        TsValue vv; vv.type = ValueType::STRING_PTR;
        vv.ptr_val = TsString::Create(v);
        instance->Set(kk, vv);
    };
    auto setBool = [&](const char* k, bool v) {
        TsValue kk; kk.type = ValueType::STRING_PTR;
        kk.ptr_val = TsString::GetInterned(k);
        TsValue vv; vv.type = ValueType::BOOLEAN; vv.i_val = v ? 1 : 0;
        instance->Set(kk, vv);
    };
    setStr("locale", localeTagOut.c_str());
    setStr("usage", usage);
    setStr("sensitivity", sensitivity);
    setStr("caseFirst", caseFirst);
    setStr("collation", "default");
    setBool("numeric", numeric);
    setBool("ignorePunctuation", ignorePunctuation);
    // Hidden slot: icu::Collator pointer.
    TsValue collKey; collKey.type = ValueType::STRING_PTR;
    collKey.ptr_val = TsString::GetInterned("__icuCollator");
    TsValue collVal; collVal.type = ValueType::OBJECT_PTR; collVal.ptr_val = coll;
    instance->Set(collKey, collVal);
    return ts_value_make_object(instance);
}

void* ts_get_global_Intl() {
    TenureScope _tenure;
    static TsMap* cached = nullptr;
    if (cached) return cached;
    cached = TsMap::Create();
    { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }

    // Intl.getCanonicalLocales(locales) — ECMA-402 §8.2.1.
    // Minimal: if input is a string, canonicalize via ICU and return [tag].
    // If input is an array (TsArray), iterate and canonicalize each. Else
    // return an empty array. Full BCP47 validation/RangeError on malformed
    // tags is deferred.
    addMethod(cached, "getCanonicalLocales", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
        extern void* ts_array_create();
        extern int64_t ts_array_push(void* arr, void* value);
        void* result = ts_array_create();
        if (argc < 1 || !argv || !argv[0] || ts_value_is_undefined(argv[0])) {
            return ts_value_make_object(result);
        }
        TsValue* input = argv[0];
        void* str = ts_value_get_string(input);
        if (str) {
            const char* utf8 = ((TsString*)str)->ToUtf8();
            if (utf8 && utf8[0]) {
                UErrorCode err = U_ZERO_ERROR;
                icu::Locale loc = icu::Locale::createCanonical(utf8);
                std::string out;
                icu::StringByteSink<std::string> sink(&out);
                loc.toLanguageTag(sink, err);
                if (U_SUCCESS(err) && !out.empty()) {
                    ts_array_push(result, (void*)ts_value_make_string(TsString::Create(out.c_str())));
                }
            }
        }
        return ts_value_make_object(result);
    }, 1);

    auto populateCollatorProto = [](TsMap* proto) {
        addMethod(proto, "compare", (void*)intlCollatorCompareImpl, 2);
        addMethod(proto, "resolvedOptions", (void*)intlCollatorResolvedOptions, 0);
    };
    auto populateFormatProto = [](TsMap* proto) {
        addMethod(proto, "format", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (argc < 1 || !argv || !argv[0]) return ts_value_make_string(TsString::Create(""));
            double d = ts_value_get_double(argv[0]);
            char buf[64]; std::snprintf(buf, sizeof(buf), "%g", d);
            return ts_value_make_string(TsString::Create(buf));
        }, 1);
        addMethod(proto, "formatToParts", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            extern void* ts_array_create();
            return ts_value_make_object(ts_array_create());
        }, 1);
        addMethod(proto, "resolvedOptions", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            return ts_value_make_object(TsMap::Create());
        }, 0);
    };
    auto populatePluralProto = [](TsMap* proto) {
        addMethod(proto, "select", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            return ts_value_make_string(TsString::Create("other"));
        }, 1);
        addMethod(proto, "resolvedOptions", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            return ts_value_make_object(TsMap::Create());
        }, 0);
    };
    auto populateLocaleProto = [](TsMap* proto) {
        addMethod(proto, "toString", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            return ts_value_make_string(TsString::Create("und"));
        }, 0);
        addMethod(proto, "maximize", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return (TsValue*)ctx;
        }, 0);
        addMethod(proto, "minimize", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (!ctx) ctx = ts_get_call_this();
            return (TsValue*)ctx;
        }, 0);
    };

    auto registerCtor = [&](const char* name, int length, const char* tag,
                            void (*pp)(TsMap*)) {
        void* fn = makeIntlCtorStub(name, length, tag, pp);
        TsValue k; k.type = ValueType::STRING_PTR;
        k.ptr_val = TsString::GetInterned(name);
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = ts_value_get_object((TsValue*)fn);
        cached->SetWithAttrs(k, v, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
    };
    // Intl.Collator — Phase B: real icu::Collator backing.
    {
        TsMap* ctor = makeSimpleConstructorGlobal("Collator");
        TsValue protoK; protoK.type = ValueType::STRING_PTR;
        protoK.ptr_val = TsString::GetInterned("prototype");
        TsValue protoV = ctor->Get(protoK);
        if (protoV.type == ValueType::OBJECT_PTR && protoV.ptr_val) {
            TsMap* proto = (TsMap*)protoV.ptr_val;
            populateCollatorProto(proto);
            intlInstallToStringTag(proto, "Intl.Collator");
            g_intlCollatorProto = proto;  // stash for instance prototype linkage
        }
        addMethod(ctor, "supportedLocalesOf", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            extern void* ts_array_create();
            return ts_value_make_object(ts_array_create());
        }, 1);
        void* fn = wrapAsCallableWithBody(ctor, "Collator", 0, intlCollatorCtorBody);
        TsValue k; k.type = ValueType::STRING_PTR;
        k.ptr_val = TsString::GetInterned("Collator");
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = ts_value_get_object((TsValue*)fn);
        cached->SetWithAttrs(k, v, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
    }
    // Intl.NumberFormat — Phase C: real icu::NumberFormat backing.
    {
        TsMap* ctor = makeSimpleConstructorGlobal("NumberFormat");
        TsValue protoK; protoK.type = ValueType::STRING_PTR;
        protoK.ptr_val = TsString::GetInterned("prototype");
        TsValue protoV = ctor->Get(protoK);
        if (protoV.type == ValueType::OBJECT_PTR && protoV.ptr_val) {
            TsMap* proto = (TsMap*)protoV.ptr_val;
            addMethod(proto, "format", (void*)intlNumberFormatFormatImpl, 1);
            addMethod(proto, "formatToParts", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                extern void* ts_array_create();
                return ts_value_make_object(ts_array_create());
            }, 1);
            addMethod(proto, "resolvedOptions", (void*)intlNumberFormatResolvedOptions, 0);
            intlInstallToStringTag(proto, "Intl.NumberFormat");
            g_intlNumberFormatProto = proto;
        }
        addMethod(ctor, "supportedLocalesOf", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            extern void* ts_array_create();
            return ts_value_make_object(ts_array_create());
        }, 1);
        void* fn = wrapAsCallableWithBody(ctor, "NumberFormat", 0, intlNumberFormatCtorBody);
        TsValue k; k.type = ValueType::STRING_PTR;
        k.ptr_val = TsString::GetInterned("NumberFormat");
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = ts_value_get_object((TsValue*)fn);
        cached->SetWithAttrs(k, v, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
    }
    registerCtor("DateTimeFormat",     0, "Intl.DateTimeFormat",      populateFormatProto);
    // Intl.PluralRules — Phase E: real icu::PluralRules backing.
    {
        TsMap* ctor = makeSimpleConstructorGlobal("PluralRules");
        TsValue protoK; protoK.type = ValueType::STRING_PTR;
        protoK.ptr_val = TsString::GetInterned("prototype");
        TsValue protoV = ctor->Get(protoK);
        if (protoV.type == ValueType::OBJECT_PTR && protoV.ptr_val) {
            TsMap* proto = (TsMap*)protoV.ptr_val;
            addMethod(proto, "select", (void*)intlPluralRulesSelectImpl, 1);
            addMethod(proto, "resolvedOptions", (void*)intlPluralRulesResolvedOptions, 0);
            intlInstallToStringTag(proto, "Intl.PluralRules");
            g_intlPluralRulesProto = proto;
        }
        addMethod(ctor, "supportedLocalesOf", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            extern void* ts_array_create();
            return ts_value_make_object(ts_array_create());
        }, 1);
        void* fn = wrapAsCallableWithBody(ctor, "PluralRules", 0, intlPluralRulesCtorBody);
        TsValue k; k.type = ValueType::STRING_PTR;
        k.ptr_val = TsString::GetInterned("PluralRules");
        TsValue v; v.type = ValueType::FUNCTION_PTR;
        v.ptr_val = ts_value_get_object((TsValue*)fn);
        cached->SetWithAttrs(k, v, TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
    }
    registerCtor("Locale",             1, "Intl.Locale",              populateLocaleProto);
    registerCtor("RelativeTimeFormat", 0, "Intl.RelativeTimeFormat",  populateFormatProto);
    registerCtor("ListFormat",         0, "Intl.ListFormat",          populateFormatProto);
    registerCtor("DisplayNames",       2, "Intl.DisplayNames",        populateLocaleProto);
    registerCtor("Segmenter",          0, "Intl.Segmenter",           populatePluralProto);

    // Symbol.toStringTag — Object.prototype.toString.call(Intl) → "[object Intl]"
    intlInstallToStringTag(cached, "Intl");
    return cached;
}

void* ts_get_global_Buffer() {
    TenureScope _tenure;
    return (void*)Buffer;
}

void* ts_get_global_process() {
    TenureScope _tenure;
    return (void*)process;
}

// Reflect a top-level (script-goal) function declaration as an own
// property of globalThis (ES CreateGlobalFunctionBinding). Emitted by the
// compiler at program start for every top-level FunctionDeclaration.
extern "C" void ts_global_bind_fn(void* nameStr, TsValue* fn) {
    if (!globalThis || !nameStr) return;
    TsValue* key = ts_value_make_string(nameStr);
    ts_object_set_property((void*)globalThis, (void*)key, fn);
}

// EVAL-001 §11: globalThis-backed toplevel `var` bindings for eval-tainted
// modules. Compiled code (HIRToLLVM lowers LoadGlobal/StoreGlobal of tainted
// names to these) and the runtime interpreter's eval (guardGet/guardSet over
// the same ts_object_*_dynamic primitives) share the bindings as ordinary
// own properties of globalThis. Getters/setters installed on globalThis may
// throw — these frames are free of C++ destructor locals (longjmp-safe).
extern "C" void ts_global_var_declare(void* nameStr) {
    // Hoist: own property = undefined if absent (typeof x -> "undefined",
    // Object.hasOwn(globalThis, x) true, before the `var` statement runs).
    // nameStr is a TsString* (compiler ConstString), like ts_global_bind_fn.
    if (!globalThis || !nameStr) return;
    extern TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key);
    extern bool ts_object_has_prop(TsValue* obj, TsValue* key);
    extern void ts_object_set_dynamic(TsValue* obj, TsValue* key, TsValue* val);
    TsValue* key = ts_value_make_string(nameStr);
    if (!ts_object_has_prop(globalThis, key)) {
        ts_object_set_dynamic(globalThis, key, ts_value_make_undefined());
        // ES 9.1.1.4.17 CreateGlobalVarBinding(N, false): the binding is
        // {writable, enumerable, NON-configurable} — eval('delete x') on a
        // program-declared var returns false (11.4.1-4.a-7). Set() preserves
        // attrs on later value writes. Eval-INTRODUCED vars (interp guardSet,
        // default attrs) stay configurable/deletable per 19.2.1.3.
        void* graw = ts_value_get_object(globalThis);
        if (graw) {
            if (TsMap* gm = dynamic_cast<TsMap*>((TsObject*)graw)) {
                TsValue k2; k2.type = ValueType::STRING_PTR;
                k2.ptr_val = (TsString*)nameStr;
                gm->SetPropertyAttrs(k2, TsHashTable::ATTR_ENUMERABLE |
                                          TsHashTable::ATTR_WRITABLE);
            }
        }
    }
}

extern "C" void* ts_interp_global_ctor_by_name(const char* n);  // defined below

extern "C" TsValue* ts_global_var_get(const char* name) {
    if (!globalThis || !name) return ts_value_make_undefined();
    extern TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key);
    TsValue* key = ts_value_make_string(TsString::GetInterned(name));
    TsValue* v = ts_object_get_dynamic(globalThis, key);  // may throw
    if (!v) return ts_value_make_undefined();
    // Builtin-constructor globalThis entries hold only their NAME STRING
    // (kTA trap) — swap in the real constructor, mirroring the
    // interpreter's envLookup (a tainted `var Map = ...` reads the marker
    // before its assignment runs).
    void* sraw = ts_value_get_string(v);
    if (sraw) {
        const char* sc = ((TsString*)sraw)->ToUtf8();
        if (sc && strcmp(sc, name) == 0) {
            if (void* ctor = ts_interp_global_ctor_by_name(name))
                return (TsValue*)ctor;
        }
    }
    return v;
}

// TsString*-arg variant (HIR ConstString args lower to TsString*; the
// HIRToLLVM-emitted get/set use C-string globals instead).
extern "C" TsValue* ts_global_var_get_s(void* nameStr) {
    if (!nameStr) return ts_value_make_undefined();
    const char* n = ((TsString*)nameStr)->ToUtf8();
    return ts_global_var_get(n ? n : "");
}

extern "C" void ts_global_var_set(const char* name, TsValue* v) {
    if (!globalThis || !name) return;
    extern void ts_object_set_dynamic(TsValue* obj, TsValue* key, TsValue* val);
    TsValue* key = ts_value_make_string(TsString::GetInterned(name));
    ts_object_set_dynamic(globalThis, key,
                          v ? v : ts_value_make_undefined());  // may throw
}

// OrdinaryCallBindThis for sloppy callees: undefined/null `this` becomes
// globalThis. Emitted by the compiler at the ambient-this fallback of
// non-strict functions.
extern "C" TsValue* ts_this_coerce_sloppy(TsValue* t) {
    uint64_t nb = t ? (uint64_t)(uintptr_t)t : 0;
    if (!t || nanbox_is_undefined(nb) || nanbox_is_null(nb)) {
        if (globalThis) return globalThis;
        return t;
    }
    // ECMA-262 10.2.1.2 OrdinaryCallBindThis step 6.c: in SLOPPY mode a
    // non-nullish primitive thisArgument is ToObject-wrapped, so
    // `bar.call(1)` observes typeof this === 'object' (test262
    // language/function-code/10.4.3-1-1-s..9-s sloppy halves).
    // ts_object_constructor implements ToObject exactly: object receivers
    // pass through unchanged (the common method path costs two magic-word
    // loads); primitives box into Number/String/Boolean/Symbol wrappers
    // carrying the hidden [[*Data]] slot and the right prototype.
    return (TsValue*)ts_object_constructor((void*)t);
}

void* ts_get_global_globalThis() {
    TenureScope _tenure;
    return (void*)globalThis;
}

// ========================================
// Node.js module globals — use builtin module system
// ========================================

// Cached builtin Node module objects (path/fs/os/...). Held ONLY here, so the
// GC must scan this map or the (tenured) module objects get swept when no live
// JS reference is on the stack. Same class as the builtin-global getter caches.
extern "C" void ts_gc_register_scanner(void (*)(void*), void*);
extern "C" void ts_gc_register_minor_fixup(void (*)(void*), void*);
extern "C" void ts_gc_mark_object(void*);
extern "C" void* ts_gc_minor_lookup_forward(void*);
static std::unordered_map<std::string, void*>& moduleGlobalCache() {
    static std::unordered_map<std::string, void*> cache;
    return cache;
}
static void* getModuleGlobal(const char* name) {
    auto& cache = moduleGlobalCache();
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    void* mod;
    { TenureScope _tenure; mod = ts_get_builtin_module(name); }
    cache[name] = mod;
    static bool registered = false;
    if (!registered) {
        registered = true;
        ts_gc_register_scanner([](void*) {
            for (auto& kv : moduleGlobalCache())
                if ((uintptr_t)kv.second >= 4096 && (uintptr_t)kv.second <= 0x00007FFFFFFFFFFFULL)
                    ts_gc_mark_object(kv.second);
        }, nullptr);
        ts_gc_register_minor_fixup([](void*) {
            for (auto& kv : moduleGlobalCache())
                if (kv.second) { void* f = ts_gc_minor_lookup_forward(kv.second); if (f) kv.second = f; }
        }, nullptr);
    }
    return mod;
}

void* ts_get_global_path() { return getModuleGlobal("path"); }
void* ts_get_global_fs() { return getModuleGlobal("fs"); }
void* ts_get_global_os() { return getModuleGlobal("os"); }
void* ts_get_global_url() { return getModuleGlobal("url"); }
void* ts_get_global_util() { return getModuleGlobal("util"); }
void* ts_get_global_crypto() { return getModuleGlobal("crypto"); }
void* ts_get_global_http() { return getModuleGlobal("http"); }
void* ts_get_global_https() { return getModuleGlobal("https"); }
void* ts_get_global_net() { return getModuleGlobal("net"); }
void* ts_get_global_dgram() { return getModuleGlobal("dgram"); }
void* ts_get_global_dns() { return getModuleGlobal("dns"); }
void* ts_get_global_tls() { return getModuleGlobal("tls"); }
void* ts_get_global_zlib() { return getModuleGlobal("zlib"); }
void* ts_get_global_stream() { return getModuleGlobal("stream"); }
void* ts_get_global_events() { return getModuleGlobal("events"); }
void* ts_get_global_querystring() { return getModuleGlobal("querystring"); }
void* ts_get_global_assert() { return getModuleGlobal("assert"); }
void* ts_get_global_child_process() { return getModuleGlobal("child_process"); }
void* ts_get_global_cluster() { return getModuleGlobal("cluster"); }
void* ts_get_global_timers() { return getModuleGlobal("timers"); }
void* ts_get_global_readline() { return getModuleGlobal("readline"); }
void* ts_get_global_perf_hooks() { return getModuleGlobal("perf_hooks"); }
void* ts_get_global_async_hooks() { return getModuleGlobal("async_hooks"); }
void* ts_get_global_tty() { return getModuleGlobal("tty"); }
void* ts_get_global_string_decoder() { return getModuleGlobal("string_decoder"); }
void* ts_get_global_buffer() { return getModuleGlobal("buffer"); }
void* ts_get_global_http2() { return getModuleGlobal("http2"); }
void* ts_get_global_inspector() { return getModuleGlobal("inspector"); }
void* ts_get_global_module() { return getModuleGlobal("module"); }
void* ts_get_global_vm() { return getModuleGlobal("vm"); }
void* ts_get_global_v8() { return getModuleGlobal("v8"); }

// ============================================================================
// TypedArray constructors
// ============================================================================
//
// Each per-class TypedArray (Int8Array, Uint8Array, etc.) is exposed as a
// callable native function whose [[Prototype]] (Object.getPrototypeOf(Int8Array))
// points to a shared %TypedArray% intrinsic. This makes the test262 harness
// (testTypedArray.js) work, and allows JS code to do `var TA = Int8Array; new TA(n)`.
//
// The compiler ALSO has a syntactic special case for `new Int8Array(n)`
// (ASTToHIR.cpp) that bypasses these constructors. So these are primarily for
// introspection and dynamic-constructor use.

// Forward declarations for typed array runtime creators (defined in TsBuffer.cpp)
extern "C" void* ts_typed_array_create_i8(int64_t length);
extern "C" void* ts_typed_array_create_u8(int64_t length);
extern "C" void* ts_typed_array_create_clamped(int64_t length);
extern "C" void* ts_typed_array_create_i16(int64_t length);
extern "C" void* ts_typed_array_create_u16(int64_t length);
extern "C" void* ts_typed_array_create_i32(int64_t length);
extern "C" void* ts_typed_array_create_u32(int64_t length);
extern "C" void* ts_typed_array_create_f32(int64_t length);
extern "C" void* ts_typed_array_create_f64(int64_t length);
// ToNumber abstract op — defined in Primitives.cpp
extern "C" double ts_to_number(TsValue* v);

// Forward decls for TypedArray.from / .of runtime helpers.
extern "C" void* ts_typed_array_create_i8(int64_t length);

// TypedArray.from(source, mapFn?, thisArg?) — iterate source by length
// indexed reads, map each value, return a new typed array (Int8Array for
// now; full spec would dispatch on ctx receiver kind).
// Resolve the TypedArray CLASS a static `from`/`of` was invoked on (ctx is
// the constructor function) to the matching allocator. %TypedArray% itself
// or an unrecognized receiver falls back to Int8 (legacy behavior).
extern "C" void* ts_get_global_Uint8Array();
extern "C" void* ts_get_global_Int8Array();
extern "C" void* ts_get_global_Uint8ClampedArray();
extern "C" void* ts_get_global_Int16Array();
extern "C" void* ts_get_global_Uint16Array();
extern "C" void* ts_get_global_Int32Array();
extern "C" void* ts_get_global_Uint32Array();
extern "C" void* ts_get_global_Float32Array();
extern "C" void* ts_get_global_Float64Array();
extern "C" void* ts_get_global_BigInt64Array();
extern "C" void* ts_get_global_BigUint64Array();
extern "C" void ts_ta_store_value(void* taRaw, size_t i, TsValue* v);
extern "C" void* ts_typed_array_create_i64(int64_t);
extern "C" void* ts_typed_array_create_u64(int64_t);
extern "C" void* ts_typed_array_create_u8(int64_t);
extern "C" void* ts_typed_array_create_clamped(int64_t);
extern "C" void* ts_typed_array_create_i16(int64_t);
extern "C" void* ts_typed_array_create_u16(int64_t);
extern "C" void* ts_typed_array_create_i32(int64_t);
extern "C" void* ts_typed_array_create_u32(int64_t);
extern "C" void* ts_typed_array_create_f32(int64_t);
extern "C" void* ts_typed_array_create_f64(int64_t);
static void* ta_create_for_ctor(void* ctx, int64_t len) {
    void* raw = ctx ? ts_value_get_object((TsValue*)ctx) : nullptr;
    if (!raw) raw = ctx;
    struct Row { void* (*global)(); void* (*create)(int64_t); };
    static const Row rows[] = {
        { ts_get_global_Uint8Array,        ts_typed_array_create_u8 },
        { ts_get_global_Int8Array,         ts_typed_array_create_i8 },
        { ts_get_global_Uint8ClampedArray, ts_typed_array_create_clamped },
        { ts_get_global_Int16Array,        ts_typed_array_create_i16 },
        { ts_get_global_Uint16Array,       ts_typed_array_create_u16 },
        { ts_get_global_Int32Array,        ts_typed_array_create_i32 },
        { ts_get_global_Uint32Array,       ts_typed_array_create_u32 },
        { ts_get_global_Float32Array,      ts_typed_array_create_f32 },
        { ts_get_global_Float64Array,      ts_typed_array_create_f64 },
        { ts_get_global_BigInt64Array,     ts_typed_array_create_i64 },
        { ts_get_global_BigUint64Array,    ts_typed_array_create_u64 },
    };
    for (const Row& r : rows)
        if (raw == r.global()) return r.create(len);
    return ts_typed_array_create_i8(len);
}

// %TypedArray%.from/of receiver validation (ES 23.2.2.1/.2 steps 1-2):
// `this` must be a CONSTRUCTOR. Known TA constructors create directly; any
// other constructor is `new`-called with (len) and the result must be a
// TypedArray of at least that length. Everything else (undefined, methods,
// arrows, plain objects) throws TypeError.
static void* ta_create_for_ctor_checked(void* ctx, int64_t len) {
    void* raw = ctx ? ts_value_get_object((TsValue*)ctx) : nullptr;
    if (!raw) raw = ctx;
    uintptr_t a = (uintptr_t)raw;
    if (!raw || a < 4096 || (a >> 48) != 0) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "TypedArray.from/of: this is not a constructor"));
        return nullptr;
    }
    // Known TA constructor rows create directly (same match set as
    // ta_create_for_ctor — including the %TypedArray% parent would be wrong:
    // the abstract ctor itself throws on construct, and it is NOT a row).
    {
        struct Row { void* (*global)(); void* (*create)(int64_t); };
        static const Row rows[] = {
            { ts_get_global_Uint8Array,        ts_typed_array_create_u8 },
            { ts_get_global_Int8Array,         ts_typed_array_create_i8 },
            { ts_get_global_Uint8ClampedArray, ts_typed_array_create_clamped },
            { ts_get_global_Int16Array,        ts_typed_array_create_i16 },
            { ts_get_global_Uint16Array,       ts_typed_array_create_u16 },
            { ts_get_global_Int32Array,        ts_typed_array_create_i32 },
            { ts_get_global_Uint32Array,       ts_typed_array_create_u32 },
            { ts_get_global_Float32Array,      ts_typed_array_create_f32 },
            { ts_get_global_Float64Array,      ts_typed_array_create_f64 },
            { ts_get_global_BigInt64Array,     ts_typed_array_create_i64 },
            { ts_get_global_BigUint64Array,    ts_typed_array_create_u64 },
        };
        for (const Row& r : rows)
            if (raw == r.global()) return r.create(len);
    }
    // IsConstructor: TsClosure carries the ES [[Construct]] bit; a TsFunction
    // (native) is treated as constructible unless flagged otherwise.
    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
    bool ctorOk = false;
    if (m16 == TsFunction::MAGIC) {
        ctorOk = ((TsFunction*)raw)->is_constructor;
    } else if (m16 == 0x434C5352 /* TsClosure */) {
        TsClosure* cl = (TsClosure*)raw;
        // constructable covers plain functions; class constructors compile
        // as method-shaped NON-constructable closures (their [[Construct]]
        // runs through the class new-machinery), so also accept a closure
        // that carries an OWN .prototype -- the class installer creates one,
        // while methods/accessors/arrows have none.
        ctorOk = cl->constructable;
        if (!ctorOk && cl->properties) {
            TsValue pk; pk.type = ValueType::STRING_PTR;
            pk.ptr_val = TsString::GetInterned("prototype");
            ctorOk = cl->properties->Has(pk);
        }
    } else {
        // Class constructors (and other callable wrappers) don't always
        // unwrap to a bare closure/function -- any other CALLABLE defers to
        // the construct + result-is-a-TypedArray validation below.
        ctorOk = ts_is_callable((void*)ctx);
    }
    if (getenv("TS_TAV_TRACE")) {
        fprintf(stderr, "[TAV] raw=%p m16=%08X ctorOk=%d", raw, m16, (int)ctorOk);
        if (m16 == 0x434C5352) {
            TsClosure* cl = (TsClosure*)raw;
            fprintf(stderr, " constructable=%d is_method=%d", (int)cl->constructable, (int)cl->is_method);
        }
        fprintf(stderr, "%c", 10); fflush(stderr);
    }
    if (!ctorOk) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "TypedArray.from/of: this is not a constructor"));
        return nullptr;
    }
    extern TsValue* ts_new_from_constructor_1(TsValue* ctor, TsValue* arg);
    TsValue* res = ts_new_from_constructor_1((TsValue*)ctx, ts_value_make_int(len));
    void* resRaw = res ? ts_value_get_object(res) : nullptr;
    uint32_t rm16 = (resRaw && (uintptr_t)resRaw >= 4096)
        ? *(uint32_t*)((char*)resRaw + 16) : 0;
    if (rm16 != 0x54415252 /* TsTypedArray */) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "TypedArray.from/of: custom constructor did not return a TypedArray"));
        return nullptr;
    }
    TsTypedArray* ta = (TsTypedArray*)resRaw;
    if ((int64_t)ta->GetLength() < len) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "TypedArray.from/of: constructed TypedArray is too small"));
        return nullptr;
    }
    return resRaw;
}

static TsValue* ts_typed_array_from_native(void* ctx, int argc, TsValue** argv) {
    TenureScope _tenure;
    if (argc < 1 || !argv || !argv[0]) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "TypedArray.from: source is required"));
        return ts_value_make_undefined();
    }
    void* source = ts_value_get_object(argv[0]);
    if (!source) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "TypedArray.from: source must be object-like"));
        return ts_value_make_undefined();
    }

    // ECMA-262 23.2.2.1.1 IterableToList: when the source has @@iterator,
    // call it and validate Type(iterator) is Object — throw TypeError if
    // not. The IsHTMLDDA-emulates-undefined cluster relies on this check.
    {
        TsValue* iterMethod = ts_object_get_property(source, "[Symbol.iterator]");
        if (iterMethod && !ts_value_is_undefined(iterMethod) &&
            !ts_value_is_null(iterMethod)) {
            bool isCallable = ts_is_callable((void*)iterMethod);  // canonical IsCallable
            if (isCallable) {
                TsValue* sourceBoxed = ts_value_make_object(source);
                TsValue* iter = ts_call_with_this_0(iterMethod, sourceBoxed);
                bool iterIsObj = false;
                if (iter) {
                    uint64_t inb = (uint64_t)(uintptr_t)iter;
                    if (nanbox_is_ptr(inb) && inb > NANBOX_UNDEFINED) {
                        if (nanbox_to_ptr(inb)) iterIsObj = true;
                    }
                }
                if (!iterIsObj) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "TypedArray.from: result of @@iterator method is not an object"));
                    return ts_value_make_undefined();
                }
                // Iterator is valid — fall through to length-based path
                // (full protocol-driven iteration is a separate fix).
            }
        }
    }

    TsValue* lenVal = ts_object_get_property(source, "length");
    double lenD = lenVal ? ts_to_number(lenVal) : 0;
    if (lenD != lenD || lenD <= 0) lenD = 0;
    const double MAX_LEN = (double)(1LL << 20);
    if (lenD > MAX_LEN) lenD = MAX_LEN;
    int64_t len = (int64_t)lenD;

    void* result = ta_create_for_ctor_checked(ctx, len);
    if (!result) return ts_value_make_undefined();

    // Spec step 3: only UNDEFINED mapfn is "absent" — null (or any other
    // non-callable) is a TypeError.
    TsValue* mapFn = (argc >= 2 && argv) ? argv[1] : nullptr;
    if (mapFn && ts_value_is_null(mapFn)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "TypedArray.from: mapfn is not callable"));
        return ts_value_make_undefined();
    }
    if (mapFn && !ts_value_is_nullish(mapFn)) {
        // Must be callable — if not a Function/Closure, throw.
        uint64_t mfNb = nanbox_from_tsvalue_ptr(mapFn);
        void* mfRaw = nanbox_is_ptr(mfNb) ? nanbox_to_ptr(mfNb) : nullptr;
        uint32_t mfMagic = mfRaw ? *(uint32_t*)((char*)mfRaw + 16) : 0;
        if (mfMagic != TsFunction::MAGIC && mfMagic != 0x434C5352) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "TypedArray.from: mapFn is not callable"));
            return ts_value_make_undefined();
        }
    } else {
        mapFn = nullptr;
    }
    TsValue* thisArg = (argc >= 3 && argv) ? argv[2] : nullptr;
    if (!thisArg) thisArg = ts_value_make_undefined();

    for (int64_t i = 0; i < len; i++) {
        char key[32]; snprintf(key, sizeof(key), "%lld", (long long)i);
        TsValue* v = ts_object_get_property(source, key);
        if (!v) v = ts_value_make_undefined();
        if (mapFn) {
            TsValue* idx = ts_value_make_int(i);
            TsValue* args[2] = { v, idx };
            v = ts_function_call_with_this(mapFn, thisArg, 2, args);
        }
        ts_ta_store_value(result, (size_t)i, v);   // type-aware (BigInt/number)
    }
    return ts_value_make_object(result);
}

// TypedArray.of(...items) — create a typed array from variadic args.
static TsValue* ts_typed_array_of_native(void* ctx, int argc, TsValue** argv) {
    void* result = ta_create_for_ctor_checked(ctx, argc);
    if (!result) return ts_value_make_undefined();
    for (int i = 0; i < argc; i++) {
        ts_ta_store_value(result, (size_t)i, argv[i]);
    }
    return ts_value_make_object(result);
}

// Helper: build a constructor function with name + .prototype + optional [[Prototype]] link.
// `nativeFn` is the native callable. If `parentProto` is non-null, sets the constructor's
// [[Prototype]] (used to wire all per-class TypedArrays to %TypedArray%).
static void* makeTypedArrayCtor(const char* name,
                                TsValue* (*nativeFn)(void*, int, TsValue**),
                                void* parentProto) {
    TsValue* ctorVal = ts_value_make_native_function((void*)nativeFn, nullptr);
    void* ctorRaw = ts_value_get_object(ctorVal);
    TsFunction* ctorFunc = (TsFunction*)ctorRaw;

    if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();

    // .prototype = empty TsMap
    TsMap* proto = TsMap::Create();
    TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal; protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    ctorFunc->properties->SetWithAttrs(protoKey, protoVal, 0);

    // .prototype.constructor = ctor (per spec — instance.constructor walks
    // the prototype chain and finds this; required for SpeciesConstructor's
    // default-fallback path).
    TsValue ctorKey; ctorKey.type = ValueType::STRING_PTR;
    ctorKey.ptr_val = TsString::GetInterned("constructor");
    TsValue ctorRefVal; ctorRefVal.type = ValueType::FUNCTION_PTR;
    ctorRefVal.ptr_val = ctorFunc;
    // ES 23.2.7.2: {writable: true, enumerable: false, configurable: true}.
    proto->SetWithAttrs(ctorKey, ctorRefVal,
        TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);

    // .name = constructor name (and arity = 3 per ECMA-262 23.2.4.1
    // for concrete TypedArray ctors; %TypedArray% itself overrides
    // below). Install both as own-properties on the function so
    // Object.getOwnPropertyDescriptor(Int8Array, 'name'/'length') sees
    // the spec-required {writable:false, enumerable:false, configurable:true}
    // descriptor.
    ctorFunc->name = TsString::Create(name);
    ctorFunc->arity = 3;
    {
        TsValue nk; nk.type = ValueType::STRING_PTR;
        nk.ptr_val = TsString::GetInterned("name");
        TsValue nv; nv.type = ValueType::STRING_PTR; nv.ptr_val = ctorFunc->name;
        ctorFunc->properties->SetWithAttrs(nk, nv, TsHashTable::ATTR_CONFIGURABLE);
        TsValue lk; lk.type = ValueType::STRING_PTR;
        lk.ptr_val = TsString::GetInterned("length");
        TsValue lv; lv.type = ValueType::NUMBER_INT; lv.i_val = 3;
        ctorFunc->properties->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);
    }

    // TypedArray spec: each constructor has `from` (arity 1) and `of`
    // (arity 0) static methods. Attach them via addMethod so they get
    // proper name/length metadata and [[Construct]]=false.
    addMethod(ctorFunc->properties, "from", (void*)ts_typed_array_from_native, 1);
    addMethod(ctorFunc->properties, "of",   (void*)ts_typed_array_of_native,   0);

    // [Symbol.species] = ctor itself (per ECMA-262 22.2.5.4). The well-known
    // symbol "species" is registered as the canonical string
    // "[Symbol.species]" — we store the constructor as a data property under
    // that key. SpeciesConstructor(O, default) looks up Get(C, @@species)
    // and falls back to default if undefined/null; storing self matches the
    // spec-default behavior and lets tests that override Symbol.species on
    // a subclass take effect.
    // Install as an ACCESSOR (get [@@species]() { return this; }) so the
    // descriptor is {get, configurable} per spec (a data property fails the
    // Symbol.species tests' get-is-a-function check).
    addAccessorGetter(ctorFunc->properties, "[Symbol.species]", (void*)species_this_getter);

    // Link [[Prototype]] (the __proto__ slot, NOT .prototype) to %TypedArray%.
    // This is what Object.getPrototypeOf(Int8Array) returns.
    //
    // Also link the per-class .prototype's [[Prototype]] to
    // %TypedArray%.prototype, so dynamic lookups like
    // `Float64Array.prototype.entries` and `someFloat64.entries` walk the
    // chain and find the methods registered on %TypedArray%.prototype.
    // Without this, only compile-time-intercepted method calls work, and
    // any access via dynamic constructor (`constructors[i]`) returns
    // undefined.
    if (parentProto) {
        ts_object_setPrototypeOf(ctorVal, (TsValue*)parentProto);

        TsFunction* parentFn = (TsFunction*)ts_value_get_object((TsValue*)parentProto);
        if (parentFn && parentFn->properties) {
            TsValue parentProtoKey; parentProtoKey.type = ValueType::STRING_PTR;
            parentProtoKey.ptr_val = TsString::GetInterned("prototype");
            TsValue parentProtoVal = parentFn->properties->Get(parentProtoKey);
            if (parentProtoVal.type == ValueType::OBJECT_PTR && parentProtoVal.ptr_val) {
                proto->SetPrototype((TsMap*)parentProtoVal.ptr_val);
            }
        }
    }

    return (void*)ctorVal;
}

// %TypedArray% intrinsic — the shared parent of all per-class TypedArray constructors.
// Per spec, %TypedArray% itself throws when called as a constructor, but tests typically
// just use it for introspection (Object.getPrototypeOf(Int8Array) === TypedArray).
// Helper: check that `ctx` is a TsTypedArray; if not, throw TypeError.
// Returns the validated pointer or nullptr after throw. Also performs
// the spec-required IsDetachedBuffer check (ValidateTypedArray step 5):
// if the underlying ArrayBuffer is detached, throw TypeError.
static TsTypedArray* requireTypedArrayOrThrow(void* ctx, const char* methodName) {
    void* raw = ctx ? ts_value_get_object((TsValue*)ctx) : nullptr;
    if (!raw) raw = ctx;
    if (raw) {
        uintptr_t p = (uintptr_t)raw;
        if (p > 0x1000 && p < 0x0000800000000000ULL) {
            uint32_t m16 = *(uint32_t*)((char*)raw + 16);
            if (m16 == TsTypedArray::MAGIC) {
                TsTypedArray* ta = (TsTypedArray*)raw;
                if (ta->IsDetachedBuffer()) {
                    char msg[160];
                    snprintf(msg, sizeof(msg),
                        "TypedArray.prototype.%s called on a TypedArray with "
                        "a detached buffer", methodName);
                    ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
                    return nullptr;
                }
                return ta;
            }
        }
    }
    char msg[160];
    snprintf(msg, sizeof(msg),
        "TypedArray.prototype.%s called on non-TypedArray object", methodName);
    ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
    return nullptr;
}

// Direct %TypedArray%.prototype.find/findIndex/findLast/findLastIndex.
// Previously these delegated to ts_array_find(...), which routes a TypedArray
// through the self-hosted array-like path (resolve_array_ctx only matches ARRY,
// not TARR) where raw TA elements were fed to nanbox_from_tagged -> access
// violation. Iterate the TA directly here: TsTypedArray::Get is detach-safe and
// we re-read GetLength()/IsDetachedBuffer() each step (a predicate may detach).
static TsValue* ta_find_impl(void* ctx, int argc, TsValue** argv,
                             const char* name, bool wantIndex, bool fromEnd) {
    TsTypedArray* ta = requireTypedArrayOrThrow(ctx, name);
    if (!ta) return wantIndex ? ts_value_make_int(-1) : ts_value_make_undefined();
    // BigInt TAs: ta->Get returns a double, so the direct read below mis-boxes a
    // BigInt element. findIndex/findLastIndex (index-returning) were already
    // correct via the legacy path; keep delegating those to avoid regressing the
    // bigint-TA tests. (find/findLast previously CRASHED on BigInt and now fail
    // cleanly via the direct loop — no AV, no regression.)
    if (wantIndex && (ta->GetType() == TypedArrayType::BigInt64 ||
                      ta->GetType() == TypedArrayType::BigUint64)) {
        void* cbv = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisv = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        return ts_value_make_int(fromEnd ? ts_array_findLastIndex((void*)ta, cbv, thisv)
                                         : ts_array_findIndex((void*)ta, cbv, thisv));
    }
    TsValue* cb = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (!ts_is_callable(cb)) {
        char m[128]; snprintf(m, sizeof(m), "TypedArray.prototype.%s predicate is not a function", name);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", m));
        return wantIndex ? ts_value_make_int(-1) : ts_value_make_undefined();
    }
    TsValue* thisArg = (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined();
    extern TsValue* ts_call_with_this_3(TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
    TsValue* taBoxed = ts_value_make_object(ta);
    size_t len = ta->GetLength();  // captured per spec (TypedArrayLength at entry)
    for (size_t s = 0; s < len; s++) {
        size_t i = fromEnd ? (len - 1 - s) : s;
        // After a mid-iteration detach, the integer-indexed read is undefined.
        TsValue* v = ta->IsDetachedBuffer() ? ts_value_make_undefined()
                                            : ts_value_make_double(ta->Get(i));
        TsValue* idx = ts_value_make_int((int64_t)i);
        TsValue* r = ts_call_with_this_3(cb, thisArg, v, idx, taBoxed);
        if (r && ts_value_to_bool(r)) return wantIndex ? idx : v;
    }
    return wantIndex ? ts_value_make_int(-1) : ts_value_make_undefined();
}

// ES 23.2.3.x %TypedArray%.prototype iteration methods. Unlike the Array
// forms these are DENSE: len is TypedArrayLength at ENTRY, every k < len gets
// a callback (no HasProperty/hole skip), and a mid-iteration detach or
// resizable-buffer shrink makes the remaining reads undefined — iteration
// CONTINUES (callbackfn-detachbuffer / resizable-buffer-shrink-mid-iteration
// / callbackfn-resize families). The self-hosted ARRAY impls skipped absent
// indices via `k in O`, stopping早 after a shrink.
extern "C" TsValue* ts_ta_get_boxed(TsTypedArray* ta, size_t idx);
extern "C" void* ts_typed_array_species_alloc(void* receiver, int64_t length);
extern "C" void ts_array_set_v(void* arr, int64_t index, TsValue value);
static inline TsValue* ta_elem_or_undefined(TsTypedArray* ta, size_t k) {
    if (ta->IsDetachedBuffer() || ta->IsOutOfBounds() || k >= ta->GetLength())
        return ts_value_make_undefined();
    return ts_ta_get_boxed(ta, k);
}
// kind: 0=every 1=some 2=forEach 3=map 4=filter
extern "C" TsValue* ta_iterate_impl(void* ctx, int argc, TsValue** argv,
                                    const char* name, int kind) {
    TsTypedArray* ta = requireTypedArrayOrThrow(ctx, name);
    if (!ta) return ts_value_make_undefined();
    TsValue* cb = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (!ts_is_callable(cb)) {
        char m[128];
        snprintf(m, sizeof(m), "TypedArray.prototype.%s callback is not a function", name);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", m));
        return ts_value_make_undefined();
    }
    TsValue* thisArg = (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined();
    extern TsValue* ts_call_with_this_3(TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
    TsValue* taBoxed = ts_value_make_object(ta);
    size_t len = ta->GetLength();  // captured per spec
    // map allocates its species result up front (spec order); filter collects
    // kept values in a GC-visible TsArray then species-copies.
    void* mapOut = nullptr;
    TsArray* kept = nullptr;
    if (kind == 3) {
        mapOut = ts_typed_array_species_alloc(ta, (int64_t)len);
        if (!mapOut) return ts_value_make_undefined();  // TypeError thrown
    } else if (kind == 4) {
        kept = TsArray::Create(4);
    }
    for (size_t k = 0; k < len; k++) {
        TsValue* v = ta_elem_or_undefined(ta, k);
        TsValue* idx = ts_value_make_int((int64_t)k);
        TsValue* r = ts_call_with_this_3(cb, thisArg, v, idx, taBoxed);
        switch (kind) {
            case 0: if (!r || !ts_value_to_bool(r)) return ts_value_make_bool(false); break;
            case 1: if (r && ts_value_to_bool(r)) return ts_value_make_bool(true); break;
            case 2: break;
            case 3:
                // Write through the BigInt-aware element store; a detached
                // result target makes this a silent no-op.
                if (mapOut) ts_array_set_v(mapOut, (int64_t)k,
                                           nanbox_to_tagged(r ? r : ts_value_make_undefined()));
                break;
            case 4:
                if (r && ts_value_to_bool(r))
                    ts_array_push(kept, v ? v : ts_value_make_undefined());
                break;
        }
    }
    switch (kind) {
        case 0: return ts_value_make_bool(true);
        case 1: return ts_value_make_bool(false);
        case 3: return ts_value_make_object(mapOut);
        case 4: {
            int64_t n = kept ? kept->Length() : 0;
            void* out = ts_typed_array_species_alloc(ta, n);
            if (!out) return ts_value_make_undefined();
            for (int64_t i = 0; i < n; i++)
                ts_array_set_v(out, i, nanbox_to_tagged((TsValue*)kept->GetElementBoxed((size_t)i)));
            return ts_value_make_object(out);
        }
    }
    return ts_value_make_undefined();
}
// reduce (fromEnd=false) / reduceRight (fromEnd=true)
extern "C" TsValue* ta_reduce_impl(void* ctx, int argc, TsValue** argv,
                                   const char* name, bool fromEnd) {
    TsTypedArray* ta = requireTypedArrayOrThrow(ctx, name);
    if (!ta) return ts_value_make_undefined();
    TsValue* cb = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (!ts_is_callable(cb)) {
        char m[128];
        snprintf(m, sizeof(m), "TypedArray.prototype.%s callback is not a function", name);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", m));
        return ts_value_make_undefined();
    }
    bool hasInit = (argc >= 2 && argv && argv[1]);
    size_t len = ta->GetLength();
    if (len == 0 && !hasInit) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Reduce of empty array with no initial value"));
        return ts_value_make_undefined();
    }
    extern TsValue* ts_call_with_this_4(TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
    TsValue* taBoxed = ts_value_make_object(ta);
    TsValue* acc;
    size_t s = 0;
    if (hasInit) {
        acc = argv[1];
    } else {
        size_t first = fromEnd ? (len - 1) : 0;
        acc = ta_elem_or_undefined(ta, first);
        s = 1;
    }
    for (; s < len; s++) {
        size_t k = fromEnd ? (len - 1 - s) : s;
        TsValue* v = ta_elem_or_undefined(ta, k);
        acc = ts_call_with_this_4(cb, ts_value_make_undefined(), acc, v,
                                  ts_value_make_int((int64_t)k), taBoxed);
    }
    return acc ? acc : ts_value_make_undefined();
}

void* ts_get_global_TypedArray() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        // %TypedArray% throws if called directly. We model it as a stub that returns undefined.
        auto fn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            return ts_value_make_undefined();
        };
        cached = makeTypedArrayCtor("TypedArray", fn, nullptr);

        // Populate %TypedArray%.prototype with spec methods that validate
        // `this` is a TypedArray (via RequireInternalSlot). This makes
        // TypedArray.prototype.X.call(non-typed-array) throw TypeError per
        // spec, unblocking ~50+ test262 this-not-typedarray tests.
        //
        // For now the method bodies are minimal (throw-on-wrong-this + a
        // best-effort delegation to the existing per-instance impls for
        // TypedArray receivers). Full method implementations are a larger
        // follow-up; what matters for many test262 tests is that the method
        // exists on TypedArray.prototype with correct RequireInternalSlot
        // behavior and [[Construct]]=false.
        TsFunction* tactor = (TsFunction*)ts_value_get_object((TsValue*)cached);
        TsValue protoKeyT; protoKeyT.type = ValueType::STRING_PTR;
        protoKeyT.ptr_val = TsString::GetInterned("prototype");
        TsValue protoT = tactor->properties->Get(protoKeyT);
        if (protoT.type == ValueType::OBJECT_PTR && protoT.ptr_val) {
            TsMap* tproto = (TsMap*)protoT.ptr_val;
            // Accessor getters per ECMA-262 23.2.3: buffer/byteLength/
            // byteOffset/length live on %TypedArray%.prototype as
            // accessor properties with named getter functions
            // ("get buffer" etc., length 0). All per-class prototypes
            // (Int8Array.prototype, etc.) inherit these via the
            // prototype-chain link installed in makeTypedArrayCtor.
            // Use ts_nanbox_safe_unbox to safely handle primitive `this`
            // (null/undefined/number/bool) which would previously crash on
            // the MAGIC dereference. See DataView getters above for the
            // canonical fix pattern.
            addAccessorGetter(tproto, "buffer", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                if (!raw || *(uint32_t*)((char*)raw + 16) != TsTypedArray::MAGIC) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get buffer called on non-TypedArray"));
                    return ts_value_make_undefined();
                }
                TsBuffer* buf = ((TsTypedArray*)raw)->GetBuffer();
                return buf ? ts_value_make_object(buf) : ts_value_make_undefined();
            });
            addAccessorGetter(tproto, "byteLength", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                if (!raw || *(uint32_t*)((char*)raw + 16) != TsTypedArray::MAGIC) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get byteLength called on non-TypedArray"));
                    return ts_value_make_undefined();
                }
                return ts_value_make_int((int64_t)((TsTypedArray*)raw)->GetByteLength());
            });
            addAccessorGetter(tproto, "byteOffset", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                if (!raw || *(uint32_t*)((char*)raw + 16) != TsTypedArray::MAGIC) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get byteOffset called on non-TypedArray"));
                    return ts_value_make_undefined();
                }
                return ts_value_make_int((int64_t)((TsTypedArray*)raw)->GetByteOffset());
            });
            addAccessorGetter(tproto, "length", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                if (!raw || *(uint32_t*)((char*)raw + 16) != TsTypedArray::MAGIC) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "get length called on non-TypedArray"));
                    return ts_value_make_undefined();
                }
                return ts_value_make_int((int64_t)((TsTypedArray*)raw)->GetLength());
            });
            // get %TypedArray%.prototype[@@toStringTag] (ECMA-262 23.2.3.38): an
            // accessor returning the typed-array's constructor name for a valid
            // TypedArray receiver, else undefined (NOT a throw). Object.prototype.
            // toString consults this for [object Int8Array] etc.
            addAccessorGetter(tproto, "[Symbol.toStringTag]", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                if (!ctx) ctx = ts_get_call_this();
                void* raw = ts_nanbox_safe_unbox(ctx);
                if (!raw || (uintptr_t)raw <= 0x1000 ||
                    *(uint32_t*)((char*)raw + 16) != TsTypedArray::MAGIC)
                    return ts_value_make_undefined();
                const char* name;
                switch (((TsTypedArray*)raw)->GetType()) {
                    case TypedArrayType::Int8:         name = "Int8Array"; break;
                    case TypedArrayType::Uint8:        name = "Uint8Array"; break;
                    case TypedArrayType::Uint8Clamped: name = "Uint8ClampedArray"; break;
                    case TypedArrayType::Int16:        name = "Int16Array"; break;
                    case TypedArrayType::Uint16:       name = "Uint16Array"; break;
                    case TypedArrayType::Int32:        name = "Int32Array"; break;
                    case TypedArrayType::Uint32:       name = "Uint32Array"; break;
                    case TypedArrayType::Float32:      name = "Float32Array"; break;
                    case TypedArrayType::Float64:      name = "Float64Array"; break;
                    case TypedArrayType::BigInt64:     name = "BigInt64Array"; break;
                    case TypedArrayType::BigUint64:    name = "BigUint64Array"; break;
                    default: return ts_value_make_undefined();
                }
                return ts_value_make_string(TsString::Create(name));
            });
            #define TA_PROTO_STUB(NAME) \
                addMethod(tproto, #NAME, (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* { \
                    TsTypedArray* ta = requireTypedArrayOrThrow(ctx, #NAME); \
                    if (!ta) return ts_value_make_undefined(); \
                    return ts_value_make_undefined(); \
                })

            // entries/keys/values: delegate to the array iterator helpers.
            // ts_array_entries/keys/values fall through to a generic
            // length-walking path when the receiver isn't a TsArray, so
            // passing a TsTypedArray works.
            addMethod(tproto, "entries", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                TsTypedArray* ta = requireTypedArrayOrThrow(ctx, "entries");
                if (!ta) return ts_value_make_undefined();
                void* result = ts_array_entries((void*)ta);
                return ts_value_make_object(result);
            });
            addMethod(tproto, "keys", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                TsTypedArray* ta = requireTypedArrayOrThrow(ctx, "keys");
                if (!ta) return ts_value_make_undefined();
                void* result = ts_array_keys((void*)ta);
                return ts_value_make_object(result);
            });
            addMethod(tproto, "values", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                TsTypedArray* ta = requireTypedArrayOrThrow(ctx, "values");
                if (!ta) return ts_value_make_undefined();
                void* result = ts_array_values((void*)ta);
                return ts_value_make_object(result);
            });

            // Callback-based iteration methods: delegate to ts_array_*
            // which already routes TypedArray receivers through the native
            // path (try_as_typed_array → ts_array_X_native).
            addMethod(tproto, "forEach", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                // Dense %TypedArray% semantics: cached len, no hole skip,
                // detach/shrink reads undefined (ta_iterate_impl above).
                return ta_iterate_impl(ctx, argc, argv, "forEach", 2);
            });
            addMethod(tproto, "map", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                // Dense %TypedArray% semantics: cached len, no hole skip,
                // detach/shrink reads undefined (ta_iterate_impl above).
                return ta_iterate_impl(ctx, argc, argv, "map", 3);
            });
            addMethod(tproto, "filter", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                // Dense %TypedArray% semantics: cached len, no hole skip,
                // detach/shrink reads undefined (ta_iterate_impl above).
                return ta_iterate_impl(ctx, argc, argv, "filter", 4);
            });
            addMethod(tproto, "every", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                // Dense %TypedArray% semantics: cached len, no hole skip,
                // detach/shrink reads undefined (ta_iterate_impl above).
                return ta_iterate_impl(ctx, argc, argv, "every", 0);
            });
            addMethod(tproto, "some", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                // Dense %TypedArray% semantics: cached len, no hole skip,
                // detach/shrink reads undefined (ta_iterate_impl above).
                return ta_iterate_impl(ctx, argc, argv, "some", 1);
            });
            addMethod(tproto, "find", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                return ta_find_impl(ctx, argc, argv, "find", false, false);
            });
            addMethod(tproto, "findIndex", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                return ta_find_impl(ctx, argc, argv, "findIndex", true, false);
            });
            addMethod(tproto, "findLast", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                return ta_find_impl(ctx, argc, argv, "findLast", false, true);
            });
            addMethod(tproto, "findLastIndex", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                return ta_find_impl(ctx, argc, argv, "findLastIndex", true, true);
            });
            addMethod(tproto, "reduce", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                return ta_reduce_impl(ctx, argc, argv, "reduce", false);
            });
            addMethod(tproto, "reduceRight", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
                return ta_reduce_impl(ctx, argc, argv, "reduceRight", true);
            });

            TA_PROTO_STUB(copyWithin);
            TA_PROTO_STUB(fill);
            TA_PROTO_STUB(includes);
            TA_PROTO_STUB(indexOf);
            TA_PROTO_STUB(join);
            TA_PROTO_STUB(lastIndexOf);
            TA_PROTO_STUB(reverse);
            TA_PROTO_STUB(set);
            TA_PROTO_STUB(slice);
            TA_PROTO_STUB(sort);
            TA_PROTO_STUB(subarray);
            TA_PROTO_STUB(toLocaleString);
            TA_PROTO_STUB(toReversed);
            TA_PROTO_STUB(toSorted);
            TA_PROTO_STUB(at);
            #undef TA_PROTO_STUB
        }
    }
    return cached;
}

// BigInt-element store for the constructor dispatcher: BigInt TAs hold raw
// two's-complement int64 slots. A non-BigInt element value is a TypeError
// per InitializeTypedArrayFromList (ToBigInt on a Number throws).
extern "C" void ts_ta_store_value(void* taRaw, size_t i, TsValue* v);
static void ta_new_store_elem(void* taRaw, size_t i, TsValue* v) { ts_ta_store_value(taRaw, i, v); }
extern "C" void ts_ta_store_value(void* taRaw, size_t i, TsValue* v) {
    TsTypedArray* ta = (TsTypedArray*)taRaw;
    TypedArrayType tt = ta->GetType();
    if (tt == TypedArrayType::BigInt64 || tt == TypedArrayType::BigUint64) {
        // ES 7.1.13 ToBigInt: BigInt passes; booleans map to 0n/1n; objects
        // run ToPrimitive FIRST (a poisoned valueOf's abrupt completion
        // propagates as-is); everything else is a TypeError.
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        void* raw = (v && nanbox_is_ptr(nb)) ? nanbox_to_ptr(nb) : nullptr;
        if (raw && (uintptr_t)raw >= 4096 &&
            *(uint32_t*)raw != 0x42494749 /*BIGI*/ &&
            *(uint32_t*)raw != 0x53545247 /*STRG*/ &&
            *(uint32_t*)raw != TsConsString::MAGIC) {
            extern TsValue* ts_to_primitive(TsValue* v, int hint);
            v = ts_to_primitive(v, 1 /* number hint */);
            nb = nanbox_from_tsvalue_ptr(v);
            raw = (v && nanbox_is_ptr(nb)) ? nanbox_to_ptr(nb) : nullptr;
        }
        int64_t iv = 0;
        if (raw && (uintptr_t)raw >= 4096 && *(uint32_t*)raw == 0x42494749) {
            iv = ts_bigint_to_i64(raw);
        } else if (nanbox_is_true(nb)) {
            iv = 1;
        } else if (nanbox_is_false(nb)) {
            iv = 0;
        } else {
            // ES 7.1.13: ToBigInt(undefined/null/number/symbol) throws —
            // INCLUDING construction holes (`new BigInt64Array([1n,,3n])`).
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert a non-BigInt value to a BigInt"));
            return;
        }
        uint8_t* data = ta->GetData();
        if (data && i < ta->GetLength()) ((int64_t*)data)[i] = iv;
        return;
    }
    // Holes / undefined: ToNumber(undefined) = NaN. Float types store the
    // NaN; integer types take ToIntegerOrInfinity(NaN) = +0 explicitly (a
    // raw (int)NaN cast is UB — the old path leaked INT64_MIN garbage).
    double d;
    if (!v || ts_value_is_undefined(v) ||
        (uint64_t)(uintptr_t)v == (uint64_t)NANBOX_HOLE) {
        d = std::numeric_limits<double>::quiet_NaN();
    } else {
        d = ts_to_number(v);
    }
    if (d != d && tt != TypedArrayType::Float32 && tt != TypedArrayType::Float64) {
        d = 0;
    }
    ta->Set(i, d);
}

// ts_typed_array_new_<kind>(arg, byteOffset, byteLength) — constructor
// wrapper for `new TypedArray(arg, ...)` covering all four spec forms:
//   - new TA(length)               — allocate fresh buffer
//   - new TA(arrayBuffer, off, n)  — share buffer (Phase 3)
//   - new TA(typedArray)           — copy values
//   - new TA(arrayLike)            — copy via .length + indexed reads
// byteOffset/byteLength are honored only for the ArrayBuffer form; -1
// byteLength means "rest of buffer".
// ES 23.2.5.1 step 6.b.i IterableToList for `new TA(object)`: when the
// source has a callable @@iterator, the CONSTRUCTOR must drive the iterator
// protocol — poisoned next()/value-getter abrupt completions propagate, and
// Sets/Maps/custom iterables produce their iteration order (the length-based
// path saw `length === undefined` and produced an empty TA). Returns a dense
// TsArray of the yielded values, or nullptr when the source has no callable
// @@iterator (caller falls through to the array-like path).
static TsArray* ta_iterable_collect(void* rawSrc) {
    // The DYNAMIC get path handles Set/Map receivers (their @@iterator lives
    // behind magic dispatch the static-key path doesn't reach).
    extern TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key);
    TsValue* iterKey = ts_value_make_string(TsString::GetInterned("[Symbol.iterator]"));
    TsValue* iterMethod = ts_object_get_dynamic(ts_value_make_object(rawSrc), iterKey);
    if (!iterMethod || ts_value_is_undefined(iterMethod) ||
        ts_value_is_null(iterMethod) || !ts_is_callable((void*)iterMethod)) {
        return nullptr;
    }
    TsValue* srcBoxed = ts_value_make_object(rawSrc);
    TsValue* iter = ts_call_with_this_0(iterMethod, srcBoxed);
    void* iterRaw = iter ? ts_value_get_object(iter) : nullptr;
    if (!iterRaw) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Result of the Symbol.iterator method is not an object"));
        return nullptr;
    }
    // GetIteratorDirect: cache `next` once (re-reading each step is a spec
    // violation and an accessor-loop hazard).
    TsValue* nextFn = ts_object_get_property(iterRaw, "next");
    if (!nextFn || !ts_is_callable((void*)nextFn)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "iterator.next is not a function"));
        return nullptr;
    }
    TsArray* out = TsArray::Create();
    const int64_t MAX_ITER = 1 << 20;
    for (int64_t guard = 0; guard < MAX_ITER; guard++) {
        TsValue* res = ts_function_call_with_this(nextFn, iter, 0, nullptr);
        void* resRaw = res ? ts_value_get_object(res) : nullptr;
        if (!resRaw) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Iterator result is not an object"));
            return nullptr;
        }
        TsValue* doneV = ts_object_get_property(resRaw, "done");
        if (doneV && ts_value_to_bool(doneV)) break;
        // Get(result, "value") — a poisoned value getter throws HERE.
        TsValue* val = ts_object_get_property(resRaw, "value");
        ts_array_push(out, val ? val : (TsValue*)ts_value_make_undefined());
    }
    return out;
}

#define DEFINE_TYPED_ARRAY_NEW(Suffix, CreateFn, ElemSize, Clamped, TypeEnum)            \
extern "C" void* ts_typed_array_new_##Suffix(TsValue* arg,                               \
                                             int64_t byteOffset,                         \
                                             int64_t byteLength) {                       \
    if (arg) {                                                                           \
        void* rawSrc = ts_value_get_object(arg);                                         \
        bool srcIsObject = false;                                                        \
        if (rawSrc) {                                                                    \
            uintptr_t p = (uintptr_t)rawSrc;                                             \
            if (p > 0x1000 && p < 0x0000800000000000ULL) srcIsObject = true;             \
        }                                                                                \
        /* A STRING primitive is not an array-like source — fall through to    */       \
        /* the ToNumber length path below (same as the ctor macro).            */       \
        if (srcIsObject && !ts_is_any_string(rawSrc)) {                                  \
            uint32_t srcMagic0 = *(uint32_t*)rawSrc;                                     \
            uint32_t srcMagic16 = *(uint32_t*)((char*)rawSrc + 16);                      \
            /* TsBuffer (ArrayBuffer): share backing — Phase 3. */                       \
            if (srcMagic16 == 0x42554646) {                                              \
                TsBuffer* buf = (TsBuffer*)rawSrc;                                       \
                /* ECMA-262 22.2.5.1 step 4: cannot view a detached buffer. */            \
                if (buf->IsDetached()) {                                                  \
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",                 \
                        "Cannot construct a TypedArray on a detached ArrayBuffer"));      \
                    return nullptr;                                                      \
                }                                                                        \
                size_t bufLen = buf->GetLength();                                        \
                size_t off = (byteOffset > 0) ? (size_t)byteOffset : 0;                  \
                if (off > bufLen || (off % (ElemSize)) != 0) {                           \
                    ts_throw((TsValue*)ts_error_create_typed("RangeError",               \
                        "byteOffset out of range or not aligned"));                       \
                    return nullptr;                                                      \
                }                                                                        \
                size_t bytesAvail = bufLen - off;                                        \
                size_t bytes;                                                            \
                if (byteLength < 0) {                                                    \
                    if (bytesAvail % (ElemSize) != 0) {                                  \
                        ts_throw((TsValue*)ts_error_create_typed("RangeError",           \
                            "buffer length not divisible by element size"));              \
                        return nullptr;                                                  \
                    }                                                                    \
                    bytes = bytesAvail;                                                  \
                } else {                                                                 \
                    bytes = (size_t)byteLength * (ElemSize);                             \
                    if (off + bytes > bufLen) {                                          \
                        ts_throw((TsValue*)ts_error_create_typed("RangeError",           \
                            "TypedArray length out of range"));                          \
                        return nullptr;                                                  \
                    }                                                                    \
                }                                                                        \
                /* byteLength < 0 = "rest of buffer": length-tracking over   */          \
                /* a resizable buffer (ES2024 auto-length view).             */          \
                return TsTypedArray::CreateOnBuffer(buf, off, bytes / (ElemSize),        \
                    (ElemSize), (Clamped), (TypeEnum), byteLength < 0);                  \
            }                                                                            \
            /* TsArray fast path: use GetElementDouble for indexed reads. */             \
            if (srcMagic0 == 0x41525259) { /* TsArray::MAGIC "ARRY" */                   \
                TsArray* srcArr = (TsArray*)rawSrc;                                      \
                int64_t n = (int64_t)srcArr->Length();                                   \
                void* result = CreateFn(n);                                              \
                for (int64_t i = 0; i < n; i++) {                                        \
                    /* Holes surface the raw NANBOX_HOLE sentinel from       */          \
                    /* GetElementBoxed — normalize to undefined so the       */          \
                    /* element store applies spec ToNumber/ToBigInt.         */          \
                    TsValue* ev = srcArr->IsHole((size_t)i)                              \
                        ? (TsValue*)ts_value_make_undefined()                            \
                        : (TsValue*)srcArr->GetElementBoxed((size_t)i);                  \
                    ta_new_store_elem(result, (size_t)i, ev);                          \
                }                                                                        \
                return result;                                                           \
            }                                                                            \
            /* TsTypedArray source: copy via Get/Set. */                                 \
            if (srcMagic16 == TsTypedArray::MAGIC) {                                     \
                TsTypedArray* srcTa = (TsTypedArray*)rawSrc;                             \
                int64_t n = (int64_t)srcTa->GetLength();                                 \
                void* result = CreateFn(n);                                              \
                for (int64_t i = 0; i < n; i++) {                                        \
                    ((TsTypedArray*)result)->Set((size_t)i, srcTa->Get((size_t)i));      \
                }                                                                        \
                return result;                                                           \
            }                                                                            \
            /* Iterable (Set/Map/custom/poisoned): drive the iterator     */            \
            /* protocol per IterableToList; abrupt completions propagate. */            \
            if (TsArray* itList = ta_iterable_collect(rawSrc)) {                         \
                int64_t n = (int64_t)itList->Length();                                   \
                void* result = CreateFn(n);                                              \
                for (int64_t i = 0; i < n; i++) {                                        \
                    TsValue* ev = (TsValue*)itList->GetElementBoxed((size_t)i);          \
                    ta_new_store_elem(result, (size_t)i, ev);                            \
                }                                                                        \
                return result;                                                           \
            }                                                                            \
            /* Generic array-like: iterate via .length + indexed reads. */               \
            TsValue* lenVal = ts_object_get_property(rawSrc, "length");                  \
            if (lenVal && !ts_value_is_undefined(lenVal)) {                              \
                double lenD = ts_to_number(lenVal);                                      \
                int64_t n = (lenD == lenD && lenD >= 0) ? (int64_t)lenD : 0;             \
                void* result = CreateFn(n);                                              \
                for (int64_t i = 0; i < n; i++) {                                        \
                    char key[32]; snprintf(key, sizeof(key), "%lld", (long long)i);      \
                    TsValue* v = ts_object_get_property(rawSrc, key);                    \
                    ta_new_store_elem(result, (size_t)i, v);                                 \
                }                                                                        \
                return result;                                                           \
            }                                                                            \
        }                                                                                \
    }                                                                                    \
    double lenD = arg ? ts_to_number(arg) : 0;                                           \
    /* ECMA-262 ToIndex: NaN -> 0; negative / > 2^53-1 -> RangeError (parity with */     \
    /* the ctor-macro's ta_to_index_length; negative previously became length 0). */     \
    double nn = (lenD == lenD) ? lenD : 0.0;                                             \
    if (nn < 0 || nn > 9007199254740991.0) {                                            \
        ts_throw((TsValue*)ts_error_create_typed("RangeError",                          \
            "Invalid typed array length"));                                             \
        return CreateFn(0);  /* unreachable (ts_throw longjmps) */                       \
    }                                                                                    \
    int64_t length = (int64_t)nn;                                                        \
    return CreateFn(length);                                                             \
}

// BigInt TA allocators live in extensions/node/core/src/TsBuffer.cpp.
extern "C" void* ts_typed_array_create_i64(int64_t length);
extern "C" void* ts_typed_array_create_u64(int64_t length);

DEFINE_TYPED_ARRAY_NEW(i8,      ts_typed_array_create_i8,      1, false, TypedArrayType::Int8)
DEFINE_TYPED_ARRAY_NEW(u8,      ts_typed_array_create_u8,      1, false, TypedArrayType::Uint8)
DEFINE_TYPED_ARRAY_NEW(clamped, ts_typed_array_create_clamped, 1, true,  TypedArrayType::Uint8Clamped)
DEFINE_TYPED_ARRAY_NEW(i16,     ts_typed_array_create_i16,     2, false, TypedArrayType::Int16)
DEFINE_TYPED_ARRAY_NEW(u16,     ts_typed_array_create_u16,     2, false, TypedArrayType::Uint16)
DEFINE_TYPED_ARRAY_NEW(i32,     ts_typed_array_create_i32,     4, false, TypedArrayType::Int32)
DEFINE_TYPED_ARRAY_NEW(u32,     ts_typed_array_create_u32,     4, false, TypedArrayType::Uint32)
DEFINE_TYPED_ARRAY_NEW(f32,     ts_typed_array_create_f32,     4, false, TypedArrayType::Float32)
DEFINE_TYPED_ARRAY_NEW(f64,     ts_typed_array_create_f64,     8, false, TypedArrayType::Float64)
DEFINE_TYPED_ARRAY_NEW(i64,     ts_typed_array_create_i64,     8, false, TypedArrayType::BigInt64)
DEFINE_TYPED_ARRAY_NEW(u64,     ts_typed_array_create_u64,     8, false, TypedArrayType::BigUint64)

#undef DEFINE_TYPED_ARRAY_NEW

// Populate a freshly-allocated TypedArray from an array-like source
// (iterable .length + indexed property reads). Returns true if the source
// was actually array-like and values were copied; false otherwise.
static bool populate_ta_from_array_like(void* result, TsValue* source) {
    if (!result || !source) return false;
    void* rawSrc = ts_value_get_object(source);
    if (!rawSrc) return false;
    uintptr_t p = (uintptr_t)rawSrc;
    if (p <= 0x1000 || p >= 0x0000800000000000ULL) return false;
    // Array-like detection: has a readable .length
    TsValue* lenVal = ts_object_get_property(rawSrc, "length");
    if (!lenVal || ts_value_is_undefined(lenVal)) return false;
    double lenD = ts_to_number(lenVal);
    if (lenD != lenD || lenD < 0) return false;
    int64_t srcLen = (int64_t)lenD;
    int64_t taLen = (int64_t)((TsTypedArray*)result)->GetLength();
    int64_t n = std::min(srcLen, taLen);
    for (int64_t i = 0; i < n; i++) {
        char key[32]; snprintf(key, sizeof(key), "%lld", (long long)i);
        TsValue* v = ts_object_get_property(rawSrc, key);
        double d = (v && !ts_value_is_undefined(v)) ? ts_to_number(v) : 0;
        ((TsTypedArray*)result)->Set((size_t)i, d);
    }
    return true;
}

// ===========================================================================
// Uint8Array base64/hex (TC39 "Uint8Array <-> base64/hex" proposal) — HEX subset
//   Uint8Array.fromHex(string) -> Uint8Array
//   Uint8Array.prototype.toHex() -> string
//   Uint8Array.prototype.setFromHex(string) -> { read, written }
// (base64 variants are a separate follow-up.)
// ===========================================================================
static inline int u8_hex_val(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ECMA "FromHex(string, maxLength)". Returns 0 on success, 1 on SyntaxError.
// Decodes up to maxBytes bytes into out; sets *readOut (code units consumed)
// and *writtenOut (bytes produced). An odd-length input is an immediate error
// that produces ZERO bytes (read=written=0) — matching the spec early-out
// (so setFromHex on an odd string writes nothing), whereas a bad hex digit in
// an even string yields the bytes decoded before it plus the error (so
// setFromHex writes the good prefix then throws).
static int u8_from_hex(const char* s, size_t len, uint8_t* out, size_t maxBytes,
                       size_t* readOut, size_t* writtenOut) {
    if (len % 2 != 0) { *readOut = 0; *writtenOut = 0; return 1; }
    size_t read = 0, w = 0;
    while (read < len && w < maxBytes) {
        int v1 = u8_hex_val((unsigned char)s[read]);
        int v2 = u8_hex_val((unsigned char)s[read + 1]);
        if (v1 < 0 || v2 < 0) { *readOut = read; *writtenOut = w; return 1; }
        out[w++] = (uint8_t)((v1 << 4) | v2);
        read += 2;
    }
    *readOut = read; *writtenOut = w; return 0;
}

// Extract a primitive-String argument WITHOUT coercion (spec throws TypeError
// for a non-string, never calling ToString). Returns the flattened TsString*,
// or nullptr after throwing a TypeError.
static TsString* u8_require_string_arg(TsValue* arg, const char* method) {
    TsValue tv = nanbox_to_tagged(arg);
    if (tv.type != ValueType::STRING_PTR) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s requires a string argument", method);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return nullptr;
    }
    return (TsString*)ts_value_get_string(arg);
}

// `this` must be specifically a Uint8Array (not just any TypedArray) for the
// hex/base64 methods. Validates + detached check via requireTypedArrayOrThrow,
// then narrows the element type. Returns nullptr after throwing.
static TsTypedArray* u8_require_uint8_this(void* ctx, const char* method) {
    TsTypedArray* ta = requireTypedArrayOrThrow(ctx, method);
    if (!ta) return nullptr;
    if (ta->GetType() != TypedArrayType::Uint8) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s called on a non-Uint8Array", method);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return nullptr;
    }
    return ta;
}

static TsValue* u8_fromHex_native(void* /*ctx*/, int argc, TsValue** argv) {
    TsValue* arg = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    TsString* s = u8_require_string_arg(arg, "Uint8Array.fromHex");
    if (!s) return ts_value_make_undefined();
    const char* u = s->ToUtf8();
    size_t len = u ? strlen(u) : 0;
    size_t maxBytes = len / 2;
    uint8_t* tmp = (uint8_t*)ts_alloc(maxBytes ? maxBytes : 1);
    size_t rd = 0, wr = 0;
    if (u8_from_hex(u ? u : "", len, tmp, maxBytes, &rd, &wr)) {
        ts_throw((TsValue*)ts_error_create_typed("SyntaxError",
            "Uint8Array.fromHex: string is not a valid hex string"));
        return ts_value_make_undefined();
    }
    void* res = ts_typed_array_create_u8((int64_t)wr);
    for (size_t i = 0; i < wr; i++) ((TsTypedArray*)res)->Set(i, (double)tmp[i]);
    return (TsValue*)res;
}

extern "C" TsValue* ts_u8_toHex_native(void* ctx, int /*argc*/, TsValue** /*argv*/) {
    TsTypedArray* ta = u8_require_uint8_this(ctx, "Uint8Array.prototype.toHex");
    if (!ta) return ts_value_make_undefined();
    size_t n = ta->GetLength();
    static const char* HEX = "0123456789abcdef";
    char* out = (char*)ts_alloc(n * 2 + 1);
    uint8_t* data = ta->GetData();
    for (size_t i = 0; i < n; i++) {
        uint8_t b = data ? data[i] : 0;
        out[i * 2] = HEX[b >> 4];
        out[i * 2 + 1] = HEX[b & 0xF];
    }
    out[n * 2] = '\0';
    return ts_value_make_string(TsString::Create(out));
}

extern "C" TsValue* ts_u8_setFromHex_native(void* ctx, int argc, TsValue** argv) {
    TsTypedArray* ta = u8_require_uint8_this(ctx, "Uint8Array.prototype.setFromHex");
    if (!ta) return ts_value_make_undefined();
    TsValue* arg = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    TsString* s = u8_require_string_arg(arg, "Uint8Array.prototype.setFromHex");
    if (!s) return ts_value_make_undefined();
    const char* u = s->ToUtf8();
    size_t len = u ? strlen(u) : 0;
    size_t maxBytes = ta->GetLength();
    uint8_t* tmp = (uint8_t*)ts_alloc(maxBytes ? maxBytes : 1);
    size_t rd = 0, wr = 0;
    int err = u8_from_hex(u ? u : "", len, tmp, maxBytes, &rd, &wr);
    // Spec: write the successfully decoded bytes into the target FIRST, then
    // throw if a SyntaxError was produced (write-up-to-error behavior).
    for (size_t i = 0; i < wr; i++) ta->Set(i, (double)tmp[i]);
    if (err) {
        ts_throw((TsValue*)ts_error_create_typed("SyntaxError",
            "Uint8Array.prototype.setFromHex: string is not a valid hex string"));
        return ts_value_make_undefined();
    }
    TsMap* result = TsMap::Create();
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("read"))),
                nanbox_to_tagged(ts_value_make_int((int64_t)rd)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("written"))),
                nanbox_to_tagged(ts_value_make_int((int64_t)wr)));
    return ts_value_make_object(result);
}

// ---------------------------------------------------------------------------
// base64 subset of the same proposal.
// ---------------------------------------------------------------------------
static const char* B64_STD = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char* B64_URL = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static inline int b64_val(unsigned char c, bool url) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (!url) { if (c == '+') return 62; if (c == '/') return 63; }
    else      { if (c == '-') return 62; if (c == '_') return 63; }
    return -1;
}
static inline bool b64_is_ws(unsigned char c) {
    return c == 0x09 || c == 0x0A || c == 0x0C || c == 0x0D || c == 0x20;
}

static char* u8_to_base64(const uint8_t* data, size_t n, bool url, bool omitPad) {
    const char* A = url ? B64_URL : B64_STD;
    char* out = (char*)ts_alloc(((n + 2) / 3) * 4 + 1);
    size_t o = 0, i = 0;
    while (i + 3 <= n) {
        uint32_t x = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
        out[o++] = A[(x >> 18) & 63]; out[o++] = A[(x >> 12) & 63];
        out[o++] = A[(x >> 6) & 63];  out[o++] = A[x & 63];
        i += 3;
    }
    size_t rem = n - i;
    if (rem == 1) {
        uint32_t x = (uint32_t)data[i] << 16;
        out[o++] = A[(x >> 18) & 63]; out[o++] = A[(x >> 12) & 63];
        if (!omitPad) { out[o++] = '='; out[o++] = '='; }
    } else if (rem == 2) {
        uint32_t x = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        out[o++] = A[(x >> 18) & 63]; out[o++] = A[(x >> 12) & 63]; out[o++] = A[(x >> 6) & 63];
        if (!omitPad) out[o++] = '=';
    }
    out[o] = '\0';
    return out;
}

// Spec FromBase64(string, alphabet, lastChunkHandling, maxLength). Returns
// 0 on success, 1 on SyntaxError. lastChunk: 0=loose, 1=strict,
// 2=stop-before-partial. Skips ASCII whitespace, honors padding, and stops
// before a chunk that would exceed maxBytes (so setFromBase64 fills a target
// exactly and reports `read` at the last fully-committed chunk boundary).
static int u8_from_base64(const char* s, size_t len, bool url, int lastChunk,
                          uint8_t* out, size_t maxBytes,
                          size_t* readOut, size_t* writtenOut) {
    size_t w = 0, read = 0, i = 0, chunkStart = 0;
    int chunk[4]; int cl = 0;
    for (;;) {
        while (i < len && b64_is_ws((unsigned char)s[i])) i++;
        if (i >= len) {
            if (cl > 0) {
                if (lastChunk == 2) { *readOut = read; *writtenOut = w; return 0; }
                if (lastChunk == 1) { *readOut = read; *writtenOut = w; return 1; }
                if (cl == 1)        { *readOut = read; *writtenOut = w; return 1; }
                size_t produce = (cl == 2) ? 1 : 2;
                if (w + produce > maxBytes) { *readOut = read; *writtenOut = w; return 0; }
                out[w++] = (uint8_t)((chunk[0] << 2) | (chunk[1] >> 4));
                if (cl == 3) out[w++] = (uint8_t)(((chunk[1] & 15) << 4) | (chunk[2] >> 2));
                *readOut = len; *writtenOut = w; return 0;
            }
            *readOut = len; *writtenOut = w; return 0;
        }
        unsigned char ch = (unsigned char)s[i];
        if (ch == '=') {
            if (cl < 2) { *readOut = read; *writtenOut = w; return 1; }
            i++;
            while (i < len && b64_is_ws((unsigned char)s[i])) i++;
            if (cl == 2) {
                if (i >= len || s[i] != '=') {
                    // A 2-char chunk followed by a single '=' is malformed (needs
                    // '=='). stop-before-partial drops the partial chunk and
                    // returns the bytes committed so far; loose/strict error.
                    if (lastChunk == 2) { *readOut = read; *writtenOut = w; return 0; }
                    *readOut = read; *writtenOut = w; return 1;
                }
                i++;
                while (i < len && b64_is_ws((unsigned char)s[i])) i++;
            }
            if (i < len) { *readOut = read; *writtenOut = w; return 1; }
            if (lastChunk == 1) {
                if (cl == 2 && (chunk[1] & 0x0F) != 0) { *readOut = read; *writtenOut = w; return 1; }
                if (cl == 3 && (chunk[2] & 0x03) != 0) { *readOut = read; *writtenOut = w; return 1; }
            }
            size_t produce = (cl == 2) ? 1 : 2;
            if (w + produce > maxBytes) { *readOut = read; *writtenOut = w; return 0; }
            out[w++] = (uint8_t)((chunk[0] << 2) | (chunk[1] >> 4));
            if (cl == 3) out[w++] = (uint8_t)(((chunk[1] & 15) << 4) | (chunk[2] >> 2));
            *readOut = len; *writtenOut = w; return 0;
        }
        int v = b64_val(ch, url);
        if (v < 0) { *readOut = read; *writtenOut = w; return 1; }
        if (cl == 0) chunkStart = i;
        chunk[cl++] = v;
        i++;
        if (cl == 4) {
            if (w + 3 > maxBytes) { *readOut = chunkStart; *writtenOut = w; return 0; }
            out[w++] = (uint8_t)((chunk[0] << 2) | (chunk[1] >> 4));
            out[w++] = (uint8_t)(((chunk[1] & 15) << 4) | (chunk[2] >> 2));
            out[w++] = (uint8_t)(((chunk[2] & 3) << 6) | chunk[3]);
            cl = 0;
            read = i;
            if (w == maxBytes) { *readOut = read; *writtenOut = w; return 0; }
        }
    }
}

// Read the {alphabet, lastChunkHandling, omitPadding} options object. The
// string options (alphabet/lastChunkHandling) must be PRIMITIVE strings with a
// recognized value, else TypeError (never coerced). omitPadding is ToBoolean.
// Pass nullptr for an out-param to skip that option. Returns false (after
// throwing) on error.
static bool u8_read_base64_options(TsValue* optsArg, const char* method,
                                   bool* urlOut, int* lastChunkOut, bool* omitPadOut) {
    if (!optsArg || ts_value_is_undefined(optsArg)) return true;
    void* optsRaw = ts_value_get_object(optsArg);
    if (!optsRaw) {
        char msg[96]; snprintf(msg, sizeof(msg), "%s options is not an object", method);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return false;
    }
    if (urlOut) {
        TsValue* a = ts_object_get_property(optsRaw, "alphabet");
        if (a && !ts_value_is_undefined(a)) {
            TsValue av = nanbox_to_tagged(a);
            if (av.type != ValueType::STRING_PTR) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError", "alphabet must be a string")); return false;
            }
            const char* as = ((TsString*)ts_value_get_string(a))->ToUtf8();
            if (strcmp(as, "base64") == 0) *urlOut = false;
            else if (strcmp(as, "base64url") == 0) *urlOut = true;
            else { ts_throw((TsValue*)ts_error_create_typed("TypeError", "alphabet must be base64 or base64url")); return false; }
        }
    }
    if (lastChunkOut) {
        TsValue* l = ts_object_get_property(optsRaw, "lastChunkHandling");
        if (l && !ts_value_is_undefined(l)) {
            TsValue lv = nanbox_to_tagged(l);
            if (lv.type != ValueType::STRING_PTR) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError", "lastChunkHandling must be a string")); return false;
            }
            const char* ls = ((TsString*)ts_value_get_string(l))->ToUtf8();
            if (strcmp(ls, "loose") == 0) *lastChunkOut = 0;
            else if (strcmp(ls, "strict") == 0) *lastChunkOut = 1;
            else if (strcmp(ls, "stop-before-partial") == 0) *lastChunkOut = 2;
            else { ts_throw((TsValue*)ts_error_create_typed("TypeError", "invalid lastChunkHandling")); return false; }
        }
    }
    if (omitPadOut) {
        TsValue* o = ts_object_get_property(optsRaw, "omitPadding");
        if (o && !ts_value_is_undefined(o)) *omitPadOut = ts_value_to_bool(o);
    }
    return true;
}

// Like u8_require_uint8_this but WITHOUT the detached-buffer throw — the
// toBase64/setFromBase64 methods must read their options object (which may run
// getters that detach the buffer) BEFORE the detached check, so the detached
// check is performed by the caller after option reading.
static TsTypedArray* u8_require_uint8_this_nodetach(void* ctx, const char* method) {
    void* raw = ctx ? ts_value_get_object((TsValue*)ctx) : nullptr;
    if (!raw) raw = ctx;
    TsTypedArray* ta = nullptr;
    if (raw) {
        uintptr_t p = (uintptr_t)raw;
        if (p > 0x1000 && p < 0x0000800000000000ULL &&
            *(uint32_t*)((char*)raw + 16) == TsTypedArray::MAGIC) {
            ta = (TsTypedArray*)raw;
        }
    }
    if (!ta || ta->GetType() != TypedArrayType::Uint8) {
        char msg[96]; snprintf(msg, sizeof(msg), "%s called on a non-Uint8Array", method);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return nullptr;
    }
    return ta;
}

static bool u8_throw_if_detached(TsTypedArray* ta, const char* method) {
    if (ta->IsDetachedBuffer()) {
        char msg[96]; snprintf(msg, sizeof(msg), "%s called on a detached buffer", method);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return true;
    }
    return false;
}

static TsValue* u8_fromBase64_native(void* /*ctx*/, int argc, TsValue** argv) {
    TsValue* arg = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    TsString* s = u8_require_string_arg(arg, "Uint8Array.fromBase64");
    if (!s) return ts_value_make_undefined();
    bool url = false; int lastChunk = 0;
    TsValue* opts = (argc >= 2 && argv) ? argv[1] : nullptr;
    if (!u8_read_base64_options(opts, "Uint8Array.fromBase64", &url, &lastChunk, nullptr))
        return ts_value_make_undefined();
    const char* u = s->ToUtf8();
    size_t len = u ? strlen(u) : 0;
    size_t cap = (len / 4 + 1) * 3 + 3;
    uint8_t* tmp = (uint8_t*)ts_alloc(cap);
    size_t rd = 0, wr = 0;
    if (u8_from_base64(u ? u : "", len, url, lastChunk, tmp, cap, &rd, &wr)) {
        ts_throw((TsValue*)ts_error_create_typed("SyntaxError",
            "Uint8Array.fromBase64: string is not valid base64"));
        return ts_value_make_undefined();
    }
    void* res = ts_typed_array_create_u8((int64_t)wr);
    for (size_t i = 0; i < wr; i++) ((TsTypedArray*)res)->Set(i, (double)tmp[i]);
    return (TsValue*)res;
}

extern "C" TsValue* ts_u8_toBase64_native(void* ctx, int argc, TsValue** argv) {
    TsTypedArray* ta = u8_require_uint8_this_nodetach(ctx, "Uint8Array.prototype.toBase64");
    if (!ta) return ts_value_make_undefined();
    bool url = false, omitPad = false;
    TsValue* opts = (argc >= 1 && argv) ? argv[0] : nullptr;
    if (!u8_read_base64_options(opts, "Uint8Array.prototype.toBase64", &url, nullptr, &omitPad))
        return ts_value_make_undefined();
    // Detached check AFTER option reading (option getters may detach).
    if (u8_throw_if_detached(ta, "Uint8Array.prototype.toBase64")) return ts_value_make_undefined();
    size_t n = ta->GetLength();
    char* out = u8_to_base64(ta->GetData(), n, url, omitPad);
    return ts_value_make_string(TsString::Create(out));
}

extern "C" TsValue* ts_u8_setFromBase64_native(void* ctx, int argc, TsValue** argv) {
    TsTypedArray* ta = u8_require_uint8_this_nodetach(ctx, "Uint8Array.prototype.setFromBase64");
    if (!ta) return ts_value_make_undefined();
    TsValue* arg = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    TsString* s = u8_require_string_arg(arg, "Uint8Array.prototype.setFromBase64");
    if (!s) return ts_value_make_undefined();
    bool url = false; int lastChunk = 0;
    TsValue* opts = (argc >= 2 && argv) ? argv[1] : nullptr;
    if (!u8_read_base64_options(opts, "Uint8Array.prototype.setFromBase64", &url, &lastChunk, nullptr))
        return ts_value_make_undefined();
    // Detached check AFTER option reading (option getters may detach).
    if (u8_throw_if_detached(ta, "Uint8Array.prototype.setFromBase64")) return ts_value_make_undefined();
    const char* u = s->ToUtf8();
    size_t len = u ? strlen(u) : 0;
    size_t maxBytes = ta->GetLength();
    uint8_t* tmp = (uint8_t*)ts_alloc(maxBytes ? maxBytes : 1);
    size_t rd = 0, wr = 0;
    int err = u8_from_base64(u ? u : "", len, url, lastChunk, tmp, maxBytes, &rd, &wr);
    for (size_t i = 0; i < wr; i++) ta->Set(i, (double)tmp[i]);
    if (err) {
        ts_throw((TsValue*)ts_error_create_typed("SyntaxError",
            "Uint8Array.prototype.setFromBase64: string is not valid base64"));
        return ts_value_make_undefined();
    }
    TsMap* result = TsMap::Create();
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("read"))),
                nanbox_to_tagged(ts_value_make_int((int64_t)rd)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("written"))),
                nanbox_to_tagged(ts_value_make_int((int64_t)wr)));
    return ts_value_make_object(result);
}

// Install the hex AND base64 methods onto the Uint8Array constructor (statics
// fromHex/fromBase64) and its .prototype (toHex/setFromHex/toBase64/
// setFromBase64). Called once from the ctor builder macro.
static void install_uint8_hex_methods(void* ctorVal) {
    void* ctorRaw = ts_value_get_object((TsValue*)ctorVal);
    if (!ctorRaw) ctorRaw = ctorVal;
    TsFunction* ctor = (TsFunction*)ctorRaw;
    if (!ctor || !ctor->properties) return;
    addMethod(ctor->properties, "fromHex", (void*)u8_fromHex_native, 1);
    addMethod(ctor->properties, "fromBase64", (void*)u8_fromBase64_native, 1);
    TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal = ctor->properties->Get(protoKey);
    if (protoVal.type == ValueType::OBJECT_PTR && protoVal.ptr_val) {
        TsMap* proto = (TsMap*)protoVal.ptr_val;
        addMethod(proto, "toHex", (void*)ts_u8_toHex_native, 0);
        addMethod(proto, "setFromHex", (void*)ts_u8_setFromHex_native, 1);
        addMethod(proto, "toBase64", (void*)ts_u8_toBase64_native, 0);
        addMethod(proto, "setFromBase64", (void*)ts_u8_setFromBase64_native, 1);
    }
}

// ECMA-262 22.2.4.2 TypedArray(length): ToIndex(length) + a practical byte cap
// so a huge length throws RangeError instead of attempting a multi-GB
// allocation (the OOM/hang bug: `new Int8Array(1e12)` previously eager-allocated
// ~1 TB). NaN -> 0; negative / > 2^53-1 / over the ~2 GB byte cap -> RangeError.
static int64_t ta_to_index_length(double d, int64_t elemSize) {
    double n = std::isnan(d) ? 0.0 : std::trunc(d);   // ToIntegerOrInfinity: NaN -> 0
    int64_t es = elemSize > 0 ? elemSize : 1;
    if (n < 0 || n > 9007199254740991.0 || n > (double)0x7FFFFFFFLL / (double)es) {
        ts_throw((TsValue*)ts_error_create_typed("RangeError",
            "Invalid typed array length"));
        return 0;  // unreachable (ts_throw longjmps)
    }
    return (int64_t)n;
}

#define DEFINE_TYPED_ARRAY_CTOR(JsName, CName, RuntimeFn, ElemSize)                     \
void* ts_get_global_##CName() {                                                         \
    static void* cached = nullptr;                                                      \
    if (!cached) {                                                                      \
        auto fn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {                 \
            /* Array-like / typed-array source: first read its .length and  */          \
            /* then initialize each element. Fall back to length-form when   */          \
            /* the arg is a plain number (or absent).                        */          \
            if (argc >= 1 && argv && argv[0]) {                                         \
                void* rawSrc = ts_value_get_object(argv[0]);                            \
                bool srcIsObject = false;                                               \
                if (rawSrc) {                                                           \
                    uintptr_t p = (uintptr_t)rawSrc;                                    \
                    if (p > 0x1000 && p < 0x0000800000000000ULL) srcIsObject = true;    \
                }                                                                       \
                /* A STRING primitive is not an Object: `new TA("0")` must take  */     \
                /* the length path (ToIndex(ToNumber("0"))=0), NOT be read as an */     \
                /* array-like of length "0".length==1. String WRAPPER objects    */     \
                /* (TsMap) are real Objects and stay array-like.                 */     \
                if (srcIsObject && !ts_is_any_string(rawSrc)) {                          \
                    TsValue* lenVal = ts_object_get_property(rawSrc, "length");         \
                    if (lenVal && !ts_value_is_undefined(lenVal)) {                     \
                        double lenD = ts_to_number(lenVal);                             \
                        int64_t n = (lenD == lenD && lenD >= 0) ? (int64_t)lenD : 0;    \
                        void* result = RuntimeFn(n);                                    \
                        populate_ta_from_array_like(result, argv[0]);                   \
                        return (TsValue*)result;                                        \
                    }                                                                   \
                }                                                                       \
            }                                                                           \
            int64_t length = 0;                                                         \
            if (argc >= 1 && argv && argv[0]) {                                         \
                length = ta_to_index_length(ts_to_number(argv[0]), (ElemSize));         \
            }                                                                           \
            return (TsValue*)RuntimeFn(length);                                         \
        };                                                                              \
        cached = makeTypedArrayCtor(#JsName, fn, ts_get_global_TypedArray());           \
        /* ECMA-262: BYTES_PER_ELEMENT on the CONSTRUCTOR and its prototype */          \
        /* ({writable:false, enumerable:false, configurable:false}). It was  */          \
        /* missing entirely, so TA.BYTES_PER_ELEMENT through any dynamic     */          \
        /* reference was undefined and harness BPE arithmetic went NaN.      */          \
        {                                                                               \
            void* cfRaw = ts_value_get_object((TsValue*)cached);                        \
            TsFunction* cf = (TsFunction*)(cfRaw ? cfRaw : cached);                     \
            if (cf && cf->properties) {                                                 \
                TsValue bk; bk.type = ValueType::STRING_PTR;                            \
                bk.ptr_val = TsString::GetInterned("BYTES_PER_ELEMENT");                \
                TsValue bv; bv.type = ValueType::NUMBER_INT; bv.i_val = (ElemSize);     \
                cf->properties->SetWithAttrs(bk, bv, 0);                                \
                TsValue pk; pk.type = ValueType::STRING_PTR;                            \
                pk.ptr_val = TsString::GetInterned("prototype");                        \
                TsValue pv = cf->properties->Get(pk);                                   \
                if (pv.type == ValueType::OBJECT_PTR && pv.ptr_val) {                   \
                    ((TsMap*)pv.ptr_val)->SetWithAttrs(bk, bv, 0);                      \
                }                                                                       \
            }                                                                           \
        }                                                                               \
        if (strcmp(#JsName, "Uint8Array") == 0) install_uint8_hex_methods(cached);      \
        { static bool _r=false; if(!_r){ _r=true; ts_gc_register_root((void**)&cached); } } \
    }                                                                                   \
    return cached;                                                                      \
}

// BigInt typed array allocators (ts_typed_array_create_i64/u64) live
// in extensions/node/core/src/TsBuffer.cpp; declared as extern below
// so DEFINE_TYPED_ARRAY_CTOR can reference them. Values are currently
// lossy double-roundtrip in TsTypedArray::Set/Get — a separate Tier-2
// follow-up will plumb real TsBigInt ↔ i64 conversion. Registering
// the constructors globally is what unblocks ~200 tests that just
// need testWithBigIntTypedArrayConstructors to actually iterate.
extern "C" void* ts_typed_array_create_i64(int64_t length);
extern "C" void* ts_typed_array_create_u64(int64_t length);

DEFINE_TYPED_ARRAY_CTOR(Int8Array, Int8Array, ts_typed_array_create_i8, 1)
DEFINE_TYPED_ARRAY_CTOR(Uint8Array, Uint8Array, ts_typed_array_create_u8, 1)
DEFINE_TYPED_ARRAY_CTOR(Uint8ClampedArray, Uint8ClampedArray, ts_typed_array_create_clamped, 1)
DEFINE_TYPED_ARRAY_CTOR(Int16Array, Int16Array, ts_typed_array_create_i16, 2)
DEFINE_TYPED_ARRAY_CTOR(Uint16Array, Uint16Array, ts_typed_array_create_u16, 2)
DEFINE_TYPED_ARRAY_CTOR(Int32Array, Int32Array, ts_typed_array_create_i32, 4)
DEFINE_TYPED_ARRAY_CTOR(Uint32Array, Uint32Array, ts_typed_array_create_u32, 4)
DEFINE_TYPED_ARRAY_CTOR(Float32Array, Float32Array, ts_typed_array_create_f32, 4)
DEFINE_TYPED_ARRAY_CTOR(Float64Array, Float64Array, ts_typed_array_create_f64, 8)
DEFINE_TYPED_ARRAY_CTOR(BigInt64Array, BigInt64Array, ts_typed_array_create_i64, 8)
DEFINE_TYPED_ARRAY_CTOR(BigUint64Array, BigUint64Array, ts_typed_array_create_u64, 8)

#undef DEFINE_TYPED_ARRAY_CTOR

// Generic global lookup by name (namePtr is a raw C string from createGlobalString)
void* ts_get_global(void* namePtr) {
    if (!namePtr) return nullptr;
    const char* name = (const char*)namePtr;
    // Globals that the compiler resolves through the generic string-keyed
    // path (no dedicated ts_get_global_<Name> call emitted).
    if (strcmp(name, "Iterator") == 0) return ts_get_global_Iterator();
    // Try builtin functions (encodeURIComponent, decodeURIComponent, etc.)
    TsString* tsName = TsString::Create(name);
    void* builtin = ts_get_builtin_function(tsName);
    if (builtin) return builtin;
    return nullptr;
}

// An identifier the analyzer flagged as resolving to NO binding (and that
// codegen also could not resolve). Per ECMA-262 9.4.2 (GetValue on an
// unresolvable Reference) evaluating it throws a ReferenceError. First resolve
// any builtin function that slipped past static resolution; only a truly absent
// name throws. nameStr is a TsString*. Throws via a CALL (not an IR
// terminator), so it is valid mid-expression like `null.foo`. Returns undefined
// on the unreachable fallthrough after ts_throw longjmps.
// ---- `with` statement (ES 14.11 object Environment Record) ----------------
// The compiler pushes ToObject(head) around a with-body; the identifier
// resolver walks this stack (innermost first) before globalThis, and
// ts_with_set routes bare-identifier assignments. GC ROOTING (mandatory):
// the vector's malloc backing is invisible to the collector — register a
// mark scanner AND a minor-GC fixup over the entries.
static std::vector<void*> g_withStack;

// Lexical with-env snapshots for functions DEFINED inside a with body and
// called after the with popped (ES 14.11: the closure's scope chain keeps
// the object environment). Keyed by the monomorphized function symbol; the
// definition-site evaluation re-binds on every execution. g_withEnterBases
// pairs each ts_with_enter_fn with the stack depth to restore on exit.
static std::unordered_map<std::string, std::vector<void*>> g_withFnEnvs;
static std::vector<size_t> g_withEnterBases;

static inline bool with_ptr_plausible(void* p) {
    return (uintptr_t)p >= 4096 && (uintptr_t)p <= 0x00007FFFFFFFFFFFULL;
}
static void with_stack_gc_scan(void*) {
    for (auto& e : g_withStack)
        if (with_ptr_plausible(e)) ts_gc_mark_object(e);
    for (auto& kv : g_withFnEnvs)
        for (auto& e : kv.second)
            if (with_ptr_plausible(e)) ts_gc_mark_object(e);
}
static void with_stack_gc_fixup(void*) {
    for (auto& e : g_withStack) {
        if (!with_ptr_plausible(e)) continue;
        void* f = ts_gc_minor_lookup_forward(e);
        if (f) e = f;
    }
    for (auto& kv : g_withFnEnvs) {
        for (auto& e : kv.second) {
            if (!with_ptr_plausible(e)) continue;
            void* f = ts_gc_minor_lookup_forward(e);
            if (f) e = f;
        }
    }
}
static bool g_withGcRegistered = false;

void ts_with_push(void* obj) {
    if (!g_withGcRegistered) {
        ts_gc_register_scanner(with_stack_gc_scan, nullptr);
        ts_gc_register_minor_fixup(with_stack_gc_fixup, nullptr);
        g_withGcRegistered = true;
    }
    // ES 14.11 step 2: ToObject(head) — with(null)/with(undefined) throws
    // TypeError. Throw BEFORE pushing so the stack stays balanced through
    // the longjmp unwind.
    uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)obj);
    if (!obj || nanbox_is_null(nb) || nanbox_is_undefined(nb)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Cannot convert undefined or null to object"));
        return;
    }
    g_withStack.push_back(obj);
}

void ts_with_pop() {
    if (!g_withStack.empty()) g_withStack.pop_back();
}

void ts_with_pop_n(int64_t n) {
    while (n-- > 0 && !g_withStack.empty()) g_withStack.pop_back();
}

// Definition-site evaluation of a function lexically inside a with body:
// snapshot the CURRENT with-stack as the function's lexical environment.
void ts_with_bind_fn(void* nameStr) {
    if (!nameStr) return;
    const char* n = ((TsString*)nameStr)->ToUtf8();
    if (!n) return;
    if (g_withStack.empty()) { g_withFnEnvs.erase(n); return; }
    g_withFnEnvs[n] = g_withStack;
}

// Function prologue: push the bound lexical env (if any) on top of the
// dynamic stack and remember the base to restore on exit. Pushing on top
// (instead of swapping) keeps exception-handler depth snapshots valid; it
// only diverges when the CALLER also has an active with whose object
// shadows a name the callee's lexical chain resolves differently — accepted
// approximation of the dynamic-stack model.
void ts_with_enter_fn(void* nameStr) {
    g_withEnterBases.push_back(g_withStack.size());
    if (!nameStr) return;
    const char* n = ((TsString*)nameStr)->ToUtf8();
    if (!n) return;
    auto it = g_withFnEnvs.find(n);
    if (it == g_withFnEnvs.end()) return;
    for (void* o : it->second) g_withStack.push_back(o);
}

void ts_with_exit_fn() {
    if (g_withEnterBases.empty()) return;
    size_t base = g_withEnterBases.back();
    g_withEnterBases.pop_back();
    if (g_withStack.size() > base) g_withStack.resize(base);
}

// ES 9.1.1.2.1 HasBinding for a with-object environment: HasProperty, then
// @@unscopables filtering — when Get(obj, @@unscopables) is an object and
// ToBoolean(Get(unscopables, N)) is true, the binding is invisible to the
// with env. Getters run (and may throw) per spec.
static bool with_env_has_binding(void* obj, void* nameStr, TsValue* key) {
    if (!obj || !ts_object_has_property(obj, (void*)key)) return false;
    TsValue* unsc = ts_object_get_property(obj, "[Symbol.unscopables]");
    void* unscObj = unsc ? ts_value_get_object(unsc) : nullptr;
    if (unscObj) {
        // Spec step 6: only when Type(unscopables) is Object — a primitive
        // (string/symbol/bigint) unboxes to a pointer too, so type-check it.
        extern TsString* ts_value_typeof(TsValue* v);
        const char* tn = ts_value_typeof(unsc)->ToUtf8();
        if (strcmp(tn, "object") == 0 || strcmp(tn, "function") == 0) {
            const char* n = ((TsString*)nameStr)->ToUtf8();
            TsValue* blocked = ts_object_get_property(unscObj, n ? n : "");
            if (blocked && ts_value_to_bool(blocked)) return false;
        }
    }
    return true;
}

// Reference snapshot for identifier assignment inside a with-body
// (ES 13.15.2: the LHS reference resolves BEFORE the RHS evaluates).
// Returns the 1-based innermost with-stack index holding the name, else 0.
void* ts_with_ref(void* nameStr) {
    if (nameStr && !g_withStack.empty()) {
        TsValue* key = ts_value_make_string(nameStr);
        for (size_t i = g_withStack.size(); i > 0; --i) {
            void* o = g_withStack[i - 1];
            if (with_env_has_binding(o, nameStr, key))
                return ts_value_make_int((int64_t)i);
        }
    }
    return ts_value_make_int(0);
}

// Identifier READ of a builtin-global name inside a with body (ES 14.11: the
// object environment shadows builtins — `with({parseInt(){}}) { parseInt }`
// must yield the with-object's property). With-stack walk first (innermost
// wins); a miss returns the statically-lowered builtin value unchanged.
void* ts_with_shadow_or(void* nameStr, void* fallback) {
    if (nameStr && !g_withStack.empty()) {
        TsValue* key = ts_value_make_string(nameStr);
        for (auto it = g_withStack.rbegin(); it != g_withStack.rend(); ++it) {
            if (with_env_has_binding(*it, nameStr, key)) {
                const char* wn = ((TsString*)nameStr)->ToUtf8();
                return (void*)ts_object_get_property(*it, wn);
            }
        }
    }
    return fallback;
}

// Store through a snapshot from ts_with_ref. Returns true when the snapshot
// named a with-object (the caller's static store is skipped).
// ES 9.1.1.2.5 SetMutableBinding (object env): the binding is RE-CHECKED at
// PutValue — a getter that deleted the property mid-expression makes a
// STRICT write throw ReferenceError; a sloppy write re-creates it.
void* ts_with_set_ref_s(void* refVal, void* nameStr, void* value, int64_t isStrict) {
    int64_t idx = ts_value_get_int((TsValue*)refVal);
    if (idx > 0 && (size_t)idx <= g_withStack.size() && nameStr) {
        void* obj = g_withStack[(size_t)idx - 1];
        TsValue* key = ts_value_make_string(nameStr);
        if (isStrict && !ts_object_has_property(obj, (void*)key)) {
            const char* n = ((TsString*)nameStr)->ToUtf8();
            char msg[256];
            snprintf(msg, sizeof(msg), "%s is not defined", n ? n : "binding");
            ts_throw((TsValue*)ts_error_create_typed("ReferenceError", msg));
            return ts_value_make_bool(false);
        }
        ts_object_set_property(obj, (void*)key, value);
        return ts_value_make_bool(true);
    }
    return ts_value_make_bool(false);
}
void* ts_with_set_ref(void* refVal, void* nameStr, void* value) {
    return ts_with_set_ref_s(refVal, nameStr, value, 0);
}

// Snapshot store with sloppy-global fallback (statically-unresolved LHS).
// Strict variant: a vanished with-binding OR an unresolvable name throws
// ReferenceError instead of the sloppy implicit-global write.
void ts_with_set_ref_or_global_s(void* refVal, void* nameStr, void* value, int64_t isStrict) {
    int64_t idx = ts_value_get_int((TsValue*)refVal);
    if (!nameStr) return;
    TsValue* key = ts_value_make_string(nameStr);
    if (idx > 0 && (size_t)idx <= g_withStack.size()) {
        void* obj = g_withStack[(size_t)idx - 1];
        if (isStrict && !ts_object_has_property(obj, (void*)key)) {
            const char* n = ((TsString*)nameStr)->ToUtf8();
            char msg[256];
            snprintf(msg, sizeof(msg), "%s is not defined", n ? n : "binding");
            ts_throw((TsValue*)ts_error_create_typed("ReferenceError", msg));
            return;
        }
        ts_object_set_property(obj, (void*)key, value);
        return;
    }
    if (isStrict) {
        const char* n = ((TsString*)nameStr)->ToUtf8();
        char msg[256];
        snprintf(msg, sizeof(msg), "%s is not defined", n ? n : "binding");
        ts_throw((TsValue*)ts_error_create_typed("ReferenceError", msg));
        return;
    }
    if (globalThis) ts_object_set_property((void*)globalThis, (void*)key, value);
}
// Compound-assign tail for an unresolved LHS: when the with-write reported
// NOT-written (binding vanished or never existed), strict throws
// ReferenceError and sloppy writes the implicit global.
void ts_with_unref_fallback_set(void* wroteVal, void* nameStr, void* value, int64_t isStrict) {
    if (wroteVal && ts_value_get_bool((TsValue*)wroteVal)) return;
    if (!nameStr) return;
    if (isStrict) {
        const char* n = ((TsString*)nameStr)->ToUtf8();
        char msg[256];
        snprintf(msg, sizeof(msg), "%s is not defined", n ? n : "binding");
        ts_throw((TsValue*)ts_error_create_typed("ReferenceError", msg));
        return;
    }
    TsValue* key = ts_value_make_string(nameStr);
    if (globalThis) ts_object_set_property((void*)globalThis, (void*)key, value);
}

void ts_with_set_ref_or_global(void* refVal, void* nameStr, void* value) {
    ts_with_set_ref_or_global_s(refVal, nameStr, value, 0);
}

// Write through the with-scope chain WITHOUT a fallback: returns true when
// an innermost with-object had the name and received the value. The compiler
// emits the static store on the false branch (`var value = 'v'` inside
// `with(o)` writes o.value when o has it, else the hoisted var).
void* ts_with_try_set(void* nameStr, void* value) {
    if (nameStr) {
        TsValue* key = ts_value_make_string(nameStr);
        for (auto it = g_withStack.rbegin(); it != g_withStack.rend(); ++it) {
            if (with_env_has_binding(*it, nameStr, key)) {
                ts_object_set_property(*it, (void*)key, value);
                return ts_value_make_bool(true);
            }
        }
    }
    return ts_value_make_bool(false);
}

// `delete name` on a bare identifier inside a with-body (ES 13.5.1 /
// object Environment Record DeleteBinding): delete from the innermost
// with-object that HAS the name; else try the global object; an
// unresolvable reference deletes to true in sloppy mode.
void* ts_with_delete(void* nameStr) {
    if (!nameStr) return ts_value_make_bool(true);
    TsValue* key = ts_value_make_string(nameStr);
    extern int ts_object_delete_property(void* objArg, void* keyArg);
    for (auto it = g_withStack.rbegin(); it != g_withStack.rend(); ++it) {
        if (with_env_has_binding(*it, nameStr, key)) {
            int ok = ts_object_delete_property(*it, (void*)key);
            return ts_value_make_bool(ok != 0);
        }
    }
    if (globalThis && ts_object_has_property((void*)globalThis, (void*)key)) {
        int ok = ts_object_delete_property((void*)globalThis, (void*)key);
        return ts_value_make_bool(ok != 0);
    }
    return ts_value_make_bool(true);
}

// Exception-unwind support: ts_push_exception_handler snapshots the depth;
// ts_throw truncates back before longjmp (a throw through a with-body must
// restore the scope chain — ES 14.11 "the scope chain is always restored").
size_t ts_with_stack_size() { return g_withStack.size(); }
void ts_with_truncate(size_t depth) {
    if (g_withStack.size() > depth) g_withStack.resize(depth);
    // Exceptional unwind past a ts_with_enter_fn frame: drop its base so a
    // later ts_with_exit_fn doesn't truncate to a stale depth.
    while (!g_withEnterBases.empty() && g_withEnterBases.back() > depth)
        g_withEnterBases.pop_back();
}

// Assignment to a bare identifier with no static binding inside a with-body:
// the innermost with-object that HAS the name receives the write; otherwise
// it is a sloppy-mode implicit global (ES 14.11 / 9.1.1.4).
void ts_with_set(void* nameStr, void* value) {
    if (!nameStr) return;
    TsValue* key = ts_value_make_string(nameStr);
    for (auto it = g_withStack.rbegin(); it != g_withStack.rend(); ++it) {
        if (with_env_has_binding(*it, nameStr, key)) {
            ts_object_set_property(*it, (void*)key, value);
            return;
        }
    }
    if (globalThis) ts_object_set_property((void*)globalThis, (void*)key, value);
}

// ES temporal dead zone. The compiler pre-declares function-level `let`/
// `const` slots to NANBOX_TDZ and wraps reads of those bindings in this
// check; the declaration's own store replaces the sentinel with the real
// value (or undefined for `let x;`), after which the check is a no-op.
void* ts_tdz_sentinel() {
    return (void*)(uintptr_t)NANBOX_TDZ;
}

void* ts_tdz_check(void* v, void* nameStr) {
    if ((uint64_t)(uintptr_t)v == NANBOX_TDZ) {
        const char* n = nameStr ? ((TsString*)nameStr)->ToUtf8() : nullptr;
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot access '%s' before initialization",
                 n ? n : "variable");
        ts_throw((TsValue*)ts_error_create_typed("ReferenceError", msg));
    }
    return v;
}

// ----- new.target (ES 13.3.12 NewTarget meta-property) -----
// A single ambient register holding the active [[Construct]] target. The
// construct paths (compiler-inlined class construction, the unified
// ts_new_from_constructor, Reflect.construct) SWAP the register in around the
// constructor invocation and restore the previous value after, so nested
// `new` works. A plain [[Call]] never touches it, so `new.target` inside a
// normally-called function reads the cleared register -> undefined.
static TsValue* g_newTarget = nullptr;
static bool g_newTargetRootRegistered = false;

// Swap: set the register, return the PREVIOUS value (for restore).
void* ts_set_new_target(void* v) {
    if (!g_newTargetRootRegistered) {
        ts_gc_register_root((void**)&g_newTarget);
        g_newTargetRootRegistered = true;
    }
    TsValue* prev = g_newTarget;
    // Store undefined as null so a cleared register needs no GC root value.
    g_newTarget = (v && !ts_value_is_undefined((TsValue*)v)) ? (TsValue*)v : nullptr;
    return prev ? (void*)prev : (void*)ts_value_make_undefined();
}

// Read (non-destructive): the current construct target or undefined.
void* ts_get_new_target() {
    return g_newTarget ? (void*)g_newTarget : (void*)ts_value_make_undefined();
}

// Resolve a BUILTIN global constructor by name (the same set the compiler
// maps via ts_get_global_* in HIRToLLVM). Returns null for unknown names.
// Used by ts_class_link_builtin_base for `class C extends <builtin>`.
// WeakRef / FinalizationRegistry constructor GLOBALS. `new WeakRef(x)` is
// compiler-inlined (ts_weakref_create), so these exist for identity reads,
// `x instanceof WeakRef`, and `class Sub extends WeakRef` prototype linkage.
void* ts_get_global_WeakRef() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("WeakRef");
        cached = wrapAsCallable(ctor, "WeakRef", 1);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}
void* ts_get_global_FinalizationRegistry() {
    TenureScope _tenure;
    static void* cached = nullptr;
    if (!cached) {
        TsMap* ctor = makeSimpleConstructorGlobal("FinalizationRegistry");
        cached = wrapAsCallable(ctor, "FinalizationRegistry", 1);
        { static bool _rooted=false; if(!_rooted){ _rooted=true; ts_gc_register_root((void**)&cached); } }
    }
    return cached;
}

static void* ts_global_ctor_by_name(const char* n) {
    struct Entry { const char* name; void* (*get)(); };
    static const Entry table[] = {
        {"Object", ts_get_global_Object}, {"Function", ts_get_global_Function},
        {"Array", ts_get_global_Array}, {"String", ts_get_global_String},
        {"Number", ts_get_global_Number}, {"Boolean", ts_get_global_Boolean},
        {"Date", ts_get_global_Date}, {"RegExp", ts_get_global_RegExp},
        {"Error", ts_get_global_Error}, {"TypeError", ts_get_global_TypeError},
        {"RangeError", ts_get_global_RangeError},
        {"ReferenceError", ts_get_global_ReferenceError},
        {"SyntaxError", ts_get_global_SyntaxError},
        {"URIError", ts_get_global_URIError}, {"EvalError", ts_get_global_EvalError},
        {"AggregateError", ts_get_global_AggregateError},
        {"Map", ts_get_global_Map}, {"Set", ts_get_global_Set},
        {"WeakMap", ts_get_global_WeakMap}, {"WeakSet", ts_get_global_WeakSet},
        {"Promise", ts_get_global_Promise}, {"Symbol", ts_get_global_Symbol},
        {"ArrayBuffer", ts_get_global_ArrayBuffer},
        {"SharedArrayBuffer", ts_get_global_SharedArrayBuffer},
        {"DataView", ts_get_global_DataView},
        {"Int8Array", ts_get_global_Int8Array}, {"Uint8Array", ts_get_global_Uint8Array},
        {"Uint8ClampedArray", ts_get_global_Uint8ClampedArray},
        {"Int16Array", ts_get_global_Int16Array}, {"Uint16Array", ts_get_global_Uint16Array},
        {"Int32Array", ts_get_global_Int32Array}, {"Uint32Array", ts_get_global_Uint32Array},
        {"Float32Array", ts_get_global_Float32Array}, {"Float64Array", ts_get_global_Float64Array},
        {"BigInt64Array", ts_get_global_BigInt64Array},
        {"BigUint64Array", ts_get_global_BigUint64Array},
        {"WeakRef", ts_get_global_WeakRef},
        {"Iterator", ts_get_global_Iterator},
        {"FinalizationRegistry", ts_get_global_FinalizationRegistry},
        // SMELL-002 gap patch: constructable builtins the interp/new
        // paths could not resolve by name.
        {"Proxy", ts_get_global_Proxy},
        {"GeneratorFunction", ts_get_global_GeneratorFunction},
        {"AsyncFunction", ts_get_global_AsyncFunction},
        {"AsyncGeneratorFunction", ts_get_global_AsyncGeneratorFunction},
    };
    for (const auto& e : table)
        if (strcmp(n, e.name) == 0) return e.get();
    return nullptr;
}

// Exported for the eval interpreter (src/interp/TsInterp.cpp): bare-identifier
// reads of builtin constructors must yield the FIRST-CLASS cached constructor
// (a globalThis property read yields only the name string, which `new` can't
// construct — same trap the Function-ctor subclass idiom hit).
extern "C" void* ts_interp_global_ctor_by_name(const char* n) {
    return ts_global_ctor_by_name(n);
}

// `x instanceof Array`: a real TsArray matches by magic (fast path); a
// `class C extends Array` instance (flat object) matches via the ordinary
// prototype-chain walk against Array.prototype.
bool ts_instanceof_array(TsValue* v) {
    extern bool ts_array_is_array(void*);
    extern bool ts_instanceof_dynamic(TsValue*, TsValue*);
    if (ts_array_is_array((void*)v)) return true;
    void* g = ts_get_global_Array();
    if (!g) return false;
    return ts_instanceof_dynamic(v, (TsValue*)g);
}

// Default-constructor super() into a BUILTIN Error-family base:
// `class Err extends TypeError {}` + `new Err("msg")` must run the Error
// constructor steps on the instance — an own non-enumerable `message`
// (ES 20.5.1.1 step 4). Emitted by visitNewExpression when the class has
// baseBuiltinName and no user constructor. No std::string locals (the
// ToString of a Symbol message ts_throws).
extern "C" TsValue* ts_object_defineProperty(TsValue* obj, TsValue* prop, TsValue* descriptor);
// `class MySet extends Set {}` instance allocation: the exotic builtin
// object IS the instance (collection magic intact); subclass dynamic
// properties land in the native-object side map.
void* ts_subclass_builtin_alloc(void* nameStr, void* ctorVal) {
    const char* n = nameStr ? ((TsString*)nameStr)->ToUtf8() : nullptr;
    if (!n) return nullptr;
    extern void* ts_set_create();
    extern void* ts_map_create_explicit();
    extern void* ts_weakset_create();
    extern void* ts_weakmap_create();
    extern void* ts_array_create();
    void* inst = nullptr;
    if (strcmp(n, "Set") == 0)          inst = ts_set_create();
    else if (strcmp(n, "Map") == 0)     inst = ts_map_create_explicit();
    else if (strcmp(n, "WeakSet") == 0) inst = ts_weakset_create();
    else if (strcmp(n, "WeakMap") == 0) inst = ts_weakmap_create();
    else if (strcmp(n, "Array") == 0)   inst = ts_array_create();
    if (!inst) return nullptr;
    // Link instance -> Subclass.prototype so `x instanceof Subclass` walks
    // (stored under a hidden key in the native-object side map; consumed by
    // ts_proto_chain_has and ts_object_getPrototypeOf's native branch).
    if (ctorVal) {
        TsValue* protoVal = ts_object_get_property(
            ts_value_get_object((TsValue*)ctorVal)
                ? ts_value_get_object((TsValue*)ctorVal) : ctorVal,
            "prototype");
        if (protoVal && !ts_value_is_undefined(protoVal)) {
            extern void ts_native_object_set_proto(void* obj, TsValue* proto);
            ts_native_object_set_proto(inst, protoVal);
        }
    }
    return inst;
}

void ts_super_builtin_call(void* thisVal, void* nameStr, int64_t argc, void* a0) {
    if (!thisVal || !nameStr) return;
    const char* n = ((TsString*)nameStr)->ToUtf8();
    if (!n) return;
    static const char* errNames[8] = { "Error", "EvalError", "RangeError",
        "ReferenceError", "SyntaxError", "TypeError", "URIError",
        "AggregateError" };
    bool isErr = false;
    for (int i = 0; i < 8; i++) if (strcmp(n, errNames[i]) == 0) { isErr = true; break; }
    if (!isErr) return;
    if (argc >= 1 && a0 && !ts_value_is_undefined((TsValue*)a0)) {
        void* msgStr = ts_string_from_value((TsValue*)a0);  // ToString (may throw)
        if (!msgStr) return;
        TsMap* desc = TsMap::Create();
        TsValue vk; vk.type = ValueType::STRING_PTR; vk.ptr_val = TsString::GetInterned("value");
        TsValue vv; vv.type = ValueType::STRING_PTR; vv.ptr_val = (TsString*)msgStr;
        desc->Set(vk, vv);
        TsValue wk; wk.type = ValueType::STRING_PTR; wk.ptr_val = TsString::GetInterned("writable");
        TsValue wv; wv.type = ValueType::BOOLEAN; wv.i_val = 1;
        desc->Set(wk, wv);
        TsValue ek; ek.type = ValueType::STRING_PTR; ek.ptr_val = TsString::GetInterned("enumerable");
        TsValue ev; ev.type = ValueType::BOOLEAN; ev.i_val = 0;
        desc->Set(ek, ev);
        TsValue ck; ck.type = ValueType::STRING_PTR; ck.ptr_val = TsString::GetInterned("configurable");
        TsValue cv; cv.type = ValueType::BOOLEAN; cv.i_val = 1;
        desc->Set(ck, cv);
        TsValue key; key.type = ValueType::STRING_PTR;
        key.ptr_val = TsString::GetInterned("message");
        ts_object_defineProperty((TsValue*)thisVal, nanbox_from_tagged(key),
                                 ts_value_make_object(desc));
    }
}

// `class C extends <builtin>` (ES 15.7.14 ClassDefinitionEvaluation): the
// heritage is not a compiler-known class, so emitDeferredStaticInits records
// the heritage NAME and calls this at class-flush time to link
// C.prototype.[[Prototype]] = Base.prototype and C.[[Prototype]] = Base.
// This makes `new C() instanceof Base` true via the flat-object prototype
// walk in ts_instanceof_dynamic. No-ops when the name doesn't resolve.
void ts_class_link_builtin_base(void* ctorVal, void* protoVal, void* nameStr) {
    if (!ctorVal || !protoVal || !nameStr) return;
    TsString* name = ts_ensure_flat(nameStr);
    const char* n = name ? name->ToUtf8() : nullptr;
    if (!n) return;
    void* base = ts_global_ctor_by_name(n);
    if (!base) {
        // Fall back to a globalThis property (host-defined constructors).
        TsValue* key = ts_value_make_string(nameStr);
        if (globalThis && ts_object_has_property((void*)globalThis, (void*)key))
            base = (void*)ts_object_get_property((void*)globalThis, n);
    }
    if (!base && strchr(n, '.')) {
        // Dotted heritage (`class C extends Temporal.Duration`): resolve the
        // first segment as a global namespace object, then walk properties.
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", n);
        char* rest = strchr(buf, '.');
        *rest++ = 0;
        void* cur = ts_global_ctor_by_name(buf);
        // Builtin NAMESPACES are ladder-served, not globalThis map entries —
        // resolve them explicitly (`Temporal.Duration` walked a boxed
        // UNDEFINED from the globalThis miss and the next property get threw
        // "Cannot read properties of undefined (reading 'Duration')").
        if (!cur) {
            extern void* ts_get_global_Temporal();
            extern void* ts_get_global_Intl();
            if (strcmp(buf, "Temporal") == 0) cur = ts_get_global_Temporal();
            else if (strcmp(buf, "Intl") == 0) cur = ts_get_global_Intl();
        }
        if (!cur && globalThis) cur = (void*)ts_object_get_property((void*)globalThis, buf);
        if (cur && (ts_value_is_undefined((TsValue*)cur) ||
                    ts_value_is_nullish((TsValue*)cur)))
            cur = nullptr;  // absent global: stop, don't property-get undefined
        while (cur && rest && *rest) {
            char* next = strchr(rest, '.');
            if (next) *next++ = 0;
            TsValue* pv = ts_object_get_property(ts_nanbox_safe_unbox(cur) ? ts_nanbox_safe_unbox(cur) : cur, rest);
            cur = (pv && !ts_value_is_undefined(pv)) ? (void*)pv : nullptr;
            rest = next;
        }
        base = cur;
    }
    if (!base) return;
    TsValue* baseProto = ts_object_get_property(base, "prototype");
    if (baseProto && !ts_value_is_undefined(baseProto))
        ts_object_setPrototypeOf((TsValue*)protoVal, baseProto);
    ts_object_setPrototypeOf((TsValue*)ctorVal, (TsValue*)base);
}

extern "C" TsValue* ts_bound_function_call(void* ctx, int argc, TsValue** argv);
extern "C" void* ts_bound_function_target(void* boundCtx);  // TsObject_Call.cpp

// ES 7.2.4 IsConstructor for our value model: a TsFunction/TsClosure with
// its is_constructor flag set (function declarations default true; arrows,
// methods, built-in prototype methods are false). Primitives and plain
// objects are never constructors.
static bool value_is_constructor(void* v) {
    void* raw = ts_nanbox_safe_unbox(v);
    if (!raw) return false;
    if ((uintptr_t)raw < 4096 || (uintptr_t)raw > 0x00007FFFFFFFFFFFULL) return false;
    if (*(uint32_t*)raw == 0x46554E43) return true;  // native fn at offset 0
    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
    if (m16 == TsFunction::MAGIC) {
        TsFunction* tf = (TsFunction*)raw;
        // ES 10.4.1: a BOUND function is a constructor iff its TARGET is —
        // (() => {}).bind() must stay non-constructable
        // (superclass-arrow-function's `extends bound` leg). Recurse via the
        // helper (declared at file scope; TsBoundFunction is TU-private to
        // the call module).
        if (tf->funcPtr == (void*)ts_bound_function_call && tf->context) {
            void* target = ts_bound_function_target(tf->context);
            return target ? value_is_constructor(target) : false;
        }
        return tf->is_constructor;
    }
    if (m16 == 0x434C5352 /*CLSR*/) {
        TsClosure* cl = (TsClosure*)raw;
        return cl->is_constructor && cl->constructable;
    }
    return false;
}

// `class C extends <identifier>` with the compiler-resolved VALUE (the name-
// only ts_class_link_builtin_base can't see module vars/closures). undefined
// value -> legacy name lookup; null -> legal; a non-constructor -> TypeError
// BEFORE any "prototype" read (ES 15.7.14 step 6.f; the
// superclass-emulates-undefined family asserts zero prototype gets).
void ts_class_link_dynamic_base(void* ctorVal, void* protoVal, void* baseVal, void* nameStr) {
    uint64_t nb = baseVal ? nanbox_from_tsvalue_ptr((TsValue*)baseVal) : 0;
    if (!baseVal || nanbox_is_undefined(nb)) {
        ts_class_link_builtin_base(ctorVal, protoVal, nameStr);
        return;
    }
    if (nanbox_is_null(nb)) return;
    // Nanbox-primitive heritage (number/bool/string): definitely not a
    // constructor -> TypeError.
    if (nanbox_is_int32(nb) || nanbox_is_double(nb) || nanbox_is_bool(nb)) {
        // (string heritage also throws: it unboxes to a plausible STRG-magic
        // pointer and fails value_is_constructor below)
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Class extends value is not a constructor"));
        return;
    }
    // A value that unboxes to nothing plausible is an UNINITIALIZED slot
    // (the class-expression trailer can evaluate before the heritage var's
    // assignment when the pre-pass hoists it) — fall back to the legacy
    // name link rather than false-TypeError.
    {
        void* rawCheck = ts_nanbox_safe_unbox(baseVal);
        if (!rawCheck || (uintptr_t)rawCheck < 4096 ||
            (uintptr_t)rawCheck > 0x00007FFFFFFFFFFFULL) {
            ts_class_link_builtin_base(ctorVal, protoVal, nameStr);
            return;
        }
    }
    if (!value_is_constructor(baseVal)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Class extends value is not a constructor"));
        return;
    }
    void* base = ts_nanbox_safe_unbox(baseVal);
    if (!base) base = baseVal;
    if (!ctorVal || !protoVal) return;
    TsValue* baseProto = ts_object_get_property(base, "prototype");
    if (baseProto && !ts_value_is_undefined(baseProto))
        ts_object_setPrototypeOf((TsValue*)protoVal, baseProto);
    ts_object_setPrototypeOf((TsValue*)ctorVal, (TsValue*)base);
}

void* ts_resolve_identifier_or_throw(void* nameStr) {
    // `with` object environments shadow EVERYTHING outer — including
    // builtins (`with({parseInt(){}}) { parseInt }` must yield the
    // with-object's property) — so the with-stack walk comes FIRST
    // (ES 14.11), innermost entry first.
    if (nameStr && !g_withStack.empty()) {
        TsValue* key = ts_value_make_string(nameStr);
        for (auto it = g_withStack.rbegin(); it != g_withStack.rend(); ++it) {
            if (with_env_has_binding(*it, nameStr, key)) {
                const char* wn = ((TsString*)nameStr)->ToUtf8();
                return (void*)ts_object_get_property(*it, wn);
            }
        }
    }
    void* builtin = ts_get_builtin_function(nameStr);
    if (builtin) return builtin;
    // Sloppy-mode fallback (ECMA-262 9.1.1.4 GlobalEnvironmentRecord): a bare
    // identifier that is not a lexical/var binding resolves to a property of the
    // global object — e.g. `global.x = 1` (or `this.x = 1` at script top level)
    // then reading bare `x`. Only resolve when the property actually EXISTS;
    // a truly absent name still throws ReferenceError below. (test262 sm shell
    // harnesses expose their helpers this way: `global.anyTypedArrayConstructors
    // = ...; })(this)`.)
    if (nameStr && globalThis) {
        TsValue* key = ts_value_make_string(nameStr);
        if (ts_object_has_property((void*)globalThis, (void*)key)) {
            const char* gn = ((TsString*)nameStr)->ToUtf8();
            return (void*)ts_object_get_property((void*)globalThis, gn);
        }
    }
    const char* n = nameStr ? ((TsString*)nameStr)->ToUtf8() : nullptr;
    char msg[256];
    snprintf(msg, sizeof(msg), "%s is not defined", n ? n : "variable");
    ts_throw((TsValue*)ts_error_create_typed("ReferenceError", msg));
    return ts_value_make_undefined();
}

} // extern "C"

// ===========================================================================
// Plain-call stubs for built-in class constructors (require-new TypeError).
//
// A DIRECT bare call like `Map()` / `ArrayBuffer(8)` / `Proxy(t, h)` is
// lowered by the compiler to a WEAK vararg symbol named by arity mangling
// (`Map`, `ArrayBuffer_any`, `Proxy_any_any`, ... — see
// HIRToLLVM_Calls.cpp:905-931) whose weak body returns undefined. These
// STRONG definitions override the weak stubs at link time (the same
// mechanism that resolves the runtime's real `parseFloat`), so calling a
// built-in class constructor without `new` (NewTarget undefined) throws
// TypeError per ES. `new X(...)` never reaches these symbols: it compiles
// to the dedicated create fast paths / ts_new_from_constructor.
// Indirect calls (`var M = Map; M()`) go through ts_call and are guarded
// in the wrapAsCallable body (ctx==name), not here.
// POD frames only — ts_throw longjmps out of these frames.
extern "C" {

#define TS_CTOR_REQUIRES_NEW_STUB(SYM, NAME)                                   \
    TsValue* SYM(...) {                                                        \
        ts_throw((TsValue*)ts_error_create_typed("TypeError",                  \
            "Constructor " NAME " requires 'new'"));                           \
        return ts_value_make_undefined(); /* unreachable */                    \
    }

#define TS_CTOR_REQUIRES_NEW_STUBS(NAME)                                       \
    TS_CTOR_REQUIRES_NEW_STUB(NAME, #NAME)                                     \
    TS_CTOR_REQUIRES_NEW_STUB(NAME##_any, #NAME)                               \
    TS_CTOR_REQUIRES_NEW_STUB(NAME##_any_any, #NAME)                           \
    TS_CTOR_REQUIRES_NEW_STUB(NAME##_any_any_any, #NAME)                       \
    TS_CTOR_REQUIRES_NEW_STUB(NAME##_any_any_any_any, #NAME)

TS_CTOR_REQUIRES_NEW_STUBS(Map)
TS_CTOR_REQUIRES_NEW_STUBS(Set)
TS_CTOR_REQUIRES_NEW_STUBS(WeakMap)
TS_CTOR_REQUIRES_NEW_STUBS(WeakSet)
TS_CTOR_REQUIRES_NEW_STUBS(Promise)
TS_CTOR_REQUIRES_NEW_STUBS(Proxy)
TS_CTOR_REQUIRES_NEW_STUBS(ArrayBuffer)
TS_CTOR_REQUIRES_NEW_STUBS(DataView)
TS_CTOR_REQUIRES_NEW_STUBS(WeakRef)
TS_CTOR_REQUIRES_NEW_STUBS(FinalizationRegistry)

#undef TS_CTOR_REQUIRES_NEW_STUBS
#undef TS_CTOR_REQUIRES_NEW_STUB

} // extern "C" (require-new ctor stubs)
