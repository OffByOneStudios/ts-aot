// Self-hosted builtin installation.
//
// The prelude (src/runtime/prelude/*.ts) writes spec-correct builtins in TS and
// installs them by calling the global __defineBuiltin(target, name, length, fn).
// This records the impl pointer in a small per-method table so the hard-wired
// array natives (ts_array_filter, …) can delegate a direct `arr.method(...)`
// call to the self-hosted impl — the dispatch keystone — and installs the
// closure onto the target prototype with correct builtin attributes
// (non-enumerable, length, name, no [[Construct]]).
//
// Keep this file small and focused; per-family self-hosted TS lives under
// src/runtime/prelude/, not here.

#include "TsObject.h"
#include "TsClosure.h"
#include "TsString.h"
#include <cstring>
#include <cstdint>

extern "C" {
// Self-hosted Array.prototype method implementations, looked up by the
// corresponding array natives. nullptr => no self-hosted impl installed (take
// the native path). A direct pointer check, independent of the prototype
// version counter, so installing the prelude does not force every array method
// onto its slow path.
void* g_selfhosted_filter = nullptr;
}

extern "C" void ts_define_builtin_method(TsValue* target, TsValue* nameStr,
                                         int32_t length, TsValue* fn) {
    if (!nameStr || !fn) return;
    void* nsRaw = ts_value_get_string(nameStr);
    const char* name = nsRaw ? ((TsString*)nsRaw)->ToUtf8() : nullptr;
    if (!name) return;

    // Record the self-hosted impl so the native delegates direct calls to it.
    if (strcmp(name, "filter") == 0) g_selfhosted_filter = (void*)fn;

    // Give the closure spec builtin metadata: arity (.length), .name, and no
    // [[Construct]] / .prototype.
    void* fnRaw = ts_value_get_object(fn);
    if (fnRaw && *(uint32_t*)((char*)fnRaw + 16) == 0x434C5352 /* TsClosure */) {
        TsClosure* cl = (TsClosure*)fnRaw;
        ts_closure_set_no_prototype(cl);
        ts_closure_set_arity(cl, length);
        if (nsRaw) ts_closure_set_name(cl, nsRaw);
    }

    // NOTE: we deliberately do NOT install onto the target prototype. The
    // Array.prototype.<m> getter keeps returning the native wrapper, which does
    // the inverted dispatch (packed real array → C++ fast loop; holey/array-like
    // → the self-hosted impl recorded above). Installing the closure on the slot
    // would route every dynamic `arr.<m>()` straight to the JS impl, defeating the
    // fast path. (Stage C may revisit this for method identity/metadata tests.)
    (void)target;
}
