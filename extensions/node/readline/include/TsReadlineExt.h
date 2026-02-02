#ifndef TS_READLINE_EXT_H
#define TS_READLINE_EXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Interface creation and methods
void* ts_readline_create_interface(void* options);
void ts_readline_close(void* rl);
void ts_readline_pause(void* rl);
void ts_readline_resume(void* rl);
void ts_readline_prompt(void* rl, void* preserveCursor);
void ts_readline_set_prompt(void* rl, void* prompt);
void ts_readline_question(void* rl, void* query, void* callback);
void ts_readline_write(void* rl, void* data);
void* ts_readline_get_line(void* rl);
int64_t ts_readline_get_cursor(void* rl);
void* ts_readline_get_prompt(void* rl);

// Utility functions - ANSI escape sequences
void ts_readline_clear_line(void* stream, void* dir);
void ts_readline_clear_screen_down(void* stream);
void ts_readline_cursor_to(void* stream, void* x, void* y);
void ts_readline_move_cursor(void* stream, void* dx, void* dy);
void ts_readline_emit_keypress_events(void* stream, void* iface);

// Async iterator support
void* ts_readline_get_async_iterator(void* rl);
void* ts_readline_async_iterator_next(void* iter);

// Signal event emitters
void ts_readline_emit_sigint(void* rl);
void ts_readline_emit_sigtstp(void* rl);
void ts_readline_emit_sigcont(void* rl);

#ifdef __cplusplus
}
#endif

#endif // TS_READLINE_EXT_H
