#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register dns module's runtime functions once
static bool dnsFunctionsRegistered = false;
static void ensureDNSFunctionsRegistered(BoxingPolicy& bp) {
    if (dnsFunctionsRegistered) return;
    dnsFunctionsRegistered = true;

    // Callback-based APIs
    bp.registerRuntimeApi("ts_dns_lookup", {true, true, true}, false);
    bp.registerRuntimeApi("ts_dns_resolve", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_resolve4", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_resolve6", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_reverse", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_get_servers", {}, true);
    bp.registerRuntimeApi("ts_dns_set_servers", {true}, false);

    // Callback-based APIs (new record types)
    bp.registerRuntimeApi("ts_dns_resolve_cname", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_resolve_mx", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_resolve_ns", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_resolve_txt", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_resolve_srv", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_resolve_ptr", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_resolve_naptr", {true, true}, false);
    bp.registerRuntimeApi("ts_dns_resolve_soa", {true, true}, false);

    // Promise-based APIs
    bp.registerRuntimeApi("ts_dns_promises_lookup", {true, true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve4", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve6", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_reverse", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve_cname", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve_mx", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve_ns", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve_txt", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve_srv", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve_ptr", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve_naptr", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_resolve_soa", {true}, true);
    bp.registerRuntimeApi("ts_dns_promises_get_servers", {}, true);
    bp.registerRuntimeApi("ts_dns_promises_set_servers", {true}, true);

    // Error code constants
    bp.registerRuntimeApi("ts_dns_NODATA", {}, false);
    bp.registerRuntimeApi("ts_dns_FORMERR", {}, false);
    bp.registerRuntimeApi("ts_dns_SERVFAIL", {}, false);
    bp.registerRuntimeApi("ts_dns_NOTFOUND", {}, false);
    bp.registerRuntimeApi("ts_dns_NOTIMP", {}, false);
    bp.registerRuntimeApi("ts_dns_REFUSED", {}, false);
    bp.registerRuntimeApi("ts_dns_BADQUERY", {}, false);
    bp.registerRuntimeApi("ts_dns_BADNAME", {}, false);
    bp.registerRuntimeApi("ts_dns_BADFAMILY", {}, false);
    bp.registerRuntimeApi("ts_dns_BADRESP", {}, false);
    bp.registerRuntimeApi("ts_dns_CONNREFUSED", {}, false);
    bp.registerRuntimeApi("ts_dns_TIMEOUT", {}, false);
    bp.registerRuntimeApi("ts_dns_EOF", {}, false);
    bp.registerRuntimeApi("ts_dns_FILE", {}, false);
    bp.registerRuntimeApi("ts_dns_NOMEM", {}, false);
    bp.registerRuntimeApi("ts_dns_DESTRUCTION", {}, false);
    bp.registerRuntimeApi("ts_dns_BADSTR", {}, false);
    bp.registerRuntimeApi("ts_dns_BADFLAGS", {}, false);
    bp.registerRuntimeApi("ts_dns_NONAME", {}, false);
    bp.registerRuntimeApi("ts_dns_BADHINTS", {}, false);
    bp.registerRuntimeApi("ts_dns_NOTINITIALIZED", {}, false);
    bp.registerRuntimeApi("ts_dns_LOADIPHLPAPI", {}, false);
    bp.registerRuntimeApi("ts_dns_ADDRGETNETWORKPARAMS", {}, false);
    bp.registerRuntimeApi("ts_dns_CANCELLED", {}, false);
}

bool IRGenerator::tryGenerateDNSCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureDNSFunctionsRegistered(boxingPolicy);

    // Check if this is a dns.xxx call or dns.promises.xxx call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());

    // Check for dns.promises.xxx
    auto parentProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get());
    if (parentProp) {
        auto parentId = dynamic_cast<ast::Identifier*>(parentProp->expression.get());
        if (parentId && parentId->name == "dns" && parentProp->name == "promises") {
            return tryGenerateDNSPromisesCall(node, prop->name);
        }
    }

    if (!id || id->name != "dns") return false;

    SPDLOG_DEBUG("tryGenerateDNSCall: dns.{}", prop->name);

    const std::string& methodName = prop->name;

    // =========================================================================
    // dns.lookup(hostname, [options], callback)
    // =========================================================================
    if (methodName == "lookup") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get hostname
        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::Value* optionsOrCallback;
        llvm::Value* callbackOrNull;

        if (node->arguments.size() == 2) {
            // Two args: hostname, callback
            visit(node->arguments[1].get());
            optionsOrCallback = lastValue;
            if (!boxedValues.count(optionsOrCallback)) {
                optionsOrCallback = boxValue(optionsOrCallback, node->arguments[1]->inferredType);
            }
            callbackOrNull = llvm::ConstantPointerNull::get(builder->getPtrTy());
        } else if (node->arguments.size() >= 3) {
            // Three args: hostname, options, callback
            visit(node->arguments[1].get());
            optionsOrCallback = lastValue;
            if (!boxedValues.count(optionsOrCallback)) {
                optionsOrCallback = boxValue(optionsOrCallback, node->arguments[1]->inferredType);
            }
            visit(node->arguments[2].get());
            callbackOrNull = lastValue;
            if (!boxedValues.count(callbackOrNull)) {
                callbackOrNull = boxValue(callbackOrNull, node->arguments[2]->inferredType);
            }
        } else {
            optionsOrCallback = llvm::ConstantPointerNull::get(builder->getPtrTy());
            callbackOrNull = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_lookup", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, optionsOrCallback, callbackOrNull });
        return true;
    }

    // =========================================================================
    // dns.resolve(hostname, callback)
    // =========================================================================
    if (methodName == "resolve") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.resolve4(hostname, callback)
    // =========================================================================
    if (methodName == "resolve4") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve4", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.resolve6(hostname, callback)
    // =========================================================================
    if (methodName == "resolve6") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve6", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.reverse(ip, callback)
    // =========================================================================
    if (methodName == "reverse") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* ip = lastValue;
        if (!boxedValues.count(ip)) {
            ip = boxValue(ip, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_reverse", ft);
        lastValue = createCall(ft, fn.getCallee(), { ip, callback });
        return true;
    }

    // =========================================================================
    // dns.resolveCname(hostname, callback)
    // =========================================================================
    if (methodName == "resolveCname") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve_cname", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.resolveMx(hostname, callback)
    // =========================================================================
    if (methodName == "resolveMx") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve_mx", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.resolveNs(hostname, callback)
    // =========================================================================
    if (methodName == "resolveNs") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve_ns", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.resolveTxt(hostname, callback)
    // =========================================================================
    if (methodName == "resolveTxt") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve_txt", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.resolveSrv(hostname, callback)
    // =========================================================================
    if (methodName == "resolveSrv") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve_srv", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.resolvePtr(hostname, callback)
    // =========================================================================
    if (methodName == "resolvePtr") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve_ptr", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.resolveNaptr(hostname, callback)
    // =========================================================================
    if (methodName == "resolveNaptr") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve_naptr", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.resolveSoa(hostname, callback)
    // =========================================================================
    if (methodName == "resolveSoa") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_resolve_soa", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, callback });
        return true;
    }

    // =========================================================================
    // dns.getServers()
    // =========================================================================
    if (methodName == "getServers") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            {},
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_get_servers", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.setServers(servers)
    // =========================================================================
    if (methodName == "setServers") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* servers = lastValue;
        if (!boxedValues.count(servers)) {
            servers = boxValue(servers, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_set_servers", ft);
        lastValue = createCall(ft, fn.getCallee(), { servers });
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateDNSPromisesCall(ast::CallExpression* node, const std::string& methodName) {
    ensureDNSFunctionsRegistered(boxingPolicy);

    SPDLOG_DEBUG("tryGenerateDNSPromisesCall: dns.promises.{}", methodName);

    // =========================================================================
    // dns.promises.lookup(hostname, options?)
    // =========================================================================
    if (methodName == "lookup") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::Value* options;
        if (node->arguments.size() >= 2) {
            visit(node->arguments[1].get());
            options = lastValue;
            if (!boxedValues.count(options)) {
                options = boxValue(options, node->arguments[1]->inferredType);
            }
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_lookup", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname, options });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolve(hostname)
    // =========================================================================
    if (methodName == "resolve") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolve4(hostname)
    // =========================================================================
    if (methodName == "resolve4") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve4", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolve6(hostname)
    // =========================================================================
    if (methodName == "resolve6") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve6", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.reverse(ip)
    // =========================================================================
    if (methodName == "reverse") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* ip = lastValue;
        if (!boxedValues.count(ip)) {
            ip = boxValue(ip, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_reverse", ft);
        lastValue = createCall(ft, fn.getCallee(), { ip });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolveCname(hostname)
    // =========================================================================
    if (methodName == "resolveCname") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve_cname", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolveMx(hostname)
    // =========================================================================
    if (methodName == "resolveMx") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve_mx", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolveNs(hostname)
    // =========================================================================
    if (methodName == "resolveNs") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve_ns", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolveTxt(hostname)
    // =========================================================================
    if (methodName == "resolveTxt") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve_txt", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolveSrv(hostname)
    // =========================================================================
    if (methodName == "resolveSrv") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve_srv", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolvePtr(hostname)
    // =========================================================================
    if (methodName == "resolvePtr") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve_ptr", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolveNaptr(hostname)
    // =========================================================================
    if (methodName == "resolveNaptr") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve_naptr", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.resolveSoa(hostname)
    // =========================================================================
    if (methodName == "resolveSoa") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* hostname = lastValue;
        if (!boxedValues.count(hostname)) {
            hostname = boxValue(hostname, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_resolve_soa", ft);
        lastValue = createCall(ft, fn.getCallee(), { hostname });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.getServers()
    // =========================================================================
    if (methodName == "getServers") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            {},
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_get_servers", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // dns.promises.setServers(servers)
    // =========================================================================
    if (methodName == "setServers") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* servers = lastValue;
        if (!boxedValues.count(servers)) {
            servers = boxValue(servers, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_dns_promises_set_servers", ft);
        lastValue = createCall(ft, fn.getCallee(), { servers });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateDNSPropertyAccess(ast::PropertyAccessExpression* node) {
    ensureDNSFunctionsRegistered(boxingPolicy);

    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (!id || id->name != "dns") return false;

    const std::string& propName = node->name;

    // Check for error code constants
    static const std::map<std::string, std::string> errorConstants = {
        {"NODATA", "ts_dns_NODATA"},
        {"FORMERR", "ts_dns_FORMERR"},
        {"SERVFAIL", "ts_dns_SERVFAIL"},
        {"NOTFOUND", "ts_dns_NOTFOUND"},
        {"NOTIMP", "ts_dns_NOTIMP"},
        {"REFUSED", "ts_dns_REFUSED"},
        {"BADQUERY", "ts_dns_BADQUERY"},
        {"BADNAME", "ts_dns_BADNAME"},
        {"BADFAMILY", "ts_dns_BADFAMILY"},
        {"BADRESP", "ts_dns_BADRESP"},
        {"CONNREFUSED", "ts_dns_CONNREFUSED"},
        {"TIMEOUT", "ts_dns_TIMEOUT"},
        {"EOF", "ts_dns_EOF"},
        {"FILE", "ts_dns_FILE"},
        {"NOMEM", "ts_dns_NOMEM"},
        {"DESTRUCTION", "ts_dns_DESTRUCTION"},
        {"BADSTR", "ts_dns_BADSTR"},
        {"BADFLAGS", "ts_dns_BADFLAGS"},
        {"NONAME", "ts_dns_NONAME"},
        {"BADHINTS", "ts_dns_BADHINTS"},
        {"NOTINITIALIZED", "ts_dns_NOTINITIALIZED"},
        {"LOADIPHLPAPI", "ts_dns_LOADIPHLPAPI"},
        {"ADDRGETNETWORKPARAMS", "ts_dns_ADDRGETNETWORKPARAMS"},
        {"CANCELLED", "ts_dns_CANCELLED"},
    };

    auto it = errorConstants.find(propName);
    if (it != errorConstants.end()) {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt64Ty(),
            {},
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction(it->second.c_str(), ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    return false;
}

} // namespace ts
