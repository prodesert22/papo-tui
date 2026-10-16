#include "ui/media/emoji_image.h"
#include "ui/media/image_decode.h"
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
static bool render_unicode(PangoFontMap *map, const char *text, int width, int height, uint32_t bg,
                           graphics_terminal terminal, uint32_t id, graphics_image *image)
{
    if (!text || width < 1 || height < 1 || width > 640 || height > 360)
        return false;
    bool ok = false;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);
    PangoContext *context = pango_font_map_create_context(map);
    PangoLayout *layout = pango_layout_new(context);
    PangoFontDescription *font = pango_font_description_from_string("Noto Color Emoji");
    pango_font_description_set_absolute_size(font, height * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    PangoRectangle ink;
    pango_layout_get_pixel_extents(layout, &ink, NULL);
    if (cairo_status(cr) == CAIRO_STATUS_SUCCESS && ink.width > 0 && ink.height > 0 &&
        !pango_layout_get_unknown_glyphs_count(layout)) {
        cairo_set_source_rgb(cr, ((bg >> 16) & 255) / 255., ((bg >> 8) & 255) / 255.,
                             (bg & 255) / 255.);
        cairo_paint(cr);
        double sx = (double)width / ink.width, sy = (double)height / ink.height;
        double scale = sx < sy ? sx : sy;
        cairo_translate(cr, (width - ink.width * scale) / 2, (height - ink.height * scale) / 2);
        cairo_scale(cr, scale, scale);
        cairo_move_to(cr, -ink.x, -ink.y);
        pango_cairo_show_layout(cr, layout);
        cairo_surface_flush(surface);
        unsigned char *rgb = calloc(1, image_frame_bytes(width, height));
        if (rgb) {
            const unsigned char *pixels = cairo_image_surface_get_data(surface);
            int stride = cairo_image_surface_get_stride(surface);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    uint32_t pixel;
                    memcpy(&pixel, pixels + y * stride + x * 4, sizeof(pixel));
                    size_t at = ((size_t)y * (size_t)width + (size_t)x) * 3;
                    rgb[at] = (unsigned char)(pixel >> 16);
                    rgb[at + 1] = (unsigned char)(pixel >> 8);
                    rgb[at + 2] = (unsigned char)pixel;
                }
            ok = graphics_image_encode_id(rgb, width, height, terminal, id, image);
            free(rgb);
        }
    }
    pango_font_description_free(font);
    g_object_unref(layout);
    g_object_unref(context);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return ok;
}
bool emoji_image_encode(const char *text, int width, int height, uint32_t bg,
                        graphics_terminal terminal, uint32_t id, graphics_image *image)
{
    static GPrivate font_map = G_PRIVATE_INIT(g_object_unref);
    PangoFontMap *map = g_private_get(&font_map);
    if (!map) {
        map = pango_cairo_font_map_new();
        g_private_set(&font_map, map);
    }
    return render_unicode(map, text, width, height, bg, terminal, id, image);
}
bool emoji_image_available(void)
{
    PangoFontMap *map = pango_cairo_font_map_new();
    PangoFontFamily **families = NULL;
    int count = 0;
    bool available = false;
    pango_font_map_list_families(map, &families, &count);
    for (int i = 0; i < count; ++i)
        if (!strcmp(pango_font_family_get_name(families[i]), "Noto Color Emoji"))
            available = true;
    g_free(families);
    g_object_unref(map);
    return available;
}
