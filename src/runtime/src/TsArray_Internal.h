#pragma once
//==============================================================================
// TsArray_Internal.h — PRIVATE shared header for the split TsArray_*.cpp files.
// NOT the public interface (that is TsArray.h). Carries the common include set
// so a split TU is self-sufficient, plus cross-TU prototypes for the file-local
// `static` helpers that a moved method needs (filled in demand-driven from the
// compiler's missing-symbol diagnostics). TsArray has no shared mutable state or
// GC scanners, so this is a pure compiler-style class-method split.
//==============================================================================

#include "TsArray.h"
#include "TsConsString.h"
#include "TsObject.h"
#include "TsNanBox.h"
#include "TsMap.h"
#include "TsRegExp.h"
#include "TsRuntime.h"
#include "TsClosure.h"
#include "TsFlatObject.h"
#include "TsBuffer.h"
#include "TsError.h"
#include "TsPromise.h"
#include "GC.h"
#include "TsGC.h"
#include <cstring>
#include <cmath>
#include <climits>
#include <iostream>
#include <new>
#include <algorithm>

//---- Cross-TU prototypes for the TsArray_*.cpp split (demand-driven) ----------
extern "C" uint64_t g_array_prototype_version;  // defined in TsArray.cpp
// C++-linkage array-internal helpers (defined in TsArray.cpp):
bool array_generic_absent_index(const TsArray* self, size_t i);
bool ts_array_has_property_at(TsArray* arr, int64_t i);
TsValue* ts_array_get_property_at(TsArray* arr, int64_t i);
bool jsDefaultSortComparator(int64_t a, int64_t b);
