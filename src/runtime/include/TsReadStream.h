#pragma once
#include "TsEventEmitter.h"
#include "TsBuffer.h"
#include <uv.h>

class TsReadStream : public TsEventEmitter {
public:
    TsReadStream(int fd);
    virtual ~TsReadStream();
    
    void Start();
    void Close();

private:
    int fd;
    TsBuffer* buffer;
    bool closed;
    
    static void OnRead(uv_fs_t* req);
};

extern "C" {
    void* ts_fs_createReadStream(void* path);
}
