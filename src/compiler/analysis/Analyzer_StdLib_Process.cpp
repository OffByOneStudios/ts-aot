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

    symbols.define("process", processType);
}

} // namespace ts
