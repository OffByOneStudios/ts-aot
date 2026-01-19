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

    // resolveCname(hostname: string, callback: (err, addresses) => void): void
    auto resolveCnameMethod = std::make_shared<FunctionType>();
    resolveCnameMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    resolveCnameMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    resolveCnameMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolveCname"] = resolveCnameMethod;

    // resolveMx(hostname: string, callback: (err, addresses) => void): void
    auto resolveMxMethod = std::make_shared<FunctionType>();
    resolveMxMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    resolveMxMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    resolveMxMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolveMx"] = resolveMxMethod;

    // resolveNs(hostname: string, callback: (err, addresses) => void): void
    auto resolveNsMethod = std::make_shared<FunctionType>();
    resolveNsMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    resolveNsMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    resolveNsMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolveNs"] = resolveNsMethod;

    // resolveTxt(hostname: string, callback: (err, addresses) => void): void
    auto resolveTxtMethod = std::make_shared<FunctionType>();
    resolveTxtMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    resolveTxtMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    resolveTxtMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolveTxt"] = resolveTxtMethod;

    // resolveSrv(hostname: string, callback: (err, addresses) => void): void
    auto resolveSrvMethod = std::make_shared<FunctionType>();
    resolveSrvMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    resolveSrvMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    resolveSrvMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolveSrv"] = resolveSrvMethod;

    // resolvePtr(hostname: string, callback: (err, addresses) => void): void
    auto resolvePtrMethod = std::make_shared<FunctionType>();
    resolvePtrMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    resolvePtrMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    resolvePtrMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolvePtr"] = resolvePtrMethod;

    // resolveNaptr(hostname: string, callback: (err, addresses) => void): void
    auto resolveNaptrMethod = std::make_shared<FunctionType>();
    resolveNaptrMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    resolveNaptrMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    resolveNaptrMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolveNaptr"] = resolveNaptrMethod;

    // resolveSoa(hostname: string, callback: (err, soa) => void): void
    auto resolveSoaMethod = std::make_shared<FunctionType>();
    resolveSoaMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    resolveSoaMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    resolveSoaMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    dnsModule->fields["resolveSoa"] = resolveSoaMethod;

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

    // promises.resolveCname(hostname: string): Promise<string[]>
    auto promisesResolveCnameMethod = std::make_shared<FunctionType>();
    promisesResolveCnameMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolveCnameMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolveCname"] = promisesResolveCnameMethod;

    // promises.resolveMx(hostname: string): Promise<{exchange, priority}[]>
    auto promisesResolveMxMethod = std::make_shared<FunctionType>();
    promisesResolveMxMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolveMxMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolveMx"] = promisesResolveMxMethod;

    // promises.resolveNs(hostname: string): Promise<string[]>
    auto promisesResolveNsMethod = std::make_shared<FunctionType>();
    promisesResolveNsMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolveNsMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolveNs"] = promisesResolveNsMethod;

    // promises.resolveTxt(hostname: string): Promise<string[][]>
    auto promisesResolveTxtMethod = std::make_shared<FunctionType>();
    promisesResolveTxtMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolveTxtMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolveTxt"] = promisesResolveTxtMethod;

    // promises.resolveSrv(hostname: string): Promise<{name, port, priority, weight}[]>
    auto promisesResolveSrvMethod = std::make_shared<FunctionType>();
    promisesResolveSrvMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolveSrvMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolveSrv"] = promisesResolveSrvMethod;

    // promises.resolvePtr(hostname: string): Promise<string[]>
    auto promisesResolvePtrMethod = std::make_shared<FunctionType>();
    promisesResolvePtrMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolvePtrMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolvePtr"] = promisesResolvePtrMethod;

    // promises.resolveNaptr(hostname: string): Promise<{flags, service, regexp, replacement, order, preference}[]>
    auto promisesResolveNaptrMethod = std::make_shared<FunctionType>();
    promisesResolveNaptrMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolveNaptrMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolveNaptr"] = promisesResolveNaptrMethod;

    // promises.resolveSoa(hostname: string): Promise<{nsname, hostmaster, serial, refresh, retry, expire, minttl}>
    auto promisesResolveSoaMethod = std::make_shared<FunctionType>();
    promisesResolveSoaMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    promisesResolveSoaMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["resolveSoa"] = promisesResolveSoaMethod;

    // promises.getServers(): Promise<string[]>
    auto promisesGetServersMethod = std::make_shared<FunctionType>();
    promisesGetServersMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["getServers"] = promisesGetServersMethod;

    // promises.setServers(servers: string[]): Promise<void>
    auto promisesSetServersMethod = std::make_shared<FunctionType>();
    promisesSetServersMethod->paramTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String)));
    promisesSetServersMethod->returnType = std::make_shared<ClassType>("Promise");
    dnsPromises->fields["setServers"] = promisesSetServersMethod;

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
