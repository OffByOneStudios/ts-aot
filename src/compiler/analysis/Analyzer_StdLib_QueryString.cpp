#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerQueryString() {
    auto querystringType = std::make_shared<ObjectType>();

    // =========================================================================
    // querystring.parse(str: string, sep?: string, eq?: string): object
    // Parse a query string into an object
    // =========================================================================
    auto parseType = std::make_shared<FunctionType>();
    parseType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    parseType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // sep
    parseType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // eq
    parseType->isOptional = { false, true, true };
    parseType->returnType = std::make_shared<Type>(TypeKind::Any);  // Object with arbitrary keys
    querystringType->fields["parse"] = parseType;

    // =========================================================================
    // querystring.stringify(obj: object, sep?: string, eq?: string): string
    // Stringify an object to a query string
    // =========================================================================
    auto stringifyType = std::make_shared<FunctionType>();
    stringifyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // Object
    stringifyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // sep
    stringifyType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // eq
    stringifyType->isOptional = { false, true, true };
    stringifyType->returnType = std::make_shared<Type>(TypeKind::String);
    querystringType->fields["stringify"] = stringifyType;

    // =========================================================================
    // querystring.escape(str: string): string
    // Percent-encode a string for use in a query string
    // =========================================================================
    auto escapeType = std::make_shared<FunctionType>();
    escapeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    escapeType->returnType = std::make_shared<Type>(TypeKind::String);
    querystringType->fields["escape"] = escapeType;

    // =========================================================================
    // querystring.unescape(str: string): string
    // Decode percent-encoded characters in a string
    // =========================================================================
    auto unescapeType = std::make_shared<FunctionType>();
    unescapeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    unescapeType->returnType = std::make_shared<Type>(TypeKind::String);
    querystringType->fields["unescape"] = unescapeType;

    // =========================================================================
    // Aliases
    // querystring.decode = querystring.parse
    // querystring.encode = querystring.stringify
    // =========================================================================
    querystringType->fields["decode"] = parseType;
    querystringType->fields["encode"] = stringifyType;

    symbols.define("querystring", querystringType);
}

} // namespace ts
