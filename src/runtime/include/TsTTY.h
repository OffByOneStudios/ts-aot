#pragma once
#include "TsReadable.h"
#include "TsWritable.h"
#include "TsString.h"
#include "TsArray.h"
#include <uv.h>

// Forward declaration
class TsBuffer;

// =============================================================================
// TsTTYReadStream - tty.ReadStream
// Readable stream for TTY input (e.g., stdin)
// =============================================================================
class TsTTYReadStream : public TsReadable {
public:
    static TsTTYReadStream* Create(int fd);
    virtual ~TsTTYReadStream();

    // TTY-specific properties
    bool IsTTY() const { return true; }
    bool IsRaw() const { return isRaw_; }

    // TTY-specific methods
    bool SetRawMode(bool mode);

    // Safe casting helper
    virtual TsReadable* AsReadable() override { return this; }

protected:
    TsTTYReadStream(int fd);

private:
    uv_tty_t* ttyHandle_;
    int fd_;
    bool isRaw_;

    // libuv callbacks
    static void OnAlloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

    void StartReading();
};

// =============================================================================
// TsTTYWriteStream - tty.WriteStream
// Writable stream for TTY output (e.g., stdout, stderr)
// =============================================================================
class TsTTYWriteStream : public TsWritable {
public:
    static TsTTYWriteStream* Create(int fd);
    virtual ~TsTTYWriteStream();

    // TTY-specific properties
    bool IsTTY() const { return true; }
    int GetColumns() const;
    int GetRows() const;
    TsArray* GetWindowSize() const;

    // Cursor/screen control methods
    bool ClearLine(int dir);
    bool ClearScreenDown();
    bool CursorTo(int x, int y);
    bool MoveCursor(int dx, int dy);

    // Color support
    int GetColorDepth() const;
    bool HasColors(int count = 16) const;

    // Stream methods (from TsWritable)
    virtual bool Write(void* data, size_t length) override;
    virtual void End() override;

    // Safe casting helper
    virtual TsWritable* AsWritable() override { return this; }

protected:
    TsTTYWriteStream(int fd);

private:
    uv_tty_t* ttyHandle_;
    int fd_;
    int cachedColumns_;
    int cachedRows_;

    // Update cached window size
    void UpdateWindowSize();

    // Write raw data to TTY
    bool WriteRaw(const char* data, size_t length);

    // libuv write callback
    static void OnWrite(uv_write_t* req, int status);
};

// =============================================================================
// C API - Module functions
// =============================================================================
extern "C" {
    // tty.isatty(fd) - check if file descriptor is a TTY
    bool ts_tty_isatty(int64_t fd);

    // ReadStream
    void* ts_tty_read_stream_create(int64_t fd);
    bool ts_tty_read_stream_get_is_tty(void* stream);
    bool ts_tty_read_stream_get_is_raw(void* stream);
    bool ts_tty_read_stream_set_raw_mode(void* stream, bool mode);

    // WriteStream
    void* ts_tty_write_stream_create(int64_t fd);
    bool ts_tty_write_stream_get_is_tty(void* stream);
    int64_t ts_tty_write_stream_get_columns(void* stream);
    int64_t ts_tty_write_stream_get_rows(void* stream);
    void* ts_tty_write_stream_get_window_size(void* stream);
    bool ts_tty_write_stream_clear_line(void* stream, int64_t dir);
    bool ts_tty_write_stream_clear_screen_down(void* stream);
    bool ts_tty_write_stream_cursor_to(void* stream, int64_t x, int64_t y);
    bool ts_tty_write_stream_move_cursor(void* stream, int64_t dx, int64_t dy);
    int64_t ts_tty_write_stream_get_color_depth(void* stream);
    bool ts_tty_write_stream_has_colors(void* stream, int64_t count);

    // WriteStream write method
    bool ts_tty_write_stream_write(void* stream, void* data);
}
