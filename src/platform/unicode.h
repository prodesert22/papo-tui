#ifndef PAPO_UNICODE_H
#define PAPO_UNICODE_H
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

/* Unicode scalars, independent of locale and the width of wchar_t. */
static inline size_t papo_utf8_next(const char *input, uint32_t *value)
{
    if (!input || !*input || !value)
        return 0;
    gunichar scalar = g_utf8_get_char_validated(input, -1);
    if (scalar == (gunichar)-1 || scalar == (gunichar)-2 || !g_unichar_validate(scalar))
        return 0;
    *value = scalar;
    return (size_t)(g_utf8_next_char(input) - input);
}
static inline int papo_unicode_width(uint32_t value)
{
    if (!g_unichar_validate(value))
        return -1;
    if (g_unichar_iszerowidth(value))
        return 0;
    return g_unichar_isprint(value) ? (g_unichar_iswide(value) ? 2 : 1) : -1;
}
/* Writes value as wchar_t units for curses and returns how many were written,
 * or 0 when this platform's curses cannot show it. With a 16-bit wchar_t, only
 * double-width characters outside the BMP become a UTF-16 surrogate pair: the
 * Windows ncurses build counts the pair as one two-column cell. */
static inline size_t papo_wchar_encode(uint32_t value, wchar_t output[2])
{
    if (value <= WCHAR_MAX && (value < 0xd800 || value > 0xdfff)) {
        output[0] = (wchar_t)value;
        return 1;
    }
#if WCHAR_MAX == 0xffff
    /* Single-width symbols outside the BMP cannot be told apart from emoji by
     * curses there. Show the interface's ones as BMP symbols of equal width. */
    static const struct {
        uint32_t from;
        wchar_t to;
    } narrow[] = {
        {0x1f870, 0x2190}, {0x1f871, 0x2191}, {0x1f872, 0x2192}, /* Heavy arrows: ← ↑ → */
        {0x1f873, 0x2193}, {0x1f5d9, 0x2715}, {0x1f588, 0x2691}, /* ↓, ✕ for 🗙, ⚑ for 🖈 */
    };
    for (size_t i = 0; i < sizeof(narrow) / sizeof(narrow[0]); ++i)
        if (narrow[i].from == value) {
            output[0] = narrow[i].to;
            return 1;
        }
    if (value > 0xffff && value <= 0x10ffff && papo_unicode_width(value) == 2) {
        value -= 0x10000;
        output[0] = (wchar_t)(0xd800 + (value >> 10));
        output[1] = (wchar_t)(0xdc00 + (value & 0x3ff));
        return 2;
    }
#endif
    return 0;
}
#endif
