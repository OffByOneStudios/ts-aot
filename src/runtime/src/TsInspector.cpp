// TsInspector.cpp - Stub implementation of Node.js inspector module
// Note: ts-aot does not use V8, so inspector functionality is not available.
// These stubs allow code that imports 'inspector' to compile and run without errors.

#include "TsInspector.h"
#include "TsString.h"
#include "GC.h"

namespace ts {

TsInspectorSession* TsInspectorSession::Create() {
    void* mem = ts_alloc(sizeof(TsInspectorSession));
    return new (mem) TsInspectorSession();
}

void TsInspectorSession::Connect() {
    // Stub: no V8 inspector available
    connected_ = true;
}

void TsInspectorSession::ConnectToMainThread() {
    // Stub: no V8 inspector available
    connected_ = true;
}

void TsInspectorSession::Disconnect() {
    // Stub: no V8 inspector available
    connected_ = false;
}

void TsInspectorSession::Post(const char* method, void* params, void* callback) {
    // Stub: no V8 inspector available
    // In a real implementation, this would send a Chrome DevTools Protocol message
    (void)method;
    (void)params;
    (void)callback;
}

}  // namespace ts

// Moved to extensions/node/inspector/src/inspector.cpp - now a separate library (ts_inspector)
// Class implementations stay here (TsInspectorSession inherits from TsEventEmitter)
#if 0
extern "C" {

// Open the inspector on the specified port
// In Node.js, this starts listening for debugger connections
void ts_inspector_open(void* port, void* host, void* wait) {
    // No-op: ts-aot doesn't have V8 inspector support
    (void)port;
    (void)host;
    (void)wait;
}

// Close the inspector
void ts_inspector_close() {
    // No-op: ts-aot doesn't have V8 inspector support
}

// Return the URL for connecting to the inspector
// Returns undefined since there's no inspector to connect to
void* ts_inspector_url() {
    // Return undefined (null) since there's no inspector URL
    return nullptr;
}

// Block until a debugger connects
// Since there's no debugger, this returns immediately
void ts_inspector_wait_for_debugger() {
    // No-op: returns immediately since there's no debugger to wait for
}

// Return the inspector console (for sending messages to devtools)
// Returns undefined since there's no inspector console
void* ts_inspector_console() {
    return nullptr;
}

// Create a new inspector Session
void* ts_inspector_session_create() {
    return ts::TsInspectorSession::Create();
}

// Connect to the inspector backend
void ts_inspector_session_connect(void* session) {
    if (!session) return;

    void* rawPtr = ts_value_get_object((TsValue*)session);
    if (!rawPtr) rawPtr = session;

    ts::TsInspectorSession* s = dynamic_cast<ts::TsInspectorSession*>((TsObject*)rawPtr);
    if (s) {
        s->Connect();
    }
}

// Connect to the main thread inspector (for worker threads)
void ts_inspector_session_connect_to_main_thread(void* session) {
    if (!session) return;

    void* rawPtr = ts_value_get_object((TsValue*)session);
    if (!rawPtr) rawPtr = session;

    ts::TsInspectorSession* s = dynamic_cast<ts::TsInspectorSession*>((TsObject*)rawPtr);
    if (s) {
        s->ConnectToMainThread();
    }
}

// Disconnect from the inspector
void ts_inspector_session_disconnect(void* session) {
    if (!session) return;

    void* rawPtr = ts_value_get_object((TsValue*)session);
    if (!rawPtr) rawPtr = session;

    ts::TsInspectorSession* s = dynamic_cast<ts::TsInspectorSession*>((TsObject*)rawPtr);
    if (s) {
        s->Disconnect();
    }
}

// Post a message to the inspector
void ts_inspector_session_post(void* session, void* method, void* params, void* callback) {
    // Stub: no V8 inspector available in ts-aot
    // All parameters are intentionally unused since this is a no-op stub
    (void)session;
    (void)method;
    (void)params;
    (void)callback;
}

}  // extern "C"
#endif // Moved to ts_inspector extension
