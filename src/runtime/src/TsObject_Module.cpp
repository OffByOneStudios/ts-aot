#include "TsObject_Internal.h"
#include "TsPromise.h"
#include "TsError.h"
#include <sstream>
#include <cctype>
#include <csetjmp>

// Module system / require() + builtin-module registration, extracted from
// TsObject.cpp. The require() cache (g_module_cache) and its GC scanner stay
// in TsObject.cpp; these functions reach the cache via the extern in the header.
extern "C" {


    void ts_module_register(TsValue* path, TsValue* exports) {
        TsString* s = (TsString*)ts_value_get_string(path);
        if (!s) return;
        std::string pathStr = s->ToUtf8();
        g_module_cache[pathStr] = exports;
    }

    TsValue* ts_module_get(const char* path) {
        std::string p = path;
        auto it = g_module_cache.find(p);
        if (it != g_module_cache.end()) {
            return it->second;
        }
        return nullptr;
    }

    // CommonJS `module` referenced from a function body: return the module
    // RECORD registered under this path (ts_module_get_cached returns its
    // .exports). Compiler-emitted by the visitIdentifier fallback.
    TsValue* ts_module_get_record(TsValue* path) {
        TsString* s = (TsString*)ts_value_get_string(path);
        if (!s) return ts_value_make_undefined();
        std::string pathStr = s->ToUtf8();
        TsValue* moduleObj = ts_module_get(pathStr.c_str());
        return moduleObj ? moduleObj : ts_value_make_undefined();
    }

    TsValue* ts_module_get_cached(TsValue* path) {
        TsString* s = (TsString*)ts_value_get_string(path);
        if (!s) return ts_value_make_undefined();
        std::string pathStr = s->ToUtf8();
        TsValue* moduleObj = ts_module_get(pathStr.c_str());
        if (!moduleObj) return ts_value_make_undefined();

        // Extract module.exports using ts_object_get_dynamic
        // which handles both flat objects and TsMaps
        TsString* exportsKey = TsString::Create("exports");
        TsValue* boxedKey = ts_value_make_string(exportsKey);
        TsValue* result = ts_object_get_dynamic(moduleObj, boxedKey);
        return result;
    }

    // Debug utility: dump all registered modules to stderr
    // Callable from CDB: `!call test_express_diag!ts_modules_dump_all`
    void ts_modules_dump_all() {
        fprintf(stderr, "[ts-aot] Module cache (%zu entries):\n", g_module_cache.size());
        for (auto& [key, val] : g_module_cache) {
            uint32_t magic0 = 0, magic16 = 0;
            void* raw = val ? ts_value_get_object((TsValue*)val) : nullptr;
            if (raw) {
                magic0 = *(uint32_t*)raw;
                magic16 = *(uint32_t*)((char*)raw + 16);
            }
            fprintf(stderr, "  [%p m0=%08X m16=%08X] %s\n",
                    (void*)val, magic0, magic16, key.c_str());
        }
        fflush(stderr);
    }

    TsValue* ts_module_get_default(TsValue* path) {
        TsValue* exports = ts_module_get_cached(path);
        if (ts_value_is_undefined(exports)) return exports;

        // Check __esModule flag (Babel/TypeScript CJS interop convention)
        // If exports.__esModule is truthy, return exports.default instead of exports
        TsString* esKey = TsString::Create("__esModule");
        TsValue* flag = ts_object_get_dynamic(exports, ts_value_make_string(esKey));
        if (!ts_value_is_undefined(flag) && !ts_value_is_null(flag)) {
            bool boolVal = ts_value_get_bool(flag);
            if (boolVal) {
                TsString* defKey = TsString::Create("default");
                return ts_object_get_dynamic(exports, ts_value_make_string(defKey));
            }
        }
        return exports;
    }

    // ============================================================
    // Built-in module require support for untyped JS modules.
    // When untyped JS does require('fs'), require('path'), etc.,
    // we create TsMap namespace objects with wrapped extension functions.
    // ============================================================

    // Generic thunk: forwards args to the real extension function.
    // The real function pointer is stored in closure->cells (repurposed, num_captures=0).
    static TsValue* builtin_fn_thunk(void* ctx, TsValue* a1, TsValue* a2, TsValue* a3) {
        TsClosure* cl = (TsClosure*)ctx;
        typedef void* (*ExtFn)(void*, void*, void*);
        void* result = ((ExtFn)(void*)cl->cells)((void*)a1, (void*)a2, (void*)a3);
        if (!result) return ts_value_make_undefined();
        return (TsValue*)result;
    }

    // Thunk for functions returning bool (e.g., fs.existsSync)
    static TsValue* builtin_bool_thunk(void* ctx, TsValue* a1, TsValue* a2, TsValue* a3) {
        TsClosure* cl = (TsClosure*)ctx;
        typedef bool (*ExtFn)(void*);
        bool result = ((ExtFn)(void*)cl->cells)((void*)a1);
        return ts_value_make_bool(result);
    }

    // Thunk for functions returning void (e.g., fs.unlinkSync)
    static TsValue* builtin_void_thunk(void* ctx, TsValue* a1, TsValue* a2, TsValue* a3) {
        TsClosure* cl = (TsClosure*)ctx;
        typedef void (*ExtFn)(void*, void*, void*);
        ((ExtFn)(void*)cl->cells)((void*)a1, (void*)a2, (void*)a3);
        return ts_value_make_undefined();
    }

    // Thunk for variadic functions (path.resolve, path.join).
    // Collects args into a TsArray, then calls the extension function.
    static TsValue* builtin_variadic_thunk(void* ctx, TsValue* a1, TsValue* a2, TsValue* a3) {
        TsClosure* cl = (TsClosure*)ctx;
        typedef void* (*ExtFn)(void*);
        TsArray* arr = TsArray::Create();
        if (a1 && !ts_value_is_undefined(a1)) arr->Push((int64_t)(uintptr_t)a1);
        if (a2 && !ts_value_is_undefined(a2)) arr->Push((int64_t)(uintptr_t)a2);
        if (a3 && !ts_value_is_undefined(a3)) arr->Push((int64_t)(uintptr_t)a3);
        void* result = ((ExtFn)(void*)cl->cells)((void*)arr);
        if (!result) return ts_value_make_undefined();
        return (TsValue*)result;
    }

    // Add a wrapped extension function to a module TsMap.
    static void add_builtin_fn(TsMap* mod, const char* name, void* real_fn, void* thunk_fn) {
        TsClosure* cl = TsClosure::Create(thunk_fn, 0);
        cl->cells = (TsCell**)real_fn;  // Repurpose cells to store real fn ptr
        cl->name = TsString::Create(name);
        TsString* key = TsString::Create(name);
        mod->Set(nanbox_to_tagged(ts_value_make_string(key)),
                 nanbox_to_tagged(ts_value_make_object(cl)));
    }

    // Add a string property to a module TsMap.
    static void add_builtin_str_prop(TsMap* mod, const char* name, const char* value) {
        TsString* key = TsString::Create(name);
        TsString* val = TsString::Create(value);
        mod->Set(nanbox_to_tagged(ts_value_make_string(key)),
                 nanbox_to_tagged(ts_value_make_string(val)));
    }

    // ---- Builtin function registry ----
    // Extensions register their functions here at static init time.
    // create_builtin_module looks them up instead of hard-referencing symbols,
    // so extensions that aren't linked don't create unresolved symbol errors.
    struct BuiltinFnEntry {
        void* fn_ptr;
        void* thunk_ptr;
    };

    // Key: "module\0function" (e.g., "fs\0readFileSync")
    static std::unordered_map<std::string, BuiltinFnEntry>& getBuiltinRegistry() {
        static std::unordered_map<std::string, BuiltinFnEntry> registry;
        return registry;
    }

    // Key: "module\0property" -> string value (e.g., "path\0sep" -> "\\")
    static std::unordered_map<std::string, std::string>& getBuiltinStrProps() {
        static std::unordered_map<std::string, std::string> props;
        return props;
    }

    // Key: "event_emitter.on" etc. for special runtime lookups
    static std::unordered_map<std::string, void*>& getBuiltinSpecialFns() {
        static std::unordered_map<std::string, void*> fns;
        return fns;
    }

    static std::string makeRegistryKey(const char* module, const char* name) {
        std::string key(module);
        key += '\0';
        key += name;
        return key;
    }

    // Post-init callbacks for builtin modules (e.g., events adds EventEmitter)
    static std::unordered_map<std::string, void(*)(void*)>& getBuiltinPostInits() {
        static std::unordered_map<std::string, void(*)(void*)> callbacks;
        return callbacks;
    }

    static TsValue* create_builtin_module(const std::string& name) {
        // Check g_module_cache for a previously created built-in module
        std::string cacheKey = "__builtin:" + name;
        TsValue* cached = ts_module_get(cacheKey.c_str());
        if (cached) return cached;

        TsMap* mod = TsMap::Create();

        // Look up all registered functions for this module
        auto& registry = getBuiltinRegistry();
        std::string prefix = name + '\0';
        for (auto& [key, entry] : registry) {
            if (key.size() > prefix.size() &&
                key.compare(0, prefix.size(), prefix) == 0) {
                const char* fnName = key.c_str() + prefix.size();
                add_builtin_fn(mod, fnName, entry.fn_ptr, entry.thunk_ptr);
            }
        }

        // Look up all registered string properties for this module
        auto& strProps = getBuiltinStrProps();
        for (auto& [key, value] : strProps) {
            if (key.size() > prefix.size() &&
                key.compare(0, prefix.size(), prefix) == 0) {
                const char* propName = key.c_str() + prefix.size();
                add_builtin_str_prop(mod, propName, value.c_str());
            }
        }

        // Module not registered at all: return empty TsMap (won't crash)

        // Set "default" and "exports" to point back to the module itself.
        // ESM `import crypto from 'crypto'` does a "default" lookup on the module.
        TsString* defaultKey = TsString::Create("default");
        mod->Set(nanbox_to_tagged(ts_value_make_string(defaultKey)),
                 nanbox_to_tagged(ts_value_make_object(mod)));
        TsString* exportsKey = TsString::Create("exports");
        mod->Set(nanbox_to_tagged(ts_value_make_string(exportsKey)),
                 nanbox_to_tagged(ts_value_make_object(mod)));

        // Call post-init callback if registered (e.g., events adds EventEmitter)
        auto& postInits = getBuiltinPostInits();
        auto postInitIt = postInits.find(name);
        if (postInitIt != postInits.end()) {
            postInitIt->second((void*)mod);
        }

        TsValue* result = (TsValue*)mod;
        // Cache in g_module_cache so the GC scanner keeps it alive
        TsValue* pathVal = ts_value_make_string(TsString::Create(cacheKey.c_str()));
        ts_module_register(pathVal, result);
        return result;
    }

// Expose create_builtin_module for TsGlobals.cpp module accessors
TsValue* ts_get_builtin_module(const char* name) {
    return create_builtin_module(std::string(name));
}

// ---- Public registration API for extensions ----
void ts_builtin_register(const char* module, const char* name, void* fn_ptr, int thunk_type) {
    static void* thunks[] = {
        (void*)builtin_fn_thunk,
        (void*)builtin_bool_thunk,
        (void*)builtin_void_thunk,
        (void*)builtin_variadic_thunk,
    };
    void* thunk = (thunk_type >= 0 && thunk_type <= 3) ? thunks[thunk_type] : thunks[0];
    auto key = makeRegistryKey(module, name);
    getBuiltinRegistry()[key] = {fn_ptr, thunk};
}

void ts_builtin_register_str_prop(const char* module, const char* name, const char* value) {
    auto key = makeRegistryKey(module, name);
    getBuiltinStrProps()[key] = value;
}

void ts_builtin_register_post_init(const char* module, void (*callback)(void*)) {
    getBuiltinPostInits()[module] = callback;
}

void ts_builtin_register_special(const char* name, void* fn_ptr) {
    getBuiltinSpecialFns()[name] = fn_ptr;
}

void* ts_builtin_lookup_special(const char* name) {
    auto& fns = getBuiltinSpecialFns();
    auto it = fns.find(name);
    return (it != fns.end()) ? it->second : nullptr;
}

    static bool is_builtin_module_name(const std::string& spec) {
        static const char* builtins[] = {
            "assert", "async_hooks", "buffer", "child_process", "cluster",
            "console", "crypto", "dgram", "dns", "events", "fs", "http",
            "http2", "https", "inspector", "module", "net", "os", "path",
            "perf_hooks", "process", "querystring", "readline", "stream",
            "string_decoder", "timers", "timers/promises", "tls", "tty",
            "url", "util", "zlib"
        };
        std::string s = spec;
        if (s.rfind("node:", 0) == 0) s = s.substr(5);
        for (const char* b : builtins) {
            if (s == b) return true;
        }
        return false;
    }

    TsValue* ts_require(TsValue* specifier, const char* referrerPath) {
        TsString* s = (TsString*)ts_value_get_string(specifier);
        if (!s) {
            return ts_value_make_undefined();
        }
        std::string spec = s->ToUtf8();

        // Strip "node:" prefix for built-in module lookup
        std::string lookupSpec = spec;
        if (lookupSpec.rfind("node:", 0) == 0) lookupSpec = lookupSpec.substr(5);

        // Check for built-in modules first (prevents crash on null referrerPath)
        if (is_builtin_module_name(lookupSpec)) {
            return create_builtin_module(lookupSpec);
        }

        // Guard against null referrerPath for non-builtin modules
        if (!referrerPath) {
            return ts_value_make_undefined();
        }

        try {
            fs::path resolved;
            std::string absPath;

            // Check for relative or absolute paths
            bool isRelative = spec.rfind("./", 0) == 0 || spec.rfind("../", 0) == 0 || spec.rfind("/", 0) == 0;
            // Windows absolute paths like "E:\..." or "C:/..."
            bool isAbsoluteWin = spec.size() >= 2 && std::isalpha((unsigned char)spec[0]) && (spec[1] == ':');
            if (isRelative) {
                resolved = fs::path(referrerPath).parent_path() / spec;
                absPath = finalize_module_path(resolved);
            } else if (isAbsoluteWin || (!spec.empty() && spec[0] == '/')) {
                // Already an absolute path
                absPath = finalize_module_path(spec);
            } else {
                absPath = resolve_node_module(spec, referrerPath);
            }

            if (absPath.empty()) {
                absPath = finalize_module_path(spec);
            }

            if (absPath.empty()) {
                return ts_value_make_undefined();
            }

            // Handle JSON files: read and parse at runtime
            if (absPath.size() >= 5 && absPath.substr(absPath.size() - 5) == ".json") {
                // Read the JSON file
                std::ifstream file(absPath);
                if (!file.is_open()) {
                    return ts_value_make_undefined();
                }
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string content = buffer.str();
                
                // Parse the JSON content
                TsString* jsonStr = TsString::Create(content.c_str());
                extern void* ts_json_parse(void* json_str);
                TsValue* parsed = (TsValue*)ts_json_parse(jsonStr);
                if (parsed) {
                    // Cache it for future requires
                    TsValue* pathVal = ts_value_make_string(TsString::Create(absPath.c_str()));
                    ts_module_register(pathVal, parsed);
                    return parsed;
                }
                return ts_value_make_undefined();
            }

            TsValue* moduleObj = ts_module_get(absPath.c_str());
            if (moduleObj) {
                // CommonJS: return module.exports
                uint64_t modNb = nanbox_from_tsvalue_ptr(moduleObj);
                if (nanbox_is_ptr(modNb)) {
                    void* rawMod = nanbox_to_ptr(modNb);
                    // Use inline map operations to get "exports" property
                    TsString* exportsStr = TsString::Create("exports");
                    uint64_t hash = (uint64_t)exportsStr;
                    int64_t bucket = __ts_map_find_bucket(rawMod, hash, (uint8_t)ValueType::STRING_PTR, (int64_t)exportsStr);
                    if (bucket >= 0) {
                        TsValue result;
                        __ts_map_get_value_at(rawMod, bucket, reinterpret_cast<uint8_t*>(&result.type), &result.i_val);
                        if (result.type != ValueType::UNDEFINED) {
                            return nanbox_from_tagged(result);
                        }

                        // CommonJS default: module.exports starts as {}
                        TsMap* exportsMap = TsMap::Create();
                        TsValue* exportsBoxed = ts_value_make_object(exportsMap);
                        TsValue exportsKey;
                        exportsKey.type = ValueType::STRING_PTR;
                        exportsKey.ptr_val = exportsStr;
                        ((TsMap*)rawMod)->Set(exportsKey, nanbox_to_tagged(exportsBoxed));
                        return exportsBoxed;
                    }

                    // No exports key at all: initialize to {}
                    TsMap* exportsMap = TsMap::Create();
                    TsValue* exportsBoxed = ts_value_make_object(exportsMap);
                    TsValue exportsKey;
                    exportsKey.type = ValueType::STRING_PTR;
                    exportsKey.ptr_val = exportsStr;
                    ((TsMap*)rawMod)->Set(exportsKey, nanbox_to_tagged(exportsBoxed));
                    return exportsBoxed;
                }
                return moduleObj;
            }
        } catch (const std::exception& e) {
            // Swallow errors in requires to keep parity with JS runtime behavior
        }
        
        return ts_value_make_undefined();
    }

    // ---- CONF-P3 Phase 2: memoized module-evaluation errors. A bundled
    // module whose top-level code THROWS (e.g. a dynamic-import fixture with
    // `throw new URIError()`) must not abort the program at startup — the
    // compiler wraps such (dynamically-reachable-only) inits in a catch that
    // records the error here; import() then rejects with the SAME error
    // object every time (spec: the evaluation error is memoized).
    void ts_module_set_init_error(TsValue* path, TsValue* err) {
        TsString* s = (TsString*)ts_value_get_string(path);
        if (!s) return;
        TsValue* record = ts_module_get(s->ToUtf8());
        if (!record) {
            TsMap* rec = TsMap::Create();
            record = ts_value_make_object(rec);
            ts_module_register(path, record);
        }
        TsString* ek = TsString::GetInterned("\x01initerr");
        ts_object_set_dynamic(record, ts_value_make_string(ek),
                              err ? err : ts_value_make_undefined());
    }

    // Returns the memoized evaluation error for a module record, or nullptr.
    TsValue* ts_module_get_init_error(TsValue* record) {
        if (!record) return nullptr;
        TsString* ek = TsString::GetInterned("\x01initerr");
        TsValue* e = ts_object_get_dynamic(record, ts_value_make_string(ek));
        if (!e || ts_value_is_undefined(e)) return nullptr;
        return e;
    }

    // Guarded ToString(specifier) for import(): runs user @@toPrimitive /
    // toString hooks; an abrupt completion is captured (not propagated) and
    // returned via *errOut so the caller can REJECT with it. No std::string
    // locals — ts_throw longjmps through this frame.
    static TsString* module_spec_to_string(TsValue* spec, TsValue** errOut) {
        *errOut = nullptr;
        if (!spec) return nullptr;
        extern void* ts_push_exception_handler();
        extern void ts_pop_exception_handler();
        extern TsValue* ts_get_exception();
        extern void ts_set_exception(TsValue* e);
        extern TsValue* ts_to_primitive(TsValue* val, int hint);
        // The ENTIRE conversion runs guarded: even ts_value_get_string
        // ToPrimitives object operands (user toString hooks can throw).
        void* handler = ts_push_exception_handler();
        jmp_buf* env = (jmp_buf*)handler;
        if (setjmp(*env) == 0) {
            TsString* out;
            void* sp = ts_value_get_string(spec);
            if (sp) {
                out = (TsString*)sp;
            } else {
                TsValue* prim = ts_to_primitive(spec, 2 /* string hint */);
                out = (TsString*)ts_string_from_value(prim);
            }
            ts_pop_exception_handler();
            return out;
        }
        ts_pop_exception_handler();
        *errOut = ts_get_exception();
        ts_set_exception(nullptr);
        return nullptr;
    }

    // ---- CONF-P3 Phase 1a: import(specifier) over the closed-world registry.
    // The Monomorphizer bundles every literal specifier it can see and rewrites
    // those to pre-resolved ts_dynamic_import calls; this handles EVERYTHING
    // else — any-expression specifiers resolve at runtime against the module
    // cache. Bundled modules are eagerly evaluated at startup, so a hit is a
    // resolved Promise of the exports; a miss REJECTS (host policy: only
    // compile-time-known modules are resolvable — ES HostLoadImportedModule
    // is host-defined). This function never ts_throws: every failure becomes
    // a rejection (the std::string locals here are safe — no longjmp exits).
    TsValue* ts_module_dynamic_import(TsValue* spec, TsValue* importerPath) {
        using namespace ts;   // TsPromise & the ts_promise_* C API live in ns ts
        TsPromise* promise = ts_promise_create();
        auto rejectWith = [&](const char* msg) -> TsValue* {
            ts_promise_reject_internal(promise,
                (TsValue*)ts_error_create_typed("TypeError", msg));
            return ts_value_make_object(promise);
        };
        // ToString(specifier) — runs user hooks; an abrupt completion becomes
        // a REJECTION with the thrown value (spec: IfAbruptRejectPromise).
        TsValue* toStrErr = nullptr;
        TsString* sstr = module_spec_to_string(spec, &toStrErr);
        if (toStrErr) {
            ts_promise_reject_internal(promise, toStrErr);
            return ts_value_make_object(promise);
        }
        const char* specC = sstr ? sstr->ToUtf8() : nullptr;
        if (!specC || !*specC) {
            return rejectWith("Failed to resolve module specifier");
        }
        std::string specStr = specC;
        if (specStr.rfind("node:", 0) == 0) specStr = specStr.substr(5);
        if (is_builtin_module_name(specStr)) {
            TsValue* mod = create_builtin_module(specStr);
            ts_promise_resolve_internal(promise, mod);
            return ts_value_make_object(promise);
        }
        // Canonicalize a relative/absolute file specifier against the
        // importing module's directory (mirrors the compile-time resolver).
        TsString* ip = importerPath ? (TsString*)ts_value_get_string(importerPath) : nullptr;
        const char* ipC = ip ? ip->ToUtf8() : nullptr;
        bool isRel = specStr.rfind("./", 0) == 0 || specStr.rfind("../", 0) == 0;
        bool isAbsWin = specStr.size() >= 2 &&
                        std::isalpha((unsigned char)specStr[0]) && specStr[1] == ':';
        bool isAbs = isAbsWin || (!specStr.empty() && specStr[0] == '/');
        std::string lookup;
        try {
            if (isRel && ipC) {
                lookup = finalize_module_path(fs::path(ipC).parent_path() / specStr);
            } else if (isAbs) {
                lookup = finalize_module_path(fs::path(specStr));
            }
        } catch (...) { lookup.clear(); }
        TsValue* record = nullptr;
        if (!lookup.empty()) record = ts_module_get(lookup.c_str());
        if (!record) record = ts_module_get(specStr.c_str());
        if (!record) {
            // Separator/case-insensitive fallback over the (small) cache —
            // compile-time keys come from the compiler's resolver and may
            // differ in slash direction or drive-letter case.
            auto norm = [](std::string v) {
                for (auto& c : v) { if (c == '/') c = '\\'; c = (char)std::tolower((unsigned char)c); }
                return v;
            };
            std::string want = norm(!lookup.empty() ? lookup : specStr);
            for (auto& kv : g_module_cache) {
                if (norm(kv.first) == want) { record = kv.second; break; }
            }
        }
        if (!record) {
            std::string msg = "Failed to resolve module specifier '" + specStr + "'";
            return rejectWith(msg.c_str());
        }
        // Memoized evaluation error: reject with the SAME error object.
        if (TsValue* initErr = ts_module_get_init_error(record)) {
            ts_promise_reject_internal(promise, initErr);
            return ts_value_make_object(promise);
        }
        // The cache holds the module RECORD ({exports: ...}); the import()
        // namespace is its exports object.
        TsString* ek = TsString::GetInterned("exports");
        TsValue* ns = ts_object_get_dynamic(record, ts_value_make_string(ek));
        if (!ns || ts_value_is_undefined(ns)) ns = record;
        ts_promise_resolve_internal(promise, ns);
        return ts_value_make_object(promise);
    }

}  // extern "C"
