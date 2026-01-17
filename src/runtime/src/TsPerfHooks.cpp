// TsPerfHooks.cpp - Node.js perf_hooks module implementation

#include "TsPerfHooks.h"
#include "TsRuntime.h"
#include "GC.h"
#include <cstring>
#include <algorithm>

// PerformanceEntry implementation
TsPerformanceEntry::TsPerformanceEntry(TsString* n, TsString* type, double start, double dur)
    : name(n), entryType(type), startTime(start), duration(dur) {}

// PerformanceMark implementation
TsPerformanceMark::TsPerformanceMark(TsString* n, double start)
    : TsPerformanceEntry(n, TsString::Create("mark"), start, 0.0) {}

// PerformanceMeasure implementation
TsPerformanceMeasure::TsPerformanceMeasure(TsString* n, double start, double dur)
    : TsPerformanceEntry(n, TsString::Create("measure"), start, dur) {}

// TsPerformance implementation
static TsPerformance* performanceInstance = nullptr;

TsPerformance::TsPerformance() {
    startTime_ = std::chrono::high_resolution_clock::now();
    // Calculate time origin as Unix timestamp in milliseconds
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    timeOrigin = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

TsPerformance* TsPerformance::GetInstance() {
    if (!performanceInstance) {
        void* mem = ts_alloc(sizeof(TsPerformance));
        performanceInstance = new (mem) TsPerformance();
    }
    return performanceInstance;
}

double TsPerformance::Now() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - startTime_);
    return duration.count() / 1000.0; // Convert to milliseconds
}

TsPerformanceMark* TsPerformance::Mark(TsString* name, double startTime) {
    if (startTime < 0) {
        startTime = Now();
    }

    void* mem = ts_alloc(sizeof(TsPerformanceMark));
    TsPerformanceMark* mark = new (mem) TsPerformanceMark(name, startTime);
    entries_.push_back(mark);
    return mark;
}

double TsPerformance::FindMarkTime(TsString* name) {
    if (!name) return Now();

    const char* nameStr = name->ToUtf8();
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (strcmp((*it)->entryType->ToUtf8(), "mark") == 0 &&
            strcmp((*it)->name->ToUtf8(), nameStr) == 0) {
            return (*it)->startTime;
        }
    }
    return Now(); // If not found, use current time
}

TsPerformanceMeasure* TsPerformance::Measure(TsString* name, TsString* startMark, TsString* endMark) {
    double startTime = FindMarkTime(startMark);
    double endTime = endMark ? FindMarkTime(endMark) : Now();
    double duration = endTime - startTime;

    void* mem = ts_alloc(sizeof(TsPerformanceMeasure));
    TsPerformanceMeasure* measure = new (mem) TsPerformanceMeasure(name, startTime, duration);
    entries_.push_back(measure);
    return measure;
}

TsArray* TsPerformance::GetEntries() {
    TsArray* result = TsArray::Create();
    for (auto entry : entries_) {
        ts_array_push(result, ts_value_make_object(entry));
    }
    return result;
}

TsArray* TsPerformance::GetEntriesByName(TsString* name, TsString* type) {
    TsArray* result = TsArray::Create();
    const char* nameStr = name ? name->ToUtf8() : nullptr;
    const char* typeStr = type ? type->ToUtf8() : nullptr;

    for (auto entry : entries_) {
        bool nameMatch = !nameStr || strcmp(entry->name->ToUtf8(), nameStr) == 0;
        bool typeMatch = !typeStr || strcmp(entry->entryType->ToUtf8(), typeStr) == 0;

        if (nameMatch && typeMatch) {
            ts_array_push(result, ts_value_make_object(entry));
        }
    }
    return result;
}

TsArray* TsPerformance::GetEntriesByType(TsString* type) {
    TsArray* result = TsArray::Create();
    const char* typeStr = type ? type->ToUtf8() : nullptr;

    for (auto entry : entries_) {
        if (!typeStr || strcmp(entry->entryType->ToUtf8(), typeStr) == 0) {
            ts_array_push(result, ts_value_make_object(entry));
        }
    }
    return result;
}

void TsPerformance::ClearMarks(TsString* name) {
    const char* nameStr = name ? name->ToUtf8() : nullptr;

    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
            [nameStr](TsPerformanceEntry* entry) {
                if (strcmp(entry->entryType->ToUtf8(), "mark") != 0) return false;
                if (!nameStr) return true;
                return strcmp(entry->name->ToUtf8(), nameStr) == 0;
            }),
        entries_.end()
    );
}

void TsPerformance::ClearMeasures(TsString* name) {
    const char* nameStr = name ? name->ToUtf8() : nullptr;

    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
            [nameStr](TsPerformanceEntry* entry) {
                if (strcmp(entry->entryType->ToUtf8(), "measure") != 0) return false;
                if (!nameStr) return true;
                return strcmp(entry->name->ToUtf8(), nameStr) == 0;
            }),
        entries_.end()
    );
}

// C API implementation
extern "C" {

double ts_performance_now() {
    return TsPerformance::GetInstance()->Now();
}

double ts_performance_time_origin() {
    return TsPerformance::GetInstance()->timeOrigin;
}

void* ts_performance_mark(void* nameArg, double startTime) {
    TsString* name = nullptr;
    if (nameArg) {
        // Use ts_value_get_string for string extraction
        void* raw = ts_value_get_string((TsValue*)nameArg);
        if (raw) {
            name = (TsString*)raw;
        } else {
            // Fallback: might be a raw TsString*
            name = (TsString*)nameArg;
        }
    }

    if (!name) {
        name = TsString::Create("unnamed");
    }

    TsPerformanceMark* mark = TsPerformance::GetInstance()->Mark(name, startTime);
    return ts_value_make_object(mark);
}

void* ts_performance_measure(void* nameArg, void* startMarkArg, void* endMarkArg) {
    auto extractString = [](void* arg) -> TsString* {
        if (!arg) return nullptr;
        // Use ts_value_get_string for string extraction
        void* raw = ts_value_get_string((TsValue*)arg);
        if (raw) {
            return (TsString*)raw;
        }
        // Fallback: might be a raw TsString*
        return (TsString*)arg;
    };

    TsString* name = extractString(nameArg);
    TsString* startMark = extractString(startMarkArg);
    TsString* endMark = extractString(endMarkArg);

    if (!name) {
        name = TsString::Create("unnamed");
    }

    TsPerformanceMeasure* measure = TsPerformance::GetInstance()->Measure(name, startMark, endMark);
    return ts_value_make_object(measure);
}

void* ts_performance_get_entries() {
    TsArray* entries = TsPerformance::GetInstance()->GetEntries();
    return ts_value_make_object(entries);
}

void* ts_performance_get_entries_by_name(void* nameArg, void* typeArg) {
    auto extractString = [](void* arg) -> TsString* {
        if (!arg) return nullptr;
        void* raw = ts_value_get_string((TsValue*)arg);
        if (raw) {
            return (TsString*)raw;
        }
        return (TsString*)arg;
    };

    TsString* name = extractString(nameArg);
    TsString* type = extractString(typeArg);

    TsArray* entries = TsPerformance::GetInstance()->GetEntriesByName(name, type);
    return ts_value_make_object(entries);
}

void* ts_performance_get_entries_by_type(void* typeArg) {
    TsString* type = nullptr;
    if (typeArg) {
        void* raw = ts_value_get_string((TsValue*)typeArg);
        if (raw) {
            type = (TsString*)raw;
        } else {
            type = (TsString*)typeArg;
        }
    }

    TsArray* entries = TsPerformance::GetInstance()->GetEntriesByType(type);
    return ts_value_make_object(entries);
}

void ts_performance_clear_marks(void* nameArg) {
    TsString* name = nullptr;
    if (nameArg) {
        void* raw = ts_value_get_string((TsValue*)nameArg);
        if (raw) {
            name = (TsString*)raw;
        } else {
            name = (TsString*)nameArg;
        }
    }
    TsPerformance::GetInstance()->ClearMarks(name);
}

void ts_performance_clear_measures(void* nameArg) {
    TsString* name = nullptr;
    if (nameArg) {
        void* raw = ts_value_get_string((TsValue*)nameArg);
        if (raw) {
            name = (TsString*)raw;
        } else {
            name = (TsString*)nameArg;
        }
    }
    TsPerformance::GetInstance()->ClearMeasures(name);
}

void* ts_performance_entry_get_name(void* entryArg) {
    if (!entryArg) return ts_value_make_string(TsString::Create(""));

    void* raw = ts_value_get_object((TsValue*)entryArg);
    if (!raw) raw = entryArg;

    TsPerformanceEntry* entry = dynamic_cast<TsPerformanceEntry*>((TsObject*)raw);
    if (!entry) return ts_value_make_string(TsString::Create(""));

    return ts_value_make_string(entry->name);
}

void* ts_performance_entry_get_entry_type(void* entryArg) {
    if (!entryArg) return ts_value_make_string(TsString::Create(""));

    void* raw = ts_value_get_object((TsValue*)entryArg);
    if (!raw) raw = entryArg;

    TsPerformanceEntry* entry = dynamic_cast<TsPerformanceEntry*>((TsObject*)raw);
    if (!entry) return ts_value_make_string(TsString::Create(""));

    return ts_value_make_string(entry->entryType);
}

double ts_performance_entry_get_start_time(void* entryArg) {
    if (!entryArg) return 0.0;

    void* raw = ts_value_get_object((TsValue*)entryArg);
    if (!raw) raw = entryArg;

    TsPerformanceEntry* entry = dynamic_cast<TsPerformanceEntry*>((TsObject*)raw);
    if (!entry) return 0.0;

    return entry->startTime;
}

double ts_performance_entry_get_duration(void* entryArg) {
    if (!entryArg) return 0.0;

    void* raw = ts_value_get_object((TsValue*)entryArg);
    if (!raw) raw = entryArg;

    TsPerformanceEntry* entry = dynamic_cast<TsPerformanceEntry*>((TsObject*)raw);
    if (!entry) return 0.0;

    return entry->duration;
}

} // extern "C"
