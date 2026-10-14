#define _POSIX_C_SOURCE 200809L
#include "ui/image_decode.h"
#include <libavutil/log.h>
#include <assert.h>
#include <stdio.h>
static bool cancelled(void *context) { (void)context; return true; }
int main(int argc, char **argv)
{
    assert(argc == 7);
    av_log_set_level(AV_LOG_QUIET);
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
        request.path = argv[5];
        assert(!image_decode_file(&request, &image, &frames, NULL, NULL));
        assert(!frames && !image.data);
    }
    puts("GIF frame pixels/delays, native dimensions, proportional limits, crops and cancellation verified");
    return 0;
}
