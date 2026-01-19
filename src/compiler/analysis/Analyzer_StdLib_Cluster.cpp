#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerCluster() {
    auto clusterModule = std::make_shared<ObjectType>();

    // =========================================================================
    // Worker class
    // Represents a worker process in the cluster
    // =========================================================================
    auto workerClass = std::make_shared<ClassType>("Worker");

    // Get ChildProcess class reference
    auto childProcessClass = std::make_shared<ClassType>("ChildProcess");

    // Properties
    workerClass->fields["id"] = std::make_shared<Type>(TypeKind::Int);
    workerClass->fields["process"] = childProcessClass;
    workerClass->fields["isDead"] = std::make_shared<Type>(TypeKind::Boolean);
    workerClass->fields["exitedAfterDisconnect"] = std::make_shared<Type>(TypeKind::Boolean);

    // Methods
    // send(message: any, sendHandle?: any): boolean
    auto sendMethod = std::make_shared<FunctionType>();
    sendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // message
    sendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // sendHandle
    sendMethod->isOptional = { false, true };
    sendMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    workerClass->methods["send"] = sendMethod;

    // disconnect(): void
    auto disconnectMethod = std::make_shared<FunctionType>();
    disconnectMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    workerClass->methods["disconnect"] = disconnectMethod;

    // kill(signal?: string): void
    auto killMethod = std::make_shared<FunctionType>();
    killMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // signal
    killMethod->isOptional = { true };
    killMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    workerClass->methods["kill"] = killMethod;

    // isConnected(): boolean
    auto isConnectedMethod = std::make_shared<FunctionType>();
    isConnectedMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    workerClass->methods["isConnected"] = isConnectedMethod;

    // EventEmitter methods (inherited)
    // on(event: string, listener: Function): this
    auto onMethod = std::make_shared<FunctionType>();
    onMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    onMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    onMethod->returnType = workerClass;
    workerClass->methods["on"] = onMethod;

    // once(event: string, listener: Function): this
    auto onceMethod = std::make_shared<FunctionType>();
    onceMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    onceMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    onceMethod->returnType = workerClass;
    workerClass->methods["once"] = onceMethod;

    // Register the Worker class
    symbols.defineType("Worker", workerClass);
    clusterModule->fields["Worker"] = workerClass;

    // =========================================================================
    // Cluster module properties
    // =========================================================================

    // isMaster: boolean - Deprecated alias for isPrimary
    clusterModule->fields["isMaster"] = std::make_shared<Type>(TypeKind::Boolean);

    // isPrimary: boolean - true if this is the primary process
    clusterModule->fields["isPrimary"] = std::make_shared<Type>(TypeKind::Boolean);

    // isWorker: boolean - true if this is a worker process
    clusterModule->fields["isWorker"] = std::make_shared<Type>(TypeKind::Boolean);

    // worker: Worker | undefined - Reference to current worker object (only in worker)
    clusterModule->fields["worker"] = workerClass;

    // workers: { [id: number]: Worker } - Map of all active workers (only in primary)
    // Use a generic object type - element access will return Worker type
    clusterModule->fields["workers"] = std::make_shared<Type>(TypeKind::Any);

    // settings: ClusterSettings
    auto settingsType = std::make_shared<ObjectType>();
    settingsType->fields["execArgv"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    settingsType->fields["exec"] = std::make_shared<Type>(TypeKind::String);
    settingsType->fields["args"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    settingsType->fields["silent"] = std::make_shared<Type>(TypeKind::Boolean);
    settingsType->fields["stdio"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
    settingsType->fields["uid"] = std::make_shared<Type>(TypeKind::Int);
    settingsType->fields["gid"] = std::make_shared<Type>(TypeKind::Int);
    settingsType->fields["inspectPort"] = std::make_shared<Type>(TypeKind::Any);  // number | Function
    settingsType->fields["serialization"] = std::make_shared<Type>(TypeKind::String);  // 'json' | 'advanced'
    settingsType->fields["cwd"] = std::make_shared<Type>(TypeKind::String);
    settingsType->fields["windowsHide"] = std::make_shared<Type>(TypeKind::Boolean);
    clusterModule->fields["settings"] = settingsType;

    // schedulingPolicy: number - SCHED_NONE (0) or SCHED_RR (1)
    clusterModule->fields["schedulingPolicy"] = std::make_shared<Type>(TypeKind::Int);

    // Scheduling policy constants
    clusterModule->fields["SCHED_NONE"] = std::make_shared<Type>(TypeKind::Int);
    clusterModule->fields["SCHED_RR"] = std::make_shared<Type>(TypeKind::Int);

    // =========================================================================
    // Cluster module methods
    // =========================================================================

    // fork(env?: object): Worker
    auto forkMethod = std::make_shared<FunctionType>();
    forkMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // env
    forkMethod->isOptional = { true };
    forkMethod->returnType = workerClass;
    clusterModule->fields["fork"] = forkMethod;

    // setupPrimary(settings?: ClusterSettings): void
    auto setupPrimaryMethod = std::make_shared<FunctionType>();
    setupPrimaryMethod->paramTypes.push_back(settingsType);  // settings
    setupPrimaryMethod->isOptional = { true };
    setupPrimaryMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    clusterModule->fields["setupPrimary"] = setupPrimaryMethod;

    // setupMaster(settings?: ClusterSettings): void - Deprecated alias
    auto setupMasterMethod = std::make_shared<FunctionType>();
    setupMasterMethod->paramTypes.push_back(settingsType);  // settings
    setupMasterMethod->isOptional = { true };
    setupMasterMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    clusterModule->fields["setupMaster"] = setupMasterMethod;

    // disconnect(callback?: Function): void
    auto disconnectClusterMethod = std::make_shared<FunctionType>();
    disconnectClusterMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));  // callback
    disconnectClusterMethod->isOptional = { true };
    disconnectClusterMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    clusterModule->fields["disconnect"] = disconnectClusterMethod;

    // EventEmitter methods on cluster object
    // on(event: string, listener: Function): cluster
    // Use Any for return type to avoid circular reference with ObjectType
    auto clusterOnMethod = std::make_shared<FunctionType>();
    clusterOnMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    clusterOnMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    clusterOnMethod->returnType = std::make_shared<Type>(TypeKind::Any);
    clusterModule->fields["on"] = clusterOnMethod;

    // once(event: string, listener: Function): cluster
    auto clusterOnceMethod = std::make_shared<FunctionType>();
    clusterOnceMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    clusterOnceMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    clusterOnceMethod->returnType = std::make_shared<Type>(TypeKind::Any);
    clusterModule->fields["once"] = clusterOnceMethod;

    // Register the cluster module
    symbols.define("cluster", clusterModule);
}

} // namespace ts
