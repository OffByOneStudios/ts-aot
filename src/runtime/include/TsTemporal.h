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

// Temporal.Duration — a length of time as ten signed integer components. All
// components share one sign (mixed signs are a RangeError). No calendar/zone.
class TsDuration : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x54445552; // 'TDUR'
    int64_t years = 0, months = 0, weeks = 0, days = 0, hours = 0;
    int64_t minutes = 0, seconds = 0, milliseconds = 0, microseconds = 0, nanoseconds = 0;

    static TsDuration* Create(int64_t y, int64_t mo, int64_t w, int64_t d, int64_t h,
                              int64_t mi, int64_t s, int64_t ms, int64_t us, int64_t ns);
    int Sign() const;  // -1 | 0 | 1 (first non-zero component's sign)
    TsValue GetPropertyVirtual(const char* key) override;
};

// Temporal.PlainDate — a calendar date (ISO-8601 calendar), no time/zone.
class TsPlainDate : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x504C4454; // 'PLDT'
    int iso_year = 0;
    int iso_month = 1;  // 1-12
    int iso_day = 1;    // 1-31
    static TsPlainDate* Create(int y, int m, int d);
    TsValue GetPropertyVirtual(const char* key) override;
};

// Temporal.PlainYearMonth — a calendar year+month (ISO), with a reference day.
class TsPlainYearMonth : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x504C594D; // 'PLYM'
    int iso_year = 0; int iso_month = 1; int iso_day = 1;  // ref day (default 1)
    static TsPlainYearMonth* Create(int y, int m, int refDay);
    TsValue GetPropertyVirtual(const char* key) override;
};

// Temporal.PlainMonthDay — a calendar month+day (ISO), with a reference year.
class TsPlainMonthDay : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x504C4D44; // 'PLMD'
    int iso_year = 1972; int iso_month = 1; int iso_day = 1;  // ref year (leap)
    static TsPlainMonthDay* Create(int m, int d, int refYear);
    TsValue GetPropertyVirtual(const char* key) override;
};

// Temporal.PlainDateTime — a calendar date + wall-clock time (ISO), no zone.
class TsPlainDateTime : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x50444D54; // 'PDMT'
    int iso_year=0, iso_month=1, iso_day=1;
    int iso_hour=0, iso_minute=0, iso_second=0, iso_ms=0, iso_us=0, iso_ns=0;
    static TsPlainDateTime* Create(int y,int mo,int d,int h,int mi,int s,int ms,int us,int ns);
    TsValue GetPropertyVirtual(const char* key) override;
};

// Temporal.Instant — an exact point in time, epoch nanoseconds. Stored as
// truncated int64 milliseconds + int sub-millisecond nanoseconds (same sign),
// so epochNanoseconds (BigInt-range) is built from a decimal string.
class TsInstant : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x494E5354; // 'INST'
    int64_t epoch_ms = 0;
    int sub_ns = 0;  // -999999..999999, same sign as epoch_ms
    static TsInstant* Create(int64_t ms, int subNs);
    TsValue GetPropertyVirtual(const char* key) override;
};

// Temporal.ZonedDateTime — an Instant + a time zone (ISO calendar). This
// implementation supports the "UTC" zone and fixed numeric offsets (+HH:MM);
// named IANA zones (with DST) require a timezone database (not yet present).
class TsZonedDateTime : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x5A44544D; // 'ZDTM'
    int64_t epoch_ms = 0;
    int sub_ns = 0;
    int offset_minutes = 0;  // fixed offset from UTC (used when zone_name is empty)
    bool is_utc = true;      // "UTC" named zone vs a numeric offset zone
    char zone_name[40] = {0};// named IANA zone ("America/New_York"); empty => fixed offset
    static TsZonedDateTime* Create(int64_t ms, int subNs, int offMin, bool utc);
    static TsZonedDateTime* CreateNamed(int64_t ms, int subNs, const char* zone);
    TsValue GetPropertyVirtual(const char* key) override;
};

extern "C" {
    // The Temporal namespace object (globalThis.Temporal). Cached.
    void* ts_get_global_Temporal();
    TsValue* ts_temporal_duration_construct(int argc, TsValue** argv);
    void* ts_temporal_get_duration_ctor();
    TsValue* ts_temporal_zoneddatetime_construct(int argc, TsValue** argv);
    void* ts_temporal_get_zoneddatetime_ctor();
    TsValue* ts_temporal_instant_construct(int argc, TsValue** argv);
    void* ts_temporal_get_instant_ctor();
    TsValue* ts_temporal_plaindatetime_construct(int argc, TsValue** argv);
    void* ts_temporal_get_plaindatetime_ctor();
    TsValue* ts_temporal_plaindate_construct(int argc, TsValue** argv);
    void* ts_temporal_get_plaindate_ctor();
    TsValue* ts_temporal_plainyearmonth_construct(int argc, TsValue** argv);
    void* ts_temporal_get_plainyearmonth_ctor();
    TsValue* ts_temporal_plainmonthday_construct(int argc, TsValue** argv);
    void* ts_temporal_get_plainmonthday_ctor();
    // new Temporal.PlainTime(h,m,s,ms,us,ns): ToIntegerWithTruncation each arg
    // (missing -> 0), RejectTime range-check, then Create. Boxed object result.
    TsValue* ts_temporal_plaintime_construct(int argc, TsValue** argv);
    // The cached PlainTime constructor function (for the new-dispatch match).
    void* ts_temporal_get_plaintime_ctor();
}
