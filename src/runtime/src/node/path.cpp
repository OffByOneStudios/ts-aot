#include "TsRuntime.h"
#include "TsString.h"
#include "TsArray.h"
#include <string>
#include <iostream>
#include <filesystem>
#include <vector>

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
    
    if (!p1 || !p2) return TsString::Create("");
    
    fs::path path = fs::path(p1->ToUtf8()) / fs::path(p2->ToUtf8());
    return TsString::Create(path.lexically_normal().make_preferred().string().c_str());
}

void* ts_path_join_variadic(void* paths_ptr) {
    TsArray* paths = (TsArray*)paths_ptr;
    if (!paths) return TsString::Create("");

    fs::path result;
    bool first = true;
    for (int64_t i = 0; i < paths->Length(); ++i) {
        TsString* s = UnboxString((void*)paths->Get(i));
        if (s) {
            std::string part = s->ToUtf8();
            if (part.empty()) continue;
            if (first) {
                result = fs::path(part);
                first = false;
            } else {
                result /= fs::path(part);
            }
        }
    }
    return TsString::Create(result.lexically_normal().make_preferred().string().c_str());
}

void* ts_path_resolve(void* paths_ptr) {
    TsArray* paths = (TsArray*)paths_ptr;
    fs::path result = fs::current_path();

    if (paths) {
        for (int64_t i = 0; i < paths->Length(); ++i) {
            TsString* s = UnboxString((void*)paths->Get(i));
            if (s) {
                fs::path p(s->ToUtf8());
                if (p.is_absolute()) result = p;
                else result /= p;
            }
        }
    }
    
    return TsString::Create(fs::absolute(result).lexically_normal().make_preferred().string().c_str());
}

void* ts_path_normalize(void* path_ptr) {
    TsString* s = UnboxString(path_ptr);
    if (!s) return TsString::Create("");
    return TsString::Create(fs::path(s->ToUtf8()).lexically_normal().make_preferred().string().c_str());
}

int ts_path_is_absolute(void* path_ptr) {
    TsString* s = UnboxString(path_ptr);
    if (!s) return 0;
    std::string p_str = s->ToUtf8();
    return fs::path(p_str).is_absolute() ? 1 : 0;
}

void* ts_path_basename(void* path_ptr, void* ext_ptr) {
    TsString* s = UnboxString(path_ptr);
    if (!s) return TsString::Create("");
    
    fs::path p(s->ToUtf8());
    std::string filename = p.filename().string();
    
    TsString* ext = UnboxString(ext_ptr);
    if (ext) {
        std::string ext_str = ext->ToUtf8();
        if (filename.size() >= ext_str.size() && filename.compare(filename.size() - ext_str.size(), ext_str.size(), ext_str) == 0) {
            filename = filename.substr(0, filename.size() - ext_str.size());
        }
    }
    
    return TsString::Create(filename.c_str());
}

void* ts_path_dirname(void* path_ptr) {
    TsString* s = UnboxString(path_ptr);
    if (!s) return TsString::Create("");
    return TsString::Create(fs::path(s->ToUtf8()).parent_path().make_preferred().string().c_str());
}

void* ts_path_extname(void* path_ptr) {
    TsString* s = UnboxString(path_ptr);
    if (!s) return TsString::Create("");
    return TsString::Create(fs::path(s->ToUtf8()).extension().string().c_str());
}

void* ts_path_relative(void* from_ptr, void* to_ptr) {
    TsString* from = UnboxString(from_ptr);
    TsString* to = UnboxString(to_ptr);
    if (!from || !to) return TsString::Create("");
    
    return TsString::Create(fs::relative(fs::path(to->ToUtf8()), fs::path(from->ToUtf8())).make_preferred().string().c_str());
}

void* ts_path_get_sep() {
#ifdef _WIN32
    return TsString::Create("\\");
#else
    return TsString::Create("/");
#endif
}

void* ts_path_get_delimiter() {
#ifdef _WIN32
    return TsString::Create(";");
#else
    return TsString::Create(":");
#endif
}

} // extern "C"
