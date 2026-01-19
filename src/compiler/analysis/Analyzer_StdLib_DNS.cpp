#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerDNS() {
    auto dnsModule = std::make_shared<ObjectType>();

    // =========================================================================
    // Types used by DNS module
    // =========================================================================

    // LookupOptions interface
    auto lookupOptionsType = std::make_shared<ObjectType>();
    lookupOptionsType->fields["family"] = std::make_shared<Type>(TypeKind::Int);     // 0 | 4 | 6
    lookupOptionsType->fields["hints"] = std::make_shared<Type>(TypeKind::Int);      // AI_* flags
    lookupOptionsType->fields["all"] = std::make_shared<Type>(TypeKind::Boolean);    // Return all addresses
    lookupOptionsType->fields["verbatim"] = std::make_shared<Type>(TypeKind::Boolean);

    // LookupResult interface (when all: false)
    auto lookupResultType = std::make_shared<ObjectType>();
    lookupResultType->fields["address"] = std::make_shared<Type>(TypeKind::String);
    lookupResultType->fields["family"] = std::make_shared<Type>(TypeKind::Int);

    // LookupResult[] (when all: true)
    auto lookupResultArrayType = std::make_shared<ArrayType>(lookupResultType);

    // =========================================================================
    // Callback types
    // =========================================================================

    // lookup callback: (err: Error | null, address: string, family: number) => void
    auto lookupCallbackType = std::make_shared<FunctionType>();
    lookupCallbackType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // err
    lookupCallbackType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // address
    lookupCallbackType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));  // family
    lookupCallbackType->returnType = std::make_shared<Type>(TypeKind::Void);

    // resolve callback: (err: Error | null, addresses: string[]) => void
    auto resolveCallbackType = std::make_shared<FunctionType>();
    resolveCallbackType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // err
    resolveCallbackType->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));  // addresses
    resolveCallbackType->returnType = std::make_shared<Type>(TypeKind::Void);

    // reverse callback: (err: Error | null, hostnames: string[]) => void
    auto reverseCallbackType = std::make_shared<FunctionType>();
    reverseCallbackType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // err
    reverseCallbackType->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));  // hostnames
    reverseCallbackType->returnType = std::make_shared<Type>(TypeKind::Void);

    // =========================================================================
    // DNS module methods
    // =========================================================================

    // lookup(hostname: string, callback: (err, address, family) => void): void
    // lookup(hostname: string, options: LookupOptions, callback: (err, address, family) => void): void
    auto lookupMethod = std::make_shared<FunctionType>();
    lookupMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // hostname
    lookupMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));     // options or callback
    lookupMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));     // callback (optional)
    lookupMethod->isOptional = { false, false, true };
    lookupMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["lookup"] = lookupMethod;

    // resolve(hostname: string, callback: (err, addresses) => void): void
    auto resolveMethod = std::make_shared<FunctionType>();
    resolveMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // hostname
    resolveMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));     // callback
    resolveMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolve"] = resolveMethod;

    // resolve4(hostname: string, callback: (err, addresses) => void): void
    auto resolve4Method = std::make_shared<FunctionType>();
    resolve4Method->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // hostname
    resolve4Method->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));     // callback
    resolve4Method->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolve4"] = resolve4Method;

    // resolve6(hostname: string, callback: (err, addresses) => void): void
    auto resolve6Method = std::make_shared<FunctionType>();
    resolve6Method->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // hostname
    resolve6Method->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));     // callback
    resolve6Method->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolve6"] = resolve6Method;

    // reverse(ip: string, callback: (err, hostnames) => void): void
    auto reverseMethod = std::make_shared<FunctionType>();
    reverseMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // ip
    reverseMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));     // callback
    reverseMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["reverse"] = reverseMethod;

    // getServers(): string[]
    auto getServersMethod = std::make_shared<FunctionType>();
    getServersMethod->returnType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
    dnsModule->fields["getServers"] = getServersMethod;

    // setServers(servers: string[]): void
    auto setServersMethod = std::make_shared<FunctionType>();
    setServersMethod->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));
    setServersMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["setServers"] = setServersMethod;

    // =========================================================================
    // dns.promises namespace
    // =========================================================================
    auto dnsPromises = std::make_shared<ObjectType>();

    // Promise<LookupResult> type - just use ClassType("Promise") without type params
    auto lookupPromiseType = std::make_shared<ClassType>("Promise");

    // promises.lookup(hostname: string, options?: LookupOptions): Promise<{address, family}>
    auto promisesLookupMethod = std::make_shared<FunctionType>();
    promisesLookupMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // hostname
    promisesLookupMethod->paramTypes.push_back(lookupOptionsType);  // options
    promisesLookupMethod->isOptional = { false, true };
    promisesLookupMethod->returnType = lookupPromiseType;
    dnsPromises->fields["lookup"] = promisesLookupMethod;

    // promises.resolve(hostname: string): Promise<string[]>
    auto promisesResolveMethod = std::make_shared<FunctionType>();
    promisesResolveMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolveMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolve"] = promisesResolveMethod;

    // promises.resolve4(hostname: string): Promise<string[]>
    auto promisesResolve4Method = std::make_shared<FunctionType>();
    promisesResolve4Method->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolve4Method->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolve4"] = promisesResolve4Method;

    // promises.resolve6(hostname: string): Promise<string[]>
    auto promisesResolve6Method = std::make_shared<FunctionType>();
    promisesResolve6Method->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolve6Method->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolve6"] = promisesResolve6Method;

    // promises.reverse(ip: string): Promise<string[]>
    auto promisesReverseMethod = std::make_shared<FunctionType>();
    promisesReverseMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesReverseMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["reverse"] = promisesReverseMethod;

    dnsModule->fields["promises"] = dnsPromises;

    // =========================================================================
    // DNS Error codes (constants)
    // =========================================================================
    dnsModule->fields["NODATA"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["FORMERR"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["SERVFAIL"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["NOTFOUND"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["NOTIMP"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["REFUSED"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["BADQUERY"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["BADNAME"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["BADFAMILY"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["BADRESP"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["CONNREFUSED"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["TIMEOUT"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["EOF"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["FILE"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["NOMEM"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["DESTRUCTION"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["BADSTR"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["BADFLAGS"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["NONAME"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["BADHINTS"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["NOTINITIALIZED"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["LOADIPHLPAPI"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["ADDRGETNETWORKPARAMS"] = std::make_shared<Type>(TypeKind::Int);
    dnsModule->fields["CANCELLED"] = std::make_shared<Type>(TypeKind::Int);

    // Register the dns module
    symbols.define("dns", dnsModule);
}

} // namespace ts
