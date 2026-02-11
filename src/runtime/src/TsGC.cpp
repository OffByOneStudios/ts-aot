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
#endif

// ============================================================================
// Configuration
// ============================================================================

static const size_t BLOCK_SIZE = 256 * 1024;  // 256KB per block
static const size_t MAX_SMALL_SIZE = 512;

// Size classes: 8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512
static const size_t SIZE_CLASSES[] = { 8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512 };
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

// ============================================================================
// Platform Memory
// ============================================================================

static void* platform_alloc_block() {
#ifdef _WIN32
    return VirtualAlloc(nullptr, BLOCK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void* p = mmap(nullptr, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
#endif
}

static void platform_free_block(void* ptr) {
#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, BLOCK_SIZE);
#endif
}

static void* platform_alloc_large(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
#endif
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

// Forward declaration
static void gc_collect_internal();

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

    g_heap->total_allocated += size;
    if (g_heap->total_allocated > g_heap->peak_allocated) {
        g_heap->peak_allocated = g_heap->total_allocated;
    }

    // Data follows header, VirtualAlloc returns zeroed memory
    return (char*)mem + sizeof(LargeObjHeader);
}

// ============================================================================
// Pointer Validation (ts_gc_base replacement for GC_base)
// ============================================================================

// Find the base address of the GC object containing ptr, or nullptr.
static void* gc_find_base(void* ptr) {
    if (!ptr || !g_heap) return nullptr;

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

    // Check large objects
    for (LargeObjHeader* h = g_heap->large_sentinel.next;
         h != &g_heap->large_sentinel; h = h->next) {
        uintptr_t data_start = (uintptr_t)h + sizeof(LargeObjHeader);
        uintptr_t data_end = data_start + h->data_size;
        if (addr >= data_start && addr < data_end) {
            return (void*)data_start;
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

    // Check large objects
    for (LargeObjHeader* h = g_heap->large_sentinel.next;
         h != &g_heap->large_sentinel; h = h->next) {
        uintptr_t data_start = (uintptr_t)h + sizeof(LargeObjHeader);
        uintptr_t data_end = data_start + h->data_size;
        if (addr >= data_start && addr < data_end) {
            if (!h->marked) {
                h->marked = true;
                g_heap->mark_worklist.push_back((void*)data_start);
            }
            return;
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

    // Check large objects
    for (LargeObjHeader* h = g_heap->large_sentinel.next;
         h != &g_heap->large_sentinel; h = h->next) {
        uintptr_t data_start = (uintptr_t)h + sizeof(LargeObjHeader);
        if (addr == data_start) {
            return h->data_size;
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
static void gc_push_conservative_stack_roots() {
    // Get approximate RSP
    void* rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));

    // Get stack base from /proc/self/maps or pthread
    // For now, scan 1MB below stack base (conservative estimate)
    uintptr_t scan_start = ((uintptr_t)rsp + 7) & ~(uintptr_t)7;
    uintptr_t scan_end = scan_start + 1024 * 1024;

    for (uintptr_t p = scan_start; p + sizeof(void*) <= scan_end; p += sizeof(void*)) {
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
    LargeObjHeader* h = g_heap->large_sentinel.next;
    while (h != &g_heap->large_sentinel) {
        LargeObjHeader* next = h->next;
        if (!h->marked) {
            // Unlink from list
            h->prev->next = h->next;
            h->next->prev = h->prev;
            freed_large++;
            platform_free_large(h, h->alloc_size);
        } else {
            live_bytes += h->data_size;
        }
        h = next;
    }

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

static void gc_collect_internal() {
    if (!g_heap) return;

    g_heap->collection_count++;
    g_in_collection = true;

    auto t0 = std::chrono::high_resolution_clock::now();
    gc_mark_phase();
    auto t1 = std::chrono::high_resolution_clock::now();
    gc_process_weak_refs();
    gc_process_finalizers();
    gc_sweep_phase();
    auto t2 = std::chrono::high_resolution_clock::now();

    g_in_collection = false;

    if (g_gc_verbose) {
        auto mark_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        auto sweep_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
        fprintf(stderr, "[TsGC]   mark=%lldus, sweep=%lldus\n",
                (long long)mark_us, (long long)sweep_us);
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

    if (g_gc_verbose) {
        fprintf(stderr, "[TsGC] Custom mark-sweep GC initialized "
                "(threshold=%zuMB, growth=%.1f, max_heap=%zuMB)\n",
                g_min_gc_threshold / (1024*1024),
                g_gc_growth_factor,
                g_max_heap_size / (1024*1024));
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

} // extern "C"
