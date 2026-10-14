#include "ui/theme.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#define FIRST_CUSTOM_COLOR 240

enum { BACKGROUND, SURFACE, RAISED, TEXT, SECONDARY, ORANGE, RED, GREEN, RULE, WARM, HISTORY_BACKGROUND,
       PROFILE_WHITE, PROFILE_HIGHLIGHT };

static short component(unsigned char value)
{
    /* ncurses accepts RGB components in the range 0..1000. */
    return (short)(((unsigned int)value * 1000 + 127) / 255);
}

void theme_init(ui_theme *theme)
{
    memset(theme, 0, sizeof(*theme));
    if (!has_colors() || start_color() == ERR) return;
    static const unsigned char rgb[THEME_COLOR_COUNT][3] = {
        {0x0d, 0x0f, 0x12}, /* #0d0f12 canvas */
        {0x15, 0x18, 0x1e}, /* #15181e panels */
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
    };
    /* Fixed xterm palette approximations. Never redefine the first 16 colors. */
    int colors[THEME_COLOR_COUNT] = {232, 233, 234, 254, 247, 215, 210, 150, 238, 235, 233, 231, 94};
    if (COLORS >= 0x1000000) {
        /* Direct-color terminfo: color numbers are packed RGB values. */
        for (int i = 0; i < THEME_COLOR_COUNT; ++i)
            colors[i] = (int)rgb[i][0] * 0x10000 + (int)rgb[i][1] * 0x100 + rgb[i][2];
    } else if (COLORS < 256) {
        const int basic[THEME_COLOR_COUNT] = {
            COLOR_BLACK, COLOR_BLACK, COLOR_BLACK, COLOR_WHITE, COLOR_WHITE,
            COLOR_YELLOW, COLOR_RED, COLOR_GREEN, COLOR_WHITE, COLOR_BLACK, COLOR_BLACK,
            COLOR_WHITE, COLOR_YELLOW
        };
        memcpy(colors, basic, sizeof(colors));
    } else if (can_change_color() && !getenv("KONSOLE_VERSION")) {
        /* Konsole may advertise ccc while ignoring terminfo's rgb: syntax.
         * Keep its indexed palette intact; direct-color entries above still
         * provide exact RGB when explicitly advertised by terminfo. */
        for (int i = 0; i < THEME_COLOR_COUNT; ++i) {
            short index = FIRST_CUSTOM_COLOR + i;
            if (color_content(index, &theme->colors[i].red, &theme->colors[i].green,
                              &theme->colors[i].blue) != OK) continue;
            if (init_color(index, component(rgb[i][0]), component(rgb[i][1]),
                           component(rgb[i][2])) != OK) continue;
            theme->colors[i].changed = true;
            colors[i] = index;
        }
    }
    init_extended_pair(NORMAL, colors[TEXT], colors[BACKGROUND]);
    init_extended_pair(MUTED, colors[SECONDARY], colors[BACKGROUND]);
    init_extended_pair(ACCENT, colors[ORANGE], colors[BACKGROUND]);
    init_extended_pair(INPUT, colors[TEXT], colors[SURFACE]);
    init_extended_pair(FOCUSED, colors[ORANGE], colors[RAISED]);
    init_extended_pair(ERROR, colors[RED], colors[BACKGROUND]);
    init_extended_pair(SUCCESS, colors[GREEN], colors[BACKGROUND]);
    init_extended_pair(PANEL, colors[TEXT], colors[SURFACE]);
    init_extended_pair(PANEL_MUTED, colors[SECONDARY], colors[SURFACE]);
    init_extended_pair(PANEL_ACCENT, colors[ORANGE], colors[SURFACE]);
    init_extended_pair(PANEL_ACTIVE, colors[ORANGE], colors[WARM]);
    init_extended_pair(BORDER, colors[RULE], colors[BACKGROUND]);
    init_extended_pair(FOCUS_AREA, colors[TEXT], colors[RAISED]);
    init_extended_pair(FOCUS_MUTED, colors[SECONDARY], colors[RAISED]);
    init_extended_pair(FOCUS_BORDER, colors[RULE], colors[RAISED]);
    init_extended_pair(HISTORY_AREA, colors[TEXT], colors[HISTORY_BACKGROUND]);
    init_extended_pair(HISTORY_MUTED, colors[SECONDARY], colors[HISTORY_BACKGROUND]);
    init_extended_pair(HISTORY_ACCENT, colors[ORANGE], colors[HISTORY_BACKGROUND]);
    init_extended_pair(HISTORY_ERROR, colors[RED], colors[HISTORY_BACKGROUND]);
    init_extended_pair(HISTORY_BORDER, colors[RULE], colors[HISTORY_BACKGROUND]);
    init_extended_pair(PROFILE_TEXT, colors[PROFILE_WHITE], colors[BACKGROUND]);
    init_extended_pair(PROFILE_PANEL, colors[PROFILE_WHITE], colors[SURFACE]);
    init_extended_pair(PROFILE_SELECTED, colors[PROFILE_WHITE], colors[WARM]);
    init_extended_pair(PROFILE_FOCUS, colors[PROFILE_WHITE], colors[PROFILE_HIGHLIGHT]);
    init_extended_pair(PROFILE_BUTTON_SELECTED, colors[ORANGE], colors[WARM]);
    init_extended_pair(PROFILE_BUTTON_FOCUS, colors[ORANGE], colors[PROFILE_HIGHLIGHT]);
    init_extended_pair(EMBED_AREA, colors[TEXT], colors[RAISED]);
    init_extended_pair(EMBED_MUTED, colors[SECONDARY], colors[RAISED]);
    init_extended_pair(EMBED_ACCENT, colors[ORANGE], colors[RAISED]);
    wbkgd(stdscr, COLOR_PAIR(NORMAL));
}

void theme_restore(ui_theme *theme)
{
    for (int i = 0; i < THEME_COLOR_COUNT; ++i) {
        if (!theme->colors[i].changed) continue;
        init_color(FIRST_CUSTOM_COLOR + i, theme->colors[i].red,
                   theme->colors[i].green, theme->colors[i].blue);
        theme->colors[i].changed = false;
    }
}
