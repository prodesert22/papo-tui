#define _POSIX_C_SOURCE 200809L
#include "ui/sixel.h"
#include <ncurses.h>
#include <sixel.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
static sixel_terminal terminal;
static bool probed;
bool sixel_terminal_reply(sixel_terminal *t, const char *reply)
{
    if (!t || !reply || strlen(reply) > 96) return false;
    if (!strncmp(reply, "\033[?", 3)) {
        const char *p = reply + 3;
        bool sixel = false;
        int values = 0;
        while (*p >= '0' && *p <= '9' && values < 24) {
            char *end; unsigned long n = strtoul(p, &end, 10);
            if (n > 65535) return false;
            /* The first number is the terminal class, not a feature flag. */
            if (values && n == 4) sixel = true;
            ++values; p = end;
            if (*p != ';') break;
            ++p;
        }
        if (*p != 'c' || p[1] || !values) return false;
        t->supported = sixel;
        return true;
    }
    int h, w, used = 0;
    if (sscanf(reply, "\033[6;%d;%dt%n", &h, &w, &used) == 2 && used && !reply[used] &&
        h > 0 && h <= 128 && w > 0 && w <= 128) {
        t->cell_height = h; t->cell_width = w; return true;
    }
    if (sscanf(reply, "\033[4;%d;%dt%n", &h, &w, &used) == 2 && used && !reply[used]) {
        struct winsize size;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row && size.ws_col && h > 0 && w > 0 &&
            h / size.ws_row <= 128 && w / size.ws_col <= 128) {
            t->cell_height = h / size.ws_row; t->cell_width = w / size.ws_col; return true;
        }
    }
    return false;
}
void sixel_terminal_probe(void)
{
    if (probed || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return;
    probed = true;
    const char *term = getenv("TERM");
    if (!term || !strcmp(term, "dumb")) return;
    /* Called after raw/noecho and the first frame. Replies are filtered by
     * the shared curses input wrapper; negotiation never waits here. */
    fputs("\033[c\033[16t\033[14t", stdout); fflush(stdout);
}
int sixel_terminal_input(wint_t *key)
{
    for (;;) {
        int type = get_wch(key);
        if (type != OK || *key != 27 || !probed) return type;
        wint_t next;
        int next_type = get_wch(&next);
        if (next_type != OK || next != '[') {
            if (next_type == OK) unget_wch(next);
            else if (next_type == KEY_CODE_YES) ungetch((int)next);
            return OK;
        }
        char reply[97] = "\033["; size_t used = 2;
        while (used < sizeof(reply) - 1) {
            next_type = get_wch(&next);
            if (next_type != OK || next > 127) break;
            reply[used++] = (char)next; reply[used] = 0;
            if (next == 'c' || next == 't' || !((next >= '0' && next <= '9') || next == '?' || next == ';')) break;
        }
        if (sixel_terminal_reply(&terminal, reply)) continue;
        /* Unknown sequences preserve ordinary curses behavior: ESC followed
         * by literal input. Recognized keyboard/mouse codes never enter here. */
        if (next_type == KEY_CODE_YES) ungetch((int)next);
        else if (next_type == OK && next > 127) unget_wch(next);
        while (used > 1) unget_wch((unsigned char)reply[--used]);
        *key = 27; return OK;
    }
}
sixel_terminal sixel_terminal_get(void)
{
    struct winsize size;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row && size.ws_col && size.ws_xpixel && size.ws_ypixel) {
        terminal.cell_width = size.ws_xpixel / size.ws_col;
        terminal.cell_height = size.ws_ypixel / size.ws_row;
    }
    sixel_terminal result = terminal;
    result.supported &= result.cell_width > 0 && result.cell_width <= 128 && result.cell_height > 0 && result.cell_height <= 128;
    return result;
}
static int append(char *data, int size, void *context)
{
    sixel_image *out = context;
    if (size < 0 || (size_t)size > 4U * 1024U * 1024U - out->length) return -1;
    char *next = realloc(out->data, out->length + (size_t)size);
    if (!next && size) return -1;
    out->data = next; memcpy(next + out->length, data, (size_t)size); out->length += (size_t)size;
    return size;
}
bool sixel_image_encode(const unsigned char *pixels, int width, int height, sixel_image *out)
{
    memset(out, 0, sizeof(*out));
    if (!pixels || width < 1 || height < 1 || width > 640 || height > 360) return false;
    sixel_output_t *output = NULL;
    sixel_dither_t *dither = NULL;
    bool ok = false;
    if (SIXEL_FAILED(sixel_output_new(&output, append, out, NULL)) ||
        SIXEL_FAILED(sixel_dither_new(&dither, 256, NULL))) goto done;
    if (SIXEL_FAILED(sixel_dither_initialize(dither, (unsigned char *)pixels, width, height, SIXEL_PIXELFORMAT_RGB888,
        SIXEL_LARGE_AUTO, SIXEL_REP_AUTO, SIXEL_QUALITY_LOW))) goto done;
    sixel_dither_set_diffusion_type(dither, SIXEL_DIFFUSE_NONE);
    ok = SIXEL_SUCCEEDED(sixel_encode((unsigned char *)pixels, width, height, 3, dither, output));
done:
    sixel_output_unref(output); sixel_dither_unref(dither);
    if (!ok) sixel_image_free(out);
    return ok;
}
void sixel_image_free(sixel_image *out) { free(out->data); out->data = NULL; out->length = 0; }
void sixel_image_draw(const sixel_image *image, int y, int x)
{
    if (!image || !image->data) return;
    /* DEC save/restore leaves ncurses' physical cursor model intact. */
    fprintf(stdout, "\0337\033[%d;%dH", y + 1, x + 1);
    fwrite(image->data, 1, image->length, stdout);
    fputs("\0338", stdout); fflush(stdout);
}
