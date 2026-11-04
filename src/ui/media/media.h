#include <stdint.h>
#ifndef PAPO_MEDIA_VIEW_H
#define PAPO_MEDIA_VIEW_H
#include "model/conversation/history.h"
#include "ui/media/message_blocks.h"
#include <signal.h>
typedef struct media_view media_view;
typedef struct media_cache media_cache;
media_cache *media_view_cache(media_view *view);
/* Owns cache, downloader, worker, modal and player. Borrows auth. Must be
 * destroyed before the authentication client. Worker never touches curses,
 * cookies or HTTP. Main thread owns every terminal write. */
media_view *media_view_create(auth_client *auth);
void media_view_destroy(media_view *v);
uint64_t media_view_dimensions_revision(const media_view *v);
void media_view_update(media_view *v, const json_t *event);
void media_view_idle(media_view *v);
void media_view_suspend(media_view *v);
void media_view_reset(media_view *v);
bool media_view_poll(media_view *v, history *h);
/* Milliseconds to wait for input: keep animation responsive without waking an idle view. */
int media_view_input_delay(media_view *v);
void media_view_measure(void *context, const message_media *item, int available_width, int *width,
                        int *height);
/* History viewport in cells; invalidates measured image heights on changes. */
void media_view_viewport(media_view *v, int rows);
void media_view_frame(media_view *v);
void media_view_begin(media_view *v);
/* y/x are absolute cells; top/bottom clipping is measured in image rows.
 * background is the RGB around the item (0xRRGGBB): transparent pixels blend
 * into it, and a change re-encodes the item from its cached pixels. */
void media_view_offer(media_view *v, const message_media *item, int y, int x, int width, int height,
                      int crop_top, int crop_bottom, uint32_t background);
void media_view_draw(media_view *v);
/* Inline player bar over an audio attachment row (absolute cells). pair is the
 * row's base color: THEME_HISTORY_AREA, THEME_EMBED_AREA or THEME_PANEL. */
void media_view_audio_row(media_view *v, const message_media *item, int y, int x, int width,
                          int pair);
/* Paints the offered bars; the background audio shows its progress there. */
void media_view_draw_rows(media_view *v);
void media_view_prepare(media_view *v);
bool media_view_modal(const media_view *v);
typedef enum {
    MEDIA_CONTEXT_NONE,
    MEDIA_CONTEXT_LIST,
    MEDIA_CONTEXT_IMAGE,
    MEDIA_CONTEXT_VIDEO,
    MEDIA_CONTEXT_AUDIO,
    MEDIA_CONTEXT_LOADING,
    MEDIA_CONTEXT_ERROR
} media_context;
media_context media_view_context(const media_view *v);
/* Opens a message-owned item directly; embed video covers open their player.
 * Image arrows use metadata from the message in the last polled history. */
bool media_view_open(media_view *v, const message_media *item);
/* Audio left playing after its modal closed (Esc). It lasts while this
 * server's tab is shown: reset, a mode change or another player end it. */
bool media_view_audio(const media_view *v, char channel[37], char message[37], char created_at[64]);
void media_view_audio_stop(media_view *v);
/* Reopens the player modal on the running audio without restarting it. Uses
 * the last polled history, so the message should already be loaded. */
bool media_view_audio_resume(media_view *v);
/* Mini-player on a footer row, right aligned; hit regions are kept for clicks. */
void media_view_draw_audio(media_view *v, WINDOW *window, int row);
typedef enum { MEDIA_MINI_NONE, MEDIA_MINI_HANDLED, MEDIA_MINI_OPEN } media_mini_action;
/* Absolute cells. Play/pause and stop act at once; the rest asks to reopen. */
media_mini_action media_view_audio_click(media_view *v, int y, int x);
bool media_view_key(media_view *v, int type, uint32_t key);
bool media_view_scroll(media_view *v, int y, int x, int delta);
bool media_view_click(media_view *v, int y, int x);
#endif
