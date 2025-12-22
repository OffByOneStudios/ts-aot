#include "TsRegExp.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsRuntime.h"
#include <unicode/unistr.h>
#include <unicode/regex.h>

extern "C" void* ts_alloc(size_t size);

TsRegExp* TsRegExp::Create(const char* pattern, const char* flags) {
    void* mem = ts_alloc(sizeof(TsRegExp));
    return new(mem) TsRegExp(pattern, flags);
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
    }

    matcher = new icu::RegexMatcher(patternStr, icuFlags, status);
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

        TsArray* result = TsArray::Create();
        int32_t count = matcher->groupCount();
        
        for (int32_t i = 0; i <= count; ++i) {
            icu::UnicodeString group = matcher->group(i, status);
            if (matcher->start(i, status) == -1) {
                result->Push((int64_t)ts_value_make_undefined());
            } else {
                std::string utf8;
                group.toUTF8String(utf8);
                result->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
            }
        }
        
        // Add index and input properties to the array (simplified for now)
        // In JS, exec returns an array with extra properties.
        // For now we just return the array of matches.
        
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
}
