#define _POSIX_C_SOURCE 200809L
#include "ui/image_decode.h"
#include <assert.h>
#include <stdio.h>
static bool cancelled(void *context) { (void)context; return true; }
int main(int argc, char **argv)
{
    assert(argc == 9);
    image_decode_request request = {.width = 640, .height = 360, .native = true, .preview = true,
        .image_id = 42, .terminal = {.protocol = GRAPHICS_SIXEL, .cell_width = 8, .cell_height = 16}};
    for (int mode = GRAPHICS_SIXEL; mode <= GRAPHICS_ITERM2; ++mode) {
        request.terminal.protocol = (graphics_protocol)mode;
        graphics_image image = {0}; animation *frames = NULL;
        request.path = argv[1];
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 16 && image.height == 12 && !frames);
        assert(image.source_width == 16 && image.source_height == 12);
        graphics_image_free(&image);
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
        request.animation = frames; request.frame = 1;
        uint64_t first_revision = image.revision;
        graphics_image_free(&image);
        assert(image_encode_frame(&request, &image));
        assert(image.id == 42 && image.revision != first_revision && image.width == 16);
        graphics_image_free(&image);
        request.frame = 2; assert(!image_encode_frame(&request, &image));
        animation_ref(frames); animation_free(frames); animation_free(frames);
        request.animation = NULL; frames = NULL; request.frame = 0;
        request.path = argv[3];
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 640 && image.height == 320 && !frames);
        assert(image.source_width == 1000 && image.source_height == 500);
        graphics_image_free(&image);
        request.path = argv[4];
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 128 && image.height == 160 && !frames);
        graphics_image_free(&image);
        request.top = 32; request.bottom = 16;
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.height == (mode == GRAPHICS_KITTY ? 160 : 112));
        assert(image.source_height == 160);
        graphics_image_free(&image); request.top = request.bottom = 0;
        assert(!image_decode_file(&request, &image, &frames, cancelled, NULL));
        assert(!frames && !image.data);
        request.path = argv[6];
        assert(!image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(!frames && !image.data);
        request.width = 1600; request.height = 1000; request.path = argv[3];
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 1000 && image.height == 500 && !frames);
        graphics_image_free(&image);
        request.path = argv[7]; request.width = 1200; request.height = 900;
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 600 && image.height == 900 && !frames);
        assert(image.source_width == 1000 && image.source_height == 1500);
        graphics_image_free(&image);
        request.path = argv[8]; request.width = 960; request.height = 720; request.retain_static = true;
        assert(image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(image.width == 960 && image.height == 720);
        assert(frames && frames->count == 1 && frames->bytes == image_frame_bytes(960, 720));
        assert(image.length <= graphics_image_capacity(image.width, image.height));
        animation_free(frames); frames = NULL; graphics_image_free(&image);
        request.width = request.height = 100000;
        assert(!image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(!frames && !image.data);
        request.width = 640; request.height = 360; request.retain_static = false;
        request.path = argv[5];
        assert(!image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(!frames && !image.data);
        image_decode_request avatar = request;
        avatar.path = argv[2]; avatar.width = avatar.height = 48;
        avatar.native = false; avatar.center = avatar.matte = true;
        avatar.cover = true; avatar.background = 0x15181e;
        assert(image_decode_file(&avatar, &image, &frames, NULL, NULL));
        assert(frames && frames->width == 48 && frames->height == 48);
        for (size_t f = 0; f < frames->count; ++f)
            for (int y = 0; y < 48; ++y) for (int x = 0; x < 48; ++x) {
                const unsigned char *pixel = frames->pixels[f] + (y * 48 + x) * 3;
                assert(pixel[f ? 2 : 0] > 230);
            }
        graphics_image_free(&image); animation_free(frames); frames = NULL;
    }
    puts("GIF frame pixels/delays, native dimensions, proportional limits, crops and cancellation verified");
    return 0;
}
