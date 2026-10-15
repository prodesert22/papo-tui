#ifndef PAPO_IMAGE_DECODE_H
#define PAPO_IMAGE_DECODE_H
#include "ui/graphics.h"
#include <stdatomic.h>
/* Shared decoded frames have immutable pixels; each worker job owns a reference.
 * Decoding is bounded to 256 frames / 32 MiB per animation. */
#define ANIMATION_BYTES (32U * 1024U * 1024U)
#define ANIMATION_FRAMES 256
typedef struct {
    atomic_uint references;
    unsigned char *pixels[ANIMATION_FRAMES];
    uint32_t delay[ANIMATION_FRAMES];
    size_t count, bytes;
    int width, height, source_width, source_height;
    uint64_t duration;
} animation;
typedef struct {
    const char *path;
    int width, height;
    graphics_terminal terminal;
    uint32_t image_id;
    bool preview, native;
    animation *animation;
    size_t frame;
    int top, bottom;
    bool matte, center, first_frame;
    uint32_t background;
    int max_side;
    /* Optional content size limit inside the output canvas; zero uses it all. */
    int fit_side;
} image_decode_request;
void animation_ref(animation *frames);
void animation_free(animation *frames);
size_t animation_frame_at(const animation *frames, uint64_t epoch, uint64_t now);
bool image_encode_frame(const image_decode_request *request, graphics_image *image);
/* Return owned graphics and, for multi-frame GIFs, an owned frame reference.
 * FFmpeg performs GIF composition/disposal and supplies presentation delays. */
bool image_decode_file(const image_decode_request *request, graphics_image *image, animation **frames,
                       bool (*cancelled)(void *context), void *context);
#endif
