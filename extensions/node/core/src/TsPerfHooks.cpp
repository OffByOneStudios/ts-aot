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

// TsEventLoopUtilization implementation
TsEventLoopUtilization::TsEventLoopUtilization(double i, double a, double u)
    : idle(i), active(a), utilization(u) {}

// TsPerformanceObserverEntryList implementation
TsArray* TsPerformanceObserverEntryList::GetEntries() {
    TsArray* result = TsArray::Create();
    for (auto entry : entries) {
        ts_array_push(result, ts_value_make_object(entry));
    }
    return result;
}

TsArray* TsPerformanceObserverEntryList::GetEntriesByName(TsString* name, TsString* type) {
    TsArray* result = TsArray::Create();
    const char* nameStr = name ? name->ToUtf8() : nullptr;
    const char* typeStr = type ? type->ToUtf8() : nullptr;

    for (auto entry : entries) {
        bool nameMatch = !nameStr || strcmp(entry->name->ToUtf8(), nameStr) == 0;
        bool typeMatch = !typeStr || strcmp(entry->entryType->ToUtf8(), typeStr) == 0;

        if (nameMatch && typeMatch) {
            ts_array_push(result, ts_value_make_object(entry));
        }
    }
    return result;
}

TsArray* TsPerformanceObserverEntryList::GetEntriesByType(TsString* type) {
    TsArray* result = TsArray::Create();
    const char* typeStr = type ? type->ToUtf8() : nullptr;

    for (auto entry : entries) {
        if (!typeStr || strcmp(entry->entryType->ToUtf8(), typeStr) == 0) {
            ts_array_push(result, ts_value_make_object(entry));
        }
    }
    return result;
}

// TsPerformanceObserver implementation
TsPerformanceObserver* TsPerformanceObserver::Create(void* callback) {
    void* mem = ts_alloc(sizeof(TsPerformanceObserver));
    TsPerformanceObserver* observer = new (mem) TsPerformanceObserver();
    observer->callback_ = callback;
    observer->connected_ = false;
    return observer;
}

void TsPerformanceObserver::Observe(TsArray* entryTypes) {
    entryTypes_.clear();
    if (entryTypes) {
        int64_t len = ts_array_length(entryTypes);
        for (int64_t i = 0; i < len; i++) {
            TsValue* val = ts_array_get_as_value(entryTypes, i);
            void* raw = ts_value_get_string(val);
            if (raw) {
                entryTypes_.push_back((TsString*)raw);
            }
        }
    }
    connected_ = true;
    TsPerformance::GetInstance()->AddObserver(this);
}

void TsPerformanceObserver::Disconnect() {
    connected_ = false;
    TsPerformance::GetInstance()->RemoveObserver(this);
}

TsArray* TsPerformanceObserver::TakeRecords() {
    TsArray* result = TsArray::Create();
    for (auto entry : bufferedEntries_) {
        ts_array_push(result, ts_value_make_object(entry));
    }
    bufferedEntries_.clear();
    return result;
}

// TsPerformance implementation
static TsPerformance* performanceInstance = nullptr;

TsPerformance::TsPerformance() : idleTime_(0), activeTime_(0) {
    startTime_ = std::chrono::high_resolution_clock::now();
    lastUpdateTime_ = startTime_;
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
    NotifyObservers(mark);
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
    NotifyObservers(measure);
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

TsEventLoopUtilization* TsPerformance::EventLoopUtilization(TsEventLoopUtilization* util1, TsEventLoopUtilization* util2) {
    // Update timing based on elapsed time since last call
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - lastUpdateTime_).count() / 1000.0;
    lastUpdateTime_ = now;

    // Simple heuristic: assume event loop is active during this call
    // In a real implementation, we'd integrate with libuv's idle callbacks
    activeTime_ += elapsed;

    double totalIdle = idleTime_;
    double totalActive = activeTime_;

    // If comparing two ELU objects
    if (util1 && util2) {
        totalIdle = util2->idle - util1->idle;
        totalActive = util2->active - util1->active;
    } else if (util1) {
        // Compare against util1
        totalIdle = idleTime_ - util1->idle;
        totalActive = activeTime_ - util1->active;
    }

    double total = totalIdle + totalActive;
    double utilization = total > 0 ? totalActive / total : 0;

    void* mem = ts_alloc(sizeof(TsEventLoopUtilization));
    return new (mem) TsEventLoopUtilization(totalIdle, totalActive, utilization);
}

void* TsPerformance::Timerify(void* fn) {
    // timerify wraps a function to measure its execution time
    // For AOT compilation, we return the function as-is since we can't
    // dynamically wrap functions. The user would need to use mark/measure instead.
    // In a more complete implementation, this would create a proxy function.
    return fn;
}

void TsPerformance::AddObserver(TsPerformanceObserver* observer) {
    // Check if already added
    for (auto obs : observers_) {
        if (obs == observer) return;
    }
    observers_.push_back(observer);
}

void TsPerformance::RemoveObserver(TsPerformanceObserver* observer) {
    observers_.erase(
        std::remove(observers_.begin(), observers_.end(), observer),
        observers_.end()
    );
}

void TsPerformance::NotifyObservers(TsPerformanceEntry* entry) {
    for (auto observer : observers_) {
        if (!observer->connected_) continue;

        // Check if this entry type is being observed
        bool shouldNotify = observer->entryTypes_.empty();
        for (auto type : observer->entryTypes_) {
            if (strcmp(type->ToUtf8(), entry->entryType->ToUtf8()) == 0) {
                shouldNotify = true;
                break;
            }
        }

        if (shouldNotify) {
            observer->bufferedEntries_.push_back(entry);
        }
    }
}

