#pragma once
#include "TsEventEmitter.h"
#include <uv.h>

class TsWriteStream : public TsEventEmitter {
public:
    TsWriteStream(int fd);
    virtual ~TsWriteStream();

    void Write(void* data, size_t length);
    void End();

private:
    int fd;
    bool closed;

    static void OnWrite(uv_fs_t* req);
};

extern "C" {
    void* ts_fs_createWriteStream(void* path);
}
