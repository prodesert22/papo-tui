#ifndef PAPO_MEDIA_VIEW_H
#define PAPO_MEDIA_VIEW_H
#include "ui/message_blocks.h"
#include "model/history.h"
#include <signal.h>
typedef struct media_view media_view;
/* Owns cache, downloader, worker, modal and player. Borrows auth. Must be
 * destroyed before the authentication client. Worker never touches curses,
 * cookies or HTTP. Main thread owns every terminal write. */
media_view *media_view_create(auth_client *auth);
void media_view_destroy(media_view *v);
void media_view_reset(media_view *v);
bool media_view_poll(media_view *v, history *h);
void media_view_begin(media_view *v);
/* y/x are absolute cells; top/bottom clipping is measured in image rows. */
void media_view_offer(media_view *v, const message_media *item, int y, int x,
                      int width, int height, int crop_top, int crop_bottom);
void media_view_draw(media_view *v);
void media_view_flush(media_view *v);
bool media_view_modal(const media_view *v);
bool media_view_key(media_view *v, int type, wint_t key);
bool media_view_click(media_view *v, int y, int x);
#endif
