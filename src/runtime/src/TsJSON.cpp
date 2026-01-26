#include "TsJSON.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsDate.h"
#include "TsRegExp.h"
#include "GC.h"
#include <nlohmann/json.hpp>
#include <string>
#include <cstring>

#include <set>
#include <stdexcept>

using json = nlohmann::json;

static TsValue json_to_ts(const json& j) {
    if (j.is_null()) return TsValue(nullptr);
    if (j.is_boolean()) return TsValue(j.get<bool>());
    if (j.is_number_integer()) {
        return TsValue((int64_t)j.get<int64_t>());
    }
    if (j.is_number_float()) {
        return TsValue(j.get<double>());
    }
    if (j.is_string()) {
        TsValue v;
        std::memset(&v, 0, sizeof(TsValue));  // Zero-initialize including padding
        v.type = ValueType::STRING_PTR;
        v.ptr_val = TsString::Create(j.get<std::string>().c_str());
        return v;
    }
    if (j.is_array()) {
        TsArray* arr = TsArray::Create();
        for (const auto& element : j) {
            TsValue val = json_to_ts(element);
            int64_t raw = 0;
            if (val.type == ValueType::NUMBER_INT) raw = val.i_val;
            else if (val.type == ValueType::NUMBER_DBL) {
                // Bit-cast double to int64_t for storage in array
                std::memcpy(&raw, &val.d_val, sizeof(double));
            }
            else raw = (int64_t)val.ptr_val;
            arr->Push(raw);
        }
        TsValue v;
        std::memset(&v, 0, sizeof(TsValue));  // Zero-initialize including padding
        v.type = ValueType::ARRAY_PTR;
        v.ptr_val = arr;
        return v;
    }
    if (j.is_object()) {
        TsMap* map = TsMap::Create();
        for (auto it = j.begin(); it != j.end(); ++it) {
            map->Set(TsString::Create(it.key().c_str()), json_to_ts(it.value()));
        }
        TsValue v;
        std::memset(&v, 0, sizeof(TsValue));  // Zero-initialize including padding
        v.type = ValueType::OBJECT_PTR;
        v.ptr_val = map;
        return v;
    }
    return TsValue(nullptr);
}

static nlohmann::json ts_to_json_internal(void* p, std::set<void*>& visited);

static nlohmann::json ts_value_to_json(TsValue v, std::set<void*>& visited) {
    switch (v.type) {
        case ValueType::UNDEFINED: return nullptr;
        case ValueType::NUMBER_INT: return v.i_val;
        case ValueType::NUMBER_DBL: return v.d_val;
        case ValueType::BOOLEAN: return v.b_val;
        case ValueType::STRING_PTR: return ts_to_json_internal(v.ptr_val, visited);
        case ValueType::OBJECT_PTR: return ts_to_json_internal(v.ptr_val, visited);
        case ValueType::ARRAY_PTR: return ts_to_json_internal(v.ptr_val, visited);
        case ValueType::PROMISE_PTR: return "[Promise]";
        default: return nullptr;
    }
}

static nlohmann::json ts_to_json_internal(void* p, std::set<void*>& visited) {
    if (!p) return nullptr;
    
    // Check if it's a small integer (raw)
    if ((uintptr_t)p < 4096) return (int64_t)p;

    // Check if it's a bit-casted double (raw)
    uint64_t val = (uint64_t)p;
    if (val > 0x00007FFFFFFFFFFF) {
        double d;
        std::memcpy(&d, &val, sizeof(double));
        return d;
    }

    // Check magic numbers. Some objects have a vtable (TsObject), some don't.
    // Layout varies:
    //   TsString, TsArray, TsDate, TsRegExp: magic at offset 0 (no vtable)
    //   TsMap, TsSet, TsBuffer, etc: magic at offset 16 (TsObject-derived with vtable)
    uint32_t magic = *(uint32_t*)p;
    uint32_t magic_offset16 = 0;
    if ((uintptr_t)p > 0x1000) {
        magic_offset16 = *(uint32_t*)((char*)p + 16);
    }

    if (magic == TsString::MAGIC) {
        return ((TsString*)p)->ToUtf8();
    }

    if (magic == TsDate::MAGIC) {
        return ((TsDate*)p)->ToISOString()->ToUtf8();
    }

    if (magic == TsRegExp::MAGIC) {
        return nlohmann::json::object();
    }

    if (magic == TsArray::MAGIC) {
        if (visited.find(p) != visited.end()) {
            throw std::runtime_error("Circular reference in JSON.stringify");
        }
        visited.insert(p);
        TsArray* arr = (TsArray*)p;
        nlohmann::json j = nlohmann::json::array();
        for (int64_t i = 0; i < arr->Length(); ++i) {
            // TsArray::Get returns raw int64_t, which might be a pointer or a boxed value
            j.push_back(ts_to_json_internal((void*)arr->Get(i), visited));
        }
        visited.erase(p);
        return j;
    }

    // TsMap is TsObject-derived, magic at offset 16
    if (magic_offset16 == TsMap::MAGIC) {
        if (visited.find(p) != visited.end()) {
            throw std::runtime_error("Circular reference in JSON.stringify");
        }
        visited.insert(p);
        TsMap* map = (TsMap*)p;
        nlohmann::json j = nlohmann::json::object();
        TsArray* keys = (TsArray*)map->GetKeys();
        for (int64_t i = 0; i < keys->Length(); ++i) {
            TsValue* kPtr = (TsValue*)keys->Get(i);
            TsValue val = map->Get(*kPtr);
            if (val.type == ValueType::UNDEFINED) continue;
            
            std::string keyStr;
            if (kPtr->type == ValueType::STRING_PTR) {
                keyStr = ((TsString*)kPtr->ptr_val)->ToUtf8();
            } else {
                // JSON only supports string keys
                continue;
            }
            j[keyStr] = ts_value_to_json(val, visited);
        }
        visited.erase(p);
        return j;
    }

    // Fallback: check if it's a boxed TsValue
    uint8_t type_byte = *(uint8_t*)p;
    if (type_byte <= (uint8_t)ValueType::SYMBOL_PTR) {
        return ts_value_to_json(*(TsValue*)p, visited);
    }

    return (int64_t)p;
}

static nlohmann::json ts_to_json(void* p) {
    std::set<void*> visited;
    return ts_to_json_internal(p, visited);
}

extern "C" {
    void* ts_json_parse(void* json_str) {
        if (!json_str) return nullptr;
        TsString* s = (TsString*)json_str;
        try {
            nlohmann::json j = nlohmann::json::parse(s->ToUtf8());
            TsValue val = json_to_ts(j);

            TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
            // Zero-initialize to ensure padding bytes are zero
            // This is required for ts_value_get_object's TsValue detection heuristic
            std::memset(res, 0, sizeof(TsValue));
            *res = val;
            return res;
        } catch (...) {
            return nullptr;
        }
    }

    void* ts_json_stringify(void* obj, void* replacer, void* space) {
        try {
            nlohmann::json j = ts_to_json(obj);

            int indent = -1;
            if (space) {
                nlohmann::json s = ts_to_json(space);
                if (s.is_number()) {
                    indent = s.get<int>();
                } else if (s.is_string()) {
                    indent = (int)s.get<std::string>().length();
                }
            }

            if (replacer) {
                nlohmann::json r = ts_to_json(replacer);
                if (r.is_array() && j.is_object()) {
                    nlohmann::json filtered = nlohmann::json::object();
                    for (auto& key : r) {
                        if (key.is_string()) {
                            std::string k = key.get<std::string>();
                            if (j.contains(k)) {
                                filtered[k] = j[k];
                            }
                        }
                    }
                    j = filtered;
                }
            }

            std::string s = (indent >= 0) ? j.dump(indent) : j.dump();
            return TsString::Create(s.c_str());
        } catch (...) {
            return TsString::Create("null");
        }
    }
}
