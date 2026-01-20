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

void* ts_performance_event_loop_utilization(void* util1Arg, void* util2Arg) {
    TsEventLoopUtilization* util1 = nullptr;
    TsEventLoopUtilization* util2 = nullptr;

    if (util1Arg) {
        void* raw = ts_value_get_object((TsValue*)util1Arg);
        if (!raw) raw = util1Arg;
        util1 = dynamic_cast<TsEventLoopUtilization*>((TsObject*)raw);
    }

    if (util2Arg) {
        void* raw = ts_value_get_object((TsValue*)util2Arg);
        if (!raw) raw = util2Arg;
        util2 = dynamic_cast<TsEventLoopUtilization*>((TsObject*)raw);
    }

    TsEventLoopUtilization* result = TsPerformance::GetInstance()->EventLoopUtilization(util1, util2);
    return ts_value_make_object(result);
}

void* ts_performance_timerify(void* fn) {
    return TsPerformance::GetInstance()->Timerify(fn);
}

// EventLoopUtilization property getters
double ts_elu_get_idle(void* eluArg) {
    if (!eluArg) return 0.0;

    void* raw = ts_value_get_object((TsValue*)eluArg);
    if (!raw) raw = eluArg;

    TsEventLoopUtilization* elu = dynamic_cast<TsEventLoopUtilization*>((TsObject*)raw);
    if (!elu) return 0.0;

    return elu->idle;
}

double ts_elu_get_active(void* eluArg) {
    if (!eluArg) return 0.0;

    void* raw = ts_value_get_object((TsValue*)eluArg);
    if (!raw) raw = eluArg;

    TsEventLoopUtilization* elu = dynamic_cast<TsEventLoopUtilization*>((TsObject*)raw);
    if (!elu) return 0.0;

    return elu->active;
}

double ts_elu_get_utilization(void* eluArg) {
    if (!eluArg) return 0.0;

    void* raw = ts_value_get_object((TsValue*)eluArg);
    if (!raw) raw = eluArg;

    TsEventLoopUtilization* elu = dynamic_cast<TsEventLoopUtilization*>((TsObject*)raw);
    if (!elu) return 0.0;

    return elu->utilization;
}

// PerformanceObserver C API
void* ts_performance_observer_create(void* callback) {
    return ts_value_make_object(TsPerformanceObserver::Create(callback));
}

void ts_performance_observer_observe(void* observerArg, void* optionsArg) {
    if (!observerArg) return;

    void* raw = ts_value_get_object((TsValue*)observerArg);
    if (!raw) raw = observerArg;

    TsPerformanceObserver* observer = dynamic_cast<TsPerformanceObserver*>((TsObject*)raw);
    if (!observer) return;

    // Extract entryTypes from options
    TsArray* entryTypes = nullptr;
    if (optionsArg) {
        void* rawOpts = ts_value_get_object((TsValue*)optionsArg);
        if (!rawOpts) rawOpts = optionsArg;

        // Try to get entryTypes property from options object
        TsObject* optsObj = (TsObject*)rawOpts;
        if (optsObj) {
            void* entryTypesVal = ts_object_get_property(optsObj, "entryTypes");
            if (entryTypesVal) {
                void* rawArr = ts_value_get_object((TsValue*)entryTypesVal);
                if (rawArr) {
                    entryTypes = dynamic_cast<TsArray*>((TsObject*)rawArr);
                }
            }
        }
    }

    observer->Observe(entryTypes);
}

void ts_performance_observer_disconnect(void* observerArg) {
    if (!observerArg) return;

    void* raw = ts_value_get_object((TsValue*)observerArg);
    if (!raw) raw = observerArg;

    TsPerformanceObserver* observer = dynamic_cast<TsPerformanceObserver*>((TsObject*)raw);
    if (!observer) return;

    observer->Disconnect();
}

void* ts_performance_observer_take_records(void* observerArg) {
    if (!observerArg) return ts_value_make_object(TsArray::Create());

    void* raw = ts_value_get_object((TsValue*)observerArg);
    if (!raw) raw = observerArg;

    TsPerformanceObserver* observer = dynamic_cast<TsPerformanceObserver*>((TsObject*)raw);
    if (!observer) return ts_value_make_object(TsArray::Create());

    return ts_value_make_object(observer->TakeRecords());
}

// PerformanceObserverEntryList C API
void* ts_performance_observer_entry_list_get_entries(void* listArg) {
    if (!listArg) return ts_value_make_object(TsArray::Create());

    void* raw = ts_value_get_object((TsValue*)listArg);
    if (!raw) raw = listArg;

    TsPerformanceObserverEntryList* list = dynamic_cast<TsPerformanceObserverEntryList*>((TsObject*)raw);
    if (!list) return ts_value_make_object(TsArray::Create());

    return ts_value_make_object(list->GetEntries());
}

void* ts_performance_observer_entry_list_get_entries_by_name(void* listArg, void* nameArg, void* typeArg) {
    if (!listArg) return ts_value_make_object(TsArray::Create());

    void* raw = ts_value_get_object((TsValue*)listArg);
    if (!raw) raw = listArg;

    TsPerformanceObserverEntryList* list = dynamic_cast<TsPerformanceObserverEntryList*>((TsObject*)raw);
    if (!list) return ts_value_make_object(TsArray::Create());

    auto extractString = [](void* arg) -> TsString* {
        if (!arg) return nullptr;
        void* strRaw = ts_value_get_string((TsValue*)arg);
        if (strRaw) return (TsString*)strRaw;
        return (TsString*)arg;
    };

    TsString* name = extractString(nameArg);
    TsString* type = extractString(typeArg);

    return ts_value_make_object(list->GetEntriesByName(name, type));
}

void* ts_performance_observer_entry_list_get_entries_by_type(void* listArg, void* typeArg) {
    if (!listArg) return ts_value_make_object(TsArray::Create());

    void* raw = ts_value_get_object((TsValue*)listArg);
    if (!raw) raw = listArg;

    TsPerformanceObserverEntryList* list = dynamic_cast<TsPerformanceObserverEntryList*>((TsObject*)raw);
    if (!list) return ts_value_make_object(TsArray::Create());

    TsString* type = nullptr;
    if (typeArg) {
        void* strRaw = ts_value_get_string((TsValue*)typeArg);
        if (strRaw) type = (TsString*)strRaw;
        else type = (TsString*)typeArg;
    }

    return ts_value_make_object(list->GetEntriesByType(type));
}

} // extern "C"
