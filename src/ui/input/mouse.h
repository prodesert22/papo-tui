#ifndef PAPO_UI_MOUSE_H
#define PAPO_UI_MOUSE_H

#include <ncurses.h>
#include <stdio.h>

/* Initialize once before other mousemask calls; release after disabling it. */
void ui_mouse_initialize(mmask_t mask);
void ui_mouse_shutdown(void);

/* XM commonly requests only 1000 (press/release), even with ncurses' motion
 * mask. Request button motion explicitly, without idle pointer reports. */
static inline void ui_mouse_drag_tracking(bool enabled)
{
    fputs(enabled ? "\033[?1002h" : "\033[?1002l", stdout);
    fflush(stdout);
}

/* Preserve the chronological press -> motion -> release batch. Discarding a
 * release leaves the next click acting like an unfinished selection drag. */
static inline size_t ui_mouse_events(MEVENT *events, size_t capacity)
{
    MEVENT event;
    size_t count = 0;
    while (getmouse(&event) == OK)
        if (count < capacity)
            events[count++] = event;
    return count;
}

/* Drain the entire report run:
 * a release can follow an actionable press in the same KEY_MOUSE notification.
 * Callers act on an actionable press/click/wheel and ignore separately delivered
 * releases, which must still be requested by mousemask to avoid blocking. */
static inline int ui_mouse_get(MEVENT *event)
{
    MEVENT queued;
    int result = ERR;
    while (getmouse(&queued) == OK) {
        if (result == ERR && !(queued.bstate & REPORT_MOUSE_POSITION) &&
            (queued.bstate &
             (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED))) {
            *event = queued;
            result = OK;
        }
    }
    return result;
}

#endif
