// "use fast" NativeArray — an UNMANAGED, contiguous, typed container
// (docs/design/use-fast.md, Phase 2). Backing memory is malloc'd, OFF the GC
// heap: no GC scanning, no write barriers, cache-friendly linear layout (the
// SoA substrate for data-oriented code). Lifetime is explicit via an Allocator
// (Temp / Persistent) + dispose().
//
// First cut: 8-byte element slots (matching HIR's only numeric widths,
// Int64/Float64). Exact-width packing (4-byte i32/f32) and Temp arena auto-free
// are later refinements; today Temp and Persistent both malloc + dispose().
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
constexpr uint32_t NARR_MAGIC = 0x4E415252;  // 'NARR'

struct TsNativeArray {
    uint32_t magic;
    int32_t  allocKind;   // 0 = Temp, 1 = Persistent
    int64_t  length;      // element count
    // 8-byte element slots follow inline (length * 8 bytes).
};

inline TsNativeArray* asNarr(void* p) {
    if (!p) return nullptr;
    TsNativeArray* a = (TsNativeArray*)p;
    return (a->magic == NARR_MAGIC) ? a : nullptr;
}
inline uint64_t* slots(TsNativeArray* a) {
    return (uint64_t*)((char*)a + sizeof(TsNativeArray));
}
}  // namespace

extern "C" {

// Allocate a NativeArray of `length` 8-byte slots, zero-initialized.
void* ts_native_array_new(int64_t length, int32_t allocKind) {
    if (length < 0) length = 0;
    size_t total = sizeof(TsNativeArray) + (size_t)length * 8;
    TsNativeArray* a = (TsNativeArray*)std::malloc(total);
    if (!a) return nullptr;
    a->magic = NARR_MAGIC;
    a->allocKind = allocKind;
    a->length = length;
    std::memset(slots(a), 0, (size_t)length * 8);
    return a;
}

int64_t ts_native_array_length(void* arr) {
    TsNativeArray* a = asNarr(arr);
    return a ? a->length : 0;
}

double ts_native_array_get_f64(void* arr, int64_t i) {
    TsNativeArray* a = asNarr(arr);
    if (!a || i < 0 || i >= a->length) return 0.0;
    double v;
    std::memcpy(&v, &slots(a)[i], 8);
    return v;
}

void ts_native_array_set_f64(void* arr, int64_t i, double v) {
    TsNativeArray* a = asNarr(arr);
    if (!a || i < 0 || i >= a->length) return;
    std::memcpy(&slots(a)[i], &v, 8);
}

int64_t ts_native_array_get_i64(void* arr, int64_t i) {
    TsNativeArray* a = asNarr(arr);
    if (!a || i < 0 || i >= a->length) return 0;
    return (int64_t)slots(a)[i];
}

void ts_native_array_set_i64(void* arr, int64_t i, int64_t v) {
    TsNativeArray* a = asNarr(arr);
    if (!a || i < 0 || i >= a->length) return;
    slots(a)[i] = (uint64_t)v;
}

// Free a Persistent NativeArray (no-op for a null / already-freed handle).
void ts_native_array_dispose(void* arr) {
    TsNativeArray* a = asNarr(arr);
    if (a) { a->magic = 0; std::free(a); }
}

}  // extern "C"
