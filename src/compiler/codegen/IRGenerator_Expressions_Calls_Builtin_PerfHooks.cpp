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

} // namespace ts
