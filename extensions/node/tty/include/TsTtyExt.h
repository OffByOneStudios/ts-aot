#ifndef TS_TTY_EXT_H
#define TS_TTY_EXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Module Functions
// =============================================================================

// tty.isatty(fd) - check if file descriptor is a TTY
bool ts_tty_isatty(int64_t fd);

// =============================================================================
// ReadStream Class
// =============================================================================

void* ts_tty_read_stream_create(int64_t fd);
bool ts_tty_read_stream_get_is_tty(void* stream);
bool ts_tty_read_stream_get_is_raw(void* stream);
bool ts_tty_read_stream_set_raw_mode(void* stream, bool mode);

// =============================================================================
// WriteStream Class
// =============================================================================

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
bool ts_tty_write_stream_write(void* stream, void* data);

#ifdef __cplusplus
}
#endif

#endif // TS_TTY_EXT_H
