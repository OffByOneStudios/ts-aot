#pragma once
#include "TsEventEmitter.h"
#include "TsChildProcess.h"
#include "TsMap.h"
#include <string>

/*
 * TsCluster - Node.js cluster module implementation
 *
 * The cluster module allows spawning multiple worker processes that can share
 * server ports, enabling utilization of multiple CPU cores for scalable applications.
 *
 * Detection mechanism:
 *   - Master: CLUSTER_WORKER_ID environment variable not set
 *   - Worker: CLUSTER_WORKER_ID environment variable set to worker ID
 *
 * Inherits from TsEventEmitter to emit events:
 *   - 'fork': Emitted when a new worker is forked
 *   - 'online': Emitted when worker sends online message
 *   - 'exit': Emitted when worker process exits
 *   - 'disconnect': Emitted when worker disconnects
 *   - 'message': Emitted when cluster receives message from worker
 */

class TsWorker;

class TsCluster : public TsEventEmitter {
public:
    static constexpr uint32_t MAGIC = 0x434C5354; // "CLST"

    // Get the singleton instance
    static TsCluster* GetInstance();

    // Initialize the cluster module (called from Core.cpp)
    static void Initialize();

    // Safe casting helper
    virtual TsCluster* AsCluster() { return this; }

    // Properties
    bool IsMaster() const { return isMaster_; }
    bool IsPrimary() const { return isMaster_; }  // Alias for isMaster
    bool IsWorker() const { return isWorker_; }
    TsWorker* GetWorker() const { return worker_; }  // Only valid in worker context
    TsMap* GetWorkers() const { return workers_; }   // Map of id -> Worker (master only)
    void* GetSettings() const { return settings_; }

    // Methods
    TsWorker* Fork(void* env = nullptr);
    void SetupPrimary(void* settings = nullptr);
    void SetupMaster(void* settings = nullptr) { SetupPrimary(settings); }  // Alias
    void Disconnect(void* callback = nullptr);

    // Internal: called when a worker sends a message
    void OnWorkerMessage(TsWorker* worker, void* message);
    void OnWorkerOnline(TsWorker* worker);
    void OnWorkerDisconnect(TsWorker* worker);
    void OnWorkerExit(TsWorker* worker, int code, const char* signal);

private:
    TsCluster();
    virtual ~TsCluster();

    static TsCluster* instance_;

    bool isMaster_;
    bool isWorker_;
    TsMap* workers_;           // id -> Worker map (master only)
    uint32_t nextWorkerId_;
    TsWorker* worker_;         // Current worker (worker context only)
    void* settings_;           // Cluster settings
    std::string execPath_;     // Path to current executable
    std::string execArgv_;     // Execution arguments
};

/*
 * TsWorker - Represents a worker process in the cluster
 *
 * Wraps TsChildProcess for worker management.
 *
 * Inherits from TsEventEmitter to emit events:
 *   - 'online': Emitted when worker is online and ready
 *   - 'message': Emitted when worker receives message
 *   - 'disconnect': Emitted when IPC channel closes
 *   - 'exit': Emitted when worker process exits
 *   - 'error': Emitted on errors
 */
class TsWorker : public TsEventEmitter {
public:
    static constexpr uint32_t MAGIC = 0x574F524B; // "WORK"

    TsWorker(uint32_t id, TsChildProcess* process);
    virtual ~TsWorker();

    // Safe casting helper
    virtual TsWorker* AsWorker() { return this; }

    // Properties
    uint32_t GetId() const { return id_; }
    TsChildProcess* GetProcess() const { return process_; }
    bool IsDead() const { return isDead_; }
    bool ExitedAfterDisconnect() const { return exitedAfterDisconnect_; }

    // Methods
    bool Send(void* message, void* sendHandle = nullptr);
    void Disconnect();
    void Kill(const char* signal = "SIGTERM");
    bool IsConnected() const;

    // Internal
    void SetDead(bool dead) { isDead_ = dead; }
    void SetExitedAfterDisconnect(bool val) { exitedAfterDisconnect_ = val; }

private:
    uint32_t id_;
    TsChildProcess* process_;
    bool isDead_;
    bool exitedAfterDisconnect_;
};

// C API for cluster module
extern "C" {
    // Module initialization
    void ts_cluster_init();

    // Singleton access
    void* ts_cluster_get_instance();

    // Properties
    bool ts_cluster_is_master();
    bool ts_cluster_is_primary();
    bool ts_cluster_is_worker();
    void* ts_cluster_get_worker();
    void* ts_cluster_get_workers();
    void* ts_cluster_get_settings();

    // Methods
    void* ts_cluster_fork(void* env);
    void ts_cluster_setup_primary(void* settings);
    void ts_cluster_setup_master(void* settings);
    void ts_cluster_disconnect(void* callback);

    // Worker properties
    int64_t ts_worker_get_id(void* worker);
    void* ts_worker_get_process(void* worker);
    bool ts_worker_is_dead(void* worker);
    bool ts_worker_exited_after_disconnect(void* worker);
    bool ts_worker_is_connected(void* worker);

    // Worker methods
    bool ts_worker_send(void* worker, void* message, void* sendHandle);
    void ts_worker_disconnect(void* worker);
    void ts_worker_kill(void* worker, void* signal);
}
