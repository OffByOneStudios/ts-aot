// TsProfileRuntime.cpp — Minimal LLVM profile runtime for coverage instrumentation
//
// Provides __llvm_profile_runtime and writes .profraw on exit.
// Uses COFF section sentinels to find counter/data/name boundaries.
//
// Profraw v9 format (LLVM 18):
//   Header (14 x uint64), Data records, Counters, Bitmaps, Names

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// COFF section sentinels — linker merges $A < $M < $Z
#ifdef _WIN32
#pragma section(".lprfc$A", read, write)
#pragma section(".lprfc$Z", read, write)
#pragma section(".lprfd$A", read, write)
#pragma section(".lprfd$Z", read, write)
#pragma section(".lprfn$A", read, write)
#pragma section(".lprfn$Z", read, write)

__declspec(allocate(".lprfc$A")) static uint64_t __lprfc_a = 0;
__declspec(allocate(".lprfc$Z")) static uint64_t __lprfc_z = 0;
__declspec(allocate(".lprfd$A")) static uint64_t __lprfd_a[8] = {};  // 64 bytes placeholder
__declspec(allocate(".lprfd$Z")) static uint64_t __lprfd_z[8] = {};
__declspec(allocate(".lprfn$A")) static uint8_t __lprfn_a = 0;
__declspec(allocate(".lprfn$Z")) static uint8_t __lprfn_z = 0;
#endif

static const char* getProfileFilename() {
    const char* env = getenv("LLVM_PROFILE_FILE");
    return env ? env : "default.profraw";
}

static void writeProfileData() {
#ifdef _WIN32
    // Section sentinels bracket the $M data: $A < $M < $Z
    char* cntsBegin = (char*)(&__lprfc_a + 1);
    char* cntsEnd   = (char*)&__lprfc_z;
    char* dataBegin = (char*)(&__lprfd_a[0] + 8);  // Skip 64-byte sentinel
    char* dataEnd   = (char*)&__lprfd_z[0];
    char* namesBegin = (char*)(&__lprfn_a + 1);
    char* namesEnd   = (char*)&__lprfn_z;
#else
    // ELF: linker-defined section boundaries
    extern char __start___llvm_prf_cnts[];
    extern char __stop___llvm_prf_cnts[];
    extern char __start___llvm_prf_data[];
    extern char __stop___llvm_prf_data[];
    extern char __start___llvm_prf_names[];
    extern char __stop___llvm_prf_names[];
    char* cntsBegin = __start___llvm_prf_cnts;
    char* cntsEnd   = __stop___llvm_prf_cnts;
    char* dataBegin = __start___llvm_prf_data;
    char* dataEnd   = __stop___llvm_prf_data;
    char* namesBegin = __start___llvm_prf_names;
    char* namesEnd   = __stop___llvm_prf_names;
#endif

    if (cntsEnd <= cntsBegin || dataEnd <= dataBegin) return;

    size_t numCounters = (size_t)(cntsEnd - cntsBegin) / sizeof(uint64_t);
    size_t numData = (size_t)(dataEnd - dataBegin) / 64;  // 64 bytes per record
    size_t namesSize = (namesEnd > namesBegin) ? (size_t)(namesEnd - namesBegin) : 0;

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
    header.CountersDelta = (uint64_t)(uintptr_t)(cntsBegin - dataBegin);
    header.BitmapDelta = 0;
    header.NamesDelta = (uint64_t)(uintptr_t)(namesBegin - cntsEnd);
    header.ValueKindLast = 1;  // IPVK_Last

    fwrite(&header, sizeof(header), 1, f);

    // Fix data records: CounterPtr is a 32-bit COFF REL32 relocation stored
    // in the lower 32 bits of a 64-bit field. Sign-extend to match the
    // 64-bit CountersDelta so llvm-profdata computes correct counter indices.
    for (size_t i = 0; i < numData; i++) {
        char record[64];
        memcpy(record, dataBegin + i * 64, 64);
        // CounterPtr is at offset 16 in the record (after NameRef + FuncHash)
        int32_t rel32 = *(int32_t*)(record + 16);
        int64_t rel64 = (int64_t)rel32;
        memcpy(record + 16, &rel64, 8);
        // BitmapPtr at offset 24 — same treatment
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
