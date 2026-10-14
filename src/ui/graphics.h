#ifndef PAPO_GRAPHICS_H
#define PAPO_GRAPHICS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ncurses.h>

typedef enum { GRAPHICS_TEXT, GRAPHICS_SIXEL, GRAPHICS_KITTY, GRAPHICS_ITERM2 } graphics_protocol;
typedef struct {
    graphics_protocol protocol;
    bool supported;
    int cell_width, cell_height;
    bool synchronized;
} graphics_terminal;
typedef struct graphics_image {
    char *data;
    size_t length;
    graphics_protocol protocol;
    uint32_t id;
    uint64_t revision;
    int width, height, cell_width, cell_height;
    int columns, rows;
    struct graphics_image *crops;
    size_t crop_count;
    int crop_top, crop_bottom;
} graphics_image;

#define GRAPHICS_PROBE_ID 1347440639U
/* Only the main thread probes, consumes replies or writes the terminal. */
void graphics_terminal_probe(void);
int graphics_terminal_input(wint_t *key);
graphics_terminal graphics_terminal_get(void);
bool graphics_terminal_reply(graphics_terminal *terminal, const char *reply);
/* Borrow RGB888 pixels; return owned output. A worker uses a terminal snapshot,
 * independent library contexts and no terminal, environment or network I/O. */
bool graphics_image_encode(const unsigned char *pixels, int width, int height,
                           graphics_terminal terminal, graphics_image *image);
uint32_t graphics_image_next_id(void);
bool graphics_image_encode_id(const unsigned char *pixels, int width, int height,
                              graphics_terminal terminal, uint32_t id, graphics_image *image);
/* Pre-encode the small inline preview crops in the worker. */
bool graphics_image_encode_preview(const unsigned char *pixels, int width, int height,
                                   graphics_terminal terminal, uint32_t id, graphics_image *image);
const graphics_image *graphics_image_crop(const graphics_image *image, int top, int bottom);
size_t graphics_image_bytes(const graphics_image *image);
void graphics_frame_begin(void);
/* A terminal resize/full layout rebuild can discard graphics independently
 * of curses' cell cache. Retain ownership, but force one fresh presentation. */
void graphics_frame_invalidate(void);
void graphics_image_place(const graphics_image *image, int y, int x, int top, int bottom);
/* Prepare after window composition, before doupdate; flush afterwards. */
void graphics_frame_prepare(void);
void graphics_frame_flush(void);
void graphics_update_begin(void);
void graphics_update_end(void);
void graphics_image_free(graphics_image *image);
/* Delete only this application's Kitty IDs and erase its iTerm2 image cells.
 * Call when disposing of media; normal redraws retain existing placements. */
void graphics_images_clear(void);
#endif
