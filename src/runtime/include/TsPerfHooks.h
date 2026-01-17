// TsPerfHooks.h - Node.js perf_hooks module implementation

#ifndef TS_PERF_HOOKS_H
#define TS_PERF_HOOKS_H

#include "TsObject.h"
#include "TsString.h"
#include "TsArray.h"
#include <vector>
#include <chrono>

// PerformanceEntry base class
class TsPerformanceEntry : public TsObject {
public:
    TsString* name;
    TsString* entryType;
    double startTime;
    double duration;

    TsPerformanceEntry(TsString* name, TsString* type, double start, double dur);
};

// PerformanceMark - duration is always 0
class TsPerformanceMark : public TsPerformanceEntry {
public:
    TsPerformanceMark(TsString* name, double startTime);
};

// PerformanceMeasure - measures time between marks
class TsPerformanceMeasure : public TsPerformanceEntry {
public:
    TsPerformanceMeasure(TsString* name, double startTime, double duration);
};

// Performance object (singleton-like behavior)
class TsPerformance : public TsObject {
public:
    static TsPerformance* GetInstance();

    double Now();
    TsPerformanceMark* Mark(TsString* name, double startTime = -1);
    TsPerformanceMeasure* Measure(TsString* name, TsString* startMark, TsString* endMark);
    TsArray* GetEntries();
    TsArray* GetEntriesByName(TsString* name, TsString* type = nullptr);
    TsArray* GetEntriesByType(TsString* type);
    void ClearMarks(TsString* name = nullptr);
    void ClearMeasures(TsString* name = nullptr);
    double timeOrigin;

private:
    TsPerformance();
    std::vector<TsPerformanceEntry*> entries_;
    std::chrono::high_resolution_clock::time_point startTime_;

    double FindMarkTime(TsString* name);
};

extern "C" {
    // Performance methods
    double ts_performance_now();
    double ts_performance_time_origin();
    void* ts_performance_mark(void* name, double startTime);
    void* ts_performance_measure(void* name, void* startMark, void* endMark);
    void* ts_performance_get_entries();
    void* ts_performance_get_entries_by_name(void* name, void* type);
    void* ts_performance_get_entries_by_type(void* type);
    void ts_performance_clear_marks(void* name);
    void ts_performance_clear_measures(void* name);

    // PerformanceEntry properties
    void* ts_performance_entry_get_name(void* entry);
    void* ts_performance_entry_get_entry_type(void* entry);
    double ts_performance_entry_get_start_time(void* entry);
    double ts_performance_entry_get_duration(void* entry);
}

#endif // TS_PERF_HOOKS_H
