#include "GCRoots.h"
#include "StackMap.h"
#include <cstdio>
#include <cstdlib>

#define GC_THREADS
#include <gc/gc.h>
#include <gc/gc_mark.h>

#ifdef _WIN32
#include <windows.h>
#include <winternl.h>
#endif

// ============================================================================
// Precise GC Root Pushing via LLVM Stack Maps
//
// During each Boehm GC collection, this module walks the Windows x64 call
// stack using RtlVirtualUnwind, looks up each frame's return address in
// the parsed stack map hash table, extracts GC root pointer values from
// stack slots, validates them via GC_base(), and pushes them to Boehm's
// mark queue via GC_push_all_eager().
//
// This supplements (does not replace) Boehm's conservative stack scanning.
// ============================================================================

// Chain to previous push_other_roots handler
static GC_push_other_roots_proc g_prev_push_other_roots = nullptr;

// Verbose logging (enabled via TS_GC_ROOTS_VERBOSE env var)
static bool g_gc_roots_verbose = false;

// Per-collection diagnostic counters
static size_t g_last_frames_walked = 0;
static size_t g_last_safepoints_found = 0;
static size_t g_last_roots_pushed = 0;
static size_t g_total_collections = 0;

// Safety limits
static const size_t MAX_STACK_FRAMES = 256;
static const size_t ROOT_BUFFER_SIZE = 512;  // 512 pointers = 4KB on stack

// ============================================================================
// Windows x64 Implementation
// ============================================================================

#ifdef _WIN32

// Map DWARF x86-64 register numbers to Windows CONTEXT fields
static uint64_t get_register_value(const CONTEXT& ctx, uint16_t dwarfReg) {
    switch (dwarfReg) {
        case 0:  return ctx.Rax;
        case 1:  return ctx.Rdx;
        case 2:  return ctx.Rcx;
        case 3:  return ctx.Rbx;
        case 4:  return ctx.Rsi;
        case 5:  return ctx.Rdi;
        case 6:  return ctx.Rbp;
        case 7:  return ctx.Rsp;
        case 8:  return ctx.R8;
        case 9:  return ctx.R9;
        case 10: return ctx.R10;
        case 11: return ctx.R11;
        case 12: return ctx.R12;
        case 13: return ctx.R13;
        case 14: return ctx.R14;
        case 15: return ctx.R15;
        default: return 0;
    }
}

// Check if an address is within the current thread's stack bounds
static bool is_valid_stack_address(void* addr) {
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    return (addr >= tib->StackLimit && addr < tib->StackBase);
}

// Extract a GC root pointer from a stack frame using stack map location info
static void* extract_root_pointer(const CONTEXT& ctx, const GCRootLocation& root) {
    uint64_t regVal = get_register_value(ctx, root.regNum);
    if (regVal == 0) return nullptr;

    switch (root.locType) {
        case 1: // Register: the pointer IS the register value
            return (void*)regVal;

        case 3: { // Indirect: pointer is at *(reg + offset)
            void** slot = (void**)((uint8_t*)regVal + root.offset);
            if (!is_valid_stack_address(slot)) return nullptr;
            return *slot;
        }

        default:
            return nullptr;
    }
}

// Boehm GC push_other_roots callback: walk stack and push precise roots
static void GC_CALLBACK ts_push_precise_roots(void) {
    // Chain to previous handler first (Boehm's default thread stack pushing)
    if (g_prev_push_other_roots) {
        g_prev_push_other_roots();
    }

    // Skip if no stack maps were parsed
    if (ts_stackmap_count() == 0) return;

    // Reset per-collection counters
    g_last_frames_walked = 0;
    g_last_safepoints_found = 0;
    g_last_roots_pushed = 0;
    g_total_collections++;

    // Stack-local root buffer (no heap allocation during mark phase!)
    void* rootBuffer[ROOT_BUFFER_SIZE];
    size_t rootCount = 0;

    // Capture current CPU context
    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&ctx);

    // Walk the stack frame by frame
    for (size_t frame = 0; frame < MAX_STACK_FRAMES; frame++) {
        if (ctx.Rip == 0) break;
        g_last_frames_walked++;

        // Look up this return address in the stack map
        const SafepointInfo* sp = ts_stackmap_lookup(ctx.Rip);
        if (sp) {
            g_last_safepoints_found++;

            // Extract and validate each GC root
            for (uint16_t i = 0; i < sp->numRoots; i++) {
                void* ptr = extract_root_pointer(ctx, sp->roots[i]);

                // Validate: non-null and points to a GC-allocated object
                if (ptr && GC_base(ptr)) {
                    rootBuffer[rootCount++] = ptr;

                    // Flush buffer if full
                    if (rootCount >= ROOT_BUFFER_SIZE) {
                        GC_push_all_eager(rootBuffer, rootBuffer + rootCount);
                        g_last_roots_pushed += rootCount;
                        rootCount = 0;
                    }
                }
            }
        }

        // Unwind to the next (caller) frame
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION funcEntry = RtlLookupFunctionEntry(ctx.Rip, &imageBase, NULL);

        if (!funcEntry) {
            // Leaf frame (no unwind info): return address is at [RSP]
            if (!is_valid_stack_address((void*)ctx.Rsp)) break;
            ctx.Rip = *(DWORD64*)ctx.Rsp;
            ctx.Rsp += 8;
            if (ctx.Rip == 0) break;
            continue;
        }

        // Use RtlVirtualUnwind to walk to the caller frame
        DWORD64 prevRip = ctx.Rip;
        void* handlerData = nullptr;
        DWORD64 establisherFrame = 0;
        RtlVirtualUnwind(
            UNW_FLAG_NHANDLER,
            imageBase,
            ctx.Rip,
            funcEntry,
            &ctx,
            &handlerData,
            &establisherFrame,
            NULL
        );

        // Stuck detection: if RIP didn't change, break
        if (ctx.Rip == prevRip) break;
    }

    // Final flush of remaining roots
    if (rootCount > 0) {
        GC_push_all_eager(rootBuffer, rootBuffer + rootCount);
        g_last_roots_pushed += rootCount;
    }

    if (g_gc_roots_verbose) {
        fprintf(stderr, "[GCRoots] Collection #%zu: %zu frames, %zu safepoints, %zu roots\n",
                g_total_collections, g_last_frames_walked,
                g_last_safepoints_found, g_last_roots_pushed);
        fflush(stderr);
    }
}

#else // Non-Windows stub

static void GC_CALLBACK ts_push_precise_roots(void) {
    // Chain to previous handler
    if (g_prev_push_other_roots) {
        g_prev_push_other_roots();
    }
    // No stack walking on non-Windows yet
}

#endif // _WIN32

// ============================================================================
// Public API
// ============================================================================

extern "C" {

void ts_gc_roots_init() {
    // Only enable if stack maps exist
    if (ts_stackmap_count() == 0) return;

    // Chain with existing push_other_roots handler
    g_prev_push_other_roots = GC_get_push_other_roots();
    GC_set_push_other_roots(ts_push_precise_roots);

    // Check for verbose logging via environment variable
    if (getenv("TS_GC_ROOTS_VERBOSE")) {
        g_gc_roots_verbose = true;
    }

    fprintf(stderr, "[GCRoots] Precise root pushing enabled for %zu safepoints\n",
            ts_stackmap_count());
    fflush(stderr);
}

size_t ts_gc_roots_last_frames_walked() {
    return g_last_frames_walked;
}

size_t ts_gc_roots_last_safepoints_found() {
    return g_last_safepoints_found;
}

size_t ts_gc_roots_last_roots_pushed() {
    return g_last_roots_pushed;
}

size_t ts_gc_roots_total_collections() {
    return g_total_collections;
}

}
