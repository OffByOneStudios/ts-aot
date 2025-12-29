#include <iostream>
#include <cstdint>
#include <cstddef>

class TsObject {
public:
    virtual ~TsObject() {}
    virtual class TsEventEmitter* AsEventEmitter() { return nullptr; }
    void* vtable_member;
};

class TsBuffer : public TsObject {
public:
    uint32_t magic = 0x42554646;
    uint8_t* data;
    size_t length;
};

int main() {
    TsBuffer buf;
    std::cout << "TsBuffer size: " << sizeof(TsBuffer) << std::endl;
    std::cout << "Offset of magic: " << offsetof(TsBuffer, magic) << std::endl;
    std::cout << "Offset of data: " << offsetof(TsBuffer, data) << std::endl;
    std::cout << "Offset of length: " << offsetof(TsBuffer, length) << std::endl;
    return 0;
}
