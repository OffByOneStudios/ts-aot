#include "TsRuntime.h"
#include "TsString.h"
#include "TsConsString.h"
#include "TsBigInt.h"
#include "TsSymbol.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsSet.h"
#include "TsNanBox.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <map>
#include <string>
#include <cstdlib>
#include <cinttypes>

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
        if (magic == 0x46554E43) return TsString::Create("function"); // TsFunction
        if (magic == 0x464C4154) return TsString::Create("object");   // Flat object

        // Check offset 16 for TsObject-derived types
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 == 0x46554E43) return TsString::Create("function"); // TsFunction at offset 16
        if (magic16 == 0x434C5352) return TsString::Create("function"); // TsClosure at offset 16
        if (magic16 == 0x4D415053) return TsString::Create("object");   // TsMap at offset 16

        return TsString::Create("object");
    }

    return TsString::Create("undefined");
}

bool ts_value_is_nullish(TsValue* v) {
    if (!v) return true;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    return nanbox_is_undefined(nb) || nanbox_is_null(nb);
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

double ts_value_get_double(TsValue* v) {
    if (!v) return 0.0;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (nanbox_is_int32(nb)) return (double)nanbox_to_int32(nb);
    if (nanbox_is_double(nb)) return nanbox_to_double(nb);
    if (nanbox_is_bool(nb)) return nanbox_to_bool(nb) ? 1.0 : 0.0;
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return 0.0;
        uint32_t magic = *(uint32_t*)ptr;
        if (magic == 0x53545247 || magic == TsConsString::MAGIC) {
            try { return std::stod(ts_ensure_flat(ptr)->ToUtf8()); }
            catch (...) { return std::numeric_limits<double>::quiet_NaN(); }
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
        uint32_t magic = *(uint32_t*)ptr;
        if (magic == 0x53545247 || magic == TsConsString::MAGIC) {
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
        uint32_t magic = *(uint32_t*)ptr;
        if (magic == 0x53545247 || magic == TsConsString::MAGIC) {
            return ts_string_like_length(ptr) > 0; // Empty string is falsy
        }
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
        uint32_t magic = *(uint32_t*)ptr;
        if (magic == 0x53545247 || magic == TsConsString::MAGIC) {
            TsString* str = ts_ensure_flat(ptr);
            const char* utf8 = str->ToUtf8();
            if (!utf8 || *utf8 == '\0') return 0.0;
            // Trim whitespace
            const char* s = utf8;
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
            if (*s == '\0') return 0.0;
            char* end = nullptr;
            double d = std::strtod(s, &end);
            // Check remaining chars are whitespace
            while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
            if (*end != '\0') return std::numeric_limits<double>::quiet_NaN();
            return d;
        }
        // Objects: NaN
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

}
