#pragma once
#include "TsWritable.h"
#include <uv.h>

class TsWriteStream : public TsWritable {
public:
    TsWriteStream(int fd);
    virtual ~TsWriteStream();

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

private:
    int fd;

    static void OnWrite(uv_fs_t* req);
};

extern "C" {
    void* ts_fs_createWriteStream(void* path);
}
