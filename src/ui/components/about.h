#ifndef PAPO_ABOUT_H
#define PAPO_ABOUT_H

#include <ncurses.h>
#include <stdbool.h>

#define ABOUT_REPOSITORY "https://github.com/prodesert22/papo-tui"

typedef struct {
    int link_row;        /* Window row of the source link in the last draw; -1 when hidden. */
    bool resolved;       /* The notices directory was looked up for this view. */
    char licenses[1024]; /* Installed notices directory; empty when this copy has none. */
} about_view;

/* Draws the program's version, license, author and source link in rows
 * [top, top + rows) of a borrowed window; short areas drop the description. */
void about_draw(about_view *view, WINDOW *window, int top, int left, int width, int rows);
/* Requests a copy of the source link; false when the clipboard refused it. */
bool about_copy_link(void);

#endif
