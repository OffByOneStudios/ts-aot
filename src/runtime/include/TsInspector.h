// TsInspector.h - Stub implementation of Node.js inspector module
// Note: ts-aot does not use V8, so inspector functionality is not available.
// These stubs allow code that imports 'inspector' to compile and run without errors.

#ifndef TS_INSPECTOR_H
#define TS_INSPECTOR_H

#include "TsObject.h"
#include "TsEventEmitter.h"

namespace ts {

// Inspector Session class - stub implementation
// In Node.js, this provides access to V8 inspector protocol
class TsInspectorSession : public TsEventEmitter {
public:
    static TsInspectorSession* Create();

    // Connect to inspector backend (no-op in stub)
    void Connect();

    // Connect to main thread inspector (no-op in stub)
    void ConnectToMainThread();

    // Disconnect from inspector (no-op in stub)
    void Disconnect();

    // Post a message to inspector (no-op in stub)
    void Post(const char* method, void* params, void* callback);

    bool connected_ = false;
};

}  // namespace ts

extern "C" {

// Inspector module functions
void ts_inspector_open(void* port, void* host, void* wait);
void ts_inspector_close();
void* ts_inspector_url();
void ts_inspector_wait_for_debugger();
void* ts_inspector_console();

// Inspector Session class
void* ts_inspector_session_create();
void ts_inspector_session_connect(void* session);
void ts_inspector_session_connect_to_main_thread(void* session);
void ts_inspector_session_disconnect(void* session);
void ts_inspector_session_post(void* session, void* method, void* params, void* callback);

}

#endif  // TS_INSPECTOR_H
