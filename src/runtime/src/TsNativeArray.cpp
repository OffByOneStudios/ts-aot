// "use fast" NativeArray — an UNMANAGED, contiguous, typed container
// (docs/design/use-fast.md, Phase 2). Backing memory is malloc'd, OFF the GC
// heap: no GC scanning, no write barriers, cache-friendly linear layout (the
// SoA substrate for data-oriented code). Lifetime is explicit via an Allocator
// (Temp / Persistent) + dispose().
//
// 8-byte element slots (matching HIR's only numeric widths, Int64/Float64).
// Exact-width packing (4-byte i32/f32) is a later refinement.
//
// Allocator (docs/design/use-fast.md Phase 2c):
//   Persistent (1): malloc / free — caller-managed lifetime via dispose().
//   Temp (0): bump-allocated from a thread-local ARENA. dispose() is a no-op;
//     the whole frame is bulk-released at fast-function exit (the compiler
//     emits ts_native_arena_mark() at entry and ts_native_arena_release() on
//     each return). Returning a Temp array past the frame that made it is UB,
//     exactly like Unity's Allocator.Temp.
//
// The arena is UNMANAGED (malloc-backed, off the GC heap): none of the GC
// rooting rules apply — it deliberately holds no GC pointers, only raw bytes.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr uint32_t NARR_MAGIC    = 0x4E415252;  // 'NARR'
constexpr uint32_t NARR_DISPOSED = 0x44495350;  // 'DISP' — freed Persistent handle

struct TsNativeArray {
    uint32_t magic;
    int32_t  allocKind;   // 0 = Temp, 1 = Persistent
    int64_t  length;      // element count
    // 8-byte element slots follow inline (length * 8 bytes).
};

//==========================================================================
// Dev-mode safety checks (docs/design/use-fast.md Phase 3 — the Unity
// AtomicSafetyHandle analog). Enabled by TS_FAST_CHECKS=1 in the environment;
// OFF by default so release builds pay nothing beyond the bounds branch that
// already guards against UB. In dev mode, an out-of-bounds access, a
// use-after-dispose, or a double-dispose prints a diagnostic and aborts —
// turning silent corruption into a loud, located failure.
//==========================================================================
bool fast_checks_enabled() {
    static int cached = -1;
    if (cached < 0) {
        const char* e = std::getenv("TS_FAST_CHECKS");
        cached = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return cached != 0;
}

[[noreturn]] void fast_check_fail_msg(const char* op, const char* what) {
    std::fprintf(stderr, "[use fast] NativeArray.%s: %s\n", op, what);
    std::fflush(stderr);
    std::abort();
}

[[noreturn]] void fast_check_fail_oob(const char* op, int64_t i, int64_t len) {
    std::fprintf(stderr,
        "[use fast] NativeArray.%s: index %lld out of bounds [0, %lld)\n",
        op, (long long)i, (long long)len);
    std::fflush(stderr);
    std::abort();
}

// Resolve a handle for operation `op`. In dev mode, a null / disposed / invalid
// handle aborts with a diagnostic; in release it silently yields nullptr and
// the caller's bounds branch degrades to a safe no-op.
inline TsNativeArray* resolve(void* p, const char* op) {
    if (!p) {
        if (fast_checks_enabled()) fast_check_fail_msg(op, "called on a null handle");
        return nullptr;
    }
    TsNativeArray* a = (TsNativeArray*)p;
    if (a->magic == NARR_MAGIC) return a;
    if (fast_checks_enabled()) {
        fast_check_fail_msg(op, a->magic == NARR_DISPOSED
            ? "used after dispose()" : "called on an invalid handle");
    }
    return nullptr;
}

// Bounds check for element access. Returns true if in range; in dev mode an
// out-of-range index aborts.
inline bool in_bounds(TsNativeArray* a, int64_t i, const char* op) {
    if (i >= 0 && i < a->length) return true;
    if (fast_checks_enabled()) fast_check_fail_oob(op, i, a->length);
    return false;
}

inline uint64_t* slots(TsNativeArray* a) {
    return (uint64_t*)((char*)a + sizeof(TsNativeArray));
}

//==========================================================================
// Temp arena: a thread-local chain of bump blocks with LIFO frame markers.
//==========================================================================
struct ArenaBlock {
    ArenaBlock* prev;   // chain toward older blocks (also used for the freelist)
    size_t      cap;    // usable bytes in data[]
    size_t      used;   // bump offset
    char* data() { return reinterpret_cast<char*>(this + 1); }
};
struct ArenaFrame { ArenaBlock* block; size_t used; };

constexpr size_t kArenaBlock = 1u << 20;  // 1 MiB default block

thread_local ArenaBlock* g_arena_top  = nullptr;  // current bump block
thread_local ArenaBlock* g_arena_free = nullptr;  // recycled blocks
thread_local std::vector<ArenaFrame> g_arena_frames;

ArenaBlock* arena_take_block(size_t need) {
    // Reuse a big-enough recycled block if one exists.
    for (ArenaBlock** pp = &g_arena_free; *pp; pp = &(*pp)->prev) {
        if ((*pp)->cap >= need) {
            ArenaBlock* b = *pp;
            *pp = b->prev;
            b->used = 0;
            return b;
        }
    }
    size_t cap = need > kArenaBlock ? need : kArenaBlock;
    ArenaBlock* b = (ArenaBlock*)std::malloc(sizeof(ArenaBlock) + cap);
    if (!b) return nullptr;
    b->cap = cap;
    b->used = 0;
    return b;
}

void* arena_alloc(size_t n) {
    n = (n + 7) & ~size_t(7);  // 8-byte align
    if (!g_arena_top || g_arena_top->used + n > g_arena_top->cap) {
        ArenaBlock* b = arena_take_block(n);
        if (!b) return nullptr;
        b->prev = g_arena_top;
        g_arena_top = b;
    }
    void* p = g_arena_top->data() + g_arena_top->used;
    g_arena_top->used += n;
    return p;
}
}  // namespace

extern "C" {

// Push an arena frame; returns a LIFO token (frame depth). The compiler emits
// this at fast-function entry.
uint64_t ts_native_arena_mark() {
    g_arena_frames.push_back({ g_arena_top, g_arena_top ? g_arena_top->used : 0 });
    return (uint64_t)g_arena_frames.size();
}

// Pop back to the frame `token` marked, recycling any blocks allocated since.
// The compiler emits this on each return of a fast function.
void ts_native_arena_release(uint64_t token) {
    if (token == 0 || token > g_arena_frames.size()) return;
    ArenaFrame f = g_arena_frames[token - 1];
    g_arena_frames.resize(token - 1);
    // Recycle blocks newer than the marked block onto the freelist.
    while (g_arena_top && g_arena_top != f.block) {
        ArenaBlock* b = g_arena_top;
        g_arena_top = b->prev;
        b->prev = g_arena_free;
        g_arena_free = b;
    }
    if (g_arena_top) g_arena_top->used = f.used;  // rewind within the frame block
}

// Allocate a NativeArray of `length` 8-byte slots, zero-initialized.
// allocKind 0 = Temp (arena, bulk-released), 1 = Persistent (malloc/free).
void* ts_native_array_new(int64_t length, int32_t allocKind) {
    if (length < 0) length = 0;
    size_t total = sizeof(TsNativeArray) + (size_t)length * 8;
    TsNativeArray* a = (allocKind == 1)
        ? (TsNativeArray*)std::malloc(total)
        : (TsNativeArray*)arena_alloc(total);
    if (!a) return nullptr;
    a->magic = NARR_MAGIC;
    a->allocKind = allocKind;
    a->length = length;
    std::memset(slots(a), 0, (size_t)length * 8);
    return a;
}

int64_t ts_native_array_length(void* arr) {
    TsNativeArray* a = resolve(arr, "length");
    return a ? a->length : 0;
}

double ts_native_array_get_f64(void* arr, int64_t i) {
    TsNativeArray* a = resolve(arr, "get");
    if (!a || !in_bounds(a, i, "get")) return 0.0;
    double v;
    std::memcpy(&v, &slots(a)[i], 8);
    return v;
}

void ts_native_array_set_f64(void* arr, int64_t i, double v) {
    TsNativeArray* a = resolve(arr, "set");
    if (!a || !in_bounds(a, i, "set")) return;
    std::memcpy(&slots(a)[i], &v, 8);
}

int64_t ts_native_array_get_i64(void* arr, int64_t i) {
    TsNativeArray* a = resolve(arr, "get");
    if (!a || !in_bounds(a, i, "get")) return 0;
    return (int64_t)slots(a)[i];
}

void ts_native_array_set_i64(void* arr, int64_t i, int64_t v) {
    TsNativeArray* a = resolve(arr, "set");
    if (!a || !in_bounds(a, i, "set")) return;
    slots(a)[i] = (uint64_t)v;
}

// Dispose a NativeArray. Persistent (1) is freed; Temp (0) is a no-op — its
// backing lives in the arena and is bulk-released at frame exit, so calling
// free() on it would corrupt the C heap.
//
// Dev mode (TS_FAST_CHECKS=1): a Persistent handle is marked NARR_DISPOSED but
// NOT freed, so a later use-after-dispose or double-dispose is caught by
// resolve() instead of reading freed memory. (This intentionally leaks in dev —
// a debugging trade, like Unity's leak detector.)
void ts_native_array_dispose(void* arr) {
    if (!arr) {
        if (fast_checks_enabled()) fast_check_fail_msg("dispose", "called on a null handle");
        return;
    }
    TsNativeArray* a = (TsNativeArray*)arr;
    if (a->magic != NARR_MAGIC) {
        if (fast_checks_enabled())
            fast_check_fail_msg("dispose", a->magic == NARR_DISPOSED
                ? "called twice (double dispose)" : "called on an invalid handle");
        return;
    }
    if (a->allocKind == 1) {
        a->magic = NARR_DISPOSED;
        if (!fast_checks_enabled()) std::free(a);
    }
    // Temp: no-op; ts_native_arena_release reclaims the whole frame. Using a
    // Temp array after dispose() is legal (valid until frame exit).
}

}  // extern "C"
