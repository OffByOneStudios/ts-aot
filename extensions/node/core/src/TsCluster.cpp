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

// Context struct for worker callbacks
struct WorkerContext {
    TsCluster* cluster;
    TsWorker* worker;
};

// Static callback for child process messages
static void OnWorkerMessageCallback(void* context, void* message) {
    WorkerContext* ctx = (WorkerContext*)context;
    if (ctx && ctx->cluster && ctx->worker) {
        // Let cluster process the message - it will emit events as needed
        ctx->cluster->OnWorkerMessage(ctx->worker, message);
    }
}

// Static callback for child process exit
static void OnWorkerExitCallback(void* context, int code, const char* signal) {
    WorkerContext* ctx = (WorkerContext*)context;
    if (ctx && ctx->cluster && ctx->worker) {
        ctx->worker->SetDead(true);
        if (!ctx->worker->IsConnected()) {
            ctx->worker->SetExitedAfterDisconnect(true);
        }

        // Emit exit event on worker
        TsValue* codeVal = ts_value_make_int(code);
        TsValue* signalVal = signal ? ts_value_make_string(TsString::Create(signal)) : ts_value_make_null();
        void* exitArgs[] = { codeVal, signalVal };
        ctx->worker->Emit("exit", 2, exitArgs);

        // Emit exit event on cluster
        ctx->cluster->OnWorkerExit(ctx->worker, code, signal);
    }
}

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
    , schedulingPolicy_(SCHED_RR)  // Default to round-robin on most platforms
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

    // On Windows, default to SCHED_NONE (OS handles scheduling)
#ifdef _WIN32
    schedulingPolicy_ = SCHED_NONE;
#endif

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

    // Create context for callbacks
    WorkerContext* ctx = (WorkerContext*)ts_alloc(sizeof(WorkerContext));
    ctx->cluster = this;
    ctx->worker = worker;

    // Set up message and exit callbacks on the child process
    process->SetMessageCallback(OnWorkerMessageCallback, ctx);
    process->SetExitCallback(OnWorkerExitCallback, ctx);

    // Emit 'fork' event
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
    // Internal messages have a 'cmd' field with values like 'online'

    TsValue* msgVal = (TsValue*)message;
    if (msgVal && msgVal->type == ValueType::OBJECT_PTR) {
        TsObject* msgObj = (TsObject*)msgVal->ptr_val;
        TsMap* msgMap = dynamic_cast<TsMap*>(msgObj);
        if (msgMap) {
            // Check for 'cmd' field (internal cluster protocol)
            TsValue cmdKey;
            cmdKey.type = ValueType::STRING_PTR;
            cmdKey.ptr_val = TsString::Create("cmd");
            TsValue cmdVal = msgMap->Get(cmdKey);

            if (cmdVal.type == ValueType::STRING_PTR) {
                TsString* cmdStr = (TsString*)cmdVal.ptr_val;
                const char* cmd = cmdStr->ToUtf8();

                if (strcmp(cmd, "online") == 0) {
                    // Worker is online - emit 'online' events
                    OnWorkerOnline(worker);
                    return;  // Don't forward internal messages to user code
                }
                if (strcmp(cmd, "listening") == 0) {
                    // Worker is listening on a server - emit 'listening' events
                    // TODO: Extract address info from message
                    OnWorkerListening(worker, nullptr);
                    return;  // Don't forward internal messages to user code
                }
                // Other internal commands are filtered out
                if (strncmp(cmd, "NODE_", 5) == 0) {
                    return;  // Internal Node.js cluster protocol message
                }
            }
        }
    }

    // Forward non-internal messages to worker and cluster 'message' events
    void* workerMsgArgs[] = { message };
    worker->Emit("message", 1, workerMsgArgs);

    void* clusterMsgArgs[] = { worker, message };
    Emit("message", 2, clusterMsgArgs);
}

void TsCluster::OnWorkerOnline(TsWorker* worker) {
    // Emit 'online' event on worker
    worker->Emit("online", 0, nullptr);

    // Emit 'online' event on cluster
    void* args[] = { worker };
    Emit("online", 1, args);
}

void TsCluster::OnWorkerListening(TsWorker* worker, void* address) {
    // Emit 'listening' event on worker with address info
    if (address) {
        void* workerArgs[] = { address };
        worker->Emit("listening", 1, workerArgs);
    } else {
        worker->Emit("listening", 0, nullptr);
    }

    // Emit 'listening' event on cluster
    if (address) {
        void* args[] = { worker, address };
        Emit("listening", 2, args);
    } else {
        void* args[] = { worker };
        Emit("listening", 1, args);
    }
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

