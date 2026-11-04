#define _POSIX_C_SOURCE 200809L
#include "ui/media/image_decode.h"
#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
static bool cancelled(void *context)
{
    (void)context;
    return true;
}
/* The iTerm2 payload is a little-endian TIFF: alpha needs four samples per
 * pixel (tag 277) and an ExtraSamples entry (tag 338). */
static bool iterm2_tiff_has_alpha(const graphics_image *image)
{
    const char *start = memmem(image->data, image->length, ":", 1);
    const char *end =
        start ? memchr(start, '\a', image->length - (size_t)(start - image->data)) : NULL;
    if (!start || !end)
        return false;
    char *text = g_strndup(start + 1, (gsize)(end - start - 1));
    gsize size = 0;
    guchar *tiff = g_base64_decode(text, &size);
    g_free(text);
    bool samples = false, extra = false;
    if (size >= 8 && !memcmp(tiff, "II*\0", 4)) {
        guint32 at = tiff[4] | tiff[5] << 8 | tiff[6] << 16 | (guint32)tiff[7] << 24;
        if (at + 2 <= size) {
            unsigned count = tiff[at] | tiff[at + 1] << 8;
            for (unsigned i = 0; i < count && at + 2 + 12 * (i + 1) <= size; ++i) {
                const guchar *entry = tiff + at + 2 + 12 * i;
                unsigned tag = entry[0] | entry[1] << 8, value = entry[8] | entry[9] << 8;
                samples |= tag == 277 && value == 4;
                extra |= tag == 338;
            }
        }
    }
    g_free(tiff);
    return samples && extra;
}
int main(int argc, char **argv)
{
    assert(argc == 13);
    image_decode_request request = {
        .width = 640,
        .height = 360,
        .native = true,
        .preview = true,
        .image_id = 42,
        .terminal = {.protocol = GRAPHICS_SIXEL, .cell_width = 8, .cell_height = 16}};
    for (int mode = GRAPHICS_SIXEL; mode <= GRAPHICS_ITERM2; ++mode) {
        request.terminal.protocol = (graphics_protocol)mode;
        graphics_image image = {0};
        animation *frames = NULL;
        request.path = argv[1];
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 16 && image.height == 12 && !frames);
        assert(image.source_width == 16 && image.source_height == 12);
        graphics_image_free(&image);
        request.region = true;
        request.top = 2;
        request.bottom = 3;
        request.left = 4;
        request.right = 5;
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 7 && image.height == 7 && !frames);
        assert(image.source_width == 16 && image.source_height == 12);
        graphics_image_free(&image);
        request.left = 16;
        assert(!image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(!image.data && !frames);
        request.top = request.bottom = request.left = request.right = 0;
        request.region = false;
        request.path = argv[2];
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(frames && frames->count == 2 && frames->width == 16 && frames->height == 12);
        assert(frames->delay[0] == 200 && frames->delay[1] == 200 && frames->duration == 400);
        assert(animation_frame_at(frames, 1000, 1199) == 0);
        assert(animation_frame_at(frames, 1000, 1200) == 1);
        assert(animation_frame_at(frames, 1000, 1399) == 1);
        assert(animation_frame_at(frames, 1000, 1400) == 0);
        assert(frames->pixels[0][0] > 230 && frames->pixels[0][2] < 25);
        assert(frames->pixels[1][0] < 25 && frames->pixels[1][2] > 230);
        request.animation = frames;
        request.frame = 1;
        uint64_t first_revision = image.revision;
        graphics_image_free(&image);
        assert(image_encode_frame(&request, &image));
        assert(image.id == 42 && image.revision != first_revision && image.width == 16);
        graphics_image_free(&image);
        request.frame = 2;
        assert(!image_encode_frame(&request, &image));
        animation_ref(frames);
        animation_free(frames);
        animation_free(frames);
        request.animation = NULL;
        frames = NULL;
        request.frame = 0;
        request.path = argv[3];
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 640 && image.height == 320 && !frames);
        assert(image.source_width == 1000 && image.source_height == 500);
        graphics_image_free(&image);
        request.path = argv[4];
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 128 && image.height == 160 && !frames);
        graphics_image_free(&image);
        request.top = 32;
        request.bottom = 16;
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.height == (mode == GRAPHICS_KITTY ? 160 : 112));
        assert(image.source_height == 160);
        graphics_image_free(&image);
        request.top = request.bottom = 0;
        assert(!image_decode_file(&request, &image, &frames, cancelled, NULL));
        assert(!frames && !image.data);
        request.path = argv[6];
        assert(!image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(!frames && !image.data);
        request.width = 1600;
        request.height = 1000;
        request.path = argv[3];
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 1000 && image.height == 500 && !frames);
        graphics_image_free(&image);
        request.path = argv[7];
        request.width = 1200;
        request.height = 900;
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 600 && image.height == 900 && !frames);
        assert(image.source_width == 1000 && image.source_height == 1500);
        graphics_image_free(&image);
        request.path = argv[8];
        request.width = 960;
        request.height = 720;
        request.retain_static = true;
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 960 && image.height == 720);
        assert(frames && frames->count == 1 && frames->bytes == image_frame_bytes(960, 720));
        assert(image.length <= graphics_image_capacity(image.width, image.height));
        animation_free(frames);
        frames = NULL;
        graphics_image_free(&image);
        request.width = request.height = 100000;
        assert(!image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(!frames && !image.data);
        request.width = 640;
        request.height = 360;
        request.retain_static = false;
        request.path = argv[5];
        assert(!image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(!frames && !image.data);
        image_decode_request avatar = request;
        avatar.path = argv[2];
        avatar.width = avatar.height = 48;
        avatar.native = false;
        avatar.center = avatar.matte = true;
        avatar.cover = true;
        avatar.background = 0x15181e;
        assert(image_decode_file(&avatar, &image, &frames, NULL, NULL));
        assert(frames && frames->width == 48 && frames->height == 48);
        for (size_t f = 0; f < frames->count; ++f)
            for (int y = 0; y < 48; ++y)
                for (int x = 0; x < 48; ++x) {
                    const unsigned char *pixel = frames->pixels[f] + (y * 48 + x) * 3;
                    assert(pixel[f ? 2 : 0] > 230);
                }
        graphics_image_free(&image);
        animation_free(frames);
        frames = NULL;
    }
    /* One canonical transparent raster supports different focus backgrounds. */
    image_decode_request transparent = {.path = argv[9],
                                        .width = 48,
                                        .height = 48,
                                        .native = true,
                                        .decode_only = true,
                                        .retain_static = true};
    graphics_image red = {0}, blue = {0};
    animation *alpha = NULL;
    assert(image_decode_file(&transparent, &red, &alpha, NULL, NULL));
    assert(alpha && alpha->rgba[0] && alpha->pixels[0][3] == 0 && !red.data);
    transparent.animation = alpha;
    transparent.decode_only = false;
    transparent.native = false;
    transparent.center = transparent.cover = transparent.matte = true;
    transparent.terminal =
        (graphics_terminal){.protocol = GRAPHICS_KITTY, .cell_width = 8, .cell_height = 16};
    transparent.image_id = 42;
    /* Kitty and iTerm2 receive the alpha itself, whatever the background. */
    const graphics_protocol alpha_protocols[] = {GRAPHICS_KITTY, GRAPHICS_ITERM2};
    for (size_t i = 0; i < 2; ++i) {
        transparent.terminal.protocol = alpha_protocols[i];
        transparent.background = 0xff0000;
        assert(image_encode_frame(&transparent, &red));
        transparent.background = 0x0000ff;
        assert(image_encode_frame(&transparent, &blue));
        assert(red.width == 48 && red.height == 48 && blue.width == 48 && blue.height == 48);
        assert(red.length == blue.length && !memcmp(red.data, blue.data, red.length));
        if (alpha_protocols[i] == GRAPHICS_KITTY)
            assert(memmem(red.data, red.length, "f=32,", 5));
        else
            assert(memmem(red.data, red.length, "\033]1337;File=", 12) &&
                   iterm2_tiff_has_alpha(&red));
        graphics_image_free(&red);
        graphics_image_free(&blue);
    }
    /* Sixel has no partial transparency: it blends into the given background. */
    transparent.terminal.protocol = GRAPHICS_SIXEL;
    transparent.background = 0xff0000;
    assert(image_encode_frame(&transparent, &red));
    transparent.background = 0x0000ff;
    assert(image_encode_frame(&transparent, &blue));
    assert(red.length != blue.length || memcmp(red.data, blue.data, red.length));
    graphics_image_free(&red);
    graphics_image_free(&blue);
    animation_free(alpha);
    image_decode_request poster = {
        .path = argv[10],
        .width = 160,
        .height = 120,
        .native = true,
        .terminal = {.protocol = GRAPHICS_KITTY, .cell_width = 8, .cell_height = 16}};
    graphics_image cover = {0};
    animation *still = NULL;
    assert(!image_decode_file(&poster, &cover, &still, NULL, NULL));
    poster.video_poster = true;
    assert(image_decode_file(&poster, &cover, &still, NULL, NULL));
    assert(cover.source_width == 32 && cover.source_height == 24 && !still);
    graphics_image_free(&cover);
    poster.decode_only = poster.retain_static = true;
    assert(image_decode_file(&poster, &cover, &still, NULL, NULL));
    assert(still && still->count == 1 && still->source_width == 32);
    animation_free(still);
    still = NULL;
    assert(!image_decode_file(&poster, &cover, &still, cancelled, NULL));
    /* A silent clip loops like a GIF, B-frame delay included; sound keeps it a cover. */
    image_decode_request loop = poster;
    loop.path = argv[11];
    loop.video_clip = true;
    loop.decode_only = loop.retain_static = true;
    animation *frames = NULL;
    assert(image_decode_file(&loop, &cover, &frames, NULL, NULL));
    assert(frames && frames->count == 5 && frames->duration == 500 && frames->delay[0] == 100);
    animation_free(frames);
    frames = NULL;
    loop.path = argv[10];
    assert(image_decode_file(&loop, &cover, &frames, NULL, NULL));
    assert(frames && frames->count == 1);
    animation_free(frames);
    /* Frames that would not fit the budget at full size stay animated, smaller. */
    image_decode_request many = {
        .path = argv[12],
        .width = 480,
        .height = 426,
        .native = true,
        .decode_only = true,
        .retain_static = true,
        .terminal = {.protocol = GRAPHICS_KITTY, .cell_width = 8, .cell_height = 16}};
    assert(image_decode_file(&many, &cover, &frames, NULL, NULL));
    assert(frames && frames->count == 150 && frames->duration == 10000);
    assert(frames->width < 480 && frames->source_width == 480 && frames->bytes <= ANIMATION_BYTES);
    animation_free(frames);
    puts("GIF frame pixels/delays, native dimensions, proportional limits, crops and cancellation "
         "verified");
    return 0;
}
