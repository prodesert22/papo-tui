#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "platform/runtime.h"
#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <unistd.h>

char *papo_executable_path(void)
{
    char *path = g_file_read_link("/proc/self/exe", NULL);
    char *result = path ? strdup(path) : NULL;
    g_free(path);
    return result;
}

bool papo_terminal_size(unsigned *rows, unsigned *columns, unsigned *pixels_x, unsigned *pixels_y)
{
    struct winsize size;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) || !size.ws_row || !size.ws_col)
        return false;
    *rows = size.ws_row;
    *columns = size.ws_col;
    *pixels_x = size.ws_xpixel;
    *pixels_y = size.ws_ypixel;
    return true;
}

bool papo_localtime(const time_t *value, struct tm *result)
{
    return localtime_r(value, result) != NULL;
}

char *papo_mkdtemp(char *pattern)
{
    return mkdtemp(pattern);
}

int papo_mkstemp(char *pattern)
{
    return mkstemp(pattern);
}

bool papo_random_bytes(void *bytes, size_t length)
{
    unsigned char *out = bytes;
    while (length) {
        ssize_t n = getrandom(out, length, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        out += n;
        length -= (size_t)n;
    }
    return true;
}
