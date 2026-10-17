#include "config/app_mode.h"
#include "ui/media/emoji_image.h"
#include "ui/media/media_service.h"
#include <assert.h>
#include <glib/gstdio.h>
#include <string.h>

static const graphics_terminal terminal = {
    .protocol = GRAPHICS_CHAFA, .supported = true, .cell_width = 8, .cell_height = 16};

static void assert_red(const graphics_image *image)
{
    assert(image->protocol == GRAPHICS_CHAFA && image->length >= sizeof(graphics_cell));
    const graphics_cell *cells = (const graphics_cell *)image->data;
    bool red = false;
    for (size_t i = 0; i < image->length / sizeof(*cells); ++i)
        red |= cells[i].foreground == 0xff0000 || cells[i].background == 0xff0000;
    assert(red);
}

static gpointer encode(gpointer unused)
{
    (void)unused;
    unsigned char rgb[8 * 16 * 3] = {0};
    for (size_t i = 0; i < sizeof(rgb); i += 3)
        rgb[i] = 255;
    graphics_image image = {0};
    assert(graphics_image_encode(rgb, 8, 16, terminal, &image));
    assert_red(&image);
    graphics_image_free(&image);
    assert(graphics_image_encode_region_id(rgb, 8, 16, 2, 3, 1, 2, terminal, 77, &image));
    assert_red(&image);
    assert(image.width == 5 && image.height == 11 && image.id == 77);
    graphics_image_free(&image);
    return NULL;
}

int main(void)
{
    app_mode_set(APP_MODE_ULTRALIGHT);
    assert(!media_image_backend() && !media_video_backend());

    app_mode_set(APP_MODE_LIGHT);
    assert(!media_video_backend());
    GThread *threads[4];
    for (size_t i = 0; i < G_N_ELEMENTS(threads); ++i)
        threads[i] = g_thread_new("test-encode", encode, NULL);
    for (size_t i = 0; i < G_N_ELEMENTS(threads); ++i)
        g_thread_join(threads[i]);

    app_mode_set(APP_MODE_NORMAL);
    graphics_image glyph = {0};
    assert(emoji_image_encode_region("😀", 32, 32, 0, terminal, 78, &glyph, 2, 3, 4, 5));
    assert(glyph.width == 23 && glyph.height == 27 && glyph.id == 78 && glyph.data);
    graphics_image_free(&glyph);
    app_mode_set(APP_MODE_LIGHT);

    /* Exercise module discovery and binary pipe transport, including NULs and
     * newlines in pixel data. All paths belong to this synthetic fixture. */
    char *directory = g_dir_make_tmp("papo-service-XXXXXX", NULL);
    assert(directory);
    char *path = g_build_filename(directory, "red.png", NULL);
    static const char png[] = "iVBORw0KGgoAAAANSUhEUgAAAAgAAAAQCAIAAACk6KkqAAAACXBIWXMAAAAAAAAAAQCE"
                              "eRdzAAAAFElEQVR4nGP8z4AdsOAQH5WgqQQAv4ABPawe0s0AAAAASUVORK5CYII=";
    gsize length;
    guchar *pixels = g_base64_decode(png, &length);
    assert(g_file_set_contents(path, (const char *)pixels, length, NULL));
    g_free(pixels);
    image_decode_request request = {
        .path = path, .width = 8, .height = 16, .terminal = terminal, .image_id = 42};
    graphics_image image = {0};
    animation *frames = NULL;
    assert(image_decode_file(&request, &image, &frames, NULL, NULL));
    assert_red(&image);
    graphics_image_free(&image);
    animation_free(frames);
    assert(!g_remove(path) && !g_rmdir(directory));
    g_free(path);
    g_free(directory);

    gint64 start = g_get_monotonic_time();
    media_service_stop();
    assert(g_get_monotonic_time() - start < G_USEC_PER_SEC);
    encode(NULL);
    app_mode_set(APP_MODE_ULTRALIGHT);
    media_service_stop();
    assert(!media_image_backend());
    return 0;
}
