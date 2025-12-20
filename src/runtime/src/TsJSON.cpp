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
            else if (val.type == ValueType::NUMBER_DBL) raw = val.i_val;
            else raw = (int64_t)val.ptr_val;
            arr->Push(raw);
        }
        TsValue v;
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
        v.type = ValueType::OBJECT_PTR;
        v.ptr_val = map;
        return v;
    }
    return TsValue(nullptr);
}

static nlohmann::json ts_to_json_internal(void* p, std::set<void*>& visited, bool is_object_value = false) {
    if (!p) return nullptr;
    if ((uintptr_t)p < 4096) return (int64_t)p;

    // Check if it's a bit-casted double
    uint64_t val = (uint64_t)p;
    if (val > 0x00007FFFFFFFFFFF) {
        double d;
        std::memcpy(&d, &val, sizeof(double));
        return d;
    }

    // Check magic numbers first
    uint32_t magic = *(uint32_t*)p;
    if (magic == TsString::MAGIC) {
        return ((TsString*)p)->ToUtf8();
    }

    if (magic == TsDate::MAGIC) {
        return ((TsDate*)p)->ToISOString()->ToUtf8();
    }

    if (magic == TsRegExp::MAGIC) {
        return nlohmann::json::object();
    }

    // For objects (Arrays and Maps), check for circular references
    if (magic == TsArray::MAGIC || magic == TsMap::MAGIC) {
        if (visited.find(p) != visited.end()) {
            throw std::runtime_error("Circular reference in JSON.stringify");
        }
        visited.insert(p);

        nlohmann::json j;
        if (magic == TsArray::MAGIC) {
            TsArray* arr = (TsArray*)p;
            j = nlohmann::json::array();
            for (int64_t i = 0; i < arr->Length(); ++i) {
                j.push_back(ts_to_json_internal((void*)arr->Get(i), visited));
            }
        } else {
            TsMap* map = (TsMap*)p;
            j = nlohmann::json::object();
            TsArray* keys = (TsArray*)map->GetKeys();
            for (int64_t i = 0; i < keys->Length(); ++i) {
                TsString* key = (TsString*)keys->Get(i);
                void* val_ptr = (void*)map->Get(key);
                
                // In JS, undefined values in objects are omitted
                if (val_ptr == nullptr) continue;
                
                // Check if it's a TsValue with UNDEFINED type
                bool is_undefined = false;
                if ((uintptr_t)val_ptr >= 4096) {
                    uint8_t type_byte = *(uint8_t*)val_ptr;
                    if (type_byte == (uint8_t)ValueType::UNDEFINED) {
                        is_undefined = true;
                    }
                }
                
                if (is_undefined) continue;

                j[key->ToUtf8()] = ts_to_json_internal(val_ptr, visited, true);
            }
        }

        visited.erase(p);
        return j;
    }

    // Check if it's a TsValue
    uint8_t type_byte = *(uint8_t*)p;
    if (type_byte <= 5) {
        TsValue* v = (TsValue*)p;
        switch (v->type) {
            case ValueType::UNDEFINED: return nullptr;
            case ValueType::NUMBER_INT: return v->i_val;
            case ValueType::NUMBER_DBL: return v->d_val;
            case ValueType::BOOLEAN: return v->b_val;
            case ValueType::STRING_PTR: return ts_to_json_internal(v->ptr_val, visited);
            case ValueType::OBJECT_PTR: return ts_to_json_internal(v->ptr_val, visited);
        }
    }

    return (int64_t)p;
}

static nlohmann::json ts_to_json(void* p) {
    std::set<void*> visited;
    return ts_to_json_internal(p, visited, false);
}

extern "C" {
    void* ts_json_parse(void* json_str) {
        if (!json_str) return nullptr;
        TsString* s = (TsString*)json_str;
        try {
            nlohmann::json j = nlohmann::json::parse(s->ToUtf8());
            TsValue val = json_to_ts(j);
            
            TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
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
