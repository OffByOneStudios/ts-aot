#include "TsArray_Internal.h"

// Array operation methods extracted from TsArray.cpp (TsArray:: members; decls
// stay in TsArray.h): Sort, IndexOf/LastIndexOf/Includes/Remove, Flat/FlatMap,
// ForEach/Map/Filter/Reduce/ReduceRight/Some/Every, Find*/FindIndex*, At, Reverse,
// ToReversed/ToSorted/ToSpliced/With, Slice, Join.

void TsArray::Sort() {
    std::sort((int64_t*)elements, ((int64_t*)elements) + length, jsDefaultSortComparator);
}

// Strict-equality comparison of two NaN-boxed array elements, used by the
// generic (non-specialized) IndexOf/LastIndexOf scans: exact bit match,
// TsString value equality, or numeric cross-type equality.
static inline bool array_search_equals(uint64_t elemNB, uint64_t searchNB) {
    if (elemNB == searchNB) return true;  // ints, bools, null, undefined, same ptr
    if (nanbox_is_ptr(elemNB) && nanbox_is_ptr(searchNB)) {
        void* ep = nanbox_to_ptr(elemNB);
        void* sp = nanbox_to_ptr(searchNB);
        if (ep && sp) {
            uint32_t eMagic = *(uint32_t*)ep;
            uint32_t sMagic = *(uint32_t*)sp;
            if (eMagic == TsString::MAGIC && sMagic == TsString::MAGIC) {
                if (((TsString*)ep)->Equals((TsString*)sp)) return true;
            }
        }
    }
    if (nanbox_is_number(elemNB) && nanbox_is_number(searchNB)) {
        double ed = nanbox_to_number(elemNB);
        double sd = nanbox_to_number(searchNB);
        if (ed == sd) return true;
    }
    return false;
}

int64_t TsArray::IndexOf(int64_t value, size_t fromIndex) {
    if (fromIndex >= length) return -1;
    // For PackedDouble arrays, codegen passes raw double bits directly (via bitcast).
    // Handle this FIRST to avoid trying to dereference double bits as a pointer.
    if (isSpecialized && isDouble) {
        // Value is raw IEEE 754 double bits - compare directly
        double searchVal;
        memcpy(&searchVal, &value, sizeof(double));

        for (size_t i = fromIndex; i < length; ++i) {
            if (((double*)elements)[i] == searchVal) return (int64_t)i;
        }
        return -1;
    }

    // For specialized integer arrays (PackedSmi), unbox the NaN-boxed search value
    if (isSpecialized) {
        uint64_t nb = (uint64_t)value;
        int64_t rawValue;
        if (nanbox_is_int32(nb)) {
            rawValue = (int64_t)nanbox_to_int32(nb);
        } else if (nanbox_is_double(nb)) {
            rawValue = (int64_t)nanbox_to_double(nb);
        } else {
            rawValue = value;  // raw int passed directly
        }
        for (size_t i = fromIndex; i < length; ++i) {
            if (((int64_t*)elements)[i] == rawValue) return (int64_t)i;
        }
        return -1;
    }

    // For non-specialized (generic) arrays, elements are NaN-boxed values stored as int64.
    // The search value is also NaN-boxed. Compare using NaN-box decoding.
    uint64_t searchNB = (uint64_t)value;

    // Dense region first (lower indices win for indexOf). Bound to capacity:
    // a sparse array has length > capacity with far indices in sparseElements,
    // so reading ((int64_t*)elements)[i] up to `length` would run past the
    // backing buffer (crash) and iterate a multi-billion hole gap (hang).
    size_t denseTo = length < capacity ? length : capacity;
    for (size_t i = fromIndex; i < denseTo; ++i) {
        uint64_t elemNB = (uint64_t)((int64_t*)elements)[i];
        if (array_search_equals(elemNB, searchNB)) return (int64_t)i;
    }
    // Sparse store: lowest stored index >= fromIndex that matches.
    if (length > capacity && sparseElements) {
        int64_t best = -1;
        TsArray* ents = (TsArray*)sparseElements->GetEntries();
        int64_t n = ents->Length();
        for (int64_t e = 0; e < n; ++e) {
            TsArray* kv = (TsArray*)ents->Get(e);
            TsValue keyTv = nanbox_to_tagged((TsValue*)kv->Get(0));
            int64_t k = (keyTv.type == ValueType::NUMBER_INT) ? keyTv.i_val
                      : (keyTv.type == ValueType::NUMBER_DBL) ? (int64_t)keyTv.d_val : -1;
            if (k < 0 || (size_t)k < fromIndex) continue;
            uint64_t elemNB = (uint64_t)readSlot((size_t)k);
            if (array_search_equals(elemNB, searchNB) && (best < 0 || k < best)) best = k;
        }
        if (best >= 0) return best;
    }
    return -1;
}

int64_t TsArray::LastIndexOf(int64_t value, int64_t fromIndex) {
    if (length == 0) return -1;
    // Clamp fromIndex: -1 (default) means "from end" i.e. length-1.
    // Otherwise fromIndex is an upper-bound INCLUSIVE.
    size_t startFrom;
    if (fromIndex < 0 || fromIndex >= (int64_t)length) {
        startFrom = length;  // iterate full [length..1], i.e. all indices
    } else {
        startFrom = (size_t)(fromIndex + 1);  // inclusive upper bound → i iterates [startFrom..1]
    }
    // For PackedDouble arrays, codegen passes raw double bits directly (via bitcast).
    // Handle this FIRST to avoid trying to dereference double bits as a pointer.
    if (isSpecialized && isDouble) {
        // Value is raw IEEE 754 double bits - compare directly
        double searchVal;
        memcpy(&searchVal, &value, sizeof(double));

        for (size_t i = startFrom; i > 0; --i) {
            if (((double*)elements)[i - 1] == searchVal) return (int64_t)(i - 1);
        }
        return -1;
    }

    // For specialized integer arrays (PackedSmi), unbox the search value and compare directly
    if (isSpecialized) {
        int64_t rawValue = value;
        // Decode NaN-boxed value to extract integer for comparison
        TsValue decoded = nanbox_to_tagged((TsValue*)value);
        if (decoded.type == ValueType::NUMBER_INT) {
            rawValue = decoded.i_val;
        } else if (decoded.type == ValueType::NUMBER_DBL) {
            rawValue = (int64_t)decoded.d_val;
        }
        for (size_t i = startFrom; i > 0; --i) {
            if (((int64_t*)elements)[i - 1] == rawValue) return (int64_t)(i - 1);
        }
        return -1;
    }

    // For non-specialized (generic) arrays, elements are NaN-boxed values
    // stored as int64. Compare via NaN-box decoding.
    uint64_t searchNB = (uint64_t)value;

    // Sparse arrays (length > capacity) keep far indices in sparseElements.
    // Those indices are all higher than any dense slot, so a sparse match is
    // always the last index — check the sparse store first. This also avoids
    // reading ((int64_t*)elements)[i-1] past the backing buffer (crash) and
    // iterating the multi-billion hole gap of a huge sparse length (hang).
    if (length > capacity && sparseElements) {
        int64_t best = -1;
        TsArray* ents = (TsArray*)sparseElements->GetEntries();
        int64_t n = ents->Length();
        for (int64_t e = 0; e < n; ++e) {
            TsArray* kv = (TsArray*)ents->Get(e);
            TsValue keyTv = nanbox_to_tagged((TsValue*)kv->Get(0));
            int64_t k = (keyTv.type == ValueType::NUMBER_INT) ? keyTv.i_val
                      : (keyTv.type == ValueType::NUMBER_DBL) ? (int64_t)keyTv.d_val : -1;
            if (k < 0 || (size_t)k >= startFrom) continue;  // index < startFrom
            uint64_t elemNB = (uint64_t)readSlot((size_t)k);
            if (array_search_equals(elemNB, searchNB) && k > best) best = k;
        }
        if (best >= 0) return best;
    }

    size_t denseFrom = startFrom < capacity ? startFrom : capacity;
    for (size_t i = denseFrom; i > 0; --i) {
        uint64_t elemNB = (uint64_t)((int64_t*)elements)[i - 1];
        if (array_search_equals(elemNB, searchNB)) return (int64_t)(i - 1);
    }
    return -1;
}

bool TsArray::Includes(int64_t value, size_t fromIndex) {
    // ECMA-262 23.1.3.16 reads array holes as `undefined` (unlike indexOf, which
    // skips them). Only the undefined search needs special handling — for every
    // other value defer to IndexOf, which carries the full SameValueZero/element
    // comparison. A hole matched against undefined returns true.
    if (nanbox_is_undefined((uint64_t)value)) {
        for (size_t i = fromIndex; i < length; i++) {
            if (IsHole(i)) return true;
            if (((int64_t*)elements)[i] == value) return true;
        }
        return false;
    }
    return IndexOf(value, fromIndex) != -1;
}

void TsArray::Remove(int64_t value) {
    int64_t index = IndexOf(value);
    if (index == -1) return;
    
    if (index < (int64_t)length - 1) {
        std::memmove((int64_t*)elements + index, (int64_t*)elements + index + 1, (length - index - 1) * 8);
    }
    length--;
}

void* TsArray::Flat(int64_t depth) {
    TsArray* result = TsArray::Create();
    for (size_t i = 0; i < length; ++i) {
        int64_t val = ((int64_t*)elements)[i];
        uint64_t nb = (uint64_t)val;
        if (depth > 0 && nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            if (ptr) {
                // Check magic at offset 0 for TsArray
                uint32_t magic = *(uint32_t*)ptr;
                if (magic == TsArray::MAGIC) {
                    TsArray* sub = (TsArray*)ptr;
                    TsArray* flattenedSub = (TsArray*)sub->Flat(depth - 1);
                    for (size_t j = 0; j < flattenedSub->length; ++j) {
                        result->Push(((int64_t*)flattenedSub->elements)[j]);
                    }
                    continue;
                }
            }
        }
        result->Push(val);
    }
    return result;
}

void* TsArray::FlatMap(void* callback, void* thisArg) {
    TsArray* mapped = (TsArray*)Map(callback, thisArg);
    return mapped->Flat(1);
}

void TsArray::ForEach(void* callback, void* thisArg) {
    const size_t len0 = length;  // ES: capture length once (ignore mid-iteration growth)
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback) && g_array_prototype_version == 0 && !thisArg) {
        TsClosure* closure = (TsClosure*)callback;
        if (IsDoubleArray() || IsSmiArray()) {
            // Fast path: numeric arrays with typed closures (double calling convention)
            for (size_t i = 0; i < len0; ++i) {
                double elem = GetElementDouble(i);
                ts_closure_invoke_1d_void(closure, elem);
            }
        } else {
            // Generic path: string/mixed arrays with untyped closures (ptr calling convention)
            for (size_t i = 0; i < len0; ++i) {
                if (IsHole(i)) continue;  // spec: skip holes
                if (array_generic_absent_index(this, i)) continue;  // skip absent array-like index
                TsValue* v = GetElementBoxed(i);
                TsValue* idx = ts_value_make_int(i);
                TsValue* arr = ts_value_make_object(CallbackReceiver());
                if (thisArg)
                    ts_call_with_this_3((TsValue*)callback, (TsValue*)thisArg, v, idx, arr);
                else
                    tsCall((TsValue*)callback, v, idx, arr);
            }
        }
        return;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return;

    bool slow = (g_array_prototype_version != 0);
    TsValue* thisArgV = (TsValue*)thisArg;
    for (size_t i = 0; i < len0; ++i) {
        TsValue* v;
        if (slow) {
            if (!ts_array_has_property_at(this, (int64_t)i)) continue;
            v = ts_array_get_property_at(this, (int64_t)i);
        } else {
            if (IsHole(i)) continue;  // spec: skip holes in fast path
            v = GetElementBoxed(i);
        }
        if (array_generic_absent_index(this, i)) continue;  // skip absent array-like index
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        if (thisArgV)
            ts_call_with_this_3(cbVal, thisArgV, v, idx, arr);
        else
            tsCall(cbVal, v, idx, arr);
    }
}

void* TsArray::Map(void* callback, void* thisArg) {
    const size_t len0 = length;  // ES: capture length once (ignore mid-iteration growth)
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback) && g_array_prototype_version == 0 && !thisArg) {
        TsClosure* closure = (TsClosure*)callback;
        TsArray* result = TsArray::Create(length);
        if (IsDoubleArray() || IsSmiArray()) {
            // Fast path: numeric arrays with typed closures (double calling convention)
            for (size_t i = 0; i < len0; ++i) {
                double elem = GetElementDouble(i);
                double mapped = ts_closure_invoke_1d(closure, elem);
                result->PushDouble(mapped);
            }
        } else {
            // Generic path: string/mixed arrays with untyped closures (ptr calling convention)
            for (size_t i = 0; i < len0; ++i) {
                if (IsHole(i) || array_generic_absent_index(this, i)) {
                    // Preserve hole in output (CreateSized would be cleaner,
                    // but this path uses Push). Push a placeholder; close this
                    // edge case with the full-correctness path below. Absent
                    // indices on a generic array-like receiver are likewise
                    // not mapped (spec: HasProperty gate) — keep alignment.
                    result->Push((int64_t)ts_value_make_undefined());
                    continue;
                }
                TsValue* v = GetElementBoxed(i);
                TsValue* idx = ts_value_make_int(i);
                TsValue* arr = ts_value_make_object(CallbackReceiver());
                TsValue* res = thisArg
                    ? ts_call_with_this_3((TsValue*)callback, (TsValue*)thisArg, v, idx, arr)
                    : tsCall((TsValue*)callback, v, idx, arr);
                result->Push((int64_t)res);
            }
        }
        return result;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return nullptr;

    // Start result with `length` holes; we'll overwrite non-hole positions.
    TsArray* result = TsArray::CreateSized(length);
    bool slow = (g_array_prototype_version != 0);
    TsValue* thisArgV = (TsValue*)thisArg;
    for (size_t i = 0; i < len0; ++i) {
        TsValue* v;
        if (slow) {
            if (!ts_array_has_property_at(this, (int64_t)i)) continue;
            v = ts_array_get_property_at(this, (int64_t)i);
        } else {
            if (IsHole(i)) continue;
            v = GetElementBoxed(i);
        }
        // Absent index on a generic array-like receiver: not mapped; the
        // CreateSized(length) result keeps its pre-filled hole here.
        if (array_generic_absent_index(this, i)) continue;
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        TsValue* res = thisArgV
            ? ts_call_with_this_3(cbVal, thisArgV, v, idx, arr)
            : tsCall(cbVal, v, idx, arr);
        result->SetUnchecked(i, (int64_t)res);
    }
    return result;
}

void* TsArray::Filter(void* callback, void* thisArg) {
    const size_t len0 = length;  // ES: capture length once (ignore mid-iteration growth)
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback) && g_array_prototype_version == 0 && !thisArg) {
        TsClosure* closure = (TsClosure*)callback;
        TsArray* result = TsArray::Create();
        if (IsDoubleArray() || IsSmiArray()) {
            // Fast path: numeric arrays with typed closures
            for (size_t i = 0; i < len0; ++i) {
                double elem = GetElementDouble(i);
                if (ts_closure_invoke_1d_bool(closure, elem)) {
                    result->PushDouble(elem);
                }
            }
        } else {
            // Generic path: string/mixed arrays with untyped closures
            for (size_t i = 0; i < len0; ++i) {
                if (IsHole(i)) continue;  // spec: skip holes
                if (array_generic_absent_index(this, i)) continue;  // skip absent array-like index
                TsValue* v = GetElementBoxed(i);
                TsValue* idx = ts_value_make_int(i);
                TsValue* arr = ts_value_make_object(CallbackReceiver());
                TsValue* res = thisArg
                    ? ts_call_with_this_3((TsValue*)callback, (TsValue*)thisArg, v, idx, arr)
                    : tsCall((TsValue*)callback, v, idx, arr);
                if (ts_value_to_bool(res)) {
                    result->Push(((int64_t*)elements)[i]);
                }
            }
        }
        return result;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return nullptr;

    // Preserve the source array's specialized type in the result
    TsArray* result;
    if (isSpecialized) {
        result = TsArray::CreateSpecialized(0, elementSize, isDouble);
    } else {
        result = TsArray::Create();
    }

    bool slow = (g_array_prototype_version != 0);
    TsValue* thisArgV = (TsValue*)thisArg;
    for (size_t i = 0; i < len0; ++i) {
        TsValue* v;
        if (slow) {
            if (!ts_array_has_property_at(this, (int64_t)i)) continue;
            v = ts_array_get_property_at(this, (int64_t)i);
        } else {
            if (IsHole(i)) continue;  // spec: skip holes
            v = GetElementBoxed(i);
        }
        if (array_generic_absent_index(this, i)) continue;  // skip absent array-like index
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        TsValue* res;
        if (thisArgV) {
            res = ts_call_with_this_3(cbVal, thisArgV, v, idx, arr);
        } else {
            // Use ts_call_with_arity to respect callback's declared parameter count
            res = ts_call_with_arity(cbVal, v, idx, arr);
        }

        // Use JavaScript truthiness, not strict boolean check
        if (ts_value_to_bool(res)) {
            if (slow) {
                // We don't know if `v` matches the raw elements slot when
                // slow mode, so push the read value itself (NaN-boxed).
                result->Push((int64_t)v);
            } else if (isSpecialized && isDouble) {
                result->PushDouble(((double*)elements)[i]);
            } else {
                result->Push(((int64_t*)elements)[i]);
            }
        }
    }
    return result;
}

void* TsArray::Reduce(void* callback, void* initialValue) {
    // Note: we do NOT special-case TsClosure here. The previous fast path
    // used ts_closure_invoke_2v which only passed (acc, val), dropping idx
    // and arr. Test262 callbacks that read idx/arr got register garbage.
    // By always taking the standard ts_call_4 path below, all 4 callback
    // arguments are delivered correctly.

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return nullptr;

    TsValue* accumulator = (TsValue*)initialValue;
    size_t startIdx = 0;
    bool slow = (g_array_prototype_version != 0);
    // Spec: capture initial length. The loop runs to initialLen regardless
    // of length mutations triggered by callbacks/getters. Per-iteration
    // HasProperty determines presence using the CURRENT state.
    size_t initialLen = (size_t)length;
    if (!accumulator && initialLen > 0) {
        if (slow) {
            while (startIdx < initialLen && !ts_array_has_property_at(this, (int64_t)startIdx)) {
                startIdx++;
            }
            if (startIdx < initialLen) {
                accumulator = ts_array_get_property_at(this, (int64_t)startIdx);
                startIdx++;
            }
        } else {
            // Skip leading holes / absent array-like indices when seeding.
            while (startIdx < initialLen &&
                   (IsHole(startIdx) || array_generic_absent_index(this, startIdx)))
                startIdx++;
            if (startIdx < initialLen) {
                accumulator = GetElementBoxed(startIdx);
                startIdx++;
            }
        }
    }
    // Spec: if the array has no present elements and no initialValue, throw.
    if (!accumulator) {
        ts_throw((TsValue*)ts_error_create_typed(
            "TypeError", "Reduce of empty array with no initial value"));
        return nullptr;
    }

    for (size_t i = startIdx; i < initialLen; ++i) {
        TsValue* v;
        if (slow) {
            if (!ts_array_has_property_at(this, (int64_t)i)) continue;
            v = ts_array_get_property_at(this, (int64_t)i);
        } else {
            // Fast path: respect current length for mid-iteration truncation.
            if (i >= (size_t)length) break;
            if (IsHole(i)) continue;  // spec: skip holes
            v = GetElementBoxed(i);
        }
        if (array_generic_absent_index(this, i)) continue;  // skip absent array-like index
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        accumulator = tsCall(cbVal, accumulator, v, idx, arr);
    }
    return accumulator;
}

void* TsArray::ReduceRight(void* callback, void* initialValue) {
    // Check if callback is a TsClosure (from HIR path)
    // Skip TsClosure fast path — same reason as Reduce above.

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) cbVal = (TsValue*)callback;  // ensure non-null for closure dispatch
    if (!cbVal) return nullptr;

    TsValue* accumulator = (TsValue*)initialValue;
    bool slow = (g_array_prototype_version != 0);
    // Spec: capture initial length at the outset.
    size_t initialLen = (size_t)length;
    size_t startIdx = initialLen;
    if (!accumulator && initialLen > 0) {
        if (slow) {
            // Find the last present element per HasProperty to seed.
            int64_t k = (int64_t)initialLen - 1;
            while (k >= 0 && !ts_array_has_property_at(this, k)) k--;
            if (k >= 0) {
                accumulator = ts_array_get_property_at(this, k);
                startIdx = (size_t)k;
            } else {
                startIdx = 0;
            }
        } else {
            // Scan from the right for the first present (non-hole / not-absent) slot.
            int64_t k = (int64_t)initialLen - 1;
            while (k >= 0 &&
                   (IsHole((size_t)k) || array_generic_absent_index(this, (size_t)k)))
                k--;
            if (k >= 0) {
                accumulator = GetElementBoxed((size_t)k);
                startIdx = (size_t)k;
            } else {
                startIdx = 0;
            }
        }
    }
    if (!accumulator) {
        ts_throw((TsValue*)ts_error_create_typed(
            "TypeError", "Reduce of empty array with no initial value"));
        return nullptr;
    }

    for (size_t i = startIdx; i > 0; --i) {
        TsValue* v;
        if (slow) {
            if (!ts_array_has_property_at(this, (int64_t)(i - 1))) continue;
            v = ts_array_get_property_at(this, (int64_t)(i - 1));
        } else {
            if ((i - 1) >= (size_t)length) continue;
            if (IsHole(i - 1)) continue;  // spec: skip holes
            v = GetElementBoxed(i - 1);
        }
        if (array_generic_absent_index(this, i - 1)) continue;  // skip absent array-like index
        TsValue* idx = ts_value_make_int(i - 1);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        accumulator = tsCall(cbVal, accumulator, v, idx, arr);
    }
    return accumulator;
}

bool TsArray::Some(void* callback, void* thisArg) {
    const size_t len0 = length;  // ES: capture length once (ignore mid-iteration growth)
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback) && g_array_prototype_version == 0 && !thisArg) {
        TsClosure* closure = (TsClosure*)callback;
        if (IsDoubleArray() || IsSmiArray()) {
            for (size_t i = 0; i < len0; ++i) {
                double elem = GetElementDouble(i);
                if (ts_closure_invoke_1d_bool(closure, elem)) return true;
            }
        } else {
            for (size_t i = 0; i < len0; ++i) {
                if (IsHole(i)) continue;  // spec: skip holes
                if (array_generic_absent_index(this, i)) continue;  // skip absent array-like index
                TsValue* v = GetElementBoxed(i);
                TsValue* idx = ts_value_make_int(i);
                TsValue* arr = ts_value_make_object(CallbackReceiver());
                TsValue* res = thisArg
                    ? ts_call_with_this_3((TsValue*)callback, (TsValue*)thisArg, v, idx, arr)
                    : tsCall((TsValue*)callback, v, idx, arr);
                if (ts_value_to_bool(res)) return true;
            }
        }
        return false;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return false;

    bool slow = (g_array_prototype_version != 0);
    TsValue* thisArgV = (TsValue*)thisArg;
    for (size_t i = 0; i < len0; ++i) {
        TsValue* v;
        if (slow) {
            if (!ts_array_has_property_at(this, (int64_t)i)) continue;
            v = ts_array_get_property_at(this, (int64_t)i);
        } else {
            if (IsHole(i)) continue;  // spec: skip holes
            v = GetElementBoxed(i);
        }
        if (array_generic_absent_index(this, i)) continue;  // skip absent array-like index
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        TsValue* res = thisArgV
            ? ts_call_with_this_3(cbVal, thisArgV, v, idx, arr)
            : tsCall(cbVal, v, idx, arr);
        if (ts_value_to_bool(res)) return true;
    }
    return false;
}

bool TsArray::Every(void* callback, void* thisArg) {
    const size_t len0 = length;  // ES: capture length once (ignore mid-iteration growth)
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback) && g_array_prototype_version == 0 && !thisArg) {
        TsClosure* closure = (TsClosure*)callback;
        if (IsDoubleArray() || IsSmiArray()) {
            for (size_t i = 0; i < len0; ++i) {
                double elem = GetElementDouble(i);
                if (!ts_closure_invoke_1d_bool(closure, elem)) return false;
            }
        } else {
            for (size_t i = 0; i < len0; ++i) {
                if (IsHole(i)) continue;  // spec: skip holes (vacuously true)
                if (array_generic_absent_index(this, i)) continue;  // skip absent array-like index
                TsValue* v = GetElementBoxed(i);
                TsValue* idx = ts_value_make_int(i);
                TsValue* arr = ts_value_make_object(CallbackReceiver());
                TsValue* res = thisArg
                    ? ts_call_with_this_3((TsValue*)callback, (TsValue*)thisArg, v, idx, arr)
                    : tsCall((TsValue*)callback, v, idx, arr);
                if (!ts_value_to_bool(res)) return false;
            }
        }
        return true;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return false;

    bool slow = (g_array_prototype_version != 0);
    TsValue* thisArgV = (TsValue*)thisArg;
    for (size_t i = 0; i < len0; ++i) {
        TsValue* v;
        if (slow) {
            if (!ts_array_has_property_at(this, (int64_t)i)) continue;
            v = ts_array_get_property_at(this, (int64_t)i);
        } else {
            if (IsHole(i)) continue;  // spec: skip holes
            v = GetElementBoxed(i);
        }
        if (array_generic_absent_index(this, i)) continue;  // skip absent array-like index
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        TsValue* res = thisArgV
            ? ts_call_with_this_3(cbVal, thisArgV, v, idx, arr)
            : tsCall(cbVal, v, idx, arr);
        if (!ts_value_to_bool(res)) return false;
    }
    return true;
}

TsValue* TsArray::Find(void* callback, void* thisArg) {
    const size_t len0 = length;  // ES: capture length once (ignore mid-iteration growth)
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback) && g_array_prototype_version == 0 && !thisArg) {
        TsClosure* closure = (TsClosure*)callback;
        if (IsDoubleArray() || IsSmiArray()) {
            for (size_t i = 0; i < len0; ++i) {
                double elem = GetElementDouble(i);
                if (ts_closure_invoke_1d_bool(closure, elem)) {
                    return ts_value_make_double(elem);
                }
            }
        } else {
            for (size_t i = 0; i < len0; ++i) {
                TsValue* v = GetElementBoxed(i);
                TsValue* idx = ts_value_make_int(i);
                TsValue* arr = ts_value_make_object(CallbackReceiver());
                TsValue* res = thisArg
                    ? ts_call_with_this_3((TsValue*)callback, (TsValue*)thisArg, v, idx, arr)
                    : tsCall((TsValue*)callback, v, idx, arr);
                if (ts_value_to_bool(res)) return GetElementBoxed(i);
            }
        }
        return ts_value_make_undefined();
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return ts_value_make_undefined();

    bool slow = (g_array_prototype_version != 0);
    TsValue* thisArgV = (TsValue*)thisArg;
    for (size_t i = 0; i < len0; ++i) {
        TsValue* v;
        if (slow) {
            // find doesn't skip holes — reads undefined for absent (per spec)
            v = ts_array_has_property_at(this, (int64_t)i)
                ? ts_array_get_property_at(this, (int64_t)i)
                : ts_value_make_undefined();
        } else {
            v = GetElementBoxed(i);
        }
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        TsValue* res = thisArgV
            ? ts_call_with_this_3(cbVal, thisArgV, v, idx, arr)
            : tsCall(cbVal, v, idx, arr);
        if (ts_value_to_bool(res)) {
            return v;
        }
    }
    return ts_value_make_undefined();
}

int64_t TsArray::FindIndex(void* callback, void* thisArg) {
    const size_t len0 = length;  // ES: capture length once (ignore mid-iteration growth)
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback) && g_array_prototype_version == 0 && !thisArg) {
        TsClosure* closure = (TsClosure*)callback;
        if (IsDoubleArray() || IsSmiArray()) {
            for (size_t i = 0; i < len0; ++i) {
                double elem = GetElementDouble(i);
                if (ts_closure_invoke_1d_bool(closure, elem)) return (int64_t)i;
            }
        } else {
            for (size_t i = 0; i < len0; ++i) {
                TsValue* v = GetElementBoxed(i);
                TsValue* idx = ts_value_make_int(i);
                TsValue* arr = ts_value_make_object(CallbackReceiver());
                TsValue* res = thisArg
                    ? ts_call_with_this_3((TsValue*)callback, (TsValue*)thisArg, v, idx, arr)
                    : tsCall((TsValue*)callback, v, idx, arr);
                if (ts_value_to_bool(res)) return (int64_t)i;
            }
        }
        return -1;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return -1;
    TsValue* thisArgV = (TsValue*)thisArg;

    for (size_t i = 0; i < len0; ++i) {
        TsValue* v = GetElementBoxed(i);
        TsValue* idx = ts_value_make_int(i);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        TsValue* res = thisArgV
            ? ts_call_with_this_3(cbVal, thisArgV, v, idx, arr)
            : tsCall(cbVal, v, idx, arr);
        if (ts_value_to_bool(res)) return (int64_t)i;
    }
    return -1;
}

TsValue* TsArray::FindLast(void* callback, void* thisArg) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback) && g_array_prototype_version == 0 && !thisArg) {
        TsClosure* closure = (TsClosure*)callback;
        if (IsDoubleArray() || IsSmiArray()) {
            for (size_t i = length; i > 0; --i) {
                size_t idx = i - 1;
                double elem = GetElementDouble(idx);
                if (ts_closure_invoke_1d_bool(closure, elem)) {
                    return ts_value_make_double(elem);
                }
            }
        } else {
            for (size_t i = length; i > 0; --i) {
                TsValue* v = GetElementBoxed(i - 1);
                TsValue* idx_v = ts_value_make_int(i - 1);
                TsValue* arr = ts_value_make_object(CallbackReceiver());
                TsValue* res = tsCall((TsValue*)callback, v, idx_v, arr);
                if (ts_value_to_bool(res)) return GetElementBoxed(i - 1);
            }
        }
        return ts_value_make_undefined();
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return ts_value_make_undefined();
    TsValue* thisArgV = (TsValue*)thisArg;

    for (size_t i = length; i > 0; --i) {
        size_t idx = i - 1;
        TsValue* v = GetElementBoxed(idx);
        TsValue* idxVal = ts_value_make_int(idx);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        TsValue* res = thisArgV
            ? ts_call_with_this_3(cbVal, thisArgV, v, idxVal, arr)
            : tsCall(cbVal, v, idxVal, arr);
        if (ts_value_to_bool(res)) {
            return GetElementBoxed(idx);
        }
    }
    return ts_value_make_undefined();
}

int64_t TsArray::FindLastIndex(void* callback, void* thisArg) {
    // Check if callback is a TsClosure (from HIR path)
    if (ts_is_closure(callback) && g_array_prototype_version == 0 && !thisArg) {
        TsClosure* closure = (TsClosure*)callback;
        if (IsDoubleArray() || IsSmiArray()) {
            for (size_t i = length; i > 0; --i) {
                size_t idx = i - 1;
                double elem = GetElementDouble(idx);
                if (ts_closure_invoke_1d_bool(closure, elem)) return (int64_t)idx;
            }
        } else {
            for (size_t i = length; i > 0; --i) {
                TsValue* v = GetElementBoxed(i - 1);
                TsValue* idx_v = ts_value_make_int(i - 1);
                TsValue* arr = ts_value_make_object(CallbackReceiver());
                TsValue* res = tsCall((TsValue*)callback, v, idx_v, arr);
                if (ts_value_to_bool(res)) return (int64_t)(i - 1);
            }
        }
        return -1;
    }

    // Standard TsValue/TsFunction path (NaN-boxed callback pointer)
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal) return -1;
    TsValue* thisArgV = (TsValue*)thisArg;

    for (size_t i = length; i > 0; --i) {
        size_t idx = i - 1;
        TsValue* v = GetElementBoxed(idx);
        TsValue* idxVal = ts_value_make_int(idx);
        TsValue* arr = ts_value_make_object(CallbackReceiver());
        TsValue* res = thisArgV
            ? ts_call_with_this_3(cbVal, thisArgV, v, idxVal, arr)
            : tsCall(cbVal, v, idxVal, arr);
        if (ts_value_to_bool(res)) return (int64_t)idx;
    }
    return -1;
}

int64_t TsArray::At(int64_t index) {
    if (index < 0) index = length + index;
    if (index < 0 || index >= (int64_t)length) return 0;
    return ((int64_t*)elements)[index];
}

void TsArray::Reverse() {
    if (length <= 1) return;
    int64_t* elems = (int64_t*)elements;
    for (size_t i = 0; i < length / 2; ++i) {
        size_t j = length - 1 - i;
        int64_t temp = elems[i];
        elems[i] = elems[j];
        elems[j] = temp;
    }
}

// ES2023 "change array by copy" methods
TsArray* TsArray::ToReversed() {
    TsArray* result;
    if (isSpecialized) {
        result = TsArray::CreateSpecialized(length, elementSize, isDouble);
        if (isDouble) {
            double* srcElems = (double*)elements;
            double* dstElems = (double*)result->elements;
            for (size_t i = 0; i < length; ++i) {
                dstElems[i] = srcElems[length - 1 - i];
            }
        } else {
            int64_t* srcElems = (int64_t*)elements;
            int64_t* dstElems = (int64_t*)result->elements;
            for (size_t i = 0; i < length; ++i) {
                dstElems[i] = srcElems[length - 1 - i];
            }
        }
    } else {
        result = TsArray::Create(length);
        int64_t* srcElems = (int64_t*)elements;
        for (size_t i = 0; i < length; ++i) {
            result->Push(srcElems[length - 1 - i]);
        }
    }
    return result;
}

TsArray* TsArray::ToSorted() {
    TsArray* result;
    if (isSpecialized) {
        result = TsArray::CreateSpecialized(length, elementSize, isDouble);
        if (isDouble) {
            double* srcElems = (double*)elements;
            double* dstElems = (double*)result->elements;
            for (size_t i = 0; i < length; ++i) {
                dstElems[i] = srcElems[i];
            }
        } else {
            int64_t* srcElems = (int64_t*)elements;
            int64_t* dstElems = (int64_t*)result->elements;
            for (size_t i = 0; i < length; ++i) {
                dstElems[i] = srcElems[i];
            }
        }
    } else {
        result = TsArray::Create(length);
        int64_t* srcElems = (int64_t*)elements;
        for (size_t i = 0; i < length; ++i) {
            result->Push(srcElems[i]);
        }
    }
    result->Sort();
    return result;
}

TsArray* TsArray::ToSpliced(int64_t start, int64_t deleteCount, void* items, int64_t itemCount) {
    // Normalize start
    if (start < 0) start = length + start;
    if (start < 0) start = 0;
    if (start > (int64_t)length) start = length;

    // Normalize deleteCount
    if (deleteCount < 0) deleteCount = 0;
    if (start + deleteCount > (int64_t)length) deleteCount = length - start;

    // Guard: items must be a valid heap pointer — not a raw small integer
    // NOR a NaN-boxed value (0xFFFE... tags observed arriving as `items`
    // under alternate lowering modes; the elements deref crashed).
    if (items && ((uint64_t)(uintptr_t)items < 0x10000 ||
                  (uint64_t)(uintptr_t)items >= 0x0000800000000000ULL)) {
        items = nullptr;
        itemCount = 0;
    }
    // Cross-check: never trust itemCount beyond the actual array length.
    if (items && ((TsArray*)items)->Length() < itemCount) {
        itemCount = ((TsArray*)items)->Length();
    }

    // Calculate new length
    size_t newLength = length - deleteCount + itemCount;

    // items is a TsArray* containing the items to insert
    TsArray* itemsArr = items ? (TsArray*)items : nullptr;

    TsArray* result;
    if (isSpecialized) {
        result = TsArray::CreateSpecialized(newLength, elementSize, isDouble);
        size_t dstIdx = 0;

        if (isDouble) {
            double* srcElems = (double*)elements;
            double* dstElems = (double*)result->elements;

            // Copy elements before start
            for (int64_t i = 0; i < start; ++i) {
                dstElems[dstIdx++] = srcElems[i];
            }
            // Insert new items - items from generic array need conversion
            for (int64_t i = 0; i < itemCount && itemsArr; ++i) {
                // Get item as int64 (stored as pointer in generic array)
                int64_t* itemElems = (int64_t*)itemsArr->GetElementsPtr();
                int64_t rawVal = itemElems[i];
                // Convert to double (rawVal is the bit pattern of the double)
                double val = *reinterpret_cast<double*>(&rawVal);
                dstElems[dstIdx++] = val;
            }
            // Copy elements after deleted section
            for (size_t i = start + deleteCount; i < length; ++i) {
                dstElems[dstIdx++] = srcElems[i];
            }
        } else {
            int64_t* srcElems = (int64_t*)elements;
            int64_t* dstElems = (int64_t*)result->elements;

            // Copy elements before start
            for (int64_t i = 0; i < start; ++i) {
                dstElems[dstIdx++] = srcElems[i];
            }
            // Insert new items
            for (int64_t i = 0; i < itemCount && itemsArr; ++i) {
                int64_t* itemElems = (int64_t*)itemsArr->GetElementsPtr();
                dstElems[dstIdx++] = itemElems[i];
            }
            // Copy elements after deleted section
            for (size_t i = start + deleteCount; i < length; ++i) {
                dstElems[dstIdx++] = srcElems[i];
            }
        }
    } else {
        result = TsArray::Create(newLength);
        int64_t* srcElems = (int64_t*)elements;

        // Copy elements before start
        for (int64_t i = 0; i < start; ++i) {
            result->Push(srcElems[i]);
        }
        // Insert new items
        for (int64_t i = 0; i < itemCount && itemsArr; ++i) {
            int64_t* itemElems = (int64_t*)itemsArr->GetElementsPtr();
            result->Push(itemElems[i]);
        }
        // Copy elements after deleted section
        for (size_t i = start + deleteCount; i < length; ++i) {
            result->Push(srcElems[i]);
        }
    }

    return result;
}

TsArray* TsArray::With(int64_t index, int64_t value) {
    // Normalize index (negative indices count from end)
    if (index < 0) index = length + index;

    // ECMA-262 23.1.3.39 Array.prototype.with step 5: a normalized index that is
    // out of range (>= len or < 0) throws a RangeError. No std::containers are live
    // in this frame, so the ts_throw longjmp unwinds cleanly.
    if (index < 0 || index >= (int64_t)length) {
        ts_throw((TsValue*)ts_error_create_typed("RangeError",
            "Invalid index : Array.prototype.with index is out of range"));
        return nullptr;  // unreachable
    }

    TsArray* result;
    if (isSpecialized) {
        result = TsArray::CreateSpecialized(length, elementSize, isDouble);
        // Copy all elements first
        std::memcpy(result->elements, elements, length * elementSize);
        ts_gc_write_barrier_range(result->elements, length * elementSize);
        // Then replace the element at index
        if (isDouble) {
            double* dstElems = (double*)result->elements;
            // value is passed as int64_t but represents a double bit pattern for specialized double arrays
            dstElems[index] = *reinterpret_cast<double*>(&value);
        } else {
            int64_t* dstElems = (int64_t*)result->elements;
            dstElems[index] = value;
        }
    } else {
        result = TsArray::Create(length);
        int64_t* srcElems = (int64_t*)elements;
        for (size_t i = 0; i < length; ++i) {
            if ((int64_t)i == index) {
                result->Push(value);
            } else {
                result->Push(srcElems[i]);
            }
        }
    }
    return result;
}

void* TsArray::Slice(int64_t start, int64_t end) {
    if (start < 0) start = length + start;
    if (start < 0) start = 0;
    if (start > (int64_t)length) start = length;

    if (end < 0) end = length + end;
    if (end > (int64_t)length) end = length;
    if (end < start) end = start;

    size_t newLength = end - start;
    TsArray* result = TsArray::Create(newLength);

    // Preserve specialized array type and element kind
    if (isSpecialized) {
        result->isSpecialized = true;
        result->isDouble = isDouble;
    }
    result->elementKind_ = elementKind_;

    for (size_t i = 0; i < newLength; ++i) {
        int64_t rawBits = ((int64_t*)elements)[start + i];
        result->Push(rawBits);
    }
    return result;
}

#include "TsString.h"
#include <sstream>

extern "C" void* ts_string_from_value(TsValue* val);

void* TsArray::Join(void* separator) {
    // ECMA-262 23.1.3.18: if separator is undefined use ","; otherwise
    // ToString(separator). The previous `(TsString*)separator` blind-cast a
    // nanboxed non-string separator (join(true)/join(null)/join(Infinity)) to a
    // TsString* and crashed in ToUtf8 (the #1 test262 crash site, ~94 tests).
    TsString* sepHolder = nullptr;
    const char* sepStr;
    if (!separator || nanbox_is_undefined((uint64_t)separator)) {
        sepStr = ",";
    } else {
        sepHolder = (TsString*)ts_string_from_value((TsValue*)separator);
        sepStr = sepHolder ? sepHolder->ToUtf8() : "";
    }

    std::stringstream ss;
    for (size_t i = 0; i < length; ++i) {
        if (i > 0) ss << sepStr;

        // ECMA-262 23.1.3.18: a hole (and undefined/null) joins as the empty
        // string. The separator was already emitted above; emit no element.
        // Without this the NANBOX_HOLE sentinel fell through to
        // ts_string_from_value and rendered as "unknown" (`[1,,3].join('-')`
        // → "1-unknown-3", `new Array(3).join()` → "unknown-unknown-...").
        if (IsHole(i)) continue;

        // Handle specialized arrays - output numeric values directly
        if (isSpecialized) {
            if (isDouble) {
                double val = ((double*)elements)[i];
                // Format to avoid trailing zeros (like JavaScript)
                if (val == (int64_t)val) {
                    ss << (int64_t)val;  // Whole number, print as int
                } else {
                    ss << val;
                }
            } else {
                ss << ((int64_t*)elements)[i];
            }
            continue;
        }

        // Generic array - elements are NaN-boxed values. Get(i) is accessor-aware
        // (invokes a per-index getter defined via Object.defineProperty); for a
        // plain array it is just the slot read (gated on `properties`).
        uint64_t nb = (uint64_t)Get(i);
        if (nanbox_is_undefined(nb)) {
            // undefined joins as empty string (JS spec)
        } else if (nanbox_is_null(nb) || nb == 0) {
            // null joins as empty string (JS spec)
        } else if (nanbox_is_int32(nb)) {
            ss << nanbox_to_int32(nb);
        } else if (nanbox_is_double(nb)) {
            double d = nanbox_to_double(nb);
            if (d == (int64_t)d && !std::isinf(d)) {
                ss << (int64_t)d;
            } else {
                ss << d;
            }
        } else if (nanbox_is_bool(nb)) {
            ss << (nanbox_to_bool(nb) ? "true" : "false");
        } else if (nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            if (!ptr) {
                // null pointer
            } else {
                uint32_t magic = *(uint32_t*)ptr;
                if (magic == TsString::MAGIC) {
                    ss << ((TsString*)ptr)->ToUtf8();
                } else if (magic == TsArray::MAGIC) {
                    // Nested array: recursively join with comma
                    TsString* sub = (TsString*)((TsArray*)ptr)->Join(nullptr);
                    if (sub) ss << sub->ToUtf8();
                } else {
                    // Other object - use ts_string_from_value
                    TsString* str = (TsString*)ts_string_from_value((TsValue*)(uintptr_t)nb);
                    if (str) ss << str->ToUtf8();
                    else ss << "[object Object]";
                }
            }
        } else {
            // Fallback: use ts_string_from_value
            TsString* str = (TsString*)ts_string_from_value((TsValue*)(uintptr_t)nb);
            if (str) ss << str->ToUtf8();
        }
    }
    return TsString::Create(ss.str().c_str());
}
