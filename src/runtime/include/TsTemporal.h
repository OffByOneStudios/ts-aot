#pragma once
// Temporal API runtime types (TC39 Temporal proposal). Implemented as a
// self-contained library: per-type C++ classes (TsObject subclasses, magic at
// offset 16, dynamic_cast-able) plus the Temporal namespace registered in
// TsGlobals.cpp via ts_get_global_Temporal().
#include "TsObject.h"
#include <cstdint>

// Temporal.PlainTime — a wall-clock time (hour/minute/second + sub-second), no
// date, no time zone, no calendar. ISO field ranges: hour 0-23, minute 0-59,
// second 0-59, millisecond/microsecond/nanosecond 0-999.
class TsPlainTime : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x504C5449; // 'PLTI'
    int iso_hour = 0;
    int iso_minute = 0;
    int iso_second = 0;
    int iso_millisecond = 0;
    int iso_microsecond = 0;
    int iso_nanosecond = 0;

    // Caller must have already range-validated the fields (RegulateTime).
    static TsPlainTime* Create(int h, int m, int s, int ms, int us, int ns);

    // Instance property access (pt.hour, pt.minute, ...). Reached from
    // ts_object_get_property via the magic16 whitelist.
    TsValue GetPropertyVirtual(const char* key) override;
};

extern "C" {
    // The Temporal namespace object (globalThis.Temporal). Cached.
    void* ts_get_global_Temporal();
    // new Temporal.PlainTime(h,m,s,ms,us,ns): ToIntegerWithTruncation each arg
    // (missing -> 0), RejectTime range-check, then Create. Boxed object result.
    TsValue* ts_temporal_plaintime_construct(int argc, TsValue** argv);
    // The cached PlainTime constructor function (for the new-dispatch match).
    void* ts_temporal_get_plaintime_ctor();
}
