#include "TsError.h"
#include "TsString.h"
#include "TsRuntime.h"
#include "GC.h"
#include "TsNanBox.h"
#include <new>
#include <cstdio>
#include <string>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

// Helper to build stack trace and create error object
static TsValue* buildErrorObject(TsString* msgStr, void* options) {
    TsMap* err = TsMap::Create();
    err->Set(TsString::Create("message"), nanbox_to_tagged(ts_value_make_string(msgStr)));
    err->Set(TsString::Create("name"), nanbox_to_tagged(ts_value_make_string(TsString::Create("Error"))));

    // ES2022: Handle options.cause
    if (options) {
        uint64_t optNb = (uint64_t)(uintptr_t)options;
        if (nanbox_is_ptr(optNb)) {
            void* optPtr = nanbox_to_ptr(optNb);
            if (optPtr) {
                // Check if it's a TsMap
                uint32_t magic = *(uint32_t*)optPtr;
                uint32_t magic16 = *(uint32_t*)((char*)optPtr + 16);
                TsMap* optMap = nullptr;
                if (magic == 0x4D415053) optMap = (TsMap*)optPtr;
                else if (magic16 == 0x4D415053) optMap = (TsMap*)optPtr;
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
#else
    ss << "    at <stack trace not supported on this platform>\n";
#endif

    err->Set(TsString::Create("stack"), nanbox_to_tagged(ts_value_make_string(TsString::Create(ss.str().c_str()))));

    // Return NaN-boxed pointer to the error map
    return (TsValue*)err;
}

extern "C" {
    void* ts_error_create(void* message) {
        return buildErrorObject((TsString*)message, nullptr);
    }

    // ES2022: Error constructor with options { cause: ... }
    void* ts_error_create_with_options(void* message, void* options) {
        return buildErrorObject((TsString*)message, options);
    }
}
