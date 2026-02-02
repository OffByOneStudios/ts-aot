// path.cpp - Node.js path module implementation for ts-aot
//
// This is part of the ts_path extension library, which is conditionally
// linked when a TypeScript file imports the 'path' module.

#include "TsPath.h"

// Include runtime headers from the main tsruntime
#include "TsRuntime.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsObject.h"

#include <string>
#include <iostream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

enum class PathPlatform {
    Default,
    Win32,
    Posix
};

static char GetSeparator(PathPlatform platform) {
    if (platform == PathPlatform::Win32) return '\\';
    if (platform == PathPlatform::Posix) return '/';
    return (char)fs::path::preferred_separator;
}

static TsString* UnboxString(void* ptr) {
    if (!ptr) return nullptr;
    uint32_t magic = *(uint32_t*)ptr;
    if (magic == 0x53545247) { // TsString::MAGIC
        return (TsString*)ptr;
    }
    TsValue* v = (TsValue*)ptr;
    if (v->type == ValueType::STRING_PTR) {
        return (TsString*)v->ptr_val;
    }
    return nullptr;
}

static void NormalizeSeparators(std::string& s, PathPlatform platform) {
    for (char& c : s) {
        if (platform == PathPlatform::Win32 && c == '/') c = '\\';
        else if (platform == PathPlatform::Posix && c == '\\') c = '/';
    }
}

extern "C" {

void* ts_path_join(void* path1, void* path2) {
    TsString* p1 = UnboxString(path1);
    TsString* p2 = UnboxString(path2);

    if (!p1 || !p2) return TsString::Create("");

    fs::path path = fs::path(p1->ToUtf8()) / fs::path(p2->ToUtf8());
    return TsString::Create(path.lexically_normal().make_preferred().string().c_str());
}

void* ts_path_join_variadic_ex(void* paths_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsArray* paths = (TsArray*)paths_ptr;
    if (!paths) return TsString::Create("");

    std::string result;
    char sep = GetSeparator(platform);
    bool first = true;

    for (int64_t i = 0; i < paths->Length(); ++i) {
        TsString* s = UnboxString((void*)paths->Get(i));
        if (s) {
            std::string part = s->ToUtf8();
            if (part.empty()) continue;
            if (first) {
                result = part;
                first = false;
            } else {
                if (!result.empty() && result.back() != '/' && result.back() != '\\') {
                    result += sep;
                }
                result += part;
            }
        }
    }
    NormalizeSeparators(result, platform);
    return TsString::Create(result.c_str());
}

void* ts_path_join_variadic(void* paths_ptr) {
    return ts_path_join_variadic_ex(paths_ptr, (int)PathPlatform::Default);
}

void* ts_path_resolve_ex(void* paths_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsArray* paths = (TsArray*)paths_ptr;
    fs::path result = fs::current_path();

    if (paths) {
        for (int64_t i = 0; i < paths->Length(); ++i) {
            TsString* s = UnboxString((void*)paths->Get(i));
            if (s) {
                std::string part = s->ToUtf8();
                NormalizeSeparators(part, platform);
                fs::path p(part);
                if (p.is_absolute()) result = p;
                else result /= p;
            }
        }
    }

    std::string res_str = fs::absolute(result).lexically_normal().string();
    NormalizeSeparators(res_str, platform);
    return TsString::Create(res_str.c_str());
}

void* ts_path_resolve(void* paths_ptr) {
    return ts_path_resolve_ex(paths_ptr, (int)PathPlatform::Default);
}

void* ts_path_normalize_ex(void* path_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsString* s = UnboxString(path_ptr);
    if (!s) return TsString::Create("");
    std::string p_str = s->ToUtf8();
    NormalizeSeparators(p_str, platform);
    std::string res = fs::path(p_str).lexically_normal().string();
    NormalizeSeparators(res, platform);
    return TsString::Create(res.c_str());
}

void* ts_path_normalize(void* path_ptr) {
    return ts_path_normalize_ex(path_ptr, (int)PathPlatform::Default);
}

int ts_path_is_absolute_ex(void* path_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsString* s = UnboxString(path_ptr);
    if (!s) return 0;
    std::string p_str = s->ToUtf8();
    NormalizeSeparators(p_str, platform);
    return fs::path(p_str).is_absolute() ? 1 : 0;
}

int ts_path_is_absolute(void* path_ptr) {
    return ts_path_is_absolute_ex(path_ptr, (int)PathPlatform::Default);
}

void* ts_path_basename_ex(void* path_ptr, void* ext_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsString* s = UnboxString(path_ptr);
    if (!s) return TsString::Create("");

    std::string p_str = s->ToUtf8();
    NormalizeSeparators(p_str, platform);
    fs::path p(p_str);
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

void* ts_path_basename(void* path_ptr, void* ext_ptr) {
    return ts_path_basename_ex(path_ptr, ext_ptr, (int)PathPlatform::Default);
}

void* ts_path_dirname_ex(void* path_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsString* s = UnboxString(path_ptr);
    if (!s) return TsString::Create("");
    std::string p_str = s->ToUtf8();
    NormalizeSeparators(p_str, platform);
    std::string res = fs::path(p_str).parent_path().string();
    NormalizeSeparators(res, platform);
    return TsString::Create(res.c_str());
}

void* ts_path_dirname(void* path_ptr) {
    return ts_path_dirname_ex(path_ptr, (int)PathPlatform::Default);
}

void* ts_path_extname_ex(void* path_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsString* s = UnboxString(path_ptr);
    if (!s) return TsString::Create("");
    std::string p_str = s->ToUtf8();
    NormalizeSeparators(p_str, platform);
    return TsString::Create(fs::path(p_str).extension().string().c_str());
}

void* ts_path_extname(void* path_ptr) {
    return ts_path_extname_ex(path_ptr, (int)PathPlatform::Default);
}

void* ts_path_relative_ex(void* from_ptr, void* to_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsString* from = UnboxString(from_ptr);
    TsString* to = UnboxString(to_ptr);
    if (!from || !to) return TsString::Create("");

    std::string from_str = from->ToUtf8();
    std::string to_str = to->ToUtf8();
    NormalizeSeparators(from_str, platform);
    NormalizeSeparators(to_str, platform);

    std::string res = fs::relative(fs::path(to_str), fs::path(from_str)).string();
    NormalizeSeparators(res, platform);
    return TsString::Create(res.c_str());
}

void* ts_path_relative(void* from_ptr, void* to_ptr) {
    return ts_path_relative_ex(from_ptr, to_ptr, (int)PathPlatform::Default);
}

void* ts_path_get_sep() {
#ifdef _WIN32
    return TsString::Create("\\");
#else
    return TsString::Create("/");
#endif
}

void* ts_path_get_sep_win32() {
    return TsString::Create("\\");
}

void* ts_path_get_sep_posix() {
    return TsString::Create("/");
}

void* ts_path_get_delimiter() {
#ifdef _WIN32
    return TsString::Create(";");
#else
    return TsString::Create(":");
#endif
}

void* ts_path_get_delimiter_win32() {
    return TsString::Create(";");
}

void* ts_path_get_delimiter_posix() {
    return TsString::Create(":");
}

void* ts_path_parse_ex(void* p_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsValue* p = (TsValue*)p_ptr;
    TsString* ts_p = UnboxString(p);
    if (!ts_p) {
        return ts_value_make_undefined();
    }

    std::string path_str = ts_p->ToUtf8();
    NormalizeSeparators(path_str, platform);
    fs::path path(path_str);

    TsMap* map = TsMap::Create();

    auto set_prop = [&](const char* key, const std::string& val) {
        TsValue k; k.type = ValueType::STRING_PTR; k.ptr_val = TsString::Create(key);
        TsValue v; v.type = ValueType::STRING_PTR; v.ptr_val = TsString::Create(val.c_str());
        map->Set(k, v);
    };

    set_prop("root", path.root_name().string() + path.root_directory().string());
    set_prop("dir", path.parent_path().string());
    set_prop("base", path.filename().string());
    set_prop("ext", path.extension().string());
    set_prop("name", path.stem().string());

    return ts_value_make_object(map);
}

void* ts_path_parse(void* p_ptr) {
    return ts_path_parse_ex(p_ptr, (int)PathPlatform::Default);
}

void* ts_path_format_ex(void* obj_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsValue* obj = (TsValue*)obj_ptr;
    if (!obj || obj->type != ValueType::OBJECT_PTR) {
        return TsString::Create("");
    }
    void* o = obj->ptr_val;

    auto get_prop = [&](const char* key) -> std::string {
        TsValue* val = ts_object_get_property(o, key);
        if (val) {
            TsString* s = UnboxString(val);
            if (s) return s->ToUtf8();
        }
        return "";
    };

    std::string dir = get_prop("dir");
    std::string root = get_prop("root");
    std::string base = get_prop("base");
    std::string name = get_prop("name");
    std::string ext = get_prop("ext");

    std::string result;
    char sep = GetSeparator(platform);

    if (!dir.empty()) {
        result = dir;
        if (result.back() != '\\' && result.back() != '/') {
            result += sep;
        }
    } else if (!root.empty()) {
        result = root;
    }

    if (!base.empty()) {
        result += base;
    } else {
        result += name + ext;
    }

    NormalizeSeparators(result, platform);
    return TsString::Create(result.c_str());
}

void* ts_path_format(void* obj_ptr) {
    return ts_path_format_ex(obj_ptr, (int)PathPlatform::Default);
}

void* ts_path_to_namespaced_path_ex(void* p_ptr, int platform_int) {
    PathPlatform platform = (PathPlatform)platform_int;
    TsValue* p = (TsValue*)p_ptr;
    TsString* ts_p = UnboxString(p);
    if (!ts_p) return nullptr;

    std::string path_str = ts_p->ToUtf8();
    NormalizeSeparators(path_str, platform);

#ifdef _WIN32
    if (platform == PathPlatform::Win32 || (platform == PathPlatform::Default)) {
        if (path_str.empty()) return ts_p;

        if (path_str.size() >= 2 && path_str[1] == ':') {
            if (path_str.size() == 2 || (path_str[2] != '\\' && path_str[2] != '/')) {
                 // Not absolute enough
            } else if (path_str.substr(0, 4) != "\\\\?\\") {
                return TsString::Create(("\\\\?\\" + path_str).c_str());
            }
        }
    }
#endif
    return TsString::Create(path_str.c_str());
}

void* ts_path_to_namespaced_path(void* p_ptr) {
    return ts_path_to_namespaced_path_ex(p_ptr, (int)PathPlatform::Default);
}

} // extern "C"
