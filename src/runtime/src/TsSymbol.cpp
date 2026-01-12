#include "TsSymbol.h"
#include "TsRuntime.h"
#include <map>
#include <string>

static std::map<std::string, TsSymbol*> symbol_registry;

TsSymbol::TsSymbol(TsString* desc) : description(desc) {
    magic = 0x53594D42;
}

TsSymbol* TsSymbol::Create(TsString* desc) {
    TsSymbol* sym = (TsSymbol*)ts_alloc(sizeof(TsSymbol));
    new (sym) TsSymbol(desc);
    return sym;
}

TsSymbol* TsSymbol::For(TsString* key) {
    std::string k = key ? key->ToUtf8() : "";
    if (symbol_registry.count(k)) {
        return symbol_registry[k];
    }
    TsSymbol* sym = Create(key);
    symbol_registry[k] = sym;
    return sym;
}

TsString* TsSymbol::KeyFor(TsSymbol* sym) {
    for (auto const& [key, val] : symbol_registry) {
        if (val == sym) {
            return TsString::Create(key.c_str());
        }
    }
    return nullptr;
}

extern "C" {

void* ts_symbol_create(void* desc) {
    return TsSymbol::Create((TsString*)desc);
}

void* ts_symbol_for(void* key) {
    return TsSymbol::For((TsString*)key);
}

void* ts_symbol_key_for(void* sym) {
    TsString* key = TsSymbol::KeyFor((TsSymbol*)sym);
    // Return nullptr (which becomes undefined in JS) when symbol isn't in registry
    return key;
}

TsValue* ts_value_make_symbol(void* s) {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::SYMBOL_PTR;
    v->ptr_val = s;
    return v;
}

// ES2019: Symbol.prototype.description getter
void* ts_symbol_get_description(void* sym) {
    if (!sym) return nullptr;
    TsSymbol* s = (TsSymbol*)sym;
    return s->description;  // Returns TsString* or nullptr (undefined)
}

}
