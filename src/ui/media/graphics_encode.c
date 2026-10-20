#include "ui/media/graphics.h"
#include "ui/media/sixel.h"
#include <chafa.h>
#include <gio/gio.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static atomic_uint_fast64_t next_revision = 1;
static bool kitty_compressed(const unsigned char *pixels, graphics_image *out)
{
    size_t raw = (size_t)out->width * out->height * 3;
    /* GIO supplies the ZLIB codec. A bounded spare block covers expansion;
     * unusual conversion failures fall back to Chafa's existing transport. */
    unsigned char *compressed = malloc(raw + 64U * 1024U);
    if (!compressed)
        return false;
    GZlibCompressor *codec = g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB, 1);
    gsize consumed = 0, written = 0;
    GConverterResult result =
        g_converter_convert(G_CONVERTER(codec), pixels, raw, compressed, raw + 64U * 1024U,
                            G_CONVERTER_INPUT_AT_END, &consumed, &written, NULL);
    g_object_unref(codec);
    if (result != G_CONVERTER_FINISHED || consumed != raw || written >= raw) {
        free(compressed);
        return false;
    }
    /* 3072 input bytes become at most 4096 base64 bytes per protocol chunk.
     * No full-size base64 copy is kept alongside the encoded output. */
    size_t chunks = (written + 3071) / 3072;
    size_t capacity = (written + 2) / 3 * 4 + chunks * 32 + 256;
    if (capacity > graphics_image_capacity(out->width, out->height)) {
        free(compressed);
        return false;
    }
    char *data = malloc(capacity);
    if (!data) {
        free(compressed);
        return false;
    }
    int header =
        snprintf(data, capacity, "\033_Ga=t,f=24,s=%d,v=%d,c=%d,r=%d,i=%u,o=z,m=1,q=2,C=1;\033\\",
                 out->width, out->height, out->columns, out->rows, out->id);
    size_t at = header > 0 ? (size_t)header : capacity;
    for (size_t offset = 0; offset < written && at < capacity; offset += 3072) {
        size_t count = MIN((size_t)written - offset, 3072U);
        char *encoded = g_base64_encode(compressed + offset, count);
        size_t length = strlen(encoded);
        header = snprintf(data + at, capacity - at, "\033_Gm=%d;", offset + count < written);
        if (header < 0 || (size_t)header >= capacity - at ||
            length + 2 >= capacity - at - (size_t)header) {
            g_free(encoded);
            at = capacity;
            break;
        }
        at += (size_t)header;
        memcpy(data + at, encoded, length);
        g_free(encoded);
        at += length;
        memcpy(data + at, "\033\\", 2);
        at += 2;
    }
    free(compressed);
    if (at >= capacity) {
        free(data);
        return false;
    }
    data[at] = 0;
    out->data = data;
    out->length = at;
    return true;
}
bool graphics_image_encode(const unsigned char *pixels, int width, int height, graphics_terminal t,
                           graphics_image *out)
{
    return graphics_image_encode_id(pixels, width, height, t, graphics_image_next_id(), out);
}

bool graphics_image_encode_id(const unsigned char *pixels, int width, int height,
                              graphics_terminal t, uint32_t id, graphics_image *out)
{
    memset(out, 0, sizeof(*out));
    if (!pixels || !graphics_image_capacity(width, height) || t.protocol == GRAPHICS_TEXT ||
        t.cell_width < 1 || t.cell_width > 128 || t.cell_height < 1 || t.cell_height > 128)
        return false;
    out->protocol = t.protocol;
    out->id = id;
    out->revision = atomic_fetch_add(&next_revision, 1);
    out->width = width;
    out->height = height;
    out->cell_width = t.cell_width;
    out->cell_height = t.cell_height;
    out->columns = (width + t.cell_width - 1) / t.cell_width;
    out->rows = (height + t.cell_height - 1) / t.cell_height;
    if (t.protocol == GRAPHICS_KITTY && kitty_compressed(pixels, out))
        return true;
    if (t.protocol == GRAPHICS_CHAFA) {
        ChafaCanvasConfig *config = chafa_canvas_config_new();
        chafa_canvas_config_set_pixel_mode(config, CHAFA_PIXEL_MODE_SYMBOLS);
        chafa_canvas_config_set_canvas_mode(config, CHAFA_CANVAS_MODE_TRUECOLOR);
        chafa_canvas_config_set_geometry(config, out->columns, out->rows);
        chafa_canvas_config_set_cell_geometry(config, t.cell_width, t.cell_height);
        ChafaSymbolMap *symbols = chafa_symbol_map_new();
        chafa_symbol_map_add_by_tags(symbols, CHAFA_SYMBOL_TAG_BLOCK | CHAFA_SYMBOL_TAG_ASCII);
        chafa_canvas_config_set_symbol_map(config, symbols);
        chafa_symbol_map_unref(symbols);
        ChafaCanvas *canvas = chafa_canvas_new(config);
        chafa_canvas_config_unref(config);
        if (!canvas)
            return false;
        chafa_canvas_draw_all_pixels(canvas, CHAFA_PIXEL_RGB8, pixels, width, height, width * 3);
        out->length = (size_t)out->columns * (size_t)out->rows * sizeof(graphics_cell);
        out->data = malloc(out->length);
        if (!out->data) {
            chafa_canvas_unref(canvas);
            memset(out, 0, sizeof(*out));
            return false;
        }
        graphics_cell *cells = (graphics_cell *)out->data;
        for (int y = 0; y < out->rows; ++y)
            for (int x = 0; x < out->columns; ++x) {
                graphics_cell *cell = &cells[(size_t)y * (size_t)out->columns + (size_t)x];
                cell->character = chafa_canvas_get_char_at(canvas, x, y);
                chafa_canvas_get_colors_at(canvas, x, y, &cell->foreground, &cell->background);
            }
        chafa_canvas_unref(canvas);
        return true;
    }
    if (t.protocol == GRAPHICS_SIXEL) {
        sixel_image image;
        if (!sixel_image_encode(pixels, width, height, &image))
            return false;
        out->data = image.data;
        out->length = image.length;
        return true;
    }
    ChafaCanvasConfig *config = chafa_canvas_config_new();
    ChafaTermInfo *info = chafa_term_db_get_fallback_info(chafa_term_db_get_default());
    ChafaCanvas *canvas = NULL;
    GString *printed = NULL;
    ChafaFrame *frame = NULL;
    ChafaImage *source = NULL;
    ChafaPlacement *placement = NULL;
    bool ok = false;
    if (!config || !info)
        goto done;
    chafa_canvas_config_set_pixel_mode(
        config, t.protocol == GRAPHICS_KITTY ? CHAFA_PIXEL_MODE_KITTY : CHAFA_PIXEL_MODE_ITERM2);
    chafa_canvas_config_set_geometry(config, out->columns, out->rows);
    chafa_canvas_config_set_cell_geometry(config, t.cell_width, t.cell_height);
    if (t.protocol == GRAPHICS_KITTY) {
        char header[160];
        snprintf(header, sizeof(header),
                 "\033_Ga=t,f=%%1,s=%%2,v=%%3,c=%%4,r=%%5,i=%u,m=1,q=2,C=1\033\\", out->id);
        if (!chafa_term_info_set_seq(info, CHAFA_TERM_SEQ_BEGIN_KITTY_IMMEDIATE_IMAGE_V1, header,
                                     NULL))
            goto done;
    }
    canvas = chafa_canvas_new(config);
    if (!canvas)
        goto done;
    frame = chafa_frame_new_borrow(pixels, CHAFA_PIXEL_RGB8, width, height, width * 3);
    source = chafa_image_new();
    if (!frame || !source)
        goto done;
    chafa_image_set_frame(source, frame);
    placement = chafa_placement_new(source, 1);
    if (!placement)
        goto done;
    chafa_placement_set_tuck(placement, CHAFA_TUCK_SHRINK_TO_FIT);
    chafa_placement_set_halign(placement, CHAFA_ALIGN_START);
    chafa_placement_set_valign(placement, CHAFA_ALIGN_START);
    chafa_canvas_set_placement(canvas, placement);
    printed = chafa_canvas_print(canvas, info);
    if (!printed || !printed->len || printed->len > graphics_image_capacity(width, height))
        goto done;
    out->data = malloc(printed->len);
    if (!out->data)
        goto done;
    memcpy(out->data, printed->str, printed->len);
    out->length = printed->len;
    ok = true;
done:
    if (printed)
        g_string_free(printed, true);
    if (canvas)
        chafa_canvas_unref(canvas);
    if (placement)
        chafa_placement_unref(placement);
    if (source)
        chafa_image_unref(source);
    if (frame)
        chafa_frame_unref(frame);
    if (config)
        chafa_canvas_config_unref(config);
    if (info)
        chafa_term_info_unref(info);
    if (!ok)
        graphics_image_free(out);
    return ok;
}

bool graphics_image_encode_region_id(const unsigned char *pixels, int width, int height, int top,
                                     int bottom, int left, int right, graphics_terminal t,
                                     uint32_t id, graphics_image *out)
{
    memset(out, 0, sizeof(*out));
    if (!pixels || !graphics_image_capacity(width, height) || top < 0 || bottom < 0 || left < 0 ||
        right < 0 || top >= height || bottom >= height - top || left >= width ||
        right >= width - left)
        return false;
    int w = width - left - right, h = height - top - bottom;
    if (!left && !right)
        return graphics_image_encode_id(pixels + (size_t)top * width * 3, w, h, t, id, out);
    unsigned char *packed = calloc(1, (size_t)w * h * 3 + 64);
    if (!packed)
        return false;
    for (int row = 0; row < h; ++row)
        memcpy(packed + (size_t)row * w * 3, pixels + ((size_t)(row + top) * width + left) * 3,
               (size_t)w * 3);
    bool ok = graphics_image_encode_id(packed, w, h, t, id, out);
    free(packed);
    return ok;
}

bool graphics_image_encode_preview(const unsigned char *pixels, int width, int height,
                                   graphics_terminal t, uint32_t id, graphics_image *out)
{
    if (!graphics_image_encode_id(pixels, width, height, t, id, out))
        return false;
    if (t.protocol == GRAPHICS_KITTY || t.protocol == GRAPHICS_CHAFA)
        return true;
    int rows = (height + t.cell_height - 1) / t.cell_height;
    if (rows > 6)
        return true;
    size_t count = (size_t)rows * (size_t)(rows + 1) / 2 - 1;
    if (!count)
        return true;
    out->crops = calloc(count, sizeof(*out->crops));
    if (!out->crops) {
        graphics_image_free(out);
        return false;
    }
    out->crop_count = count;
    size_t at = 0;
    for (int top = 0; top < rows; ++top)
        for (int bottom = 0; bottom < rows - top; ++bottom) {
            if (!top && !bottom)
                continue;
            int crop_bottom = bottom ? height - (rows - bottom) * t.cell_height : 0;
            int h = height - top * t.cell_height - crop_bottom;
            if (h < 1)
                continue;
            graphics_image *crop = &out->crops[at++];
            if (!graphics_image_encode_id(pixels + (size_t)top * (size_t)t.cell_height *
                                                       (size_t)width * 3,
                                          width, h, t, id, crop)) {
                graphics_image_free(out);
                return false;
            }
            crop->crop_top = top * t.cell_height;
            crop->crop_bottom = crop_bottom;
        }
    return true;
}
