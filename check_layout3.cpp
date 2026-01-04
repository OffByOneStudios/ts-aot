#include <iostream>
#include <cstddef>
#include <cstdint>

class TsObject {
public:
    virtual ~TsObject() {}
    void* vtable;
    uint32_t magic;
};

class TsMap : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x4D415053;
protected:
    TsMap() {}
private:
    uint32_t magic = MAGIC;
    void* impl;
};

int main() {
    std::cout << \"sizeof(TsObject): \" << sizeof(TsObject) << std::endl;
    std::cout << \"sizeof(TsMap): \" << sizeof(TsMap) << std::endl;
    std::cout << \"offsetof magic in TsObject: \" << offsetof(TsObject, magic) << std::endl;
    // TsMap is protected, can't use offsetof directly
    return 0;
}
