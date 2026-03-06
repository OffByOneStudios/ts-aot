#include "StackMap.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// LLVM Stack Map v3 Parser
//
// Parses the .llvm_stackmaps section emitted by LLVM's RewriteStatepointsForGC
// pass. This section records which GC-managed pointers are live at each call
// site (safepoint), enabling precise stack scanning.
//
// Format reference: https://llvm.org/docs/StackMaps.html#stack-map-format
// ============================================================================

// Maximum number of safepoints we support
static const size_t MAX_SAFEPOINTS = 8192;

// Hash table for return_address -> SafepointInfo lookup
// Open-addressing with linear probing
static const size_t HASH_TABLE_SIZE = 16384;  // Must be power of 2

static SafepointInfo* g_safepoints = nullptr;
static size_t g_safepoint_count = 0;

// Hash table: maps return address to index in g_safepoints
struct HashEntry {
    uint64_t key;    // return address (0 = empty)
    uint32_t index;  // index into g_safepoints
};

static HashEntry* g_hash_table = nullptr;

static uint64_t hash_address(uint64_t addr) {
    // FNV-1a style mixing
    addr ^= addr >> 33;
    addr *= 0xff51afd7ed558ccdULL;
    addr ^= addr >> 33;
    addr *= 0xc4ceb9fe1a85ec53ULL;
    addr ^= addr >> 33;
    return addr;
}

static void hash_insert(uint64_t returnAddr, uint32_t index) {
    if (!g_hash_table) return;
    uint64_t slot = hash_address(returnAddr) & (HASH_TABLE_SIZE - 1);
    while (g_hash_table[slot].key != 0) {
        slot = (slot + 1) & (HASH_TABLE_SIZE - 1);
    }
    g_hash_table[slot].key = returnAddr;
    g_hash_table[slot].index = index;
}

// ============================================================================
// PE Section Finder
// ============================================================================

#ifdef _WIN32

struct SectionInfo {
    const uint8_t* data;
    size_t size;
};

// Walk PE headers to find .llvm_stackmaps section (truncated to .llvm_st in COFF)
static SectionInfo find_stackmap_section() {
    SectionInfo result = { nullptr, 0 };

    HMODULE hModule = GetModuleHandle(NULL);
    if (!hModule) return result;

    const uint8_t* base = (const uint8_t*)hModule;

    // MZ header
    if (base[0] != 'M' || base[1] != 'Z') return result;

    // PE header offset
    uint32_t peOffset = *(const uint32_t*)(base + 60);
    const uint8_t* peHeader = base + peOffset;

    // Verify PE signature
    if (peHeader[0] != 'P' || peHeader[1] != 'E' ||
        peHeader[2] != 0 || peHeader[3] != 0) return result;

    // COFF header at peOffset + 4
    const uint8_t* coffHeader = peHeader + 4;
    uint16_t numSections = *(const uint16_t*)(coffHeader + 2);
    uint16_t optHeaderSize = *(const uint16_t*)(coffHeader + 16);

    // Section headers start after optional header
    const uint8_t* sectionHeader = coffHeader + 20 + optHeaderSize;

    for (uint16_t i = 0; i < numSections; i++) {
        // Section name is first 8 bytes
        char name[9] = {};
        memcpy(name, sectionHeader, 8);

        // Look for .llvm_st (truncated .llvm_stackmaps)
        if (strncmp(name, ".llvm_st", 8) == 0) {
            uint32_t virtualSize = *(const uint32_t*)(sectionHeader + 8);
            uint32_t virtualAddr = *(const uint32_t*)(sectionHeader + 12);

            result.data = base + virtualAddr;
            result.size = virtualSize;
            return result;
        }

        sectionHeader += 40;  // Each section header is 40 bytes
    }

    return result;
}

#elif defined(__linux__)

#include <elf.h>
#include <link.h>
#include <cstring>
#include <cstdio>
#include <vector>

struct SectionInfo {
    const uint8_t* data;
    size_t size;
};

// Read .llvm_stackmaps section from the ELF binary on disk.
// Section headers are NOT mapped into memory, so we must read the file directly.
static SectionInfo find_stackmap_section() {
    static std::vector<uint8_t> stackmap_data;
    SectionInfo result = { nullptr, 0 };

    // Read the executable from /proc/self/exe
    FILE* f = fopen("/proc/self/exe", "rb");
    if (!f) return result;

    // Read ELF header
    Elf64_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) { fclose(f); return result; }

    // Verify ELF magic
    if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        fclose(f); return result;
    }

    if (ehdr.e_shstrndx == SHN_UNDEF || ehdr.e_shnum == 0) {
        fclose(f); return result;
    }

    // Read section headers
    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    fseek(f, ehdr.e_shoff, SEEK_SET);
    if (fread(shdrs.data(), sizeof(Elf64_Shdr), ehdr.e_shnum, f) != ehdr.e_shnum) {
        fclose(f); return result;
    }

    // Read section header string table
    const Elf64_Shdr& strtab_shdr = shdrs[ehdr.e_shstrndx];
    std::vector<char> strtab(strtab_shdr.sh_size);
    fseek(f, strtab_shdr.sh_offset, SEEK_SET);
    if (fread(strtab.data(), 1, strtab_shdr.sh_size, f) != strtab_shdr.sh_size) {
        fclose(f); return result;
    }

    // Find .llvm_stackmaps section
    for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
        const char* name = strtab.data() + shdrs[i].sh_name;
        if (strcmp(name, ".llvm_stackmaps") == 0 || strncmp(name, ".llvm_st", 8) == 0) {
            stackmap_data.resize(shdrs[i].sh_size);
            fseek(f, shdrs[i].sh_offset, SEEK_SET);
            if (fread(stackmap_data.data(), 1, shdrs[i].sh_size, f) == shdrs[i].sh_size) {
                result.data = stackmap_data.data();
                result.size = shdrs[i].sh_size;
            }
            break;
        }
    }

    fclose(f);
    return result;
}

#else

// Other platforms: stub
static struct { const uint8_t* data; size_t size; } find_stackmap_section() {
    return { nullptr, 0 };
}

#endif

// ============================================================================
// Stack Map v3 Parser
// ============================================================================

// Read a little-endian uint16
static uint16_t read_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Read a little-endian uint32
static uint32_t read_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Read a little-endian uint64
static uint64_t read_u64(const uint8_t* p) {
    return (uint64_t)read_u32(p) | ((uint64_t)read_u32(p + 4) << 32);
}

static void parse_stackmap_section(const uint8_t* data, size_t size, uint64_t imageBase) {
    if (size < 16) return;

    // Header (16 bytes)
    uint8_t version = data[0];
    if (version != 3) {
        fprintf(stderr, "[StackMap] Unsupported stack map version: %u (expected 3)\n", version);
        return;
    }

    uint32_t numFunctions = read_u32(data + 4);
    uint32_t numConstants = read_u32(data + 8);
    uint32_t numRecords = read_u32(data + 12);

    if (numRecords > MAX_SAFEPOINTS) {
        fprintf(stderr, "[StackMap] Too many safepoints: %u (max %zu)\n",
                numRecords, MAX_SAFEPOINTS);
        return;
    }

    // Allocate storage
    g_safepoints = (SafepointInfo*)calloc(numRecords, sizeof(SafepointInfo));
    g_hash_table = (HashEntry*)calloc(HASH_TABLE_SIZE, sizeof(HashEntry));
    if (!g_safepoints || !g_hash_table) {
        fprintf(stderr, "[StackMap] Failed to allocate safepoint storage\n");
        return;
    }

    const uint8_t* pos = data + 16;
    const uint8_t* end = data + size;

    // Parse function entries (24 bytes each)
    struct FuncEntry {
        uint64_t address;
        uint64_t stackSize;
        uint64_t recordCount;
    };

    FuncEntry* functions = nullptr;
    if (numFunctions > 0) {
        functions = (FuncEntry*)calloc(numFunctions, sizeof(FuncEntry));
        for (uint32_t i = 0; i < numFunctions; i++) {
            if (pos + 24 > end) goto cleanup;
            functions[i].address = read_u64(pos);
            functions[i].stackSize = read_u64(pos + 8);
            functions[i].recordCount = read_u64(pos + 16);
            pos += 24;
        }
    }

    // Skip constants (8 bytes each)
    pos += numConstants * 8;
    if (pos > end) goto cleanup;

    // Parse records
    {
        uint32_t funcIdx = 0;
        uint64_t recordsRemaining = (numFunctions > 0) ? functions[0].recordCount : 0;

        for (uint32_t i = 0; i < numRecords; i++) {
            // Track which function this record belongs to
            while (funcIdx < numFunctions - 1 && recordsRemaining == 0) {
                funcIdx++;
                recordsRemaining = functions[funcIdx].recordCount;
            }

            if (pos + 8 > end) break;
            // uint64_t patchpointId = read_u64(pos);
            pos += 8;

            if (pos + 4 > end) break;
            uint32_t instrOffset = read_u32(pos);
            pos += 4;

            if (pos + 4 > end) break;
            // uint16_t reserved = read_u16(pos);
            uint16_t numLocations = read_u16(pos + 2);
            pos += 4;

            // Compute return address: function_address + instruction_offset
            // The function address in the stack map is relative to the image base.
            // At runtime, we add the actual image base to get the real address.
            uint64_t returnAddr = 0;
            if (funcIdx < numFunctions) {
                returnAddr = functions[funcIdx].address + instrOffset + imageBase;
            }

            SafepointInfo* sp = &g_safepoints[g_safepoint_count];
            sp->returnAddress = returnAddr;
            sp->numRoots = 0;

            // Parse location entries (12 bytes each in v3)
            for (uint16_t j = 0; j < numLocations; j++) {
                if (pos + 12 > end) break;

                uint8_t locType = pos[0];
                // pos[1] = reserved
                // uint16_t locSize = read_u16(pos + 2);
                uint16_t dwarfReg = read_u16(pos + 4);
                // pos[6..7] = reserved
                int32_t offset = (int32_t)read_u32(pos + 8);
                pos += 12;

                // Only track Indirect (stack slot) and Register locations
                // These are the GC pointer locations
                if ((locType == 3 || locType == 1) && sp->numRoots < 32) {
                    GCRootLocation* root = &sp->roots[sp->numRoots++];
                    root->regNum = dwarfReg;
                    root->offset = offset;
                    root->locType = locType;
                }
            }

            // Align to 8 bytes after locations
            uintptr_t posVal = (uintptr_t)(pos - data);
            if (posVal % 8 != 0) {
                pos += 8 - (posVal % 8);
            }

            // Live outs: uint16 padding + uint16 num_live_outs
            if (pos + 4 > end) break;
            // uint16_t pad = read_u16(pos);
            uint16_t numLiveOuts = read_u16(pos + 2);
            pos += 4;

            // Skip live out entries (4 bytes each)
            pos += numLiveOuts * 4;

            // Align to 8 bytes after live outs
            posVal = (uintptr_t)(pos - data);
            if (posVal % 8 != 0) {
                pos += 8 - (posVal % 8);
            }

            // Only add to hash table if we found GC roots
            if (sp->numRoots > 0) {
                hash_insert(returnAddr, (uint32_t)g_safepoint_count);
            }

            g_safepoint_count++;
            recordsRemaining--;
        }
    }

cleanup:
    free(functions);
}

// ============================================================================
// Public API
// ============================================================================

extern "C" {

void ts_stackmap_init() {
    auto section = find_stackmap_section();
    if (!section.data || section.size == 0) {
        // No stack maps - this is normal for non-statepoint builds
        return;
    }

    // Get image base for ASLR-adjusted addresses
    uint64_t imageBase = 0;
#ifdef _WIN32
    imageBase = (uint64_t)GetModuleHandle(NULL);
#elif defined(__linux__)
    extern char __executable_start;
    imageBase = (uint64_t)&__executable_start;
#endif

    parse_stackmap_section(section.data, section.size, imageBase);

    // Count total GC roots across all safepoints
    size_t totalRoots = 0;
    for (size_t i = 0; i < g_safepoint_count; i++) {
        totalRoots += g_safepoints[i].numRoots;
    }

    if (g_safepoint_count > 0) {
        fprintf(stderr, "[StackMap] Parsed %zu safepoints with %zu GC root locations\n",
                g_safepoint_count, totalRoots);
    }
}

const SafepointInfo* ts_stackmap_lookup(uint64_t returnAddress) {
    if (!g_hash_table || g_safepoint_count == 0) return nullptr;

    uint64_t slot = hash_address(returnAddress) & (HASH_TABLE_SIZE - 1);
    for (size_t probe = 0; probe < HASH_TABLE_SIZE; probe++) {
        uint64_t key = g_hash_table[slot].key;
        if (key == 0) return nullptr;  // Empty slot - not found
        if (key == returnAddress) {
            return &g_safepoints[g_hash_table[slot].index];
        }
        slot = (slot + 1) & (HASH_TABLE_SIZE - 1);
    }
    return nullptr;
}

size_t ts_stackmap_count() {
    return g_safepoint_count;
}

}
