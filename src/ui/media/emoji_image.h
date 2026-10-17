#ifndef PAPO_EMOJI_IMAGE_H
#define PAPO_EMOJI_IMAGE_H
#include "ui/media/graphics.h"
#include "ui/media/image_decode.h"
bool emoji_image_encode_request(const char *text, const image_decode_request *request,
                                graphics_image *image);
/* Called by the shared image pool; font maps are local to each worker. */
bool emoji_image_encode(const char *text, int width, int height, uint32_t background,
                        graphics_terminal terminal, uint32_t id, graphics_image *image);
bool emoji_image_encode_region(const char *text, int width, int height, uint32_t background,
                               graphics_terminal terminal, uint32_t id, graphics_image *image,
                               int top, int bottom, int left, int right);
bool emoji_image_available(void);
#endif
