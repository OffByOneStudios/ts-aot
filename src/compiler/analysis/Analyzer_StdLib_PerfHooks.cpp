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

    // =========================================================================
    // EventLoopUtilization object
    // =========================================================================
    auto eluClass = std::make_shared<ClassType>("EventLoopUtilization");
    eluClass->fields["idle"] = std::make_shared<Type>(TypeKind::Double);
    eluClass->fields["active"] = std::make_shared<Type>(TypeKind::Double);
    eluClass->fields["utilization"] = std::make_shared<Type>(TypeKind::Double);
    symbols.defineType("EventLoopUtilization", eluClass);

    // eventLoopUtilization(util1?: EventLoopUtilization, util2?: EventLoopUtilization): EventLoopUtilization
    auto eluMethod = std::make_shared<FunctionType>();
    eluMethod->paramTypes.push_back(eluClass);  // optional
    eluMethod->paramTypes.push_back(eluClass);  // optional
    eluMethod->returnType = eluClass;
    performanceObj->fields["eventLoopUtilization"] = eluMethod;

    // timerify(fn: Function): Function - wraps function to measure timing
    auto timerifyMethod = std::make_shared<FunctionType>();
    timerifyMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Function));
    timerifyMethod->returnType = std::make_shared<Type>(TypeKind::Function);
    performanceObj->fields["timerify"] = timerifyMethod;

    // =========================================================================
    // PerformanceObserverEntryList class
    // NOTE: Using fields instead of methods to avoid vtable generation
    // =========================================================================
    auto entryListClass = std::make_shared<ClassType>("PerformanceObserverEntryList");

    // getEntries(): PerformanceEntry[]
    auto entryListGetEntriesMethod = std::make_shared<FunctionType>();
    entryListGetEntriesMethod->returnType = std::make_shared<ArrayType>(performanceEntryClass);
    entryListClass->fields["getEntries"] = entryListGetEntriesMethod;

    // getEntriesByName(name: string, type?: string): PerformanceEntry[]
    auto entryListGetEntriesByNameMethod = std::make_shared<FunctionType>();
    entryListGetEntriesByNameMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    entryListGetEntriesByNameMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // optional
    entryListGetEntriesByNameMethod->returnType = std::make_shared<ArrayType>(performanceEntryClass);
    entryListClass->fields["getEntriesByName"] = entryListGetEntriesByNameMethod;

    // getEntriesByType(type: string): PerformanceEntry[]
    auto entryListGetEntriesByTypeMethod = std::make_shared<FunctionType>();
    entryListGetEntriesByTypeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    entryListGetEntriesByTypeMethod->returnType = std::make_shared<ArrayType>(performanceEntryClass);
    entryListClass->fields["getEntriesByType"] = entryListGetEntriesByTypeMethod;

    symbols.defineType("PerformanceObserverEntryList", entryListClass);

    // =========================================================================
    // PerformanceObserver class
    // NOTE: Using fields instead of methods to avoid vtable generation
    // =========================================================================
    auto observerClass = std::make_shared<ClassType>("PerformanceObserver");

    // observe(options: { entryTypes: string[] }): void
    auto observeMethod = std::make_shared<FunctionType>();
    observeMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // options object
    observeMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    observerClass->fields["observe"] = observeMethod;

    // disconnect(): void
    auto disconnectMethod = std::make_shared<FunctionType>();
    disconnectMethod->returnType = std::make_shared<Type>(TypeKind::Void);
    observerClass->fields["disconnect"] = disconnectMethod;

    // takeRecords(): PerformanceEntry[]
    auto takeRecordsMethod = std::make_shared<FunctionType>();
    takeRecordsMethod->returnType = std::make_shared<ArrayType>(performanceEntryClass);
    observerClass->fields["takeRecords"] = takeRecordsMethod;

    symbols.defineType("PerformanceObserver", observerClass);

    // Register static PerformanceObserver constructor
    auto observerStaticObj = std::make_shared<ObjectType>();
    observerStaticObj->fields["prototype"] = observerClass;
    symbols.define("PerformanceObserver", observerStaticObj);

    // Register the performance object
    symbols.define("performance", performanceObj);

    // Module object for 'import { performance, PerformanceObserver } from "perf_hooks"'
    auto perfHooksModule = std::make_shared<ObjectType>();
    perfHooksModule->fields["performance"] = performanceObj;
    perfHooksModule->fields["PerformanceObserver"] = observerStaticObj;

    symbols.define("perf_hooks", perfHooksModule);
}

} // namespace ts
