// TsProfileRuntime.cpp — Minimal LLVM profile runtime for coverage instrumentation
//
// Provides __llvm_profile_runtime symbol and atexit handler that writes .profraw.
// This replaces clang_rt.profile when it's not available.
//
// The profraw format (version 9, LLVM 18) is:
//   Header: magic, version, counts, padding, counters/names offsets
//   Counter data section
//   Name data section
//
// Reference: llvm/lib/ProfileData/InstrProfWriter.cpp
//            compiler-rt/lib/profile/InstrProfilingFile.c

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
// COFF section iteration for Windows
#pragma section(".lprfc$A", read, write)
#pragma section(".lprfc$Z", read, write)
#pragma section(".lprfd$A", read, write)
#pragma section(".lprfd$Z", read, write)
#pragma section(".lprfn$A", read, write)
#pragma section(".lprfn$Z", read, write)
#endif

extern "C" {

// The sentinel that InstrProfilingLoweringPass references
int __llvm_profile_runtime = 0;

// LLVM's profraw header (version 9 for LLVM 18)
// Reference: llvm/include/llvm/ProfileData/InstrProfData.inc
#define INSTR_PROF_RAW_MAGIC_64  0xFF6C70726F667281ULL
#define INSTR_PROF_RAW_VERSION   9

struct __llvm_profile_header {
    uint64_t Magic;
    uint64_t Version;
    uint64_t BinaryIdsSize;
    uint64_t DataSize;
    uint64_t PaddingBytesBeforeCounters;
    uint64_t NumCounters;
    uint64_t PaddingBytesAfterCounters;
    uint64_t NamesSize;
    uint64_t CountersDelta;
    uint64_t NamesDelta;
    uint64_t ValueKindLast;
};

// __llvm_profile_data structure - one per instrumented function
// Must match LLVM's __llvm_profile_data layout exactly
struct __llvm_profile_data {
    uint64_t NameRef;       // MD5 hash of function name
    uint64_t FuncHash;      // Function hash
    void*    CounterPtr;    // Pointer to counter array
    void*    FunctionPointer;
    void*    Values;
    uint32_t NumCounters;
    uint16_t NumValueSites[2];
};

// Linker-defined section boundaries (COFF)
// The InstrProfilingLoweringPass places data in these sections:
//   .lprfc = counters
//   .lprfd = function data
//   .lprfn = function names
#ifdef _WIN32
__declspec(allocate(".lprfc$A")) static uint64_t __prof_cnts_start = 0;
__declspec(allocate(".lprfc$Z")) static uint64_t __prof_cnts_end = 0;
__declspec(allocate(".lprfd$A")) static __llvm_profile_data __prof_data_start = {};
__declspec(allocate(".lprfd$Z")) static __llvm_profile_data __prof_data_end = {};
__declspec(allocate(".lprfn$A")) static char __prof_names_start = 0;
__declspec(allocate(".lprfn$Z")) static char __prof_names_end = 0;
#else
extern uint64_t __start___llvm_prf_cnts;
extern uint64_t __stop___llvm_prf_cnts;
extern __llvm_profile_data __start___llvm_prf_data;
extern __llvm_profile_data __stop___llvm_prf_data;
extern char __start___llvm_prf_names;
extern char __stop___llvm_prf_names;
#endif

static const char* getProfileFilename() {
    const char* env = getenv("LLVM_PROFILE_FILE");
    return env ? env : "default.profraw";
}

void __ts_profile_write();  // forward decl

static void writeProfileData() {
#ifdef _WIN32
    uint64_t* countersBegin = &__prof_cnts_start + 1;  // Skip sentinel
    uint64_t* countersEnd = &__prof_cnts_end;
    __llvm_profile_data* dataBegin = &__prof_data_start + 1;
    __llvm_profile_data* dataEnd = &__prof_data_end;
    char* namesBegin = &__prof_names_start + 1;
    char* namesEnd = &__prof_names_end;
#else
    uint64_t* countersBegin = &__start___llvm_prf_cnts;
    uint64_t* countersEnd = &__stop___llvm_prf_cnts;
    __llvm_profile_data* dataBegin = &__start___llvm_prf_data;
    __llvm_profile_data* dataEnd = &__stop___llvm_prf_data;
    char* namesBegin = &__start___llvm_prf_names;
    char* namesEnd = &__stop___llvm_prf_names;
#endif

    size_t numCounters = (size_t)(countersEnd - countersBegin);
    size_t dataSize = (size_t)(dataEnd - dataBegin);
    size_t namesSize = (size_t)(namesEnd - namesBegin);

    if (numCounters == 0 && dataSize == 0) return;  // No instrumentation

    const char* filename = getProfileFilename();
    FILE* f = fopen(filename, "wb");
    if (!f) return;

    // Compute padding (counters and names must be aligned)
    size_t countersSizeBytes = numCounters * sizeof(uint64_t);
    size_t paddingAfterCounters = 0;
    if (namesSize > 0) {
        size_t aligned = (countersSizeBytes + 7) & ~7ULL;
        paddingAfterCounters = aligned - countersSizeBytes;
    }
    size_t paddingAfterNames = 0;
    if (namesSize > 0) {
        size_t aligned = (namesSize + 7) & ~7ULL;
        paddingAfterNames = aligned - namesSize;
    }

    // Build header
    __llvm_profile_header header = {};
    header.Magic = INSTR_PROF_RAW_MAGIC_64;
    header.Version = INSTR_PROF_RAW_VERSION;
    header.BinaryIdsSize = 0;
    header.DataSize = dataSize;
    header.PaddingBytesBeforeCounters = 0;
    header.NumCounters = numCounters;
    header.PaddingBytesAfterCounters = paddingAfterCounters;
    header.NamesSize = namesSize;
    header.CountersDelta = (uint64_t)((char*)countersBegin - (char*)dataBegin);
    header.NamesDelta = (uint64_t)((char*)namesBegin - (char*)countersEnd);
    header.ValueKindLast = 0;

    // Write: header, data records, counters, names
    fwrite(&header, sizeof(header), 1, f);
    fwrite(dataBegin, sizeof(__llvm_profile_data), dataSize, f);
    fwrite(countersBegin, sizeof(uint64_t), numCounters, f);
    // Padding after counters
    if (paddingAfterCounters > 0) {
        uint64_t zero = 0;
        fwrite(&zero, 1, paddingAfterCounters, f);
    }
    fwrite(namesBegin, 1, namesSize, f);
    // Padding after names
    if (paddingAfterNames > 0) {
        uint64_t zero = 0;
        fwrite(&zero, 1, paddingAfterNames, f);
    }

    fclose(f);
}

// Called by generated code via atexit() registration in __ts_coverage_init
void __ts_profile_write() {
    writeProfileData();
}

} // extern "C"
