#pragma once
#include "TsWritable.h"
#include <uv.h>

struct WriteStreamOptions {
    int64_t start;      // Starting position in the file (-1 = append/default)
    bool autoClose;     // Auto close on end/error
    int flags;          // File open flags (O_WRONLY | O_CREAT | O_TRUNC default)
    int mode;           // File mode (0o666 default)

    WriteStreamOptions() : start(-1), autoClose(true), flags(-1), mode(0666) {}
};

class TsWriteStream : public TsWritable {
public:
    TsWriteStream(int fd, const WriteStreamOptions& opts = WriteStreamOptions());
    virtual ~TsWriteStream();

    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    // Properties
    int64_t GetBytesWritten() const { return bytesWritten; }
    const char* GetPath() const { return path; }
    void SetPath(const char* p);
    bool IsPending() const { return !started; }

private:
    int fd;
    int64_t position;       // Current write position
    int64_t bytesWritten;   // Total bytes written
    char* path;             // File path
    bool autoClose;
    bool started;
    bool closed;

    static void OnWrite(uv_fs_t* req);
};

extern "C" {
    void* ts_fs_createWriteStream(void* path);
    void* ts_fs_createWriteStream_opts(void* path, void* options);

    // WriteStream property accessors
    int64_t ts_write_stream_bytes_written(void* stream);
    void* ts_write_stream_path(void* stream);
    bool ts_write_stream_pending(void* stream);
}
