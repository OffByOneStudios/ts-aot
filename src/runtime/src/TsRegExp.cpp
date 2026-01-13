#include "TsRegExp.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsRuntime.h"
#include <unicode/unistr.h>
#include <unicode/regex.h>
#include <regex>

extern "C" void* ts_alloc(size_t size);

// TsRegExpMatchArray implementation
TsRegExpMatchArray* TsRegExpMatchArray::Create(TsArray* source, int64_t matchIndex, TsString* input) {
    void* mem = ts_alloc(sizeof(TsRegExpMatchArray));
    return new(mem) TsRegExpMatchArray(source, matchIndex, input);
}

TsRegExpMatchArray::TsRegExpMatchArray(TsArray* source, int64_t matchIndex, TsString* input)
    : matchIndex(matchIndex), input(input) {
    // Copy the array's data pointers so that inline codegen struct access works
    if (source) {
        elements = source->GetElementsPtr();
        length = source->Length();
        // We don't have direct access to capacity, but we can set it equal to length
        capacity = length;
    }
}

void* TsRegExpMatchArray::Get(size_t idx) const {
    if (idx >= length) return nullptr;
    return (void*)((int64_t*)elements)[idx];
}

TsRegExp* TsRegExp::Create(const char* pattern, const char* flags) {
    void* mem = ts_alloc(sizeof(TsRegExp));
    return new(mem) TsRegExp(pattern, flags);
}

// Parse pattern to extract named capture groups (?<name>...)
// Returns a vector of (name, groupNumber) pairs
void TsRegExp::parseNamedGroups() {
    std::string patternUtf8;
    patternStr.toUTF8String(patternUtf8);

    // Track group numbers - named and unnamed groups are numbered in order of opening paren
    int32_t groupNumber = 0;
    size_t i = 0;

    while (i < patternUtf8.size()) {
        // Skip escaped characters
        if (patternUtf8[i] == '\\' && i + 1 < patternUtf8.size()) {
            i += 2;
            continue;
        }

        // Skip character classes
        if (patternUtf8[i] == '[') {
            i++;
            while (i < patternUtf8.size()) {
                if (patternUtf8[i] == '\\' && i + 1 < patternUtf8.size()) {
                    i += 2;
                    continue;
                }
                if (patternUtf8[i] == ']') {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }

        // Check for groups
        if (patternUtf8[i] == '(' && i + 1 < patternUtf8.size()) {
            if (patternUtf8[i + 1] == '?') {
                // Non-capturing group or special construct
                if (i + 2 < patternUtf8.size() && patternUtf8[i + 2] == '<') {
                    // Could be named group (?<name>...) or lookbehind (?<!...) or (?<=...)
                    if (i + 3 < patternUtf8.size() && patternUtf8[i + 3] != '=' && patternUtf8[i + 3] != '!') {
                        // Named group: extract name
                        groupNumber++;
                        size_t nameStart = i + 3;
                        size_t nameEnd = patternUtf8.find('>', nameStart);
                        if (nameEnd != std::string::npos) {
                            std::string name = patternUtf8.substr(nameStart, nameEnd - nameStart);
                            namedGroups.push_back({name, groupNumber});
                            i = nameEnd + 1;
                            continue;
                        }
                    }
                }
                // Other non-capturing constructs: (?:...), (?=...), (?!...), (?<=...), (?<!...)
                // These don't create capturing groups
                i += 2;
                continue;
            } else {
                // Regular capturing group
                groupNumber++;
            }
        }
        i++;
    }
}

TsRegExp::TsRegExp(const char* pattern, const char* flags) {
    UErrorCode status = U_ZERO_ERROR;
    patternStr = icu::UnicodeString::fromUTF8(pattern);
    flagsStr = flags ? flags : "";
    
    uint32_t icuFlags = 0;
    if (flags) {
        std::string f(flags);
        if (f.find('i') != std::string::npos) {
            icuFlags |= UREGEX_CASE_INSENSITIVE;
            ignoreCase = true;
        }
        if (f.find('m') != std::string::npos) {
            icuFlags |= UREGEX_MULTILINE;
            multiline = true;
        }
        if (f.find('s') != std::string::npos) icuFlags |= UREGEX_DOTALL;
        if (f.find('u') != std::string::npos) icuFlags |= UREGEX_UWORD;
        if (f.find('x') != std::string::npos) icuFlags |= UREGEX_COMMENTS;
        
        if (f.find('g') != std::string::npos) global = true;
        if (f.find('y') != std::string::npos) sticky = true;
        if (f.find('d') != std::string::npos) hasIndices = true;
    }

    matcher = new icu::RegexMatcher(patternStr, icuFlags, status);

    // Parse pattern to extract named capture groups
    parseNamedGroups();
}

TsRegExp::~TsRegExp() {
    delete matcher;
}

TsString* TsRegExp::GetSource() const {
    std::string utf8;
    patternStr.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsRegExp::GetFlags() const {
    return TsString::Create(flagsStr.c_str());
}

bool TsRegExp::Test(TsString* str) {
    if (!matcher) return false;
    
    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString input = str->ToUnicodeString();
    matcher->reset(input);
    
    if (global || sticky) {
        matcher->region(lastIndex, input.length(), status);
    }

    bool found = matcher->find();
    
    if (global || sticky) {
        if (found) {
            lastIndex = matcher->end(status);
        } else {
            lastIndex = 0;
        }
    }
    
    return found;
}

void* TsRegExp::Exec(TsString* str) {
    if (!matcher) return nullptr;

    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString input = str->ToUnicodeString();
    matcher->reset(input);

    if (global || sticky) {
        matcher->region(lastIndex, input.length(), status);
    }

    if (matcher->find()) {
        if (sticky && matcher->start(status) != lastIndex) {
            lastIndex = 0;
            return nullptr;
        }

        TsArray* matches = TsArray::Create();
        int32_t count = matcher->groupCount();
        int64_t matchIndex = matcher->start(status);  // Index of full match

        // Build array of match strings
        for (int32_t i = 0; i <= count; ++i) {
            icu::UnicodeString group = matcher->group(i, status);
            int32_t groupStart = matcher->start(i, status);
            if (groupStart == -1) {
                matches->Push((int64_t)ts_value_make_undefined());
            } else {
                std::string utf8;
                group.toUTF8String(utf8);
                matches->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
            }
        }

        // Create the match array wrapper with index and input
        TsRegExpMatchArray* result = TsRegExpMatchArray::Create(matches, matchIndex, str);

        // If d flag (hasIndices) is set, build the indices array
        if (hasIndices) {
            TsArray* indices = TsArray::Create();

            for (int32_t i = 0; i <= count; ++i) {
                int32_t start = matcher->start(i, status);
                int32_t end = matcher->end(i, status);

                if (start == -1) {
                    // Group did not participate in match
                    indices->Push((int64_t)ts_value_make_undefined());
                } else {
                    // Create [start, end] pair as a 2-element array
                    TsArray* pair = TsArray::Create(2);
                    pair->Push((int64_t)ts_value_make_int(start));
                    pair->Push((int64_t)ts_value_make_int(end));
                    indices->Push((int64_t)ts_value_make_object(pair));
                }
            }

            result->SetIndices(indices);
        }

        // Build groups object if pattern has named capture groups
        if (!namedGroups.empty()) {
            TsMap* groups = TsMap::Create();

            for (const auto& [name, groupNum] : namedGroups) {
                TsString* nameStr = TsString::Create(name.c_str());
                int32_t groupStart = matcher->start(groupNum, status);

                // Create key as proper TsValue with STRING_PTR type
                TsValue keyVal;
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = nameStr;

                if (groupStart == -1) {
                    // Group did not participate in match
                    TsValue undefinedVal;
                    undefinedVal.type = ValueType::UNDEFINED;
                    undefinedVal.ptr_val = nullptr;
                    groups->Set(keyVal, undefinedVal);
                } else {
                    icu::UnicodeString groupValue = matcher->group(groupNum, status);
                    std::string utf8;
                    groupValue.toUTF8String(utf8);

                    TsValue stringVal;
                    stringVal.type = ValueType::STRING_PTR;
                    stringVal.ptr_val = TsString::Create(utf8.c_str());
                    groups->Set(keyVal, stringVal);
                }
            }

            result->SetGroups(groups);
        }

        if (global || sticky) {
            lastIndex = matcher->end(status);
        }

        return result;
    }

    if (global || sticky) {
        lastIndex = 0;
    }

    return nullptr;
}

extern "C" {
    void* ts_regexp_create(void* pattern, void* flags) {
        TsString* p = (TsString*)pattern;
        TsString* f = (TsString*)flags;
        return TsRegExp::Create(p->ToUtf8(), f ? f->ToUtf8() : "");
    }

    void* ts_regexp_from_literal(void* literal) {
        TsString* s = (TsString*)literal;
        std::string text = s->ToUtf8();
        if (text.empty() || text[0] != '/') return nullptr;
        
        size_t lastSlash = text.find_last_of('/');
        if (lastSlash == 0 || lastSlash == std::string::npos) return nullptr;
        
        std::string pattern = text.substr(1, lastSlash - 1);
        std::string flags = text.substr(lastSlash + 1);
        
        return TsRegExp::Create(pattern.c_str(), flags.c_str());
    }

    int32_t RegExp_test(void* re, void* str) {
        TsRegExp* r = (TsRegExp*)re;
        TsString* s = (TsString*)str;
        return r->Test(s) ? 1 : 0;
    }

    void* RegExp_exec(void* re, void* str) {
        TsRegExp* r = (TsRegExp*)re;
        TsString* s = (TsString*)str;
        return r->Exec(s);
    }

    int64_t RegExp_get_lastIndex(void* re) {
        return ((TsRegExp*)re)->GetLastIndex();
    }

    void RegExp_set_lastIndex(void* re, int64_t index) {
        ((TsRegExp*)re)->SetLastIndex(index);
    }

    void* RegExp_get_source(void* re) {
        return ((TsRegExp*)re)->GetSource();
    }

    void* RegExp_get_flags(void* re) {
        return ((TsRegExp*)re)->GetFlags();
    }

    int32_t RegExp_get_global(void* re) {
        return ((TsRegExp*)re)->IsGlobal() ? 1 : 0;
    }

    int32_t RegExp_get_sticky(void* re) {
        return ((TsRegExp*)re)->IsSticky() ? 1 : 0;
    }

    int32_t RegExp_get_ignoreCase(void* re) {
        return ((TsRegExp*)re)->IsIgnoreCase() ? 1 : 0;
    }

    int32_t RegExp_get_multiline(void* re) {
        return ((TsRegExp*)re)->IsMultiline() ? 1 : 0;
    }

    int32_t RegExp_get_hasIndices(void* re) {
        return ((TsRegExp*)re)->HasIndices() ? 1 : 0;
    }
}
