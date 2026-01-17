#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerPerfHooks() {
    // =========================================================================
    // perf_hooks module - Performance measurement utilities
    //
    // Provides high-resolution performance timing similar to Node.js perf_hooks.
    // =========================================================================

    // PerformanceEntry class (base for marks and measures)
    auto performanceEntryClass = std::make_shared<ClassType>("PerformanceEntry");
    performanceEntryClass->fields["name"] = std::make_shared<Type>(TypeKind::String);
    performanceEntryClass->fields["entryType"] = std::make_shared<Type>(TypeKind::String);
    performanceEntryClass->fields["startTime"] = std::make_shared<Type>(TypeKind::Double);
    performanceEntryClass->fields["duration"] = std::make_shared<Type>(TypeKind::Double);
    symbols.defineType("PerformanceEntry", performanceEntryClass);

    // PerformanceMark class (extends PerformanceEntry)
    auto performanceMarkClass = std::make_shared<ClassType>("PerformanceMark");
    performanceMarkClass->fields["name"] = std::make_shared<Type>(TypeKind::String);
    performanceMarkClass->fields["entryType"] = std::make_shared<Type>(TypeKind::String);
    performanceMarkClass->fields["startTime"] = std::make_shared<Type>(TypeKind::Double);
    performanceMarkClass->fields["duration"] = std::make_shared<Type>(TypeKind::Double);
    symbols.defineType("PerformanceMark", performanceMarkClass);

    // PerformanceMeasure class (extends PerformanceEntry)
    auto performanceMeasureClass = std::make_shared<ClassType>("PerformanceMeasure");
    performanceMeasureClass->fields["name"] = std::make_shared<Type>(TypeKind::String);
    performanceMeasureClass->fields["entryType"] = std::make_shared<Type>(TypeKind::String);
    performanceMeasureClass->fields["startTime"] = std::make_shared<Type>(TypeKind::Double);
    performanceMeasureClass->fields["duration"] = std::make_shared<Type>(TypeKind::Double);
    symbols.defineType("PerformanceMeasure", performanceMeasureClass);

    // Performance object (singleton)
    auto performanceObj = std::make_shared<ObjectType>();

    // timeOrigin: number - Unix timestamp when performance measurement started
    performanceObj->fields["timeOrigin"] = std::make_shared<Type>(TypeKind::Double);

    // now(): number - Returns high-resolution timestamp in milliseconds
    auto nowMethod = std::make_shared<FunctionType>();
    nowMethod->returnType = std::make_shared<Type>(TypeKind::Double);
    performanceObj->fields["now"] = nowMethod;

    // mark(name: string, options?: { startTime?: number }): PerformanceMark
    auto markMethod = std::make_shared<FunctionType>();
    markMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    markMethod->returnType = performanceMarkClass;
    performanceObj->fields["mark"] = markMethod;

    // measure(name: string, startMark?: string, endMark?: string): PerformanceMeasure
    auto measureMethod = std::make_shared<FunctionType>();
    measureMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    measureMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // optional
    measureMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // optional
    measureMethod->returnType = performanceMeasureClass;
    performanceObj->fields["measure"] = measureMethod;

    // getEntries(): PerformanceEntry[]
    auto getEntriesMethod = std::make_shared<FunctionType>();
    getEntriesMethod->returnType = std::make_shared<ArrayType>(performanceEntryClass);
    performanceObj->fields["getEntries"] = getEntriesMethod;

    // getEntriesByName(name: string, type?: string): PerformanceEntry[]
    auto getEntriesByNameMethod = std::make_shared<FunctionType>();
    getEntriesByNameMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    getEntriesByNameMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // optional
    getEntriesByNameMethod->returnType = std::make_shared<ArrayType>(performanceEntryClass);
    performanceObj->fields["getEntriesByName"] = getEntriesByNameMethod;

    // getEntriesByType(type: string): PerformanceEntry[]
    auto getEntriesByTypeMethod = std::make_shared<FunctionType>();
    getEntriesByTypeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    getEntriesByTypeMethod->returnType = std::make_shared<ArrayType>(performanceEntryClass);
    performanceObj->fields["getEntriesByType"] = getEntriesByTypeMethod;

    // clearMarks(name?: string): void
    auto clearMarksMethod = std::make_shared<FunctionType>();
    clearMarksMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // optional
    clearMarksMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    performanceObj->fields["clearMarks"] = clearMarksMethod;

    // clearMeasures(name?: string): void
    auto clearMeasuresMethod = std::make_shared<FunctionType>();
    clearMeasuresMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // optional
    clearMeasuresMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    performanceObj->fields["clearMeasures"] = clearMeasuresMethod;

    // Register the performance object
    symbols.define("performance", performanceObj);

    // Module object for 'import { performance } from "perf_hooks"'
    auto perfHooksModule = std::make_shared<ObjectType>();
    perfHooksModule->fields["performance"] = performanceObj;

    symbols.define("perf_hooks", perfHooksModule);
}

} // namespace ts
