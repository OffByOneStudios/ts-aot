#include "Analyzer.h"

namespace ts {

void Analyzer::registerProcess() {
    auto processType = std::make_shared<ObjectType>();
    
    // process.argv: string[]
    processType->fields["argv"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    
    // process.env: { [key: string]: string }
    auto envType = std::make_shared<ObjectType>();
    processType->fields["env"] = envType;

    // process.exit(code?: number): void
    auto exitType = std::make_shared<FunctionType>();
    exitType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    exitType->returnType = std::make_shared<Type>(TypeKind::Void);
    processType->fields["exit"] = exitType;

    // process.exitCode: number
    processType->fields["exitCode"] = std::make_shared<Type>(TypeKind::Int);

    // process.cwd(): string
    auto cwdType = std::make_shared<FunctionType>();
    cwdType->returnType = std::make_shared<Type>(TypeKind::String);
    processType->fields["cwd"] = cwdType;

    // process.chdir(dir: string): void
    auto chdirType = std::make_shared<FunctionType>();
    chdirType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    chdirType->returnType = std::make_shared<Type>(TypeKind::Void);
    processType->fields["chdir"] = chdirType;

    // process.nextTick(callback: Function, ...args: any[]): void
    auto nextTickType = std::make_shared<FunctionType>();
    nextTickType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    nextTickType->returnType = std::make_shared<Type>(TypeKind::Void);
    processType->fields["nextTick"] = nextTickType;

    // process.platform: string
    processType->fields["platform"] = std::make_shared<Type>(TypeKind::String);
    
    // process.arch: string
    processType->fields["arch"] = std::make_shared<Type>(TypeKind::String);

    // process.stdout, stderr, stdin
    auto writableClass = symbols.lookupType("Writable");
    auto readableClass = symbols.lookupType("Readable");
    
    if (writableClass) {
        processType->fields["stdout"] = writableClass;
        processType->fields["stderr"] = writableClass;
    } else {
        processType->fields["stdout"] = std::make_shared<Type>(TypeKind::Any);
        processType->fields["stderr"] = std::make_shared<Type>(TypeKind::Any);
    }
    
    if (readableClass) {
        processType->fields["stdin"] = readableClass;
    } else {
        processType->fields["stdin"] = std::make_shared<Type>(TypeKind::Any);
    }

    // ========================================================================
    // Milestone 102.5: Process Info
    // ========================================================================
    
    // process.pid: number
    processType->fields["pid"] = std::make_shared<Type>(TypeKind::Int);
    
    // process.ppid: number
    processType->fields["ppid"] = std::make_shared<Type>(TypeKind::Int);
    
    // process.version: string
    processType->fields["version"] = std::make_shared<Type>(TypeKind::String);
    
    // process.versions: object
    auto versionsType = std::make_shared<ObjectType>();
    versionsType->fields["node"] = std::make_shared<Type>(TypeKind::String);
    versionsType->fields["v8"] = std::make_shared<Type>(TypeKind::String);
    versionsType->fields["uv"] = std::make_shared<Type>(TypeKind::String);
    versionsType->fields["icu"] = std::make_shared<Type>(TypeKind::String);
    processType->fields["versions"] = versionsType;
    
    // process.argv0: string
    processType->fields["argv0"] = std::make_shared<Type>(TypeKind::String);
    
    // process.execPath: string
    processType->fields["execPath"] = std::make_shared<Type>(TypeKind::String);
    
    // process.execArgv: string[]
    processType->fields["execArgv"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    
    // process.title: string (getter/setter)
    processType->fields["title"] = std::make_shared<Type>(TypeKind::String);
    
    // ========================================================================
    // Milestone 102.6: High-Resolution Time & Resource Usage
    // ========================================================================
    
    // process.hrtime(time?: [number, number]): [number, number]
    auto hrtimeType = std::make_shared<FunctionType>();
    auto hrtimeTupleType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Int));
    hrtimeType->paramTypes.push_back(hrtimeTupleType); // optional prev time
    hrtimeType->returnType = hrtimeTupleType;
    
    // process.hrtime.bigint(): bigint - add as a method object
    auto hrtimeBigintType = std::make_shared<FunctionType>();
    hrtimeBigintType->returnType = std::make_shared<Type>(TypeKind::Int); // TODO: Use BigInt when available
    
    auto hrtimeObjType = std::make_shared<ObjectType>();
    hrtimeObjType->fields["bigint"] = hrtimeBigintType;
    // hrtime is both callable and has .bigint property
    processType->fields["hrtime"] = hrtimeType; // For now just the function
    
    // process.uptime(): number
    auto uptimeType = std::make_shared<FunctionType>();
    uptimeType->returnType = std::make_shared<Type>(TypeKind::Double);
    processType->fields["uptime"] = uptimeType;
    
    // process.memoryUsage(): MemoryUsage
    auto memoryUsageReturnType = std::make_shared<ObjectType>();
    memoryUsageReturnType->fields["rss"] = std::make_shared<Type>(TypeKind::Int);
    memoryUsageReturnType->fields["heapTotal"] = std::make_shared<Type>(TypeKind::Int);
    memoryUsageReturnType->fields["heapUsed"] = std::make_shared<Type>(TypeKind::Int);
    memoryUsageReturnType->fields["external"] = std::make_shared<Type>(TypeKind::Int);
    memoryUsageReturnType->fields["arrayBuffers"] = std::make_shared<Type>(TypeKind::Int);
    
    auto memoryUsageType = std::make_shared<FunctionType>();
    memoryUsageType->returnType = memoryUsageReturnType;
    processType->fields["memoryUsage"] = memoryUsageType;
    
    // process.cpuUsage(previousValue?: CpuUsage): CpuUsage
    auto cpuUsageReturnType = std::make_shared<ObjectType>();
    cpuUsageReturnType->fields["user"] = std::make_shared<Type>(TypeKind::Int);
    cpuUsageReturnType->fields["system"] = std::make_shared<Type>(TypeKind::Int);
    
    auto cpuUsageType = std::make_shared<FunctionType>();
    cpuUsageType->paramTypes.push_back(cpuUsageReturnType); // optional prev usage
    cpuUsageType->returnType = cpuUsageReturnType;
    processType->fields["cpuUsage"] = cpuUsageType;
    
    // process.resourceUsage(): ResourceUsage
    auto resourceUsageReturnType = std::make_shared<ObjectType>();
    resourceUsageReturnType->fields["userCPUTime"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["systemCPUTime"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["maxRSS"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["sharedMemorySize"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["unsharedDataSize"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["unsharedStackSize"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["minorPageFault"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["majorPageFault"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["swappedOut"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["fsRead"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["fsWrite"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["ipcSent"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["ipcReceived"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["signalsCount"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["voluntaryContextSwitches"] = std::make_shared<Type>(TypeKind::Int);
    resourceUsageReturnType->fields["involuntaryContextSwitches"] = std::make_shared<Type>(TypeKind::Int);
    
    auto resourceUsageType = std::make_shared<FunctionType>();
    resourceUsageType->returnType = resourceUsageReturnType;
    processType->fields["resourceUsage"] = resourceUsageType;
    
    // ========================================================================
    // Milestone 102.7: Process Control
    // ========================================================================
    
    // process.kill(pid: number, signal?: number | string): boolean
    auto killType = std::make_shared<FunctionType>();
    killType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    killType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int)); // signal
    killType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    processType->fields["kill"] = killType;
    
    // process.abort(): never
    auto abortType = std::make_shared<FunctionType>();
    abortType->returnType = std::make_shared<Type>(TypeKind::Void);
    processType->fields["abort"] = abortType;
    
    // process.umask(mask?: number): number
    auto umaskType = std::make_shared<FunctionType>();
    umaskType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    umaskType->returnType = std::make_shared<Type>(TypeKind::Int);
    processType->fields["umask"] = umaskType;
    
    // process.emitWarning(warning: string | Error): void
    auto emitWarningType = std::make_shared<FunctionType>();
    emitWarningType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    emitWarningType->returnType = std::make_shared<Type>(TypeKind::Void);
    processType->fields["emitWarning"] = emitWarningType;
    
    // ========================================================================
    // Milestone 102.10: Configuration & Features
    // ========================================================================
    
    // process.config: object
    auto configType = std::make_shared<ObjectType>();
    processType->fields["config"] = configType;
    
    // process.features: object
    auto featuresType = std::make_shared<ObjectType>();
    featuresType->fields["inspector"] = std::make_shared<Type>(TypeKind::Boolean);
    featuresType->fields["debug"] = std::make_shared<Type>(TypeKind::Boolean);
    featuresType->fields["uv"] = std::make_shared<Type>(TypeKind::Boolean);
    featuresType->fields["ipv6"] = std::make_shared<Type>(TypeKind::Boolean);
    featuresType->fields["tls_alpn"] = std::make_shared<Type>(TypeKind::Boolean);
    featuresType->fields["tls_sni"] = std::make_shared<Type>(TypeKind::Boolean);
    featuresType->fields["tls_ocsp"] = std::make_shared<Type>(TypeKind::Boolean);
    featuresType->fields["tls"] = std::make_shared<Type>(TypeKind::Boolean);
    processType->fields["features"] = featuresType;
    
    // process.release: object
    auto releaseType = std::make_shared<ObjectType>();
    releaseType->fields["name"] = std::make_shared<Type>(TypeKind::String);
    releaseType->fields["lts"] = std::make_shared<Type>(TypeKind::String);
    processType->fields["release"] = releaseType;
    
    // process.debugPort: number
    processType->fields["debugPort"] = std::make_shared<Type>(TypeKind::Int);

    // ========================================================================
    // Milestone 102.8: Process Events
    // ========================================================================
    
    // process.on(event: string, listener: Function): this
    auto onType = std::make_shared<FunctionType>();
    onType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    onType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    onType->returnType = processType;
    processType->fields["on"] = onType;
    
    // process.once(event: string, listener: Function): this
    auto onceType = std::make_shared<FunctionType>();
    onceType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    onceType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    onceType->returnType = processType;
    processType->fields["once"] = onceType;
    
    // process.removeListener(event: string, listener: Function): this
    auto removeListenerType = std::make_shared<FunctionType>();
    removeListenerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    removeListenerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    removeListenerType->returnType = processType;
    processType->fields["removeListener"] = removeListenerType;
    
    // process.removeAllListeners(event?: string): this
    auto removeAllListenersType = std::make_shared<FunctionType>();
    removeAllListenersType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    removeAllListenersType->returnType = processType;
    processType->fields["removeAllListeners"] = removeAllListenersType;
    
    // process.setUncaughtExceptionCaptureCallback(fn: Function | null): void
    auto setUncaughtType = std::make_shared<FunctionType>();
    setUncaughtType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    setUncaughtType->returnType = std::make_shared<Type>(TypeKind::Void);
    processType->fields["setUncaughtExceptionCaptureCallback"] = setUncaughtType;
    
    // process.hasUncaughtExceptionCaptureCallback(): boolean
    auto hasUncaughtType = std::make_shared<FunctionType>();
    hasUncaughtType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    processType->fields["hasUncaughtExceptionCaptureCallback"] = hasUncaughtType;
    
    // ========================================================================
    // Milestone 102.9: Event Loop Handles
    // ========================================================================
    
    // process.ref(): this
    auto refType = std::make_shared<FunctionType>();
    refType->returnType = processType;
    processType->fields["ref"] = refType;
    
    // process.unref(): this
    auto unrefType = std::make_shared<FunctionType>();
    unrefType->returnType = processType;
    processType->fields["unref"] = unrefType;
    
    // process.getActiveResourcesInfo(): string[]
    auto getActiveResourcesInfoType = std::make_shared<FunctionType>();
    getActiveResourcesInfoType->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    processType->fields["getActiveResourcesInfo"] = getActiveResourcesInfoType;
    
    // ========================================================================
    // Milestone 102.12: Memory Info
    // ========================================================================
    
    // process.constrainedMemory(): number | undefined
    auto constrainedMemoryType = std::make_shared<FunctionType>();
    constrainedMemoryType->returnType = std::make_shared<Type>(TypeKind::Any); // number | undefined
    processType->fields["constrainedMemory"] = constrainedMemoryType;
    
    // process.availableMemory(): number
    auto availableMemoryType = std::make_shared<FunctionType>();
    availableMemoryType->returnType = std::make_shared<Type>(TypeKind::Int);
    processType->fields["availableMemory"] = availableMemoryType;
    
    // ========================================================================
    // Milestone 102.13: Internal/Debug APIs
    // ========================================================================
    
    // process._getActiveHandles(): any[]
    auto getActiveHandlesType = std::make_shared<FunctionType>();
    getActiveHandlesType->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
    processType->fields["_getActiveHandles"] = getActiveHandlesType;
    
    // process._getActiveRequests(): any[]
    auto getActiveRequestsType = std::make_shared<FunctionType>();
    getActiveRequestsType->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
    processType->fields["_getActiveRequests"] = getActiveRequestsType;
    
    // process._tickCallback(): void
    auto tickCallbackType = std::make_shared<FunctionType>();
    tickCallbackType->returnType = std::make_shared<Type>(TypeKind::Void);
    processType->fields["_tickCallback"] = tickCallbackType;
    
    // ========================================================================
    // Milestone 102.14: Diagnostics & Reporting
    // ========================================================================
    
    // process.report: object
    auto reportType = std::make_shared<ObjectType>();
    
    auto getReportType = std::make_shared<FunctionType>();
    getReportType->returnType = std::make_shared<Type>(TypeKind::Any);
    reportType->fields["getReport"] = getReportType;
    
    auto writeReportType = std::make_shared<FunctionType>();
    writeReportType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeReportType->returnType = std::make_shared<Type>(TypeKind::Void);
    reportType->fields["writeReport"] = writeReportType;
    
    reportType->fields["directory"] = std::make_shared<Type>(TypeKind::String);
    reportType->fields["filename"] = std::make_shared<Type>(TypeKind::String);
    
    processType->fields["report"] = reportType;

    symbols.define("process", processType);
}

} // namespace ts
