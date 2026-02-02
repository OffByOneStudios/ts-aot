#ifndef TS_INSPECTOR_EXT_H
#define TS_INSPECTOR_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Inspector Module Functions
// =============================================================================

// Open the inspector on the specified port
void ts_inspector_open(void* port, void* host, void* wait);

// Close the inspector
void ts_inspector_close();

// Return the URL for connecting to the inspector (returns undefined/null)
void* ts_inspector_url();

// Block until a debugger connects (no-op, returns immediately)
void ts_inspector_wait_for_debugger();

// Return the inspector console (returns undefined/null)
void* ts_inspector_console();

// =============================================================================
// Inspector Session Functions
// =============================================================================

// Create a new inspector Session
void* ts_inspector_session_create();

// Connect to the inspector backend
void ts_inspector_session_connect(void* session);

// Connect to the main thread inspector (for worker threads)
void ts_inspector_session_connect_to_main_thread(void* session);

// Disconnect from the inspector
void ts_inspector_session_disconnect(void* session);

// Post a message to the inspector
void ts_inspector_session_post(void* session, void* method, void* params, void* callback);

#ifdef __cplusplus
}
#endif

#endif // TS_INSPECTOR_EXT_H
