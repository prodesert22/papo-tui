#include "ui/media/media_layout.h"
#include <string.h>
bool media_modal_geometry(int columns, int rows, bool info, int image_columns, int image_rows,
                          media_modal_layout *out)
{
    memset(out, 0, sizeof(*out));
    if (columns < 30 || rows < 12)
        return false;
    int sidebar = columns >= 80 ? 26 : 0;
    int margin = (columns - sidebar) / 12;
    if (margin < 4)
        margin = 4;
    if (margin > 8)
        margin = 8;
    out->x = sidebar + margin;
    out->width = columns - sidebar - 2 * margin;
    out->y = rows / 8;
    if (out->y < 2)
        out->y = 2;
    if (out->y > 5)
        out->y = 5;
    out->height = rows - 2 * out->y;
    out->area_x = out->x + 3;
    out->area_y = out->y + 3;
    out->area_width = out->width - 6;
    out->area_height = out->height - 7;
    if (info) {
        out->info_width = out->width / 3;
        if (out->info_width < 18)
            out->info_width = 18;
        if (out->info_width > 30)
            out->info_width = 30;
        if (out->info_width > out->area_width - 9)
            out->info_width = out->area_width - 9;
        out->area_width -= out->info_width + 1;
        out->info_x = out->x + out->width - 3 - out->info_width;
    }
    /* Keep the toggle in the header so opening the pane never moves its hit box. */
    out->button_width = 6;
    out->button_x = out->x + 2;
    out->button_y = out->y + 1;
    out->image_x = out->area_x;
    out->image_y = out->area_y;
    if (image_columns > 0 && image_columns <= out->area_width)
        out->image_x += (out->area_width - image_columns) / 2;
    if (image_rows > 0 && image_rows <= out->area_height)
        out->image_y += (out->area_height - image_rows) / 2;
    return out->area_width > 0 && out->area_height > 0;
}
bool media_modal_contains(const media_modal_layout *layout, int y, int x)
{
    return x >= layout->x && x < layout->x + layout->width && y >= layout->y &&
           y < layout->y + layout->height;
}

bool media_controls_geometry(int row, int x, int width, media_controls_layout *out)
{
    memset(out, 0, sizeof(*out));
    out->volume_label_x = out->percent_x = -1;
    if (row < 0 || x < 0 || width < 9)
        return false;
    out->row = row;
    out->play_x = x;
    out->play_width = width >= 20 ? 3 : 1;
    bool label = width >= 20, percent = width >= 28;
    int tracks = width - out->play_width - 6 - (label ? 1 : 0) - (percent ? 5 : 0);
    out->volume_width = tracks / 3;
    if (out->volume_width < 1)
        out->volume_width = 1;
    out->timeline_width = tracks - out->volume_width;
    out->timeline_x = x + out->play_width + 2;
    int next = out->timeline_x + out->timeline_width + 2;
    if (label)
        out->volume_label_x = next++;
    out->volume_x = next + 1;
    if (percent)
        out->percent_x = out->volume_x + out->volume_width + 2;
    return true;
}

static double track_fraction(int x, int left, int width)
{
    if (x <= left)
        return 0;
    if (x >= left + width - 1)
        return 1;
    return (double)(x - left) / (width - 1);
}

media_control media_controls_hit(const media_controls_layout *layout, int y, int x,
                                 double *fraction)
{
    if (!layout || !fraction || layout->timeline_width < 1 || layout->volume_width < 1)
        return MEDIA_CONTROL_NONE;
    *fraction = 0;
    if (y != layout->row)
        return MEDIA_CONTROL_NONE;
    if (x >= layout->play_x && x < layout->play_x + layout->play_width)
        return MEDIA_CONTROL_PLAY;
    if (x >= layout->timeline_x - 1 && x <= layout->timeline_x + layout->timeline_width) {
        *fraction = track_fraction(x, layout->timeline_x, layout->timeline_width);
        return MEDIA_CONTROL_SEEK;
    }
    if (x >= layout->volume_x - 1 && x <= layout->volume_x + layout->volume_width) {
        *fraction = track_fraction(x, layout->volume_x, layout->volume_width);
        return MEDIA_CONTROL_VOLUME;
    }
    return MEDIA_CONTROL_NONE;
}
