#ifndef PAPO_TABS_H
#define PAPO_TABS_H
#include <ncurses.h>
#include <stdbool.h>
#define TAB_STRIP_MAX 8
/* One row of tab labels inside a panel. When they do not fit, a window of
 * tabs around the active one is shown between ‹ and › markers. The strip
 * keeps the hit boxes of its last draw; labels are borrowed for the call. */
typedef struct {
    int x[TAB_STRIP_MAX], w[TAB_STRIP_MAX], prev, next;
} tab_strip;
void tab_strip_reset(tab_strip *strip);
void tab_strip_draw(tab_strip *strip, WINDOW *window, int row, int width, const char *const *labels,
                    int count, int active, bool focused);
/* The tab chosen by a click at column x of the tab row: the tab under it, the
 * neighbour behind a marker, or the neighbour on that half of the panel. */
int tab_strip_hit(const tab_strip *strip, int count, int active, int x, int width);
#endif
