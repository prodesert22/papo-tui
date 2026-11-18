#ifndef PAPO_ABOUT_H
#define PAPO_ABOUT_H

#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>

#define ABOUT_REPOSITORY "https://github.com/prodesert22/papo-tui"

typedef enum {
    ABOUT_NOTHING,
    ABOUT_COPY_SOURCE,
    ABOUT_LEAVE_UP, /* Up or Down from the one line that takes the focus. */
    ABOUT_LEAVE_DOWN
} about_action;
typedef struct {
    int link_row;        /* Window row of the source link in the last draw; -1 when hidden. */
    bool resolved;       /* The notices directory was looked up for this view. */
    char licenses[1024]; /* Installed notices directory; empty when this copy has none. */
} about_view;

/* Draws the program's version, license, author and source link in rows
 * [top, top + rows) of a borrowed window; short areas keep the version and the
 * link. focused marks the source link, the only line that takes the focus. */
void about_draw(about_view *view, WINDOW *window, int top, int left, int width, int rows,
                bool focused);
/* Enter copies the link; the arrows leave the line. The owner carries it out. */
about_action about_key(about_view *view, int type, uint32_t key);
/* ABOUT_COPY_SOURCE when the source link was drawn at that window row. */
about_action about_click(about_view *view, int row);
/* Requests a copy of the source link; false when the clipboard refused it. */
bool about_copy_source(void);

#endif
