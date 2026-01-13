#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unicode/regex.h>
#include "TsString.h"

class TsArray;

// TsRegExpMatchArray - Result of RegExp.exec() with indices support
// This class has the same struct layout as TsArray for the first 4 fields
// so that codegen can do inline access to length and elements.
class TsRegExpMatchArray {
public:
    static constexpr uint32_t MAGIC = 0x524D4154; // "RMAT" (RegExp Match ArraY)
    static TsRegExpMatchArray* Create(TsArray* matches, int64_t matchIndex, TsString* input);

    // These mirror TsArray's accessors for codegen compatibility
    int64_t Length() const { return length; }
    void* Get(size_t idx) const;
    void* GetElementsPtr() { return elements; }

    // Extra properties for exec result
    int64_t GetMatchIndex() const { return matchIndex; }
    TsString* GetInput() const { return input; }
    TsArray* GetIndices() const { return indices; }
    void SetIndices(TsArray* ind) { indices = ind; }
    void* GetGroups() const { return groups; }
    void SetGroups(void* g) { groups = g; }

private:
    TsRegExpMatchArray(TsArray* source, int64_t matchIndex, TsString* input);

    // These 4 fields MUST match TsArray's layout for inline codegen access:
    // offset 0: magic (uint32_t)
    // offset 8: elements (void*)  - after padding
    // offset 16: length (size_t)
    // offset 24: capacity (size_t)
    uint32_t magic = MAGIC;
    void* elements = nullptr;    // Copy from source TsArray
    size_t length = 0;           // Copy from source TsArray
    size_t capacity = 0;         // Copy from source TsArray

    // Extra fields for RegExp match result
    int64_t matchIndex = 0;      // Index of match in input string
    TsString* input = nullptr;   // Original input string
    TsArray* indices = nullptr;  // Array of [start, end] pairs when d flag used
    void* groups = nullptr;      // Object mapping named capture group names to values
};

class TsRegExp {
public:
    static constexpr uint32_t MAGIC = 0x52454758; // "REGX"
    static TsRegExp* Create(const char* pattern, const char* flags = "");
    
    bool Test(TsString* str);
    void* Exec(TsString* str); // Returns TsArray of matches or null

    int64_t GetLastIndex() const { return lastIndex; }
    void SetLastIndex(int64_t index) { lastIndex = index; }

    TsString* GetSource() const;
    TsString* GetFlags() const;
    bool IsGlobal() const { return global; }
    bool IsSticky() const { return sticky; }
    bool IsIgnoreCase() const { return ignoreCase; }
    bool IsMultiline() const { return multiline; }
    bool HasIndices() const { return hasIndices; }
    void* GetMatcher() const { return matcher; }

    // Named capture groups support
    bool HasNamedGroups() const { return !namedGroups.empty(); }
    const std::vector<std::pair<std::string, int32_t>>& GetNamedGroups() const { return namedGroups; }

private:
    void parseNamedGroups();
    TsRegExp(const char* pattern, const char* flags);
    ~TsRegExp();

    uint32_t magic = MAGIC;
    icu::RegexMatcher* matcher = nullptr;
    icu::UnicodeString patternStr;
    std::string flagsStr;
    int64_t lastIndex = 0;
    bool global = false;
    bool sticky = false;
    bool ignoreCase = false;
    bool multiline = false;
    bool hasIndices = false;
    std::vector<std::pair<std::string, int32_t>> namedGroups;  // name -> group number
};

extern "C" {
    void* ts_regexp_create(void* pattern, void* flags);
    void* ts_regexp_from_literal(void* literal);
    int32_t RegExp_test(void* re, void* str);
    void* RegExp_exec(void* re, void* str);
    int64_t RegExp_get_lastIndex(void* re);
    void RegExp_set_lastIndex(void* re, int64_t index);
    void* RegExp_get_source(void* re);
    void* RegExp_get_flags(void* re);
    int32_t RegExp_get_global(void* re);
    int32_t RegExp_get_sticky(void* re);
    int32_t RegExp_get_ignoreCase(void* re);
    int32_t RegExp_get_multiline(void* re);
    int32_t RegExp_get_hasIndices(void* re);
}
