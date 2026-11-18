#ifndef PAPO_UPDATES_H
#define PAPO_UPDATES_H

#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>

/* The Atualizações section of Settings: the switches of the update check, what
 * the last check found and what the newer version's download is doing. */

/* Lines that take the focus, in display order. */
typedef enum {
    UPDATES_CHECK,   /* The switch of the daily update check. */
    UPDATES_BETA,    /* The switch that lets pre-releases be offered too. */
    UPDATES_RELEASE, /* The page of the newer release, when one is known; copies it. */
    UPDATES_COMMAND, /* How to install the package just saved; copies it. */
    UPDATES_ITEMS
} updates_item;
typedef enum {
    UPDATES_NOTHING,
    UPDATES_TOGGLE_CHECK,
    UPDATES_TOGGLE_BETA,
    UPDATES_COPY_RELEASE,
    UPDATES_COPY_COMMAND,
    UPDATES_LEAVE_UP, /* Up from the first line, Down from the last. */
    UPDATES_LEAVE_DOWN
} updates_action;
typedef struct {
    int rows[UPDATES_ITEMS]; /* Window row of each item in the last draw; -1 when hidden. */
    int selection;
} updates_view;

/* Draws the section in rows [top, top + rows) of a borrowed window; short
 * areas keep the switches and the state. focused marks the selected line;
 * automatic and prereleases are the stored switches of the daily check and of
 * accepting pre-releases. */
void updates_draw(updates_view *view, WINDOW *window, int top, int left, int width, int rows,
                  bool focused, bool automatic, bool prereleases);
/* Moves between the lines of the last draw; the owner carries out the action. */
updates_action updates_key(updates_view *view, int type, uint32_t key);
/* Selects the line drawn at a window row; UPDATES_NOTHING when there is none. */
updates_action updates_click(updates_view *view, int row);
/* Request a copy of the newer release's page, or of how to install the saved
 * package; false when the clipboard refused it or there is nothing to copy. */
bool updates_copy_release(void);
bool updates_copy_command(void);
/* What the section's button does now: ask GitHub, download the package of the
 * newer version, stop that download, or copy how to install what was saved.
 * A copy that can replace itself downloads to install, and then offers to put
 * the prepared version in place and leave. */
typedef enum {
    UPDATES_BUTTON_CHECK,
    UPDATES_BUTTON_DOWNLOAD,
    UPDATES_BUTTON_CANCEL,
    UPDATES_BUTTON_COPY,
    UPDATES_BUTTON_INSTALL,
    UPDATES_BUTTON_APPLY
} updates_button;
updates_button updates_button_now(void);

#endif
