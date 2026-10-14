#include "ui/media_layout.h"
#include <string.h>
bool media_modal_geometry(int columns, int rows, bool info, int image_columns, int image_rows,
                          media_modal_layout *out)
{
    memset(out, 0, sizeof(*out));
    if (columns < 30 || rows < 12) return false;
    int sidebar = columns >= 80 ? 26 : 0;
    int margin = (columns - sidebar) / 12;
    if (margin < 4) margin = 4;
    if (margin > 8) margin = 8;
    out->x = sidebar + margin; out->width = columns - sidebar - 2 * margin;
    out->y = rows / 8;
    if (out->y < 2) out->y = 2;
    if (out->y > 5) out->y = 5;
    out->height = rows - 2 * out->y;
    out->area_x = out->x + 3; out->area_y = out->y + 3;
    out->area_width = out->width - 6; out->area_height = out->height - 7;
    if (info) {
        out->info_width = out->width / 3;
        if (out->info_width < 18) out->info_width = 18;
        if (out->info_width > 30) out->info_width = 30;
        if (out->info_width > out->area_width - 9) out->info_width = out->area_width - 9;
        out->area_width -= out->info_width + 1;
        out->info_x = out->x + out->width - 3 - out->info_width;
    }
    /* Keep the toggle in the header so opening the pane never moves its hit box. */
    out->button_width = 6;
    out->button_x = out->x + 2;
    out->button_y = out->y + 1;
    out->image_x = out->area_x; out->image_y = out->area_y;
    if (image_columns > 0 && image_columns <= out->area_width)
        out->image_x += (out->area_width - image_columns) / 2;
    if (image_rows > 0 && image_rows <= out->area_height)
        out->image_y += (out->area_height - image_rows) / 2;
    return out->area_width > 0 && out->area_height > 0;
}
bool media_modal_contains(const media_modal_layout *layout, int y, int x)
{
    return x >= layout->x && x < layout->x + layout->width &&
           y >= layout->y && y < layout->y + layout->height;
}
