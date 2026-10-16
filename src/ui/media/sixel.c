#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/media/sixel.h"
#include "platform/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
bool sixel_terminal_reply(sixel_terminal *t, const char *reply)
{
    if (!t || !reply || strlen(reply) > 96)
        return false;
    if (!strncmp(reply, "\033[?", 3)) {
        const char *p = reply + 3;
        bool sixel = false;
        int values = 0;
        while (*p >= '0' && *p <= '9' && values < 24) {
            char *end;
            unsigned long n = strtoul(p, &end, 10);
            if (n > 65535)
                return false;
            /* The first number is the terminal class, not a feature flag. */
            if (values && n == 4)
                sixel = true;
            ++values;
            p = end;
            if (*p != ';')
                break;
            ++p;
        }
        if (*p != 'c' || p[1] || !values)
            return false;
        t->supported = sixel;
        return true;
    }
    int h, w, used = 0;
    if (sscanf(reply, "\033[6;%d;%dt%n", &h, &w, &used) == 2 && used && !reply[used] && h > 0 &&
        h <= 128 && w > 0 && w <= 128) {
        t->cell_height = h;
        t->cell_width = w;
        return true;
    }
    if (sscanf(reply, "\033[4;%d;%dt%n", &h, &w, &used) == 2 && used && !reply[used]) {
        unsigned rows, columns, pixels_x, pixels_y;
        if (papo_terminal_size(&rows, &columns, &pixels_x, &pixels_y) && h > 0 && w > 0 &&
            h / rows <= 128 && w / columns <= 128) {
            t->cell_height = h / rows;
            t->cell_width = w / columns;
            return true;
        }
    }
    return false;
}
