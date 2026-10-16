#ifndef PAPO_UNICODE_H
#define PAPO_UNICODE_H
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
#endif
