#pragma once
//
// TsTyped.h — validated, type-safe magic-tag dispatch.
//
// PROBLEM THIS SOLVES
//   The runtime identifies heap objects by reading a 4-byte "magic" word and
//   comparing it to a per-type constant. Historically this was written by hand
//   at ~300 sites as `*(uint32_t*)((char*)p + OFF) == TsT::MAGIC`, where OFF was
//   a literal (0, 8, 16, 20, 24) that the author had to know for each type.
//   Two failure modes follow:
//     1. No pointer validation -> reading the magic off a NaN-boxed int, null,
//        or stale pointer dereferences garbage and crashes / misdispatches.
//     2. The offset is hand-written and type-dependent, so it silently desyncs
//        from the C++ layout. (Real example: TsBuffer's magic is at offset 16,
//        but several sites read offset 0 and compared to TsBuffer::MAGIC -> the
//        branch never matched a real Buffer.)
//
// THE FIX
//   `ts_cast<T>(p)` / `ts_is<T>(p)` derive the offset from the type itself via
//   offsetof(T, magic) (cannot desync) and route the pointer through the
//   existing crash-safe heap guard `ts_gc_is_heap_object` before any deref.
//
//   Enrol a type with TS_DECLARE_TAG(T) once (T must expose `static constexpr
//   uint32_t MAGIC` and a `uint32_t magic` member). Using ts_cast<T> on a type
//   without a tag is a compile error, not a silent 0.
//
// SCOPE / LIMITS
//   - Use this for POD / single-inheritance tagged types (TsArray, TsString,
//     TsBuffer, TsTypedArray, TsMap, ...). For VIRTUALLY-inherited types (the
//     stream family: TsReadable : public virtual TsEventEmitter) the magic
//     offset is unstable — keep using dynamic_cast / AsXxx() there, per
//     .claude/rules/runtime-safety.md. TS_DECLARE_TAG is the opt-in boundary.
//   - `ts_cast<T>` calls ts_gc_is_heap_object (cheap: null + tagged-pointer
//     reject + gc_find_base). For pre-validated pointers on hot paths use
//     `ts_cast_unchecked<T>`, which keeps the layout-derived offset but skips
//     the heap guard. Measure before putting the checked form on a hot path.
//
#include <cstdint>
#include <cstddef>

// Crash-safe liveness guard: true iff ptr is a currently-allocated GC object.
// Defined in src/runtime/src/TsGC.cpp (declared here to avoid pulling in all of
// TsGC.h at every include site).
extern "C" bool ts_gc_is_heap_object(void* ptr);

// Per-type tag descriptor. Primary template is intentionally left undefined so
// that ts_cast<T> on an un-enrolled type fails to compile.
template <class T>
struct TsTagOf;

#define TS_DECLARE_TAG(Type)                                                   \
    template <>                                                                \
    struct TsTagOf<Type> {                                                     \
        static constexpr uint32_t kMagic = Type::MAGIC;                        \
        static constexpr size_t   kOffset = offsetof(Type, magic);             \
    }

namespace ts_typed_detail {
inline uint32_t read_tag(const void* p, size_t off) {
    return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const char*>(p) + off);
}
}  // namespace ts_typed_detail

// Validated downcast. Returns nullptr unless `p` is a live heap object whose
// magic tag matches T. Never dereferences a bad pointer.
template <class T>
inline T* ts_cast(void* p) {
    if (!ts_gc_is_heap_object(p)) return nullptr;
    if (ts_typed_detail::read_tag(p, TsTagOf<T>::kOffset) != TsTagOf<T>::kMagic)
        return nullptr;
    return reinterpret_cast<T*>(p);
}

template <class T>
inline bool ts_is(void* p) {
    return ts_cast<T>(p) != nullptr;
}

// Cheap inline pointer-canonicality guard: rejects null, tagged NaN-box
// payloads, and non-canonical (high-bits-set) addresses WITHOUT touching the
// GC's block descriptors. This is the guard hot paths can afford.
//
// Rationale (measured): the full ts_gc_is_heap_object calls gc_find_base, which
// does rebuild_descriptors() + a binary search over block (and large-object)
// descriptors — fine for cold/error paths, but far too costly to run on every
// dynamic property access / typeof / equality. Hot classification sites should
// use the *_unchecked forms below, which keep the layout-derived offset (so they
// still can't desync) and this cheap canonicality check, but skip gc_find_base.
inline bool ts_ptr_is_canonical(const void* p) {
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000 && (v >> 48) == 0;
}

// Offset-correct downcast WITHOUT the heap-liveness guard. Use on hot paths, or
// when the caller has already established that `p` is a live heap pointer.
// Still immune to offset desync; still rejects obviously-bad pointers cheaply.
template <class T>
inline T* ts_cast_unchecked(void* p) {
    if (!ts_ptr_is_canonical(p)) return nullptr;
    if (ts_typed_detail::read_tag(p, TsTagOf<T>::kOffset) != TsTagOf<T>::kMagic)
        return nullptr;
    return reinterpret_cast<T*>(p);
}

template <class T>
inline bool ts_is_unchecked(void* p) {
    return ts_cast_unchecked<T>(p) != nullptr;
}
