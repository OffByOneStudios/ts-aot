#pragma once

#include "TsObject.h"
#include "TsMap.h"

// Forward declarations
struct TsFunction;

// TsProxy wraps a target object and intercepts operations via handler traps
class TsProxy : public TsMap {
public:
    static constexpr uint32_t PROXY_MAGIC = 0x50524F58; // "PROX"

    void* target;    // The target object being proxied
    TsMap* handler;  // Handler object containing trap functions
    bool revoked;    // If true, proxy is revoked and unusable

    static TsProxy* Create(void* target, TsMap* handler);

    // Trap invocations - these check handler and call trap or forward to target
    TsValue* get(TsValue* prop, void* receiver = nullptr);
    bool set(TsValue* prop, TsValue* value, void* receiver = nullptr);
    bool has(TsValue* prop);
    bool deleteProperty(TsValue* prop);
    TsValue* apply(void* thisArg, TsValue* args, int argCount);
    TsValue* construct(TsValue* args, int argCount, void* newTarget = nullptr);
    TsValue* ownKeys();

    // Check if a trap exists in handler
    TsFunction* getTrap(const char* trapName);

protected:
    TsProxy(void* target, TsMap* handler);

private:
    uint32_t proxyMagic = PROXY_MAGIC;
};

extern "C" {
    // Proxy constructor
    TsValue* ts_proxy_create(void* target, void* handler);

    // Proxy.revocable() returns { proxy, revoke }
    TsValue* ts_proxy_revocable(void* target, void* handler);

    // Proxy trap access - called from property access codegen
    TsValue* ts_proxy_get(void* proxy, void* prop, void* receiver);
    int64_t ts_proxy_set(void* proxy, void* prop, void* value, void* receiver);
    int64_t ts_proxy_has(void* proxy, void* prop);
    int64_t ts_proxy_delete(void* proxy, void* prop);
    TsValue* ts_proxy_apply(void* proxy, void* thisArg, void* args, int64_t argCount);
    TsValue* ts_proxy_construct(void* proxy, void* args, int64_t argCount, void* newTarget);
    TsValue* ts_proxy_ownKeys(void* proxy);

    // Check if object is a proxy
    int64_t ts_is_proxy(void* obj);
}
