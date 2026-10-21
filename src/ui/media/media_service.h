#ifndef PAPO_MEDIA_SERVICE_H
#define PAPO_MEDIA_SERVICE_H
#include "ui/media/media_backend.h"
/* Stop is nonblocking and cancels outstanding RPCs before retiring the child.
 * Expensive calls below run on existing media workers, never the TUI thread. */
void media_service_stop(void);
const char *media_service_error(void);
bool media_service_iterm2(void);
bool media_service_glyph(const char *, const image_decode_request *, graphics_image *);
/* Update, render and encode in the helper. Only the encoded frame crosses IPC.
 * Success with an empty image means no new frame; info is still refreshed. */
bool media_service_video_frame(void *player, const video_controls *, const image_decode_request *,
                               bool force, video_info *, graphics_image *);
#endif
