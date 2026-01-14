#include "Analyzer.h"

namespace ts {

void Analyzer::registerOS() {
    auto osType = std::make_shared<ObjectType>();

    // ========================================================================
    // Basic System Info
    // ========================================================================

    // os.platform(): string - returns process.platform (e.g., 'win32', 'linux', 'darwin')
    auto platformType = std::make_shared<FunctionType>();
    platformType->returnType = std::make_shared<Type>(TypeKind::String);
    osType->fields["platform"] = platformType;

    // os.arch(): string - returns process.arch (e.g., 'x64', 'arm64')
    auto archType = std::make_shared<FunctionType>();
    archType->returnType = std::make_shared<Type>(TypeKind::String);
    osType->fields["arch"] = archType;

    // os.type(): string - returns 'Windows_NT', 'Linux', 'Darwin'
    auto typeType = std::make_shared<FunctionType>();
    typeType->returnType = std::make_shared<Type>(TypeKind::String);
    osType->fields["type"] = typeType;

    // os.release(): string - OS release version
    auto releaseType = std::make_shared<FunctionType>();
    releaseType->returnType = std::make_shared<Type>(TypeKind::String);
    osType->fields["release"] = releaseType;

    // os.version(): string - kernel version
    auto versionType = std::make_shared<FunctionType>();
    versionType->returnType = std::make_shared<Type>(TypeKind::String);
    osType->fields["version"] = versionType;

    // os.hostname(): string
    auto hostnameType = std::make_shared<FunctionType>();
    hostnameType->returnType = std::make_shared<Type>(TypeKind::String);
    osType->fields["hostname"] = hostnameType;

    // os.machine(): string - e.g., 'x86_64', 'arm64'
    auto machineType = std::make_shared<FunctionType>();
    machineType->returnType = std::make_shared<Type>(TypeKind::String);
    osType->fields["machine"] = machineType;

    // ========================================================================
    // Paths
    // ========================================================================

    // os.homedir(): string
    auto homedirType = std::make_shared<FunctionType>();
    homedirType->returnType = std::make_shared<Type>(TypeKind::String);
    osType->fields["homedir"] = homedirType;

    // os.tmpdir(): string
    auto tmpdirType = std::make_shared<FunctionType>();
    tmpdirType->returnType = std::make_shared<Type>(TypeKind::String);
    osType->fields["tmpdir"] = tmpdirType;

    // os.devNull: string - '/dev/null' on POSIX, '\\\\.\\nul' on Windows
    osType->fields["devNull"] = std::make_shared<Type>(TypeKind::String);

    // ========================================================================
    // Line Ending
    // ========================================================================

    // os.EOL: string - '\r\n' on Windows, '\n' on POSIX
    osType->fields["EOL"] = std::make_shared<Type>(TypeKind::String);

    // ========================================================================
    // Memory
    // ========================================================================

    // os.totalmem(): number
    auto totalmemType = std::make_shared<FunctionType>();
    totalmemType->returnType = std::make_shared<Type>(TypeKind::Int);
    osType->fields["totalmem"] = totalmemType;

    // os.freemem(): number
    auto freememType = std::make_shared<FunctionType>();
    freememType->returnType = std::make_shared<Type>(TypeKind::Int);
    osType->fields["freemem"] = freememType;

    // ========================================================================
    // System Info
    // ========================================================================

    // os.uptime(): number
    auto uptimeType = std::make_shared<FunctionType>();
    uptimeType->returnType = std::make_shared<Type>(TypeKind::Double);
    osType->fields["uptime"] = uptimeType;

    // os.loadavg(): number[] - returns [0, 0, 0] on Windows
    auto loadavgType = std::make_shared<FunctionType>();
    loadavgType->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Double));
    osType->fields["loadavg"] = loadavgType;

    // os.cpus(): CpuInfo[]
    auto cpuInfoType = std::make_shared<ObjectType>();
    cpuInfoType->fields["model"] = std::make_shared<Type>(TypeKind::String);
    cpuInfoType->fields["speed"] = std::make_shared<Type>(TypeKind::Int);

    auto timesType = std::make_shared<ObjectType>();
    timesType->fields["user"] = std::make_shared<Type>(TypeKind::Int);
    timesType->fields["nice"] = std::make_shared<Type>(TypeKind::Int);
    timesType->fields["sys"] = std::make_shared<Type>(TypeKind::Int);
    timesType->fields["idle"] = std::make_shared<Type>(TypeKind::Int);
    timesType->fields["irq"] = std::make_shared<Type>(TypeKind::Int);
    cpuInfoType->fields["times"] = timesType;

    auto cpusType = std::make_shared<FunctionType>();
    cpusType->returnType = std::make_shared<ArrayType>(cpuInfoType);
    osType->fields["cpus"] = cpusType;

    // os.endianness(): 'BE' | 'LE'
    auto endiannessType = std::make_shared<FunctionType>();
    endiannessType->returnType = std::make_shared<Type>(TypeKind::String);
    osType->fields["endianness"] = endiannessType;

    // ========================================================================
    // Network
    // ========================================================================

    // os.networkInterfaces(): { [name: string]: NetworkInterfaceInfo[] }
    auto networkInterfaceInfoType = std::make_shared<ObjectType>();
    networkInterfaceInfoType->fields["address"] = std::make_shared<Type>(TypeKind::String);
    networkInterfaceInfoType->fields["netmask"] = std::make_shared<Type>(TypeKind::String);
    networkInterfaceInfoType->fields["family"] = std::make_shared<Type>(TypeKind::String);
    networkInterfaceInfoType->fields["mac"] = std::make_shared<Type>(TypeKind::String);
    networkInterfaceInfoType->fields["internal"] = std::make_shared<Type>(TypeKind::Boolean);
    networkInterfaceInfoType->fields["cidr"] = std::make_shared<Type>(TypeKind::String);
    networkInterfaceInfoType->fields["scopeid"] = std::make_shared<Type>(TypeKind::Int);

    auto networkInterfacesType = std::make_shared<FunctionType>();
    networkInterfacesType->returnType = std::make_shared<ObjectType>(); // Generic object for now
    osType->fields["networkInterfaces"] = networkInterfacesType;

    // ========================================================================
    // User Info
    // ========================================================================

    // os.userInfo(options?: { encoding: string }): UserInfo
    auto userInfoReturnType = std::make_shared<ObjectType>();
    userInfoReturnType->fields["uid"] = std::make_shared<Type>(TypeKind::Int);
    userInfoReturnType->fields["gid"] = std::make_shared<Type>(TypeKind::Int);
    userInfoReturnType->fields["username"] = std::make_shared<Type>(TypeKind::String);
    userInfoReturnType->fields["homedir"] = std::make_shared<Type>(TypeKind::String);
    userInfoReturnType->fields["shell"] = std::make_shared<Type>(TypeKind::String);

    auto userInfoType = std::make_shared<FunctionType>();
    userInfoType->returnType = userInfoReturnType;
    osType->fields["userInfo"] = userInfoType;

    // ========================================================================
    // Priority
    // ========================================================================

    // os.getPriority(pid?: number): number
    auto getPriorityType = std::make_shared<FunctionType>();
    getPriorityType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    getPriorityType->returnType = std::make_shared<Type>(TypeKind::Int);
    osType->fields["getPriority"] = getPriorityType;

    // os.setPriority(pid: number, priority: number): void
    auto setPriorityType = std::make_shared<FunctionType>();
    setPriorityType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setPriorityType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    setPriorityType->returnType = std::make_shared<Type>(TypeKind::Void);
    osType->fields["setPriority"] = setPriorityType;

    // ========================================================================
    // Constants
    // ========================================================================

    auto constantsType = std::make_shared<ObjectType>();

    // os.constants.signals
    auto signalsType = std::make_shared<ObjectType>();
    signalsType->fields["SIGHUP"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGINT"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGQUIT"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGILL"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGTRAP"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGABRT"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGFPE"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGKILL"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGSEGV"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGPIPE"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGALRM"] = std::make_shared<Type>(TypeKind::Int);
    signalsType->fields["SIGTERM"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["signals"] = signalsType;

    // os.constants.errno - common error codes
    auto errnoType = std::make_shared<ObjectType>();
    errnoType->fields["ENOENT"] = std::make_shared<Type>(TypeKind::Int);
    errnoType->fields["EACCES"] = std::make_shared<Type>(TypeKind::Int);
    errnoType->fields["EEXIST"] = std::make_shared<Type>(TypeKind::Int);
    errnoType->fields["EPERM"] = std::make_shared<Type>(TypeKind::Int);
    errnoType->fields["ENOTEMPTY"] = std::make_shared<Type>(TypeKind::Int);
    constantsType->fields["errno"] = errnoType;

    osType->fields["constants"] = constantsType;

    symbols.define("os", osType);
}

} // namespace ts
