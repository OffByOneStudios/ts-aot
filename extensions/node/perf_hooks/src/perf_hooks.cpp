// Perf hooks module extension - extern "C" wrappers
// Class implementations stay in tsruntime (TsPerformance, TsPerformanceEntry, etc.)

#include "TsPerfHooksExt.h"
#include "TsPerfHooks.h"
#include "TsRuntime.h"
#include "TsString.h"
#include "TsArray.h"
#include "GC.h"

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

    void* raw = ts_nanbox_safe_unbox(entryArg);

    TsPerformanceEntry* entry = dynamic_cast<TsPerformanceEntry*>((TsObject*)raw);
    if (!entry) return ts_value_make_string(TsString::Create(""));

    return ts_value_make_string(entry->name);
}

void* ts_performance_entry_get_entry_type(void* entryArg) {
    if (!entryArg) return ts_value_make_string(TsString::Create(""));

    void* raw = ts_nanbox_safe_unbox(entryArg);

    TsPerformanceEntry* entry = dynamic_cast<TsPerformanceEntry*>((TsObject*)raw);
    if (!entry) return ts_value_make_string(TsString::Create(""));

    return ts_value_make_string(entry->entryType);
}

double ts_performance_entry_get_start_time(void* entryArg) {
    if (!entryArg) return 0.0;

    void* raw = ts_nanbox_safe_unbox(entryArg);

    TsPerformanceEntry* entry = dynamic_cast<TsPerformanceEntry*>((TsObject*)raw);
    if (!entry) return 0.0;

    return entry->startTime;
}

double ts_performance_entry_get_duration(void* entryArg) {
    if (!entryArg) return 0.0;

    void* raw = ts_nanbox_safe_unbox(entryArg);

    TsPerformanceEntry* entry = dynamic_cast<TsPerformanceEntry*>((TsObject*)raw);
    if (!entry) return 0.0;

    return entry->duration;
}

void* ts_performance_event_loop_utilization(void* util1Arg, void* util2Arg) {
    TsEventLoopUtilization* util1 = nullptr;
    TsEventLoopUtilization* util2 = nullptr;

    if (util1Arg) {
        void* raw = ts_nanbox_safe_unbox(util1Arg);
        util1 = dynamic_cast<TsEventLoopUtilization*>((TsObject*)raw);
    }

    if (util2Arg) {
        void* raw = ts_nanbox_safe_unbox(util2Arg);
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

    void* raw = ts_nanbox_safe_unbox(eluArg);

    TsEventLoopUtilization* elu = dynamic_cast<TsEventLoopUtilization*>((TsObject*)raw);
    if (!elu) return 0.0;

    return elu->idle;
}

double ts_elu_get_active(void* eluArg) {
    if (!eluArg) return 0.0;

    void* raw = ts_nanbox_safe_unbox(eluArg);

    TsEventLoopUtilization* elu = dynamic_cast<TsEventLoopUtilization*>((TsObject*)raw);
    if (!elu) return 0.0;

    return elu->active;
}

double ts_elu_get_utilization(void* eluArg) {
    if (!eluArg) return 0.0;

    void* raw = ts_nanbox_safe_unbox(eluArg);

    TsEventLoopUtilization* elu = dynamic_cast<TsEventLoopUtilization*>((TsObject*)raw);
    if (!elu) return 0.0;

    return elu->utilization;
}

// PerformanceObserver C API
void* ts_performance_observer_create(void* callback) {
    return TsPerformanceObserver::Create(callback);
}

void ts_performance_observer_observe(void* observerArg, void* optionsArg) {
    if (!observerArg) return;

    // Self pointer comes from auto-prepend - always raw TsPerformanceObserver*
    TsPerformanceObserver* observer = (TsPerformanceObserver*)observerArg;

    // Extract entryTypes from options
    TsArray* entryTypes = nullptr;
    if (optionsArg) {
        void* rawOpts = ts_nanbox_safe_unbox(optionsArg);

        // Try to get entryTypes property from options object
        TsObject* optsObj = (TsObject*)rawOpts;
        if (optsObj) {
            void* entryTypesVal = ts_object_get_property(optsObj, "entryTypes");
            if (entryTypesVal) {
                void* rawArr = ts_nanbox_safe_unbox(entryTypesVal);
                entryTypes = (TsArray*)rawArr;
            }
        }
    }

    observer->Observe(entryTypes);
}

void ts_performance_observer_disconnect(void* observerArg) {
    if (!observerArg) return;

    // Self pointer comes from auto-prepend - always raw TsPerformanceObserver*
    TsPerformanceObserver* observer = (TsPerformanceObserver*)observerArg;
    observer->Disconnect();
}

void* ts_performance_observer_take_records(void* observerArg) {
    if (!observerArg) return ts_value_make_object(TsArray::Create());

    // Self pointer comes from auto-prepend - always raw TsPerformanceObserver*
    TsPerformanceObserver* observer = (TsPerformanceObserver*)observerArg;
    return ts_value_make_object(observer->TakeRecords());
}

// PerformanceObserverEntryList C API
void* ts_performance_observer_entry_list_get_entries(void* listArg) {
    if (!listArg) return ts_value_make_object(TsArray::Create());

    void* raw = ts_nanbox_safe_unbox(listArg);

    TsPerformanceObserverEntryList* list = dynamic_cast<TsPerformanceObserverEntryList*>((TsObject*)raw);
    if (!list) return ts_value_make_object(TsArray::Create());

    return ts_value_make_object(list->GetEntries());
}

void* ts_performance_observer_entry_list_get_entries_by_name(void* listArg, void* nameArg, void* typeArg) {
    if (!listArg) return ts_value_make_object(TsArray::Create());

    void* raw = ts_nanbox_safe_unbox(listArg);

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

    void* raw = ts_nanbox_safe_unbox(listArg);

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
