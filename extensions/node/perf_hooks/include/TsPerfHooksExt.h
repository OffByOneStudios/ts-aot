#ifndef TS_PERF_HOOKS_EXT_H
#define TS_PERF_HOOKS_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

// Performance module functions
double ts_performance_now();
double ts_performance_time_origin();
void* ts_performance_mark(void* name, double startTime);
void* ts_performance_measure(void* name, void* startMark, void* endMark);
void* ts_performance_get_entries();
void* ts_performance_get_entries_by_name(void* name, void* type);
void* ts_performance_get_entries_by_type(void* type);
void ts_performance_clear_marks(void* name);
void ts_performance_clear_measures(void* name);

// PerformanceEntry property getters
void* ts_performance_entry_get_name(void* entry);
void* ts_performance_entry_get_entry_type(void* entry);
double ts_performance_entry_get_start_time(void* entry);
double ts_performance_entry_get_duration(void* entry);

// Event loop utilization
void* ts_performance_event_loop_utilization(void* util1, void* util2);
void* ts_performance_timerify(void* fn);
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

#ifdef __cplusplus
}
#endif

#endif // TS_PERF_HOOKS_EXT_H
