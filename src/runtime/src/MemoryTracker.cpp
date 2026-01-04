#include "MemoryTracker.h"
#include <algorithm>
#include <vector>

bool MemoryTracker::enabled = false;
std::unordered_map<std::string, MemoryTracker::AllocationStats> MemoryTracker::stats;
size_t MemoryTracker::totalAllocations = 0;
size_t MemoryTracker::totalBytes = 0;
size_t MemoryTracker::peakBytes = 0;
size_t MemoryTracker::currentBytes = 0;

void MemoryTracker::RecordAllocation(const char* type, size_t bytes) {
    if (!enabled) return;
    
    totalAllocations++;
    totalBytes += bytes;
    currentBytes += bytes;
    
    if (currentBytes > peakBytes) {
        peakBytes = currentBytes;
    }
    
    auto& stat = stats[type];
    stat.count++;
    stat.totalBytes += bytes;
    if (stat.totalBytes > stat.peakBytes) {
        stat.peakBytes = stat.totalBytes;
    }
}

void MemoryTracker::RecordDeallocation(const char* type, size_t bytes) {
    if (!enabled) return;
    
    currentBytes -= bytes;
    auto& stat = stats[type];
    stat.totalBytes -= bytes;
}

void MemoryTracker::PrintReport() {
    printf("\n=== MEMORY ALLOCATION REPORT ===\n");
    printf("Total allocations: %zu\n", totalAllocations);
    printf("Total bytes allocated: %.2f MB\n", totalBytes / (1024.0 * 1024.0));
    printf("Peak memory usage: %.2f MB\n", peakBytes / (1024.0 * 1024.0));
    printf("Current memory usage: %.2f MB\n", currentBytes / (1024.0 * 1024.0));
    printf("\n");
    
    // Sort by total bytes
    std::vector<std::pair<std::string, AllocationStats>> sorted;
    for (const auto& entry : stats) {
        sorted.push_back(entry);
    }
    std::sort(sorted.begin(), sorted.end(), 
        [](const auto& a, const auto& b) { 
            return a.second.totalBytes > b.second.totalBytes; 
        });
    
    printf("%-30s %10s %15s %15s\n", "Type", "Count", "Total (MB)", "Peak (MB)");
    printf("%-30s %10s %15s %15s\n", "----", "-----", "----------", "---------");
    
    for (const auto& entry : sorted) {
        printf("%-30s %10zu %15.2f %15.2f\n",
            entry.first.c_str(),
            entry.second.count,
            entry.second.totalBytes / (1024.0 * 1024.0),
            entry.second.peakBytes / (1024.0 * 1024.0));
    }
    printf("\n");
}

void MemoryTracker::Reset() {
    stats.clear();
    totalAllocations = 0;
    totalBytes = 0;
    peakBytes = 0;
    currentBytes = 0;
}
