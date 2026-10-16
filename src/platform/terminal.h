#ifndef PAPO_TERMINAL_H
#define PAPO_TERMINAL_H
#include <stdbool.h>
/* Call before curses changes termios, on the main thread. Fatal signals restore
 * input and terminal modes without using curses, stdio, locks or allocation. */
bool terminal_recovery_begin(void);
void terminal_recovery_end(void);
void terminal_interrupt_begin(void (*handler)(int));
void terminal_interrupt_end(void);
#endif
