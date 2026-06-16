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

// Forwarding verification: TS_GC_VERIFY_FORWARD=1 runs a post-forwarding scan
// at the end of each minor GC (before nursery reset). It looks for any pointer
// that STILL references a promoted (now-dead) nursery object — i.e. a holder
// slot that the fixup phases failed to update. It scans the GC heap + global
// roots (expected clean) AND re-runs the mark-only scanners (which can mark but
// not forward their own slots) to name the exact asymmetric registry holding a
// stale pointer. Debug-only.
static bool g_verify_forward = false;
static size_t g_verify_forward_leaks = 0;     // count surfaced by scanner re-run
static int g_verify_forward_cur_scanner = -1; // index of scanner being checked

// Deep verification extras (Part C full-process VirtualQuery scan + Part B
// scanner re-run). Heavy and can fault on guard pages, so they are OFF unless
// TS_GC_VERIFY_FORWARD=1 explicitly requests them. The on-demand verify
// (ts_gc_verify_now / __ts_gc_verify()) runs only the safe INV-1 scan
// (Part A old-gen+roots, Part A2 pinned survivors). (GC-001)
static bool g_verify_forward_deep = false;

// Unified verification knob (GC-001): TS_GC_VERIFY=N.
//   N>=1  run the INV-1 no-stale-pointer scan after every minor GC (report)
//   N>=2  ABORT on any INV-1 violation (holder+target magic), like Go gccheckmark
//   N>=3  also run the deep Part B/C scans
// The old TS_GC_VERIFY_FORWARD / TS_GC_VERIFY_CARDS flags remain as aliases.
static int  g_gc_verify_level = 0;
// When set, an INV-1 violation aborts the process (deterministic failure).
// Driven by TS_GC_VERIFY>=2; NOT set by the on-demand ts_gc_verify_now(),
// which instead returns the violation count for the TS program to assert on.
static bool g_verify_abort = false;

// Total INV-1 (no-stale-pointer) violations found by the most recent verified
// minor GC. Populated at the end of Phase 5v; read back by ts_gc_verify_now()
// so compiled TS (__ts_gc_verify()) can assert on it. (GC-001)
static size_t g_verify_total_violations = 0;

// TS_GC_PROMOTE_ALL=1: during minor GC, mark EVERY nursery object live, so all
// survive promotion and nothing is wiped. Diagnostic-only: if this makes a
// crash disappear, the crash is an UNDER-MARKING (missing-root) bug — a live
// object was deemed dead and wiped while still referenced.
static bool g_promote_all = false;

// GC stress mode: TS_GC_STRESS=N forces a full collection every Nth heap
// allocation (N=1 → every alloc). This shakes out missing-root / write-
// barrier bugs deterministically — anything reachable only via an
// un-rooted slot is collected the instant it's allocated, so the next use
// hits freed (rezeroed) memory immediately rather than intermittently.
// Implies nursery-disabled so EVERY object flows through the synchronized
// small/large trigger. Debug-only: extremely slow.
static uint64_t g_gc_stress = 0;        // 0 = off; else collect every Nth alloc
static uint64_t g_gc_stress_counter = 0;

// TS_PROTO_VERIFY=1: at GC entry/exit, walk every live map and assert its
// `prototype` field is null or a live TsMap. Catches the residual where a
// live map's prototype is clobbered to garbage (0x07 / a "STRG" header from a
// reused freed slot) — invisible to INV-1, which only flags stale nursery
// pointers. =2 also aborts on the first violation (with entry/exit labels, so
// we learn whether the corruption was introduced by THIS GC or by the mutator
// between GCs).
static bool g_proto_verify = false;
static bool g_proto_verify_abort = false;

// Minor GC nursery root callback: when non-null, gc_mark_ptr/ts_gc_mark_object
// will invoke this for nursery pointers (instead of ignoring them).
// Set during scanner callback invocation in gc_mark_nursery_live().
static void (*g_minor_gc_nursery_mark)(void* ptr) = nullptr;

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

// GC-001 Phase C: immortal-builtin tenuring. While this depth counter is
// non-zero, ts_gc_alloc routes allocations straight to the old generation
// instead of the moving nursery. Bracket runtime init and the lazy builtin
// getters with ts_gc_push_tenure()/ts_gc_pop_tenure() so the built-in graph
// (Object/Array/prototypes/etc., reached by the compiler via cached
// extern "C" TsValue* .data bindings) is born immortal and never moves —
// so those .data bindings and globalMap entries stay permanently valid
// across minor GC. A depth counter (not a bool) lets nested/lazy getters
// compose. User-function/object allocation runs with depth==0 (normal
// nursery semantics) — this does NOT tenure user code.
extern "C" int g_gc_tenure_depth = 0;
extern "C" void ts_gc_push_tenure() { g_gc_tenure_depth++; }
extern "C" void ts_gc_pop_tenure()  { if (g_gc_tenure_depth > 0) g_gc_tenure_depth--; }

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
static void gc_verify_prototypes(const char* when);

static void* gc_alloc_small(size_t size) {
    int class_idx = find_size_class(size);
    if (class_idx < 0) return nullptr;

    size_t alloc_size = SIZE_CLASSES[class_idx];

    // GC stress: force a full collect every Nth alloc (debug; see g_gc_stress).
    if (g_gc_stress && !g_in_collection && (++g_gc_stress_counter % g_gc_stress) == 0) {
        gc_collect_internal();
    }
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

    // GC stress: force a full collect every Nth alloc (debug; see g_gc_stress).
    if (g_gc_stress && !g_in_collection && (++g_gc_stress_counter % g_gc_stress) == 0) {
        gc_collect_internal();
    }
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

    // Nursery pointers: during full GC, scanned via gc_scan_nursery_roots().
    // During minor GC scanner callback invocation, redirect to nursery marker.
    if (g_nursery.enabled && is_nursery_ptr(ptr)) {
        if (g_minor_gc_nursery_mark) {
            g_minor_gc_nursery_mark(ptr);
        }
        return;  // Nursery objects not mark-swept in old-gen sense
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
// This is the standard approach used by conservative stack scanners.
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
// Actual signature: TsValue* ts_call_n(TsValue*, int64_t argc, TsValue** argv)
// We use void* since we don't include TsObject.h here. (The arity-suffixed
// ts_call_1 was removed; the array-form entry covers the 1-arg finalizer calls.)
extern "C" void* ts_call_n(void* func, long long argc, void** argv);
static inline void gc_call_1(void* func, void* arg1) {
    void* argv[1] = { arg1 };
    ts_call_n(func, 1, argv);
}

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

// ---- GC watch diagnostic: __ts_gc_watch(obj) registers a pointer; at each
// full GC (after marking, before sweep) we report whether it (and, for arrays,
// its elements buffer) is marked. Catches "live object freed" use-after-free. --
static void* g_gc_watch_obj = nullptr;
static void* g_gc_watch_elems = nullptr;
// Whether g_gc_watch_obj was marked (reachable) at the most recent full GC.
// Exposed to JS via __ts_gc_watch_alive() so a gc-suite test can assert that an
// object held only by a container survived a forced collection — a robust,
// reuse-immune signal (marking is decided during GC, not by post-GC slot reuse).
static bool g_gc_watch_last_marked = false;

// Return true iff ptr is the base of a currently-MARKED small/large GC object.
static bool gc_is_marked(void* ptr) {
    if (!ptr) return false;
    uintptr_t addr = (uintptr_t)ptr;
    rebuild_descriptors();
    auto& descs = g_heap->block_descriptors;
    if (!descs.empty()) {
        size_t lo = 0, hi = descs.size();
        while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (descs[mid].base <= addr) lo = mid + 1; else hi = mid; }
        if (lo > 0) {
            const BlockDescriptor& d = descs[lo - 1];
            if (addr >= d.base && addr < d.end) {
                size_t idx = (addr - d.base) / d.slot_size;
                if (idx < d.header->slot_count && bitmap_get(d.header->allocated_bits, idx))
                    return bitmap_get(d.header->mark_bits, idx);
                return false;
            }
        }
    }
    rebuild_large_descriptors();
    auto& ld = g_heap->large_descriptors;
    if (!ld.empty()) {
        size_t lo = 0, hi = ld.size();
        while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (ld[mid].base <= addr) lo = mid + 1; else hi = mid; }
        if (lo > 0) { const LargeObjDescriptor& d = ld[lo - 1]; if (addr >= d.base && addr < d.end) return d.header->marked; }
    }
    return false;
}

static void gc_watch_check_after_mark() {
    if (!g_gc_watch_obj) return;
    void* raw = g_gc_watch_obj;
    bool om = gc_is_marked(raw);
    g_gc_watch_last_marked = om;
    uint32_t magic = ((uintptr_t)raw > 4096) ? *(uint32_t*)raw : 0;
    void* curElems = (magic == 0x41525259) ? *(void**)((char*)raw + 8) : nullptr;
    bool em = curElems ? gc_is_marked(curElems) : true;
    static const bool verbose = getenv("TS_GC_WATCH_VERBOSE") != nullptr;
    if (om && em && !verbose) return;  // default: only report a problem
    void *e0=nullptr,*e1=nullptr,*e2=nullptr;
    size_t len = 0;
    if (magic == 0x41525259) {
        len = *(size_t*)((char*)raw + 16);
        if (curElems) { e0=((void**)curElems)[0]; e1=((void**)curElems)[1]; e2=((void**)curElems)[2]; }
    }
    fprintf(stderr, "[WATCH] coll#%zu obj=%p om=%d magic=0x%08X len=%zu elems=%p(orig=%p chg=%d) em=%d  e[0..2]=%p %p %p%s\n",
            g_heap->collection_count, raw, om, magic, len, curElems, g_gc_watch_elems,
            (curElems != g_gc_watch_elems), em, e0, e1, e2,
            (!om ? "  <-- OBJECT SWEPT" : !em ? "  <-- ELEMS SWEPT" : ""));
    fflush(stderr);
}

extern "C" void* ts_value_get_object(void* val);

// __ts_dbg_bits(v): dump a value's raw 64-bit encoding, its NaN-box tag class,
// and (if a heap pointer) the magic words at offset 0 and 16. For pinning
// bit-level corruption of a value (e.g. a builtin function losing its tag).
void ts_dbg_bits(void* boxed) {
    uint64_t raw = (uint64_t)(uintptr_t)boxed;
    const char* cls = "ptr?";
    bool isPtr = (raw & 0xFFFF000000000000ULL) == 0 && raw > 0x0A;
    if ((raw & 0xFFFE000000000000ULL) == 0xFFFE000000000000ULL) cls = "int32";
    else if (raw & 0xFFFE000000000000ULL) cls = "double";
    else if (raw == 0x0A) cls = "undefined";
    else if (raw == 0x02) cls = "null";
    else if (raw == 0x06 || raw == 0x07) cls = "bool";
    else if (isPtr) cls = "ptr";
    uint32_t m0 = 0, m16 = 0;
    if (isPtr) { m0 = *(uint32_t*)(uintptr_t)raw; m16 = *(uint32_t*)((char*)(uintptr_t)raw + 16); }
    fprintf(stderr, "[BITS] raw=0x%016llX class=%s magic@0=0x%08X magic@16=0x%08X\n",
            (unsigned long long)raw, cls, m0, m16);
    fflush(stderr);
}

void ts_gc_dbg_watch(void* boxed) {
    void* raw = ts_value_get_object(boxed);
    if (!raw) raw = boxed;
    g_gc_watch_obj = raw;
    g_gc_watch_elems = nullptr;
    // If it's a TsArray (magic 'ARRY' = 0x41525259 at offset 0), grab elements ptr (offset 8).
    if (raw && (uintptr_t)raw > 4096) {
        uint32_t magic = *(uint32_t*)raw;
        if (magic == 0x41525259) {
            g_gc_watch_elems = *(void**)((char*)raw + 8);
        }
    }
    void *e0=nullptr,*e1=nullptr,*e2=nullptr; size_t len=0;
    if (g_gc_watch_elems) {
        len = *(size_t*)((char*)raw + 16);
        e0=((void**)g_gc_watch_elems)[0]; e1=((void**)g_gc_watch_elems)[1]; e2=((void**)g_gc_watch_elems)[2];
    }
    fprintf(stderr, "[WATCH] registered obj=%p elems=%p magic=0x%08X len=%zu e[0..2]=%p %p %p\n",
            g_gc_watch_obj, g_gc_watch_elems, raw ? *(uint32_t*)raw : 0, len, e0, e1, e2);
    fflush(stderr);
    g_gc_watch_last_marked = true;  // assume live until a GC says otherwise
}

// __ts_gc_watch_alive(): true iff the watched object was MARKED (reachable) at
// the most recent full GC. After dropping all JS refs + __ts_gc_major(), this
// returns false iff the holding container failed to keep it alive.
bool ts_gc_dbg_watch_alive() {
    return g_gc_watch_last_marked;
}

static void gc_collect_internal() {
    if (!g_heap) return;

    gc_verify_prototypes("full-entry");
    g_heap->collection_count++;
    g_in_collection = true;

    auto t0 = std::chrono::high_resolution_clock::now();
    gc_mark_phase();
    gc_watch_check_after_mark();
    auto t1 = std::chrono::high_resolution_clock::now();
    gc_process_weak_refs();
    gc_process_finalizers();
    gc_sweep_phase();
    auto t2 = std::chrono::high_resolution_clock::now();

    g_in_collection = false;
    gc_verify_prototypes("full-exit");

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
    if (getenv("TS_GC_VERIFY_FORWARD")) {
        g_verify_forward = true;
        g_verify_forward_deep = true;  // env explicitly wants the heavy Part B/C scans
    }
    // Unified verify knob (GC-001). TS_GC_VERIFY=N, N>=1.
    if (const char* vlvl = getenv("TS_GC_VERIFY")) {
        int n = atoi(vlvl);
        if (n < 1) n = 1;  // presence implies at least level 1
        g_gc_verify_level = n;
        if (n >= 1) g_verify_forward = true;        // INV-1 scan after each minor GC
        if (n >= 2) g_verify_abort = true;          // abort on violation
        if (n >= 3) g_verify_forward_deep = true;   // heavy Part B/C scans
    }
    if (getenv("TS_GC_PROMOTE_ALL")) {
        g_promote_all = true;
    }
    if (const char* pv = getenv("TS_PROTO_VERIFY")) {
        g_proto_verify = true;
        if (atoi(pv) >= 2) g_proto_verify_abort = true;
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

    // GC stress mode: collect every Nth allocation to flush out rooting /
    // write-barrier bugs. Forces nursery off so every object goes through the
    // synchronized small/large allocation trigger.
    const char* stress_env = getenv("TS_GC_STRESS");
    if (stress_env) {
        long n = atol(stress_env);
        if (n > 0) {
            g_gc_stress = (uint64_t)n;
            fprintf(stderr, "[TsGC] STRESS mode: full collect every %llu alloc(s); nursery disabled\n",
                    (unsigned long long)g_gc_stress);
            fflush(stderr);
        }
    }

    // Initialize nursery (bump-pointer allocator for short-lived objects)
    bool nursery_disabled = (g_gc_stress > 0);
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
        gc_call_1(cb.callback, cb.arg);
    }
}

// GC-001 0.4: drain queued FinalizationRegistry cleanup callbacks. The collector
// only queues them (it can't run JS during a GC); the event loop calls this each
// iteration so the callbacks actually fire after their target was collected.
extern "C" void ts_gc_run_finalizer_callbacks() {
    gc_run_pending_callbacks();
}

void* ts_gc_alloc(size_t size) {
    if (!g_heap) gc_init();
    if (size == 0) size = 8; // Minimum allocation

    // ---- Nursery fast path: no mutex, bump pointer in fragment ----
    // Skipped while tenuring (g_gc_tenure_depth>0) so immortal builtins go
    // straight to old-gen and never move — see g_gc_tenure_depth above.
    if (g_nursery.enabled && g_gc_tenure_depth == 0 && size <= NURSERY_MAX_OBJ_SIZE) {
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
        gc_call_1(cb.callback, cb.arg);
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
        gc_call_1(cb.callback, cb.arg);
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
        gc_call_1(cb.callback, cb.arg);
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

// True iff `ptr` points into a CURRENTLY-ALLOCATED GC object (nursery live
// object, old-gen small slot, or large object). Returns false for freed /
// decommitted slots, tagged primitives, and non-heap addresses. Cheap in
// steady state (rebuild_descriptors is dirty-flag-guarded → two binary
// searches). Use to make defensive deref paths crash-safe: validate a pointer
// before reading its header when it may be stale (e.g. a closure cell array
// whose backing block was freed and reused). gc_find_base returns the
// containing object's base, checking the slot's allocated bit.
bool ts_gc_is_heap_object(void* ptr) {
    if (!g_heap || !ptr) return false;
    uintptr_t v = (uintptr_t)ptr;
    if (v < 0x10000 || (v >> 48) != 0) return false;  // tagged / non-canonical
    return gc_find_base(ptr) != nullptr;
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

    // Diagnostic: TS_GC_PROMOTE_ALL marks EVERY nursery object live.
    if (g_promote_all) {
        for (size_t i = 0; i < nursery_objects.size(); i++) {
            uint64_t* prefix = (uint64_t*)nursery_objects[i].prefix_ptr;
            if (!nursery_is_marked(*prefix)) {
                nursery_set_marked(prefix);
                marked_count++;
            }
        }
        if (g_gc_verbose) {
            fprintf(stderr, "[TsGC] PROMOTE_ALL: marked all %zu nursery objects live\n",
                    nursery_objects.size());
            fflush(stderr);
        }
        return;
    }

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

    // Root source 3: Old-gen → nursery pointers (FULL old-gen block scan).
    //
    // This MUST be a full scan, NOT card-table-guided, and it MUST be
    // symmetric with the Phase 3 fixup scan below (which is already full).
    // Rationale: container types (TsMap/TsHashTable bucket arrays, TsArray
    // elements buffers, and tenured headers) store nursery pointers into
    // old-gen slots without a reliable write barrier on every mutation site,
    // so the card table is an UNDER-approximation of old-gen→nursery edges.
    // If mark were card-guided while fixup is full, a non-barriered edge is:
    //   - missed by mark  → the nursery target is treated dead → wiped, and
    //   - found by fixup  → not in the forwarding table → assumed "pinned" →
    //     the old-gen slot is left pointing at now-dead nursery memory.
    // That asymmetry is exactly the moving-GC corruption (VERIFY-FWD reports
    // "old-gen slot -> DEAD nursery obj marked=0"). Making mark a full scan
    // — the same coverage the fixup already pays for — closes it. The cost is
    // one extra full old-gen walk per minor GC (same order as the existing
    // fixup walk); correctness first until per-container write barriers are
    // complete enough to trust the card table as an exact remembered set.
    {
        for (size_t sc = 0; sc < NUM_SIZE_CLASSES; sc++) {
            for (BlockHeader* bh = g_heap->block_lists[sc]; bh; bh = bh->next) {
                if (!bh->block_mem || bh->live_count == 0) continue;
                uintptr_t bstart = (uintptr_t)bh->block_mem;
                for (size_t slot = 0; slot < bh->slot_count; slot++) {
                    if (!(bh->allocated_bits[slot / 8] & (1 << (slot % 8)))) continue;
                    uintptr_t s = bstart + slot * bh->slot_size;
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

    // Root source 6: Custom scanner callbacks (module cache, native props, etc.)
    // These call ts_gc_mark_object → gc_mark_ptr, which normally ignores nursery
    // pointers. We temporarily install a redirect so nursery pointers found by
    // scanners are marked as live roots. After the callbacks, we scan for any
    // newly-marked objects and add them to the BFS worklist.
    if (!g_heap->scanners.empty()) {
        // Snapshot which objects are already marked
        std::vector<bool> was_marked(nursery_objects.size());
        for (size_t i = 0; i < nursery_objects.size(); i++) {
            was_marked[i] = nursery_is_marked(*(uint64_t*)nursery_objects[i].prefix_ptr);
        }

        // Install redirect: gc_mark_ptr will call this for nursery pointers
        g_minor_gc_nursery_mark = [](void* ptr) {
            if (!is_nursery_ptr(ptr)) return;
            void* base = nursery_find_base(ptr);
            if (!base) return;
            uint64_t* prefix = (uint64_t*)((char*)base - NURSERY_SIZE_PREFIX);
            if (!nursery_is_marked(*prefix)) {
                nursery_set_marked(prefix);
            }
        };

        for (auto& entry : g_heap->scanners) {
            entry.callback(entry.context);
        }

        // GC-001 step 3b: consume PRECISE stack-map roots in the MINOR GC.
        // With --gc-statepoints, RS4GC records the exact stack-slot/register
        // locations of live GC pointers at each call safepoint. The full GC
        // already calls this; doing it here (while g_minor_gc_nursery_mark routes
        // marks to the nursery) marks nursery objects referenced from those
        // precise roots so they are promoted rather than wiped — a precise
        // complement to the conservative Phase-0 pin. No-op if no statepoints
        // (g_has_statepoints false). The promoted objects' stack slots are then
        // forwarded by Phase 7. ts_gc_mark_object routes through gc_mark_ptr,
        // which honors the nursery-mark hook for nursery pointers.
        ts_gc_push_precise_stack_roots();

        g_minor_gc_nursery_mark = nullptr;

        // Add newly-marked objects to worklist for BFS tracing
        for (size_t i = 0; i < nursery_objects.size(); i++) {
            if (!was_marked[i] && nursery_is_marked(*(uint64_t*)nursery_objects[i].prefix_ptr)) {
                worklist.push_back(i);
                marked_count++;
            }
        }
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

// TS_GC_VERIFY_FORWARD redirect: installed into g_minor_gc_nursery_mark while
// the mark-only scanners are re-run AFTER forwarding. Each nursery pointer a
// scanner surfaces is checked against the (still-live) forwarding table; if it
// maps to a promoted object, the scanner is holding the stale nursery address
// and never forwarded it — a leak. Names the offending scanner + object magic.
static void gc_verify_forward_scanner_redirect(void* ptr) {
    if (!is_nursery_ptr(ptr)) return;
    void* fwd = ts_gc_minor_lookup_forward(ptr);
    if (fwd == ptr) return;  // pinned (legitimately still nursery) — not a leak
    g_verify_forward_leaks++;
    if (g_verify_forward_leaks <= 40) {
        uint32_t magic = 0;
        // Promoted copy lives at fwd; object magic sits at byte offset 8.
        if (fwd) magic = *(uint32_t*)((char*)fwd + 8);
        const char* tag = "?";
        switch (magic) {
            case 0x434C5352: tag = "CLSR(closure)"; break;
            case 0x43454C4C: tag = "CELL"; break;
            default: break;
        }
        void* cb = (g_verify_forward_cur_scanner >= 0 &&
                    (size_t)g_verify_forward_cur_scanner < g_heap->scanners.size())
                       ? (void*)g_heap->scanners[g_verify_forward_cur_scanner].callback
                       : nullptr;
        fprintf(stderr, "[TsGC] VERIFY-FWD: scanner #%d (cb=%p) holds STALE nursery ptr %p "
                        "(promoted->%p magic=0x%08X %s)\n",
                g_verify_forward_cur_scanner, cb, ptr, fwd, magic, tag);
        fflush(stderr);
    }
}

// Prototype-chain invariant verifier (TS_PROTO_VERIFY). Walks every live GC
// object; for each TsMap (magic@0x10 == "MAPS") whose `prototype` field (@0x28)
// is non-null, asserts the prototype is a live heap object that is itself a
// map. A violation = a live map's prototype was clobbered to garbage. `when`
// labels the call site (e.g. "minor-entry"/"minor-exit") so we can tell whether
// a given GC introduced the corruption or the mutator did between GCs.
static void gc_verify_prototypes(const char* when) {
    if (!g_proto_verify || !g_heap) return;
    const uint32_t MAPS = 0x4D415053;  // "MAPS" (TsObject::magic @ +0x10)
    size_t bad = 0;
    auto check_map = [&](void* obj) {
        if (!obj) return;
        if (*(uint32_t*)((char*)obj + 0x10) != MAPS) return;   // not a TsMap
        void* proto = *(void**)((char*)obj + 0x28);            // TsMap::prototype
        if (!proto) return;                                    // null is fine
        void* pbase = gc_find_base(proto);                     // live heap obj?
        bool live = (pbase == proto);
        bool isMap = live && (*(uint32_t*)((char*)proto + 0x10) == MAPS);
        if (!isMap && ++bad <= 30) {
            const char* loc = "old-gen-or-other";
            uintptr_t nlo = (uintptr_t)g_nursery.region;
            uintptr_t nhw = nlo + g_nursery.high_water;
            uintptr_t nhi = nlo + g_nursery.region_size;
            if ((uintptr_t)proto >= nlo && (uintptr_t)proto < nhi) {
                loc = ((uintptr_t)proto >= nhw) ? "nursery-beyond-highwater"
                    : (pbase ? "nursery-mid-object" : "nursery-gap-or-dead");
            }
            // Bytes currently at proto (readable if in committed nursery/heap).
            uint64_t w0 = 0, w1 = 0, w2 = 0;
            if ((uintptr_t)proto >= nlo && (uintptr_t)proto + 24 <= nhi) {
                w0 = *(uint64_t*)proto; w1 = *(uint64_t*)((char*)proto + 8);
                w2 = *(uint64_t*)((char*)proto + 16);
            }
            bool objNursery = ((uintptr_t)obj >= nlo && (uintptr_t)obj < nhi);
            fprintf(stderr, "[PROTO] %s: map %p(%s) prototype=%p %s nbase=%p "
                    "bytes=[%016llx %016llx %016llx]\n",
                    when, obj, objNursery ? "nursery" : "oldgen", proto, loc, pbase,
                    (unsigned long long)w0, (unsigned long long)w1, (unsigned long long)w2);
            fflush(stderr);
        }
    };
    // Old-gen small slots
    for (size_t sc = 0; sc < NUM_SIZE_CLASSES; sc++) {
        for (BlockHeader* bh = g_heap->block_lists[sc]; bh; bh = bh->next) {
            if (!bh->block_mem || bh->live_count == 0) continue;
            uintptr_t bstart = (uintptr_t)bh->block_mem;
            for (size_t slot = 0; slot < bh->slot_count; slot++) {
                if (!(bh->allocated_bits[slot / 8] & (1 << (slot % 8)))) continue;
                check_map((void*)(bstart + slot * bh->slot_size));
            }
        }
    }
    // Large objects
    for (LargeObjHeader* lo = g_heap->large_sentinel.next;
         lo != &g_heap->large_sentinel; lo = lo->next) {
        if (lo->data_size == 0 || lo->data_size > (size_t)2 * 1024 * 1024 * 1024) continue;
        check_map((void*)((char*)lo + sizeof(LargeObjHeader)));
    }
    // Nursery live objects
    if (g_nursery.enabled && g_nursery.region) {
        char* nb = (char*)g_nursery.region;
        size_t off = 0;
        while (off + NURSERY_SIZE_PREFIX <= g_nursery.high_water) {
            uint64_t raw = *(uint64_t*)(nb + off);
            size_t osz = nursery_get_size(raw);
            if (osz == 0) { off += 8; continue; }
            if (osz > NURSERY_MAX_OBJ_SIZE) break;
            check_map((void*)(nb + off + NURSERY_SIZE_PREFIX));
            off += NURSERY_SIZE_PREFIX + osz;
        }
    }
    if (bad) {
        fprintf(stderr, "[PROTO] %s: %zu map(s) with corrupt prototype\n", when, bad);
        fflush(stderr);
        if (g_proto_verify_abort) abort();
    }
}

static void gc_minor_collect_internal() {
    if (!g_nursery.enabled || g_nursery.high_water == 0) return;

    gc_verify_prototypes("minor-entry");
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

    // Phase 0: Pin nursery objects referenced from the stack
    gc_pin_nursery_stack_roots();

    // Phase 0b: Mark live nursery objects (root discovery + BFS tracing)
    gc_mark_nursery_live();

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

        // Full old-gen scan: scan ALL allocated slots for nursery pointers.
        // This is necessary because TsMap/TsHashTable stores values in
        // ts_gc_alloc_old_gen buckets without write barriers, so the card
        // table doesn't track all old-gen → nursery references.
        // (V8's semi-space approach avoids this by using remembered sets,
        // but for correctness we do a full scan until write barriers are
        // added to all container types.)
        for (size_t sc = 0; sc < NUM_SIZE_CLASSES; sc++) {
            for (BlockHeader* bh = g_heap->block_lists[sc]; bh; bh = bh->next) {
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
        // Large objects
        for (LargeObjHeader* lo = g_heap->large_sentinel.next;
             lo != &g_heap->large_sentinel; lo = lo->next) {
            if (lo->data_size == 0 || lo->data_size > (size_t)2 * 1024 * 1024 * 1024) continue;
            uintptr_t s = (uintptr_t)lo + sizeof(LargeObjHeader);
            uintptr_t e = s + lo->data_size;
            for (uintptr_t p = s; p + sizeof(void*) <= e; p += sizeof(void*)) {
                fixup_word(p);
            }
        }

        // Clear card table, then re-dirty for pinned references
        if (g_card_table) {
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

    // Phase 5v: Forwarding verification (TS_GC_VERIFY_FORWARD=1).
    // Runs AFTER all fixup phases but BEFORE the nursery reset, while the
    // forwarding table is still valid and the promoted objects' old nursery
    // copies still exist. Finds any pointer that still references a promoted
    // (soon-to-be-dead) nursery object — i.e. a holder slot the fixups missed.
    if (g_verify_forward && !forwarding.empty()) {
        // Part A: GC heap + global roots. Two failure categories:
        //   (1) STALE: candidate was promoted but this slot wasn't forwarded.
        //       Phase 3 full-scans old-gen + Phase 4 forwards roots → expect 0.
        //   (2) DANGLING: candidate points to a nursery object that is neither
        //       forwarded (promoted) nor pinned → it was deemed DEAD and will be
        //       wiped at Phase 6, yet this slot still references it. This is the
        //       use-after-free: a live old-gen → nursery edge the MARK phase
        //       missed (so the target was never promoted).
        size_t heap_leaks = 0;
        size_t dangling = 0;
        auto report_word = [&](uintptr_t p, const char* region) {
            void* candidate = *(void**)p;
            if (!candidate) return;
            if ((uintptr_t)candidate < 4096) return;
            if ((uintptr_t)candidate > 0x00007FFFFFFFFFFF) return;
            if (!is_nursery_ptr(candidate)) return;
            void* fwd = lookup_forward(candidate);
            if (fwd != candidate) {
                heap_leaks++;
                if (heap_leaks <= 40) {
                    uint32_t magic = fwd ? *(uint32_t*)((char*)fwd + 8) : 0;
                    fprintf(stderr, "[TsGC] VERIFY-FWD: %s slot %p holds STALE nursery ptr %p "
                                    "(promoted->%p magic=0x%08X)\n",
                            region, (void*)p, candidate, fwd, magic);
                    fflush(stderr);
                }
                return;
            }
            // Not forwarded — pinned (OK) or dead-but-referenced (DANGLING)?
            void* nbase = nursery_find_base(candidate);
            if (!nbase) return;
            uint64_t prefix = *(uint64_t*)((char*)nbase - NURSERY_SIZE_PREFIX);
            if (nursery_is_pinned(prefix)) return;  // legitimately stays in nursery
            // Dead (unmarked / unpinned) but still referenced → the bug.
            dangling++;
            if (dangling <= 40) {
                uint32_t tgt_magic = *(uint32_t*)((char*)candidate + 8);
                // Describe the HOLDER object so we can name the registry/struct.
                uint32_t holder_magic = 0;
                void* holder = gc_find_base((void*)p);
                if (holder) holder_magic = *(uint32_t*)((char*)holder + 8);
                fprintf(stderr, "[TsGC] VERIFY-FWD: %s slot %p (holder=%p magic=0x%08X) "
                                "-> DEAD nursery obj %p magic=0x%08X marked=%d\n",
                        region, (void*)p, holder, holder_magic,
                        candidate, tgt_magic, (int)nursery_is_marked(prefix));
                fflush(stderr);
            }
        };
        for (size_t sc = 0; sc < NUM_SIZE_CLASSES; sc++) {
            for (BlockHeader* bh = g_heap->block_lists[sc]; bh; bh = bh->next) {
                if (!bh->block_mem || bh->live_count == 0) continue;
                uintptr_t bstart = (uintptr_t)bh->block_mem;
                for (size_t slot = 0; slot < bh->slot_count; slot++) {
                    if (!(bh->allocated_bits[slot / 8] & (1 << (slot % 8)))) continue;
                    uintptr_t s = bstart + slot * bh->slot_size;
                    uintptr_t e = s + bh->slot_size;
                    for (uintptr_t p = s; p + sizeof(void*) <= e; p += sizeof(void*)) {
                        report_word(p, "old-gen");
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
                report_word(p, "large-obj");
            }
        }
        for (void** root : g_heap->global_roots) {
            if (root) report_word((uintptr_t)root, "global-root");
        }

        // Part A2: scan LIVE nursery survivors (pinned objects that stay in the
        // nursery) for pointers to DEAD nursery objects. Part A only covers
        // old-gen + roots; a pinned survivor holding a dead-object pointer is a
        // BFS/marking gap (holder marked, referent not traced) and is invisible
        // to the old-gen scan. Promoted survivors live in old-gen (covered by A).
        {
            char* nbase = (char*)g_nursery.region;
            size_t noff = 0;
            while (noff + NURSERY_SIZE_PREFIX <= g_nursery.high_water) {
                uint64_t raw = *(uint64_t*)(nbase + noff);
                size_t osz = nursery_get_size(raw);
                if (osz == 0) { noff += 8; continue; }
                if (osz > NURSERY_MAX_OBJ_SIZE) break;
                if (nursery_is_pinned(raw)) {
                    uintptr_t s = (uintptr_t)(nbase + noff + NURSERY_SIZE_PREFIX);
                    for (uintptr_t p = s; p + sizeof(void*) <= s + osz; p += sizeof(void*)) {
                        report_word(p, "nursery-pinned");
                    }
                }
                noff += NURSERY_SIZE_PREFIX + osz;
            }
            if (g_verify_forward_deep || g_gc_verbose) {
                fprintf(stderr, "[TsGC] VERIFY-FWD: nursery region [%p..%p) high_water=%zu\n",
                        g_nursery.region, (char*)g_nursery.region + g_nursery.high_water,
                        g_nursery.high_water);
                fflush(stderr);
            }
        }

        // Part C: brute-force scan of the ENTIRE committed process address space
        // for any pointer to a PROMOTED nursery object. This covers off-GC-heap
        // holders the targeted scans miss — data-segment globals, malloc'd C++
        // structures, etc. Heavy, and can fault on PAGE_GUARD pages, so it only
        // runs in deep mode (TS_GC_VERIFY_FORWARD=1), never the on-demand verify.
#ifdef _WIN32
        if (g_verify_forward_deep) {
            size_t proc_stale = 0;
            uintptr_t nlo = (uintptr_t)g_nursery.region;
            uintptr_t nhi = nlo + g_nursery.region_size;
            // Identify the CURRENT thread stack so we can skip it: Phase 0 already
            // pins from it, and at this point it also contains the GC's own live
            // frames (loop temporaries holding nursery pointers) which would be
            // false positives. A real missed stack root would be pinned already,
            // or live on ANOTHER thread's stack (still scanned below).
            volatile int stack_marker = 0;
            uintptr_t cur_stack_addr = (uintptr_t)&stack_marker;
            uintptr_t addr = 0x10000;
            MEMORY_BASIC_INFORMATION mbi;
            while (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
                uintptr_t region_base = (uintptr_t)mbi.BaseAddress;
                uintptr_t region_end = region_base + mbi.RegionSize;
                bool writable = (mbi.State == MEM_COMMIT) &&
                    (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                    PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
                // Skip the nursery region itself (its stale internal ptrs are not holders).
                bool is_nursery_region = (region_base >= nlo && region_base < nhi);
                // Skip the current thread's stack region (Phase 0 owns it; GC frames
                // here are false positives).
                bool is_cur_stack = (cur_stack_addr >= region_base && cur_stack_addr < region_end);
                if (writable && !is_nursery_region && !is_cur_stack) {
                    for (uintptr_t p = region_base; p + sizeof(void*) <= region_end; p += sizeof(void*)) {
                        void* candidate = *(void**)p;
                        if ((uintptr_t)candidate < 4096) continue;
                        if ((uintptr_t)candidate > 0x00007FFFFFFFFFFF) continue;
                        if (!is_nursery_ptr(candidate)) continue;
                        void* fwd = lookup_forward(candidate);
                        if (fwd == candidate) continue;  // pinned
                        proc_stale++;
                        if (proc_stale <= 40) {
                            uint32_t magic = fwd ? *(uint32_t*)((char*)fwd + 8) : 0;
                            void* gcbase = gc_find_base((void*)p);
                            fprintf(stderr, "[TsGC] VERIFY-FWD PROC: slot %p (region %p-%p prot=0x%X gc=%s) "
                                            "-> nursery %p promoted->%p magic=0x%08X\n",
                                    (void*)p, (void*)region_base, (void*)region_end, mbi.Protect,
                                    gcbase ? "yes" : "NO", candidate, fwd, magic);
                            fflush(stderr);
                        }
                    }
                }
                addr = region_end;
                if (addr < region_base) break;  // overflow guard
            }
            if (proc_stale) {
                fprintf(stderr, "[TsGC] VERIFY-FWD PROC: %zu stale nursery ptrs across process memory\n", proc_stale);
                fflush(stderr);
            }
        }
#endif

        // Part B: re-run the mark-only scanners. They surface pointer VALUES
        // (no slot address), so they can mark/promote but never forward their
        // own registry slot — the suspected asymmetry. Any surfaced pointer
        // that maps to a promoted object is a stale holder; this names which
        // scanner (and object kind) owns it.
        size_t scanner_leaks = 0;
        if (g_verify_forward_deep && !g_heap->scanners.empty()) {
            g_current_forwarding = &forwarding;
            g_verify_forward_leaks = 0;
            g_minor_gc_nursery_mark = gc_verify_forward_scanner_redirect;
            for (size_t i = 0; i < g_heap->scanners.size(); i++) {
                g_verify_forward_cur_scanner = (int)i;
                g_heap->scanners[i].callback(g_heap->scanners[i].context);
            }
            g_minor_gc_nursery_mark = nullptr;
            g_verify_forward_cur_scanner = -1;
            g_current_forwarding = nullptr;
            scanner_leaks = g_verify_forward_leaks;
        }

        // Record for ts_gc_verify_now() (__ts_gc_verify() in compiled TS).
        g_verify_total_violations = heap_leaks + dangling + scanner_leaks;

        if (heap_leaks || scanner_leaks || dangling) {
            fprintf(stderr, "[TsGC] VERIFY-FWD SUMMARY: %zu GC-heap/root stale, "
                            "%zu dangling (dead-but-referenced), "
                            "%zu mark-only-scanner stale (of %zu forwarded)\n",
                    heap_leaks, dangling, scanner_leaks, forwarding.size());
            fflush(stderr);
            // INV-1 assert mode (TS_GC_VERIFY>=2): a stale/dangling holder after
            // minor GC is a moving-GC correctness bug. Fail loudly and immediately
            // (like Go's gccheckmark) so the harness catches it deterministically.
            // The on-demand ts_gc_verify_now() leaves g_verify_abort off and
            // returns the count instead.
            if (g_verify_abort) {
                fprintf(stderr, "[TsGC] INV-1 FAILED: %zu stale/dangling holder(s) "
                                "survive minor GC — aborting (TS_GC_VERIFY>=2)\n",
                        g_verify_total_violations);
                fflush(stderr);
                abort();
            }
        } else if (g_gc_verbose) {
            fprintf(stderr, "[TsGC] VERIFY-FWD: clean (%zu forwarded, no stale/dangling holders)\n",
                    forwarding.size());
            fflush(stderr);
        }
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

    gc_verify_prototypes("minor-exit");
}

extern "C" { // Resume extern "C" for public API

void ts_gc_minor_collect() {
    if (!g_heap || !g_nursery.enabled) return;
    nursery_sync_from_exported();
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    gc_minor_collect_internal();
    // gc_minor_collect_internal already calls nursery_sync_to_exported()
}

// ---------------------------------------------------------------------------
// GC verification-harness entry points (GC-001).
// These back the __ts_gc_* builtins so compiled TS can drive and inspect the
// collector. ts_value_get_object unboxes a NaN-boxed/heap-boxed value.
// ---------------------------------------------------------------------------
extern "C" void* ts_value_get_object(void* val);

double ts_gc_dbg_collection_count() {
    return (double)ts_gc_collection_count();
}

double ts_gc_dbg_live_size() {
    return (double)ts_gc_live_size();
}

// __ts_gc_is_nursery(obj): is the value's backing object currently in the
// nursery? Returns false for non-pointer values.
bool ts_gc_dbg_is_nursery(void* boxed) {
    if (!boxed) return false;
    void* raw = ts_value_get_object(boxed);
    if (!raw) raw = boxed;  // already a raw pointer
    return ts_gc_is_nursery(raw);
}

// __ts_gc_verify(): run a verified minor GC and return the number of INV-1
// (no-stale-pointer-after-minor-GC) violations detected. 0 == clean.
double ts_gc_verify_now() {
    if (!g_heap || !g_nursery.enabled) return 0.0;
    nursery_sync_from_exported();
    std::lock_guard<std::mutex> lock(g_heap->gc_mutex);
    bool savedFwd = g_verify_forward;
    bool savedAbort = g_verify_abort;
    g_verify_forward = true;
    g_verify_abort = false;  // verify_now reports a count; it never aborts
    g_verify_total_violations = 0;
    gc_minor_collect_internal();
    g_verify_forward = savedFwd;
    g_verify_abort = savedAbort;
    return (double)g_verify_total_violations;
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
