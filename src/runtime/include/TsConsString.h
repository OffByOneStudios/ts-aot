#pragma once

#include <cstdint>

class TsString;

// TsConsString: Rope node for O(1) string concatenation.
// Instead of copying string data on every concat, we create a lightweight
// tree node pointing to left and right children. The tree is flattened
// to a real TsString on demand (when content is accessed).
//
// Layout (32 bytes):
//   [4B magic: 0x434F4E53 "CONS"]
//   [4B totalLength]
//   [8B left pointer  - TsString* or TsConsString*]
//   [8B right pointer - TsString* or TsConsString*]
//   [8B flattened     - cached TsString* after first flatten, initially nullptr]
class TsConsString {
public:
    static constexpr uint32_t MAGIC = 0x434F4E53; // "CONS"
    static constexpr int MAX_DEPTH = 64;

    // Create a cons node from two string-like pointers (TsString* or TsConsString*).
    // Returns a TsConsString* if tree depth is within limits, otherwise
    // eagerly flattens and returns a TsString*.
    static void* Create(void* left, void* right);

    // Flatten the rope tree into a single TsString*.
    // Uses iterative traversal (no recursion) to avoid stack overflow.
    // Result is cached in `flattened` for subsequent calls.
    TsString* Flatten();

    uint32_t magic;
    uint32_t totalLength;
    void* left;
    void* right;
    void* flattened; // Cached TsString* after first flatten
    uint16_t depth;  // Tree depth for O(1) depth check

private:
    TsConsString() = default;
};

// Check if a pointer is a TsConsString (magic == 0x434F4E53).
inline bool ts_is_cons_string(void* ptr) {
    if (!ptr) return false;
    return *(uint32_t*)ptr == TsConsString::MAGIC;
}

// Check if a pointer is a TsString (magic == 0x53545247).
inline bool ts_is_string(void* ptr) {
    if (!ptr) return false;
    return *(uint32_t*)ptr == 0x53545247;
}

// Check if a pointer is any string type (TsString or TsConsString).
inline bool ts_is_any_string(void* ptr) {
    if (!ptr) return false;
    uint32_t m = *(uint32_t*)ptr;
    return m == 0x53545247 || m == TsConsString::MAGIC;
}

// Ensure a string-like pointer is a flat TsString*.
// If already a TsString*, returns it directly.
// If a TsConsString*, flattens and returns the result.
// Returns nullptr if ptr is neither.
TsString* ts_ensure_flat(void* ptr);

// Get the length of a string-like pointer without flattening.
// For TsConsString, returns totalLength directly.
int64_t ts_string_like_length(void* ptr);
