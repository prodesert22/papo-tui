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
void media_view_measure(void *context, const message_media *item, int available_width, int *width,
                        int *height);
/* History viewport in cells; invalidates measured image heights on changes. */
void media_view_viewport(media_view *v, int rows);
void media_view_frame(media_view *v);
void media_view_begin(media_view *v);
/* y/x are absolute cells; top/bottom clipping is measured in image rows. */
void media_view_offer(media_view *v, const message_media *item, int y, int x, int width, int height,
                      int crop_top, int crop_bottom);
void media_view_draw(media_view *v);
void media_view_prepare(media_view *v);
bool media_view_modal(const media_view *v);
/* Opens a message-owned item directly; embed video covers open their player.
 * Image arrows use metadata from the message in the last polled history. */
bool media_view_open(media_view *v, const message_media *item);
bool media_view_key(media_view *v, int type, uint32_t key);
bool media_view_scroll(media_view *v, int y, int x, int delta);
bool media_view_click(media_view *v, int y, int x);
#endif
