#pragma once
#include <cstddef>
#include <unordered_map>
#include <string>
#include <cstdio>

// Memory allocation tracker for debugging
class MemoryTracker {
public:
    struct AllocationStats {
        size_t count = 0;
        size_t totalBytes = 0;
        size_t peakBytes = 0;
    };

    static void RecordAllocation(const char* type, size_t bytes);
    static void RecordDeallocation(const char* type, size_t bytes);
    static void PrintReport();
    static void Reset();
    
    static bool IsEnabled() { return enabled; }
    static void Enable() { enabled = true; }
    static void Disable() { enabled = false; }

private:
    static bool enabled;
    static std::unordered_map<std::string, AllocationStats> stats;
    static size_t totalAllocations;
    static size_t totalBytes;
    static size_t peakBytes;
    static size_t currentBytes;
};
