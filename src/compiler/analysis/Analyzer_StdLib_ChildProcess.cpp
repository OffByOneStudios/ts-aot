#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerChildProcess() {
    auto childProcessModule = std::make_shared<ObjectType>();

    // =========================================================================
    // ChildProcess class
    // Represents a spawned child process
    // =========================================================================
    auto childProcessClass = std::make_shared<ClassType>("ChildProcess");

    // Properties
    childProcessClass->fields["pid"] = std::make_shared<Type>(TypeKind::Int);
    childProcessClass->fields["connected"] = std::make_shared<Type>(TypeKind::Boolean);
    childProcessClass->fields["killed"] = std::make_shared<Type>(TypeKind::Boolean);
    childProcessClass->fields["exitCode"] = std::make_shared<Type>(TypeKind::Any);  // number | null
    childProcessClass->fields["signalCode"] = std::make_shared<Type>(TypeKind::Any);  // string | null
    childProcessClass->fields["spawnfile"] = std::make_shared<Type>(TypeKind::String);
    childProcessClass->fields["spawnargs"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));

    // Get Writable and Readable types for stdio streams
    auto writableClass = std::make_shared<ClassType>("Writable");
    auto readableClass = std::make_shared<ClassType>("Readable");

    // Stdio streams
    childProcessClass->fields["stdin"] = writableClass;   // Writable | null
    childProcessClass->fields["stdout"] = readableClass;  // Readable | null
    childProcessClass->fields["stderr"] = readableClass;  // Readable | null

    // stdio array [stdin, stdout, stderr]
    childProcessClass->fields["stdio"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));

    // Channel for IPC
    childProcessClass->fields["channel"] = std::make_shared<Type>(TypeKind::Any);  // Pipe | null

    // Methods
    // kill(signal?: string | number): boolean
    auto killMethod = std::make_shared<FunctionType>();
    killMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // signal (string or number)
    killMethod->isOptional = { true };
    killMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    childProcessClass->methods["kill"] = killMethod;

    // send(message: any, sendHandle?: any): boolean
    auto sendMethod = std::make_shared<FunctionType>();
    sendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // message
    sendMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // sendHandle
    sendMethod->isOptional = { false, true };
    sendMethod->returnType = std::make_shared<Type>(TypeKind::Boolean);
    childProcessClass->methods["send"] = sendMethod;

    // disconnect(): void
    auto disconnectMethod = std::make_shared<FunctionType>();
    disconnectMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    childProcessClass->methods["disconnect"] = disconnectMethod;

    // ref(): this
    auto refMethod = std::make_shared<FunctionType>();
    refMethod->returnType = childProcessClass;
    childProcessClass->methods["ref"] = refMethod;

    // unref(): this
    auto unrefMethod = std::make_shared<FunctionType>();
    unrefMethod->returnType = childProcessClass;
    childProcessClass->methods["unref"] = unrefMethod;

    // EventEmitter methods (inherited)
    // on(event: string, listener: Function): this
    auto onMethod = std::make_shared<FunctionType>();
    onMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    onMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    onMethod->returnType = childProcessClass;
    childProcessClass->methods["on"] = onMethod;

    // once(event: string, listener: Function): this
    auto onceMethod = std::make_shared<FunctionType>();
    onceMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    onceMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    onceMethod->returnType = childProcessClass;
    childProcessClass->methods["once"] = onceMethod;

    // Register the class
    symbols.defineType("ChildProcess", childProcessClass);
    childProcessModule->fields["ChildProcess"] = childProcessClass;

    // =========================================================================
    // Module-level functions
    // =========================================================================

    // spawn(command: string, args?: string[], options?: SpawnOptions): ChildProcess
    auto spawnType = std::make_shared<FunctionType>();
    spawnType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // command
    spawnType->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));  // args
    spawnType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    spawnType->isOptional = { false, true, true };
    spawnType->returnType = childProcessClass;
    childProcessModule->fields["spawn"] = spawnType;

    // exec(command: string, options?: ExecOptions, callback?: Function): ChildProcess
    auto execType = std::make_shared<FunctionType>();
    execType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // command
    execType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    execType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));  // callback
    execType->isOptional = { false, true, true };
    execType->returnType = childProcessClass;
    childProcessModule->fields["exec"] = execType;

    // execSync(command: string, options?: ExecSyncOptions): Buffer | string
    auto execSyncType = std::make_shared<FunctionType>();
    execSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // command
    execSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    execSyncType->isOptional = { false, true };
    execSyncType->returnType = std::make_shared<ClassType>("Buffer");
    childProcessModule->fields["execSync"] = execSyncType;

    // execFile(file: string, args?: string[], options?: ExecFileOptions, callback?: Function): ChildProcess
    auto execFileType = std::make_shared<FunctionType>();
    execFileType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // file
    execFileType->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));  // args
    execFileType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    execFileType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));  // callback
    execFileType->isOptional = { false, true, true, true };
    execFileType->returnType = childProcessClass;
    childProcessModule->fields["execFile"] = execFileType;

    // execFileSync(file: string, args?: string[], options?: ExecFileSyncOptions): Buffer | string
    auto execFileSyncType = std::make_shared<FunctionType>();
    execFileSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // file
    execFileSyncType->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));  // args
    execFileSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    execFileSyncType->isOptional = { false, true, true };
    execFileSyncType->returnType = std::make_shared<ClassType>("Buffer");
    childProcessModule->fields["execFileSync"] = execFileSyncType;

    // fork(modulePath: string, args?: string[], options?: ForkOptions): ChildProcess
    auto forkType = std::make_shared<FunctionType>();
    forkType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // modulePath
    forkType->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));  // args
    forkType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    forkType->isOptional = { false, true, true };
    forkType->returnType = childProcessClass;
    childProcessModule->fields["fork"] = forkType;

    // spawnSync(command: string, args?: string[], options?: SpawnSyncOptions): SpawnSyncReturns
    // Returns an object with pid, output, stdout, stderr, status, signal, error
    auto spawnSyncReturnType = std::make_shared<ObjectType>();
    spawnSyncReturnType->fields["pid"] = std::make_shared<Type>(TypeKind::Int);
    spawnSyncReturnType->fields["output"] = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
    spawnSyncReturnType->fields["stdout"] = std::make_shared<ClassType>("Buffer");
    spawnSyncReturnType->fields["stderr"] = std::make_shared<ClassType>("Buffer");
    spawnSyncReturnType->fields["status"] = std::make_shared<Type>(TypeKind::Any);  // number | null
    spawnSyncReturnType->fields["signal"] = std::make_shared<Type>(TypeKind::Any);  // string | null
    spawnSyncReturnType->fields["error"] = std::make_shared<Type>(TypeKind::Any);  // Error | undefined

    auto spawnSyncType = std::make_shared<FunctionType>();
    spawnSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // command
    spawnSyncType->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));  // args
    spawnSyncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options
    spawnSyncType->isOptional = { false, true, true };
    spawnSyncType->returnType = spawnSyncReturnType;
    childProcessModule->fields["spawnSync"] = spawnSyncType;

    // Register the child_process module
    symbols.define("child_process", childProcessModule);
}

} // namespace ts
