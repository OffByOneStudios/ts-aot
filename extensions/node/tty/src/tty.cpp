// TTY extension for ts-aot
// extern "C" wrappers for TsTTYReadStream and TsTTYWriteStream
// Class implementations remain in src/runtime/src/node/TTY.cpp

#include "TsTTY.h"
#include "TsBuffer.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "TsError.h"
#include "GC.h"
#include <cstring>

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
    void* rawData = ts_nanbox_safe_unbox(data);

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
