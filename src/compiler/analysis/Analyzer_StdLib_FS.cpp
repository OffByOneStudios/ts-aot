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
    
    // fs.readFileSync(path: string): string
    auto readFileSync = std::make_shared<FunctionType>();
    readFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    readFileSync->returnType = std::make_shared<Type>(TypeKind::String);
    fsType->fields["readFileSync"] = readFileSync;

    // fs.writeFileSync(path: string, data: string): void
    auto writeFileSync = std::make_shared<FunctionType>();
    writeFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeFileSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeFileSync->returnType = std::make_shared<Type>(TypeKind::Void);
    fsType->fields["writeFileSync"] = writeFileSync;

    // fs.existsSync(path: string): boolean
    auto existsSync = std::make_shared<FunctionType>();
    existsSync->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    existsSync->returnType = std::make_shared<Type>(TypeKind::Boolean);
    fsType->fields["existsSync"] = existsSync;

    // fs.createReadStream(path: string): ReadStream
    auto createReadStream = std::make_shared<FunctionType>();
    createReadStream->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    createReadStream->returnType = std::make_shared<Type>(TypeKind::Any); // TODO: Define ReadStream type
    fsType->fields["createReadStream"] = createReadStream;

    // fs.createWriteStream(path: string): WriteStream
    auto createWriteStream = std::make_shared<FunctionType>();
    createWriteStream->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    createWriteStream->returnType = std::make_shared<Type>(TypeKind::Any); // TODO: Define WriteStream type
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

    // fs.statSync(path: string): any
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

    // fs.constants
    auto constants = std::make_shared<ObjectType>();
    constants->fields["F_OK"] = std::make_shared<Type>(TypeKind::Double);
    constants->fields["R_OK"] = std::make_shared<Type>(TypeKind::Double);
    constants->fields["W_OK"] = std::make_shared<Type>(TypeKind::Double);
    constants->fields["X_OK"] = std::make_shared<Type>(TypeKind::Double);
    fsType->fields["constants"] = constants;

    // fs.promises
    auto promises = std::make_shared<ObjectType>();
    
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

    fsType->fields["promises"] = promises;

    symbols.define("fs", fsType);
}

} // namespace ts
