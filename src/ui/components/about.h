#ifndef PAPO_ABOUT_H
#define PAPO_ABOUT_H

#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>

#define ABOUT_REPOSITORY "https://github.com/prodesert22/papo-tui"

/* Lines that take the focus, in display order. */
typedef enum {
    ABOUT_SOURCE,  /* The source link; copies it. */
    ABOUT_CHECK,   /* The switch of the daily update check. */
    ABOUT_RELEASE, /* The page of the newer release, when one is known; copies it. */
    ABOUT_ITEMS
} about_item;
typedef enum {
    ABOUT_NOTHING,
    ABOUT_COPY_SOURCE,
    ABOUT_TOGGLE_CHECK,
    ABOUT_COPY_RELEASE,
    ABOUT_LEAVE_UP, /* Up from the first line, Down from the last. */
    ABOUT_LEAVE_DOWN
} about_action;
typedef struct {
    int rows[ABOUT_ITEMS]; /* Window row of each item in the last draw; -1 when hidden. */
    int selection;
    bool resolved;       /* The notices directory was looked up for this view. */
    char licenses[1024]; /* Installed notices directory; empty when this copy has none. */
} about_view;

/* Draws the program's version, license, author and source link, and the state
 * of the update check, in rows [top, top + rows) of a borrowed window. Short
 * areas keep the version, the link and the update lines. focused marks the
 * selected line; automatic is the stored switch of the daily check. */
void about_draw(about_view *view, WINDOW *window, int top, int left, int width, int rows,
                bool focused, bool automatic);
/* Moves between the lines of the last draw; the owner carries out the action. */
about_action about_key(about_view *view, int type, uint32_t key);
/* Selects the line drawn at a window row; ABOUT_NOTHING when there is none. */
about_action about_click(about_view *view, int row);
/* Request a copy of the source link or of the newer release's page; false when
 * the clipboard refused it or there is no such page. */
bool about_copy_source(void);
bool about_copy_release(void);

#endif
