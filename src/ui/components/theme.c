#include "ui/components/theme.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#define FIRST_CUSTOM_COLOR 240

enum {
    BACKGROUND,
    SURFACE,
    RAISED,
    TEXT,
    SECONDARY,
    ORANGE,
    RED,
    GREEN,
    RULE,
    WARM,
    HISTORY_BACKGROUND,
    PROFILE_WHITE,
    PROFILE_HIGHLIGHT,
    NOTIFICATION_RED
};

static short component(unsigned char value)
{
    /* ncurses accepts RGB components in the range 0..1000. */
    return (short)(((unsigned int)value * 1000 + 127) / 255);
}

void theme_init(ui_theme *theme)
{
    memset(theme, 0, sizeof(*theme));
    if (!has_colors() || start_color() == ERR)
        return;
    const char *term = getenv("TERM"), *kitty_window = getenv("KITTY_WINDOW_ID");
    bool kitty = (term && !strcmp(term, "xterm-kitty")) || (kitty_window && *kitty_window);
    static const unsigned char rgb[THEME_COLOR_COUNT][3] = {
        {0x00, 0x00, 0x00}, /* #000000 canvas */
        {0x00, 0x00, 0x00}, /* #000000 panels; keep focus colors distinct */
        {0x1d, 0x20, 0x26}, /* #1d2026 subtle panel focus */
        {0xe4, 0xe6, 0xeb}, /* #e4e6eb text */
        {0xa0, 0xa4, 0xaf}, /* #a0a4af secondary text */
        {0xff, 0xb3, 0x47}, /* #ffb347 accent requested by the user */
        {0xff, 0x7b, 0x72}, /* #ff7b72 errors */
        {0xa7, 0xc9, 0x8b}, /* #a7c98b success */
        {0x38, 0x3e, 0x49}, /* #383e49 separators */
        {0x30, 0x24, 0x19}, /* #302419 selected conversation */
        {0x15, 0x17, 0x1b}, /* #15171b subtle history focus */
        {0xff, 0xff, 0xff}, /* #ffffff profile labels and controls */
        {0x50, 0x38, 0x1f}, /* #50381f dark orange profile focus */
        {0xff, 0x00, 0x00}, /* #ff0000 unread notification dot */
    };
    /* Fixed xterm palette approximations. Never redefine the first 16 colors. */
    int colors[THEME_COLOR_COUNT] = {16,  16,  234, 254, 247, 215, 210,
                                     150, 238, 235, 233, 231, 94,  196};
    if (COLORS >= 0x1000000) {
        /* Direct-color terminfo: color numbers are packed RGB values. */
        for (int i = 0; i < THEME_COLOR_COUNT; ++i)
            colors[i] = (int)rgb[i][0] * 0x10000 + (int)rgb[i][1] * 0x100 + rgb[i][2];
    } else if (COLORS < 256) {
        const int basic[THEME_COLOR_COUNT] = {COLOR_BLACK,  COLOR_BLACK,  COLOR_BLACK, COLOR_WHITE,
                                              COLOR_WHITE,  COLOR_YELLOW, COLOR_RED,   COLOR_GREEN,
                                              COLOR_WHITE,  COLOR_BLACK,  COLOR_BLACK, COLOR_WHITE,
                                              COLOR_YELLOW, COLOR_RED};
        memcpy(colors, basic, sizeof(colors));
    } else if (can_change_color() && !kitty && !getenv("KONSOLE_VERSION")) {
        /* Konsole may advertise ccc while ignoring terminfo's rgb: syntax.
         * Kitty resets overridden indices on configuration reload, turning
         * our custom theme colors into gray. Keep both indexed palettes
         * intact; the direct-color branch above still provides exact RGB. */
        for (int i = 0; i < THEME_COLOR_COUNT; ++i) {
            short index = FIRST_CUSTOM_COLOR + i;
            if (color_content(index, &theme->colors[i].red, &theme->colors[i].green,
                              &theme->colors[i].blue) != OK)
                continue;
            if (init_color(index, component(rgb[i][0]), component(rgb[i][1]),
                           component(rgb[i][2])) != OK)
                continue;
            theme->colors[i].changed = true;
            colors[i] = index;
        }
    }
    init_extended_pair(THEME_NORMAL, colors[TEXT], colors[BACKGROUND]);
    init_extended_pair(THEME_MUTED, colors[SECONDARY], colors[BACKGROUND]);
    init_extended_pair(THEME_ACCENT, colors[ORANGE], colors[BACKGROUND]);
    init_extended_pair(THEME_INPUT, colors[TEXT], colors[SURFACE]);
    init_extended_pair(THEME_FOCUSED, colors[ORANGE], colors[RAISED]);
    init_extended_pair(THEME_ERROR, colors[RED], colors[BACKGROUND]);
    init_extended_pair(THEME_SUCCESS, colors[GREEN], colors[BACKGROUND]);
    init_extended_pair(THEME_PANEL, colors[TEXT], colors[SURFACE]);
    init_extended_pair(THEME_PANEL_MUTED, colors[SECONDARY], colors[SURFACE]);
    init_extended_pair(THEME_PANEL_ACCENT, colors[ORANGE], colors[SURFACE]);
    init_extended_pair(THEME_PANEL_ACTIVE, colors[ORANGE], colors[WARM]);
    init_extended_pair(THEME_BORDER, colors[RULE], colors[BACKGROUND]);
    init_extended_pair(THEME_FOCUS_AREA, colors[TEXT], colors[RAISED]);
    init_extended_pair(THEME_FOCUS_MUTED, colors[SECONDARY], colors[RAISED]);
    init_extended_pair(THEME_FOCUS_BORDER, colors[RULE], colors[RAISED]);
    init_extended_pair(THEME_HISTORY_AREA, colors[TEXT], colors[HISTORY_BACKGROUND]);
    init_extended_pair(THEME_HISTORY_MUTED, colors[SECONDARY], colors[HISTORY_BACKGROUND]);
    init_extended_pair(THEME_HISTORY_ACCENT, colors[ORANGE], colors[HISTORY_BACKGROUND]);
    init_extended_pair(THEME_HISTORY_ERROR, colors[RED], colors[HISTORY_BACKGROUND]);
    init_extended_pair(THEME_HISTORY_BORDER, colors[RULE], colors[HISTORY_BACKGROUND]);
    init_extended_pair(THEME_PROFILE_TEXT, colors[PROFILE_WHITE], colors[BACKGROUND]);
    init_extended_pair(THEME_PROFILE_PANEL, colors[PROFILE_WHITE], colors[SURFACE]);
    init_extended_pair(THEME_PROFILE_SELECTED, colors[PROFILE_WHITE], colors[WARM]);
    init_extended_pair(THEME_PROFILE_FOCUS, colors[PROFILE_WHITE], colors[PROFILE_HIGHLIGHT]);
    init_extended_pair(THEME_PROFILE_BUTTON_SELECTED, colors[ORANGE], colors[WARM]);
    init_extended_pair(THEME_PROFILE_BUTTON_FOCUS, colors[ORANGE], colors[PROFILE_HIGHLIGHT]);
    init_extended_pair(THEME_EMBED_AREA, colors[TEXT], colors[SURFACE]);
    init_extended_pair(THEME_EMBED_MUTED, colors[SECONDARY], colors[SURFACE]);
    init_extended_pair(THEME_EMBED_ACCENT, colors[RULE], colors[SURFACE]);
    init_extended_pair(THEME_TEXT_SELECTION, colors[BACKGROUND], colors[ORANGE]);
    init_extended_pair(THEME_NOTIFICATION, colors[NOTIFICATION_RED], colors[SURFACE]);
    init_extended_pair(THEME_NOTIFICATION_FOCUS, colors[NOTIFICATION_RED], colors[RAISED]);
    init_extended_pair(THEME_NOTIFICATION_SELECTED, colors[NOTIFICATION_RED], colors[WARM]);
    init_extended_pair(THEME_NOTIFICATION_BUTTON_FOCUS, colors[NOTIFICATION_RED],
                       colors[PROFILE_HIGHLIGHT]);
    /* Do not redefine palette entries: Kitty/Konsole can reload them at runtime. */
    static const int ramp[] = {22,  28,  34,  40,  46,  46,  82,  82,  118, 118, 154,
                               154, 154, 226, 226, 220, 220, 220, 203, 196, 196};
    static const int levels[] = {0, 95, 135, 175, 215, 255};
    for (int i = 0; i <= 20 && THEME_VOLUME_FIRST + i < COLOR_PAIRS; ++i) {
        int color = ramp[i];
        if (COLORS >= 0x1000000) {
            int cube = color - 16;
            color = levels[cube / 36] * 0x10000 + levels[cube / 6 % 6] * 0x100 + levels[cube % 6];
        } else if (COLORS < 256)
            color = i <= 12 ? COLOR_GREEN : i <= 17 ? COLOR_YELLOW : COLOR_RED;
        init_extended_pair(THEME_VOLUME_FIRST + i, color, colors[SURFACE]);
    }
    wbkgd(stdscr, COLOR_PAIR(THEME_NORMAL));
}

int theme_volume_pair(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    int step = percent / 5;
    if (percent > 60 && step == 12)
        step = 13;
    if (percent > 85 && step == 17)
        step = 18;
    int pair = THEME_VOLUME_FIRST + step;
    return has_colors() && pair < COLOR_PAIRS ? pair : THEME_PANEL;
}

void theme_restore(ui_theme *theme)
{
    for (int i = 0; i < THEME_COLOR_COUNT; ++i) {
        if (!theme->colors[i].changed)
            continue;
        init_color(FIRST_CUSTOM_COLOR + i, theme->colors[i].red, theme->colors[i].green,
                   theme->colors[i].blue);
        theme->colors[i].changed = false;
    }
}
