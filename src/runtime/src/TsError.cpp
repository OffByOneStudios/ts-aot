#include "TsError.h"
#include "TsString.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsHashTable.h"
#include "GC.h"
#include "TsNanBox.h"
#include <new>
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#elif defined(__linux__)
#include <execinfo.h>
#include <cxxabi.h>
#endif

#include "TsMap.h"
#include "TsObject.h"  // for ts_object_get_property, ts_value_get_object
#include "TsTyped.h"
// Type tags are enrolled in their headers (TsArray.h, TsMap.h, ...).

// Forward declarations — defined later in this file.
static void* getErrorConstructorByName(const char* name);
static void errSetProto(TsMap* err, const char* name);

// Helper to build stack trace and create error object
static TsValue* buildErrorObject(TsString* msgStr, void* options) {
    TsMap* err = TsMap::Create();
    err->Set(TsString::Create("message"), nanbox_to_tagged(ts_value_make_string(msgStr)));
    err->Set(TsString::Create("name"), nanbox_to_tagged(ts_value_make_string(TsString::Create("Error"))));
    // Brand as Error per [[ErrorData]] internal slot via @@toStringTag own
    // property (string-key convention). Spec says Object.prototype.toString
    // returns "[object Error]" for instances with [[ErrorData]].
    {
        TsValue tagKey; tagKey.type = ValueType::STRING_PTR;
        tagKey.ptr_val = TsString::GetInterned("[Symbol.toStringTag]");
        TsValue tagVal; tagVal.type = ValueType::STRING_PTR;
        tagVal.ptr_val = TsString::Create("Error");
        err->SetWithAttrs(tagKey, tagVal, TsHashTable::ATTR_CONFIGURABLE);
    }

    // ES2022: Handle options.cause
    if (options) {
        uint64_t optNb = (uint64_t)(uintptr_t)options;
        if (nanbox_is_ptr(optNb)) {
            void* optPtr = nanbox_to_ptr(optNb);
            if (optPtr) {
                // Check if it's a TsMap (validated, offset-derived tag).
                TsMap* optMap = ts_cast<TsMap>(optPtr);
                if (optMap) {
                    TsValue causeKey; causeKey.type = ValueType::STRING_PTR; causeKey.ptr_val = TsString::Create("cause");
                    TsValue causeVal = optMap->Get(causeKey);
                    if (causeVal.type != ValueType::UNDEFINED) {
                        err->Set(TsString::Create("cause"), causeVal);
                    }
                }
            }
        }
    }

    std::stringstream ss;
    ss << "Error: " << (msgStr ? msgStr->ToUtf8() : "") << "\n";

#ifdef _WIN32
    void* stack[64];
    unsigned short frames = CaptureStackBackTrace(1, 63, stack, NULL);

    HANDLE process = GetCurrentProcess();
    static bool symInitialized = false;
    if (!symInitialized) {
        SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
        if (!SymInitialize(process, NULL, TRUE)) {
            SymCleanup(process);
            SymInitialize(process, NULL, TRUE);
        }
        symInitialized = true;
    }

    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    IMAGEHLP_LINE64 line;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD displacement;

    for (unsigned short i = 0; i < frames; i++) {
        DWORD64 address = (DWORD64)stack[i];
        ss << "    at ";
        if (SymFromAddr(process, address, 0, symbol)) {
            ss << symbol->Name;
            if (SymGetLineFromAddr64(process, address - 1, &displacement, &line)) {
                ss << " (" << line.FileName << ":" << std::dec << line.LineNumber << ")";
            } else {
                ss << " (0x" << std::hex << address << ")";
            }
            ss << "\n";
        } else {
            ss << "0x" << std::hex << address << "\n";
        }
    }
#elif defined(__linux__)
    // backtrace() can crash (SIGABRT) in statically linked binaries when
    // DWARF unwind info is incomplete. Use a safe fallback.
    void* stack[64];
    int frames = 0;
    char** symbols = nullptr;

    // Only attempt backtrace if not statically linked (check for dynamic linker)
    // For static binaries, skip to avoid SIGABRT
    FILE* maps = fopen("/proc/self/maps", "r");
    bool is_dynamic = false;
    if (maps) {
        char line[512];
        while (fgets(line, sizeof(line), maps)) {
            if (strstr(line, "ld-linux") || strstr(line, "ld-musl")) {
                is_dynamic = true;
                break;
            }
        }
        fclose(maps);
    }

    if (is_dynamic) {
        frames = backtrace(stack, 63);
        symbols = backtrace_symbols(stack, frames);
    }

    for (int i = 1; i < frames; i++) {
        ss << "    at ";
        if (symbols && symbols[i]) {
            std::string sym(symbols[i]);
            size_t begin = sym.find('(');
            size_t end = sym.find('+', begin);
            if (begin != std::string::npos && end != std::string::npos) {
                std::string mangled = sym.substr(begin + 1, end - begin - 1);
                int status = 0;
                char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
                if (status == 0 && demangled) {
                    ss << demangled;
                    free(demangled);
                } else {
                    ss << mangled;
                }
                size_t addr_begin = sym.find('[');
                size_t addr_end = sym.find(']');
                if (addr_begin != std::string::npos && addr_end != std::string::npos) {
                    ss << " (" << sym.substr(addr_begin + 1, addr_end - addr_begin - 1) << ")";
                }
            } else {
                ss << sym;
            }
        } else {
            ss << "0x" << std::hex << (uintptr_t)stack[i];
        }
        ss << "\n";
    }
    if (symbols) free(symbols);
#else
    ss << "    at <stack trace not supported on this platform>\n";
#endif

    err->Set(TsString::Create("stack"), nanbox_to_tagged(ts_value_make_string(TsString::Create(ss.str().c_str()))));

    // Link to Error.prototype for `e instanceof Error`.
    errSetProto(err, "Error");

    // Return NaN-boxed pointer to the error map
    return (TsValue*)err;
}

// Build error object with a specific name (TypeError, RangeError, etc.)
static TsValue* buildTypedErrorObject(const char* name, TsString* msgStr) {
    TsMap* err = TsMap::Create();
    err->Set(TsString::Create("message"), nanbox_to_tagged(ts_value_make_string(msgStr)));
    err->Set(TsString::Create("name"), nanbox_to_tagged(ts_value_make_string(TsString::Create(name))));
    // Brand as Error per [[ErrorData]] internal slot.
    {
        TsValue tagKey; tagKey.type = ValueType::STRING_PTR;
        tagKey.ptr_val = TsString::GetInterned("[Symbol.toStringTag]");
        TsValue tagVal; tagVal.type = ValueType::STRING_PTR;
        tagVal.ptr_val = TsString::Create("Error");
        err->SetWithAttrs(tagKey, tagVal, TsHashTable::ATTR_CONFIGURABLE);
    }

    // Set .constructor to the matching global constructor so
    // `e.constructor === TypeError` works for assert.throws identity checks.
    void* ctor = getErrorConstructorByName(name);
    if (ctor) {
        TsValue ctorVal; ctorVal.type = ValueType::OBJECT_PTR; ctorVal.ptr_val = ctor;
        err->Set(TsString::Create("constructor"), ctorVal);
    }

    std::stringstream ss;
    ss << name << ": " << (msgStr ? msgStr->ToUtf8() : "") << "\n";

#ifdef _WIN32
    void* stack[64];
    unsigned short frames = CaptureStackBackTrace(1, 63, stack, NULL);
    HANDLE process = GetCurrentProcess();
    static bool symInitialized = false;
    if (!symInitialized) {
        SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
        if (!SymInitialize(process, NULL, TRUE)) {
            SymCleanup(process);
            SymInitialize(process, NULL, TRUE);
        }
        symInitialized = true;
    }
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    for (unsigned short i = 0; i < frames; i++) {
        DWORD64 address = (DWORD64)stack[i];
        ss << "    at ";
        if (SymFromAddr(process, address, 0, symbol)) {
            ss << symbol->Name << " (0x" << std::hex << address << std::dec << ")";
        } else {
            ss << "0x" << std::hex << address << std::dec;
        }
        ss << "\n";
    }
#endif

    err->Set(TsString::Create("stack"), nanbox_to_tagged(ts_value_make_string(TsString::Create(ss.str().c_str()))));

    // Link to <name>.prototype for `e instanceof TypeError` etc.
    errSetProto(err, name);

    return (TsValue*)err;
}

// Forward declarations for global constructor getters
extern "C" void* ts_get_global_Error();
extern "C" void* ts_get_global_TypeError();
extern "C" void* ts_get_global_RangeError();
extern "C" void* ts_get_global_ReferenceError();
extern "C" void* ts_get_global_SyntaxError();
extern "C" void* ts_get_global_URIError();
extern "C" void* ts_get_global_EvalError();

// Look up global error constructor by name
static void* getErrorConstructorByName(const char* name) {
    if (strcmp(name, "Error") == 0) return ts_get_global_Error();
    if (strcmp(name, "TypeError") == 0) return ts_get_global_TypeError();
    if (strcmp(name, "RangeError") == 0) return ts_get_global_RangeError();
    if (strcmp(name, "ReferenceError") == 0) return ts_get_global_ReferenceError();
    if (strcmp(name, "SyntaxError") == 0) return ts_get_global_SyntaxError();
    if (strcmp(name, "URIError") == 0) return ts_get_global_URIError();
    if (strcmp(name, "EvalError") == 0) return ts_get_global_EvalError();
    return ts_get_global_Error();
}

static void errSetProto(TsMap* err, const char* name) {
    void* ctor = getErrorConstructorByName(name);
    if (!ctor) return;
    void* ctorRaw = ts_value_get_object((TsValue*)ctor);
    if (!ctorRaw) return;
    TsValue* protoVal = ts_object_get_property(ctorRaw, "prototype");
    if (!protoVal) return;
    void* protoRaw = ts_value_get_object(protoVal);
    if (!protoRaw) return;
    // Only set prototype if it's a TsMap (magic at offset 16).
    if (!ts_is<TsMap>(protoRaw)) return;
    err->SetPrototype((TsMap*)protoRaw);
}

extern "C" {
    void* ts_error_create(void* message) {
        return buildErrorObject((TsString*)message, nullptr);
    }

    // ES2022: Error constructor with options { cause: ... }
    void* ts_error_create_with_options(void* message, void* options) {
        return buildErrorObject((TsString*)message, options);
    }

    void* ts_error_create_typed(const char* name, const char* message) {
        return buildTypedErrorObject(name, TsString::Create(message));
    }

    // Create a typed error from JS code: name and message are TsValue* (strings)
    // Sets .constructor to the matching global constructor for assert.throws compat
    void* ts_error_create_typed_js(void* nameVal, void* messageVal) {
        TsString* name = nullptr;
        TsString* msg = nullptr;

        if (nameVal) {
            void* raw = ts_value_get_string((TsValue*)nameVal);
            if (raw) name = (TsString*)raw;
        }
        if (messageVal) {
            void* raw = ts_value_get_string((TsValue*)messageVal);
            if (raw) msg = (TsString*)raw;
        }

        const char* nameStr = name ? name->ToUtf8() : "Error";
        TsValue* err = buildTypedErrorObject(nameStr, msg ? msg : TsString::Create(""));

        // Set .constructor to the global constructor for this error type
        void* ctor = getErrorConstructorByName(nameStr);
        if (ctor && err) {
            void* errRaw = ts_value_get_object(err);
            if (errRaw) {
                if (TsMap* errMap = ts_cast<TsMap>(errRaw)) {  // TsMap
                    TsValue ctorKey; ctorKey.type = ValueType::STRING_PTR;
                    ctorKey.ptr_val = TsString::GetInterned("constructor");
                    TsValue ctorVal; ctorVal.type = ValueType::OBJECT_PTR;
                    ctorVal.ptr_val = ctor;
                    errMap->Set(ctorKey, ctorVal);
                }
            }
        }

        return err;
    }

    // AggregateError(errors, message?) — ES2021. Constructs an Error-shaped
    // object with name="AggregateError", optional .message, and .errors set
    // to a fresh array containing each item from the iterable `errorsVal`.
    // Differs from ts_error_create_typed_js, which treats arg0 as message.
    void* ts_error_create_aggregate(void* errorsVal, void* messageVal) {
        TsString* msg = nullptr;
        if (messageVal) {
            void* raw = ts_value_get_string((TsValue*)messageVal);
            if (raw) msg = (TsString*)raw;
        }
        TsValue* err = buildTypedErrorObject("AggregateError",
            msg ? msg : TsString::Create(""));

        // Build .errors array. If errorsVal is already a TsArray, copy its
        // contents element-by-element. Other iterables would need full
        // iterator-protocol walking; we accept arrays as the common case.
        TsArray* errs = (TsArray*)ts_array_create();
        if (errorsVal) {
            void* rawSrc = ts_value_get_object((TsValue*)errorsVal);
            if (!rawSrc) rawSrc = errorsVal;
            if (TsArray* src = ts_cast<TsArray>(rawSrc)) {  // ARRY
                int64_t n = src->Length();
                for (int64_t i = 0; i < n; i++) errs->Push(src->Get(i));
            }
        }
        if (err) {
            void* errRaw = ts_value_get_object(err);
            if (errRaw) {
                if (TsMap* errMap = ts_cast<TsMap>(errRaw)) {  // TsMap
                    TsValue errsKey; errsKey.type = ValueType::STRING_PTR;
                    errsKey.ptr_val = TsString::GetInterned("errors");
                    TsValue errsValOut; errsValOut.type = ValueType::OBJECT_PTR;
                    errsValOut.ptr_val = errs;
                    errMap->Set(errsKey, errsValOut);
                    // .constructor → AggregateError ctor.
                    void* ctor = getErrorConstructorByName("AggregateError");
                    if (ctor) {
                        TsValue ctorKey; ctorKey.type = ValueType::STRING_PTR;
                        ctorKey.ptr_val = TsString::GetInterned("constructor");
                        TsValue ctorValOut; ctorValOut.type = ValueType::OBJECT_PTR;
                        ctorValOut.ptr_val = ctor;
                        errMap->Set(ctorKey, ctorValOut);
                    }
                }
            }
        }
        return err;
    }
}
