#ifndef TS_EVENTS_EXT_H
#define TS_EVENTS_EXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// EventEmitter constructor
void* ts_event_emitter_create();

// Instance methods
void ts_event_emitter_on(void* emitter, void* event, void* callback);
void ts_event_emitter_once(void* emitter, void* event, void* callback);
void ts_event_emitter_prepend_listener(void* emitter, void* event, void* callback);
void ts_event_emitter_prepend_once_listener(void* emitter, void* event, void* callback);
void ts_event_emitter_remove_listener(void* emitter, void* event, void* callback);
void ts_event_emitter_remove_all_listeners(void* emitter, void* event);
void ts_event_emitter_set_max_listeners(void* emitter, int n);
int ts_event_emitter_get_max_listeners(void* emitter);
int ts_event_emitter_listener_count(void* emitter, void* event);
void* ts_event_emitter_listeners(void* emitter, void* event);
void* ts_event_emitter_raw_listeners(void* emitter, void* event);
bool ts_event_emitter_emit(void* emitter, void* event, int argc, void** argv);
void* ts_event_emitter_event_names(void* emitter);

// Static module methods (events.once, events.on)
void* ts_event_emitter_static_once(void* emitter, void* event);
void* ts_event_emitter_static_on(void* emitter, void* event);

// Deprecated static method: EventEmitter.listenerCount(emitter, eventName)
int64_t ts_event_emitter_static_listener_count(void* emitter, void* event);

#ifdef __cplusplus
}
#endif

#endif // TS_EVENTS_EXT_H
