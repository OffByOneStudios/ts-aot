// Cluster extension for ts-aot
// Migrated from src/runtime/src/TsCluster.cpp

#include "TsCluster.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsRuntime.h"
#include "GC.h"
#include <string.h>
#include <stdlib.h>
#include <new>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Register ts_cluster_init as the Node.js init hook via static initializer.
// This runs before main(), so ts_node_init_hook is set by the time ts_main() calls it.
static struct ClusterHookRegistrar {
    ClusterHookRegistrar() { ts_node_init_hook = &ts_cluster_init; }
} s_cluster_hook_registrar;

// ============================================================================
// C API Implementation
// ============================================================================

extern "C" {

void ts_cluster_init() {
    TsCluster::Initialize();

    // If we're a worker, send 'online' message to master
    TsCluster* cluster = TsCluster::GetInstance();
    if (cluster->IsWorker() && ts_process_get_connected()) {
        // Create internal message: { cmd: 'online' }
        TsMap* msg = TsMap::Create();
        TsValue cmdKey;
        cmdKey.type = ValueType::STRING_PTR;
        cmdKey.ptr_val = TsString::Create("cmd");
        TsValue cmdVal;
        cmdVal.type = ValueType::STRING_PTR;
        cmdVal.ptr_val = TsString::Create("online");
        msg->Set(cmdKey, cmdVal);

        // Send to master
        TsValue* msgVal = ts_value_make_object(msg);
        ts_process_send(msgVal);
    }
}

void* ts_cluster_get_instance() {
    return TsCluster::GetInstance();
}

bool ts_cluster_is_master() {
    return TsCluster::GetInstance()->IsMaster();
}

bool ts_cluster_is_primary() {
    return TsCluster::GetInstance()->IsPrimary();
}

bool ts_cluster_is_worker() {
    return TsCluster::GetInstance()->IsWorker();
}

void* ts_cluster_get_worker() {
    TsWorker* worker = TsCluster::GetInstance()->GetWorker();
    if (!worker) {
        // Return null pointer instead of boxed undefined
        // This allows `if (!cluster.worker)` to work correctly
        return nullptr;
    }
    return ts_value_make_object(worker);
}

void* ts_cluster_get_workers() {
    TsMap* workers = TsCluster::GetInstance()->GetWorkers();
    if (!workers) {
        return ts_value_make_object(TsMap::Create());
    }
    return ts_value_make_object(workers);
}

void* ts_cluster_get_settings() {
    void* settings = TsCluster::GetInstance()->GetSettings();
    if (!settings) {
        return ts_value_make_object(TsMap::Create());
    }
    return settings;
}

void* ts_cluster_fork(void* env) {
    // Unbox env if provided
    void* rawEnv = nullptr;
    if (env) {
        rawEnv = ts_value_get_object((TsValue*)env);
        if (!rawEnv) rawEnv = env;
    }

    TsWorker* worker = TsCluster::GetInstance()->Fork(rawEnv);
    if (!worker) {
        return ts_value_make_undefined();
    }
    return ts_value_make_object(worker);
}

void ts_cluster_setup_primary(void* settings) {
    void* rawSettings = nullptr;
    if (settings) {
        rawSettings = ts_value_get_object((TsValue*)settings);
        if (!rawSettings) rawSettings = settings;
    }
    TsCluster::GetInstance()->SetupPrimary(rawSettings);
}

void ts_cluster_setup_master(void* settings) {
    ts_cluster_setup_primary(settings);
}

void ts_cluster_disconnect(void* callback) {
    TsCluster::GetInstance()->Disconnect(callback);
}

int64_t ts_cluster_get_scheduling_policy() {
    return TsCluster::GetInstance()->GetSchedulingPolicy();
}

void ts_cluster_set_scheduling_policy(int64_t policy) {
    TsCluster::GetInstance()->SetSchedulingPolicy(static_cast<int>(policy));
}

int64_t ts_cluster_SCHED_NONE() {
    return TsCluster::CLUSTER_SCHED_NONE;
}

int64_t ts_cluster_SCHED_RR() {
    return TsCluster::CLUSTER_SCHED_RR;
}

// Worker C API

int64_t ts_worker_get_id(void* worker) {
    void* rawPtr = ts_nanbox_safe_unbox(worker);

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return -1;

    return w->GetId();
}

void* ts_worker_get_process(void* worker) {
    void* rawPtr = ts_nanbox_safe_unbox(worker);

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return ts_value_make_undefined();

    TsChildProcess* process = w->GetProcess();
    if (!process) return ts_value_make_undefined();

    return ts_value_make_object(process);
}

bool ts_worker_is_dead(void* worker) {
    void* rawPtr = ts_nanbox_safe_unbox(worker);

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return true;

    return w->IsDead();
}

bool ts_worker_exited_after_disconnect(void* worker) {
    void* rawPtr = ts_nanbox_safe_unbox(worker);

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return false;

    return w->ExitedAfterDisconnect();
}

bool ts_worker_is_connected(void* worker) {
    void* rawPtr = ts_nanbox_safe_unbox(worker);

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return false;

    return w->IsConnected();
}

bool ts_worker_send(void* worker, void* message, void* sendHandle) {
    void* rawPtr = ts_nanbox_safe_unbox(worker);

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return false;

    return w->Send(message, sendHandle);
}

void ts_worker_disconnect(void* worker) {
    void* rawPtr = ts_nanbox_safe_unbox(worker);

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return;

    w->Disconnect();
}

void ts_worker_kill(void* worker, void* signal) {
    void* rawPtr = ts_nanbox_safe_unbox(worker);

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return;

    const char* sig = "SIGTERM";
    if (signal) {
        void* sigRaw = ts_nanbox_safe_unbox(signal);
        TsString* sigStr = dynamic_cast<TsString*>((TsObject*)sigRaw);
        if (sigStr) {
            sig = sigStr->ToUtf8();
        }
    }

    w->Kill(sig);
}

} // extern "C"
