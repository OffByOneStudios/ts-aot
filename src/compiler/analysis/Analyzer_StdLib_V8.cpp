#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::registerV8() {
    // =========================================================================
    // v8 module - Stubbed (AOT incompatible)
    //
    // The v8 module exposes APIs that are specific to the V8 JavaScript engine
    // built into Node.js. Since ts-aot compiles to native code via LLVM,
    // most V8-specific features are not applicable.
    //
    // These stubs allow code to compile but will return stub values at runtime.
    // =========================================================================
    auto v8Type = std::make_shared<ObjectType>();

    // v8.getHeapStatistics(): HeapStatistics
    auto heapStatsType = std::make_shared<ObjectType>();
    heapStatsType->fields["total_heap_size"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["total_heap_size_executable"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["total_physical_size"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["total_available_size"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["used_heap_size"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["heap_size_limit"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["malloced_memory"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["peak_malloced_memory"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["does_zap_garbage"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["number_of_native_contexts"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["number_of_detached_contexts"] = std::make_shared<Type>(TypeKind::Int);
    heapStatsType->fields["external_memory"] = std::make_shared<Type>(TypeKind::Int);

    auto getHeapStatisticsType = std::make_shared<FunctionType>();
    getHeapStatisticsType->returnType = heapStatsType;
    v8Type->fields["getHeapStatistics"] = getHeapStatisticsType;

    // v8.getHeapSpaceStatistics(): HeapSpaceStatistics[]
    auto heapSpaceStatsType = std::make_shared<ObjectType>();
    heapSpaceStatsType->fields["space_name"] = std::make_shared<Type>(TypeKind::String);
    heapSpaceStatsType->fields["space_size"] = std::make_shared<Type>(TypeKind::Int);
    heapSpaceStatsType->fields["space_used_size"] = std::make_shared<Type>(TypeKind::Int);
    heapSpaceStatsType->fields["space_available_size"] = std::make_shared<Type>(TypeKind::Int);
    heapSpaceStatsType->fields["physical_space_size"] = std::make_shared<Type>(TypeKind::Int);

    auto getHeapSpaceStatisticsType = std::make_shared<FunctionType>();
    getHeapSpaceStatisticsType->returnType = std::make_shared<ArrayType>(heapSpaceStatsType);
    v8Type->fields["getHeapSpaceStatistics"] = getHeapSpaceStatisticsType;

    // v8.getHeapCodeStatistics(): HeapCodeStatistics
    auto heapCodeStatsType = std::make_shared<ObjectType>();
    heapCodeStatsType->fields["code_and_metadata_size"] = std::make_shared<Type>(TypeKind::Int);
    heapCodeStatsType->fields["bytecode_and_metadata_size"] = std::make_shared<Type>(TypeKind::Int);
    heapCodeStatsType->fields["external_script_source_size"] = std::make_shared<Type>(TypeKind::Int);
    heapCodeStatsType->fields["cpu_profiler_metadata_size"] = std::make_shared<Type>(TypeKind::Int);

    auto getHeapCodeStatisticsType = std::make_shared<FunctionType>();
    getHeapCodeStatisticsType->returnType = heapCodeStatsType;
    v8Type->fields["getHeapCodeStatistics"] = getHeapCodeStatisticsType;

    // v8.setFlagsFromString(flags: string): void
    auto setFlagsFromStringType = std::make_shared<FunctionType>();
    setFlagsFromStringType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    setFlagsFromStringType->returnType = std::make_shared<Type>(TypeKind::Void);
    v8Type->fields["setFlagsFromString"] = setFlagsFromStringType;

    // v8.writeHeapSnapshot(filename?: string): string
    auto writeHeapSnapshotType = std::make_shared<FunctionType>();
    writeHeapSnapshotType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    writeHeapSnapshotType->returnType = std::make_shared<Type>(TypeKind::String);
    v8Type->fields["writeHeapSnapshot"] = writeHeapSnapshotType;

    // v8.serialize(value: any): Buffer
    auto serializeType = std::make_shared<FunctionType>();
    serializeType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    serializeType->returnType = std::make_shared<ClassType>("Buffer");
    v8Type->fields["serialize"] = serializeType;

    // v8.deserialize(buffer: Buffer): any
    auto deserializeType = std::make_shared<FunctionType>();
    deserializeType->paramTypes.push_back(std::make_shared<ClassType>("Buffer"));
    deserializeType->returnType = std::make_shared<Type>(TypeKind::Any);
    v8Type->fields["deserialize"] = deserializeType;

    // v8.cachedDataVersionTag(): number
    auto cachedDataVersionTagType = std::make_shared<FunctionType>();
    cachedDataVersionTagType->returnType = std::make_shared<Type>(TypeKind::Int);
    v8Type->fields["cachedDataVersionTag"] = cachedDataVersionTagType;

    symbols.define("v8", v8Type);
}

} // namespace ts
