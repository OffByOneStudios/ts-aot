#include "TsSymbol.h"
#include "TsRuntime.h"
#include "TsNanBox.h"
#include "TsGC.h"
#include <map>
#include <string>

static std::map<std::string, TsSymbol*> symbol_registry;

// Register symbol_registry scanner with custom GC
static struct SymbolRegistryScanner {
    SymbolRegistryScanner() {
        ts_gc_register_scanner([](void*) {
            for (auto& [key, sym] : symbol_registry) {
                ts_gc_mark_object(sym);
            }
        }, nullptr);
    }
} g_symbol_registry_scanner;

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
    // Allocate in old-gen: symbol_registry is in malloc'd memory,
    // invisible to nursery GC card table
    TsSymbol* sym = (TsSymbol*)ts_gc_alloc_old_gen(sizeof(TsSymbol));
    new (sym) TsSymbol(key);
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
    // Normalize the description to a real TsString or nullptr. Symbol() with no
    // (or undefined/null) description passes a NaN-boxed primitive here; storing
    // it verbatim made sym->description a garbage non-null pointer, so any read
    // (ts_symbol_storage_key -> ToUtf8, e.g. `{ [Symbol()]: 1 }`) crashed.
    TsString* d = nullptr;
    if (desc) {
        uint64_t nb = (uint64_t)(uintptr_t)desc;
        if (!nanbox_is_undefined(nb) && !nanbox_is_null(nb)) {
            // Resolve the string from any representation: boxed TsValue, a
            // NaN-boxed string pointer, or a raw TsString*.
            void* s = ts_value_get_string((TsValue*)desc);
            if (!s && nanbox_is_ptr(nb)) s = nanbox_to_ptr(nb);
            if (!s) s = desc;
            // Keep it only if it really is a heap TsString.
            uintptr_t p = (uintptr_t)s;
            if (p >= 0x10000 && (p & 0xFFFF000000000000ULL) == 0 &&
                *(uint32_t*)s == 0x53545247 /*TsString "STRG"*/) {
                d = (TsString*)s;
            }
        }
    }
    return TsSymbol::Create(d);
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
    // NaN boxing: pointer is encoded as-is (top 16 bits = 0 for valid pointers)
    return (TsValue*)(uintptr_t)s;
}

// ES2019: Symbol.prototype.description getter
void* ts_symbol_get_description(void* sym) {
    if (!sym) return nullptr;
    TsSymbol* s = (TsSymbol*)sym;
    return s->description;  // Returns TsString* or nullptr (undefined)
}

}
