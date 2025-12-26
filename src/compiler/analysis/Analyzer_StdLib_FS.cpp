#include "Analyzer.h"
#include "../ast/AstLoader.h"

namespace ts {

using namespace ast;

void Analyzer::registerFS() {
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

    fsType->fields["promises"] = promises;

    symbols.define("fs", fsType);
}

} // namespace ts
