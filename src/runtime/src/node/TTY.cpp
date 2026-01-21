#include "TsTTY.h"
#include "TsBuffer.h"
#include "TsObject.h"
#include "TsError.h"
#include "GC.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>


// =============================================================================
// TsTTYReadStream Implementation
// =============================================================================

TsTTYReadStream* TsTTYReadStream::Create(int fd) {
    void* mem = ts_alloc(sizeof(TsTTYReadStream));
    return new (mem) TsTTYReadStream(fd);
}

TsTTYReadStream::TsTTYReadStream(int fd) : fd_(fd), isRaw_(false) {
    // Allocate TTY handle
    ttyHandle_ = (uv_tty_t*)ts_alloc(sizeof(uv_tty_t));

    // Initialize TTY handle
    uv_loop_t* loop = uv_default_loop();
    int r = uv_tty_init(loop, ttyHandle_, fd, 1);  // 1 = readable
    if (r != 0) {
        ttyHandle_ = nullptr;
        return;
    }

    // Store self reference for callbacks
    ttyHandle_->data = this;

    // Start reading
    StartReading();
}

TsTTYReadStream::~TsTTYReadStream() {
    if (ttyHandle_) {
        uv_close((uv_handle_t*)ttyHandle_, nullptr);
    }
}

bool TsTTYReadStream::SetRawMode(bool mode) {
    if (!ttyHandle_) return false;

    // UV_TTY_MODE_NORMAL = 0, UV_TTY_MODE_RAW = 1
    uv_tty_mode_t uvMode = mode ? UV_TTY_MODE_RAW : UV_TTY_MODE_NORMAL;
    int r = uv_tty_set_mode(ttyHandle_, uvMode);
    if (r == 0) {
        isRaw_ = mode;
        return true;
    }
    return false;
}

void TsTTYReadStream::OnAlloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)ts_alloc(suggested_size);
    buf->len = (unsigned long)suggested_size;
}

void TsTTYReadStream::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    TsTTYReadStream* self = (TsTTYReadStream*)stream->data;
    if (!self) return;

    if (nread > 0) {
        // Create a buffer with the data
        TsBuffer* data = TsBuffer::Create((size_t)nread);
        memcpy(data->GetData(), buf->base, (size_t)nread);
        void* args[] = { data };
        self->Emit("data", 1, args);
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            self->ended_ = true;
            self->Emit("end", 0, nullptr);
        } else {
            // Error
            void* err = ts_error_create(TsString::Create(uv_strerror((int)nread)));
            void* errArgs[] = { err };
            self->Emit("error", 1, errArgs);
        }
    }
}

void TsTTYReadStream::StartReading() {
    if (!ttyHandle_) return;
    uv_read_start((uv_stream_t*)ttyHandle_, OnAlloc, OnRead);
}

// =============================================================================
// TsTTYWriteStream Implementation
// =============================================================================

TsTTYWriteStream* TsTTYWriteStream::Create(int fd) {
    void* mem = ts_alloc(sizeof(TsTTYWriteStream));
    return new (mem) TsTTYWriteStream(fd);
}

TsTTYWriteStream::TsTTYWriteStream(int fd) : fd_(fd), cachedColumns_(80), cachedRows_(24) {
    // Allocate TTY handle
    ttyHandle_ = (uv_tty_t*)ts_alloc(sizeof(uv_tty_t));

    // Initialize TTY handle
    uv_loop_t* loop = uv_default_loop();
    int r = uv_tty_init(loop, ttyHandle_, fd, 0);  // 0 = writable
    if (r != 0) {
        ttyHandle_ = nullptr;
        return;
    }

    // Store self reference for callbacks
    ttyHandle_->data = this;

    // Get initial window size
    UpdateWindowSize();
}

TsTTYWriteStream::~TsTTYWriteStream() {
    if (ttyHandle_) {
        uv_close((uv_handle_t*)ttyHandle_, nullptr);
    }
}

void TsTTYWriteStream::UpdateWindowSize() {
    if (!ttyHandle_) return;

    int width = 0, height = 0;
    int r = uv_tty_get_winsize(ttyHandle_, &width, &height);
    if (r == 0) {
        cachedColumns_ = width;
        cachedRows_ = height;
    }
}

int TsTTYWriteStream::GetColumns() const {
    // Return cached value, could call UpdateWindowSize() for fresh value
    return cachedColumns_;
}

int TsTTYWriteStream::GetRows() const {
    return cachedRows_;
}

TsArray* TsTTYWriteStream::GetWindowSize() const {
    TsArray* result = TsArray::Create();
    result->Push((int64_t)cachedColumns_);
    result->Push((int64_t)cachedRows_);
    return result;
}

bool TsTTYWriteStream::WriteRaw(const char* data, size_t length) {
    if (!ttyHandle_) return false;

    // Allocate write request
    uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    req->data = this;

    // Copy data (GC-allocated)
    char* buf = (char*)ts_alloc(length);
    memcpy(buf, data, length);

    uv_buf_t uvBuf = uv_buf_init(buf, (unsigned int)length);

    int r = uv_write(req, (uv_stream_t*)ttyHandle_, &uvBuf, 1, OnWrite);
    return r == 0;
}

void TsTTYWriteStream::OnWrite(uv_write_t* req, int status) {
    TsTTYWriteStream* self = (TsTTYWriteStream*)req->data;
    if (!self) return;

    if (status < 0) {
        void* err = ts_error_create(TsString::Create(uv_strerror(status)));
        void* errArgs[] = { err };
        self->Emit("error", 1, errArgs);
    }
}

bool TsTTYWriteStream::Write(void* data, size_t length) {
    // data is expected to be a TsString* or TsBuffer*
    TsString* str = dynamic_cast<TsString*>((TsObject*)data);
    if (str) {
        const char* cstr = str->ToUtf8();
        return WriteRaw(cstr, strlen(cstr));
    }

    TsBuffer* buf = dynamic_cast<TsBuffer*>((TsObject*)data);
    if (buf) {
        return WriteRaw((const char*)buf->GetData(), buf->GetLength());
    }

    return false;
}

void TsTTYWriteStream::End() {
    TsWritable::End();
    Emit("finish", 0, nullptr);
}

// ANSI escape sequences for terminal control
bool TsTTYWriteStream::ClearLine(int dir) {
    const char* seq = nullptr;
    switch (dir) {
        case -1:  // Clear to left
            seq = "\x1b[1K";
            break;
        case 0:   // Clear entire line
            seq = "\x1b[2K";
            break;
        case 1:   // Clear to right (default)
        default:
            seq = "\x1b[0K";
            break;
    }
    return WriteRaw(seq, strlen(seq));
}

bool TsTTYWriteStream::ClearScreenDown() {
    const char* seq = "\x1b[J";
    return WriteRaw(seq, strlen(seq));
}

bool TsTTYWriteStream::CursorTo(int x, int y) {
    char buf[32];
    int len;

    if (y < 0) {
        // Only horizontal movement
        len = snprintf(buf, sizeof(buf), "\x1b[%dG", x + 1);  // 1-based
    } else {
        // Full positioning
        len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1);  // 1-based
    }

    return WriteRaw(buf, (size_t)len);
}

bool TsTTYWriteStream::MoveCursor(int dx, int dy) {
    char buf[64];
    int len = 0;

    // Vertical movement
    if (dy > 0) {
        len += snprintf(buf + len, sizeof(buf) - len, "\x1b[%dB", dy);  // Down
    } else if (dy < 0) {
        len += snprintf(buf + len, sizeof(buf) - len, "\x1b[%dA", -dy);  // Up
    }

    // Horizontal movement
    if (dx > 0) {
        len += snprintf(buf + len, sizeof(buf) - len, "\x1b[%dC", dx);  // Right
    } else if (dx < 0) {
        len += snprintf(buf + len, sizeof(buf) - len, "\x1b[%dD", -dx);  // Left
    }

    if (len > 0) {
        return WriteRaw(buf, (size_t)len);
    }
    return true;
}

int TsTTYWriteStream::GetColorDepth() const {
    // Check environment variables for color support
    const char* colorterm = getenv("COLORTERM");
    if (colorterm) {
        if (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0) {
            return 24;  // True color (16M colors)
        }
    }

    const char* term = getenv("TERM");
    if (term) {
        if (strstr(term, "256color") || strstr(term, "256")) {
            return 8;  // 256 colors
        }
        if (strstr(term, "color") || strstr(term, "ansi") ||
            strcmp(term, "xterm") == 0 || strcmp(term, "linux") == 0) {
            return 4;  // 16 colors
        }
    }

    // Windows Terminal and modern terminals typically support at least 4-bit color
#ifdef _WIN32
    return 4;
#else
    return 1;  // Monochrome fallback
#endif
}

bool TsTTYWriteStream::HasColors(int count) const {
    int depth = GetColorDepth();

    // Calculate required depth for count colors
    // 2^depth >= count
    if (count <= 2) return depth >= 1;
    if (count <= 16) return depth >= 4;
    if (count <= 256) return depth >= 8;
    return depth >= 24;
}

// =============================================================================
// C API Implementation
// =============================================================================

extern "C" {

bool ts_tty_isatty(int64_t fd) {
    uv_handle_type type = uv_guess_handle((int)fd);
    return type == UV_TTY;
}

// ReadStream
void* ts_tty_read_stream_create(int64_t fd) {
    return TsTTYReadStream::Create((int)fd);
}

bool ts_tty_read_stream_get_is_tty(void* stream) {
    TsTTYReadStream* tty = dynamic_cast<TsTTYReadStream*>((TsObject*)stream);
    return tty ? tty->IsTTY() : false;
}

bool ts_tty_read_stream_get_is_raw(void* stream) {
    TsTTYReadStream* tty = dynamic_cast<TsTTYReadStream*>((TsObject*)stream);
    return tty ? tty->IsRaw() : false;
}

bool ts_tty_read_stream_set_raw_mode(void* stream, bool mode) {
    TsTTYReadStream* tty = dynamic_cast<TsTTYReadStream*>((TsObject*)stream);
    return tty ? tty->SetRawMode(mode) : false;
}

// WriteStream
void* ts_tty_write_stream_create(int64_t fd) {
    return TsTTYWriteStream::Create((int)fd);
}

bool ts_tty_write_stream_get_is_tty(void* stream) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    return tty ? tty->IsTTY() : false;
}

int64_t ts_tty_write_stream_get_columns(void* stream) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    return tty ? (int64_t)tty->GetColumns() : 80;
}

int64_t ts_tty_write_stream_get_rows(void* stream) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    return tty ? (int64_t)tty->GetRows() : 24;
}

void* ts_tty_write_stream_get_window_size(void* stream) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    return tty ? tty->GetWindowSize() : nullptr;
}

bool ts_tty_write_stream_clear_line(void* stream, int64_t dir) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    return tty ? tty->ClearLine((int)dir) : false;
}

bool ts_tty_write_stream_clear_screen_down(void* stream) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    return tty ? tty->ClearScreenDown() : false;
}

bool ts_tty_write_stream_cursor_to(void* stream, int64_t x, int64_t y) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    return tty ? tty->CursorTo((int)x, (int)y) : false;
}

bool ts_tty_write_stream_move_cursor(void* stream, int64_t dx, int64_t dy) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    return tty ? tty->MoveCursor((int)dx, (int)dy) : false;
}

int64_t ts_tty_write_stream_get_color_depth(void* stream) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    return tty ? (int64_t)tty->GetColorDepth() : 1;
}

bool ts_tty_write_stream_has_colors(void* stream, int64_t count) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    return tty ? tty->HasColors((int)count) : false;
}

bool ts_tty_write_stream_write(void* stream, void* data) {
    TsTTYWriteStream* tty = dynamic_cast<TsTTYWriteStream*>((TsObject*)stream);
    if (!tty || !data) return false;

    // Unbox the data if needed
    void* rawData = ts_value_get_object((TsValue*)data);
    if (!rawData) rawData = data;

    TsString* str = dynamic_cast<TsString*>((TsObject*)rawData);
    if (str) {
        const char* cstr = str->ToUtf8();
        return tty->Write((void*)str, strlen(cstr));
    }

    TsBuffer* buf = dynamic_cast<TsBuffer*>((TsObject*)rawData);
    if (buf) {
        return tty->Write((void*)buf, buf->GetLength());
    }

    return false;
}

} // extern "C"
