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

/* Local window coordinates, shared by painting and mouse hit testing. */
typedef struct {
    int row, play_x, play_width;
    int timeline_x, timeline_width;
    int volume_label_x, volume_x, volume_width;
    int percent_x;
} media_controls_layout;
typedef enum {
    MEDIA_CONTROL_NONE,
    MEDIA_CONTROL_PLAY,
    MEDIA_CONTROL_SEEK,
    MEDIA_CONTROL_VOLUME
} media_control;
bool media_controls_geometry(int row, int x, int width, media_controls_layout *out);
media_control media_controls_hit(const media_controls_layout *layout, int y, int x,
                                 double *fraction);

/* Audio option row, in local window coordinates: "[−] 1.25× [+]", then the
 * loop toggle and the stop button when the width allows. Absent buttons
 * have zero width. */
#define MEDIA_AUDIO_SPEED_WIDTH 5
#define MEDIA_AUDIO_LOOP_WIDTH 14
#define MEDIA_AUDIO_STOP_WIDTH 7
typedef struct {
    int row, slower_x, speed_x, faster_x;
    int loop_x, loop_width, stop_x, stop_width;
} media_audio_layout;
typedef enum {
    MEDIA_AUDIO_NONE,
    MEDIA_AUDIO_SLOWER,
    MEDIA_AUDIO_FASTER,
    MEDIA_AUDIO_LOOP,
    MEDIA_AUDIO_STOP
} media_audio_control;
bool media_audio_geometry(int row, int x, int width, media_audio_layout *out);
media_audio_control media_audio_hit(const media_audio_layout *layout, int y, int x);
#endif
