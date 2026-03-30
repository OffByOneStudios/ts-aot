// TsProfileRuntime.cpp — Minimal LLVM profile runtime for coverage instrumentation
//
// Provides __llvm_profile_runtime and writes .profraw on exit.
// Finds counter/data/name sections by walking PE section headers at runtime,
// avoiding fragile COFF section sentinel approaches.
//
// Profraw v9 format (LLVM 18):
//   Header (14 x uint64), Data records, Counters, Bitmaps, Names

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {

// Sentinel referenced by InstrProfilingLoweringPass
int __llvm_profile_runtime = 0;

#define INSTR_PROF_RAW_MAGIC_64  0xFF6C70726F667281ULL
#define INSTR_PROF_RAW_VERSION   9

// Profraw v9 header — 14 fields (LLVM 18, from InstrProfData.inc)
struct __llvm_profile_header {
    uint64_t Magic;
    uint64_t Version;
    uint64_t BinaryIdsSize;
    uint64_t NumData;
    uint64_t PaddingBytesBeforeCounters;
    uint64_t NumCounters;
    uint64_t PaddingBytesAfterCounters;
    uint64_t NumBitmapBytes;
    uint64_t PaddingBytesAfterBitmapBytes;
    uint64_t NamesSize;
    uint64_t CountersDelta;
    uint64_t BitmapDelta;
    uint64_t NamesDelta;
    uint64_t ValueKindLast;
};

// Find a PE section by name prefix. Returns base address and size.
// Matches sections starting with `prefix` (e.g., ".lprfc" matches ".lprfc").
// When multiple sections share a prefix, returns the LARGEST one
// (the $M data section, not the tiny $A/$Z sentinels).
#ifdef _WIN32
static bool findPESection(const char* prefix, char*& outBase, size_t& outSize) {
    HMODULE hModule = GetModuleHandleA(NULL);
    if (!hModule) return false;

    auto* dosHeader = (IMAGE_DOS_HEADER*)hModule;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* ntHeaders = (IMAGE_NT_HEADERS*)((char*)hModule + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return false;

    auto* section = IMAGE_FIRST_SECTION(ntHeaders);
    WORD numSections = ntHeaders->FileHeader.NumberOfSections;
    size_t prefixLen = strlen(prefix);

    outBase = nullptr;
    outSize = 0;

    for (WORD i = 0; i < numSections; i++) {
        // Section name is 8 bytes, may not be null-terminated
        char name[9] = {};
        memcpy(name, section[i].Name, 8);

        if (strncmp(name, prefix, prefixLen) == 0) {
            size_t sz = section[i].Misc.VirtualSize;
            if (sz == 0) sz = section[i].SizeOfRawData;
            // Pick the largest matching section (skip tiny sentinels)
            if (sz > outSize) {
                outBase = (char*)hModule + section[i].VirtualAddress;
                outSize = sz;
            }
        }
    }
    return outBase != nullptr && outSize > 0;
}
#endif

static const char* getProfileFilename() {
    const char* env = getenv("LLVM_PROFILE_FILE");
    return env ? env : "default.profraw";
}

static void writeProfileData() {
    char* cntsBegin = nullptr; size_t cntsSize = 0;
    char* dataBegin = nullptr; size_t dataSize = 0;
    char* namesBegin = nullptr; size_t namesSize = 0;

#ifdef _WIN32
    if (!findPESection(".lprfc", cntsBegin, cntsSize)) return;
    if (!findPESection(".lprfd", dataBegin, dataSize)) return;
    findPESection(".lprfn", namesBegin, namesSize);  // Names optional
#else
    extern char __start___llvm_prf_cnts[];
    extern char __stop___llvm_prf_cnts[];
    extern char __start___llvm_prf_data[];
    extern char __stop___llvm_prf_data[];
    extern char __start___llvm_prf_names[];
    extern char __stop___llvm_prf_names[];
    cntsBegin = __start___llvm_prf_cnts;
    cntsSize = __stop___llvm_prf_cnts - __start___llvm_prf_cnts;
    dataBegin = __start___llvm_prf_data;
    dataSize = __stop___llvm_prf_data - __start___llvm_prf_data;
    namesBegin = __start___llvm_prf_names;
    namesSize = __stop___llvm_prf_names - __start___llvm_prf_names;
#endif

    size_t numCounters = cntsSize / sizeof(uint64_t);
    size_t numData = dataSize / 64;  // 64 bytes per record

    if (numCounters == 0 || numData == 0) return;

    const char* filename = getProfileFilename();
    FILE* f = fopen(filename, "wb");
    if (!f) return;

    size_t counterBytes = numCounters * sizeof(uint64_t);
    size_t paddingAfterCounters = (8 - (counterBytes % 8)) % 8;
    size_t paddingAfterNames = namesSize > 0 ? (8 - (namesSize % 8)) % 8 : 0;

    __llvm_profile_header header = {};
    header.Magic = INSTR_PROF_RAW_MAGIC_64;
    header.Version = INSTR_PROF_RAW_VERSION;
    header.BinaryIdsSize = 0;
    header.NumData = numData;
    header.PaddingBytesBeforeCounters = 0;
    header.NumCounters = numCounters;
    header.PaddingBytesAfterCounters = paddingAfterCounters;
    header.NumBitmapBytes = 0;
    header.PaddingBytesAfterBitmapBytes = 0;
    header.NamesSize = namesSize;
    header.CountersDelta = (uint64_t)(intptr_t)(cntsBegin - dataBegin);
    header.BitmapDelta = 0;
    header.NamesDelta = (uint64_t)(intptr_t)(namesBegin - (cntsBegin + cntsSize));
    header.ValueKindLast = 1;  // IPVK_Last

    fwrite(&header, sizeof(header), 1, f);

    // Write data records with sign-extended CounterPtr/BitmapPtr.
    // These fields contain 32-bit COFF REL32 relocations in the lower 32 bits
    // of a 64-bit field. Sign-extend to match the 64-bit CountersDelta.
    for (size_t i = 0; i < numData; i++) {
        char record[64];
        memcpy(record, dataBegin + i * 64, 64);
        // CounterPtr at offset 16 (after NameRef + FuncHash)
        int32_t rel32 = *(int32_t*)(record + 16);
        int64_t rel64 = (int64_t)rel32;
        memcpy(record + 16, &rel64, 8);
        // BitmapPtr at offset 24
        rel32 = *(int32_t*)(record + 24);
        rel64 = (int64_t)rel32;
        memcpy(record + 24, &rel64, 8);
        fwrite(record, 64, 1, f);
    }

    fwrite(cntsBegin, sizeof(uint64_t), numCounters, f);
    if (paddingAfterCounters > 0) {
        uint64_t zero = 0;
        fwrite(&zero, 1, paddingAfterCounters, f);
    }
    if (namesSize > 0) {
        fwrite(namesBegin, 1, namesSize, f);
        if (paddingAfterNames > 0) {
            uint64_t zero = 0;
            fwrite(&zero, 1, paddingAfterNames, f);
        }
    }

    fclose(f);
}

// Called by generated code via atexit()
void __ts_profile_write() {
    writeProfileData();
}

} // extern "C"
