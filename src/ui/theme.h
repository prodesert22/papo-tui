#ifndef PAPO_THEME_H
#define PAPO_THEME_H

#include <stdbool.h>

enum {
    NORMAL = 1, MUTED, ACCENT, INPUT, FOCUSED, ERROR, SUCCESS,
    PANEL, PANEL_MUTED, PANEL_ACCENT, PANEL_ACTIVE, BORDER,
    FOCUS_AREA, FOCUS_MUTED, FOCUS_BORDER,
    HISTORY_AREA, HISTORY_MUTED, HISTORY_ACCENT, HISTORY_ERROR, HISTORY_BORDER
};

#define THEME_COLOR_COUNT 11

typedef struct {
    struct {
        short red, green, blue;
        bool changed;
    } colors[THEME_COLOR_COUNT];
} ui_theme;

/* Called after initscr. Owns temporary palette changes; restore the values
 * saved from ncurses before endwin. */
void theme_init(ui_theme *theme);
void theme_restore(ui_theme *theme);

#endif
