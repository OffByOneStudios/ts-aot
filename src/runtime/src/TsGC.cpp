#include "TsGC.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <csetjmp>
#include <vector>
#include <mutex>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <winternl.h>
#include <intrin.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#endif

// ============================================================================
// Configuration
// ============================================================================

static const size_t BLOCK_SIZE = 256 * 1024;  // 256KB per block
static const size_t MAX_SMALL_SIZE = 4096;  // Must match last entry in SIZE_CLASSES

// Size classes: 8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512
static const size_t SIZE_CLASSES[] = { 8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 2048, 4096 };
static const size_t NUM_SIZE_CLASSES = sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]);

// GC trigger: collect when total_allocated > growth_factor * live_after_last_gc
static const size_t DEFAULT_MIN_GC_THRESHOLD = 64 * 1024 * 1024;  // 64MB minimum
static const size_t DEFAULT_MAX_HEAP_SIZE = 2ULL * 1024 * 1024 * 1024;  // 2GB
static const double DEFAULT_GC_GROWTH_FACTOR = 2.0;

// Mutable tuning parameters (can be overridden via environment variables)
static size_t g_min_gc_threshold = DEFAULT_MIN_GC_THRESHOLD;
static size_t g_max_heap_size = DEFAULT_MAX_HEAP_SIZE;
static double g_gc_growth_factor = DEFAULT_GC_GROWTH_FACTOR;

// Mark worklist initial capacity (no allocations during mark phase)
static const size_t MARK_WORKLIST_CAPACITY = 16384;

// Verbose diagnostics via TS_GC_VERBOSE=1
static bool g_gc_verbose = false;

// Card-table verification: TS_GC_VERIFY_CARDS=1 does a full old-gen scan
// after the card-table scan and reports any missed nursery pointers.
static bool g_verify_cards = false;

// ============================================================================
// Nursery Configuration (Pin-Based Promotion, SGen-style)
// ============================================================================

static const size_t DEFAULT_NURSERY_SIZE = 4 * 1024 * 1024;  // 4MB single region
static const size_t NURSERY_MAX_OBJ_SIZE = 256;   // Objects <= 256 bytes go to nursery
static const size_t NURSERY_SIZE_PREFIX = 8;       // 8-byte size header before each object

// Pin bit: stored in bit 63 of the 8-byte size prefix.
// Mark bit: stored in bit 62 (used during minor GC liveness tracing).
// Object sizes are <= 256 (9 bits), so bits 62-63 are always free.
static const uint64_t NURSERY_PIN_BIT  = (uint64_t)1 << 63;
static const uint64_t NURSERY_MARK_BIT = (uint64_t)1 << 62;
static const uint64_t NURSERY_META_MASK = NURSERY_PIN_BIT | NURSERY_MARK_BIT;

static inline size_t nursery_get_size(uint64_t prefix) {
    return (size_t)(prefix & ~NURSERY_META_MASK);
}
static inline bool nursery_is_pinned(uint64_t prefix) {
    return (prefix & NURSERY_PIN_BIT) != 0;
}
static inline void nursery_set_pinned(uint64_t* prefix_ptr) {
    *prefix_ptr |= NURSERY_PIN_BIT;
}
static inline void nursery_clear_pinned(uint64_t* prefix_ptr) {
    *prefix_ptr &= ~NURSERY_PIN_BIT;
}
static inline bool nursery_is_marked(uint64_t prefix) {
    return (prefix & NURSERY_MARK_BIT) != 0;
}
static inline void nursery_set_marked(uint64_t* prefix_ptr) {
    *prefix_ptr |= NURSERY_MARK_BIT;
}

// Fragment: a free region within the nursery between pinned objects
struct NurseryFragment {
    size_t offset;   // Byte offset from nursery region start
    size_t size;     // Free region size in bytes
};

struct Nursery {
    void* region;              // Single contiguous VirtualAlloc
    size_t region_size;        // Total nursery size (default 4MB)
    size_t cursor;             // Current bump offset within active fragment
    size_t cursor_limit;       // End of current fragment
    bool enabled;
    size_t total_allocated;    // Total bytes bumped in nursery (for stats)
    size_t alloc_count;        // Number of nursery allocations (for stats)
    size_t high_water;         // Highest cursor value ever (for iteration)

    // Fragment free list (rebuilt each minor GC)
    std::vector<NurseryFragment> fragments;
    size_t current_fragment;   // Index into fragments

    // Stats
    size_t pinned_count;
    size_t pinned_bytes;
};

static Nursery g_nursery = {};

// ============================================================================
// Card Table Configuration
// ============================================================================
// One byte per 512-byte "card". When an old-gen store writes a nursery pointer,
// the corresponding card is dirtied. During minor GC, dirty cards are scanned
// to find old-gen-to-nursery references.

static const size_t CARD_SHIFT = 9;                             // 512 bytes per card
static const size_t CARD_SIZE = (size_t)1 << CARD_SHIFT;       // 512
static const size_t CARD_TABLE_SIZE = (size_t)1 << 21;         // 2M entries (modular indexing)

static uint8_t* g_card_table = nullptr;       // malloc'd (not GC-managed)

// Non-GC slot tracking: slots outside card table coverage that hold nursery pointers.
// Safety net for any memory not covered by the card table (e.g., malloc'd structures).
static std::vector<void**> g_non_gc_nursery_slots;

// Modular card index: maps any virtual address to a card table entry.
// Uses power-of-2 masking for fast computation. Different addresses may alias
// to the same card (false positives), which causes extra scanning but no
// missed pointers — a safe trade-off.
static inline size_t card_index(uintptr_t addr) {
    return (addr >> CARD_SHIFT) & (CARD_TABLE_SIZE - 1);
}

// Minor GC fixup callbacks: registered by modules with caches/registries
// that hold nursery pointers in malloc'd memory (not covered by card table).
struct MinorFixupEntry {
    ts_gc_minor_fixup_callback callback;
    void* context;
};
static std::vector<MinorFixupEntry> g_minor_fixup_scanners;

// Forwarding table entry for minor GC (nursery → old-gen mapping).
struct ForwardEntry {
    uintptr_t nursery_addr;
    void* old_gen_addr;
    size_t size;
};

// File-scope pointer to current forwarding table, valid only during minor GC.
// Used by ts_gc_minor_lookup_forward().
static std::vector<ForwardEntry>* g_current_forwarding = nullptr;

// ============================================================================
// Platform Memory
// ============================================================================

// NaN-boxing requires GC pointers to have top 16 bits = 0 (48-bit address space).
// On Windows, VirtualAlloc(nullptr, ...) can return addresses above 0x0000FFFFFFFFFFFF
// when the process has a large virtual address space. We use a hint address in the
// low 48-bit range and fall back to nullptr if the hint fails.
static void* platform_alloc_low(size_t size) {
#ifdef _WIN32
    // NaN-boxing requires pointers with top 16 bits = 0 (48-bit address space).
    // Use a hint starting at 256MB and retry at lower addresses if needed.
    static void* hint = (void*)0x0000000010000000ULL;
    void* p = VirtualAlloc(hint, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p) {
        // Verify the allocation is in the 48-bit range
        if ((uintptr_t)p < 0x0001000000000000ULL) {
            hint = (void*)((uintptr_t)p + size);
            return p;
        }
        // Address too high — free and retry with lower hint
        VirtualFree(p, 0, MEM_RELEASE);
    }
    // Try lower addresses
    for (uintptr_t base = 0x20000000ULL; base < 0x0000800000000000ULL; base += 0x10000000ULL) {
        p = VirtualAlloc((void*)base, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p && (uintptr_t)p < 0x0001000000000000ULL) {
            hint = (void*)((uintptr_t)p + size);
            return p;
        }
        if (p) VirtualFree(p, 0, MEM_RELEASE);
    }
    // Last resort — accept any address
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
#endif
}

static void* platform_alloc_block() {
    return platform_alloc_low(BLOCK_SIZE);
}

static void platform_free_block(void* ptr) {
#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, BLOCK_SIZE);
#endif
}

static void* platform_alloc_large(size_t size) {
    return platform_alloc_low(size);
}

static void platform_free_large(void* ptr, size_t size) {
#ifdef _WIN32
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

// ============================================================================
// Block Allocator Structures
// ============================================================================

struct BlockHeader {
    void* block_mem;           // VirtualAlloc'd BLOCK_SIZE region
    size_t slot_size;
    size_t slot_count;
    uint8_t* allocated_bits;   // malloc'd bitmap: 1 bit per slot
    uint8_t* mark_bits;        // malloc'd bitmap: 1 bit per slot
    BlockHeader* next;         // linked list per size class
    size_t free_cursor;        // hint for next free slot scan
    size_t live_count;         // slots currently allocated (for fast stats)
};

struct LargeObjHeader {
    size_t alloc_size;         // total VirtualAlloc size (header + data)
    size_t data_size;          // user-requested size
    bool marked;
    LargeObjHeader* next;
    LargeObjHeader* prev;
    // user data follows immediately after this struct
};

// For binary search during pointer validation
struct BlockDescriptor {
    uintptr_t base;            // start of block memory
    uintptr_t end;             // base + BLOCK_SIZE
    size_t slot_size;
    BlockHeader* header;
};

// For binary search of large objects (replaces O(n) linked list scan)
struct LargeObjDescriptor {
    uintptr_t base;            // start of user data (after header)
    uintptr_t end;             // base + data_size
    LargeObjHeader* header;
};

// ============================================================================
// GC Heap State
// ============================================================================

struct ScannerEntry {
    ts_gc_scan_callback callback;
    void* context;
};

struct FinalizerEntry {
    void* target;
    void* callback;
    void* held_value;
    void* unregister_token;
};

struct PendingCallback {
    void* callback;
    void* arg;
};

struct GCHeap {
    // Per-size-class block lists
    BlockHeader* block_lists[NUM_SIZE_CLASSES] = {};

    // Large object doubly-linked list (sentinel head)
    LargeObjHeader large_sentinel;

    // Sorted descriptor array for binary search in ts_gc_base()
    std::vector<BlockDescriptor> block_descriptors;
    bool descriptors_dirty = false;

    // Sorted descriptor array for binary search of large objects
    std::vector<LargeObjDescriptor> large_descriptors;
    bool large_descriptors_dirty = false;

    // Root management
    std::vector<void**> global_roots;
    std::vector<ScannerEntry> scanners;
    std::vector<void**> weak_refs;       // pointers-to-pointer that hold weak refs
    std::vector<FinalizerEntry> finalizers;

    // Mark worklist
    std::vector<void*> mark_worklist;

    // Pending finalizer callbacks (filled during GC, executed after lock released)
    std::vector<PendingCallback> pending_callbacks;

    // Statistics
    size_t total_allocated = 0;       // current heap usage
    size_t live_after_last_gc = 0;    // live bytes after last collection
    size_t gc_threshold = DEFAULT_MIN_GC_THRESHOLD;
    size_t collection_count = 0;
    size_t peak_allocated = 0;

    // Thread safety
    std::mutex gc_mutex;

    GCHeap() {
        large_sentinel.next = &large_sentinel;
        large_sentinel.prev = &large_sentinel;
        mark_worklist.reserve(MARK_WORKLIST_CAPACITY);
    }
};

static GCHeap* g_heap = nullptr;
static bool g_in_collection = false;  // True while GC is running (lock held)

// Exported globals for compiler inline write barriers
extern "C" {
    uint64_t ts_gc_nursery_base = 0;
    uint64_t ts_gc_nursery_end = 0;
    uint8_t* ts_gc_card_table_ptr = nullptr;
    uint64_t ts_gc_card_table_base_addr = 0;  // Unused with modular indexing, kept for ABI compat

    // Exported globals for compiler inline nursery bump-pointer allocation
    char* ts_nursery_cursor = nullptr;        // Absolute pointer: region + cursor offset
    char* ts_nursery_cursor_limit = nullptr;  // Absolute pointer: region + cursor_limit offset
}

// Nursery sync helpers: keep exported globals in sync with g_nursery
static inline void nursery_sync_from_exported() {
    if (ts_nursery_cursor && g_nursery.region) {
        g_nursery.cursor = (size_t)(ts_nursery_cursor - (char*)g_nursery.region);
        if (g_nursery.cursor > g_nursery.high_water)
            g_nursery.high_water = g_nursery.cursor;
    }
}
static inline void nursery_sync_to_exported() {
    if (g_nursery.region) {
        ts_nursery_cursor = (char*)g_nursery.region + g_nursery.cursor;
        ts_nursery_cursor_limit = (char*)g_nursery.region + g_nursery.cursor_limit;
    }
}

// Forward declarations for nursery helpers
static void gc_mark_ptr(void* ptr);

// ============================================================================
// Nursery Helpers
// ============================================================================

static inline bool is_nursery_ptr(void* ptr) {
    // Single unsigned comparison: (addr - base) < region_size
    return ((uintptr_t)ptr - (uintptr_t)g_nursery.region) < g_nursery.region_size;
}

// Find the base address of a nursery object containing ptr, or nullptr.
// Walks the single nursery region using size prefixes, skipping gaps (prefix == 0).
static void* nursery_find_base(void* ptr) {
    if (!g_nursery.enabled || !g_nursery.region) return nullptr;
    uintptr_t addr = (uintptr_t)ptr;
    if (!is_nursery_ptr(ptr)) return nullptr;

    char* base = (char*)g_nursery.region;
    size_t limit = g_nursery.high_water;
    size_t offset = 0;

    while (offset + NURSERY_SIZE_PREFIX <= limit) {
        uint64_t raw_prefix = *(uint64_t*)(base + offset);
        size_t obj_size = nursery_get_size(raw_prefix);

        if (obj_size == 0) {
            // Gap (zeroed memory or dead object) - step forward 8 bytes
            offset += 8;
            continue;
        }
        if (obj_size > NURSERY_MAX_OBJ_SIZE) break;  // Corruption

        uintptr_t obj_addr = (uintptr_t)(base + offset + NURSERY_SIZE_PREFIX);
        if (addr >= obj_addr && addr < obj_addr + obj_size) {
            return (void*)obj_addr;
        }
        offset += NURSERY_SIZE_PREFIX + obj_size;
    }
    return nullptr;
}

// Scan all nursery objects conservatively during full GC mark phase.
// Nursery objects may reference old-gen objects that must be marked live.
// Walks single region with gap handling (prefix == 0 means gap).
static void gc_scan_nursery_roots() {
    if (!g_nursery.enabled || g_nursery.high_water == 0) return;

    char* scan = (char*)g_nursery.region;
    size_t offset = 0;

    while (offset + NURSERY_SIZE_PREFIX <= g_nursery.high_water) {
        uint64_t raw_prefix = *(uint64_t*)(scan + offset);
        size_t obj_size = nursery_get_size(raw_prefix);

        if (obj_size == 0) {
            offset += 8;  // Skip gap
            continue;
        }
        if (obj_size > NURSERY_MAX_OBJ_SIZE) break;

        void* obj = scan + offset + NURSERY_SIZE_PREFIX;
        uintptr_t start = (uintptr_t)obj;
        uintptr_t end = start + obj_size;

        for (uintptr_t p = start; p + sizeof(void*) <= end; p += sizeof(void*)) {
            void* candidate = *(void**)p;
            if ((uintptr_t)candidate < 4096) continue;
            if ((uintptr_t)candidate > 0x00007FFFFFFFFFFF) continue;
            gc_mark_ptr(candidate);
        }

        offset += NURSERY_SIZE_PREFIX + obj_size;
    }
}

// Clear the card table (called after minor GC completes)
static void gc_clear_card_table() {
    if (!g_card_table) return;
    memset(g_card_table, 0, CARD_TABLE_SIZE);
}

// Check if any card covering the range [start, start+size) is dirty.
static inline bool card_range_is_dirty(uintptr_t start, size_t size) {
    if (!g_card_table) return false;
    size_t lo = card_index(start);
    size_t hi = card_index(start + size - 1);
    // Modular wrap: if lo > hi, check both ranges
    if (lo <= hi) {
        for (size_t i = lo; i <= hi; i++) {
            if (g_card_table[i]) return true;
        }
    } else {
        for (size_t i = lo; i < CARD_TABLE_SIZE; i++) {
            if (g_card_table[i]) return true;
        }
        for (size_t i = 0; i <= hi; i++) {
            if (g_card_table[i]) return true;
        }
    }
    return false;
}

// ============================================================================
// Bitmap Helpers
// ============================================================================

static inline size_t bitmap_bytes(size_t bits) {
    return (bits + 7) / 8;
}

static inline bool bitmap_get(const uint8_t* bits, size_t idx) {
    return (bits[idx / 8] >> (idx % 8)) & 1;
}

static inline void bitmap_set(uint8_t* bits, size_t idx) {
    bits[idx / 8] |= (1u << (idx % 8));
}

static inline void bitmap_clear(uint8_t* bits, size_t idx) {
    bits[idx / 8] &= ~(1u << (idx % 8));
}

// ============================================================================
// Size Class Lookup
// ============================================================================

static inline int find_size_class(size_t size) {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= SIZE_CLASSES[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ============================================================================
// Block Descriptor Management (for binary search in ts_gc_base)
// ============================================================================

static void rebuild_descriptors() {
    if (!g_heap->descriptors_dirty) return;

    g_heap->block_descriptors.clear();
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        for (BlockHeader* bh = g_heap->block_lists[i]; bh; bh = bh->next) {
            BlockDescriptor desc;
            desc.base = (uintptr_t)bh->block_mem;
            desc.end = desc.base + BLOCK_SIZE;
            desc.slot_size = bh->slot_size;
            desc.header = bh;
            g_heap->block_descriptors.push_back(desc);
        }
    }
    // Sort by base address for binary search
    std::sort(g_heap->block_descriptors.begin(), g_heap->block_descriptors.end(),
              [](const BlockDescriptor& a, const BlockDescriptor& b) {
                  return a.base < b.base;
              });
    g_heap->descriptors_dirty = false;
}

static void rebuild_large_descriptors() {
    if (!g_heap->large_descriptors_dirty) return;

    g_heap->large_descriptors.clear();
    for (LargeObjHeader* h = g_heap->large_sentinel.next;
         h != &g_heap->large_sentinel; h = h->next) {
        LargeObjDescriptor desc;
        desc.base = (uintptr_t)h + sizeof(LargeObjHeader);
        desc.end = desc.base + h->data_size;
        desc.header = h;
        g_heap->large_descriptors.push_back(desc);
    }
    std::sort(g_heap->large_descriptors.begin(), g_heap->large_descriptors.end(),
              [](const LargeObjDescriptor& a, const LargeObjDescriptor& b) {
                  return a.base < b.base;
              });
    g_heap->large_descriptors_dirty = false;
}

// ============================================================================
// Block Allocation
// ============================================================================

static BlockHeader* create_block(size_t slot_size, int class_idx) {
    void* mem = platform_alloc_block();
    if (!mem) return nullptr;

    size_t slot_count = BLOCK_SIZE / slot_size;
    size_t bm_bytes = bitmap_bytes(slot_count);

    BlockHeader* bh = (BlockHeader*)malloc(sizeof(BlockHeader));
    if (!bh) {
        platform_free_block(mem);
        return nullptr;
    }

    bh->block_mem = mem;
    bh->slot_size = slot_size;
    bh->slot_count = slot_count;
    bh->allocated_bits = (uint8_t*)calloc(1, bm_bytes);
    bh->mark_bits = (uint8_t*)calloc(1, bm_bytes);
    bh->free_cursor = 0;
    bh->live_count = 0;

    // Link into size class list
    bh->next = g_heap->block_lists[class_idx];
    g_heap->block_lists[class_idx] = bh;

    // Card table uses modular indexing — no base tracking needed.

    g_heap->descriptors_dirty = true;
    return bh;
}

// Allocate a slot from a specific block. Returns nullptr if block is full.
// Uses byte-level scanning with bit intrinsics for fast free-slot finding.
static void* alloc_from_block(BlockHeader* bh) {
    size_t bm_bytes = bitmap_bytes(bh->slot_count);
    size_t start_byte = bh->free_cursor / 8;

    for (size_t i = 0; i < bm_bytes; i++) {
        size_t byte_idx = (start_byte + i) % bm_bytes;
        uint8_t byte_val = bh->allocated_bits[byte_idx];
        if (byte_val != 0xFF) {
            // Find first zero bit using intrinsics
            uint8_t free_bits = (uint8_t)~byte_val;
            int bit;
#ifdef _MSC_VER
            unsigned long idx;
            _BitScanForward(&idx, (unsigned long)free_bits);
            bit = (int)idx;
#else
            bit = __builtin_ctz((unsigned int)free_bits);
#endif
            size_t slot_idx = byte_idx * 8 + bit;
            if (slot_idx >= bh->slot_count) continue;

            bitmap_set(bh->allocated_bits, slot_idx);
            bh->free_cursor = slot_idx + 1;
            bh->live_count++;
            void* ptr = (char*)bh->block_mem + slot_idx * bh->slot_size;
            // Memory from VirtualAlloc is already zeroed for fresh blocks.
            // For recycled slots, we need to zero explicitly.
            memset(ptr, 0, bh->slot_size);
            return ptr;
        }
    }
    return nullptr; // Block is full
}

// Forward declarations
static void gc_collect_internal();
static void gc_minor_collect_internal();

static void* gc_alloc_small(size_t size) {
    int class_idx = find_size_class(size);
    if (class_idx < 0) return nullptr;

    size_t alloc_size = SIZE_CLASSES[class_idx];

    // Check GC trigger threshold
    if (g_heap->total_allocated + alloc_size > g_heap->gc_threshold) {
        gc_collect_internal();
    }

    // Check heap limit
    if (g_heap->total_allocated + alloc_size > g_max_heap_size) {
        return nullptr;
    }

    // Try existing blocks
    for (BlockHeader* bh = g_heap->block_lists[class_idx]; bh; bh = bh->next) {
        if (bh->live_count < bh->slot_count) {
            void* ptr = alloc_from_block(bh);
            if (ptr) {
                g_heap->total_allocated += alloc_size;
                if (g_heap->total_allocated > g_heap->peak_allocated) {
                    g_heap->peak_allocated = g_heap->total_allocated;
                }
                return ptr;
            }
        }
    }

    // Need a new block
    BlockHeader* bh = create_block(alloc_size, class_idx);
    if (!bh) return nullptr;

    void* ptr = alloc_from_block(bh);
    if (ptr) {
        g_heap->total_allocated += alloc_size;
        if (g_heap->total_allocated > g_heap->peak_allocated) {
            g_heap->peak_allocated = g_heap->total_allocated;
        }
    }
    return ptr;
}

// ============================================================================
// Large Object Allocation
// ============================================================================

static void* gc_alloc_large(size_t size) {
    size_t total = sizeof(LargeObjHeader) + size;

    // Check GC trigger
    if (g_heap->total_allocated + size > g_heap->gc_threshold) {
        gc_collect_internal();
    }

    // Check heap limit
    if (g_heap->total_allocated + size > g_max_heap_size) {
        return nullptr;
    }

    void* mem = platform_alloc_large(total);
    if (!mem) return nullptr;

    LargeObjHeader* hdr = (LargeObjHeader*)mem;
    hdr->alloc_size = total;
    hdr->data_size = size;
    hdr->marked = false;

    // Insert into doubly-linked list after sentinel
    hdr->next = g_heap->large_sentinel.next;
    hdr->prev = &g_heap->large_sentinel;
    g_heap->large_sentinel.next->prev = hdr;
    g_heap->large_sentinel.next = hdr;
    g_heap->large_descriptors_dirty = true;

    g_heap->total_allocated += size;
    if (g_heap->total_allocated > g_heap->peak_allocated) {
        g_heap->peak_allocated = g_heap->total_allocated;
    }

    // Card table uses modular indexing — no base tracking needed.

    // Data follows header, VirtualAlloc returns zeroed memory
    return (char*)mem + sizeof(LargeObjHeader);
}

// ============================================================================
// Pointer Validation (ts_gc_base replacement for GC_base)
// ============================================================================

// Find the base address of the GC object containing ptr, or nullptr.
static void* gc_find_base(void* ptr) {
    if (!ptr || !g_heap) return nullptr;

    // Check nursery first (fast range check)
    if (g_nursery.enabled && is_nursery_ptr(ptr)) {
        return nursery_find_base(ptr);
    }

    uintptr_t addr = (uintptr_t)ptr;

    // Binary search in block descriptors
    rebuild_descriptors();

    auto& descs = g_heap->block_descriptors;
    if (!descs.empty()) {
        // Find the last descriptor with base <= addr
        size_t lo = 0, hi = descs.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (descs[mid].base <= addr) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        if (lo > 0) {
            const BlockDescriptor& desc = descs[lo - 1];
            if (addr >= desc.base && addr < desc.end) {
                // Found containing block
                size_t offset = addr - desc.base;
                size_t slot_idx = offset / desc.slot_size;
                if (slot_idx < desc.header->slot_count &&
                    bitmap_get(desc.header->allocated_bits, slot_idx)) {
                    return (void*)(desc.base + slot_idx * desc.slot_size);
                }
            }
        }
    }

    // Check large objects (binary search)
    rebuild_large_descriptors();
    auto& ldescs = g_heap->large_descriptors;
    if (!ldescs.empty()) {
        size_t lo = 0, hi = ldescs.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (ldescs[mid].base <= addr) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo > 0) {
            const LargeObjDescriptor& ldesc = ldescs[lo - 1];
            if (addr >= ldesc.base && addr < ldesc.end) {
                return (void*)ldesc.base;
            }
        }
    }

    return nullptr;
}

// ============================================================================
// Mark Phase
// ============================================================================

// Mark a single pointer as live and push to worklist if newly marked
static void gc_mark_ptr(void* ptr) {
    if (!ptr) return;

    // Nursery pointers: pinned objects stay in nursery, so during full GC
    // we scan them via gc_scan_nursery_roots(). No forwarding table needed.
    if (g_nursery.enabled && is_nursery_ptr(ptr)) {
        return;  // Nursery objects scanned separately, not mark-swept
    }

    uintptr_t addr = (uintptr_t)ptr;

    // Check small objects first (binary search)
    rebuild_descriptors();
    auto& descs = g_heap->block_descriptors;
    if (!descs.empty()) {
        size_t lo = 0, hi = descs.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (descs[mid].base <= addr) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo > 0) {
            const BlockDescriptor& desc = descs[lo - 1];
            if (addr >= desc.base && addr < desc.end) {
                size_t offset = addr - desc.base;
                size_t slot_idx = offset / desc.slot_size;
                if (slot_idx < desc.header->slot_count &&
                    bitmap_get(desc.header->allocated_bits, slot_idx)) {
                    // Valid allocated slot - check if already marked
                    if (!bitmap_get(desc.header->mark_bits, slot_idx)) {
                        bitmap_set(desc.header->mark_bits, slot_idx);
                        void* base = (void*)(desc.base + slot_idx * desc.slot_size);
                        g_heap->mark_worklist.push_back(base);
                    }
                }
                return;
            }
        }
    }

    // Check large objects (binary search)
    rebuild_large_descriptors();
    auto& ldescs = g_heap->large_descriptors;
    if (!ldescs.empty()) {
        size_t lo = 0, hi = ldescs.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (ldescs[mid].base <= addr) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo > 0) {
            const LargeObjDescriptor& ldesc = ldescs[lo - 1];
            if (addr >= ldesc.base && addr < ldesc.end) {
                if (!ldesc.header->marked) {
                    ldesc.header->marked = true;
                    g_heap->mark_worklist.push_back((void*)ldesc.base);
                }
                return;
            }
        }
    }
}

// Get the size of an object at a given base address (for conservative scanning)
static size_t gc_object_size(void* base) {
    uintptr_t addr = (uintptr_t)base;

    // Check block descriptors
    auto& descs = g_heap->block_descriptors;
    if (!descs.empty()) {
        size_t lo = 0, hi = descs.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (descs[mid].base <= addr) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo > 0) {
            const BlockDescriptor& desc = descs[lo - 1];
            if (addr >= desc.base && addr < desc.end) {
                return desc.slot_size;
            }
        }
    }

    // Check large objects (binary search)
    rebuild_large_descriptors();
    auto& ldescs = g_heap->large_descriptors;
    if (!ldescs.empty()) {
        size_t lo = 0, hi = ldescs.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (ldescs[mid].base <= addr) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo > 0) {
            const LargeObjDescriptor& ldesc = ldescs[lo - 1];
            if (addr == ldesc.base) {
                return ldesc.header->data_size;
            }
        }
    }

    return 0;
}

// Conservative scan: treat every aligned word in the object as a potential pointer
static void gc_scan_object(void* obj, size_t size) {
    // Scan all 8-byte-aligned words
    uintptr_t start = (uintptr_t)obj;
    uintptr_t end = start + size;
    // Align start up to 8 bytes
    start = (start + 7) & ~(uintptr_t)7;

    for (uintptr_t p = start; p + sizeof(void*) <= end; p += sizeof(void*)) {
        void* candidate = *(void**)p;
        // Quick filter: check if candidate looks like a heap pointer
        // Skip small values (likely integers, booleans, enum values)
        if ((uintptr_t)candidate < 4096) continue;
        // Skip values that are clearly not pointers (too large)
        if ((uintptr_t)candidate > 0x00007FFFFFFFFFFF) continue;
        gc_mark_ptr(candidate);
    }
}

// Push stack roots (conservative scan of current thread's stack + registers)
//
// Uses setjmp() to flush callee-saved registers (RBX, RBP, RSI, RDI, R12-R15)
// to the stack, then scans from a local variable's address up to StackBase.
// This matches the approach used by Boehm GC and other conservative collectors.
#ifdef _WIN32
static void __declspec(noinline) gc_push_conservative_stack_roots() {
    // Flush callee-saved registers to the stack via setjmp.
    // This ensures any GC pointers held in registers are visible to
    // the conservative scanner, even if they haven't been spilled to
    // a stack slot by the calling function.
    volatile jmp_buf regs;
    setjmp((jmp_buf&)regs);

    // Scan the jmp_buf contents as potential roots
    for (size_t i = 0; i < sizeof(jmp_buf) / sizeof(uintptr_t); i++) {
        uintptr_t val = ((volatile uintptr_t*)&regs)[i];
        if (val >= 4096 && val <= 0x00007FFFFFFFFFFF) {
            gc_mark_ptr((void*)val);
        }
    }

    // Get stack bounds from TEB
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    uintptr_t stack_high = (uintptr_t)tib->StackBase;

    // Use address of our local variable as the lowest scan point.
    // This is guaranteed to be at or below the current RSP, ensuring
    // we scan ALL stack frames above us including any pushed registers.
    volatile uintptr_t stack_anchor = 0;
    uintptr_t scan_start = ((uintptr_t)&stack_anchor) & ~(uintptr_t)7;

    // Scan from our stack frame up to StackBase
    for (uintptr_t p = scan_start; p + sizeof(void*) <= stack_high; p += sizeof(void*)) {
        void* candidate = *(void**)p;
        if ((uintptr_t)candidate < 4096) continue;
        if ((uintptr_t)candidate > 0x00007FFFFFFFFFFF) continue;
        gc_mark_ptr(candidate);
    }
}
#else
// Helper to get stack bounds via pthread on Linux
static void get_stack_bounds(uintptr_t* out_low, uintptr_t* out_high) {
    pthread_attr_t attr;
    void* stack_addr = nullptr;
    size_t stack_size = 0;

    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        pthread_attr_getstack(&attr, &stack_addr, &stack_size);
        pthread_attr_destroy(&attr);
        *out_low = (uintptr_t)stack_addr;
        *out_high = (uintptr_t)stack_addr + stack_size;
    } else {
        // Fallback: estimate from current stack pointer
        volatile uintptr_t anchor = 0;
        *out_low = (uintptr_t)&anchor;
        *out_high = (uintptr_t)&anchor + 1024 * 1024;
    }
}

static void __attribute__((noinline)) gc_push_conservative_stack_roots() {
    // Flush callee-saved registers to the stack via setjmp.
    volatile jmp_buf regs;
    setjmp((jmp_buf&)regs);

    // Scan the jmp_buf contents as potential roots
    for (size_t i = 0; i < sizeof(jmp_buf) / sizeof(uintptr_t); i++) {
        uintptr_t val = ((volatile uintptr_t*)&regs)[i];
        if (val >= 4096 && val <= 0x00007FFFFFFFFFFF) {
            gc_mark_ptr((void*)val);
        }
    }

    // Get stack bounds via pthread
    uintptr_t stack_low, stack_high;
    get_stack_bounds(&stack_low, &stack_high);

    // Use address of our local variable as the lowest scan point.
    volatile uintptr_t stack_anchor = 0;
    uintptr_t scan_start = ((uintptr_t)&stack_anchor) & ~(uintptr_t)7;

    // Scan from our stack frame up to stack top
    for (uintptr_t p = scan_start; p + sizeof(void*) <= stack_high; p += sizeof(void*)) {
        void* candidate = *(void**)p;
        if ((uintptr_t)candidate < 4096) continue;
        if ((uintptr_t)candidate > 0x00007FFFFFFFFFFF) continue;
        gc_mark_ptr(candidate);
    }
}
#endif

// Push precise stack roots from LLVM statepoints (if available)
// Forward-declared; defined in GCRoots.cpp
extern "C" void ts_gc_push_precise_stack_roots();

static void gc_push_global_roots() {
    // Global root pointers
    for (void** root : g_heap->global_roots) {
        if (root && *root) {
            gc_mark_ptr(*root);
        }
    }

    // Custom scanner callbacks
    for (auto& entry : g_heap->scanners) {
        entry.callback(entry.context);
    }
}

static void gc_mark_phase() {
    // Clear all mark bits
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        for (BlockHeader* bh = g_heap->block_lists[i]; bh; bh = bh->next) {
            memset(bh->mark_bits, 0, bitmap_bytes(bh->slot_count));
        }
    }
    for (LargeObjHeader* h = g_heap->large_sentinel.next;
         h != &g_heap->large_sentinel; h = h->next) {
        h->marked = false;
    }

    // Push roots
    g_heap->mark_worklist.clear();
    gc_push_global_roots();
    gc_push_conservative_stack_roots();
    ts_gc_push_precise_stack_roots();  // Additional precise roots from LLVM statepoints

    // Scan nursery objects as roots: nursery objects may reference old-gen objects
    // that must be kept alive. Without this, old-gen objects only referenced from
    // the nursery would be incorrectly swept.
    gc_scan_nursery_roots();

    // Process mark worklist (BFS)
    while (!g_heap->mark_worklist.empty()) {
        void* obj = g_heap->mark_worklist.back();
        g_heap->mark_worklist.pop_back();

        size_t size = gc_object_size(obj);
        if (size > 0) {
            gc_scan_object(obj, size);
        }
    }
}

// ============================================================================
// Weak Reference Processing (between mark and sweep)
// ============================================================================

static void gc_process_weak_refs() {
    for (void** loc : g_heap->weak_refs) {
        if (loc && *loc) {
            // Check if the target is still live
            void* target = *loc;
            void* base = gc_find_base(target);
            if (!base) {
                // Target not in heap (external pointer) - leave it
                continue;
            }

            // Check mark bit
            uintptr_t addr = (uintptr_t)base;
            bool is_marked = false;

            // Check small objects
            auto& descs = g_heap->block_descriptors;
            if (!descs.empty()) {
                size_t lo = 0, hi = descs.size();
                while (lo < hi) {
                    size_t mid = lo + (hi - lo) / 2;
                    if (descs[mid].base <= addr) lo = mid + 1;
                    else hi = mid;
                }
                if (lo > 0) {
                    const BlockDescriptor& desc = descs[lo - 1];
                    if (addr >= desc.base && addr < desc.end) {
                        size_t slot_idx = (addr - desc.base) / desc.slot_size;
                        is_marked = bitmap_get(desc.header->mark_bits, slot_idx);
                    }
                }
            }

            // Check large objects
            if (!is_marked) {
                for (LargeObjHeader* h = g_heap->large_sentinel.next;
                     h != &g_heap->large_sentinel; h = h->next) {
                    uintptr_t data_start = (uintptr_t)h + sizeof(LargeObjHeader);
                    if (addr == data_start) {
                        is_marked = h->marked;
                        break;
                    }
                }
            }

            if (!is_marked) {
                // Target is dead - clear the weak reference
                *loc = nullptr;
            }
        }
    }
}

// ============================================================================
// Finalizer Processing
// ============================================================================

// Forward declaration for calling finalizers after GC
// Actual signature: TsValue* ts_call_1(TsValue*, TsValue*)
// We use void* since we don't include TsObject.h here
extern "C" void* ts_call_1(void* func, void* arg1);

static void gc_process_finalizers() {
    // Check each finalizer target - if dead, queue cleanup callback
    // Callbacks are stored in pending_callbacks and invoked AFTER the GC
    // lock is released (no allocations allowed during GC).
    auto it = g_heap->finalizers.begin();
    while (it != g_heap->finalizers.end()) {
        void* base = gc_find_base(it->target);
        if (!base) {
            // Target not in heap - remove finalizer
            it = g_heap->finalizers.erase(it);
            continue;
        }

        // Check if target is marked
        bool is_marked = false;
        uintptr_t addr = (uintptr_t)base;

        auto& descs = g_heap->block_descriptors;
        if (!descs.empty()) {
            size_t lo = 0, hi = descs.size();
            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (descs[mid].base <= addr) lo = mid + 1;
                else hi = mid;
            }
            if (lo > 0) {
                const BlockDescriptor& desc = descs[lo - 1];
                if (addr >= desc.base && addr < desc.end) {
                    size_t slot_idx = (addr - desc.base) / desc.slot_size;
                    is_marked = bitmap_get(desc.header->mark_bits, slot_idx);
                }
            }
        }

        if (!is_marked) {
            for (LargeObjHeader* h = g_heap->large_sentinel.next;
                 h != &g_heap->large_sentinel; h = h->next) {
                uintptr_t data_start = (uintptr_t)h + sizeof(LargeObjHeader);
                if (addr == data_start) {
                    is_marked = h->marked;
                    break;
                }
            }
        }

        if (!is_marked && it->callback) {
            // Target is dead - mark the held_value as live so it survives
            // (it needs to be passed to the callback)
            gc_mark_ptr(it->held_value);
            gc_mark_ptr(it->callback);

            // Queue callback for execution after GC completes
            g_heap->pending_callbacks.push_back({ it->callback, it->held_value });

            it = g_heap->finalizers.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Sweep Phase
// ============================================================================

static void gc_sweep_phase() {
    size_t live_bytes = 0;
    size_t freed_small = 0;
    size_t freed_large = 0;

    // Sweep small object blocks
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        BlockHeader** prev_ptr = &g_heap->block_lists[i];
        BlockHeader* bh = *prev_ptr;

        while (bh) {
            BlockHeader* next = bh->next;
            size_t bm_bytes = bitmap_bytes(bh->slot_count);
            size_t new_live = 0;

            // dead = allocated & ~marked
            for (size_t j = 0; j < bm_bytes; j++) {
                uint8_t dead = bh->allocated_bits[j] & ~bh->mark_bits[j];
                if (dead) {
                    // Count freed slots in this byte
                    for (int bit = 0; bit < 8; bit++) {
                        if (dead & (1u << bit)) {
                            freed_small++;
                        }
                    }
                    // Clear dead slots from allocated
                    bh->allocated_bits[j] &= bh->mark_bits[j];
                }
                // Count remaining live
                uint8_t live = bh->allocated_bits[j];
                while (live) {
                    new_live++;
                    live &= (live - 1); // Clear lowest set bit
                }
            }

            bh->live_count = new_live;
            live_bytes += new_live * bh->slot_size;

            // Free completely empty blocks
            if (new_live == 0) {
                *prev_ptr = next;
                platform_free_block(bh->block_mem);
                free(bh->allocated_bits);
                free(bh->mark_bits);
                free(bh);
                g_heap->descriptors_dirty = true;
            } else {
                // Reset free cursor to start for better locality
                bh->free_cursor = 0;
                *prev_ptr = bh;
                prev_ptr = &bh->next;
            }

            bh = next;
        }
        *prev_ptr = nullptr;
    }

    // Sweep large objects
    bool any_large_freed = false;
    LargeObjHeader* h = g_heap->large_sentinel.next;
    while (h != &g_heap->large_sentinel) {
        LargeObjHeader* next = h->next;
        if (!h->marked) {
            // Unlink from list
            h->prev->next = h->next;
            h->next->prev = h->prev;
            freed_large++;
            any_large_freed = true;
            platform_free_large(h, h->alloc_size);
        } else {
            live_bytes += h->data_size;
        }
        h = next;
    }
    if (any_large_freed) g_heap->large_descriptors_dirty = true;

    // Update stats
    g_heap->total_allocated = live_bytes;
    g_heap->live_after_last_gc = live_bytes;

    // Adjust threshold: collect again when heap grows by growth factor from live size
    size_t new_threshold = (size_t)(live_bytes * g_gc_growth_factor);
    if (new_threshold < g_min_gc_threshold) {
        new_threshold = g_min_gc_threshold;
    }
    g_heap->gc_threshold = new_threshold;

    if (g_gc_verbose) {
        fprintf(stderr, "[TsGC] Collection #%zu: live=%zuKB, freed_small=%zu, freed_large=%zu, threshold=%zuKB\n",
                g_heap->collection_count, live_bytes / 1024,
                freed_small, freed_large, new_threshold / 1024);
        fflush(stderr);
    }
}

// ============================================================================
// Full Collection
// ============================================================================

// Debug: watched closure globals (defined here, referenced by TsClosure.cpp)
void* g_watched_closure_ptr = nullptr;
void* g_watched_fp_original_ptr = nullptr;

// Debug: check if the watched closure is corrupted
static void ts_check_watched_closure(const char* phase) {
    if (!g_watched_closure_ptr) return;
    void* currentFp = *(void**)((char*)g_watched_closure_ptr + 24); // offset of func_ptr
    if (currentFp != g_watched_fp_original_ptr) {
        fprintf(stderr, "[GC-WATCH] *** func_ptr corrupted during %s! was=%p now=%p ***\n",
            phase, g_watched_fp_original_ptr, currentFp);
        fflush(stderr);
    }
}

static void gc_collect_internal() {
    if (!g_heap) return;

    g_heap->collection_count++;
    g_in_collection = true;

    ts_check_watched_closure("pre-mark");
    auto t0 = std::chrono::high_resolution_clock::now();
    gc_mark_phase();
    ts_check_watched_closure("post-mark");
    auto t1 = std::chrono::high_resolution_clock::now();
    gc_process_weak_refs();
    gc_process_finalizers();
    ts_check_watched_closure("post-finalizers");
    gc_sweep_phase();
    ts_check_watched_closure("post-sweep");
    auto t2 = std::chrono::high_resolution_clock::now();

    g_in_collection = false;

    // NOTE: Do NOT clear the forwarding table after full GC!
    // The stack may still hold stale nursery pointers that need to be
    // forwarded during subsequent full GCs. The forwarding table is
    // only purged when the corresponding semi-space is reused (at the
    // start of gc_minor_collect_internal).

    if (g_gc_verbose) {
        auto mark_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        auto sweep_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
        fprintf(stderr, "[TsGC]   mark=%lldus, sweep=%lldus",
                (long long)mark_us, (long long)sweep_us);
        if (g_nursery.enabled) {
            fprintf(stderr, ", nursery=%zuKB/%zuKB (%zu allocs)",
                    g_nursery.cursor / 1024, g_nursery.region_size / 1024,
                    g_nursery.alloc_count);
        }
        fprintf(stderr, "\n");
        fflush(stderr);
    }
}

// ============================================================================
// Heap Initialization
// ============================================================================

static void gc_init() {
    if (g_heap) return;

    g_heap = new GCHeap();

    // Check for verbose mode
    if (getenv("TS_GC_VERBOSE")) {
        g_gc_verbose = true;
    }
    if (getenv("TS_GC_VERIFY_CARDS")) {
        g_verify_cards = true;
    }

    // Tunable GC parameters via environment variables
    const char* threshold_env = getenv("TS_GC_MIN_THRESHOLD_MB");
    if (threshold_env) {
        size_t mb = (size_t)atoi(threshold_env);
        if (mb > 0) {
            g_min_gc_threshold = mb * 1024 * 1024;
            g_heap->gc_threshold = g_min_gc_threshold;
        }
    }

    const char* growth_env = getenv("TS_GC_GROWTH_FACTOR");
    if (growth_env) {
        double f = atof(growth_env);
        if (f >= 1.5 && f <= 10.0) g_gc_growth_factor = f;
    }

    const char* max_heap_env = getenv("TS_GC_MAX_HEAP_MB");
    if (max_heap_env) {
        size_t mb = (size_t)atoi(max_heap_env);
        if (mb > 0) g_max_heap_size = mb * 1024 * 1024;
    }

    // Initialize nursery (bump-pointer allocator for short-lived objects)
    bool nursery_disabled = false;
    const char* nursery_env = getenv("TS_GC_NURSERY");
    if (nursery_env && strcmp(nursery_env, "0") == 0) {
        nursery_disabled = true;
    }

    if (!nursery_disabled) {
        size_t nursery_mb = 4;  // Default 4MB single region
        const char* nursery_mb_env = getenv("TS_GC_NURSERY_MB");
        if (nursery_mb_env) {
            size_t mb = (size_t)atoi(nursery_mb_env);
            if (mb >= 1 && mb <= 64) nursery_mb = mb;
        }

        g_nursery.region_size = nursery_mb * 1024 * 1024;

        g_nursery.region = platform_alloc_large(g_nursery.region_size);
        if (g_nursery.region) {
            g_nursery.cursor = 0;
            g_nursery.cursor_limit = g_nursery.region_size;
            g_nursery.enabled = true;
            g_nursery.total_allocated = 0;
            g_nursery.alloc_count = 0;
            g_nursery.high_water = 0;
            g_nursery.current_fragment = 0;
            g_nursery.pinned_count = 0;
            g_nursery.pinned_bytes = 0;

            ts_gc_nursery_base = (uint64_t)(uintptr_t)g_nursery.region;
            ts_gc_nursery_end = ts_gc_nursery_base + g_nursery.region_size;

            // Sync inline bump-pointer globals
            ts_nursery_cursor = (char*)g_nursery.region + g_nursery.cursor;
            ts_nursery_cursor_limit = (char*)g_nursery.region + g_nursery.cursor_limit;

            // Allocate card table for old-gen-to-nursery write barrier tracking.
            // Uses modular indexing (addr >> CARD_SHIFT) & (CARD_TABLE_SIZE - 1)
            // so no base address is needed.
            g_card_table = (uint8_t*)calloc(CARD_TABLE_SIZE, 1);
            if (g_card_table) {
                ts_gc_card_table_ptr = g_card_table;
            }
        }
    }

    if (g_gc_verbose) {
        fprintf(stderr, "[TsGC] Custom mark-sweep GC initialized "
                "(threshold=%zuMB, growth=%.1f, max_heap=%zuMB, nursery=%s",
                g_min_gc_threshold / (1024*1024),
                g_gc_growth_factor,
                g_max_heap_size / (1024*1024),
                g_nursery.enabled ? "on" : "off");
        if (g_nursery.enabled) {
            fprintf(stderr, " %zuMB, cards=%s",
                    g_nursery.region_size / (1024*1024),
                    g_card_table ? "on" : "off");
        }
        fprintf(stderr, ")\n");
        fflush(stderr);
    }
}

// ============================================================================
// Public API (extern "C")
// ============================================================================

extern "C" {

// Run pending finalizer callbacks outside the GC lock
static void gc_run_pending_callbacks() {
    if (!g_heap || g_heap->pending_callbacks.empty()) return;

    // Move callbacks to local vector so we don't hold references during execution
    std::vector<PendingCallback> callbacks;
    callbacks.swap(g_heap->pending_callbacks);

    // Execute callbacks (these may allocate, which is fine - lock is not held)
    for (auto& cb : callbacks) {
        ts_call_1(cb.callback, cb.arg);
    }
}

void* ts_gc_alloc(size_t size) {
    if (!g_heap) gc_init();
    if (size == 0) size = 8; // Minimum allocation

    // ---- Nursery fast path: no mutex, bump pointer in fragment ----
    if (g_nursery.enabled && size <= NURSERY_MAX_OBJ_SIZE) {
        // Sync cursor from exported globals (compiler inline path may have bumped it)
        nursery_sync_from_exported();

        size_t alloc_size = (size + 7) & ~(size_t)7;  // Align to 8 bytes
        size_t total = alloc_size + NURSERY_SIZE_PREFIX;

        // Try bump-allocate in current fragment
        if (g_nursery.cursor + total <= g_nursery.cursor_limit) {
            char* base = (char*)g_nursery.region + g_nursery.cursor;
            *(uint64_t*)base = (uint64_t)alloc_size;  // Size prefix, pin bit clear
            void* result = base + NURSERY_SIZE_PREFIX;
            g_nursery.cursor += total;
            if (g_nursery.cursor > g_nursery.high_water)
                g_nursery.high_water = g_nursery.cursor;
            g_nursery.total_allocated += alloc_size;
            g_nursery.alloc_count++;
            nursery_sync_to_exported();
            // Check if this allocation overwrites the watched closure
            if (g_watched_closure_ptr) {
                uintptr_t wAddr = (uintptr_t)g_watched_closure_ptr;
                uintptr_t aStart = (uintptr_t)result;
                uintptr_t aEnd = aStart + alloc_size;
                if (wAddr >= aStart && wAddr < aEnd) {
                    fprintf(stderr, "[ALLOC] *** OVERWRITING watched closure %p! alloc at %p size=%zu ***\n",
                        g_watched_closure_ptr, result, alloc_size);
                    fflush(stderr);
                }
                ts_check_watched_closure("alloc-fast");
            }
            return result;
        }

        // Try next fragment (if fragments exist from a previous minor GC)
        while (g_nursery.current_fragment + 1 < g_nursery.fragments.size()) {
            g_nursery.current_fragment++;
            auto& frag = g_nursery.fragments[g_nursery.current_fragment];
            if (frag.size >= total) {
                g_nursery.cursor = frag.offset;
                g_nursery.cursor_limit = frag.offset + frag.size;
                char* base = (char*)g_nursery.region + g_nursery.cursor;
                *(uint64_t*)base = (uint64_t)alloc_size;
                void* result = base + NURSERY_SIZE_PREFIX;
                g_nursery.cursor += total;
                if (g_nursery.cursor > g_nursery.high_water)
                    g_nursery.high_water = g_nursery.cursor;
                g_nursery.total_allocated += alloc_size;
                g_nursery.alloc_count++;
                nursery_sync_to_exported();
                return result;
            }
        }

        // All fragments exhausted - trigger minor GC
        {
            std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
            gc_minor_collect_internal();
        }
        nursery_sync_to_exported();

        // Retry after GC
        if (g_nursery.cursor + total <= g_nursery.cursor_limit) {
            char* base = (char*)g_nursery.region + g_nursery.cursor;
            *(uint64_t*)base = (uint64_t)alloc_size;
            void* result = base + NURSERY_SIZE_PREFIX;
            g_nursery.cursor += total;
            if (g_nursery.cursor > g_nursery.high_water)
                g_nursery.high_water = g_nursery.cursor;
            g_nursery.total_allocated += alloc_size;
            g_nursery.alloc_count++;
            nursery_sync_to_exported();
            return result;
        }
        // Still can't fit - fall through to old-gen
    }

    // ---- Old-gen path (mutex-protected) ----
    void* result = nullptr;
    std::vector<PendingCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(g_heap->gc_mutex);

        if (size <= MAX_SMALL_SIZE) {
            result = gc_alloc_small(size);
        } else {
            result = gc_alloc_large(size);
        }

        // OOM retry: force full collection and try once more
        if (!result) {
            gc_collect_internal();
            if (size <= MAX_SMALL_SIZE) {
                result = gc_alloc_small(size);
            } else {
                result = gc_alloc_large(size);
            }
        }

        // Final fallback: abort with diagnostic
        if (!result) {
            fprintf(stderr, "[TsGC] FATAL: Out of memory allocating %zu bytes "
                    "(heap=%zuMB, live=%zuMB, peak=%zuMB, collections=%zu)\n",
                    size, g_heap->total_allocated / (1024*1024),
                    g_heap->live_after_last_gc / (1024*1024),
                    g_heap->peak_allocated / (1024*1024),
                    g_heap->collection_count);
            fflush(stderr);
            abort();
        }

        // Grab pending callbacks while we hold the lock
        if (!g_heap->pending_callbacks.empty()) {
            callbacks.swap(g_heap->pending_callbacks);
        }
    }

    // Run finalizer callbacks outside the lock
    for (auto& cb : callbacks) {
        ts_call_1(cb.callback, cb.arg);
    }

    return result;
}

void* ts_gc_alloc_old_gen(size_t size) {
    if (!g_heap) gc_init();
    if (size == 0) size = 8;

    void* result = nullptr;
    std::vector<PendingCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(g_heap->gc_mutex);

        if (size <= MAX_SMALL_SIZE) {
            result = gc_alloc_small(size);
        } else {
            result = gc_alloc_large(size);
        }

        if (!result) {
            gc_collect_internal();
            if (size <= MAX_SMALL_SIZE) {
                result = gc_alloc_small(size);
            } else {
                result = gc_alloc_large(size);
            }
        }

        if (!result) {
            fprintf(stderr, "[TsGC] FATAL: Out of memory (old-gen) allocating %zu bytes\n", size);
            fflush(stderr);
            abort();
        }

        if (!g_heap->pending_callbacks.empty()) {
            callbacks.swap(g_heap->pending_callbacks);
        }
    }

    for (auto& cb : callbacks) {
        ts_call_1(cb.callback, cb.arg);
    }

    return result;
}

void* ts_gc_base(void* ptr) {
    if (!g_heap) return nullptr;
    // Skip lock if called during collection (e.g. from GCRoots precise root pushing)
    if (g_in_collection) return gc_find_base(ptr);
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    return gc_find_base(ptr);
}

void* ts_gc_realloc(void* ptr, size_t old_size, size_t new_size) {
    if (!ptr) return ts_gc_alloc(new_size);
    void* newp = ts_gc_alloc(new_size);  // Never returns null (aborts on OOM)
    size_t copy_size = old_size < new_size ? old_size : new_size;
    memcpy(newp, ptr, copy_size);
    // Old allocation will be collected when no longer referenced
    return newp;
}

void ts_gc_register_root(void** location) {
    if (!g_heap) gc_init();
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    g_heap->global_roots.push_back(location);
}

void ts_gc_unregister_root(void** location) {
    if (!g_heap) return;
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    auto& roots = g_heap->global_roots;
    roots.erase(std::remove(roots.begin(), roots.end(), location), roots.end());
}

void ts_gc_register_scanner(ts_gc_scan_callback cb, void* context) {
    if (!g_heap) gc_init();
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    g_heap->scanners.push_back({ cb, context });
}

void ts_gc_register_minor_fixup(ts_gc_minor_fixup_callback cb, void* context) {
    g_minor_fixup_scanners.push_back({ cb, context });
}

void* ts_gc_minor_lookup_forward(void* ptr) {
    if (!g_current_forwarding || !ptr || !is_nursery_ptr(ptr)) return ptr;
    uintptr_t addr = (uintptr_t)ptr;
    auto& fwd = *g_current_forwarding;
    size_t lo = 0, hi = fwd.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (fwd[mid].nursery_addr + fwd[mid].size <= addr) {
            lo = mid + 1;
        } else if (fwd[mid].nursery_addr > addr) {
            hi = mid;
        } else {
            size_t off = addr - fwd[mid].nursery_addr;
            return (char*)fwd[mid].old_gen_addr + off;
        }
    }
    return ptr; // Not forwarded (pinned)
}

void ts_gc_mark_object(void* ptr) {
    // Called from scanner callbacks during mark phase
    // No lock needed - mark phase already holds the lock
    if (!g_heap || !ptr) return;
    gc_mark_ptr(ptr);
}

void ts_gc_register_weak_ref(void** location) {
    if (!g_heap) gc_init();
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    g_heap->weak_refs.push_back(location);
}

void ts_gc_unregister_weak_ref(void** location) {
    if (!g_heap) return;
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    auto& refs = g_heap->weak_refs;
    refs.erase(std::remove(refs.begin(), refs.end(), location), refs.end());
}

void ts_gc_register_finalizer(void* target, void* callback,
                              void* held_value, void* unregister_token) {
    if (!g_heap) gc_init();
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    g_heap->finalizers.push_back({ target, callback, held_value, unregister_token });
}

bool ts_gc_unregister_finalizer(void* unregister_token) {
    if (!g_heap) return false;
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    auto& fins = g_heap->finalizers;
    auto it = std::remove_if(fins.begin(), fins.end(),
        [unregister_token](const FinalizerEntry& e) {
            return e.unregister_token == unregister_token;
        });
    if (it != fins.end()) {
        fins.erase(it, fins.end());
        return true;
    }
    return false;
}

void ts_gc_force_collect() {
    if (!g_heap) return;
    std::vector<PendingCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
        gc_collect_internal();
        if (!g_heap->pending_callbacks.empty()) {
            callbacks.swap(g_heap->pending_callbacks);
        }
    }
    // Run finalizer callbacks outside the lock
    for (auto& cb : callbacks) {
        ts_call_1(cb.callback, cb.arg);
    }
}

size_t ts_gc_heap_size() {
    return g_heap ? g_heap->total_allocated : 0;
}

size_t ts_gc_live_size() {
    return g_heap ? g_heap->live_after_last_gc : 0;
}

size_t ts_gc_collection_count() {
    return g_heap ? g_heap->collection_count : 0;
}

bool ts_gc_is_nursery(void* ptr) {
    if (!g_nursery.enabled || !ptr) return false;
    return is_nursery_ptr(ptr);
}

void ts_gc_nursery_info(void** out_base, size_t* out_size) {
    if (out_base) *out_base = g_nursery.region;
    if (out_size) *out_size = g_nursery.region_size;
}

} // extern "C" -- pause for static internal function

// ============================================================================
// Minor GC (Pin-Based Promotion, SGen-style) - called with gc_mutex held
// ============================================================================
// Phase 0: Scan stack → PIN nursery objects found on stack
// Phase 1: Copy NON-PINNED survivors to old-gen (ephemeral forwarding table)
// Phase 2: Fix promoted objects' internal pointers
// Phase 3: Fix dirty card slots (old-gen → nursery pointers)
// Phase 3b: Fix non-GC slots
// Phase 4: Fix global roots
// Phase 5: Fix weak refs / finalizers
// Phase 6: Build fragment list from gaps between pinned objects
// Phase 7: Clear/re-dirty card table

// ============================================================================
// Nursery Liveness Tracing
// ============================================================================
// Marks nursery objects that are reachable from roots. Dead (unmarked) objects
// are skipped during promotion, so they stay in the nursery and get wiped on
// reset — zero cost, same as V8's young generation.

// Nursery object entry for fast binary-search lookup during liveness tracing.
struct NurseryObjEntry {
    uintptr_t start;      // Object data start address (after prefix)
    uintptr_t prefix_ptr; // Address of the 8-byte size prefix
    size_t size;          // Object data size
};

static void gc_mark_nursery_live() {
    // Step 1: Build sorted nursery object table for O(log N) lookup.
    // This replaces the O(N) nursery_find_base() walk that was the bottleneck.
    std::vector<NurseryObjEntry> nursery_objects;
    nursery_objects.reserve(4096);

    {
        char* base = (char*)g_nursery.region;
        size_t offset = 0;
        while (offset + NURSERY_SIZE_PREFIX <= g_nursery.high_water) {
            uint64_t raw = *(uint64_t*)(base + offset);
            size_t obj_size = nursery_get_size(raw);
            if (obj_size == 0) { offset += 8; continue; }
            if (obj_size > NURSERY_MAX_OBJ_SIZE) break;
            NurseryObjEntry entry;
            entry.start = (uintptr_t)(base + offset + NURSERY_SIZE_PREFIX);
            entry.prefix_ptr = (uintptr_t)(base + offset);
            entry.size = obj_size;
            nursery_objects.push_back(entry);
            offset += NURSERY_SIZE_PREFIX + obj_size;
        }
    }

    if (nursery_objects.empty()) return;

    // Worklist of indices into nursery_objects to scan
    std::vector<size_t> worklist;
    worklist.reserve(1024);

    size_t marked_count = 0;

    // Helper: given a candidate pointer, binary search for containing nursery object.
    // Returns index into nursery_objects or SIZE_MAX if not found.
    auto find_nursery_obj = [&](uintptr_t addr) -> size_t {
        // Binary search: find last entry with start <= addr
        size_t lo = 0, hi = nursery_objects.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (nursery_objects[mid].start <= addr) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo == 0) return SIZE_MAX;
        lo--;  // nursery_objects[lo].start <= addr
        if (addr < nursery_objects[lo].start + nursery_objects[lo].size) {
            return lo;  // addr is within this object
        }
        return SIZE_MAX;
    };

    // Helper: mark a nursery object as live, add to worklist if newly marked
    auto mark_nursery_obj = [&](void* ptr) {
        if (!is_nursery_ptr(ptr)) return;
        size_t idx = find_nursery_obj((uintptr_t)ptr);
        if (idx == SIZE_MAX) return;
        uint64_t* prefix = (uint64_t*)nursery_objects[idx].prefix_ptr;
        if (!nursery_is_marked(*prefix)) {
            nursery_set_marked(prefix);
            worklist.push_back(idx);
            marked_count++;
        }
    };

    // Root source 1: Pinned objects (stack roots) — already pinned by Phase 0
    for (size_t i = 0; i < nursery_objects.size(); i++) {
        uint64_t raw = *(uint64_t*)nursery_objects[i].prefix_ptr;
        if (nursery_is_pinned(raw)) {
            nursery_set_marked((uint64_t*)nursery_objects[i].prefix_ptr);
            worklist.push_back(i);
            marked_count++;
        }
    }

    // Root source 2: Global roots
    for (void** root : g_heap->global_roots) {
        if (root && *root) mark_nursery_obj(*root);
    }

    // Root source 3: Old-gen → nursery pointers (card-table guided block scan)
    // Uses modular card indexing: for each allocated old-gen slot, check if its
    // card is dirty, and if so scan the slot for nursery pointers.
    // This avoids address reconstruction issues with modular indexing.
    if (g_card_table) {
        for (size_t sc = 0; sc < NUM_SIZE_CLASSES; sc++) {
            for (BlockHeader* bh = g_heap->block_lists[sc]; bh; bh = bh->next) {
                if (!bh->block_mem || bh->live_count == 0) continue;
                uintptr_t bstart = (uintptr_t)bh->block_mem;
                for (size_t slot = 0; slot < bh->slot_count; slot++) {
                    if (!(bh->allocated_bits[slot / 8] & (1 << (slot % 8)))) continue;
                    uintptr_t s = bstart + slot * bh->slot_size;
                    // Check if any card covering this slot is dirty
                    if (!card_range_is_dirty(s, bh->slot_size)) continue;
                    uintptr_t e = s + bh->slot_size;
                    for (uintptr_t p = s; p + sizeof(void*) <= e; p += sizeof(void*)) {
                        void* c = *(void**)p;
                        if ((uintptr_t)c < 4096 || (uintptr_t)c > 0x00007FFFFFFFFFFF) continue;
                        mark_nursery_obj(c);
                    }
                }
            }
        }
        // Large objects
        for (LargeObjHeader* lo = g_heap->large_sentinel.next;
             lo != &g_heap->large_sentinel; lo = lo->next) {
            if (lo->data_size == 0 || lo->data_size > (size_t)2 * 1024 * 1024 * 1024) continue;
            uintptr_t s = (uintptr_t)lo + sizeof(LargeObjHeader);
            if (!card_range_is_dirty(s, lo->data_size)) continue;
            uintptr_t e = s + lo->data_size;
            for (uintptr_t p = s; p + sizeof(void*) <= e; p += sizeof(void*)) {
                void* c = *(void**)p;
                if ((uintptr_t)c < 4096 || (uintptr_t)c > 0x00007FFFFFFFFFFF) continue;
                mark_nursery_obj(c);
            }
        }
    }
    // Also scan non-GC nursery slots (outside GC-managed memory)
    for (void** slot : g_non_gc_nursery_slots) {
        if (slot && *slot) mark_nursery_obj(*slot);
    }

    // Root source 3v: Verification-only full old-gen scan.
    // Only runs when TS_GC_VERIFY_CARDS=1 for debugging.
    if (g_verify_cards) {
        size_t verify_found = 0;
        auto verify_and_mark = [&](uintptr_t p) {
            void* c = *(void**)p;
            if ((uintptr_t)c < 4096 || (uintptr_t)c > 0x00007FFFFFFFFFFF) return;
            if (!is_nursery_ptr(c)) return;
            size_t nidx = find_nursery_obj((uintptr_t)c);
            if (nidx == SIZE_MAX) return;
            uint64_t* prefix = (uint64_t*)nursery_objects[nidx].prefix_ptr;
            if (nursery_is_marked(*prefix)) return;
            nursery_set_marked(prefix);
            worklist.push_back(nidx);
            marked_count++;
            verify_found++;
            if (verify_found <= 10) {
                size_t cidx = card_index(p);
                fprintf(stderr, "[TsGC] MISSED: slot %p -> nursery %p (card=%zu %s)\n",
                    (void*)p, c, cidx, g_card_table[cidx] ? "DIRTY" : "CLEAN");
                fflush(stderr);
            }
        };
        for (size_t sc_v = 0; sc_v < NUM_SIZE_CLASSES; sc_v++) {
            for (BlockHeader* bh = g_heap->block_lists[sc_v]; bh; bh = bh->next) {
                if (!bh->block_mem || bh->live_count == 0) continue;
                uintptr_t bstart = (uintptr_t)bh->block_mem;
                for (size_t slot = 0; slot < bh->slot_count; slot++) {
                    if (!(bh->allocated_bits[slot / 8] & (1 << (slot % 8)))) continue;
                    uintptr_t s = bstart + slot * bh->slot_size;
                    uintptr_t e = s + bh->slot_size;
                    for (uintptr_t p = s; p + sizeof(void*) <= e; p += sizeof(void*)) {
                        verify_and_mark(p);
                    }
                }
            }
        }
        for (LargeObjHeader* lo = g_heap->large_sentinel.next;
             lo != &g_heap->large_sentinel; lo = lo->next) {
            if (lo->data_size == 0 || lo->data_size > (size_t)2 * 1024 * 1024 * 1024) continue;
            uintptr_t s = (uintptr_t)lo + sizeof(LargeObjHeader);
            uintptr_t e = s + lo->data_size;
            for (uintptr_t p = s; p + sizeof(void*) <= e; p += sizeof(void*)) {
                verify_and_mark(p);
            }
        }
        if (verify_found > 0) {
            fprintf(stderr, "[TsGC] VERIFY: full scan found %zu missed nursery refs!\n", verify_found);
            fflush(stderr);
        }
    }

    // Root source 4: Weak refs
    for (void** loc : g_heap->weak_refs) {
        if (loc && *loc) mark_nursery_obj(*loc);
    }

    // Root source 5: Finalizer entries
    for (auto& fin : g_heap->finalizers) {
        mark_nursery_obj(fin.target);
        mark_nursery_obj(fin.callback);
        mark_nursery_obj(fin.held_value);
    }

    // BFS tracing: scan marked nursery objects for nursery-to-nursery pointers
    while (!worklist.empty()) {
        size_t idx = worklist.back();
        worklist.pop_back();

        uintptr_t start = nursery_objects[idx].start;
        uintptr_t end = start + nursery_objects[idx].size;

        for (uintptr_t p = start; p + sizeof(void*) <= end; p += sizeof(void*)) {
            void* c = *(void**)p;
            if ((uintptr_t)c < 4096 || (uintptr_t)c > 0x00007FFFFFFFFFFF) continue;
            mark_nursery_obj(c);
        }
    }

    if (g_gc_verbose) {
        fprintf(stderr, "[TsGC] nursery liveness: %zu live (marked), %zu dead (skipped) of %zu total\n",
                marked_count, nursery_objects.size() - marked_count, nursery_objects.size());
        fflush(stderr);
    }
}

// Pin nursery objects found on the stack (conservative scan).
// False positives merely pin an extra object -- wasteful but SAFE.
// No stack rewriting; stack pointers to pinned objects remain valid.
static void gc_pin_nursery_stack_roots() {
    g_nursery.pinned_count = 0;
    g_nursery.pinned_bytes = 0;

    auto pin_if_nursery = [](uintptr_t val) {
        // Skip low addresses and kernel-mode addresses
        if (val < 4096 || val > 0x00007FFFFFFFFFFF) return;
        void* candidate = (void*)val;
        if (!is_nursery_ptr(candidate)) return;

        // Walk nursery objects to find the one containing this address
        void* base = nursery_find_base(candidate);
        if (!base) return;

        // Pin the object via bit 63 in the size prefix
        uint64_t* prefix = (uint64_t*)((char*)base - NURSERY_SIZE_PREFIX);
        if (!nursery_is_pinned(*prefix)) {
            nursery_set_pinned(prefix);
            g_nursery.pinned_count++;
            g_nursery.pinned_bytes += nursery_get_size(*prefix);
        }
    };

    // Flush callee-saved registers to the stack so they can be scanned
    volatile jmp_buf regs;
    setjmp((jmp_buf&)regs);

    // Scan register contents from jmp_buf
    for (size_t i = 0; i < sizeof(jmp_buf) / sizeof(uintptr_t); i++) {
        pin_if_nursery(((volatile uintptr_t*)&regs)[i]);
    }

    // Scan the thread's stack
#ifdef _WIN32
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    uintptr_t stack_high = (uintptr_t)tib->StackBase;
#else
    uintptr_t stack_low_unused, stack_high;
    get_stack_bounds(&stack_low_unused, &stack_high);
#endif

    volatile uintptr_t stack_anchor = 0;
    uintptr_t scan_start = ((uintptr_t)&stack_anchor) & ~(uintptr_t)7;

    if (g_gc_verbose) {
        fprintf(stderr, "[TsGC] pin scan: stack range [%p .. %p] (%zuKB)\n",
                (void*)scan_start, (void*)stack_high,
                (stack_high - scan_start) / 1024);
        fflush(stderr);
    }

    for (uintptr_t p = scan_start; p + sizeof(void*) <= stack_high; p += sizeof(void*)) {
        pin_if_nursery(*(uintptr_t*)p);
    }
}

static void gc_minor_collect_internal() {
    if (!g_nursery.enabled || g_nursery.high_water == 0) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    // Before promotion, ensure old-gen has room.
    if (g_heap->total_allocated + g_nursery.high_water > g_max_heap_size) {
        gc_collect_internal();
        if (g_gc_verbose) {
            fprintf(stderr, "[TsGC] minor GC: pre-promotion full GC, total_allocated now %zuKB\n",
                    g_heap->total_allocated / 1024);
            fflush(stderr);
        }
    }

    ts_check_watched_closure("minor-pre-pin");
    // Phase 0: Pin nursery objects referenced from the stack
    gc_pin_nursery_stack_roots();
    ts_check_watched_closure("minor-post-pin");

    // Phase 0b: Mark live nursery objects (root discovery + BFS tracing)
    gc_mark_nursery_live();
    ts_check_watched_closure("minor-post-mark-live");

    // Temporarily boost GC threshold to prevent gc_alloc_small from
    // triggering a full gc_collect_internal() during promotion
    size_t saved_threshold = g_heap->gc_threshold;
    g_heap->gc_threshold = (size_t)-1;

    // Phase 1: Walk nursery objects, copy NON-PINNED to old-gen
    std::vector<ForwardEntry> forwarding;

    char* base = (char*)g_nursery.region;
    size_t offset = 0;
    size_t promoted_count = 0;
    size_t promoted_bytes = 0;

    while (offset + NURSERY_SIZE_PREFIX <= g_nursery.high_water) {
        uint64_t raw_prefix = *(uint64_t*)(base + offset);
        size_t obj_size = nursery_get_size(raw_prefix);

        if (obj_size == 0) {
            offset += 8;  // Skip gap
            continue;
        }
        if (obj_size > NURSERY_MAX_OBJ_SIZE) break;

        void* nursery_obj = base + offset + NURSERY_SIZE_PREFIX;

        if (nursery_is_pinned(raw_prefix)) {
            // Pinned: stays in nursery, skip
            offset += NURSERY_SIZE_PREFIX + obj_size;
            continue;
        }

        // Skip unmarked (dead) objects — they stay in nursery and get wiped on reset
        if (!nursery_is_marked(raw_prefix)) {
            offset += NURSERY_SIZE_PREFIX + obj_size;
            continue;
        }

        // Live, non-pinned: promote to old-gen
        void* old_gen_obj = gc_alloc_small(obj_size);
        if (old_gen_obj) {
            memcpy(old_gen_obj, nursery_obj, obj_size);
            ForwardEntry fe;
            fe.nursery_addr = (uintptr_t)nursery_obj;
            fe.old_gen_addr = old_gen_obj;
            fe.size = obj_size;
            forwarding.push_back(fe);
            promoted_count++;
            promoted_bytes += obj_size;
        } else {
            // OOM during promotion - pin everything remaining
            if (g_gc_verbose) {
                fprintf(stderr, "[TsGC] minor GC: OOM during promotion at %zu bytes\n",
                        promoted_bytes);
                fflush(stderr);
            }
            // Mark this object as pinned so it stays
            nursery_set_pinned((uint64_t*)(base + offset));
            g_nursery.pinned_count++;
            g_nursery.pinned_bytes += obj_size;
        }

        offset += NURSERY_SIZE_PREFIX + obj_size;
    }

    // Restore GC threshold
    g_heap->gc_threshold = saved_threshold;

    // Sort forwarding table by nursery address for binary search
    if (!forwarding.empty()) {
        std::sort(forwarding.begin(), forwarding.end(),
                  [](const ForwardEntry& a, const ForwardEntry& b) {
                      return a.nursery_addr < b.nursery_addr;
                  });
    }

    // Lookup helper: given a nursery pointer, return old-gen address if promoted,
    // or return the same pointer if pinned (stays in nursery).
    auto lookup_forward = [&](void* ptr) -> void* {
        if (!is_nursery_ptr(ptr)) return ptr;
        uintptr_t addr = (uintptr_t)ptr;

        size_t lo = 0, hi = forwarding.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (forwarding[mid].nursery_addr + forwarding[mid].size <= addr) {
                lo = mid + 1;
            } else if (forwarding[mid].nursery_addr > addr) {
                hi = mid;
            } else {
                size_t off = addr - forwarding[mid].nursery_addr;
                return (char*)forwarding[mid].old_gen_addr + off;
            }
        }
        return ptr; // Not forwarded (pinned or unknown)
    };

    ts_check_watched_closure("minor-post-promote");
    // Phase 2: Fix up promoted objects' internal nursery pointers
    if (g_gc_verbose) { fprintf(stderr, "[TsGC] minor GC: entering Phase 2 (%zu forwarded)\n", forwarding.size()); fflush(stderr); }
    for (auto& fwd : forwarding) {
        uintptr_t obj_start = (uintptr_t)fwd.old_gen_addr;
        uintptr_t obj_end = obj_start + fwd.size;

        for (uintptr_t p = obj_start; p + sizeof(void*) <= obj_end; p += sizeof(void*)) {
            void* candidate = *(void**)p;
            if (!candidate) continue;
            if ((uintptr_t)candidate < 4096) continue;
            if ((uintptr_t)candidate > 0x00007FFFFFFFFFFF) continue;

            if (is_nursery_ptr(candidate)) {
                void* forwarded = lookup_forward(candidate);
                if (forwarded != candidate) {
                    // Promoted → rewrite to old-gen address
                    *(void**)p = forwarded;
                } else {
                    // Pinned → leave as-is (still valid nursery address)
                    // Dirty the card for THIS specific slot so next minor GC finds it.
                    if (g_card_table) {
                        g_card_table[card_index(p)] = 1;
                    }
                }
            }
        }
    }

    ts_check_watched_closure("minor-post-phase2");
    // Phase 2b: Fix up PINNED nursery objects' internal pointers.
    // Pinned objects may reference other nursery objects that were promoted.
    // Those internal pointers must be rewritten to the new old-gen addresses.
    if (g_gc_verbose) { fprintf(stderr, "[TsGC] minor GC: entering Phase 2b\n"); fflush(stderr); }
    if (g_nursery.pinned_count > 0 && !forwarding.empty()) {
        offset = 0;
        while (offset + NURSERY_SIZE_PREFIX <= g_nursery.high_water) {
            uint64_t raw_prefix = *(uint64_t*)(base + offset);
            size_t obj_size = nursery_get_size(raw_prefix);

            if (obj_size == 0) {
                offset += 8;
                continue;
            }
            if (obj_size > NURSERY_MAX_OBJ_SIZE) break;

            if (nursery_is_pinned(raw_prefix)) {
                // This is a pinned object - fix its internal pointers
                uintptr_t obj_start = (uintptr_t)(base + offset + NURSERY_SIZE_PREFIX);
                uintptr_t obj_end = obj_start + obj_size;

                for (uintptr_t p = obj_start; p + sizeof(void*) <= obj_end; p += sizeof(void*)) {
                    void* candidate = *(void**)p;
                    if (!candidate) continue;
                    if ((uintptr_t)candidate < 4096) continue;
                    if ((uintptr_t)candidate > 0x00007FFFFFFFFFFF) continue;

                    if (is_nursery_ptr(candidate)) {
                        void* forwarded = lookup_forward(candidate);
                        if (forwarded != candidate) {
                            *(void**)p = forwarded;  // Promoted → rewrite
                        }
                        // If pinned → leave as-is (still valid nursery addr)
                    }
                }
            }

            offset += NURSERY_SIZE_PREFIX + obj_size;
        }
    }

    ts_check_watched_closure("minor-post-phase2b");
    if (g_gc_verbose) { fprintf(stderr, "[TsGC] minor GC: entering Phase 3\n"); fflush(stderr); }
    // Phase 3: Scan old-gen slots with dirty cards for nursery pointers and fix them.
    // Uses modular card indexing: iterate allocated slots, check card, fix pointers.
    {
        size_t phase3_fixups = 0;
        // Collect card indices to re-dirty for pinned nursery references.
        // Can't re-dirty inline because bulk clear follows the scan.
        std::vector<size_t> redirty_cards;
        auto fixup_word = [&](uintptr_t p) {
            void* candidate = *(void**)p;
            if (!candidate) return;
            if ((uintptr_t)candidate < 4096) return;
            if ((uintptr_t)candidate > 0x00007FFFFFFFFFFF) return;

            if (is_nursery_ptr(candidate)) {
                void* forwarded = lookup_forward(candidate);
                if (forwarded != candidate) {
                    *(void**)p = forwarded;  // Promoted → rewrite
                    phase3_fixups++;
                } else {
                    // Pinned → record card for re-dirtying after bulk clear
                    redirty_cards.push_back(card_index(p));
                }
            }
        };

        if (g_card_table) {
            // Scan all allocated slots whose cards are dirty (don't clear yet —
            // modular indexing means multiple slots can alias the same card).
            for (size_t sc = 0; sc < NUM_SIZE_CLASSES; sc++) {
                for (BlockHeader* bh = g_heap->block_lists[sc]; bh; bh = bh->next) {
                    if (!bh->block_mem || bh->live_count == 0) continue;
                    uintptr_t bstart = (uintptr_t)bh->block_mem;
                    for (size_t slot = 0; slot < bh->slot_count; slot++) {
                        if (!(bh->allocated_bits[slot / 8] & (1 << (slot % 8)))) continue;
                        uintptr_t s = bstart + slot * bh->slot_size;
                        if (!card_range_is_dirty(s, bh->slot_size)) continue;
                        uintptr_t e = s + bh->slot_size;
                        for (uintptr_t p = s; p + sizeof(void*) <= e; p += sizeof(void*)) {
                            fixup_word(p);
                        }
                    }
                }
            }
            // Large objects
            for (LargeObjHeader* lo = g_heap->large_sentinel.next;
                 lo != &g_heap->large_sentinel; lo = lo->next) {
                if (lo->data_size == 0 || lo->data_size > (size_t)2 * 1024 * 1024 * 1024) continue;
                uintptr_t s = (uintptr_t)lo + sizeof(LargeObjHeader);
                if (!card_range_is_dirty(s, lo->data_size)) continue;
                uintptr_t e = s + lo->data_size;
                for (uintptr_t p = s; p + sizeof(void*) <= e; p += sizeof(void*)) {
                    fixup_word(p);
                }
            }

            // Bulk clear card table, then re-dirty for pinned references
            memset(g_card_table, 0, CARD_TABLE_SIZE);
            for (size_t idx : redirty_cards) {
                g_card_table[idx] = 1;
            }
        }

        if (g_gc_verbose && phase3_fixups > 0) {
            fprintf(stderr, "[TsGC] minor GC phase 3: fixed %zu old-gen → nursery pointers (card scan)\n",
                    phase3_fixups);
            fflush(stderr);
        }

        // Phase 3v: Verification-only full old-gen scan.
        if (g_verify_cards) {
            size_t card_only_fixups = phase3_fixups;
            for (size_t sc_v = 0; sc_v < NUM_SIZE_CLASSES; sc_v++) {
                for (BlockHeader* bh = g_heap->block_lists[sc_v]; bh; bh = bh->next) {
                    if (!bh->block_mem || bh->live_count == 0) continue;
                    uintptr_t bstart = (uintptr_t)bh->block_mem;
                    for (size_t slot = 0; slot < bh->slot_count; slot++) {
                        if (!(bh->allocated_bits[slot / 8] & (1 << (slot % 8)))) continue;
                        uintptr_t s = bstart + slot * bh->slot_size;
                        uintptr_t e = s + bh->slot_size;
                        for (uintptr_t p = s; p + sizeof(void*) <= e; p += sizeof(void*)) {
                            fixup_word(p);
                        }
                    }
                }
            }
            for (LargeObjHeader* lo = g_heap->large_sentinel.next;
                 lo != &g_heap->large_sentinel; lo = lo->next) {
                if (lo->data_size == 0 || lo->data_size > (size_t)2 * 1024 * 1024 * 1024) continue;
                uintptr_t s = (uintptr_t)lo + sizeof(LargeObjHeader);
                uintptr_t e = s + lo->data_size;
                for (uintptr_t p = s; p + sizeof(void*) <= e; p += sizeof(void*)) {
                    fixup_word(p);
                }
            }
            size_t missed_fixups = phase3_fixups - card_only_fixups;
            if (missed_fixups > 0) {
                fprintf(stderr, "[TsGC] VERIFY: full scan found %zu missed fixups (card scan found %zu)\n",
                        missed_fixups, card_only_fixups);
                fflush(stderr);
            }
        }
    }

    if (g_gc_verbose) { fprintf(stderr, "[TsGC] minor GC: entering Phase 3b\n"); fflush(stderr); }
    // Phase 3b: Fix up non-GC slots (slots outside card table coverage).
    // These slot addresses may become stale if the GC block they pointed into
    // was freed by a full GC between the write barrier and now.
    // Validate each address before reading.
    if (!g_non_gc_nursery_slots.empty()) {
        size_t non_gc_fixups = 0;
        size_t non_gc_skipped = 0;
        for (void** slot : g_non_gc_nursery_slots) {
            if (!slot) continue;
            // Validate the slot address is still in committed memory
#ifdef _WIN32
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(slot, &mbi, sizeof(mbi)) == 0 ||
                mbi.State != MEM_COMMIT ||
                !(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
                non_gc_skipped++;
                continue;
            }
#else
            // On Linux, skip memory validation for non-GC slots.
            // gc_find_base() below will validate the pointer is in a live allocation.
#endif
            // Also verify the slot is in a live GC allocation (not a freed block).
            // gc_find_base returns non-null only for addresses in live allocations.
            void* base = gc_find_base(slot);
            if (!base) {
                // Not in a live GC allocation - could be malloc'd memory or freed GC memory.
                // For malloc'd memory, we'd want to fix up, but we can't distinguish
                // freed GC memory from valid malloc'd memory safely.
                // Phase 3 already scanned all live GC memory, so skip.
                non_gc_skipped++;
                continue;
            }
            if (*slot && is_nursery_ptr(*slot)) {
                void* forwarded = lookup_forward(*slot);
                if (forwarded != *slot) {
                    *slot = forwarded;
                    non_gc_fixups++;
                }
            }
        }
        if (g_gc_verbose && (non_gc_fixups > 0 || non_gc_skipped > 0)) {
            fprintf(stderr, "[TsGC] minor GC: Phase 3b: fixed %zu, skipped %zu stale slots\n",
                    non_gc_fixups, non_gc_skipped);
            fflush(stderr);
        }
        g_non_gc_nursery_slots.clear();
    }

    if (g_gc_verbose) { fprintf(stderr, "[TsGC] minor GC: entering Phase 4\n"); fflush(stderr); }
    // Phase 4: Fix up global roots
    for (void** root : g_heap->global_roots) {
        if (root && *root && is_nursery_ptr(*root)) {
            void* forwarded = lookup_forward(*root);
            if (forwarded != *root) {
                *root = forwarded;
            }
            // If still nursery (pinned), leave as-is - will be found next time
        }
    }

    if (g_gc_verbose) { fprintf(stderr, "[TsGC] minor GC: entering Phase 4b\n"); fflush(stderr); }
    // Phase 4b: Call registered minor GC fixup scanners.
    // These fix up nursery pointers in external caches/registries (malloc'd memory)
    // that are not covered by the card table.
    if (!g_minor_fixup_scanners.empty()) {
        g_current_forwarding = &forwarding;
        for (auto& entry : g_minor_fixup_scanners) {
            entry.callback(entry.context);
        }
        g_current_forwarding = nullptr;
    }

    if (g_gc_verbose) { fprintf(stderr, "[TsGC] minor GC: entering Phase 5\n"); fflush(stderr); }
    // Phase 5: Fix up weak refs
    for (void** loc : g_heap->weak_refs) {
        if (loc && *loc && is_nursery_ptr(*loc)) {
            void* forwarded = lookup_forward(*loc);
            if (forwarded != *loc) {
                *loc = forwarded;
            }
        }
    }

    if (g_gc_verbose) { fprintf(stderr, "[TsGC] minor GC: entering Phase 5b\n"); fflush(stderr); }
    // Phase 5b: Fix up finalizer entries
    for (auto& fin : g_heap->finalizers) {
        auto fixup_ptr = [&](void*& ptr) {
            if (ptr && is_nursery_ptr(ptr)) {
                void* fwd = lookup_forward(ptr);
                if (fwd != ptr) ptr = fwd;
            }
        };
        fixup_ptr(fin.target);
        fixup_ptr(fin.callback);
        fixup_ptr(fin.held_value);
        fixup_ptr(fin.unregister_token);
    }

    if (g_gc_verbose) { fprintf(stderr, "[TsGC] minor GC: entering Phase 6\n"); fflush(stderr); }
    // Phase 6: Build fragment list from gaps between pinned objects
    g_nursery.fragments.clear();
    g_nursery.current_fragment = 0;

    if (g_nursery.pinned_count == 0) {
        // No pinned objects! Full nursery reset (most common case).
        // Zero the entire used region so prefix-walking works cleanly next time.
        memset(g_nursery.region, 0, g_nursery.high_water);
        g_nursery.cursor = 0;
        g_nursery.cursor_limit = g_nursery.region_size;
        g_nursery.high_water = 0;
        g_nursery.alloc_count = 0;
    } else {
        // Walk nursery, collect pinned object locations, build fragments
        // between them. Clear pin bits. Zero non-pinned regions.
        size_t frag_start = 0;  // Start of current free region
        offset = 0;

        while (offset + NURSERY_SIZE_PREFIX <= g_nursery.high_water) {
            uint64_t raw_prefix = *(uint64_t*)(base + offset);
            size_t obj_size = nursery_get_size(raw_prefix);

            if (obj_size == 0) {
                offset += 8;
                continue;
            }
            if (obj_size > NURSERY_MAX_OBJ_SIZE) break;

            size_t total_obj = NURSERY_SIZE_PREFIX + obj_size;

            if (nursery_is_pinned(raw_prefix)) {
                // Found a pinned object
                // Emit fragment for the gap before this pinned object
                if (offset > frag_start) {
                    size_t gap_size = offset - frag_start;
                    // Zero the gap (dead/promoted objects)
                    memset(base + frag_start, 0, gap_size);
                    if (gap_size >= NURSERY_SIZE_PREFIX + 8) {  // Must fit at least one min object
                        NurseryFragment frag;
                        frag.offset = frag_start;
                        frag.size = gap_size;
                        g_nursery.fragments.push_back(frag);
                    }
                }

                // Clear pin and mark bits (reset for next cycle)
                *(uint64_t*)(base + offset) &= ~NURSERY_META_MASK;

                // Next free region starts after this pinned object
                frag_start = offset + total_obj;
            } else {
                // Non-pinned object that was already promoted (or dead)
                // Will be zeroed when we process the gap
            }

            offset += total_obj;
        }

        // Final fragment: gap after the last pinned object to end of used region
        // (or to region end)
        if (frag_start < g_nursery.high_water) {
            size_t gap_size = g_nursery.high_water - frag_start;
            memset(base + frag_start, 0, gap_size);
            if (gap_size >= NURSERY_SIZE_PREFIX + 8) {
                NurseryFragment frag;
                frag.offset = frag_start;
                frag.size = gap_size;
                g_nursery.fragments.push_back(frag);
            }
        }
        // Also include the unused space beyond high_water
        if (g_nursery.high_water < g_nursery.region_size) {
            NurseryFragment frag;
            frag.offset = g_nursery.high_water;
            frag.size = g_nursery.region_size - g_nursery.high_water;
            g_nursery.fragments.push_back(frag);
        }

        // Set cursor to first fragment
        if (!g_nursery.fragments.empty()) {
            g_nursery.cursor = g_nursery.fragments[0].offset;
            g_nursery.cursor_limit = g_nursery.fragments[0].offset + g_nursery.fragments[0].size;
        } else {
            // Nursery is fully pinned - no free space
            g_nursery.cursor = g_nursery.region_size;
            g_nursery.cursor_limit = g_nursery.region_size;
        }
        g_nursery.alloc_count = 0;
        // high_water stays (pinned objects haven't moved)
    }

    if (g_gc_verbose) { fprintf(stderr, "[TsGC] minor GC: entering Phase 7\n"); fflush(stderr); }
    // Phase 7: Fix remaining dangling stack pointers to promoted objects.
    // The pin scan (Phase 0) can miss nursery pointers that are only in
    // caller-saved registers or spill slots not yet visible at scan time.
    // We conservatively rewrite exact-match forwarding addresses on the stack.
    // Safety: only rewrites values that exactly match a known nursery object
    // address (probability of false positive ≈ 53K*8/2^47 ≈ 3e-9 per slot).
    if (!forwarding.empty()) {
        volatile jmp_buf fix_regs;
        setjmp((jmp_buf&)fix_regs);

        // Scan register contents in jmp_buf
        for (size_t i = 0; i < sizeof(jmp_buf) / sizeof(uintptr_t); i++) {
            uintptr_t val = ((volatile uintptr_t*)&fix_regs)[i];
            if (val < 4096 || val > 0x00007FFFFFFFFFFF) continue;
            if (!is_nursery_ptr((void*)val)) continue;
            void* fwd = lookup_forward((void*)val);
            if (fwd != (void*)val) {
                ((volatile uintptr_t*)&fix_regs)[i] = (uintptr_t)fwd;
            }
        }

#ifdef _WIN32
        NT_TIB* fix_tib = (NT_TIB*)NtCurrentTeb();
        uintptr_t fix_stack_high = (uintptr_t)fix_tib->StackBase;
#else
        uintptr_t fix_stack_low_unused, fix_stack_high;
        get_stack_bounds(&fix_stack_low_unused, &fix_stack_high);
#endif
        volatile uintptr_t fix_anchor = 0;
        uintptr_t fix_start = ((uintptr_t)&fix_anchor) & ~(uintptr_t)7;

        size_t fixed_count = 0;
        size_t nursery_not_fwd = 0;
        for (uintptr_t p = fix_start; p + sizeof(void*) <= fix_stack_high; p += sizeof(void*)) {
            uintptr_t val = *(uintptr_t*)p;
            if (val < 4096 || val > 0x00007FFFFFFFFFFF) continue;
            if (!is_nursery_ptr((void*)val)) continue;
            void* fwd = lookup_forward((void*)val);
            if (fwd != (void*)val) {
                if (g_gc_verbose) {
                    fprintf(stderr, "[TsGC] Phase7: fix stack@%p: %p -> %p\n",
                            (void*)p, (void*)val, fwd);
                }
                *(uintptr_t*)p = (uintptr_t)fwd;
                fixed_count++;
            } else {
                nursery_not_fwd++;
            }
        }

        if (g_gc_verbose) {
            fprintf(stderr, "[TsGC] Phase7: range [%p..%p] (%zuKB), fixed %zu, nursery-unfwd %zu\n",
                    (void*)fix_start, (void*)fix_stack_high,
                    (fix_stack_high - fix_start) / 1024, fixed_count, nursery_not_fwd);
            fflush(stderr);
        }
    }

    // Post-GC verification: check pinned objects for stale nursery pointers
    if (g_gc_verbose && g_nursery.pinned_count > 0) {
        size_t stale_count = 0;
        offset = 0;
        while (offset + NURSERY_SIZE_PREFIX <= g_nursery.high_water) {
            uint64_t raw_prefix = *(uint64_t*)(base + offset);
            size_t obj_size = nursery_get_size(raw_prefix);
            if (obj_size == 0) { offset += 8; continue; }
            if (obj_size > NURSERY_MAX_OBJ_SIZE) break;

            // Check non-zero prefix means this is a live pinned object (pin bit was cleared in Phase 6)
            // We need to check the actual memory content since pin bits are cleared
            // Any object with non-zero prefix at this point is a surviving pinned object
            uintptr_t obj_start = (uintptr_t)(base + offset + NURSERY_SIZE_PREFIX);
            uintptr_t obj_end = obj_start + obj_size;

            for (uintptr_t p = obj_start; p + sizeof(void*) <= obj_end; p += sizeof(void*)) {
                void* val = *(void**)p;
                if (!val) continue;
                if ((uintptr_t)val < 4096 || (uintptr_t)val > 0x00007FFFFFFFFFFF) continue;
                if (is_nursery_ptr(val)) {
                    // This nursery object field still points into nursery.
                    // Check if the target memory was zeroed (stale reference).
                    uint64_t first_word = *(uint64_t*)val;
                    if (first_word == 0) {
                        fprintf(stderr, "[TsGC] STALE: pinned obj at nursery+%zu field@+%zu "
                                "-> nursery %p (ZEROED!)\n",
                                offset + NURSERY_SIZE_PREFIX,
                                (size_t)(p - obj_start), val);
                        stale_count++;
                    }
                }
            }

            offset += NURSERY_SIZE_PREFIX + obj_size;
        }
        if (stale_count > 0) {
            fprintf(stderr, "[TsGC] WARNING: %zu stale nursery references in pinned objects!\n", stale_count);
        }
        fflush(stderr);
    }

    if (g_gc_verbose) {
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        fprintf(stderr, "[TsGC] minor GC: promoted %zu objects (%zuKB), "
                "pinned %zu (%zuKB), %zu fragments, %.2fms\n",
                promoted_count, promoted_bytes / 1024,
                g_nursery.pinned_count, g_nursery.pinned_bytes / 1024,
                g_nursery.fragments.size(), ms);
        fflush(stderr);
    }

    // Sync exported globals so compiler inline path picks up new cursor/limit
    nursery_sync_to_exported();
}

extern "C" { // Resume extern "C" for public API

void ts_gc_minor_collect() {
    if (!g_heap || !g_nursery.enabled) return;
    nursery_sync_from_exported();
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    gc_minor_collect_internal();
    // gc_minor_collect_internal already calls nursery_sync_to_exported()
}

void ts_gc_write_barrier(void* slot_addr, void* stored_value) {
    // Fast reject: no card table, no nursery, or null value
    if (!g_card_table || !g_nursery.enabled || !stored_value) return;

    // Only dirty the card if stored_value points into the nursery
    if (!is_nursery_ptr(stored_value)) return;

    // Modular card index — always valid, no overflow possible
    size_t idx = card_index((uintptr_t)slot_addr);
    g_card_table[idx] = 1;
}

void ts_gc_verify_write_barrier(void* slot_addr, void* stored_value) {
    // No-op — modular card indexing guarantees all indices are valid.
}

// Dirty all cards spanning [start, start+size).
// Call after memcpy/memmove of pointer-containing data into old-gen.
void ts_gc_write_barrier_range(void* start, size_t size) {
    if (!g_card_table || !g_nursery.enabled || size == 0) return;

    uintptr_t lo = (uintptr_t)start;
    uintptr_t hi = lo + size;
    size_t idx_lo = card_index(lo);
    size_t idx_hi = card_index(hi - 1);

    // Modular wrap: dirty all cards from lo to hi
    if (idx_lo <= idx_hi) {
        for (size_t i = idx_lo; i <= idx_hi; i++) {
            g_card_table[i] = 1;
        }
    } else {
        // Wraps around card table boundary
        for (size_t i = idx_lo; i < CARD_TABLE_SIZE; i++) {
            g_card_table[i] = 1;
        }
        for (size_t i = 0; i <= idx_hi; i++) {
            g_card_table[i] = 1;
        }
    }
}

} // extern "C"
