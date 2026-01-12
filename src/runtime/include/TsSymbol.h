#pragma once
#include "TsObject.h"
#include "TsString.h"

class TsSymbol {
public:
    uint32_t magic; // 0x53594D42 'SYMB'
    TsString* description;

    static TsSymbol* Create(TsString* desc = nullptr);
    static TsSymbol* For(TsString* key);
    static TsString* KeyFor(TsSymbol* sym);

    TsSymbol(TsString* desc = nullptr);
};

extern "C" {
    void* ts_symbol_create(void* desc);
    void* ts_symbol_for(void* key);
    void* ts_symbol_key_for(void* sym);
    void* ts_symbol_get_description(void* sym);  // ES2019
}
