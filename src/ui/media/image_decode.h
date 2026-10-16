#ifndef PAPO_IMAGE_DECODE_H
#define PAPO_IMAGE_DECODE_H
#include "ui/media/graphics.h"
#include <stdatomic.h>
/* Shared decoded frames have immutable pixels; each worker job owns a reference.
 * Animations are bounded to 256 frames / 32 MiB; larger rasters retain only
 * their first frame, within the input pixel limit. */
#define ANIMATION_BYTES (32U * 1024U * 1024U)
#define ANIMATION_FRAMES 256
/* libswscale SIMD readers may fetch past the last RGB pixel. This zeroed tail
 * belongs to each allocation and is included in decoded cache accounting. */
#define IMAGE_PIXEL_PADDING 64U
static inline size_t image_frame_bytes(int width, int height)
{
    return (size_t)width * (size_t)height * 3U + IMAGE_PIXEL_PADDING;
}
typedef struct {
    atomic_uint references;
    unsigned char *pixels[ANIMATION_FRAMES];
    uint32_t delay[ANIMATION_FRAMES];
    bool rgba[ANIMATION_FRAMES]; /* Preserve alpha in canonical transparent frames. */
    size_t count, bytes;
    int width, height, source_width, source_height;
    uint64_t duration;
} animation;
typedef struct {
    const char *path;
    int width, height;
    graphics_terminal terminal;
    uint32_t image_id;
    bool preview, native, video_poster;
    bool decode_only, retain_static, static_only;
    animation *animation;
    size_t frame;
    int top, bottom, left, right;
    bool region; /* Crop the encoded raster on every protocol, including Kitty. */
    bool matte, center, first_frame;
    uint32_t background;
    int max_side;
    /* Fill the output with a proportional center crop instead of letterboxing. */
    bool cover;
    bool notification_badge; /* Centered red badge; glyph text is its optional count. */
} image_decode_request;
void animation_ref(animation *frames);
void animation_free(animation *frames);
size_t animation_frame_at(const animation *frames, uint64_t epoch, uint64_t now);
bool image_encode_frame(const image_decode_request *request, graphics_image *image);
/* decode_only preserves alpha in canonical frames for shared variants.
 * Return owned graphics and, for animations, an owned frame reference.
 * FFmpeg performs composition/disposal and supplies presentation delays. */
bool image_decode_file(const image_decode_request *request, graphics_image *image,
                       animation **frames, bool (*cancelled)(void *context), void *context);
#endif
