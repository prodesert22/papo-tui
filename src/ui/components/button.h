#ifndef PAPO_BUTTON_H
#define PAPO_BUTTON_H

#include <ncurses.h>

typedef enum { BUTTON_NORMAL, BUTTON_SELECTED, BUTTON_FOCUSED } button_state;
typedef enum { BUTTON_CANVAS, BUTTON_PANEL } button_surface;

/* Borrows the window and label; returns the painted width in terminal cells.
 * Shares white/orange styling, Unicode sanitization and monochrome focus.
 * Placement, hit testing and actions remain owned by the calling screen. */
int button_draw(WINDOW *window, int row, int column, int width, const char *label,
                button_state state, button_surface surface);

#endif
