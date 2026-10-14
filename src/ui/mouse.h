#ifndef PAPO_UI_MOUSE_H
#define PAPO_UI_MOUSE_H

#include <ncurses.h>

/* ncurses groups reports and exposes the newest first. Drain the entire run:
 * a release can follow an actionable press in the same KEY_MOUSE notification.
 * Callers act on the newest press/click/wheel and ignore separately delivered
 * releases, which must still be requested by mousemask to avoid blocking. */
static inline int ui_mouse_get(MEVENT *event)
{
    MEVENT queued;
    int result = ERR;
    while (getmouse(&queued) == OK) {
        if (result == ERR && (queued.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED |
                                             BUTTON4_PRESSED | BUTTON5_PRESSED))) {
            *event = queued;
            result = OK;
        }
    }
    return result;
}

#endif
