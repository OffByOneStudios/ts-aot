#include "IRGenerator.h"
#include "BoxingPolicy.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

static bool perfHooksFunctionsRegistered = false;
static void ensurePerfHooksFunctionsRegistered(BoxingPolicy& bp) {
    if (perfHooksFunctionsRegistered) return;
    perfHooksFunctionsRegistered = true;

    bp.registerRuntimeApi("ts_performance_now", {}, false);  // returns double
    bp.registerRuntimeApi("ts_performance_time_origin", {}, false);  // returns double
    bp.registerRuntimeApi("ts_performance_mark", {true, false}, true);  // name (boxed), startTime (double) -> boxed entry
    bp.registerRuntimeApi("ts_performance_measure", {true, true, true}, true);  // name, startMark, endMark -> boxed entry
    bp.registerRuntimeApi("ts_performance_get_entries", {}, true);  // returns boxed array
    bp.registerRuntimeApi("ts_performance_get_entries_by_name", {true, true}, true);  // name, type -> boxed array
    bp.registerRuntimeApi("ts_performance_get_entries_by_type", {true}, true);  // type -> boxed array
    bp.registerRuntimeApi("ts_performance_clear_marks", {true}, false);  // name (optional)
    bp.registerRuntimeApi("ts_performance_clear_measures", {true}, false);  // name (optional)
    bp.registerRuntimeApi("ts_performance_entry_get_name", {true}, true);  // entry -> string
    bp.registerRuntimeApi("ts_performance_entry_get_entry_type", {true}, true);  // entry -> string
    bp.registerRuntimeApi("ts_performance_entry_get_start_time", {true}, false);  // entry -> double
    bp.registerRuntimeApi("ts_performance_entry_get_duration", {true}, false);  // entry -> double

    // New APIs
    bp.registerRuntimeApi("ts_performance_event_loop_utilization", {true, true}, true);  // util1, util2 -> boxed ELU
    bp.registerRuntimeApi("ts_performance_timerify", {true}, true);  // fn -> fn (pass-through for AOT)
    bp.registerRuntimeApi("ts_elu_get_idle", {true}, false);  // elu -> double
    bp.registerRuntimeApi("ts_elu_get_active", {true}, false);  // elu -> double
    bp.registerRuntimeApi("ts_elu_get_utilization", {true}, false);  // elu -> double
    bp.registerRuntimeApi("ts_performance_observer_create", {true}, true);  // callback -> boxed observer
    bp.registerRuntimeApi("ts_performance_observer_observe", {true, true}, false);  // observer, options -> void
    bp.registerRuntimeApi("ts_performance_observer_disconnect", {true}, false);  // observer -> void
    bp.registerRuntimeApi("ts_performance_observer_take_records", {true}, true);  // observer -> boxed array
}

bool IRGenerator::tryGeneratePerfHooksCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check for performance object method calls
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "performance") return false;

    ensurePerfHooksFunctionsRegistered(boxingPolicy);
    SPDLOG_DEBUG("tryGeneratePerfHooksCall: performance.{}", prop->name);

    const std::string& methodName = prop->name;

    if (methodName == "now") {
        // performance.now() -> returns double (ms)
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getDoubleTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_now", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    if (methodName == "mark") {
        // performance.mark(name, options?)
        llvm::Value* name = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* startTime = llvm::ConstantFP::get(builder->getDoubleTy(), -1.0);

        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            name = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        // Handle options object with startTime if provided
        if (node->arguments.size() > 1) {
            // For now, just pass the startTime if it's a number
            visit(node->arguments[1].get());
            // TODO: extract startTime from options object
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getDoubleTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_mark", ft);
        lastValue = createCall(ft, fn.getCallee(), { name, startTime });
        boxedValues.insert(lastValue);
        return true;
    }

    if (methodName == "measure") {
        // performance.measure(name, startMark?, endMark?)
        llvm::Value* name = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* startMark = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* endMark = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            name = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            startMark = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            endMark = boxValue(lastValue, node->arguments[2]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_measure", ft);
        lastValue = createCall(ft, fn.getCallee(), { name, startMark, endMark });
        boxedValues.insert(lastValue);
        return true;
    }

    if (methodName == "getEntries") {
        // performance.getEntries() -> PerformanceEntry[]
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_get_entries", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    if (methodName == "getEntriesByName") {
        // performance.getEntriesByName(name, type?)
        llvm::Value* name = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* type = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            name = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            type = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_get_entries_by_name", ft);
        lastValue = createCall(ft, fn.getCallee(), { name, type });
        boxedValues.insert(lastValue);
        return true;
    }

    if (methodName == "getEntriesByType") {
        // performance.getEntriesByType(type)
        llvm::Value* type = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            type = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_get_entries_by_type", ft);
        lastValue = createCall(ft, fn.getCallee(), { type });
        boxedValues.insert(lastValue);
        return true;
    }

    if (methodName == "clearMarks") {
        // performance.clearMarks(name?)
        llvm::Value* name = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            name = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_clear_marks", ft);
        lastValue = createCall(ft, fn.getCallee(), { name });
        return true;
    }

    if (methodName == "clearMeasures") {
        // performance.clearMeasures(name?)
        llvm::Value* name = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            name = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_clear_measures", ft);
        lastValue = createCall(ft, fn.getCallee(), { name });
        return true;
    }

    if (methodName == "eventLoopUtilization") {
        // performance.eventLoopUtilization(util1?, util2?) -> EventLoopUtilization
        llvm::Value* util1 = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* util2 = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            util1 = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            util2 = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_event_loop_utilization", ft);
        lastValue = createCall(ft, fn.getCallee(), { util1, util2 });
        boxedValues.insert(lastValue);
        return true;
    }

    if (methodName == "timerify") {
        // performance.timerify(fn) -> fn (pass-through for AOT)
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* fn = boxValue(lastValue, node->arguments[0]->inferredType);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee rtFn = getRuntimeFunction("ts_performance_timerify", ft);
        lastValue = createCall(ft, rtFn.getCallee(), { fn });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGeneratePerfHooksPropertyAccess(ast::PropertyAccessExpression* prop) {
    // Check for performance.timeOrigin
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "performance") return false;

    ensurePerfHooksFunctionsRegistered(boxingPolicy);
    SPDLOG_DEBUG("tryGeneratePerfHooksPropertyAccess: performance.{}", prop->name);

    const std::string& propName = prop->name;

    if (propName == "timeOrigin") {
        // performance.timeOrigin -> double (Unix timestamp in ms)
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getDoubleTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_time_origin", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    return false;
}

bool IRGenerator::tryGeneratePerformanceEntryPropertyAccess(ast::PropertyAccessExpression* prop) {
    // Check if the expression is a PerformanceEntry, PerformanceMark, or PerformanceMeasure
    if (!prop->expression->inferredType) return false;

    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    if (!classType || prop->expression->inferredType->kind != TypeKind::Class) return false;

    if (classType->name != "PerformanceEntry" &&
        classType->name != "PerformanceMark" &&
        classType->name != "PerformanceMeasure") {
        return false;
    }

    ensurePerfHooksFunctionsRegistered(boxingPolicy);
    SPDLOG_DEBUG("tryGeneratePerformanceEntryPropertyAccess: {}.{}", classType->name, prop->name);

    // Get the entry object
    visit(prop->expression.get());
    llvm::Value* entry = boxValue(lastValue, prop->expression->inferredType);

    const std::string& propName = prop->name;

    if (propName == "name") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_entry_get_name", ft);
        lastValue = createCall(ft, fn.getCallee(), { entry });
        boxedValues.insert(lastValue);
        return true;
    }

    if (propName == "entryType") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_entry_get_entry_type", ft);
        lastValue = createCall(ft, fn.getCallee(), { entry });
        boxedValues.insert(lastValue);
        return true;
    }

    if (propName == "startTime") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getDoubleTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_entry_get_start_time", ft);
        lastValue = createCall(ft, fn.getCallee(), { entry });
        return true;
    }

    if (propName == "duration") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getDoubleTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_entry_get_duration", ft);
        lastValue = createCall(ft, fn.getCallee(), { entry });
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateEventLoopUtilizationPropertyAccess(ast::PropertyAccessExpression* prop) {
    // Check if the expression is EventLoopUtilization
    if (!prop->expression->inferredType) return false;
    if (prop->expression->inferredType->kind != TypeKind::Class) return false;

    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    if (classType->name != "EventLoopUtilization") return false;

    ensurePerfHooksFunctionsRegistered(boxingPolicy);
    SPDLOG_DEBUG("tryGenerateEventLoopUtilizationPropertyAccess: ELU.{}", prop->name);

    // Get the ELU object
    visit(prop->expression.get());
    llvm::Value* elu = boxValue(lastValue, prop->expression->inferredType);

    const std::string& propName = prop->name;

    if (propName == "idle") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getDoubleTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_elu_get_idle", ft);
        lastValue = createCall(ft, fn.getCallee(), { elu });
        return true;
    }

    if (propName == "active") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getDoubleTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_elu_get_active", ft);
        lastValue = createCall(ft, fn.getCallee(), { elu });
        return true;
    }

    if (propName == "utilization") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getDoubleTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_elu_get_utilization", ft);
        lastValue = createCall(ft, fn.getCallee(), { elu });
        return true;
    }

    return false;
}

bool IRGenerator::tryGeneratePerformanceObserverCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check if the expression is a PerformanceObserver
    if (!prop->expression->inferredType) return false;
    if (prop->expression->inferredType->kind != TypeKind::Class) return false;

    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    if (classType->name != "PerformanceObserver") return false;

    ensurePerfHooksFunctionsRegistered(boxingPolicy);
    SPDLOG_DEBUG("tryGeneratePerformanceObserverCall: observer.{}", prop->name);

    // Get the observer object
    visit(prop->expression.get());
    llvm::Value* observer = boxValue(lastValue, prop->expression->inferredType);

    const std::string& methodName = prop->name;

    if (methodName == "observe") {
        // observer.observe({ entryTypes: [...] })
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            options = boxValue(lastValue, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_observer_observe", ft);
        lastValue = createCall(ft, fn.getCallee(), { observer, options });
        return true;
    }

    if (methodName == "disconnect") {
        // observer.disconnect()
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_observer_disconnect", ft);
        lastValue = createCall(ft, fn.getCallee(), { observer });
        return true;
    }

    if (methodName == "takeRecords") {
        // observer.takeRecords() -> PerformanceEntry[]
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_observer_take_records", ft);
        lastValue = createCall(ft, fn.getCallee(), { observer });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGeneratePerformanceObserverNewExpression(ast::NewExpression* node) {
    // Check for: new PerformanceObserver(callback)
    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (!id || id->name != "PerformanceObserver") return false;

    ensurePerfHooksFunctionsRegistered(boxingPolicy);
    SPDLOG_DEBUG("tryGeneratePerformanceObserverNewExpression");

    llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

    if (!node->arguments.empty()) {
        visit(node->arguments[0].get());
        callback = boxValue(lastValue, node->arguments[0]->inferredType);
    }

    llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
        { builder->getPtrTy() }, false);
    llvm::FunctionCallee fn = getRuntimeFunction("ts_performance_observer_create", ft);
    lastValue = createCall(ft, fn.getCallee(), { callback });
    boxedValues.insert(lastValue);
    return true;
}

} // namespace ts
