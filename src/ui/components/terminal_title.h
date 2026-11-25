#ifndef PAPO_TERMINAL_TITLE_H
#define PAPO_TERMINAL_TITLE_H

#include <stdbool.h>
#include <stddef.h>

/* Main-thread terminal output. Labels are borrowed, sanitized and bounded;
 * identical titles produce no output. Call begin/end once around the TUI. */
void terminal_title_begin(void);
/* unread puts the unread dot first, for news the channel and server lists
 * would mark with their red dot: conversations that are not muted. */
void terminal_title_set(const char *conversation, const char *server, size_t mentions, bool unread);
void terminal_title_reset(void);
/* The title last written, or "" when none is shown. */
const char *terminal_title_current(void);
void terminal_title_end(void);

#endif
