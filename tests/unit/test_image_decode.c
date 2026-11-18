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
/* Reads one pixel of the uncompressed TIFF an iTerm2 payload carries. */
static void iterm2_pixel(const graphics_image *image, int x, int y, unsigned char rgb[3])
{
    const char *start = memmem(image->data, image->length, ":", 1);
    const char *end =
        start ? memchr(start, '\a', image->length - (size_t)(start - image->data)) : NULL;
    assert(start && end);
    char *text = g_strndup(start + 1, (gsize)(end - start - 1));
    gsize size = 0;
    guchar *tiff = g_base64_decode(text, &size);
    g_free(text);
    assert(size >= 8 && !memcmp(tiff, "II*\0", 4));
    guint32 at = tiff[4] | tiff[5] << 8 | tiff[6] << 16 | (guint32)tiff[7] << 24;
    guint32 width = 0, pixels = 0, samples = 0;
    assert(at + 2 <= size);
    unsigned count = tiff[at] | tiff[at + 1] << 8;
    for (unsigned i = 0; i < count && at + 2 + 12 * (i + 1) <= size; ++i) {
        const guchar *entry = tiff + at + 2 + 12 * i;
        unsigned tag = entry[0] | entry[1] << 8;
        guint32 value = entry[8] | entry[9] << 8 | entry[10] << 16 | (guint32)entry[11] << 24;
        if (tag == 256) /* ImageWidth */
            width = value;
        else if (tag == 273) /* StripOffsets: a single strip */
            pixels = value;
        else if (tag == 277) /* SamplesPerPixel */
            samples = value & 0xffff;
    }
    gsize offset = pixels + ((gsize)y * width + (gsize)x) * samples;
    assert(width && samples >= 3 && (guint32)x < width && offset + 3 <= size);
    memcpy(rgb, tiff + offset, 3);
    g_free(tiff);
}
/* A cell of the play mark drawn with characters: white on black, blank or ▶. */
static bool play_box(const graphics_cell *cell)
{
    return (cell->character == ' ' || cell->character == 0x25b6) && cell->foreground == 0xffffff &&
           !cell->background;
}
/* Names the saturated quadrant colour of a pixel: R, G, B or W. */
static char quadrant(const animation *frames, int x, int y)
{
    int channels = frames->rgba[0] ? 4 : 3;
    const unsigned char *p = frames->pixels[0] + ((size_t)y * frames->width + x) * channels;
    bool r = p[0] > 190, g = p[1] > 190, b = p[2] > 190;
    bool dark_r = p[0] < 70, dark_g = p[1] < 70, dark_b = p[2] < 70;
    return r && g && b             ? 'W'
           : r && dark_g && dark_b ? 'R'
           : g && dark_r && dark_b ? 'G'
           : b && dark_r && dark_g ? 'B'
                                   : '?';
}
int main(int argc, char **argv)
{
    assert(argc == 22);
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
    /* A still cover carries a play mark in the middle of the picture: on pixel
     * protocols a dark disc with a white triangle, composed before any crop. */
    image_decode_request marked = poster;
    marked.native = false;
    assert(image_decode_file(&marked, &cover, &still, NULL, NULL));
    assert(still && still->width == 160 && still->height == 120 && !still->rgba[0]);
    marked.decode_only = marked.retain_static = false;
    marked.path = NULL;
    marked.animation = still;
    marked.image_id = 42;
    marked.terminal.protocol = GRAPHICS_ITERM2;
    unsigned char pixel[3];
    assert(image_encode_frame(&marked, &cover));
    iterm2_pixel(&cover, 80, 60, pixel);
    assert(pixel[0] < 30 && pixel[2] > 200);
    graphics_image_free(&cover);
    marked.play_badge = true;
    assert(image_encode_frame(&marked, &cover));
    assert(cover.width == 160 && cover.height == 120);
    iterm2_pixel(&cover, 80, 60, pixel); /* The triangle. */
    assert(pixel[0] > 230 && pixel[1] > 230 && pixel[2] > 230);
    iterm2_pixel(&cover, 62, 60, pixel); /* The disc beside it darkens the picture. */
    assert(pixel[0] < 30 && pixel[2] > 80 && pixel[2] < 125);
    iterm2_pixel(&cover, 4, 4, pixel); /* The rest of the picture is untouched. */
    assert(pixel[0] < 30 && pixel[2] > 200);
    graphics_image_free(&cover);
    /* Characters get a black box of whole cells around the ▶ of the text labels:
     * five by three in rows 2 to 4, and rows cut above the cover take their part
     * of it instead of moving it down. */
    marked.terminal.protocol = GRAPHICS_CHAFA;
    for (int cut = 0; cut <= 6; ++cut) {
        marked.top = cut * 16;
        assert(image_encode_frame(&marked, &cover));
        assert(cover.columns == 20 && cover.rows == 8 - cut);
        const graphics_cell *cells = (const graphics_cell *)cover.data;
        for (int row = cut; row < 8; ++row)
            for (int column = 0; column < 20; ++column) {
                const graphics_cell *cell = &cells[(row - cut) * 20 + column];
                bool inside = row >= 2 && row <= 4 && column >= 8 && column <= 12;
                assert(play_box(cell) == inside);
                assert((cell->character == 0x25b6) == (row == 3 && column == 10));
            }
        graphics_image_free(&cover);
    }
    /* A cover of three rows keeps a single row for the mark. */
    marked.top = 0;
    marked.width = 64;
    marked.height = 48;
    assert(image_encode_frame(&marked, &cover));
    assert(cover.columns == 8 && cover.rows == 3);
    const graphics_cell *small = (const graphics_cell *)cover.data;
    for (int i = 0; i < 24; ++i) {
        assert(play_box(&small[i]) == (i >= 8 + 3 && i <= 8 + 5));
        assert((small[i].character == 0x25b6) == (i == 8 + 4));
    }
    graphics_image_free(&cover);
    marked.play_badge = false;
    assert(image_encode_frame(&marked, &cover));
    for (int i = 0; i < 24; ++i)
        assert(!play_box(&((const graphics_cell *)cover.data)[i]));
    graphics_image_free(&cover);
    animation_free(still);
    still = NULL;
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
    /* Enlarging eight times spreads a one-pixel edge over a ramp whatever the filter; bicubic
     * makes the ramp steeper than bilinear does (largest step between neighbours: 39 against
     * 32 of 255 with the bundled FFmpeg), so enlarged pictures look sharper on dense displays. */
    image_decode_request edge = {
        .path = argv[13],
        .width = 128,
        .height = 128,
        .decode_only = true,
        .retain_static = true,
        .terminal = {.protocol = GRAPHICS_KITTY, .cell_width = 8, .cell_height = 16}};
    animation *enlarged = NULL;
    assert(image_decode_file(&edge, &cover, &enlarged, NULL, NULL));
    assert(enlarged && enlarged->width == 128 && enlarged->height == 128);
    int channels = enlarged->rgba[0] ? 4 : 3, steepest = 0;
    for (int x = 1; x < 128; ++x) {
        const unsigned char *row = enlarged->pixels[0] + (size_t)64 * 128 * channels;
        int step = row[x * channels] - row[(x - 1) * channels];
        steepest = step > steepest ? step : steepest;
    }
    assert(steepest >= 36);
    animation_free(enlarged);
    /* A camera photo is shown upright: the 64x32 picture stored as red, green
     * over blue, white is turned by its EXIF orientation 1 to 8, and the four
     * quarter turns swap the reported size. Corners: top left, top right,
     * bottom left, bottom right. */
    static const char *const upright[] = {"RGBW", "GRWB", "WBGR", "BWRG",
                                          "RBGW", "BRWG", "WGBR", "GWRB"};
    for (int orientation = 1; orientation <= 8; ++orientation) {
        int width = orientation < 5 ? 64 : 32, height = orientation < 5 ? 32 : 64;
        /* Full size, then reduced to a quarter by the scaler before the turn. */
        for (int divisor = 1; divisor <= 4; divisor += 3) {
            image_decode_request photo = {
                .path = argv[13 + orientation],
                .width = 64 / divisor,
                .height = 64 / divisor,
                .native = true,
                .decode_only = true,
                .retain_static = true,
                .terminal = {.protocol = GRAPHICS_KITTY, .cell_width = 8, .cell_height = 16}};
            animation *turned = NULL;
            assert(image_decode_file(&photo, &cover, &turned, NULL, NULL));
            assert(turned && turned->count == 1);
            assert(turned->source_width == width && turned->source_height == height);
            int w = width / divisor, h = height / divisor, inset = 4 / divisor;
            assert(turned->width == w && turned->height == h);
            const char corners[] = {quadrant(turned, inset, inset),
                                    quadrant(turned, w - 1 - inset, inset),
                                    quadrant(turned, inset, h - 1 - inset),
                                    quadrant(turned, w - 1 - inset, h - 1 - inset), 0};
            assert(!strcmp(corners, upright[orientation - 1]));
            animation_free(turned);
        }
        /* The encoded image and its layout size follow the upright picture. */
        image_decode_request shown = {
            .path = argv[13 + orientation],
            .width = 640,
            .height = 360,
            .native = true,
            .image_id = 42,
            .terminal = {.protocol = GRAPHICS_SIXEL, .cell_width = 8, .cell_height = 16}};
        animation *none = NULL;
        assert(image_decode_file(&shown, &cover, &none, NULL, NULL));
        assert(cover.width == width && cover.height == height && !none);
        assert(cover.source_width == width && cover.source_height == height);
        graphics_image_free(&cover);
    }
    puts("GIF frame pixels/delays, native dimensions, proportional limits, crops, cancellation "
         "and EXIF orientation verified");
    return 0;
}
