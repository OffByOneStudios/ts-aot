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
