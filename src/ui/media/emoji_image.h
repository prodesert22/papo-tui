#ifndef PAPO_EMOJI_IMAGE_H
#define PAPO_EMOJI_IMAGE_H
#include "ui/media/graphics.h"
/* Called by the shared image pool; font maps are local to each worker. */
bool emoji_image_encode(const char *text, int width, int height, uint32_t background,
                        graphics_terminal terminal, uint32_t id, graphics_image *image);
bool emoji_image_available(void);
#endif
