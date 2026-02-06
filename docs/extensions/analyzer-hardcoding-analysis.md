# Why the Analyzer Requires Hardcoded Node.js Knowledge

**Date:** 2026-02-06

This document explains each reason the analyzer currently hardcodes Node.js-specific information, and what extension schema features would need to be added to eliminate each one.

---

## Background

The extension system (`ExtensionSchema.h`) provides a declarative JSON schema for defining types, methods, properties, and lowering specs. The analyzer loads these via `registerTypesFromExtensions()` in `Analyzer_StdLib.cpp`.

However, the schema is **purely structural** -- it can describe "method X takes parameter Y of type Z" but cannot express any **semantic analysis rules** like "infer the callback parameter type from the receiver's generic arguments." This gap is why 30 `Analyzer_StdLib_*.cpp` files and several special cases in `Analyzer_Expressions_Calls.cpp` remain.

---

## Gap 1: Contextual Callback Typing

### What the hardcoded code does

`Analyzer_Expressions_Calls.cpp` (lines 42-78) contains `getExpectedCallbackType()`, which provides the expected types for callback parameters based on the API being called:

```cpp
// http.createServer((req, res) => { ... })
//   req → IncomingMessage, res → ServerResponse
if ((objName == "http" || objName == "https") && methodName == "createServer")

// net.createServer((socket) => { ... })
//   socket → Socket
if (objName == "net" && methodName == "createServer")

// fs.readFile(path, (err, data) => { ... })
//   err → any, data → Buffer
if (objName == "fs" && (methodName == "readFile" || methodName == "writeFile"))

// emitter.on('data', (chunk) => { ... })
//   chunk → any
if (methodName == "on")
```

Without this, when a user writes `http.createServer((req, res) => { res.writeHead(200); })`, the parameters `req` and `res` would be typed as `Any` because TypeScript relies on contextual typing to infer their types. With `Any` typing, the compiler would generate slow boxed paths and lose access to IncomingMessage/ServerResponse methods.

### Why the extension schema can't express this

The current `MethodDefinition` schema looks like:

```cpp
struct MethodDefinition {
    std::string call;
    std::vector<ParameterDefinition> params;
    TypeReference returns;
    std::optional<LoweringSpec> lowering;
};
```

A `ParameterDefinition` describes the method's OWN parameter type -- e.g., "createServer takes a function." But there is no way to say "when that function parameter is an untyped arrow function, infer its parameters as [IncomingMessage, ServerResponse]."

The schema knows `createServer` takes a `(req: IncomingMessage, res: ServerResponse) => void` callback, but when the analyzer encounters an untyped arrow `(req, res) => {}`, it needs to look UP the call chain and find the expected callback signature. The extension system registers the method signature, but the analyzer's contextual typing machinery doesn't consult extension-defined function parameter types during callback inference.

### What would need to change

**Option A: Make `registerTypesFromExtensions()` register function-typed parameters with full signatures**

Currently, when a method parameter has type `"function"` or a callback type, `convertExtTypeRef()` maps it to `TypeKind::Function` but doesn't populate the `FunctionType`'s `paramTypes` from the extension definition.

The fix: when the extension JSON defines a parameter like:
```json
{
  "name": "requestListener",
  "type": {
    "name": "function",
    "typeArgs": [
      { "name": "IncomingMessage" },
      { "name": "ServerResponse" }
    ]
  }
}
```

The type converter should build a full `FunctionType` with `paramTypes = [ClassType("IncomingMessage"), ClassType("ServerResponse")]` and register it as the expected type for that parameter position. Then the existing contextual typing stack (`pushContextualType`/`popContextualType`) would automatically pick up the types from the extension-defined signature.

**Option B: Add an explicit `callbackSignatures` field to the extension schema**

Add to `MethodDefinition`:
```cpp
struct CallbackSignature {
    int paramIndex;                          // Which parameter is the callback
    std::vector<TypeReference> paramTypes;   // Expected types for callback params
    TypeReference returnType;                // Expected return type of callback
};

struct MethodDefinition {
    // ... existing fields ...
    std::vector<CallbackSignature> callbackSignatures;
};
```

JSON:
```json
"createServer": {
  "call": "ts_http_createServer",
  "params": [{ "name": "requestListener", "type": "function" }],
  "callbackSignatures": [{
    "paramIndex": 0,
    "paramTypes": ["IncomingMessage", "ServerResponse"],
    "returnType": "void"
  }]
}
```

**Recommendation:** Option A is simpler and more general. The callback parameter types are already implicit in the extension JSON's parameter type definitions -- the analyzer just needs to extract them during `convertExtTypeRef()` when the parameter is a function type. No schema change required, only a fix to how function-typed parameters are converted.

---

## Gap 2: Promise Type Inference from Executor Body

### What the hardcoded code does

`Analyzer_Expressions_Calls.cpp` (lines 749-885) contains `inferPromiseTypeFromExecutor()`. When the analyzer sees:

```typescript
const p = new Promise((resolve, reject) => {
    resolve(42);
});
```

It scans the executor function body, finds the `resolve(42)` call, infers that `42` is a `number`, and types the result as `Promise<number>`.

### This is NOT Node.js-specific

`Promise` is an ECMAScript built-in (ES2015), not a Node.js API. This inference applies to all promises regardless of context. It is listed here only because it lives near the other hardcoded code and could be mistaken for Node.js-specific logic.

### Why the extension schema can't express this

This is an **analysis algorithm**, not a type declaration. The extension schema is purely declarative -- it describes the shapes of types and methods. It has no way to express "scan the body of argument 0, find calls to the first parameter name, and use the argument type as the generic type parameter." That requires walking AST nodes, resolving parameter names, and propagating inferred types -- all imperative operations that belong in a type inference engine, not a JSON schema.

### What would need to change

Nothing. This belongs in the core analyzer alongside other type inference algorithms (control flow narrowing, generic instantiation, type guard inference). It is not a candidate for migration to the extension system.

The current implementation is hardcoded to the name "Promise", but if other generic constructors ever need similar inference (unlikely), the pattern could be generalized at that point.

---

## Gap 3: Method Return Type Dispatch on Receiver Type

### What the hardcoded code does

`Analyzer_Expressions_Calls.cpp` (lines 310-426) has special cases for method return types:

```cpp
// "hello".split(",") returns string[]
if (prop->name == "split") → ArrayType(String)

// [1,2,3].map(fn) returns Array<ReturnType<fn>>
if (prop->name == "map") → inferred from callback

// Math.min(1, 2) returns number
if (obj->name == "Math") → specific return types

// Promise.resolve(value) returns Promise<typeof value>
if (prop->name == "resolve") → generic construction
```

### Why the extension schema can't express this

The extension schema supports static return types:
```json
"split": { "returns": { "name": "Array", "typeArgs": [{ "name": "string" }] } }
```

This works for simple cases. But it cannot express:
1. **Generic return types** that depend on argument types (e.g., `Promise.resolve(x)` returns `Promise<typeof x>`)
2. **Return types inferred from callback return types** (e.g., `array.map(fn)` returns `Array<ReturnType<fn>>`)

### What would need to change

For static return types (like `split` returning `string[]`), the extension system already works. The hardcoded cases exist because:

1. The Analyzer_StdLib_*.cpp files register these types FIRST, so extensions never get a chance
2. Some return types genuinely require inference

**For case 1:** Simply removing the Analyzer_StdLib_*.cpp registrations would let extensions handle static return types.

**For case 2 (generic inference):** Add a return type inference rule to the schema:

```json
"resolve": {
  "returns": { "name": "Promise", "typeArgs": [{ "name": "$0" }] },
  "inferReturnTypeArgs": { "T": "argType:0" }
}
```

Where `$0` means "first type parameter" and `"argType:0"` means "type of argument 0." The analyzer would interpret these rules during type inference.

**Recommendation:** Start by removing Analyzer_StdLib_*.cpp to let extensions handle static return types. Add inference rules only for the ~5 cases that genuinely need them (Promise.resolve, Array.map, Array.filter, Array.from, Object.keys).

---

## Gap 4: Builtin Module List

### What the hardcoded code does

`ModuleResolver.cpp` (lines 23-30) has a hardcoded list of ~35 module names used to determine if an import refers to a built-in module (no filesystem resolution needed):

```cpp
static const std::vector<std::string> BUILTIN_MODULES = {
    "fs", "path", "crypto", "os", "http", "https", ...
};
```

### Why the extension schema can't express this

The extension schema already HAS a `modules` field:
```json
{ "name": "path", "modules": ["path"] }
```

But `ModuleResolver` doesn't query the extension registry. It uses the hardcoded list because module resolution happens before extensions are consulted.

### What would need to change

Change `ModuleResolver::isBuiltinModule()` to query `ExtensionRegistry::instance()`:

```cpp
bool ModuleResolver::isBuiltinModule(const std::string& name) {
    // Strip "node:" prefix
    std::string moduleName = name;
    if (moduleName.starts_with("node:")) moduleName = moduleName.substr(5);

    // Check extension registry
    auto& registry = ext::ExtensionRegistry::instance();
    return registry.hasModule(moduleName);
}
```

Add `hasModule()` to `ExtensionRegistry`:
```cpp
bool ExtensionRegistry::hasModule(const std::string& moduleName) const {
    for (const auto& contract : contracts_) {
        for (const auto& mod : contract.modules) {
            if (mod == moduleName) return true;
        }
    }
    return false;
}
```

**Prerequisite:** Extensions must be loaded before module resolution begins. Currently they are (Driver.cpp loads extensions early), so this should work.

**Recommendation:** Straightforward fix. Replace the hardcoded list with an extension registry query.

---

## Gap 5: fs.promises Nested Property Access

### What the hardcoded code does

`Analyzer_Expressions.cpp` (line 188) special-cases `fs.promises` as a nested property access to ensure it resolves to the promises namespace of the fs module.

### Why the extension schema can't express this

It CAN. The extension schema supports `nestedObjects`:
```json
"objects": {
  "fs": {
    "nestedObjects": {
      "promises": { "methods": { ... } }
    }
  }
}
```

This already works for `path.win32`, `path.posix`, etc.

### What would need to change

Nothing in the schema. The hardcoded code exists because `Analyzer_StdLib_FS.cpp` registers `fs.promises` before the extension system gets a chance to. Removing the hardcoded FS registration would let the extension JSON handle it.

**Recommendation:** No schema change needed. Remove `Analyzer_StdLib_FS.cpp` and ensure `fs.ext.json` defines the nested `promises` object.

---

## Gap 6: Class Inheritance Chains

### What the hardcoded code does

The Analyzer_StdLib_*.cpp files establish inheritance relationships:
```cpp
incomingMessageClass->baseClass = readableClass;
serverResponseClass->baseClass = writableClass;
socketClass->baseClass = duplexClass;
```

### Why the extension schema can't express this

The schema HAS an `extends` field:
```cpp
struct TypeDefinition {
    std::optional<TypeReference> extends;
};
```

And `registerTypesFromExtensions()` processes it (line ~1620):
```cpp
if (typeDef.extends) {
    auto baseType = symbols.lookupType(typeDef.extends->name);
    if (baseType) classType->baseClass = baseType;
}
```

The issue is **load ordering**: if the base class is defined in a different extension contract, it might not be loaded yet when the derived class is processed.

### What would need to change

Add a second pass to `registerTypesFromExtensions()` that resolves `extends` references after all types are registered:

```cpp
// Pass 1: Register all types
for (const auto& contract : contracts) {
    for (const auto& [name, def] : contract.types) {
        registerType(name, def);  // Without base class
    }
}

// Pass 2: Resolve inheritance
for (const auto& contract : contracts) {
    for (const auto& [name, def] : contract.types) {
        if (def.extends) resolveBaseClass(name, def.extends->name);
    }
}
```

**Recommendation:** Straightforward fix. Add a second pass for inheritance resolution.

---

## Gap 7: Event-Name-to-Callback-Type Mapping

### What the hardcoded code does

The `getExpectedCallbackType()` function recognizes `.on('data', callback)` and infers callback takes `(chunk: any)`. A more complete implementation would map event names to specific callback signatures:

| Event | Callback Signature |
|-------|-------------------|
| `'data'` | `(chunk: Buffer) => void` |
| `'error'` | `(err: Error) => void` |
| `'connection'` | `(socket: Socket) => void` |
| `'request'` | `(req: IncomingMessage, res: ServerResponse) => void` |

### Why the extension schema can't express this

There is no concept of "event signatures" in the schema. The `on()` method is defined as taking `(event: string, callback: function)` but there's no way to say "when event is 'data', callback has type X; when event is 'error', callback has type Y."

### What would need to change

Add an `events` map to `TypeDefinition`:

```cpp
struct EventSignature {
    std::vector<TypeReference> callbackParams;
};

struct TypeDefinition {
    // ... existing fields ...
    std::unordered_map<std::string, EventSignature> events;
};
```

JSON:
```json
"Server": {
  "kind": "class",
  "extends": "EventEmitter",
  "events": {
    "connection": { "callbackParams": ["Socket"] },
    "request": { "callbackParams": ["IncomingMessage", "ServerResponse"] },
    "error": { "callbackParams": ["Error"] }
  }
}
```

The analyzer would then look up the event name (from the string literal argument) and use the corresponding callback signature.

**Recommendation:** This is a genuinely useful schema extension. EventEmitter patterns are pervasive in Node.js and being able to declare per-event callback signatures would eliminate a large class of hardcoded special cases.

---

## Summary: Required Schema/Analyzer Changes

| Gap | Schema Change Needed | Analyzer Change Needed | Difficulty | Node.js-Specific? |
|-----|---------------------|----------------------|------------|-------------------|
| **Callback typing** | None (Option A) | Fix `convertExtTypeRef()` for function-typed params | Medium | Yes |
| **Promise inference** | None | Keep as-is (core language feature) | N/A | No (ECMAScript) |
| **Return type dispatch** | Optional inference rules | Remove Analyzer_StdLib_*.cpp first | Low-Medium | Mixed |
| **Builtin module list** | None | Query extension registry instead of hardcoded list | Low | Yes |
| **fs.promises** | None | Already supported by nestedObjects | Low | Yes |
| **Inheritance chains** | None | Add second pass for base class resolution | Low | Yes |
| **Event signatures** | Add `events` map to TypeDefinition | Lookup event name during callback inference | Medium | Yes |

### Minimum Changes to Remove All Analyzer_StdLib_*.cpp Files

1. Fix `convertExtTypeRef()` to build full `FunctionType` for callback parameters (Gap 1)
2. Add two-pass inheritance resolution in `registerTypesFromExtensions()` (Gap 6)
3. Replace `BUILTIN_MODULES` list with extension registry query (Gap 4)
4. Ensure all extension JSON files define complete type signatures including nested objects

### Minimum Changes to Remove getExpectedCallbackType()

1. Fix callback typing (Gap 1) -- handles http.createServer, net.createServer, fs callbacks
2. Add event signature map to schema (Gap 7) -- handles EventEmitter.on() patterns
3. OR: keep a small generic fallback that reads callback types from the method signature registered by extensions

### What Should Stay Hardcoded (Core Language, Not Node.js)

- Promise executor body scanning (Gap 2) -- ECMAScript built-in, imperative type inference algorithm that cannot be expressed declaratively
- Generic type inference for `Array.map`, `Array.filter` etc. -- core ECMAScript data structure methods with generic return types
- Type narrowing and control flow analysis -- core TypeScript type system features
