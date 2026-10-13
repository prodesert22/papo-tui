#ifndef PAPO_THEME_H
#define PAPO_THEME_H

#include <stdbool.h>

enum {
    NORMAL = 1, MUTED, ACCENT, INPUT, FOCUSED, ERROR, SUCCESS,
    PANEL, PANEL_MUTED, PANEL_ACCENT, PANEL_ACTIVE, BORDER,
    FOCUS_AREA, FOCUS_MUTED, FOCUS_BORDER,
    HISTORY_AREA, HISTORY_MUTED, HISTORY_ACCENT, HISTORY_ERROR, HISTORY_BORDER,
    PROFILE_TEXT, PROFILE_PANEL, PROFILE_SELECTED, PROFILE_FOCUS,
    PROFILE_BUTTON_SELECTED, PROFILE_BUTTON_FOCUS
};

#define THEME_COLOR_COUNT 13

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
