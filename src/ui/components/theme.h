#ifndef PAPO_THEME_H
#define PAPO_THEME_H

#include <stdbool.h>
#include <stdint.h>

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
    /* The "novo" tag of conversations with unread messages, on the list's backgrounds. */
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
typedef enum { THEME_PALETTE_DARK, THEME_PALETTE_LIGHT } theme_palette;
/* Redefines every pair for palette, keeping the pair numbers; cached
 * renderings that blended the old background must be redrawn. True when the
 * palette changed. Before theme_init it only selects the palette. */
bool theme_set_palette(theme_palette palette);
theme_palette theme_get_palette(void);
/* Asks the terminal for its background color (OSC 11), once. */
void theme_probe_background(void);
/* Consumes an "ESC ] 11 ; rgb:…" reply; false when reply is not one. */
bool theme_background_reply(const char *reply);
/* 1 when the terminal background is light, 0 when dark, -1 when unknown:
 * from the OSC 11 reply, else from COLORFGBG. */
int theme_terminal_light(void);
/* Components 0..255: whether that color reads as a light background. */
bool theme_luminance_light(unsigned red, unsigned green, unsigned blue);
/* Tagged RGB foreground, preserving the existing background and palette. */
int theme_rgb_pair(unsigned rgb, int fallback);
int theme_member_pair(unsigned rgb, int background);
/* RGB the terminal shows behind pair (0xRRGGBB): exact with direct color or a
 * redefined palette, else its 256-color or basic approximation. Media blends
 * transparent pixels into it, so they match the cells around them. */
uint32_t theme_background_rgb(int pair);
/* Fixed palette colors: green through 60%, yellow through 85%, then red. */
int theme_volume_pair(int percent);

#endif
