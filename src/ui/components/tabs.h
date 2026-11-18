#ifndef PAPO_TABS_H
#define PAPO_TABS_H
#include <ncurses.h>
#include <stdbool.h>
#define TAB_STRIP_MAX 12
/* Server tabs and the Canais/DMs tabs of the conversation list: blocks of fill
 * with padding, no markers, where the terminal has 256 colors. Server names
 * show at most TAB_NAME_MAX cells; a longer name (or one squeezed by a narrow
 * strip) dissolves into the block over its last cells instead of ending hard. */
#define TAB_NAME_MAX 28
typedef enum { TAB_IDLE, TAB_SELECTED, TAB_ADD } tab_kind;
/* False on terminals with fewer colors, where the fill cannot tell tabs apart,
 * and with the screen reader mode, which reads text and not fills: the callers
 * then keep the [selected] and >cursor< markers instead. */
bool tab_blocks(void);
/* Colors of one of those tabs: the one being used, the add tab or any other,
 * with the keyboard cursor on it or not, in a strip that has the focus or not. */
int tab_pair(tab_kind kind, bool cursor, bool strip_focused);
/* Cells the name takes, up to TAB_NAME_MAX. */
int tab_name_width(const char *name);
/* Draws name in at most width cells with the window's attributes, fading its
 * last cells into the background of pair when it is cut. Returns the cells used. */
int tab_name_draw(WINDOW *window, int row, int x, int width, const char *name, int pair);
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
