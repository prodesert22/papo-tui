#ifndef PAPO_TEST_LOCALE_H
#define PAPO_TEST_LOCALE_H

#include <locale.h>
#include <stdbool.h>
#include <stddef.h>

/* Selects a UTF-8 locale for a test. C.UTF-8 is not on every system: macOS offers en_US.UTF-8 and
 * a locale named UTF-8 instead. */
static inline bool test_utf8_locale(void)
{
    static const char *const names[] = {"C.UTF-8", "en_US.UTF-8", "UTF-8"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (setlocale(LC_ALL, names[i]))
            return true;
    return false;
}

#endif
