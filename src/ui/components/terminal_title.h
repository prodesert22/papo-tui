#ifndef PAPO_TERMINAL_TITLE_H
#define PAPO_TERMINAL_TITLE_H

#include <stddef.h>

/* Main-thread terminal output. Labels are borrowed, sanitized and bounded;
 * identical titles produce no output. Call begin/end once around the TUI. */
void terminal_title_begin(void);
void terminal_title_set(const char *conversation, const char *server, size_t mentions);
void terminal_title_reset(void);
void terminal_title_end(void);

#endif
