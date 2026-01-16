#include "Analyzer.h"
#include "../ast/AstLoader.h"

namespace ts {

using namespace ast;

void Analyzer::registerFS() {
    // Define Stats type
    auto statsType = std::make_shared<ObjectType>();
    statsType->fields["size"] = std::make_shared<Type>(TypeKind::Double);
    statsType->fields["mtimeMs"] = std::make_shared<Type>(TypeKind::Double);
    statsType->fields["atimeMs"] = std::make_shared<Type>(TypeKind::Double);
    statsType->fields["birthtimeMs"] = std::make_shared<Type>(TypeKind::Double);
    
    auto isFile = std::make_shared<FunctionType>();
    isFile->returnType = std::make_shared<Type>(TypeKind::Boolean);
    statsType->fields["isFile"] = isFile;
    
    auto isDirectory = std::make_shared<FunctionType>();
    isDirectory->returnType = std::make_shared<Type>(TypeKind::Boolean);
    statsType->fields["isDirectory"] = isDirectory;

    auto isSymbolicLink = std::make_shared<FunctionType>();
    isSymbolicLink->returnType = std::make_shared<Type>(TypeKind::Boolean);
    statsType->fields["isSymbolicLink"] = isSymbolicLink;

    auto isBlockDevice = std::make_shared<FunctionType>();
    isBlockDevice->returnType = std::make_shared<Type>(TypeKind::Boolean);
    statsType->fields["isBlockDevice"] = isBlockDevice;

    auto isCharacterDevice = std::make_shared<FunctionType>();
    isCharacterDevice->returnType = std::make_shared<Type>(TypeKind::Boolean);
    statsType->fields["isCharacterDevice"] = isCharacterDevice;

    auto isFIFO = std::make_shared<FunctionType>();
    isFIFO->returnType = std::make_shared<Type>(TypeKind::Boolean);
    statsType->fields["isFIFO"] = isFIFO;

    auto isSocket = std::make_shared<FunctionType>();
    isSocket->returnType = std::make_shared<Type>(TypeKind::Boolean);
    statsType->fields["isSocket"] = isSocket;

    // Define Dirent type
    auto direntType = std::make_shared<ObjectType>();
    direntType->fields["name"] = std::make_shared<Type>(TypeKind::String);
    direntType->fields["isBlockDevice"] = isFile;
    direntType->fields["isCharacterDevice"] = isFile;
    direntType->fields["isDirectory"] = isDirectory;
    direntType->fields["isFIFO"] = isFile;
    direntType->fields["isFile"] = isFile;
    direntType->fields["isSocket"] = isFile;
    direntType->fields["isSymbolicLink"] = isSymbolicLink;

    // Define Dir type
    auto dirType = std::make_shared<ObjectType>();
    dirType->fields["path"] = std::make_shared<Type>(TypeKind::String);
    
    auto dirReadSync = std::make_shared<FunctionType>();
    dirReadSync->returnType = direntType;
    dirType->fields["readSync"] = dirReadSync;

    auto dirReadAsync = std::make_shared<FunctionType>();
    dirReadAsync->returnType = std::make_shared<Type>(TypeKind::Any); // Promise<Dirent>
    dirType->fields["read"] = dirReadAsync;

    auto dirCloseSync = std::make_shared<FunctionType>();
    dirCloseSync->returnType = std::make_shared<Type>(TypeKind::Void);
    dirType->fields["closeSync"] = dirCloseSync;

    auto dirCloseAsync = std::make_shared<FunctionType>();
    dirCloseAsync->returnType = std::make_shared<Type>(TypeKind::Any); // Promise<void>
    dirType->fields["close"] = dirCloseAsync;

    // Define fs module
    auto fsType = std::make_shared<ObjectType>();
    
    // fs.readFileSync(path: string): Buffer
    auto readFileSync = std::make_shared<FunctionType>();
    readFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    auto bufferClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Buffer"));
    if (!bufferClass) {
        bufferClass = std::make_shared<ClassType>("Buffer");
        symbols.defineType("Buffer", bufferClass);
    }
    readFileSync->returnType = bufferClass;
    fsType->fields["readFileSync"] = readFileSync;

    // fs.writeFileSync(path: string, data: string): void
    auto writeFileSync = std::make_shared<FunctionType>();
    writeFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeFileSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["writeFileSync"] = writeFileSync;

    // fs.openSync(path: string, flags?: string|number, mode?: number): number
    auto openSync = std::make_shared<FunctionType>();
    openSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    openSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    openSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    openSync->returnType = std::make_shared<Type>(TypeKind::Double);
    fsType->fields["openSync"] = openSync;

    // fs.closeSync(fd: number): void
    auto closeSync = std::make_shared<FunctionType>();
    closeSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    closeSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["closeSync"] = closeSync;

    // fs.readSync(fd: number, buffer: any, offset?: number, length?: number, position?: number): number
    auto readSync = std::make_shared<FunctionType>();
    readSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    readSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    readSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    readSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    readSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    readSync->returnType = std::make_shared<Type>(TypeKind::Double);
    fsType->fields["readSync"] = readSync;

    // fs.writeSync(fd: number, buffer: any, offset?: number, length?: number, position?: number): number
    auto writeSync = std::make_shared<FunctionType>();
    writeSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    writeSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    writeSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    writeSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    writeSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    writeSync->returnType = std::make_shared<Type>(TypeKind::Double);
    fsType->fields["writeSync"] = writeSync;

    // fs.open(path: string, flags: any, mode: any, callback: any): void
    auto open = std::make_shared<FunctionType>();
    open->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    open->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    open->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    open->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    open->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["open"] = open;

    // fs.close(fd: number, callback: any): void
    auto close = std::make_shared<FunctionType>();
    close->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    close->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    close->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["close"] = close;

    // fs.read(fd: number, buffer: any, offset: number, length: number, position: number, callback: any): void
    auto read = std::make_shared<FunctionType>();
    read->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    read->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    read->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    read->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    read->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    read->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    read->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["read"] = read;

    // fs.write(fd: number, buffer: any, offset: number, length: number, position: number, callback: any): void
    auto write = std::make_shared<FunctionType>();
    write->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    write->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    write->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    write->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    write->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    write->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    write->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["write"] = write;

    // fs.existsSync(path: string): boolean
    auto existsSync = std::make_shared<FunctionType>();
    existsSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    existsSync->returnType = std::make_shared<Type>(TypeKind::Boolean);
    fsType->fields["existsSync"] = existsSync;

    // fs.createReadStream(path: string): ReadStream
    auto createReadStream = std::make_shared<FunctionType>();
    createReadStream->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    createReadStream->returnType = symbols.lookupType("ReadStream");
    fsType->fields["createReadStream"] = createReadStream;

    // fs.createWriteStream(path: string): WriteStream
    auto createWriteStream = std::make_shared<FunctionType>();
    createWriteStream->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    createWriteStream->returnType = symbols.lookupType("WriteStream");
    fsType->fields["createWriteStream"] = createWriteStream;

    // fs.accessSync(path: string, mode?: number): void
    auto accessSync = std::make_shared<FunctionType>();
    accessSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    accessSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    accessSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["accessSync"] = accessSync;

    // fs.chmodSync(path: string, mode: number): void
    auto chmodSync = std::make_shared<FunctionType>();
    chmodSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    chmodSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    chmodSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["chmodSync"] = chmodSync;

    // fs.chownSync(path: string, uid: number, gid: number): void
    auto chownSync = std::make_shared<FunctionType>();
    chownSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    chownSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    chownSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    chownSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["chownSync"] = chownSync;

    // fs.utimesSync(path: string, atime: number, mtime: number): void
    auto utimesSync = std::make_shared<FunctionType>();
    utimesSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    utimesSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    utimesSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    utimesSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["utimesSync"] = utimesSync;

    // fs.statfsSync(path: string): any
    auto statfsSync = std::make_shared<FunctionType>();
    statfsSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    statfsSync->returnType = std::make_shared<Type>(TypeKind::Any);
    fsType->fields["statfsSync"] = statfsSync;

    // fs.linkSync(existingPath: string, newPath: string): void
    auto linkSync = std::make_shared<FunctionType>();
    linkSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    linkSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    linkSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["linkSync"] = linkSync;

    // fs.symlinkSync(target: string, path: string, type?: string): void
    auto symlinkSync = std::make_shared<FunctionType>();
    symlinkSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    symlinkSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    symlinkSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    symlinkSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["symlinkSync"] = symlinkSync;

    // fs.readlinkSync(path: string): string
    auto readlinkSync = std::make_shared<FunctionType>();
    readlinkSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    readlinkSync->returnType = std::make_shared<Type>(TypeKind::String);
    fsType->fields["readlinkSync"] = readlinkSync;

    // fs.realpathSync(path: string): string
    auto realpathSync = std::make_shared<FunctionType>();
    realpathSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    realpathSync->returnType = std::make_shared<Type>(TypeKind::String);
    fsType->fields["realpathSync"] = realpathSync;

    // fs.fchmodSync(fd: number, mode: number): void
    auto fchmodSync = std::make_shared<FunctionType>();
    fchmodSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fchmodSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fchmodSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["fchmodSync"] = fchmodSync;

    // fs.fchownSync(fd: number, uid: number, gid: number): void
    auto fchownSync = std::make_shared<FunctionType>();
    fchownSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fchownSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fchownSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fchownSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["fchownSync"] = fchownSync;

    // fs.futimesSync(fd: number, atime: number, mtime: number): void
    auto futimesSync = std::make_shared<FunctionType>();
    futimesSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    futimesSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    futimesSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    futimesSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["futimesSync"] = futimesSync;

    // fs.fstatSync(fd: number): Stats
    auto fstatSync = std::make_shared<FunctionType>();
    fstatSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fstatSync->returnType = statsType;
    fsType->fields["fstatSync"] = fstatSync;

    // fs.fsyncSync(fd: number): void
    auto fsyncSync = std::make_shared<FunctionType>();
    fsyncSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fsyncSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["fsyncSync"] = fsyncSync;

    // fs.fdatasyncSync(fd: number): void
    auto fdatasyncSync = std::make_shared<FunctionType>();
    fdatasyncSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fdatasyncSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["fdatasyncSync"] = fdatasyncSync;

    // fs.ftruncateSync(fd: number, len?: number): void
    auto ftruncateSync = std::make_shared<FunctionType>();
    ftruncateSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    ftruncateSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    ftruncateSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["ftruncateSync"] = ftruncateSync;

    // fs.readvSync(fd: number, buffers: any[], position?: number): number
    auto readvSync = std::make_shared<FunctionType>();
    readvSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    readvSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array));
    readvSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    readvSync->returnType = std::make_shared<Type>(TypeKind::Double);
    fsType->fields["readvSync"] = readvSync;

    // fs.writevSync(fd: number, buffers: any[], position?: number): number
    auto writevSync = std::make_shared<FunctionType>();
    writevSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    writevSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array));
    writevSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    writevSync->returnType = std::make_shared<Type>(TypeKind::Double);
    fsType->fields["writevSync"] = writevSync;

    // fs.lstatSync(path: string): Stats
    auto statSync = std::make_shared<FunctionType>();
    statSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    statSync->returnType = statsType;
    fsType->fields["statSync"] = statSync;

    // fs.readdirSync(path: string): string[]
    auto readdirSync = std::make_shared<FunctionType>();
    readdirSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    readdirSync->returnType = std::make_shared<Type>(TypeKind::Any);
    fsType->fields["readdirSync"] = readdirSync;

    // fs.opendirSync(path: string, options?: any): Dir
    auto opendirSync = std::make_shared<FunctionType>();
    opendirSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    opendirSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    opendirSync->returnType = dirType;
    fsType->fields["opendirSync"] = opendirSync;

    // fs.lstatSync(path: string): any
    auto lstatSync = std::make_shared<FunctionType>();
    lstatSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    lstatSync->returnType = statsType;
    fsType->fields["lstatSync"] = lstatSync;

    // fs.unlinkSync(path: string): void
    auto unlinkSync = std::make_shared<FunctionType>();
    unlinkSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    unlinkSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["unlinkSync"] = unlinkSync;

    // FSWatcher type
    auto fsWatcherType = std::make_shared<ObjectType>();
    
    auto onFn = std::make_shared<FunctionType>();
    onFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    onFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    onFn->returnType = std::make_shared<Type>(TypeKind::Void);
    fsWatcherType->fields["on"] = onFn;
    
    auto closeFn = std::make_shared<FunctionType>();
    closeFn->returnType = std::make_shared<Type>(TypeKind::Void);
    fsWatcherType->fields["close"] = closeFn;

    // fs.watch(filename: string, options?: any, listener?: (eventType: string, filename: string) => void): FSWatcher
    auto watch = std::make_shared<FunctionType>();
    watch->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    watch->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    watch->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    watch->returnType = fsWatcherType;
    fsType->fields["watch"] = watch;

    // fs.watchFile(filename: string, options?: any, listener: (curr: Stats, prev: Stats) => void): void
    auto watchFile = std::make_shared<FunctionType>();
    watchFile->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    watchFile->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    watchFile->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    watchFile->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["watchFile"] = watchFile;

    // fs.unwatchFile(filename: string, listener?: any): void
    auto unwatchFile = std::make_shared<FunctionType>();
    unwatchFile->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    unwatchFile->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    unwatchFile->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["unwatchFile"] = unwatchFile;

    // fs.renameSync(oldPath: string, newPath: string): void
    auto renameSync = std::make_shared<FunctionType>();
    renameSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    renameSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    renameSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["renameSync"] = renameSync;

    // fs.copyFileSync(src: string, dest: string, flags?: number): void
    auto copyFileSync = std::make_shared<FunctionType>();
    copyFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    copyFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    copyFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    copyFileSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["copyFileSync"] = copyFileSync;

    // fs.truncateSync(path: string, len?: number): void
    auto truncateSync = std::make_shared<FunctionType>();
    truncateSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    truncateSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    truncateSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["truncateSync"] = truncateSync;

    // fs.appendFileSync(path: string, data: string): void
    auto appendFileSync = std::make_shared<FunctionType>();
    appendFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    appendFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    appendFileSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["appendFileSync"] = appendFileSync;

    // fs.mkdirSync(path: string, options?: any): void
    auto mkdirSync = std::make_shared<FunctionType>();
    mkdirSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    mkdirSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mkdirSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["mkdirSync"] = mkdirSync;

    // fs.rmdirSync(path: string, options?: any): void
    auto rmdirSync = std::make_shared<FunctionType>();
    rmdirSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    rmdirSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    rmdirSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["rmdirSync"] = rmdirSync;

    // fs.rmSync(path: string, options?: any): void
    auto rmSync = std::make_shared<FunctionType>();
    rmSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    rmSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    rmSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["rmSync"] = rmSync;

    // fs.mkdtempSync(prefix: string): string
    auto mkdtempSync = std::make_shared<FunctionType>();
    mkdtempSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    mkdtempSync->returnType = std::make_shared<Type>(TypeKind::String);
    fsType->fields["mkdtempSync"] = mkdtempSync;

    // fs.fchmod(fd: number, mode: number, callback: any): void
    auto fchmod = std::make_shared<FunctionType>();
    fchmod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fchmod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fchmod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    fchmod->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["fchmod"] = fchmod;

    // fs.fchown(fd: number, uid: number, gid: number, callback: any): void
    auto fchown = std::make_shared<FunctionType>();
    fchown->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fchown->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fchown->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fchown->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    fchown->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["fchown"] = fchown;

    // fs.futimes(fd: number, atime: number, mtime: number, callback: any): void
    auto futimes = std::make_shared<FunctionType>();
    futimes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    futimes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    futimes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    futimes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    futimes->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["futimes"] = futimes;

    // fs.fstat(fd: number, callback: any): void
    auto fstat = std::make_shared<FunctionType>();
    fstat->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fstat->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    fstat->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["fstat"] = fstat;

    // fs.fsync(fd: number, callback: any): void
    auto fsync = std::make_shared<FunctionType>();
    fsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    fsync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["fsync"] = fsync;

    // fs.fdatasync(fd: number, callback: any): void
    auto fdatasync = std::make_shared<FunctionType>();
    fdatasync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fdatasync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    fdatasync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["fdatasync"] = fdatasync;

    // fs.ftruncate(fd: number, len: number, callback: any): void
    auto ftruncate = std::make_shared<FunctionType>();
    ftruncate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    ftruncate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    ftruncate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    ftruncate->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["ftruncate"] = ftruncate;

    // fs.readv(fd: number, buffers: any[], position: number, callback: any): void
    auto readv = std::make_shared<FunctionType>();
    readv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    readv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array));
    readv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    readv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    readv->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["readv"] = readv;

    // fs.writev(fd: number, buffers: any[], position: number, callback: any): void
    auto writev = std::make_shared<FunctionType>();
    writev->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    writev->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array));
    writev->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    writev->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    writev->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["writev"] = writev;

    // fs.constants
    auto constants = std::make_shared<ObjectType>();
    constants->fields["F_OK"] = std::make_shared<Type>(TypeKind::Double);
    constants->fields["R_OK"] = std::make_shared<Type>(TypeKind::Double);
    constants->fields["W_OK"] = std::make_shared<Type>(TypeKind::Double);
    constants->fields["X_OK"] = std::make_shared<Type>(TypeKind::Double);
    fsType->fields["constants"] = constants;

    // FileHandle type
    auto fileHandleType = std::make_shared<ObjectType>();
    fileHandleType->fields["fd"] = std::make_shared<Type>(TypeKind::Double);
    
    auto fhClose = std::make_shared<FunctionType>();
    auto fhClosePromise = std::make_shared<ClassType>("Promise");
    fhClosePromise->typeArguments.push_back(std::make_shared<Type>(TypeKind::Void));
    fhClose->returnType = fhClosePromise;
    fileHandleType->fields["close"] = fhClose;

    auto fhStat = std::make_shared<FunctionType>();
    auto fhStatPromise = std::make_shared<ClassType>("Promise");
    fhStatPromise->typeArguments.push_back(statsType);
    fhStat->returnType = fhStatPromise;
    fileHandleType->fields["stat"] = fhStat;

    auto fhChmod = std::make_shared<FunctionType>();
    fhChmod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    auto fhChmodPromise = std::make_shared<ClassType>("Promise");
    fhChmodPromise->typeArguments.push_back(std::make_shared<Type>(TypeKind::Void));
    fhChmod->returnType = fhChmodPromise;
    fileHandleType->fields["chmod"] = fhChmod;

    auto fhChown = std::make_shared<FunctionType>();
    fhChown->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fhChown->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fhChown->returnType = fhChmodPromise;
    fileHandleType->fields["chown"] = fhChown;

    auto fhSync = std::make_shared<FunctionType>();
    fhSync->returnType = fhChmodPromise;
    fileHandleType->fields["sync"] = fhSync;

    auto fhDatasync = std::make_shared<FunctionType>();
    fhDatasync->returnType = fhChmodPromise;
    fileHandleType->fields["datasync"] = fhDatasync;

    auto fhTruncate = std::make_shared<FunctionType>();
    fhTruncate->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fhTruncate->returnType = fhChmodPromise;
    fileHandleType->fields["truncate"] = fhTruncate;

    auto fhUtimes = std::make_shared<FunctionType>();
    fhUtimes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fhUtimes->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    fhUtimes->returnType = fhChmodPromise;
    fileHandleType->fields["utimes"] = fhUtimes;

    auto fhRead = std::make_shared<FunctionType>();
    fhRead->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // Buffer
    fhRead->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // offset
    fhRead->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // length
    fhRead->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // position
    auto fhReadRes = std::make_shared<ObjectType>();
    fhReadRes->fields["bytesRead"] = std::make_shared<Type>(TypeKind::Double);
    fhReadRes->fields["buffer"] = std::make_shared<Type>(TypeKind::Any);
    auto fhReadPromise = std::make_shared<ClassType>("Promise");
    fhReadPromise->typeArguments.push_back(fhReadRes);
    fhRead->returnType = fhReadPromise;
    fileHandleType->fields["read"] = fhRead;

    auto fhWrite = std::make_shared<FunctionType>();
    fhWrite->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // Buffer
    fhWrite->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // offset
    fhWrite->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // length
    fhWrite->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // position
    auto fhWriteRes = std::make_shared<ObjectType>();
    fhWriteRes->fields["bytesWritten"] = std::make_shared<Type>(TypeKind::Double);
    fhWriteRes->fields["buffer"] = std::make_shared<Type>(TypeKind::Any);
    auto fhWritePromise = std::make_shared<ClassType>("Promise");
    fhWritePromise->typeArguments.push_back(fhWriteRes);
    fhWrite->returnType = fhWritePromise;
    fileHandleType->fields["write"] = fhWrite;

    auto fhReadv = std::make_shared<FunctionType>();
    fhReadv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array)); // buffers
    fhReadv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // position
    auto fhReadvRes = std::make_shared<ObjectType>();
    fhReadvRes->fields["bytesRead"] = std::make_shared<Type>(TypeKind::Double);
    fhReadvRes->fields["buffers"] = std::make_shared<Type>(TypeKind::Array);
    auto fhReadvPromise = std::make_shared<ClassType>("Promise");
    fhReadvPromise->typeArguments.push_back(fhReadvRes);
    fhReadv->returnType = fhReadvPromise;
    fileHandleType->fields["readv"] = fhReadv;

    auto fhWritev = std::make_shared<FunctionType>();
    fhWritev->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array)); // buffers
    fhWritev->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // position
    auto fhWritevRes = std::make_shared<ObjectType>();
    fhWritevRes->fields["bytesWritten"] = std::make_shared<Type>(TypeKind::Double);
    fhWritevRes->fields["buffers"] = std::make_shared<Type>(TypeKind::Array);
    auto fhWritevPromise = std::make_shared<ClassType>("Promise");
    fhWritevPromise->typeArguments.push_back(fhWritevRes);
    fhWritev->returnType = fhWritevPromise;
    fileHandleType->fields["writev"] = fhWritev;

    // fs.promises
    auto promises = std::make_shared<ObjectType>();
    
    auto openAsync = std::make_shared<FunctionType>();
    openAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    openAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    openAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    auto openPromise = std::make_shared<ClassType>("Promise");
    openPromise->typeArguments.push_back(fileHandleType);
    openAsync->returnType = openPromise;
    promises->fields["open"] = openAsync;

    auto readFileAsync = std::make_shared<FunctionType>();
    readFileAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    readFileAsync->returnType = std::make_shared<Type>(TypeKind::Any); // Promise<string|Buffer>
    promises->fields["readFile"] = readFileAsync;

    auto writeFileAsync = std::make_shared<FunctionType>();
    writeFileAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeFileAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeFileAsync->returnType = std::make_shared<Type>(TypeKind::Any); // Promise<void>
    promises->fields["writeFile"] = writeFileAsync;

    auto accessAsync = std::make_shared<FunctionType>();
    accessAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    accessAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    accessAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["access"] = accessAsync;

    auto chmodAsync = std::make_shared<FunctionType>();
    chmodAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    chmodAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    chmodAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["chmod"] = chmodAsync;

    auto chownAsync = std::make_shared<FunctionType>();
    chownAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    chownAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    chownAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    chownAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["chown"] = chownAsync;

    auto utimesAsync = std::make_shared<FunctionType>();
    utimesAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    utimesAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    utimesAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    utimesAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["utimes"] = utimesAsync;

    auto statfsAsync = std::make_shared<FunctionType>();
    statfsAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    statfsAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["statfs"] = statfsAsync;

    auto linkAsync = std::make_shared<FunctionType>();
    linkAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    linkAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    linkAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["link"] = linkAsync;

    auto symlinkAsync = std::make_shared<FunctionType>();
    symlinkAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    symlinkAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    symlinkAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    symlinkAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["symlink"] = symlinkAsync;

    auto readlinkAsync = std::make_shared<FunctionType>();
    readlinkAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    readlinkAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["readlink"] = readlinkAsync;

    auto realpathAsync = std::make_shared<FunctionType>();
    realpathAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    realpathAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["realpath"] = realpathAsync;

    auto lstatAsync = std::make_shared<FunctionType>();
    lstatAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    lstatAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["lstat"] = lstatAsync;

    auto statAsync = std::make_shared<FunctionType>();
    statAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    statAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["stat"] = statAsync;

    auto readdirAsync = std::make_shared<FunctionType>();
    readdirAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    readdirAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["readdir"] = readdirAsync;

    auto opendirAsync = std::make_shared<FunctionType>();
    opendirAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    opendirAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    opendirAsync->returnType = std::make_shared<Type>(TypeKind::Any); // Promise<Dir>
    promises->fields["opendir"] = opendirAsync;

    auto renameAsync = std::make_shared<FunctionType>();
    renameAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    renameAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    renameAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["rename"] = renameAsync;

    auto copyFileAsync = std::make_shared<FunctionType>();
    copyFileAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    copyFileAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    copyFileAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    copyFileAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["copyFile"] = copyFileAsync;

    auto truncateAsync = std::make_shared<FunctionType>();
    truncateAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    truncateAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double));
    truncateAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["truncate"] = truncateAsync;

    auto appendFileAsync = std::make_shared<FunctionType>();
    appendFileAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    appendFileAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    appendFileAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["appendFile"] = appendFileAsync;

    auto mkdirAsync = std::make_shared<FunctionType>();
    mkdirAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    mkdirAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    mkdirAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["mkdir"] = mkdirAsync;

    auto rmdirAsync = std::make_shared<FunctionType>();
    rmdirAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    rmdirAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    rmdirAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["rmdir"] = rmdirAsync;

    auto rmAsync = std::make_shared<FunctionType>();
    rmAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    rmAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    rmAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["rm"] = rmAsync;

    auto mkdtempAsync = std::make_shared<FunctionType>();
    mkdtempAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    mkdtempAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["mkdtemp"] = mkdtempAsync;

    auto unlinkAsync = std::make_shared<FunctionType>();
    unlinkAsync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    unlinkAsync->returnType = std::make_shared<Type>(TypeKind::Any);
    promises->fields["unlink"] = unlinkAsync;

    auto pReadv = std::make_shared<FunctionType>();
    pReadv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // handle
    pReadv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array)); // buffers
    pReadv->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // position
    pReadv->returnType = fhReadvPromise;
    promises->fields["readv"] = pReadv;

    auto pWritev = std::make_shared<FunctionType>();
    pWritev->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // handle
    pWritev->paramTypes.push_back(std::make_shared<Type>(TypeKind::Array)); // buffers
    pWritev->paramTypes.push_back(std::make_shared<Type>(TypeKind::Double)); // position
    pWritev->returnType = fhWritevPromise;
    promises->fields["writev"] = pWritev;

    fsType->fields["promises"] = promises;

    symbols.define("fs", fsType);
}

} // namespace ts
