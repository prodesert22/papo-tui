#include "ui/components/theme.h"
#include "platform/terminal.h"

#include <glib.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#else
#include <unistd.h>
#endif

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

static const unsigned char dark_rgb[THEME_COLOR_COUNT][3] = {
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
/* Same roles on a soft gray canvas rather than white, which glares in a
 * terminal: the accent and status colors are darker so text keeps its
 * contrast, and "white" labels become near-black. */
static const unsigned char light_rgb[THEME_COLOR_COUNT][3] = {
    {0xe0, 0xe3, 0xe7}, /* #e0e3e7 canvas */
    {0xe0, 0xe3, 0xe7}, /* #e0e3e7 panels */
    {0xcd, 0xd1, 0xd8}, /* #cdd1d8 subtle panel focus */
    {0x16, 0x19, 0x1d}, /* #16191d text */
    {0x48, 0x50, 0x5a}, /* #48505a secondary text */
    {0x93, 0x4a, 0x00}, /* #934a00 accent */
    {0xad, 0x16, 0x1f}, /* #ad161f errors */
    {0x13, 0x69, 0x2c}, /* #13692c success */
    {0xb2, 0xb8, 0xc1}, /* #b2b8c1 separators */
    {0xe8, 0xd3, 0xb5}, /* #e8d3b5 selected conversation */
    {0xd6, 0xd9, 0xdf}, /* #d6d9df subtle history focus */
    {0x00, 0x00, 0x00}, /* #000000 profile labels and controls */
    {0xeb, 0xc2, 0x8a}, /* #ebc28a orange profile focus */
    {0xc0, 0x1c, 0x1c}, /* #c01c1c unread notification dot */
};
/* xterm-256 approximations, used when the palette cannot be redefined. */
static const int dark_indexed[THEME_COLOR_COUNT] = {16,  16,  234, 254, 247, 215, 210,
                                                    150, 238, 235, 233, 231, 94,  196};
static const int light_indexed[THEME_COLOR_COUNT] = {254, 254, 252, 234, 239, 94,  124,
                                                     22,  249, 223, 253, 16,  179, 160};
static const int dark_basic[THEME_COLOR_COUNT] = {
    COLOR_BLACK, COLOR_BLACK, COLOR_BLACK, COLOR_WHITE, COLOR_WHITE, COLOR_YELLOW, COLOR_RED,
    COLOR_GREEN, COLOR_WHITE, COLOR_BLACK, COLOR_BLACK, COLOR_WHITE, COLOR_YELLOW, COLOR_RED};
/* Yellow is unreadable on white: the accent is blue with 8 or 16 colors. */
static const int light_basic[THEME_COLOR_COUNT] = {
    COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_BLACK, COLOR_BLACK, COLOR_BLUE, COLOR_RED,
    COLOR_GREEN, COLOR_BLACK, COLOR_WHITE, COLOR_WHITE, COLOR_BLACK, COLOR_CYAN, COLOR_RED};
static theme_palette palette = THEME_PALETTE_DARK;
static const unsigned char (*rgb)[3] = dark_rgb;
/* The theme given to theme_init, so a palette change can redefine colors and
 * still restore the terminal's own values at exit. */
static ui_theme *active;

/* Pseudo color: the separator rule, or the accent where focus backgrounds
 * cannot be shown (8 or 16 colors). */
#define FOCUS_RULE THEME_COLOR_COUNT
/* Pseudo color: white text on the focused item, or the accent where focus
 * backgrounds cannot be shown (8 or 16 colors). Selected items without focus
 * keep the accent. */
#define SELECTED_TEXT (THEME_COLOR_COUNT + 1)
static const struct {
    int pair, foreground, background;
} pair_colors[] = {
    {THEME_NORMAL, TEXT, BACKGROUND},
    {THEME_MUTED, SECONDARY, BACKGROUND},
    {THEME_ACCENT, ORANGE, BACKGROUND},
    {THEME_INPUT, TEXT, SURFACE},
    {THEME_FOCUSED, SELECTED_TEXT, RAISED},
    {THEME_ERROR, RED, BACKGROUND},
    {THEME_SUCCESS, GREEN, BACKGROUND},
    {THEME_PANEL, TEXT, SURFACE},
    {THEME_PANEL_MUTED, SECONDARY, SURFACE},
    {THEME_PANEL_ACCENT, ORANGE, SURFACE},
    {THEME_PANEL_BRIGHT, PROFILE_WHITE, SURFACE},
    {THEME_PANEL_ACTIVE, ORANGE, WARM},
    {THEME_BORDER, RULE, BACKGROUND},
    {THEME_FOCUS_AREA, TEXT, RAISED},
    {THEME_FOCUS_MUTED, SECONDARY, RAISED},
    {THEME_FOCUS_BORDER, FOCUS_RULE, RAISED},
    {THEME_HISTORY_AREA, TEXT, HISTORY_BACKGROUND},
    {THEME_HISTORY_MUTED, SECONDARY, HISTORY_BACKGROUND},
    {THEME_HISTORY_ACCENT, ORANGE, HISTORY_BACKGROUND},
    {THEME_HISTORY_ERROR, RED, HISTORY_BACKGROUND},
    {THEME_HISTORY_BORDER, FOCUS_RULE, HISTORY_BACKGROUND},
    {THEME_PROFILE_TEXT, PROFILE_WHITE, BACKGROUND},
    {THEME_PROFILE_PANEL, PROFILE_WHITE, SURFACE},
    {THEME_PROFILE_SELECTED, PROFILE_WHITE, WARM},
    {THEME_PROFILE_FOCUS, PROFILE_WHITE, PROFILE_HIGHLIGHT},
    {THEME_PROFILE_BUTTON_SELECTED, ORANGE, WARM},
    {THEME_PROFILE_BUTTON_FOCUS, SELECTED_TEXT, PROFILE_HIGHLIGHT},
    {THEME_EMBED_AREA, TEXT, SURFACE},
    {THEME_EMBED_MUTED, SECONDARY, SURFACE},
    {THEME_EMBED_ACCENT, RULE, SURFACE},
    {THEME_TEXT_SELECTION, BACKGROUND, ORANGE},
    {THEME_NOTIFICATION, NOTIFICATION_RED, SURFACE},
    {THEME_NOTIFICATION_FOCUS, NOTIFICATION_RED, RAISED},
    {THEME_NOTIFICATION_SELECTED, NOTIFICATION_RED, WARM},
    {THEME_NOTIFICATION_BUTTON_FOCUS, NOTIFICATION_RED, PROFILE_HIGHLIGHT},
    {THEME_DESTRUCTIVE, PROFILE_WHITE, NOTIFICATION_RED},
};
/* Color numbers given to ncurses and the RGB the terminal shows for them. */
static int color_numbers[THEME_COLOR_COUNT];
static uint32_t shown_rgb[THEME_COLOR_COUNT];
static bool shown_ready;

static uint32_t packed(int index)
{
    return (uint32_t)rgb[index][0] << 16 | (uint32_t)rgb[index][1] << 8 | rgb[index][2];
}
/* The xterm 256-color palette, which terminals keep for indices 16-255. */
static uint32_t xterm_rgb(int index)
{
    if (index >= 232) {
        uint32_t gray = 8 + 10 * (uint32_t)(index - 232);
        return gray << 16 | gray << 8 | gray;
    }
    static const uint32_t levels[] = {0, 95, 135, 175, 215, 255};
    int cube = index - 16;
    return levels[cube / 36] << 16 | levels[cube / 6 % 6] << 8 | levels[cube % 6];
}
/* Approximate VGA values of the eight basic colors, as on the Linux console. */
static uint32_t basic_rgb(int color)
{
    static const uint32_t vga[] = {0x000000, 0xaa0000, 0x00aa00, 0xaa5500,
                                   0x0000aa, 0xaa00aa, 0x00aaaa, 0xaaaaaa};
    return color >= 0 && color < 8 ? vga[color] : 0;
}

static short component(unsigned char value)
{
    /* ncurses accepts RGB components in the range 0..1000. */
    return (short)(((unsigned int)value * 1000 + 127) / 255);
}

static struct {
    int fg, bg;
} rgb_pairs[THEME_RGB_PAIR_COUNT];
static int rgb_pair_count;
int theme_rgb_pair(unsigned rgb, int fallback)
{
    if (!(rgb & 0x1000000) || !has_colors() || COLORS < 8 || COLOR_PAIRS < 64)
        return fallback;
    /* Plain white is the default name color: the palette's bright label. */
    if (rgb == 0x1ffffff)
        rgb = 0x1000000 | packed(PROFILE_WHITE);
    int r = (int)(rgb >> 16) & 255, g = (int)(rgb >> 8) & 255, b = (int)rgb & 255;
    int fg;
    if (COLORS >= 0x1000000)
        fg = (int)(rgb & 0xffffff);
    else if (COLORS >= 256) {
        const int level[] = {0, 95, 135, 175, 215, 255};
        int best = 0x7fffffff;
        fg = 16;
        for (int red = 0; red < 6; ++red)
            for (int green = 0; green < 6; ++green)
                for (int blue = 0; blue < 6; ++blue) {
                    int dr = r - level[red], dg = g - level[green], db = b - level[blue];
                    int distance = dr * dr + dg * dg + db * db;
                    if (distance < best) {
                        best = distance;
                        fg = 16 + 36 * red + 6 * green + blue;
                    }
                }
    } else
        fg = (r > 127 ? COLOR_RED : 0) | (g > 127 ? COLOR_GREEN : 0) | (b > 127 ? COLOR_BLUE : 0);
    int oldfg, background;
    if (extended_pair_content(fallback, &oldfg, &background) == ERR)
        return fallback;
    /* Names on the focused item's fill stay black, like the rest of its text. */
    if (COLORS < 256 && shown_ready && background == color_numbers[PROFILE_HIGHLIGHT])
        fg = COLOR_BLACK;
    for (int i = 0; i < rgb_pair_count; ++i)
        if (rgb_pairs[i].fg == fg && rgb_pairs[i].bg == background)
            return THEME_PAIR_END + i;
    if (rgb_pair_count == THEME_RGB_PAIR_COUNT ||
        (THEME_PAIR_END + rgb_pair_count >= COLOR_PAIRS || THEME_PAIR_END + rgb_pair_count >= 256))
        return fallback;
    int pair = THEME_PAIR_END + rgb_pair_count;
    if (init_extended_pair(pair, fg, background) == ERR)
        return fallback;
    rgb_pairs[rgb_pair_count].fg = fg;
    rgb_pairs[rgb_pair_count++].bg = background;
    return pair;
}
attr_t theme_text_attributes(attr_t attributes, int pair)
{
    if (!(attributes & A_BOLD) || !has_colors() || COLORS >= 256)
        return attributes;
    int foreground, background;
    if (extended_pair_content(pair, &foreground, &background) == ERR || foreground != COLOR_BLACK)
        return attributes;
    return attributes & ~A_BOLD;
}
int theme_member_pair(unsigned rgb, int background)
{
    int white = theme_rgb_pair(0x1ffffff, background);
    return theme_rgb_pair(rgb, white);
}

static void apply(ui_theme *theme)
{
    rgb_pair_count = 0;
    shown_ready = false;
    if (!has_colors())
        return;
    const char *term = getenv("TERM"), *kitty_window = getenv("KITTY_WINDOW_ID");
    bool kitty = (term && !strcmp(term, "xterm-kitty")) || (kitty_window && *kitty_window);
    bool light = palette == THEME_PALETTE_LIGHT;
    /* Fixed xterm palette approximations. Never redefine the first 16 colors. */
    int colors[THEME_COLOR_COUNT];
    memcpy(colors, light ? light_indexed : dark_indexed, sizeof(colors));
    if (COLORS >= 0x1000000) {
        /* Direct-color terminfo: color numbers are packed RGB values. */
        for (int i = 0; i < THEME_COLOR_COUNT; ++i)
            colors[i] = (int)rgb[i][0] * 0x10000 + (int)rgb[i][1] * 0x100 + rgb[i][2];
    } else if (COLORS < 256) {
        memcpy(colors, light ? light_basic : dark_basic, sizeof(colors));
    } else if (can_change_color() && !kitty && !getenv("KONSOLE_VERSION")) {
        /* Konsole may advertise ccc while ignoring terminfo's rgb: syntax.
         * Kitty resets overridden indices on configuration reload, turning
         * our custom theme colors into gray. Keep both indexed palettes
         * intact; the direct-color branch above still provides exact RGB. */
        for (int i = 0; i < THEME_COLOR_COUNT; ++i) {
            short index = FIRST_CUSTOM_COLOR + i;
            /* The terminal's own value is saved once, before the first change. */
            if (!theme->colors[i].changed &&
                color_content(index, &theme->colors[i].red, &theme->colors[i].green,
                              &theme->colors[i].blue) != OK)
                continue;
            if (init_color(index, component(rgb[i][0]), component(rgb[i][1]),
                           component(rgb[i][2])) != OK)
                continue;
            theme->colors[i].changed = true;
            colors[i] = index;
        }
    }
    /* With 8 or 16 colors (such as the Linux console) the focus backgrounds
     * are black like the rest, so focused borders take the accent instead. */
    int focus_rule = COLORS < 256 ? colors[ORANGE] : colors[RULE];
    int selected_text = COLORS < 256 ? colors[ORANGE] : colors[PROFILE_WHITE];
    for (size_t i = 0; i < sizeof(pair_colors) / sizeof(pair_colors[0]); ++i) {
        int foreground = pair_colors[i].foreground == FOCUS_RULE ? focus_rule
                         : pair_colors[i].foreground == SELECTED_TEXT
                             ? selected_text
                             : colors[pair_colors[i].foreground];
        /* With 8 or 16 colors the highlight fill is the accent's own hue:
         * accent, white or red text fades on it, so focused items take black. */
        if (COLORS < 256 && pair_colors[i].background == PROFILE_HIGHLIGHT)
            foreground = COLOR_BLACK;
        init_extended_pair(pair_colors[i].pair, foreground, colors[pair_colors[i].background]);
    }
    for (int i = 0; i < THEME_COLOR_COUNT; ++i) {
        color_numbers[i] = colors[i];
        shown_rgb[i] = COLORS >= 0x1000000 || theme->colors[i].changed ? packed(i)
                       : COLORS >= 256                                 ? xterm_rgb(colors[i])
                                                                       : basic_rgb(colors[i]);
    }
    shown_ready = true;
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
    const int member_backgrounds[] = {THEME_PANEL, THEME_FOCUSED, THEME_PANEL_ACTIVE,
                                      THEME_HISTORY_AREA, THEME_PROFILE_FOCUS};
    for (size_t i = 0; i < sizeof(member_backgrounds) / sizeof(member_backgrounds[0]); ++i)
        theme_member_pair(0x1ffffff, member_backgrounds[i]);
    wbkgd(stdscr, COLOR_PAIR(THEME_NORMAL));
}

void theme_init(ui_theme *theme)
{
    memset(theme, 0, sizeof(*theme));
    active = theme;
    rgb_pair_count = 0;
    shown_ready = false;
    if (!has_colors() || start_color() == ERR)
        return;
    apply(theme);
}

bool theme_set_palette(theme_palette next)
{
    if (next != THEME_PALETTE_DARK && next != THEME_PALETTE_LIGHT)
        return false;
    if (next == palette)
        return false;
    palette = next;
    rgb = palette == THEME_PALETTE_LIGHT ? light_rgb : dark_rgb;
    if (active)
        apply(active);
    return true;
}

theme_palette theme_get_palette(void)
{
    return palette;
}

/* -1 until known; then 1 for a light terminal background, 0 for a dark one. */
static int reported = -1;
static bool queried;

bool theme_linux_console(void)
{
    return terminal_linux_console();
}
const char *theme_unread_dot(void)
{
    return theme_linux_console() ? "•" : "⦁";
}
const char *theme_spinner_frame(uint64_t step)
{
    static const char *braille[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    static const char *ascii[] = {"|", "/", "-", "\\"};
    return theme_linux_console() ? ascii[step % G_N_ELEMENTS(ascii)]
                                 : braille[step % G_N_ELEMENTS(braille)];
}
void theme_probe_background(void)
{
    if (queried)
        return;
    queried = true;
    const char *term = getenv("TERM");
    /* The Linux console prints unknown OSC strings instead of ignoring them. */
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || !term || !*term ||
        !strcmp(term, "dumb") || !strcmp(term, "linux"))
        return;
    fputs("\033]11;?\033\\", stdout);
    fflush(stdout);
}

/* One rgb: component of 1 to 4 hex digits, scaled to 0..255. */
static bool component_value(const char **at, unsigned *value)
{
    unsigned digits = 0, number = 0;
    while (g_ascii_isxdigit(**at) && digits < 4) {
        number = number * 16 + (unsigned)g_ascii_xdigit_value(**at);
        ++*at;
        ++digits;
    }
    if (!digits || g_ascii_isxdigit(**at))
        return false;
    unsigned max = (1u << (4 * digits)) - 1;
    *value = number * 255 / max;
    return true;
}

bool theme_background_reply(const char *reply)
{
    if (!reply || strncmp(reply, "\033]11;", 5))
        return false;
    const char *at = reply + 5;
    unsigned red, green, blue;
    if (!strncmp(at, "rgba:", 5))
        at += 5;
    else if (!strncmp(at, "rgb:", 4))
        at += 4;
    else
        return true;
    if (!component_value(&at, &red) || *at++ != '/' || !component_value(&at, &green) ||
        *at++ != '/' || !component_value(&at, &blue))
        return true;
    reported = theme_luminance_light(red, green, blue);
    return true;
}

bool theme_luminance_light(unsigned red, unsigned green, unsigned blue)
{
    /* Rec. 709 luma in thousandths; above the middle reads as light. */
    return 2126u * red + 7152u * green + 722u * blue > 10000u * 128u;
}

int theme_terminal_light(void)
{
    if (reported >= 0)
        return reported;
    /* rxvt-style "foreground;background" (or "fg;other;bg") ANSI indices. */
    const char *value = getenv("COLORFGBG");
    const char *last = value ? strrchr(value, ';') : NULL;
    if (!last || !last[1])
        return -1;
    char *end;
    long background = strtol(last + 1, &end, 10);
    if (*end || background < 0 || background > 15)
        return -1;
    return background == 7 || background == 15;
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

uint32_t theme_background_rgb(int pair)
{
    int index = -1;
    for (size_t i = 0; i < sizeof(pair_colors) / sizeof(pair_colors[0]) && index < 0; ++i)
        if (pair_colors[i].pair == pair)
            index = pair_colors[i].background;
    if (index < 0 && pair >= THEME_VOLUME_FIRST && pair <= THEME_VOLUME_LAST)
        index = SURFACE;
    if (index < 0 && shown_ready && pair >= THEME_PAIR_END &&
        pair < THEME_PAIR_END + rgb_pair_count)
        for (int i = 0; i < THEME_COLOR_COUNT && index < 0; ++i)
            if (color_numbers[i] == rgb_pairs[pair - THEME_PAIR_END].bg)
                index = i;
    if (index < 0)
        index = BACKGROUND;
    return shown_ready ? shown_rgb[index] : packed(index);
}
