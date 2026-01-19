#pragma once
#include "TsReadable.h"
#include "TsBuffer.h"
#include <uv.h>

struct ReadStreamOptions {
    int64_t start;      // Starting position in the file
    int64_t end;        // Ending position in the file (-1 = EOF)
    size_t highWaterMark;  // Buffer size
    bool autoClose;     // Auto close on end/error

    ReadStreamOptions() : start(0), end(-1), highWaterMark(65536), autoClose(true) {}
};

class TsReadStream : public TsReadable {
public:
    TsReadStream(int fd, const ReadStreamOptions& opts = ReadStreamOptions());
    virtual ~TsReadStream();

    void Start();
    void Close();
    virtual void Pause() override;
    virtual void Resume() override;

    virtual void On(const char* event, void* callback) override;

    // Properties
    int64_t GetBytesRead() const { return bytesRead; }
    const char* GetPath() const { return path; }
    void SetPath(const char* p);
    bool IsPending() const { return !started; }

private:
    int fd;
    TsBuffer* buffer;
    bool closed;
    bool started;
    int64_t position;    // Current read position
    int64_t endPosition; // End position (-1 = EOF)
    int64_t bytesRead;   // Total bytes read
    char* path;          // File path
    bool autoClose;

    static void OnRead(uv_fs_t* req);
};

extern "C" {
    void* ts_fs_createReadStream(void* path);
    void* ts_fs_createReadStream_opts(void* path, void* options);
}
