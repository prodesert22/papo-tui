#ifndef PAPO_SIXEL_H
#define PAPO_SIXEL_H
#include <stdbool.h>
#include <stddef.h>
typedef struct { bool supported; int cell_width, cell_height; } sixel_terminal;
typedef struct { char *data; size_t length; } sixel_image;
/* A complete reply parser shared by the probe and capability tests. */
bool sixel_terminal_reply(sixel_terminal *term, const char *reply);
/* RGB888 input is borrowed. Output is owned and must be free'd. Worker-safe
 * with independent contexts. Geometry is bounded to 640x360 pixels. */
bool sixel_image_encode(const unsigned char *pixels, int width, int height, sixel_image *out);
void sixel_image_free(sixel_image *out);
#endif
