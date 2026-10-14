#ifndef PAPO_MEDIA_LAYOUT_H
#define PAPO_MEDIA_LAYOUT_H
#include <stdbool.h>
/* Absolute terminal cells. The viewer leaves the wide channel column exposed.
 * Image coordinates center the actual raster's cell bounds in its own area. */
typedef struct {
    int x, y, width, height;
    int area_x, area_y, area_width, area_height;
    int image_x, image_y;
    int info_x, info_width;
    int button_x, button_y, button_width;
} media_modal_layout;
bool media_modal_geometry(int columns, int rows, bool info, int image_columns, int image_rows,
                          media_modal_layout *out);
bool media_modal_contains(const media_modal_layout *layout, int y, int x);
#endif
