#include "TsRegExp.h"
#include "TsArray.h"
#include "TsMap.h"
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
    
    uint32_t icuFlags = 0;
    if (flags) {
        std::string f(flags);
        if (f.find('i') != std::string::npos) icuFlags |= UREGEX_CASE_INSENSITIVE;
        if (f.find('m') != std::string::npos) icuFlags |= UREGEX_MULTILINE;
        // Add more flags as needed
    }

    matcher = new icu::RegexMatcher(patternStr, icuFlags, status);
}

TsRegExp::~TsRegExp() {
    delete matcher;
}

bool TsRegExp::Test(TsString* str) {
    if (!matcher) return false;
    
    UErrorCode status = U_ZERO_ERROR;
    matcher->reset(*(icu::UnicodeString*)str->GetImpl());
    return matcher->find();
}

void* TsRegExp::Exec(TsString* str) {
    if (!matcher) return nullptr;
    
    UErrorCode status = U_ZERO_ERROR;
    matcher->reset(*(icu::UnicodeString*)str->GetImpl());
    
    if (matcher->find()) {
        TsArray* result = TsArray::Create();
        int32_t count = matcher->groupCount();
        
        for (int32_t i = 0; i <= count; ++i) {
            icu::UnicodeString group = matcher->group(i, status);
            std::string utf8;
            group.toUTF8String(utf8);
            result->Push((int64_t)TsString::Create(utf8.c_str()));
        }
        
        return result;
    }
    
    return nullptr;
}

extern "C" {
    void* ts_regexp_create(void* pattern, void* flags) {
        TsString* p = (TsString*)pattern;
        TsString* f = (TsString*)flags;
        return TsRegExp::Create(p->ToUtf8(), f ? f->ToUtf8() : "");
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
}
