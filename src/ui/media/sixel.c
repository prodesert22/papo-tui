#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/media/sixel.h"
#include "platform/runtime.h"
#include "ui/media/graphics.h"
#include <sixel.h>
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
typedef struct {
    sixel_image *image;
    size_t limit;
} sixel_writer;
static int append(char *data, int size, void *context)
{
    sixel_writer *writer = context;
    sixel_image *out = writer->image;
    if (size == 0)
        return 0;
    if (size < 0 || (size_t)size > writer->limit - out->length)
        return -1;
    char *next = realloc(out->data, out->length + (size_t)size);
    if (!next && size)
        return -1;
    out->data = next;
    memcpy(next + out->length, data, (size_t)size);
    out->length += (size_t)size;
    return size;
}
bool sixel_image_encode(const unsigned char *pixels, int width, int height, sixel_image *out)
{
    memset(out, 0, sizeof(*out));
    size_t capacity = graphics_image_capacity(width, height);
    if (!pixels || !capacity)
        return false;
    sixel_writer writer = {out, capacity};
    sixel_output_t *output = NULL;
    sixel_dither_t *dither = NULL;
    bool ok = false;
    if (SIXEL_FAILED(sixel_output_new(&output, append, &writer, NULL)) ||
        SIXEL_FAILED(sixel_dither_new(&dither, 256, NULL)))
        goto done;
    if (SIXEL_FAILED(sixel_dither_initialize(dither, (unsigned char *)pixels, width, height,
                                             SIXEL_PIXELFORMAT_RGB888, SIXEL_LARGE_AUTO,
                                             SIXEL_REP_AUTO, SIXEL_QUALITY_LOW)))
        goto done;
    sixel_dither_set_diffusion_type(dither, SIXEL_DIFFUSE_NONE);
    ok = SIXEL_SUCCEEDED(sixel_encode((unsigned char *)pixels, width, height, 3, dither, output));
done:
    sixel_output_unref(output);
    sixel_dither_unref(dither);
    if (!ok)
        sixel_image_free(out);
    return ok;
}
void sixel_image_free(sixel_image *out)
{
    free(out->data);
    out->data = NULL;
    out->length = 0;
}
