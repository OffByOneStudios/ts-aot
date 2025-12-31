#include <iostream>
#include 'src/runtime/include/TsMap.h'
int main() {
    std::cout << 'sizeof(TsObject): ' << sizeof(TsObject) << std::endl;
    std::cout << 'offsetof(TsMap, magic) is not easy' << std::endl;
    TsMap* m = TsMap::Create();
    char* p = (char*)m;
    for (int i = 0; i < 40; i += 4) {
        uint32_t v = *(uint32_t*)(p + i);
        if (v == 0x4D415053) {
            std::cout << 'Found MAPS magic at offset ' << i << std::endl;
        }
    }
    return 0;
}
