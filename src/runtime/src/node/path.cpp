#include "TsRuntime.h"
#include "TsString.h"
#include <string>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

static TsString* UnboxString(void* ptr) {
    if (!ptr) return nullptr;
    uintptr_t val = *(uintptr_t*)ptr;
    // Heuristic: pointers are large, TsValue header (type + padding) is small
    if (val < 0x10000) {
        TsValue* v = (TsValue*)ptr;
        if (v->type == ValueType::STRING_PTR) {
            return (TsString*)v->ptr_val;
        }
        return nullptr; 
    }
    return (TsString*)ptr;
}

extern "C" {

void* ts_path_join(void* path1, void* path2) {
    TsString* p1 = UnboxString(path1);
    TsString* p2 = UnboxString(path2);
    
    if (!p1 || !p2) {
        std::cerr << "ts_path_join: Invalid arguments" << std::endl;
        return TsString::Create("");
    }
    
    const char* s1 = p1->ToUtf8();
    const char* s2 = p2->ToUtf8();

    fs::path path = fs::path(s1) / fs::path(s2);
    std::string res = path.string();
    return TsString::Create(res.c_str());
}

} // extern "C"
