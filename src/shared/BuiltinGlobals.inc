// Canonical builtin-global table (SMELL-002 item 11). ONE list generates:
//   - the compiler's lowerLoadGlobal known-name map (HIRToLLVM_CallsMethod.cpp)
//   - the runtime's ts_global_ctor_by_name table (TsGlobals.cpp)
//   - the compiler's bare-identifier builtinObjects set (ASTToHIR_Expressions_Other.cpp)
// Add a global HERE, not at the consumer sites.
//
// Consumer defines (any subset; the rest default to no-ops below):
//   BUILTIN_GLOBAL(name, isCtor, isBuiltinObj)
//     isCtor       -> appears in ts_global_ctor_by_name (interp/new-by-name)
//     isBuiltinObj -> bare identifier resolves via LoadGlobal in untyped JS
//     every entry  -> lowerLoadGlobal emits ts_get_global_<name>()
//   BUILTIN_GLOBAL_ALIAS(alias, name)  -> alias lowers to ts_get_global_<name>()
//   BUILTIN_MODULE(name)  -> node builtin module: lowerLoadGlobal only
//     (NOT a bare-identifier global: modules require require()/import;
//      "assert" is deliberately absent -- a user `function assert(){}` must win)
#ifndef BUILTIN_GLOBAL
#define BUILTIN_GLOBAL(name, isCtor, isBuiltinObj)
#endif
#ifndef BUILTIN_GLOBAL_ALIAS
#define BUILTIN_GLOBAL_ALIAS(alias, name)
#endif
#ifndef BUILTIN_MODULE
#define BUILTIN_MODULE(name)
#endif

// --- Namespace / value globals ---------------------------------------------
BUILTIN_GLOBAL(console, 0, 0)
BUILTIN_GLOBAL(Math, 0, 1)
BUILTIN_GLOBAL(JSON, 0, 1)
BUILTIN_GLOBAL(Reflect, 0, 1)
BUILTIN_GLOBAL(Intl, 0, 1)
BUILTIN_GLOBAL(Temporal, 0, 1)
BUILTIN_GLOBAL(BigInt, 0, 1)
BUILTIN_GLOBAL(Buffer, 0, 1)
BUILTIN_GLOBAL(process, 0, 1)
BUILTIN_GLOBAL(globalThis, 0, 1)
BUILTIN_GLOBAL_ALIAS(global, globalThis)
// %TypedArray% intrinsic: internal getter only -- NOT a real JS global, so it
// is neither a bare-identifier builtin nor in the ctor-by-name table.
BUILTIN_GLOBAL(TypedArray, 0, 0)

// --- Constructors ------------------------------------------------------------
BUILTIN_GLOBAL(Object, 1, 1)
BUILTIN_GLOBAL(Function, 1, 1)
BUILTIN_GLOBAL(Array, 1, 1)
BUILTIN_GLOBAL(String, 1, 1)
BUILTIN_GLOBAL(Number, 1, 1)
BUILTIN_GLOBAL(Boolean, 1, 1)
BUILTIN_GLOBAL(Date, 1, 1)
BUILTIN_GLOBAL(RegExp, 1, 1)
BUILTIN_GLOBAL(Promise, 1, 1)
BUILTIN_GLOBAL(Symbol, 1, 1)
BUILTIN_GLOBAL(Map, 1, 1)
BUILTIN_GLOBAL(Set, 1, 1)
BUILTIN_GLOBAL(WeakMap, 1, 1)
BUILTIN_GLOBAL(WeakSet, 1, 1)
BUILTIN_GLOBAL(WeakRef, 1, 1)
BUILTIN_GLOBAL(FinalizationRegistry, 1, 1)
BUILTIN_GLOBAL(Proxy, 1, 1)
BUILTIN_GLOBAL(Iterator, 1, 1)
BUILTIN_GLOBAL(ArrayBuffer, 1, 1)
BUILTIN_GLOBAL(SharedArrayBuffer, 1, 1)
BUILTIN_GLOBAL(DataView, 1, 1)
BUILTIN_GLOBAL(GeneratorFunction, 1, 0)
BUILTIN_GLOBAL(AsyncFunction, 1, 0)
BUILTIN_GLOBAL(AsyncGeneratorFunction, 1, 0)

// --- Error constructors ------------------------------------------------------
BUILTIN_GLOBAL(Error, 1, 1)
BUILTIN_GLOBAL(TypeError, 1, 1)
BUILTIN_GLOBAL(RangeError, 1, 1)
BUILTIN_GLOBAL(ReferenceError, 1, 1)
BUILTIN_GLOBAL(SyntaxError, 1, 1)
BUILTIN_GLOBAL(URIError, 1, 1)
BUILTIN_GLOBAL(EvalError, 1, 1)
BUILTIN_GLOBAL(AggregateError, 1, 1)

// --- TypedArray constructors -------------------------------------------------
BUILTIN_GLOBAL(Int8Array, 1, 1)
BUILTIN_GLOBAL(Uint8Array, 1, 1)
BUILTIN_GLOBAL(Uint8ClampedArray, 1, 1)
BUILTIN_GLOBAL(Int16Array, 1, 1)
BUILTIN_GLOBAL(Uint16Array, 1, 1)
BUILTIN_GLOBAL(Int32Array, 1, 1)
BUILTIN_GLOBAL(Uint32Array, 1, 1)
BUILTIN_GLOBAL(Float32Array, 1, 1)
BUILTIN_GLOBAL(Float64Array, 1, 1)
BUILTIN_GLOBAL(BigInt64Array, 1, 1)
BUILTIN_GLOBAL(BigUint64Array, 1, 1)

// --- Node builtin modules (lowerLoadGlobal only) -----------------------------
BUILTIN_MODULE(path)
BUILTIN_MODULE(fs)
BUILTIN_MODULE(os)
BUILTIN_MODULE(url)
BUILTIN_MODULE(util)
BUILTIN_MODULE(crypto)
BUILTIN_MODULE(http)
BUILTIN_MODULE(https)
BUILTIN_MODULE(net)
BUILTIN_MODULE(dgram)
BUILTIN_MODULE(dns)
BUILTIN_MODULE(tls)
BUILTIN_MODULE(zlib)
BUILTIN_MODULE(stream)
BUILTIN_MODULE(events)
BUILTIN_MODULE(querystring)
BUILTIN_MODULE(child_process)

#undef BUILTIN_GLOBAL
#undef BUILTIN_GLOBAL_ALIAS
#undef BUILTIN_MODULE
