#pragma once
#include "TsMap.h"

class TsError : public TsMap {
public:
    static TsError* Create(TsString* message);
    
    TsString* GetMessage();
    TsString* GetStack();

private:
    TsError();
    void CaptureStack();
};

extern "C" {
    void* ts_error_create(void* message);
    void* ts_error_create_with_options(void* message, void* options);  // ES2022
}
