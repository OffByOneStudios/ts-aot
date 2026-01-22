#include <cstdio>
#include <cstdint>
#include <unicode/unistr.h>

class TsString {
public:
    uint32_t magic;
    uint32_t length;
    bool isSmall;
    union {
        struct {
            void* impl;
            char* utf8Buffer;
        } heap;
        char inlineBuffer[64];
    } data;
};

int main() {
    printf("sizeof(TsString) = %zu\n", sizeof(TsString));
    printf("offsetof magic = %zu\n", offsetof(TsString, magic));
    printf("offsetof length = %zu\n", offsetof(TsString, length));
    printf("offsetof isSmall = %zu\n", offsetof(TsString, isSmall));
    printf("offsetof data = %zu\n", offsetof(TsString, data));
    return 0;
}
