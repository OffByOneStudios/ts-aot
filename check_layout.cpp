#include <iostream>
#include <cstdint>
#include <cstddef>

class TsObject {
public:
    virtual ~TsObject() {}
    virtual class TsEventEmitter* AsEventEmitter() { return nullptr; }
    void* vtable;
    uint32_t magic;
};

class TsMap : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x4D415053;
private:
    uint32_t map_magic = MAGIC;
    void* impl;
};

int main() {
    std::cout << "TsObject size: " << sizeof(TsObject) << std::endl;
    std::cout << "TsMap size: " << sizeof(TsMap) << std::endl;
    
    TsMap m;
    char* p = (char*)&m;
    std::cout << "Magic at offset 16: 0x" << std::hex << *(uint32_t*)(p + 16) << std::endl;
    std::cout << "Magic at offset 20: 0x" << std::hex << *(uint32_t*)(p + 20) << std::endl;
    std::cout << "Magic at offset 24: 0x" << std::hex << *(uint32_t*)(p + 24) << std::endl;
    return 0;
}
