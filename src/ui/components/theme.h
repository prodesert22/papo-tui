#ifndef PAPO_THEME_H
#define PAPO_THEME_H

#include <stdbool.h>

enum {
    THEME_NORMAL = 1,
    THEME_MUTED,
    THEME_ACCENT,
    THEME_INPUT,
    THEME_FOCUSED,
    THEME_ERROR,
    THEME_SUCCESS,
    THEME_PANEL,
    THEME_PANEL_MUTED,
    THEME_PANEL_ACCENT,
    THEME_PANEL_BRIGHT,
    THEME_PANEL_ACTIVE,
    THEME_BORDER,
    THEME_FOCUS_AREA,
    THEME_FOCUS_MUTED,
    THEME_FOCUS_BORDER,
    THEME_HISTORY_AREA,
    THEME_HISTORY_MUTED,
    THEME_HISTORY_ACCENT,
    THEME_HISTORY_ERROR,
    THEME_HISTORY_BORDER,
    THEME_PROFILE_TEXT,
    THEME_PROFILE_PANEL,
    THEME_PROFILE_SELECTED,
    THEME_PROFILE_FOCUS,
    THEME_PROFILE_BUTTON_SELECTED,
    THEME_PROFILE_BUTTON_FOCUS,
    THEME_EMBED_AREA,
    THEME_EMBED_MUTED,
    THEME_EMBED_ACCENT,
    THEME_TEXT_SELECTION,
    THEME_NOTIFICATION,
    THEME_NOTIFICATION_FOCUS,
    THEME_NOTIFICATION_SELECTED,
    THEME_NOTIFICATION_BUTTON_FOCUS,
    THEME_DESTRUCTIVE,
    THEME_VOLUME_FIRST,
    THEME_VOLUME_LAST = THEME_VOLUME_FIRST + 20,
    THEME_PAIR_END
};

#define THEME_COLOR_COUNT 14
/* Reserved before the media/Chafa pair range; never recycle visible text pairs. */
#define THEME_RGB_PAIR_COUNT 128

typedef struct {
    struct {
        short red, green, blue;
        bool changed;
    } colors[THEME_COLOR_COUNT];
} ui_theme;

/* Called after initscr. Keeps Kitty/Konsole indexed palettes intact; on other
 * mutable palettes, restore saved ncurses values before endwin. */
void theme_init(ui_theme *theme);
void theme_restore(ui_theme *theme);
/* Tagged RGB foreground, preserving the existing background and palette. */
int theme_rgb_pair(unsigned rgb, int fallback);
int theme_member_pair(unsigned rgb, int background);
/* Fixed palette colors: green through 60%, yellow through 85%, then red. */
int theme_volume_pair(int percent);

#endif
