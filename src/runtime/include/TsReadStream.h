#pragma once
#include "TsReadable.h"
#include "TsBuffer.h"
#include <uv.h>

class TsReadStream : public TsReadable {
public:
    TsReadStream(int fd);
    virtual ~TsReadStream();
    
    void Start();
    void Close();
    virtual void Pause() override;
    virtual void Resume() override;

    virtual void On(const char* event, void* callback) override;

private:
    int fd;
    TsBuffer* buffer;
    bool closed;
    
    static void OnRead(uv_fs_t* req);
};

extern "C" {
    void* ts_fs_createReadStream(void* path);
}
