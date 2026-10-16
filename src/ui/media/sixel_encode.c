#include "ui/media/graphics.h"
#include "ui/media/sixel.h"
#include <sixel.h>
#include <stdlib.h>
#include <string.h>
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
