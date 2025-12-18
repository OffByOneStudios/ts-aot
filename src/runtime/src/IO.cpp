#include "IO.h"
#include "TsString.h"
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>

extern "C" {

void* ts_fs_readFileSync(void* path) {
    TsString* pathStr = (TsString*)path;
    const char* pathCStr = pathStr->ToUtf8();

    std::ifstream t(pathCStr);
    if (!t.is_open()) {
        std::cerr << "Error: Could not open file " << pathCStr << std::endl;
        return TsString::Create("");
    }

    std::stringstream buffer;
    buffer << t.rdbuf();
    
    return TsString::Create(buffer.str().c_str());
}

void ts_fs_writeFileSync(void* path, void* content) {
    TsString* pathStr = (TsString*)path;
    TsString* contentStr = (TsString*)content;
    const char* pathCStr = pathStr->ToUtf8();
    const char* contentCStr = contentStr->ToUtf8();

    std::ofstream t(pathCStr);
    if (!t.is_open()) {
        std::cerr << "Error: Could not open file for writing " << pathCStr << std::endl;
        return;
    }

    t << contentCStr;
}

int64_t ts_parseInt(void* str) {
    TsString* s = (TsString*)str;
    const char* cStr = s->ToUtf8();
    return std::strtoll(cStr, nullptr, 10);
}

}
