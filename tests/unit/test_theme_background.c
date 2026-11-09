/* The "system" theme reads the terminal background: COLORFGBG until the
 * terminal answers OSC 11, then that answer. */
#include "ui/components/theme.h"
#undef NDEBUG
#include <assert.h>
#include <glib.h>

int main(void)
{
    assert(theme_luminance_light(255, 255, 255) && !theme_luminance_light(0, 0, 0));
    assert(!theme_luminance_light(0x28, 0x2c, 0x34) && theme_luminance_light(0xfd, 0xf6, 0xe3));
    g_unsetenv("COLORFGBG");
    assert(theme_terminal_light() == -1);
    g_setenv("COLORFGBG", "0;15", TRUE);
    assert(theme_terminal_light() == 1);
    g_setenv("COLORFGBG", "15;default;0", TRUE);
    assert(theme_terminal_light() == 0);
    g_setenv("COLORFGBG", "7;x", TRUE);
    assert(theme_terminal_light() == -1);
    g_setenv("COLORFGBG", "0;", TRUE);
    assert(theme_terminal_light() == -1);

    /* Other OSC replies are left to their own readers. */
    assert(!theme_background_reply("\033]99;i=x;\033\\"));
    assert(!theme_background_reply(NULL));
    /* Malformed answers are consumed without deciding anything. */
    assert(theme_background_reply("\033]11;rgb:zz/00/00\033\\"));
    assert(theme_background_reply("\033]11;rgb:ffff/ffff\a"));
    assert(theme_background_reply("\033]11;#ffffff\a"));
    assert(theme_background_reply("\033]11;rgb:fffff/0/0\a"));
    assert(theme_terminal_light() == -1);
    /* 4-, 2- and 1-digit components; BEL or ST terminated; rgba too. */
    assert(theme_background_reply("\033]11;rgb:ffff/ffff/ffff\033\\"));
    assert(theme_terminal_light() == 1);
    assert(theme_background_reply("\033]11;rgb:1e/1e/2e\a"));
    assert(theme_terminal_light() == 0);
    assert(theme_background_reply("\033]11;rgba:f/f/e/f\a"));
    assert(theme_terminal_light() == 1);
    /* The terminal's answer outranks COLORFGBG. */
    g_setenv("COLORFGBG", "15;0", TRUE);
    assert(theme_terminal_light() == 1);
    /* Selecting a palette before curses starts changes nothing on screen. */
    assert(theme_get_palette() == THEME_PALETTE_DARK);
    assert(theme_set_palette(THEME_PALETTE_LIGHT) && !theme_set_palette(THEME_PALETTE_LIGHT));
    assert(theme_get_palette() == THEME_PALETTE_LIGHT);
    assert(theme_set_palette(THEME_PALETTE_DARK));
    return 0;
}
