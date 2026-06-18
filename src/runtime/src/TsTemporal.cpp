// Temporal API (TC39) runtime implementation. Self-contained library.
// See include/TsTemporal.h. The Temporal namespace + per-type constructors and
// prototypes are registered in TsGlobals.cpp (ts_get_global_Temporal); this
// file holds the per-type C++ instance classes and their value logic.
#include "TsTemporal.h"
#include "TsString.h"
#include "TsNanBox.h"
#include "TsRuntime.h"
#include "TsError.h"
#include "GC.h"
#include <new>

TsPlainTime* TsPlainTime::Create(int h, int m, int s, int ms, int us, int ns) {
    void* mem = ts_alloc(sizeof(TsPlainTime));
    TsPlainTime* pt = new (mem) TsPlainTime();
    pt->magic = MAGIC;
    pt->iso_hour = h;
    pt->iso_minute = m;
    pt->iso_second = s;
    pt->iso_millisecond = ms;
    pt->iso_microsecond = us;
    pt->iso_nanosecond = ns;
    return pt;
}
