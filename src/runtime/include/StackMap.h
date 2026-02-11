#pragma once

#include <cstdint>
#include <cstddef>

// GC root location at a safepoint (from LLVM .llvm_stackmaps section)
struct GCRootLocation {
    uint16_t regNum;      // DWARF register number (7 = RSP on x86-64)
    int32_t  offset;      // Stack offset from register base
    uint8_t  locType;     // 1=Register, 2=Direct, 3=Indirect
};

// Information about a single GC safepoint (call site)
struct SafepointInfo {
    uint64_t returnAddress;       // Instruction address after call
    uint16_t numRoots;            // Number of GC root locations
    GCRootLocation roots[32];     // Root locations (max 32 per safepoint)
};

extern "C" {

// Parse .llvm_stackmaps section from the current PE executable.
// Called from ts_gc_init(). Safe to call even if no stack maps exist.
void ts_stackmap_init();

// Look up GC root locations for a given return address.
// Returns nullptr if the address is not a known safepoint.
const SafepointInfo* ts_stackmap_lookup(uint64_t returnAddress);

// Get total number of parsed safepoints (for diagnostics).
size_t ts_stackmap_count();

}
