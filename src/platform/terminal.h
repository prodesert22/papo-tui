#ifndef PAPO_TERMINAL_H
#define PAPO_TERMINAL_H
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
/* Call before curses changes termios, on the main thread. Fatal signals restore
 * input and terminal modes without using curses, stdio, locks or allocation. */
bool terminal_recovery_begin(void);
void terminal_recovery_end(void);
void terminal_interrupt_begin(void (*handler)(int));
void terminal_interrupt_end(void);
/* Whether plain stdout output, such as --help, may use ANSI colors: stdout is
 * a terminal, NO_COLOR is unset or empty and TERM is not "dumb". On Windows it
 * enables virtual terminal output, which terminal_color_end restores. */
bool terminal_color_begin(void);
void terminal_color_end(void);
/* TERM names the Linux console: linux or a linux-* variant. Its fonts hold a
 * few hundred glyphs, without emoji, braille or most block elements. */
static inline bool terminal_linux_console(void)
{
    const char *term = getenv("TERM");
    return term && !strncmp(term, "linux", 5) && (!term[5] || term[5] == '-');
}
#endif
