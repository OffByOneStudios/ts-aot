#include "TsRuntime.h"
#include "TsObject.h"
#include "TsBoundFunction.h"
#include "TsString.h"
#include "TsConsString.h"
#include "TsBigInt.h"
#include "TsSymbol.h"
#include "TsError.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsSet.h"
#include "TsBuffer.h"  // for TsTypedArray instanceof dispatch
#include "TsFlatObject.h"  // for ShapeDescriptor / ts_shape_lookup in instanceof
#include "TsNanBox.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <map>
#include <string>
#include <cstdlib>
#include <cinttypes>

// Forward declaration: ts_to_primitive lives in TsObject.cpp. Called from
// ts_value_get_double below so binary ops on plain objects invoke
// user-defined valueOf/toString per ES5.1 §9.3.
extern "C" TsValue* ts_to_primitive(TsValue* val, int hint);

static std::map<std::string, std::chrono::steady_clock::time_point> consoleTimers;
static std::map<std::string, int64_t> consoleCounters;
static int consoleGroupDepth = 0;

// Helper to print indentation based on console group depth
static void printConsoleIndent(FILE* stream = stdout) {
    for (int i = 0; i < consoleGroupDepth; i++) {
        std::fprintf(stream, "  ");
    }
}

// Helper to print double in JavaScript style (no trailing zeros for whole numbers)
static void printJsNumber(FILE* stream, double val) {
    // Check for special values
    if (std::isnan(val)) {
        std::fprintf(stream, "NaN");
        return;
    }
    if (std::isinf(val)) {
        std::fprintf(stream, val > 0 ? "Infinity" : "-Infinity");
        return;
    }
    // If it's a whole number, print without decimals
    if (val == std::floor(val) && std::abs(val) < 1e15) {
        std::fprintf(stream, "%.0f", val);
    } else {
        // Use %g for compact representation (removes trailing zeros)
        std::fprintf(stream, "%g", val);
    }
}

extern "C" {

void ts_console_error(TsString* str) {
    printConsoleIndent(stderr);
    if (str) {
        std::fprintf(stderr, "%s\n", str->ToUtf8());
    } else {
        std::fprintf(stderr, "undefined\n");
    }
    std::fflush(stderr);
}

void ts_console_error_int(int64_t val) {
    printConsoleIndent(stderr);
    std::fprintf(stderr, "%lld\n", val);
    std::fflush(stderr);
}

void ts_console_error_double(double val) {
    printConsoleIndent(stderr);
    printJsNumber(stderr, val);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

void ts_console_error_bool(bool val) {
    printConsoleIndent(stderr);
    std::fprintf(stderr, "%s\n", val ? "true" : "false");
    std::fflush(stderr);
}

// Forward declaration - implemented after ts_console_log_bool
static void ts_console_print_value_to_stream(TsValue* val, FILE* stream);

void ts_console_error_value(TsValue* val) {
    printConsoleIndent(stderr);
    ts_console_print_value_to_stream(val, stderr);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

void ts_console_time(TsString* label) {
    std::string key = label ? label->ToUtf8() : "default";
    consoleTimers[key] = std::chrono::steady_clock::now();
}

void ts_console_time_end(TsString* label) {
    std::string key = label ? label->ToUtf8() : "default";
    auto it = consoleTimers.find(key);
    if (it != consoleTimers.end()) {
        auto end = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - it->second).count();
        std::printf("%s: %lldms\n", key.c_str(), (long long)diff);
        consoleTimers.erase(it);
    }
}

void ts_console_trace() {
    std::printf("Trace: (stack trace not yet implemented)\n");
}

void ts_console_time_log(TsString* label) {
    std::string key = label ? label->ToUtf8() : "default";
    auto it = consoleTimers.find(key);
    if (it != consoleTimers.end()) {
        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        std::printf("%s: %lldms\n", key.c_str(), (long long)diff);
    } else {
        std::printf("Timer '%s' does not exist\n", key.c_str());
    }
    std::fflush(stdout);
}

void ts_console_dir(TsValue* val) {
    // Forward declaration - use ts_console_print_value_to_stream
    ts_console_print_value_to_stream(val, stdout);
    std::printf("\n");
    std::fflush(stdout);
}

void ts_console_count(TsString* label) {
    std::string key = label ? label->ToUtf8() : "default";
    consoleCounters[key]++;
    std::printf("%s: %lld\n", key.c_str(), (long long)consoleCounters[key]);
    std::fflush(stdout);
}

void ts_console_count_reset(TsString* label) {
    std::string key = label ? label->ToUtf8() : "default";
    consoleCounters[key] = 0;
}

void ts_console_group(TsString* label) {
    // Print indent based on current depth
    for (int i = 0; i < consoleGroupDepth; i++) {
        std::printf("  ");
    }
    if (label) {
        std::printf("%s\n", label->ToUtf8());
    }
    consoleGroupDepth++;
    std::fflush(stdout);
}

void ts_console_group_collapsed(TsString* label) {
    // In a terminal, collapsed behaves the same as group
    // (collapsing is a browser DevTools feature)
    ts_console_group(label);
}

void ts_console_group_end() {
    if (consoleGroupDepth > 0) {
        consoleGroupDepth--;
    }
}

void ts_console_clear() {
    // Clear terminal screen using ANSI escape codes
    // Works on Windows 10+ and Unix terminals
    std::printf("\033[2J\033[H");
    std::fflush(stdout);
}

void ts_console_table(TsValue* data, TsValue* properties) {
    // console.table displays data in tabular format
    // For arrays of objects, show columns for each property
    // For arrays of primitives, show index and value columns
    // For objects, show key-value pairs

    if (!data) {
        std::printf("undefined\n");
        std::fflush(stdout);
        return;
    }

    printConsoleIndent();

    // Decode NaN-boxed data to find array or object
    TsArray* arr = nullptr;
    TsMap* obj = nullptr;

    uint64_t dataNb = nanbox_from_tsvalue_ptr(data);
    if (nanbox_is_ptr(dataNb)) {
        void* ptr = nanbox_to_ptr(dataNb);
        if (ptr) {
            uint32_t magic = *(uint32_t*)ptr;
            if (magic == 0x41525259) { // TsArray
                arr = (TsArray*)ptr;
            } else if (magic == 0x4D415053) { // TsMap
                obj = (TsMap*)ptr;
            } else {
                // Check offset 16 for TsMap
                uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
                if (magic16 == 0x4D415053) obj = (TsMap*)ptr;
            }
        }
    }

    if (arr) {
        int64_t len = arr->Length();
        std::printf("| (index) | Value |\n");
        std::printf("|---------|-------|\n");
        for (int64_t i = 0; i < len; i++) {
            int64_t rawElem = arr->Get(i);
            std::printf("| %lld | ", (long long)i);
            ts_console_print_value_to_stream((TsValue*)(uintptr_t)rawElem, stdout);
            std::printf(" |\n");
        }
    } else if (obj) {
        std::printf("| (key) | Value |\n");
        std::printf("|-------|-------|\n");
        TsArray* keys = (TsArray*)obj->GetKeys();
        if (keys) {
            int64_t len = keys->Length();
            for (int64_t i = 0; i < len; i++) {
                int64_t rawKey = keys->Get(i);
                // Decode NaN-boxed key to find string
                uint64_t keyNb = (uint64_t)rawKey;
                TsString* keyStr = nullptr;
                if (nanbox_is_ptr(keyNb)) {
                    void* kp = nanbox_to_ptr(keyNb);
                    if (kp && ts_is_any_string(kp))
                        keyStr = ts_ensure_flat(kp);
                }
                if (keyStr) {
                    // Build TsValue key for map lookup
                    TsValue keyVal;
                    keyVal.type = ValueType::STRING_PTR;
                    keyVal.ptr_val = keyStr;
                    TsValue val = obj->Get(keyVal);
                    std::printf("| %s | ", keyStr->ToUtf8());
                    // Convert TsValue struct back to NaN-boxed for printing
                    TsValue* printVal = nullptr;
                    if (val.type == ValueType::STRING_PTR && val.ptr_val)
                        printVal = (TsValue*)val.ptr_val;
                    else if (val.type == ValueType::NUMBER_INT)
                        printVal = nanbox_to_tsvalue_ptr(nanbox_int32((int32_t)val.i_val));
                    else if (val.type == ValueType::NUMBER_DBL)
                        printVal = nanbox_to_tsvalue_ptr(nanbox_double(val.d_val));
                    else if (val.type == ValueType::BOOLEAN)
                        printVal = nanbox_to_tsvalue_ptr(nanbox_bool(val.i_val != 0));
                    else if (val.type == ValueType::UNDEFINED)
                        printVal = nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
                    else if (val.type == ValueType::OBJECT_PTR)
                        printVal = val.ptr_val ? (TsValue*)val.ptr_val : nanbox_to_tsvalue_ptr(NANBOX_NULL);
                    else
                        printVal = nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
                    ts_console_print_value_to_stream(printVal, stdout);
                    std::printf(" |\n");
                }
            }
        }
    } else {
        ts_console_print_value_to_stream(data, stdout);
        std::printf("\n");
    }
    std::fflush(stdout);
}

// Forward declaration
bool ts_value_to_bool(TsValue* v);

void ts_console_assert(TsValue* condition, TsValue* data) {
    // If condition is falsy, print assertion failed
    if (!condition || !ts_value_to_bool(condition)) {
        printConsoleIndent(stderr);
        std::fprintf(stderr, "Assertion failed");
        if (data && !nanbox_is_undefined(nanbox_from_tsvalue_ptr(data))) {
            std::fprintf(stderr, ": ");
            ts_console_print_value_to_stream(data, stderr);
        }
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
    }
}

void ts_console_warn(TsValue* val) {
    // warn is same as error - outputs to stderr
    ts_console_error_value(val);
}

void ts_console_info(TsValue* val) {
    // info is same as log - outputs to stdout
    ts_console_log_value(val);
}

void ts_console_debug(TsValue* val) {
    // debug is same as log - outputs to stdout
    ts_console_log_value(val);
}

int32_t ts_double_to_int32(double d) {
    if (std::isnan(d) || std::isinf(d)) return 0;
    double i = std::trunc(std::fmod(d, 4294967296.0));
    if (i < 0) i += 4294967296.0;
    if (i >= 2147483648.0) i -= 4294967296.0;
    return (int32_t)i;
}

uint32_t ts_double_to_uint32(double d) {
    if (std::isnan(d) || std::isinf(d)) return 0;
    double i = std::trunc(std::fmod(d, 4294967296.0));
    if (i < 0) i += 4294967296.0;
    return (uint32_t)i;
}

void ts_console_log(TsString* str) {
    printConsoleIndent();
    if (str) {
        std::printf("%s\n", str->ToUtf8());
    } else {
        std::printf("undefined\n");
    }
    std::fflush(stdout);
}

void ts_console_log_int(int64_t val) {
    printConsoleIndent();
    std::printf("%lld\n", val);
    std::fflush(stdout);
}

void ts_console_log_double(double val) {
    printConsoleIndent();
    printJsNumber(stdout, val);
    std::printf("\n");
    std::fflush(stdout);
}

void ts_console_log_bool(bool val) {
    printConsoleIndent();
    std::printf("%s\n", val ? "true" : "false");
    std::fflush(stdout);
}

// Internal helper to print value to any stream (stdout or stderr)
static void ts_console_print_value_to_stream(TsValue* val, FILE* stream) {
    if (!val) {
        std::fprintf(stream, "undefined");
        return;
    }

    uint64_t nb = nanbox_from_tsvalue_ptr(val);

    if (nanbox_is_undefined(nb)) { std::fprintf(stream, "undefined"); return; }
    if (nanbox_is_null(nb))      { std::fprintf(stream, "null"); return; }
    if (nanbox_is_true(nb))      { std::fprintf(stream, "true"); return; }
    if (nanbox_is_false(nb))     { std::fprintf(stream, "false"); return; }

    if (nanbox_is_int32(nb)) {
        std::fprintf(stream, "%d", nanbox_to_int32(nb));
        return;
    }
    if (nanbox_is_double(nb)) {
        printJsNumber(stream, nanbox_to_double(nb));
        return;
    }

    // Must be a pointer
    if (!nanbox_is_ptr(nb)) { std::fprintf(stream, "undefined"); return; }
    void* ptr = nanbox_to_ptr(nb);
    if (!ptr) { std::fprintf(stream, "null"); return; }

    uint32_t magic = *(uint32_t*)ptr;

    if (magic == 0x53545247 || magic == TsConsString::MAGIC) { // TsString or TsConsString
        std::fprintf(stream, "%s", ts_ensure_flat(ptr)->ToUtf8());
        return;
    }
    if (magic == 0x42494749) { // TsBigInt
        std::fprintf(stream, "%sn", ((TsBigInt*)ptr)->ToString());
        return;
    }
    if (magic == 0x53594D42) { // TsSymbol
        TsSymbol* sym = (TsSymbol*)ptr;
        if (sym->description) {
            std::fprintf(stream, "Symbol(%s)", sym->description->ToUtf8());
        } else {
            std::fprintf(stream, "Symbol()");
        }
        return;
    }
    if (magic == 0x41525259) { // TsArray
        TsArray* arr = (TsArray*)ptr;
        int64_t len = arr->Length();
        std::fprintf(stream, "[ ");
        for (int64_t i = 0; i < len; i++) {
            if (i > 0) std::fprintf(stream, ", ");
            int64_t rawElem = arr->Get(i);
            // Array elements are NaN-boxed values stored as int64_t
            ts_console_print_value_to_stream((TsValue*)(uintptr_t)rawElem, stream);
        }
        std::fprintf(stream, " ]");
        return;
    }
    if (magic == 0x4D415053) { // TsMap - object
        std::fprintf(stream, "[object Object]");
        return;
    }
    if (magic == 0x53455453) { // TsSet
        std::fprintf(stream, "Set(%lld)", ((TsSet*)ptr)->Size());
        return;
    }
    if (magic == 0x464C4154) { // Flat object
        std::fprintf(stream, "[object Object]");
        return;
    }

    // Check magic at offset 16 for TsObject-derived types
    uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
    if (magic16 == 0x50524F4D) { // TsPromise
        std::fprintf(stream, "[object Promise]");
        return;
    }
    if (magic16 == 0x4D415053) { // TsMap at offset 16
        std::fprintf(stream, "[object Object]");
        return;
    }

    std::fprintf(stream, "[object Object]");
}

extern "C" void ts_console_log_value_no_newline(TsValue* val) {
    printConsoleIndent();
    ts_console_print_value_to_stream(val, stdout);
}

extern "C" void ts_console_log_value(TsValue* val) {
    printConsoleIndent();
    ts_console_print_value_to_stream(val, stdout);
    std::printf("\n");
    std::fflush(stdout);
}

// Helper: convert a NaN-boxed value to its string representation for console formatting
static void appendValueToBuffer(TsValue* val, std::string& out) {
    if (!val) { out += "undefined"; return; }
    uint64_t nb = nanbox_from_tsvalue_ptr(val);
    if (nanbox_is_undefined(nb)) { out += "undefined"; return; }
    if (nanbox_is_null(nb))      { out += "null"; return; }
    if (nanbox_is_true(nb))      { out += "true"; return; }
    if (nanbox_is_false(nb))     { out += "false"; return; }
    if (nanbox_is_int32(nb)) {
        out += std::to_string(nanbox_to_int32(nb));
        return;
    }
    if (nanbox_is_double(nb)) {
        double d = nanbox_to_double(nb);
        if (std::isnan(d)) { out += "NaN"; return; }
        if (std::isinf(d)) { out += d > 0 ? "Infinity" : "-Infinity"; return; }
        // Use JS-style number formatting
        char buf[64];
        if (d == (double)(int64_t)d && std::abs(d) < 1e15) {
            std::snprintf(buf, sizeof(buf), "%" PRId64, (int64_t)d);
        } else {
            std::snprintf(buf, sizeof(buf), "%.17g", d);
            // Trim trailing zeros after decimal point
            char* dot = strchr(buf, '.');
            if (dot) {
                char* end = buf + strlen(buf) - 1;
                while (end > dot && *end == '0') *end-- = '\0';
                if (end == dot) *end = '\0';
            }
        }
        out += buf;
        return;
    }
    if (!nanbox_is_ptr(nb)) { out += "undefined"; return; }
    void* ptr = nanbox_to_ptr(nb);
    if (!ptr) { out += "null"; return; }
    uint32_t magic = *(uint32_t*)ptr;
    if (magic == 0x53545247 || magic == TsConsString::MAGIC) { out += ts_ensure_flat(ptr)->ToUtf8(); return; }
    if (magic == 0x42494749) { out += ((TsBigInt*)ptr)->ToString(); out += "n"; return; }
    if (magic == 0x41525259 || magic == 0x4D415053 || magic == 0x464C4154) {
        // For arrays/objects, print via stream helper to a temp buffer
        // Simple approach: use the existing stream printer
        out += "[object Object]";
        if (magic == 0x41525259) {
            out.resize(out.size() - strlen("[object Object]"));
            TsArray* arr = (TsArray*)ptr;
            int64_t len = arr->Length();
            for (int64_t i = 0; i < len; i++) {
                if (i > 0) out += ",";
                int64_t rawElem = arr->Get(i);
                appendValueToBuffer((TsValue*)(uintptr_t)rawElem, out);
            }
        }
        return;
    }
    out += "[object Object]";
}

// console.log with multiple args: handles util.format-style %s/%d/%f substitution
extern "C" void ts_console_log_args(void** args, int32_t argc) {
    if (argc <= 0) {
        printConsoleIndent();
        std::printf("\n");
        std::fflush(stdout);
        return;
    }
    if (argc == 1) {
        printConsoleIndent();
        ts_console_print_value_to_stream((TsValue*)args[0], stdout);
        std::printf("\n");
        std::fflush(stdout);
        return;
    }

    // Check if first arg is a string (potential format string)
    TsValue* firstArg = (TsValue*)args[0];
    uint64_t nb0 = nanbox_from_tsvalue_ptr(firstArg);
    bool firstIsString = false;
    const char* fmtStr = nullptr;
    if (nanbox_is_ptr(nb0)) {
        void* ptr = nanbox_to_ptr(nb0);
        if (ptr && ts_is_any_string(ptr)) {
            firstIsString = true;
            fmtStr = ts_ensure_flat(ptr)->ToUtf8();
        }
    }

    std::string result;
    int argIndex = 1; // start from second arg

    if (firstIsString && fmtStr) {
        size_t len = strlen(fmtStr);
        for (size_t i = 0; i < len; i++) {
            if (fmtStr[i] == '%' && i + 1 < len) {
                char spec = fmtStr[i + 1];
                if (spec == 's' && argIndex < argc) {
                    appendValueToBuffer((TsValue*)args[argIndex++], result);
                    i++;
                } else if ((spec == 'd' || spec == 'i') && argIndex < argc) {
                    TsValue* val = (TsValue*)args[argIndex++];
                    uint64_t vnb = nanbox_from_tsvalue_ptr(val);
                    if (nanbox_is_int32(vnb)) result += std::to_string(nanbox_to_int32(vnb));
                    else if (nanbox_is_double(vnb)) result += std::to_string((int64_t)nanbox_to_double(vnb));
                    else result += "NaN";
                    i++;
                } else if (spec == 'f' && argIndex < argc) {
                    TsValue* val = (TsValue*)args[argIndex++];
                    uint64_t vnb = nanbox_from_tsvalue_ptr(val);
                    double d = 0;
                    if (nanbox_is_double(vnb)) d = nanbox_to_double(vnb);
                    else if (nanbox_is_int32(vnb)) d = (double)nanbox_to_int32(vnb);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%g", d);
                    result += buf;
                    i++;
                } else if ((spec == 'o' || spec == 'O' || spec == 'j') && argIndex < argc) {
                    appendValueToBuffer((TsValue*)args[argIndex++], result);
                    i++;
                } else if (spec == '%') {
                    result += '%';
                    i++;
                } else {
                    result += fmtStr[i];
                }
            } else {
                result += fmtStr[i];
            }
        }
    } else {
        // First arg is not a string, just print all args space-separated
        appendValueToBuffer(firstArg, result);
    }

    // Append remaining args separated by spaces
    while (argIndex < argc) {
        result += ' ';
        appendValueToBuffer((TsValue*)args[argIndex++], result);
    }

    printConsoleIndent();
    std::printf("%s\n", result.c_str());
    std::fflush(stdout);
}

// console.error with multiple args (same format logic, outputs to stderr)
extern "C" void ts_console_error_args(void** args, int32_t argc) {
    if (argc <= 0) {
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
        return;
    }
    if (argc == 1) {
        printConsoleIndent();
        ts_console_print_value_to_stream((TsValue*)args[0], stderr);
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
        return;
    }

    // Check if first arg is a string (potential format string)
    TsValue* firstArg = (TsValue*)args[0];
    uint64_t nb0 = nanbox_from_tsvalue_ptr(firstArg);
    bool firstIsString = false;
    const char* fmtStr = nullptr;
    if (nanbox_is_ptr(nb0)) {
        void* ptr = nanbox_to_ptr(nb0);
        if (ptr && ts_is_any_string(ptr)) {
            firstIsString = true;
            fmtStr = ts_ensure_flat(ptr)->ToUtf8();
        }
    }

    std::string result;
    int argIndex = 1;

    if (firstIsString && fmtStr) {
        size_t len = strlen(fmtStr);
        for (size_t i = 0; i < len; i++) {
            if (fmtStr[i] == '%' && i + 1 < len) {
                char spec = fmtStr[i + 1];
                if (spec == 's' && argIndex < argc) {
                    appendValueToBuffer((TsValue*)args[argIndex++], result);
                    i++;
                } else if ((spec == 'd' || spec == 'i') && argIndex < argc) {
                    TsValue* val = (TsValue*)args[argIndex++];
                    uint64_t vnb = nanbox_from_tsvalue_ptr(val);
                    if (nanbox_is_int32(vnb)) result += std::to_string(nanbox_to_int32(vnb));
                    else if (nanbox_is_double(vnb)) result += std::to_string((int64_t)nanbox_to_double(vnb));
                    else result += "NaN";
                    i++;
                } else if (spec == 'f' && argIndex < argc) {
                    TsValue* val = (TsValue*)args[argIndex++];
                    uint64_t vnb = nanbox_from_tsvalue_ptr(val);
                    double d = 0;
                    if (nanbox_is_double(vnb)) d = nanbox_to_double(vnb);
                    else if (nanbox_is_int32(vnb)) d = (double)nanbox_to_int32(vnb);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%g", d);
                    result += buf;
                    i++;
                } else if ((spec == 'o' || spec == 'O' || spec == 'j') && argIndex < argc) {
                    appendValueToBuffer((TsValue*)args[argIndex++], result);
                    i++;
                } else if (spec == '%') {
                    result += '%';
                    i++;
                } else {
                    result += fmtStr[i];
                }
            } else {
                result += fmtStr[i];
            }
        }
    } else {
        appendValueToBuffer(firstArg, result);
    }

    while (argIndex < argc) {
        result += ' ';
        appendValueToBuffer((TsValue*)args[argIndex++], result);
    }

    printConsoleIndent();
    std::fprintf(stderr, "%s\n", result.c_str());
    std::fflush(stderr);
}

TsString* ts_typeof(void* val) {
    if (!val) return TsString::Create("undefined");

    uint64_t nb = (uint64_t)(uintptr_t)val;

    if (nanbox_is_undefined(nb)) return TsString::Create("undefined");
    if (nanbox_is_null(nb))      return TsString::Create("object"); // typeof null === "object"
    if (nanbox_is_bool(nb))      return TsString::Create("boolean");
    if (nanbox_is_int32(nb))     return TsString::Create("number");
    if (nanbox_is_double(nb))    return TsString::Create("number");

    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return TsString::Create("object");

        uint32_t magic = *(uint32_t*)ptr;
        if (magic == 0x53545247 || magic == TsConsString::MAGIC) return TsString::Create("string"); // TsString/TsConsString
        if (magic == 0x42494749) return TsString::Create("bigint");   // TsBigInt
        if (magic == 0x53594D42) return TsString::Create("symbol");   // TsSymbol
        if (magic == 0x41525259) return TsString::Create("object");   // TsArray
        if (magic == 0x4D415053) return TsString::Create("object");   // TsMap
        if (magic == 0x46554E43) {
            // Annex B § B.3.7: typeof on an [[IsHTMLDDA]] object yields
            // "undefined" (legacy DOM document.all).
            if (((TsFunction*)ptr)->is_htmldda) return TsString::Create("undefined");
            return TsString::Create("function"); // TsFunction
        }
        if (magic == 0x464C4154) return TsString::Create("object");   // Flat object

        // Check offset 16 for TsObject-derived types
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 == 0x46554E43) {
            // Annex B § B.3.7: typeof on an [[IsHTMLDDA]] object yields
            // "undefined" (legacy DOM document.all). For TsFunction
            // pointers stored without the TsObject prefix prefix,
            // is_htmldda lives at the same TsFunction layout.
            if (((TsFunction*)ptr)->is_htmldda) return TsString::Create("undefined");
            return TsString::Create("function"); // TsFunction at offset 16
        }
        if (magic16 == 0x434C5352) return TsString::Create("function"); // TsClosure at offset 16
        if (magic16 == 0x4D415053) return TsString::Create("object");   // TsMap at offset 16
        // Legacy wrapped forms (parity with the retired ts_value_typeof
        // ladder, which this function now serves as the single engine for).
        if (magic16 == 0x53594D42) return TsString::Create("symbol");   // wrapped TsSymbol
        if (magic16 == 0x42494749) return TsString::Create("bigint");   // wrapped TsBigInt
        {
            uint32_t magic8 = *(uint32_t*)((char*)ptr + 8);
            if (magic8 == 0x46554E43) return TsString::Create("function");
        }

        return TsString::Create("object");
    }

    return TsString::Create("undefined");
}

// ES Type(v) is Object — the CANONICAL check. Pointer-shaped primitives
// (strings, rope strings, symbols, bigints — magic at OFFSET 0) are NOT
// Objects; ~20 open-coded copies of this brand-exclusion list drifted
// (instanceof missed CONS, Iterator.concat missed BIGI). Use this.
bool ts_value_is_object(TsValue* v) {
    if (!v) return false;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (!nanbox_is_ptr(nb)) return false;
    void* p = nanbox_to_ptr(nb);
    if (!p || (uintptr_t)p < 0x1000) return false;
    uint32_t m0 = *(uint32_t*)p;
    return m0 != 0x53545247 /*STRG*/ && m0 != 0x434F4E53 /*CONS*/ &&
           m0 != 0x53594D42 /*SYMB*/ && m0 != 0x42494749 /*BIGI*/;
}

bool ts_value_is_nullish(TsValue* v) {
    if (!v) return true;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    return nanbox_is_undefined(nb) || nanbox_is_null(nb);
}

// JavaScript-spec-compliant instanceof for dynamic constructors.
// Checks: is constructor.prototype in obj's prototype chain?
// Used when the compiler doesn't know the class vtable at compile time
// (e.g., `this instanceof Layer` where Layer is loaded via require()).
// Table of (magic, offset, global-getter) for built-in class instances
// that need `x instanceof Ctor` to succeed. For each entry, if the LHS has
// `magic` at `offset`, compare the target prototype (from Ctor.prototype)
// against the global's `.prototype`. This avoids setting up a TsMap-style
// prototype chain on each built-in instance at construction time.
extern "C" void* ts_get_global_Date();
extern "C" void* ts_get_global_RegExp();
extern "C" void* ts_get_global_Map();
extern "C" void* ts_get_global_Set();
extern "C" void* ts_get_global_WeakMap();
extern "C" void* ts_get_global_WeakSet();
extern "C" void* ts_get_global_Promise();
extern "C" void* ts_get_global_TypedArray();
extern "C" void* ts_get_global_Int8Array();
extern "C" void* ts_get_global_Uint8Array();
extern "C" void* ts_get_global_Uint8ClampedArray();
extern "C" void* ts_get_global_Int16Array();
extern "C" void* ts_get_global_Uint16Array();
extern "C" void* ts_get_global_Int32Array();
extern "C" void* ts_get_global_Uint32Array();
extern "C" void* ts_get_global_Float32Array();
extern "C" void* ts_get_global_Float64Array();
extern "C" void* ts_get_global_BigInt64Array();
extern "C" void* ts_get_global_BigUint64Array();

namespace {
struct BuiltinInstanceCheck {
    uint32_t magic;
    int offset;
    void* (*get_global)();
};
extern "C" void* ts_temporal_get_plaintime_ctor();  // TsTemporal.h
extern "C" void* ts_temporal_get_duration_ctor();   // TsTemporal.h
extern "C" void* ts_temporal_get_plaindate_ctor();  // TsTemporal.h
extern "C" void* ts_temporal_get_plainyearmonth_ctor();
extern "C" void* ts_temporal_get_plainmonthday_ctor();
extern "C" void* ts_temporal_get_plaindatetime_ctor();
extern "C" void* ts_temporal_get_instant_ctor();
extern "C" void* ts_temporal_get_zoneddatetime_ctor();
static const BuiltinInstanceCheck g_builtin_checks[] = {
    { 0x44415445, 0,  ts_get_global_Date    },   // TsDate     "DATE"
    { 0x52454758, 0,  ts_get_global_RegExp  },   // TsRegExp   "REGX"
    { 0x50524F4D, 16, ts_get_global_Promise },   // TsPromise "PROM" (TsObject subclass: magic @16)
    { 0x53455453, 16, ts_get_global_Set     },   // TsSet      "SETS"
    { 0x574D4150, 16, ts_get_global_WeakMap },   // TsWeakMap  "WMAP"
    { 0x57534554, 16, ts_get_global_WeakSet },   // TsWeakSet  "WSET"
    { 0x504C5449, 16, ts_temporal_get_plaintime_ctor }, // TsPlainTime "PLTI"
    { 0x54445552, 16, ts_temporal_get_duration_ctor },  // TsDuration "TDUR"
    { 0x504C4454, 16, ts_temporal_get_plaindate_ctor }, // TsPlainDate "PLDT"
    { 0x504C594D, 16, ts_temporal_get_plainyearmonth_ctor }, // PlainYearMonth
    { 0x504C4D44, 16, ts_temporal_get_plainmonthday_ctor }, // PlainMonthDay
    { 0x50444D54, 16, ts_temporal_get_plaindatetime_ctor }, // PlainDateTime
    { 0x494E5354, 16, ts_temporal_get_instant_ctor }, // Instant
    { 0x5A44544D, 16, ts_temporal_get_zoneddatetime_ctor }, // ZonedDateTime
};
} // namespace

// The ordinary prototype-chain walk shared by InstanceofOperator and
// OrdinaryHasInstance: does `rawObj`'s [[Prototype]] chain contain
// `targetProto`? Handles flat class instances, TsMap objects, builtin
// instances (Date/Set/...), typed arrays, and callables.
static bool ts_proto_chain_has(void* rawObj, void* targetProto);

extern "C" TsValue* ts_bound_function_call(void* ctx, int argc, TsValue** argv);
bool ts_instanceof_dynamic(TsValue* obj, TsValue* constructor);  // defined below (same extern "C" region)
extern "C" TsValue* ts_fn_hasInstance_native(void* ctx, int argc, TsValue** argv);

// 7.3.19 OrdinaryHasInstance(C, O) — the InstanceofOperator semantics WITHOUT
// the @@hasInstance dispatch (this IS the default @@hasInstance behavior).
// Abrupt Get(C, "prototype") propagates via ts_throw.
extern "C" bool ts_ordinary_has_instance(TsValue* C, TsValue* O) {
    if (!C) return false;
    extern bool ts_is_callable(void* val);
    if (!ts_is_callable((void*)C)) return false;
    // Step 2: bound function -> InstanceofOperator(O, [[BoundTargetFunction]]).
    {
        void* rawC0 = ts_value_get_object(C);
        if (rawC0) {
            uint32_t m16 = *(uint32_t*)((char*)rawC0 + 16);
            if (m16 == 0x46554E43 /*FUNC*/) {
                TsFunction* f = (TsFunction*)rawC0;
                if (f->funcPtr == (void*)ts_bound_function_call && f->context) {
                    TsBoundFunction* b = (TsBoundFunction*)f->context;
                    if (b->targetFunction)
                        return ts_instanceof_dynamic(O, b->targetFunction);
                }
            }
        }
    }
    // Step 3: if O is not an object, return false.
    if (!O) return false;
    // Step 3: if Type(O) is not Object, return false. Canonical check —
    // the old open-coded list omitted CONS, so a rope string LHS was
    // treated as an object.
    if (!ts_value_is_object(O)) return false;
    void* rawObj = nanbox_to_ptr(nanbox_from_tsvalue_ptr(O));
    // Step 4: P = Get(C, "prototype") — abrupt completions PROPAGATE.
    void* rawC = ts_value_get_object(C);
    if (!rawC) rawC = (void*)C;
    TsValue* protoVal = ts_object_get_property(rawC, "prototype");
    // Step 5: if Type(P) is not Object, throw TypeError.
    void* targetProto = nullptr;
    if (protoVal && !ts_value_is_undefined(protoVal) &&
        ts_value_is_object(protoVal)) {
        targetProto = nanbox_to_ptr(nanbox_from_tsvalue_ptr(protoVal));
    }
    if (!targetProto) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Function has non-object prototype in instanceof check"));
        return false;  // unreachable
    }
    return ts_proto_chain_has(rawObj, targetProto);
}

// The DEFAULT Function.prototype[@@hasInstance] (ES 20.2.3.6): returns
// OrdinaryHasInstance(this, V). Installed on Function.prototype by
// ts_get_global_Function; ts_instanceof_dynamic recognizes it by funcPtr and
// skips the dispatch (its behavior IS the ordinary walk).
extern "C" TsValue* ts_fn_hasInstance_native(void* ctx, int argc, TsValue** argv) {
    TsValue* C = (TsValue*)ctx;
    // ctx pointing back at this wrapper itself means "no explicit receiver".
    if (C) {
        void* raw = ts_value_get_object(C);
        if (raw) {
            uint32_t m16 = *(uint32_t*)((char*)raw + 16);
            if (m16 == 0x46554E43 &&
                ((TsFunction*)raw)->funcPtr == (void*)ts_fn_hasInstance_native)
                C = nullptr;
        }
    }
    TsValue* V = (argc > 0 && argv) ? argv[0] : nullptr;
    return ts_value_make_bool(C && ts_ordinary_has_instance(C, V));
}

bool ts_instanceof_dynamic(TsValue* obj, TsValue* constructor) {
    if (!obj || !constructor) return false;

    // Get constructor.prototype
    uint64_t ctorNb = nanbox_from_tsvalue_ptr(constructor);
    if (!nanbox_is_ptr(ctorNb)) return false;
    void* ctorPtr = nanbox_to_ptr(ctorNb);
    if (!ctorPtr) return false;

    // ECMA-262 13.10.2 InstanceofOperator(V, target): if target has a
    // @@hasInstance method, return ToBoolean(Call(method, target, <V>)). This
    // runs BEFORE the obj null/ptr checks below so primitive operands like
    // `0 instanceof C` still reach a custom handler. Well-known symbols are
    // stored under the literal "[Symbol.xxx]" property key (see
    // ts_symbol_storage_key in TsObject.cpp). We only divert when the handler
    // is callable; ordinary constructors have no @@hasInstance and fall through
    // to the prototype-chain walk (we do not install the default
    // Function.prototype[@@hasInstance]).
    {
        TsValue* hiVal = ts_object_get_property(ctorPtr, "[Symbol.hasInstance]");
        if (hiVal && !ts_value_is_undefined(hiVal)) {
            uint64_t hiNb = nanbox_from_tsvalue_ptr(hiVal);
            if (nanbox_is_ptr(hiNb)) {
                void* hiPtr = nanbox_to_ptr(hiNb);
                if (hiPtr) {
                    uint32_t hm = *(uint32_t*)((char*)hiPtr + 16);
                    // Skip the DEFAULT Function.prototype[@@hasInstance]
                    // (installed on the shared prototype, so every function
                    // "has" it): its behavior IS the ordinary walk below, and
                    // dispatching would recurse.
                    bool isDefault = false;
                    if (hm == 0x46554E43) {
                        TsFunction* hf = (TsFunction*)hiPtr;
                        if (hf->funcPtr == (void*)ts_fn_hasInstance_native) isDefault = true;
                    }
                    if (!isDefault &&
                        (hm == 0x434C5352 /*TsClosure 'CLSR'*/ ||
                         hm == 0x46554E43 /*TsFunction 'FUNC'*/)) {
                        TsValue* hiArgs[1] = { obj };
                        TsValue* r = ts_function_call_with_this(hiVal, constructor, 1, hiArgs);
                        return ts_value_to_bool(r);
                    }
                }
            }
        }
    }

    // Get .prototype from the constructor
    TsValue* protoVal = ts_object_get_property(ctorPtr, "prototype");
    if (!protoVal || ts_value_is_undefined(protoVal)) return false;
    uint64_t protoNb = nanbox_from_tsvalue_ptr(protoVal);
    if (!nanbox_is_ptr(protoNb)) return false;
    void* targetProto = nanbox_to_ptr(protoNb);
    if (!targetProto) return false;

    // Get obj's raw pointer
    uint64_t objNb = nanbox_from_tsvalue_ptr(obj);
    if (!nanbox_is_ptr(objNb)) return false;
    void* rawObj = nanbox_to_ptr(objNb);
    if (!rawObj) return false;

    return ts_proto_chain_has(rawObj, targetProto);
}

static bool ts_proto_chain_has(void* rawObj, void* targetProto) {

    // Check magic to find prototype chain
    uint32_t magic0 = *(uint32_t*)rawObj;
    uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);

    // Subclass-of-builtin instance (Set/Map/Array/... allocated by
    // ts_subclass_builtin_alloc): its [[Prototype]] is recorded in the
    // native side map — walk from there (proto -> proto via TsMap chain).
    {
        extern void* ts_native_object_get_proto(void* obj);
        void* p = ts_native_object_get_proto(rawObj);
        int depth = 0;
        while (p && depth < 100) {
            if (p == targetProto) return true;
            uint32_t pm16 = ((uintptr_t)p >= 4096)
                ? *(uint32_t*)((char*)p + 16) : 0;
            if (pm16 != 0x4D415053) break;
            p = ((TsMap*)p)->GetPrototype();
            depth++;
        }
    }

    // Flat-object class instance: walk via ShapeDescriptor::constructorSlot
    // → constructor.prototype → ... up the chain. The prototype map is a
    // TsMap whose GetPrototype() returns Base.prototype (set by
    // emitDeferredStaticInits for `class Derived extends Base`).
    if (magic0 == 0x464C4154) {
        uint32_t shapeId = flat_object_shape_id(rawObj);
        ShapeDescriptor* desc = ts_shape_lookup(shapeId);
        if (desc && desc->constructorSlot) {
            TsValue* ctorVal = *(TsValue**)desc->constructorSlot;
            if (ctorVal) {
                TsValue* protoVal = ts_object_get_property((void*)ctorVal, "prototype");
                if (protoVal) {
                    uint64_t pNb = nanbox_from_tsvalue_ptr(protoVal);
                    if (nanbox_is_ptr(pNb)) {
                        void* protoRaw = nanbox_to_ptr(pNb);
                        // Walk: instance.[[Prototype]] = C.prototype; then
                        // C.prototype.[[Prototype]] = Base.prototype; ...
                        int depth = 0;
                        while (protoRaw && depth < 100) {
                            if (protoRaw == targetProto) return true;
                            uint32_t pm = *(uint32_t*)((char*)protoRaw + 16);
                            if (pm != 0x4D415053) break;
                            TsMap* pm_map = (TsMap*)protoRaw;
                            TsMap* next = pm_map->GetPrototype();
                            protoRaw = next;
                            depth++;
                        }
                    }
                }
            }
        }
        return false;
    }

    if (magic16 == 0x4D415053) { // TsMap (user class, JS Map, error instance)
        TsMap* map = (TsMap*)rawObj;
        // A real Map instance (`new Map()`) carries the IsExplicitMap brand;
        // plain object literals are also TsMap but do NOT. `m instanceof Map`
        // was false (Map is not in g_builtin_checks, and adding it there would
        // wrongly match `{} instanceof Map`). Brand-check against Map.prototype.
        if (map->IsExplicitMap()) {
            void* g = ts_get_global_Map();
            if (g) {
                TsValue* gp = ts_object_get_property(g, "prototype");
                if (gp) {
                    uint64_t pnb = nanbox_from_tsvalue_ptr(gp);
                    if (nanbox_is_ptr(pnb) && nanbox_to_ptr(pnb) == targetProto) return true;
                }
            }
        }
        TsMap* proto = map->GetPrototype();
        int depth = 0;
        while (proto && depth < 100) {
            if ((void*)proto == targetProto) return true;
            proto = proto->GetPrototype();
            depth++;
        }
    }

    // Built-in class instance (TsDate, TsRegExp, TsPromise, TsSet, etc.).
    // They don't carry TsMap-style prototype chains; check magic and
    // compare against the corresponding global's .prototype.
    for (const auto& entry : g_builtin_checks) {
        uint32_t m = *(uint32_t*)((char*)rawObj + entry.offset);
        if (m != entry.magic) continue;
        void* g = entry.get_global();
        if (!g) return false;
        TsValue* gproto = ts_object_get_property(g, "prototype");
        if (!gproto) return false;
        uint64_t pnb = nanbox_from_tsvalue_ptr(gproto);
        if (!nanbox_is_ptr(pnb)) return false;
        return nanbox_to_ptr(pnb) == targetProto;
    }

    // TsTypedArray instance: single magic for all nine TA kinds. `x instanceof
    // %TypedArray%` is true for any TA; `x instanceof Int8Array` is true only
    // if x's element type matches Int8. Dispatch on GetType().
    if (*(uint32_t*)((char*)rawObj + 16) == TsTypedArray::MAGIC) {
        // Accept %TypedArray% as parent of all kinds.
        void* ta_parent = ts_get_global_TypedArray();
        if (ta_parent) {
            TsValue* p = ts_object_get_property(ta_parent, "prototype");
            if (p) {
                uint64_t pnb = nanbox_from_tsvalue_ptr(p);
                if (nanbox_is_ptr(pnb) && nanbox_to_ptr(pnb) == targetProto) {
                    return true;
                }
            }
        }
        // Match the specific per-kind constructor.
        TsTypedArray* ta = (TsTypedArray*)rawObj;
        void* (*kindGlobal)() = nullptr;
        switch (ta->GetType()) {
            case TypedArrayType::Int8:         kindGlobal = ts_get_global_Int8Array;         break;
            case TypedArrayType::Uint8:        kindGlobal = ts_get_global_Uint8Array;        break;
            case TypedArrayType::Uint8Clamped: kindGlobal = ts_get_global_Uint8ClampedArray; break;
            case TypedArrayType::Int16:        kindGlobal = ts_get_global_Int16Array;        break;
            case TypedArrayType::Uint16:       kindGlobal = ts_get_global_Uint16Array;       break;
            case TypedArrayType::Int32:        kindGlobal = ts_get_global_Int32Array;        break;
            case TypedArrayType::Uint32:       kindGlobal = ts_get_global_Uint32Array;       break;
            case TypedArrayType::Float32:      kindGlobal = ts_get_global_Float32Array;      break;
            case TypedArrayType::Float64:      kindGlobal = ts_get_global_Float64Array;      break;
            case TypedArrayType::BigInt64:     kindGlobal = ts_get_global_BigInt64Array;     break;
            case TypedArrayType::BigUint64:    kindGlobal = ts_get_global_BigUint64Array;    break;
            default: return false;
        }
        if (kindGlobal) {
            void* g = kindGlobal();
            if (g) {
                TsValue* p = ts_object_get_property(g, "prototype");
                if (p) {
                    uint64_t pnb = nanbox_from_tsvalue_ptr(p);
                    if (nanbox_is_ptr(pnb) && nanbox_to_ptr(pnb) == targetProto) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Function / closure values: `fn instanceof Function` and (transitively)
    // `fn instanceof Object` must be true. We don't keep an explicit
    // [[Prototype]] = Function.prototype chain on callables, so match the
    // requested constructor's .prototype directly against Function.prototype
    // and Object.prototype. (`fn instanceof Foo` for a user Foo stays false —
    // Foo's .prototype is neither of those.) lodash's isEqual constructor
    // check exercises `ctor instanceof ctor`/`Function`; without this, callable
    // operands mis-compared.
    {
        uint32_t fm0  = *(uint32_t*)rawObj;
        uint32_t fm16 = *(uint32_t*)((char*)rawObj + 16);
        bool isCallable = (fm16 == 0x46554E43 /*FUNC*/ || fm16 == 0x434C5352 /*CLSR*/ ||
                           fm0  == 0x46554E43          || fm0  == 0x434C5352);
        if (isCallable) {
            extern void* ts_get_global_Function();
            extern void* ts_get_global_Object();
            void* gs[2] = { ts_get_global_Function(), ts_get_global_Object() };
            for (void* g : gs) {
                if (!g) continue;
                TsValue* p = ts_object_get_property(g, "prototype");
                if (!p) continue;
                uint64_t pnb = nanbox_from_tsvalue_ptr(p);
                if (nanbox_is_ptr(pnb) && nanbox_to_ptr(pnb) == targetProto) return true;
            }
            return false;
        }
    }

    return false;
}
bool ts_instanceof(void* obj, void* targetVTable) {
    if (!obj || !targetVTable) return false;

    uint64_t nb = (uint64_t)(uintptr_t)obj;
    if (!nanbox_is_ptr(nb)) return false; // Non-pointer NaN-boxed values are never instances
    obj = nanbox_to_ptr(nb);
    if (!obj) return false;

    // Check for TsString magic number at offset 0
    uint32_t magic0 = *(uint32_t*)obj;
    if (magic0 == 0x53545247 || magic0 == TsConsString::MAGIC) return false; // Strings are not instances of classes

    // Check for flat object (FLAT_MAGIC = 0x464C4154)
    if (magic0 == 0x464C4154) {
        // vtable is at offset 8
        void** vtablePtr = *(void***)((char*)obj + 8);
        if (!vtablePtr) return false;  // Object literal, not a class instance
        // Direct match
        if (vtablePtr == targetVTable) return true;
        // Traverse parent chain
        void** current = vtablePtr;
        int depth = 0;
        while (current && depth < 100) {
            if (current == targetVTable) return true;
            void* parent = current[0];
            if (!parent || parent == current) break;
            current = (void**)parent;
            depth++;
        }
        return false;
    }

    // Check magic at offset 16 to detect TsObject-derived classes
    // TsObject layout: [C++ vtable (8), void* vtable (8), uint32_t magic (4), ...]
    uint32_t magic16 = *(uint32_t*)((char*)obj + 16);
    bool isTsObjectDerived = (magic16 == 0x4D415053 ||  // TsMap "MAPS"
                              magic16 == 0x46554E43 ||  // TsFunction "FUNC"
                              magic16 == 0x574D4150 ||  // TsWeakMap "WMAP"
                              magic16 == 0x57534554);   // TsWeakSet "WSET"

    if (isTsObjectDerived) {
        // TsObject-derived classes: TypeScript vtable is at offset 8
        void** vptr8 = *(void***)((char*)obj + 8);
        if (!vptr8) return false;

        uintptr_t vptr8_val = (uintptr_t)vptr8;
        if (vptr8_val < 0x1000 || vptr8_val > 0x00007FFFFFFFFFFF) return false;

        // Direct match
        if (vptr8 == targetVTable) return true;

        // Traverse parent chain
        void** currentVTable = vptr8;
        int depth = 0;
        while (currentVTable && depth < 100) {
            if (currentVTable == targetVTable) return true;
            void* parent = currentVTable[0];
            if (!parent || parent == currentVTable) break;
            currentVTable = (void**)parent;
            depth++;
        }
        return false;
    }

    // User-defined classes: TypeScript vtable at offset 0 OR offset 8 (for TsMap-backed objects without magic)
    // First check offset 8 (HIR objects use TsMap without setting magic)
    void** vptr8 = *(void***)((char*)obj + 8);
    if (vptr8) {
        uintptr_t vptr8_val = (uintptr_t)vptr8;
        if (vptr8_val > 0x1000 && vptr8_val < 0x00007FFFFFFFFFFF) {
            // Check if vptr8 points to something that looks like a vtable (not a string)
            uint32_t vptr8Magic = *(uint32_t*)vptr8;
            if (vptr8Magic != 0x53545247 && vptr8Magic != TsConsString::MAGIC) { // Not a TsString/TsConsString
                // Direct match
                if (vptr8 == targetVTable) return true;

                // Traverse parent chain
                void** currentVTable = vptr8;
                int depth = 0;
                while (currentVTable && depth < 100) {
                    if (currentVTable == targetVTable) return true;
                    void* parent = currentVTable[0];
                    if (!parent || parent == currentVTable) break;
                    // Check if parent is a valid pointer
                    uintptr_t parentVal = (uintptr_t)parent;
                    if (parentVal < 0x1000 || parentVal > 0x00007FFFFFFFFFFF) break;
                    currentVTable = (void**)parent;
                    depth++;
                }
            }
        }
    }

    // Then check offset 0 (non-HIR user-defined classes)
    void** vptr0 = *(void***)obj;
    if (vptr0) {
        uintptr_t vptr0_val = (uintptr_t)vptr0;
        if (vptr0_val > 0x1000 && vptr0_val < 0x00007FFFFFFFFFFF) {
            // Check if vptr0 matches target directly
            if (vptr0 == targetVTable) return true;

            // Traverse from vptr0 (user-defined class vtable)
            void** currentVTable = vptr0;
            int depth = 0;
            while (currentVTable && depth < 100) {
                if (currentVTable == targetVTable) return true;
                void* parent = currentVTable[0];
                if (!parent || parent == currentVTable) break;
                // Check if parent is a valid pointer in data range
                uintptr_t parentVal = (uintptr_t)parent;
                if (parentVal < 0x1000 || parentVal > 0x00007FFFFFFFFFFF) break;
                // Sanity check: don't read magic if parent looks like garbage
                currentVTable = (void**)parent;
                depth++;
            }
        }
    }

    return false;
}

int64_t ts_value_get_int(TsValue* v) {
    if (!v) return 0;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    return nanbox_to_int64(nb);
}

// ECMA-262 7.1.4.1 StringToNumber on a trimmed-able UTF-8 string. Handles the
// pieces bare strtod gets wrong: "Infinity" (exact case, optional sign) and the
// NonDecimalIntegerLiteral prefixes 0b/0o/0x; rejects strtod's lowercase
// "inf"/"infinity"/"nan" spellings (invalid in JS) while still mapping decimal
// overflow ("1e400") to Infinity. Empty/all-whitespace -> +0.
static double js_string_to_number(const char* utf8) {
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    if (!utf8) return 0.0;
    auto isws = [](char c){ return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; };
    const char* s = utf8;
    while (*s && isws(*s)) s++;
    if (*s == '\0') return 0.0;
    const char* e = s; while (*e) e++;
    while (e > s && isws(e[-1])) e--;
    size_t n = (size_t)(e - s);
    // Infinity (optional leading sign)
    {
        const char* p = s; double sign = 1.0;
        if (*p=='+'||*p=='-'){ if(*p=='-') sign=-1.0; p++; }
        if ((size_t)(e-p)==8 && std::strncmp(p,"Infinity",8)==0)
            return sign*std::numeric_limits<double>::infinity();
    }
    // NonDecimalIntegerLiteral 0b/0o/0x (no sign permitted)
    if (n>=2 && s[0]=='0') {
        int base=0; char c1=s[1];
        if (c1=='b'||c1=='B') base=2;
        else if (c1=='o'||c1=='O') base=8;
        else if (c1=='x'||c1=='X') base=16;
        if (base) {
            if (e==s+2) return NaN;
            double val=0.0;
            for (const char* p=s+2;p<e;p++){
                int dig; char c=*p;
                if (c>='0'&&c<='9') dig=c-'0';
                else if (c>='a'&&c<='f') dig=c-'a'+10;
                else if (c>='A'&&c<='F') dig=c-'A'+10;
                else return NaN;
                if (dig>=base) return NaN;
                val=val*base+dig;
            }
            return val;
        }
    }
    // StrDecimalLiteral: must start with a digit or '.', else it's a stray
    // alpha token ("inf"/"nan"/...) that JS rejects.
    {
        const char* p = s;
        if (*p=='+'||*p=='-') p++;
        if (!((*p>='0'&&*p<='9')||*p=='.')) return NaN;
    }
    char* end=nullptr;
    double d=std::strtod(s,&end);
    if (end != e) return NaN;     // trailing non-numeric content
    if (d != d) return NaN;       // canonicalize any NaN
    return d;
}

double ts_value_get_double(TsValue* v) {
    if (!v) return 0.0;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (nanbox_is_int32(nb)) return (double)nanbox_to_int32(nb);
    if (nanbox_is_double(nb)) return nanbox_to_double(nb);
    if (nanbox_is_bool(nb)) return nanbox_to_bool(nb) ? 1.0 : 0.0;
    // ECMA-262 ToNumber(null) = 0, ToNumber(undefined) = NaN. Check
    // these before the generic pointer branch so that undefined
    // doesn't dereference a non-pointer.
    if (nb == NANBOX_NULL) return 0.0;
    if (nb == NANBOX_UNDEFINED) return std::numeric_limits<double>::quiet_NaN();
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return 0.0;
        // Raw TsBigInt (magic at offset 0): numeric value via the truncated
        // i64. ToPrimitive now returns BigInts unchanged (they ARE
        // primitives), so this unboxing helper must handle them directly —
        // it previously received the stringified form and parsed it. The
        // BigInt-TA store paths (BigInt64Array ctor/fill/element writes)
        // unbox through here.
        if (*(uint32_t*)ptr == 0x42494749) {
            extern int64_t ts_bigint_to_i64(void* bi);
            return (double)ts_bigint_to_i64(ptr);
        }
        if (ts_is_unchecked<TsString>(ptr) || ts_is_unchecked<TsConsString>(ptr)) {
            // ECMA-262 §7.1.4.1 StringToNumber: empty or whitespace-only
            // strings convert to +0, not NaN. std::stod throws on empty
            // input which we'd otherwise turn into NaN — wrong per spec.
            // Trim ASCII whitespace and reject trailing non-whitespace
            // characters to match StrUnsignedDecimalLiteral semantics.
            // ECMA-262 7.1.4.1 StringToNumber (handles Infinity, 0b/0o/0x,
            // empty -> +0, and canonicalizes NaN — the latter fixes the
            // `_.toNumber('-NaN')` AV where non-canonical NaN bits alias the
            // NaN-box tag space).
            return js_string_to_number(ts_ensure_flat(ptr)->ToUtf8());
        }
        // ES5.1 §9.3 ToNumber on an object: call ToPrimitive with hint
        // "number", which invokes user-defined valueOf/toString. If that
        // produces a different primitive, recurse on it. This is the hot
        // path hit by binary arith/comparison on plain objects since the
        // compiler lowers `any + num` etc. as ts_value_get_double + fadd.
        TsValue* coerced = ts_to_primitive(v, 1 /* hint: number */);
        if (coerced != v) {
            return ts_value_get_double(coerced);
        }
    }
    return 0.0;
}

int64_t ts_parseInt(void* value) {
    if (!value) return 0;
    uint64_t nb = (uint64_t)(uintptr_t)value;
    if (nanbox_is_int32(nb)) return (int64_t)nanbox_to_int32(nb);
    if (nanbox_is_double(nb)) return (int64_t)nanbox_to_double(nb);
    if (nanbox_is_bool(nb)) return nanbox_to_bool(nb) ? 1 : 0;
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return 0;
        if (ts_is_unchecked<TsString>(ptr) || ts_is_unchecked<TsConsString>(ptr)) {
            const char* s = ts_ensure_flat(ptr)->ToUtf8();
            if (!s) return 0;
            char* end = nullptr;
            long long v = std::strtoll(s, &end, 10);
            (void)end;
            return (int64_t)v;
        }
    }
    return 0;
}

bool ts_value_to_bool(TsValue* v) {
    if (!v) return false;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (nanbox_is_undefined(nb) || nanbox_is_null(nb)) return false;
    if (nanbox_is_false(nb)) return false;
    if (nanbox_is_true(nb)) return true;
    if (nanbox_is_int32(nb)) return nanbox_to_int32(nb) != 0;
    if (nanbox_is_double(nb)) {
        double d = nanbox_to_double(nb);
        return d != 0.0 && !std::isnan(d);
    }
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return false;
        if (ts_is_unchecked<TsString>(ptr) || ts_is_unchecked<TsConsString>(ptr)) {
            return ts_string_like_length(ptr) > 0; // Empty string is falsy
        }
        // Annex B § B.3.7: [[IsHTMLDDA]] objects coerce to false.
        if (ts_is_htmldda(v)) return false;
        return true; // Non-null objects are truthy
    }
    return false;
}

TsValue* ts_value_strict_eq(TsValue* lhs, TsValue* rhs);

TsValue* ts_value_strict_eq_wrapper(TsValue* lhs, TsValue* rhs) {
    return ts_value_strict_eq(lhs, rhs);
}

// JavaScript Number() coercion: converts any value to a double.
// Implements the ToNumber abstract operation (ECMA-262 7.1.4).
double ts_to_number(TsValue* v) {
    if (!v) return 0.0;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (nanbox_is_undefined(nb)) return std::numeric_limits<double>::quiet_NaN();
    if (nanbox_is_null(nb)) return 0.0;
    if (nanbox_is_true(nb)) return 1.0;
    if (nanbox_is_false(nb)) return 0.0;
    if (nanbox_is_int32(nb)) return (double)nanbox_to_int32(nb);
    if (nanbox_is_double(nb)) return nanbox_to_double(nb);
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return 0.0;
        uint32_t magic = *(uint32_t*)ptr;  // also used for the Symbol check below
        if (ts_is_unchecked<TsString>(ptr) || ts_is_unchecked<TsConsString>(ptr)) {
            // ECMA-262 7.1.4.1 StringToNumber (shared with ts_value_get_double).
            return js_string_to_number(ts_ensure_flat(ptr)->ToUtf8());
        }
        // Raw TsBigInt: numeric value via the truncated i64 (legacy-compat;
        // spec ToNumber(BigInt) is a TypeError, but the pre-primitive
        // behavior was numeric-via-string-parse and internal callers rely
        // on it). Operator sites throw the mix TypeError BEFORE reaching
        // ToNumber, so spec-visible arithmetic is unaffected.
        if (magic == 0x42494749) {  // "BIGI"
            extern int64_t ts_bigint_to_i64(void* bi);
            return (double)ts_bigint_to_i64(ptr);
        }
        // Per ES spec: ToNumber(symbol) throws TypeError.
        // TsSymbol has MAGIC=0x53594D42 at offset 0 (see TsSymbol.h).
        if (magic == 0x53594D42) {  // "SYMB"
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert a Symbol value to a number"));
            return 0.0;  // unreachable
        }
        // Objects: coerce via ToPrimitive(hint: number), then retry as primitive.
        // ts_to_primitive throws TypeError if neither valueOf nor toString
        // returns a primitive (spec-correct after Part A of this batch).
        TsValue* coerced = ts_to_primitive(v, 1);
        if (coerced != v) return ts_to_number(coerced);
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// ToIntegerOrInfinity for an index argument (Array/String .at, etc).
// Routes through ts_to_number so a Symbol index throws TypeError, and
// throws TypeError on a BigInt index (ToNumber(BigInt) is a TypeError per
// ECMA-262 7.1.4). Returns a truncated i64 (NaN/±0 -> 0, ±Inf saturates).
// Shared by both the runtime prototype natives and the compiler fast-path
// `_coerced` entry points so a non-coercible index always throws.
int64_t ts_to_index_integer(TsValue* v) {
    if (!v) return 0;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (nanbox_is_undefined(nb)) return 0;  // ToInteger(undefined) -> NaN -> 0
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (ptr && *(uint32_t*)ptr == 0x42494749) {  // TsBigInt magic
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert a BigInt value to a number"));
            return 0;  // unreachable
        }
    }
    double d = ts_to_number(v);  // throws TypeError on Symbol
    if (d != d || d == 0) return 0;  // NaN / ±0 -> 0
    if (std::isinf(d)) return d > 0 ? INT64_MAX : INT64_MIN;
    return (int64_t)d;  // truncate toward zero
}

// Like ts_to_index_integer, but preserves the "argument not provided" / undefined
// sentinel (INT64_MIN) that the array/string index-taking natives rely on for
// their default-value logic (e.g. fill/copyWithin/slice "use length"). A present
// Symbol/BigInt/throwing-valueOf index still throws TypeError. Used by the
// compiler fast-path `_coerced` entry points where an omitted optional argument
// arrives as a null pointer.
int64_t ts_to_index_integer_or_sentinel(TsValue* v) {
    if (!v) return INT64_MIN;  // argument not provided
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (nanbox_is_undefined(nb)) return INT64_MIN;  // ToInteger(undefined) -> default
    return ts_to_index_integer(v);  // may throw on Symbol/BigInt
}

// ToNumber for an index/position argument that must throw on a Symbol/BigInt/
// throwing-valueOf value but otherwise returns the raw double so callers that
// need +/-Infinity semantics (Array indexOf/lastIndexOf fromIndex) keep them.
// Returns the default when the argument is omitted (null) or undefined.
double ts_to_index_number_or(TsValue* v, double deflt) {
    if (!v) return deflt;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (nanbox_is_undefined(nb)) return deflt;
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (ptr && *(uint32_t*)ptr == 0x42494749) {  // TsBigInt magic
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert a BigInt value to a number"));
            return 0;  // unreachable
        }
    }
    return ts_to_number(v);  // throws TypeError on Symbol
}

// Helper: native function that returns globalThis when called
static TsValue* ts_return_globalThis_native(void* ctx, int argc, TsValue** argv) {
    extern TsValue* globalThis;
    return globalThis;
}

static TsValue* ts_noop_undefined_native(void* ctx, int argc, TsValue** argv) {
    return ts_value_make_undefined();
}

// First-class typed-array constructor getters (TsGlobals.cpp,
// DEFINE_TYPED_ARRAY_CTOR) — file scope: linkage specs can't be local.
extern "C" void* ts_get_global_Int8Array();
extern "C" void* ts_get_global_Uint8Array();
extern "C" void* ts_get_global_Uint8ClampedArray();
extern "C" void* ts_get_global_Int16Array();
extern "C" void* ts_get_global_Uint16Array();
extern "C" void* ts_get_global_Int32Array();
extern "C" void* ts_get_global_Uint32Array();
extern "C" void* ts_get_global_Float32Array();
extern "C" void* ts_get_global_Float64Array();
extern "C" void* ts_get_global_BigInt64Array();
extern "C" void* ts_get_global_BigUint64Array();

// Returns the value smuggled through ctx (used by the subclass idiom below;
// ctx is a boxed GLOBAL constructor, rooted for program lifetime).
static TsValue* ts_return_ctx_value_native(void* ctx, int argc, TsValue** argv) {
    return (TsValue*)ctx;
}

// Runtime-linked parser boundary (src/interp/TsParse.cpp, EVAL-001).
extern "C" void* ts_parse_program(const char* source, const char* file_name,
                                  int as_module);
extern "C" const char* ts_parse_error(void* handle);
extern "C" void ts_parse_free(void* handle);

// Function(source) — AOT pattern-matcher (no interpreter tier). Exact-match
// the safe literal idioms; throw EvalError for real dynamic source, the same
// class of error a CSP-restricted host throws. A silent wrong function
// (the old behavior: EVERY source returned a globalThis-returner) is a
// production hazard — loud beats lucky.
void* ts_function_constructor_stub(TsValue* body) {
    const char* s = nullptr;
    if (body) {
        void* raw = ts_value_get_string(body);
        if (raw) s = ((TsString*)raw)->ToUtf8();
    }
    // EVAL-001 Phase 0 smoke hook (env-gated, no default behavior change):
    // proves the runtime-linked parser (src/interp/TsParse.cpp) parses and
    // reports errors end-to-end. Phase 3 replaces this stub with
    // parse-then-interpret.
    if (getenv("TS_PARSE_SMOKE")) {
        void* h = ts_parse_program(s ? s : "", "<function>", 0);
        const char* perr = ts_parse_error(h);
        fprintf(stderr, "[tsparse] %s\n", perr ? perr : "ok");
        ts_parse_free(h);
    }
    if (!body || (body && ts_value_is_undefined(body)))
        return ts_value_make_native_function((void*)ts_noop_undefined_native, nullptr);
    if (s) {
        // trim ASCII whitespace/semicolons for the comparison
        std::string t(s);
        const char* WS = " \t\r\n;";
        size_t a = t.find_first_not_of(WS);
        size_t b = t.find_last_not_of(WS);
        std::string core = (a == std::string::npos) ? "" : t.substr(a, b - a + 1);
        if (core.empty())
            return ts_value_make_native_function((void*)ts_noop_undefined_native, nullptr);
        if (core == "return this")
            return ts_value_make_native_function((void*)ts_return_globalThis_native, nullptr);
        // test262 harness idiom (resizableArrayBufferUtils.js):
        //   new Function('return class My<T> extends <T> {}')()
        // AOT cannot compile a dynamic class; the empty subclass adds
        // nothing, so return a thunk yielding the BASE constructor itself.
        // Anything that doesn't resolve to a global constructor falls
        // through to the EvalError below.
        if (core.rfind("return class My", 0) == 0) {
            size_t ext = core.find(" extends ");
            size_t brace = core.find('{', ext == std::string::npos ? 0 : ext);
            if (ext != std::string::npos && brace != std::string::npos) {
                std::string base = core.substr(ext + 9, brace - (ext + 9));
                size_t e = base.find_last_not_of(" 	");
                if (e != std::string::npos) base = base.substr(0, e + 1);
                std::string tail = core.substr(brace);
                bool emptyBody = (tail == "{}" || tail == "{ }");
                bool ident = !base.empty();
                for (char ch : base)
                    if (!isalnum((unsigned char)ch) && ch != '_' && ch != '$')
                        ident = false;
                if (emptyBody && ident) {
                    // Typed-array bases resolve to their FIRST-CLASS cached
                    // constructors (a globalThis property read yields only
                    // the name string, which `new` can't construct).
                    struct TACtor { const char* n; void* (*get)(); };
                    static const TACtor kTA[] = {
                        {"Int8Array", ts_get_global_Int8Array},
                        {"Uint8Array", ts_get_global_Uint8Array},
                        {"Uint8ClampedArray", ts_get_global_Uint8ClampedArray},
                        {"Int16Array", ts_get_global_Int16Array},
                        {"Uint16Array", ts_get_global_Uint16Array},
                        {"Int32Array", ts_get_global_Int32Array},
                        {"Uint32Array", ts_get_global_Uint32Array},
                        {"Float32Array", ts_get_global_Float32Array},
                        {"Float64Array", ts_get_global_Float64Array},
                        {"BigInt64Array", ts_get_global_BigInt64Array},
                        {"BigUint64Array", ts_get_global_BigUint64Array},
                    };
                    for (const auto& e : kTA) {
                        if (base == e.n) {
                            return ts_value_make_native_function(
                                (void*)ts_return_ctx_value_native, e.get());
                        }
                    }
                    extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
                    extern TsValue* globalThis;
                    TsValue* ctor = ts_object_get_property(globalThis, base.c_str());
                    extern bool ts_is_callable(void* val);
                    if (ctor && !ts_value_is_undefined(ctor) &&
                        ts_is_callable((void*)ctor)) {
                        return ts_value_make_native_function(
                            (void*)ts_return_ctx_value_native, (void*)ctor);
                    }
                }
            }
        }
    }
    // EVAL-001: general dynamic source runs on the tree-walking interpreter
    // (src/interp/TsInterp.cpp). The literal idioms above stay first so the
    // long-standing fast paths (and their exact semantics) are preserved.
    // ts_interp_function_ctor throws SyntaxError itself on a parse failure.
    extern void* ts_interp_function_ctor(const char* bodyUtf8);
    return ts_interp_function_ctor(s ? s : "");
}

// Function(p1, ..., pn, body) with ALL arguments (EVAL-001). n <= 1 delegates
// to the stub above (pattern-matcher first). n > 1 coerces every argument to
// a string FIRST — ts_to_string_spec can ts_throw, and this frame deliberately
// holds no destructor-owning locals — then hands the string array to the
// interpreter for "(function anonymous(p1,...,pn){body})" assembly + parse.
void* ts_function_constructor_args(void* argsArrV) {
    extern void* ts_function_ctor_from_strings(void* strArr, int64_t n);
    extern void* ts_to_string_spec(TsValue* val);

    void* raw = ts_value_get_object((TsValue*)argsArrV);
    if (!raw) raw = argsArrV;
    TsArray* arr = (TsArray*)raw;
    int64_t n = arr ? ts_array_length(arr) : 0;

    if (n <= 0)
        return ts_function_constructor_stub(ts_value_make_undefined());
    if (n == 1) {
        TsValue* body = ts_array_get_dynamic((TsValue*)argsArrV, ts_value_make_int(0));
        return ts_function_constructor_stub(body);
    }

    void* strArr = ts_array_create();
    for (int64_t i = 0; i < n; i++) {
        TsValue* el = ts_array_get_dynamic((TsValue*)argsArrV, ts_value_make_int(i));
        void* s = ts_to_string_spec(el);   // may ts_throw — no locals to corrupt
        ts_array_push_any(strArr, ts_value_make_string(s));
    }
    return ts_function_ctor_from_strings(strArr, n);
}

}
