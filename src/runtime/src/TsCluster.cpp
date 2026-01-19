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

// Forward declarations for JSON functions
extern "C" {
    TsString* ts_json_stringify(TsValue* value);
    TsValue* ts_json_parse(TsValue* jsonStr);
}

// Environment variable used to detect worker processes
static const char* CLUSTER_WORKER_ID_ENV = "CLUSTER_WORKER_ID";

// ============================================================================
// TsCluster Implementation
// ============================================================================

TsCluster* TsCluster::instance_ = nullptr;

TsCluster::TsCluster()
    : isMaster_(true)
    , isWorker_(false)
    , workers_(nullptr)
    , nextWorkerId_(1)
    , worker_(nullptr)
    , settings_(nullptr)
    , execPath_("")
    , execArgv_("")
{
    this->magic = MAGIC;

    // Detect if we're a worker process by checking environment variable
    const char* workerId = getenv(CLUSTER_WORKER_ID_ENV);
    if (workerId && strlen(workerId) > 0) {
        isMaster_ = false;
        isWorker_ = true;
        // Worker ID is set in env var, but worker object is created by parent
    }

    // Create workers map (only used in master)
    if (isMaster_) {
        workers_ = TsMap::Create();
    }

    // Get executable path for forking
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    execPath_ = path;
#else
    char path[1024];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        execPath_ = path;
    }
#endif
}

TsCluster::~TsCluster() {
    // Cleanup
}

TsCluster* TsCluster::GetInstance() {
    if (!instance_) {
        void* mem = ts_alloc(sizeof(TsCluster));
        instance_ = new (mem) TsCluster();
    }
    return instance_;
}

void TsCluster::Initialize() {
    // Ensure instance is created
    GetInstance();
}

TsWorker* TsCluster::Fork(void* env) {
    if (!isMaster_) {
        // Cannot fork from a worker
        return nullptr;
    }

    // Get worker ID for this new worker
    uint32_t workerId = nextWorkerId_++;

    // Build environment for child process
    std::vector<std::string> envStrings;

    // Copy current environment
#ifdef _WIN32
    char* envBlock = GetEnvironmentStringsA();
    if (envBlock) {
        char* e = envBlock;
        while (*e) {
            envStrings.push_back(e);
            e += strlen(e) + 1;
        }
        FreeEnvironmentStringsA(envBlock);
    }
#else
    extern char** environ;
    if (environ) {
        for (char** e = environ; *e; e++) {
            envStrings.push_back(*e);
        }
    }
#endif

    // Add CLUSTER_WORKER_ID to environment
    std::ostringstream workerIdEnv;
    workerIdEnv << CLUSTER_WORKER_ID_ENV << "=" << workerId;
    envStrings.push_back(workerIdEnv.str());

    // Add NODE_CHANNEL_FD for IPC (fd 3)
    envStrings.push_back("NODE_CHANNEL_FD=3");

    // Build env array
    std::vector<char*> envArray;
    for (auto& e : envStrings) {
        envArray.push_back(const_cast<char*>(e.c_str()));
    }
    envArray.push_back(nullptr);

    // Create child process
    void* cpMem = ts_alloc(sizeof(TsChildProcess));
    TsChildProcess* process = new (cpMem) TsChildProcess();

    // Get command line arguments for the child
    // In a cluster, we spawn the same executable with the same arguments
    std::vector<std::string> args;
    // No additional args needed - the child will detect it's a worker via env var

    // Spawn with IPC
    int result = process->SpawnWithIPC(execPath_.c_str(), args, nullptr, envArray.data(), false, false);
    if (result < 0) {
        // Spawn failed
        TsValue* err = (TsValue*)ts_error_create(TsString::Create("Failed to fork worker"));
        void* errArgs[] = { err };
        Emit("error", 1, errArgs);
        return nullptr;
    }

    // Create worker wrapper
    void* workerMem = ts_alloc(sizeof(TsWorker));
    TsWorker* worker = new (workerMem) TsWorker(workerId, process);

    // Add to workers map
    TsString* idKey = TsString::Create(std::to_string(workerId).c_str());
    workers_->Set(idKey, worker);

    // Set up event forwarding from child process to worker and cluster
    // The child process emits events that we need to forward

    // Listen for 'message' events from child process
    auto onMessage = [this, worker](void* message) {
        // Forward message to worker
        void* msgArgs[] = { message };
        worker->Emit("message", 1, msgArgs);

        // Check for internal cluster messages
        // Internal messages have type: 'online', 'disconnect', etc.
        OnWorkerMessage(worker, message);
    };

    // Listen for 'exit' event from child process
    auto onExit = [this, worker](int code, const char* signal) {
        worker->SetDead(true);
        if (!worker->IsConnected()) {
            worker->SetExitedAfterDisconnect(true);
        }

        // Emit exit event on worker
        TsValue* codeVal = ts_value_make_int(code);
        TsValue* signalVal = signal ? ts_value_make_string(TsString::Create(signal)) : ts_value_make_null();
        void* exitArgs[] = { codeVal, signalVal };
        worker->Emit("exit", 2, exitArgs);

        // Emit exit event on cluster
        OnWorkerExit(worker, code, signal);
    };

    // Store callbacks on process using event emitter
    // We'll handle this through the TsChildProcess message/exit events

    // For now, emit 'fork' event
    void* forkArgs[] = { worker };
    Emit("fork", 1, forkArgs);

    return worker;
}

void TsCluster::SetupPrimary(void* settings) {
    settings_ = settings;
    // In Node.js, setupPrimary/setupMaster allows configuring exec, args, etc.
    // For now, we store the settings object
}

void TsCluster::Disconnect(void* callback) {
    if (!isMaster_) {
        return;
    }

    // Disconnect all workers
    if (workers_) {
        // Iterate through workers and disconnect each
        TsArray* keys = (TsArray*)workers_->GetKeys();
        if (keys) {
            for (int64_t i = 0; i < keys->Length(); i++) {
                // Get key as raw int64_t (stored as pointer)
                int64_t keyRaw = keys->Get(i);
                // Create a TsValue for the key (keys from GetKeys are TsString pointers)
                TsValue keyVal;
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = (void*)keyRaw;

                TsValue workerVal = workers_->Get(keyVal);
                void* workerPtr = ts_value_get_object(&workerVal);
                TsWorker* worker = dynamic_cast<TsWorker*>((TsObject*)workerPtr);
                if (worker && worker->IsConnected()) {
                    worker->Disconnect();
                }
            }
        }
    }

    // TODO: Call callback when all workers are disconnected
}

void TsCluster::OnWorkerMessage(TsWorker* worker, void* message) {
    // Check if this is an internal cluster message
    // Internal messages have a 'type' field with values like 'online'

    // Forward to cluster 'message' event
    void* msgArgs[] = { worker, message };
    Emit("message", 2, msgArgs);
}

void TsCluster::OnWorkerOnline(TsWorker* worker) {
    // Emit 'online' event on worker
    worker->Emit("online", 0, nullptr);

    // Emit 'online' event on cluster
    void* args[] = { worker };
    Emit("online", 1, args);
}

void TsCluster::OnWorkerDisconnect(TsWorker* worker) {
    // Emit 'disconnect' event on worker
    worker->Emit("disconnect", 0, nullptr);

    // Emit 'disconnect' event on cluster
    void* args[] = { worker };
    Emit("disconnect", 1, args);
}

void TsCluster::OnWorkerExit(TsWorker* worker, int code, const char* signal) {
    // Emit 'exit' event on cluster
    TsValue* codeVal = ts_value_make_int(code);
    TsValue* signalVal = signal ? ts_value_make_string(TsString::Create(signal)) : ts_value_make_null();
    void* args[] = { worker, codeVal, signalVal };
    Emit("exit", 3, args);
}

// ============================================================================
// TsWorker Implementation
// ============================================================================

TsWorker::TsWorker(uint32_t id, TsChildProcess* process)
    : id_(id)
    , process_(process)
    , isDead_(false)
    , exitedAfterDisconnect_(false)
{
    this->magic = MAGIC;
}

TsWorker::~TsWorker() {
    // Cleanup
}

bool TsWorker::Send(void* message, void* sendHandle) {
    if (!process_ || isDead_) {
        return false;
    }
    return process_->Send(message, sendHandle);
}

void TsWorker::Disconnect() {
    if (process_ && !isDead_) {
        process_->Disconnect();
    }
}

void TsWorker::Kill(const char* signal) {
    if (process_ && !isDead_) {
        process_->Kill(signal);
    }
}

bool TsWorker::IsConnected() const {
    if (!process_) {
        return false;
    }
    return process_->IsConnected();
}

// ============================================================================
// C API Implementation
// ============================================================================

extern "C" {

void ts_cluster_init() {
    TsCluster::Initialize();
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

// Worker C API

int64_t ts_worker_get_id(void* worker) {
    void* rawPtr = ts_value_get_object((TsValue*)worker);
    if (!rawPtr) rawPtr = worker;

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return -1;

    return w->GetId();
}

void* ts_worker_get_process(void* worker) {
    void* rawPtr = ts_value_get_object((TsValue*)worker);
    if (!rawPtr) rawPtr = worker;

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return ts_value_make_undefined();

    TsChildProcess* process = w->GetProcess();
    if (!process) return ts_value_make_undefined();

    return ts_value_make_object(process);
}

bool ts_worker_is_dead(void* worker) {
    void* rawPtr = ts_value_get_object((TsValue*)worker);
    if (!rawPtr) rawPtr = worker;

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return true;

    return w->IsDead();
}

bool ts_worker_exited_after_disconnect(void* worker) {
    void* rawPtr = ts_value_get_object((TsValue*)worker);
    if (!rawPtr) rawPtr = worker;

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return false;

    return w->ExitedAfterDisconnect();
}

bool ts_worker_is_connected(void* worker) {
    void* rawPtr = ts_value_get_object((TsValue*)worker);
    if (!rawPtr) rawPtr = worker;

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return false;

    return w->IsConnected();
}

bool ts_worker_send(void* worker, void* message, void* sendHandle) {
    void* rawPtr = ts_value_get_object((TsValue*)worker);
    if (!rawPtr) rawPtr = worker;

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return false;

    return w->Send(message, sendHandle);
}

void ts_worker_disconnect(void* worker) {
    void* rawPtr = ts_value_get_object((TsValue*)worker);
    if (!rawPtr) rawPtr = worker;

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return;

    w->Disconnect();
}

void ts_worker_kill(void* worker, void* signal) {
    void* rawPtr = ts_value_get_object((TsValue*)worker);
    if (!rawPtr) rawPtr = worker;

    TsWorker* w = dynamic_cast<TsWorker*>((TsObject*)rawPtr);
    if (!w) return;

    const char* sig = "SIGTERM";
    if (signal) {
        void* sigRaw = ts_value_get_object((TsValue*)signal);
        if (!sigRaw) sigRaw = signal;
        TsString* sigStr = dynamic_cast<TsString*>((TsObject*)sigRaw);
        if (sigStr) {
            sig = sigStr->ToUtf8();
        }
    }

    w->Kill(sig);
}

} // extern "C"
