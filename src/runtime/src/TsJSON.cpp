#include "TsJSON.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsArray.h"
#include "GC.h"
#include <nlohmann/json.hpp>
#include <string>
#include <cstring>

using json = nlohmann::json;

static int64_t json_to_ts(const json& j) {
    if (j.is_null()) return 0;
    if (j.is_boolean()) return j.get<bool>() ? 1 : 0;
    if (j.is_number_integer()) {
        // For integers, we use double representation if they are large, 
        // but for now let's just use double for all numbers to be safe with any
        double d = (double)j.get<int64_t>();
        int64_t i;
        std::memcpy(&i, &d, sizeof(double));
        return i;
    }
    if (j.is_number_float()) {
        double d = j.get<double>();
        int64_t i;
        std::memcpy(&i, &d, sizeof(double));
        return i;
    }
    if (j.is_string()) {
        return (int64_t)TsString::Create(j.get<std::string>().c_str());
    }
    if (j.is_array()) {
        TsArray* arr = TsArray::Create();
        for (const auto& element : j) {
            arr->Push(json_to_ts(element));
        }
        return (int64_t)arr;
    }
    if (j.is_object()) {
        TsMap* map = TsMap::Create();
        for (auto it = j.begin(); it != j.end(); ++it) {
            map->Set(TsString::Create(it.key().c_str()), json_to_ts(it.value()));
        }
        return (int64_t)map;
    }
    return 0;
}

static json ts_to_json(int64_t val) {
    if (val == 0) return nullptr;

    // Check if it's a bit-casted double
    uintptr_t ptr = (uintptr_t)val;
    if (ptr > 0x00007FFFFFFFFFFF) {
        double d;
        std::memcpy(&d, &val, sizeof(double));
        return d;
    }

    // Check magic numbers
    // Safety: we should probably check if ptr is valid, but for now assume it is if it's in the pointer range
    if (ptr < 0x1000) return val; // Small integers

    try {
        uint32_t magic = *(uint32_t*)val;
        if (magic == TsString::MAGIC) {
            return ((TsString*)val)->ToUtf8();
        }
        if (magic == TsArray::MAGIC) {
            TsArray* arr = (TsArray*)val;
            json j = json::array();
            for (int64_t i = 0; i < arr->Length(); ++i) {
                j.push_back(ts_to_json(arr->Get(i)));
            }
            return j;
        }
        if (magic == TsMap::MAGIC) {
            TsMap* map = (TsMap*)val;
            json j = json::object();
            TsArray* keys = (TsArray*)map->GetKeys();
            for (int64_t i = 0; i < keys->Length(); ++i) {
                TsString* key = (TsString*)keys->Get(i);
                j[key->ToUtf8()] = ts_to_json(map->Get(key));
            }
            return j;
        }
    } catch (...) {
        // Not a pointer to our objects
    }

    return val;
}

extern "C" {
    void* ts_json_parse(void* json_str) {
        if (!json_str) return nullptr;
        TsString* s = (TsString*)json_str;
        try {
            json j = json::parse(s->ToUtf8());
            return (void*)json_to_ts(j);
        } catch (...) {
            return nullptr;
        }
    }

    void* ts_json_stringify(void* obj) {
        try {
            json j = ts_to_json((int64_t)obj);
            return TsString::Create(j.dump().c_str());
        } catch (...) {
            return TsString::Create("");
        }
    }

    void* ts_object_get_property(void* obj, void* key) {
        if (!obj || !key) return nullptr;
        uint32_t magic = *(uint32_t*)obj;
        if (magic == TsMap::MAGIC) {
            return (void*)((TsMap*)obj)->Get((TsString*)key);
        }
        return nullptr;
    }
}
