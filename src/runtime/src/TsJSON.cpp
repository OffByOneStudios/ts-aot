#include "TsJSON.h"
#include "TsNanBox.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsDate.h"
#include "TsRegExp.h"
#include "TsFlatObject.h"
#include "GC.h"
#include <nlohmann/json.hpp>
#include <string>
#include <cstring>

#include <set>
#include <stdexcept>
#include <cmath>

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
            // Convert TsValue struct to NaN-boxed for array storage
            uint64_t nb = (uint64_t)(uintptr_t)nanbox_from_tagged(val);
            arr->Push((int64_t)nb);
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
        case ValueType::NUMBER_DBL: {
            // Format whole numbers as integers (10.0 -> 10, not 10.0)
            double d = v.d_val;
            double intPart;
            if (std::modf(d, &intPart) == 0.0 &&
                d >= -9007199254740992.0 && d <= 9007199254740992.0) {
                return (int64_t)d;
            }
            return d;
        }
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

    // Decode NaN-boxed values
    uint64_t nb = (uint64_t)(uintptr_t)p;
    if (nanbox_is_undefined(nb)) return nullptr;
    if (nanbox_is_null(nb)) return nullptr;
    if (nanbox_is_bool(nb)) return nanbox_to_bool(nb);
    if (nanbox_is_int32(nb)) {
        return (int64_t)nanbox_to_int32(nb);
    }
    if (nanbox_is_double(nb)) {
        double d = nanbox_to_double(nb);
        double intPart;
        if (std::modf(d, &intPart) == 0.0 &&
            d >= -9007199254740992.0 && d <= 9007199254740992.0) {
            return (int64_t)d;
        }
        return d;
    }

    // Must be a pointer (nanbox_is_ptr). Extract the raw pointer.
    // Note: for heap pointers, the NaN-boxed representation IS the raw pointer value.

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

    // Flat inline-slot object (magic at offset 0)
    if (magic == FLAT_MAGIC) {
        if (visited.find(p) != visited.end()) {
            throw std::runtime_error("Circular reference in JSON.stringify");
        }
        visited.insert(p);
        uint32_t shapeId = flat_object_shape_id(p);
        ShapeDescriptor* desc = ts_shape_lookup(shapeId);
        nlohmann::json j = nlohmann::json::object();
        if (desc) {
            for (uint32_t i = 0; i < desc->numSlots; i++) {
                uint64_t val = *(uint64_t*)((char*)p + 16 + i * 8);
                j[desc->propNames[i]] = ts_to_json_internal((void*)(uintptr_t)val, visited);
            }
            // Check overflow map
            void* overflow = *(void**)((char*)p + 16 + desc->numSlots * 8);
            if (overflow) {
                TsMap* map = (TsMap*)overflow;
                TsArray* keys = (TsArray*)map->GetKeys();
                for (int64_t i = 0; i < keys->Length(); i++) {
                    uint64_t keyNB = (uint64_t)keys->Get(i);
                    if (!nanbox_is_ptr(keyNB)) continue;
                    void* keyPtr = nanbox_to_ptr(keyNB);
                    if (!keyPtr || *(uint32_t*)keyPtr != TsString::MAGIC) continue;
                    TsString* keyStr = (TsString*)keyPtr;
                    TsValue keyTv;
                    std::memset(&keyTv, 0, sizeof(TsValue));
                    keyTv.type = ValueType::STRING_PTR;
                    keyTv.ptr_val = keyStr;
                    TsValue val = map->Get(keyTv);
                    if (val.type == ValueType::UNDEFINED) continue;
                    j[keyStr->ToUtf8()] = ts_value_to_json(val, visited);
                }
            }
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
            // keys->Get(i) returns a NaN-boxed value (pointer to TsString)
            uint64_t keyNB = (uint64_t)keys->Get(i);
            if (!nanbox_is_ptr(keyNB)) continue;  // JSON only supports string keys
            void* keyPtr = nanbox_to_ptr(keyNB);
            if (!keyPtr) continue;
            if (*(uint32_t*)keyPtr != TsString::MAGIC) continue;

            TsString* keyStr = (TsString*)keyPtr;
            std::string keyStdStr = keyStr->ToUtf8();

            // Create a TsValue struct for the key to pass to map->Get
            TsValue keyTv;
            std::memset(&keyTv, 0, sizeof(TsValue));
            keyTv.type = ValueType::STRING_PTR;
            keyTv.ptr_val = keyStr;
            TsValue val = map->Get(keyTv);
            if (val.type == ValueType::UNDEFINED) continue;

            j[keyStdStr] = ts_value_to_json(val, visited);
        }
        visited.erase(p);
        return j;
    }

    // Fallback: unknown object type
    return nlohmann::json::object();
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
            // Convert TsValue struct to NaN-boxed representation
            return (void*)nanbox_from_tagged(val);
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
