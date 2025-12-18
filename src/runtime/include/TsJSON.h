#pragma once

#include <cstdint>

extern "C" {
    void* ts_json_parse(void* json_str);
    void* ts_json_stringify(void* obj);
}
