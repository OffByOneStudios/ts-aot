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

// EventLoopUtilization object - returned by performance.eventLoopUtilization()
class TsEventLoopUtilization : public TsObject {
public:
    double idle;        // Time spent idle in ms
    double active;      // Time spent active in ms
    double utilization; // active / (idle + active)

    TsEventLoopUtilization(double idle, double active, double utilization);
};

// PerformanceObserver - observes performance entries
class TsPerformanceObserver;

// PerformanceObserverEntryList - list of observed entries
class TsPerformanceObserverEntryList : public TsObject {
public:
    std::vector<TsPerformanceEntry*> entries;

    TsArray* GetEntries();
    TsArray* GetEntriesByName(TsString* name, TsString* type = nullptr);
    TsArray* GetEntriesByType(TsString* type);
};

// PerformanceObserver callback type
typedef void (*PerformanceObserverCallback)(TsPerformanceObserverEntryList* list, TsPerformanceObserver* observer);

// PerformanceObserver - observes performance entries
class TsPerformanceObserver : public TsObject {
public:
    static TsPerformanceObserver* Create(void* callback);

    void Observe(TsArray* entryTypes);
    void Disconnect();
    TsArray* TakeRecords();

    void* callback_;
    std::vector<TsString*> entryTypes_;
    std::vector<TsPerformanceEntry*> bufferedEntries_;
    bool connected_;
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
    TsEventLoopUtilization* EventLoopUtilization(TsEventLoopUtilization* util1 = nullptr, TsEventLoopUtilization* util2 = nullptr);
    void* Timerify(void* fn);
    double timeOrigin;

    // Observer management
    void AddObserver(TsPerformanceObserver* observer);
    void RemoveObserver(TsPerformanceObserver* observer);
    void NotifyObservers(TsPerformanceEntry* entry);

private:
    TsPerformance();
    std::vector<TsPerformanceEntry*> entries_;
    std::vector<TsPerformanceObserver*> observers_;
    std::chrono::high_resolution_clock::time_point startTime_;

    // Event loop timing
    double idleTime_;
    double activeTime_;
    std::chrono::high_resolution_clock::time_point lastUpdateTime_;

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
    void* ts_performance_event_loop_utilization(void* util1, void* util2);
    void* ts_performance_timerify(void* fn);

    // PerformanceEntry properties
    void* ts_performance_entry_get_name(void* entry);
    void* ts_performance_entry_get_entry_type(void* entry);
    double ts_performance_entry_get_start_time(void* entry);
    double ts_performance_entry_get_duration(void* entry);

    // EventLoopUtilization properties
    double ts_elu_get_idle(void* elu);
    double ts_elu_get_active(void* elu);
    double ts_elu_get_utilization(void* elu);

    // PerformanceObserver
    void* ts_performance_observer_create(void* callback);
    void ts_performance_observer_observe(void* observer, void* options);
    void ts_performance_observer_disconnect(void* observer);
    void* ts_performance_observer_take_records(void* observer);

    // PerformanceObserverEntryList
    void* ts_performance_observer_entry_list_get_entries(void* list);
    void* ts_performance_observer_entry_list_get_entries_by_name(void* list, void* name, void* type);
    void* ts_performance_observer_entry_list_get_entries_by_type(void* list, void* type);
}

#endif // TS_PERF_HOOKS_H
