#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerPath() {
    auto pathType = std::make_shared<ObjectType>();

    // path.join(...paths: string[]): string
    auto joinType = std::make_shared<FunctionType>();
    joinType->hasRest = true;
    joinType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    joinType->returnType = std::make_shared<Type>(TypeKind::String);
    pathType->fields["join"] = joinType;

    // path.resolve(...paths: string[]): string
    auto resolveType = std::make_shared<FunctionType>();
    resolveType->hasRest = true;
    resolveType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    resolveType->returnType = std::make_shared<Type>(TypeKind::String);
    pathType->fields["resolve"] = resolveType;

    // path.normalize(path: string): string
    auto normalizeType = std::make_shared<FunctionType>();
    normalizeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    normalizeType->returnType = std::make_shared<Type>(TypeKind::String);
    pathType->fields["normalize"] = normalizeType;

    // path.isAbsolute(path: string): boolean
    auto isAbsoluteType = std::make_shared<FunctionType>();
    isAbsoluteType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    isAbsoluteType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    pathType->fields["isAbsolute"] = isAbsoluteType;

    // path.basename(path: string, ext?: string): string
    auto basenameType = std::make_shared<FunctionType>();
    basenameType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    basenameType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    basenameType->isOptional = { false, true };
    basenameType->returnType = std::make_shared<Type>(TypeKind::String);
    pathType->fields["basename"] = basenameType;

    // path.dirname(path: string): string
    auto dirnameType = std::make_shared<FunctionType>();
    dirnameType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    dirnameType->returnType = std::make_shared<Type>(TypeKind::String);
    pathType->fields["dirname"] = dirnameType;

    // path.extname(path: string): string
    auto extnameType = std::make_shared<FunctionType>();
    extnameType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    extnameType->returnType = std::make_shared<Type>(TypeKind::String);
    pathType->fields["extname"] = extnameType;

    // path.relative(from: string, to: string): string
    auto relativeType = std::make_shared<FunctionType>();
    relativeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    relativeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    relativeType->returnType = std::make_shared<Type>(TypeKind::String);
    pathType->fields["relative"] = relativeType;

    // path.parse(path: string): any
    auto parseType = std::make_shared<FunctionType>();
    parseType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    parseType->returnType = std::make_shared<Type>(TypeKind::Any);
    pathType->fields["parse"] = parseType;

    // path.format(pathObject: any): string
    auto formatType = std::make_shared<FunctionType>();
    formatType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    formatType->returnType = std::make_shared<Type>(TypeKind::String);
    pathType->fields["format"] = formatType;

    // path.toNamespacedPath(path: string): string
    auto toNamespacedPathType = std::make_shared<FunctionType>();
    toNamespacedPathType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    toNamespacedPathType->returnType = std::make_shared<Type>(TypeKind::String);
    pathType->fields["toNamespacedPath"] = toNamespacedPathType;

    // path.sep: string
    pathType->fields["sep"] = std::make_shared<Type>(TypeKind::String);

    // path.delimiter: string
    pathType->fields["delimiter"] = std::make_shared<Type>(TypeKind::String);

    // path.win32: any
    pathType->fields["win32"] = std::make_shared<Type>(TypeKind::Any);

    // path.posix: any
    pathType->fields["posix"] = std::make_shared<Type>(TypeKind::Any);

    symbols.define("path", pathType);
}

} // namespace ts
