#ifndef PAPO_MEDIA_DOWNLOAD_H
#define PAPO_MEDIA_DOWNLOAD_H
#include "net/auth/client.h"

#define MEDIA_AVATAR_LIMIT (16U * 1024U * 1024U)
#define MEDIA_IMAGE_LIMIT (32U * 1024U * 1024U)
#define MEDIA_VIDEO_LIMIT (256U * 1024U * 1024U)
#define MEDIA_EMBED_LIMIT (48U * 1024U * 1024U)
typedef enum {
    MEDIA_IDLE,
    MEDIA_WAITING,
    MEDIA_DOWNLOADING,
    MEDIA_READY,
    MEDIA_FAILED
} media_state;
typedef struct media_download media_download;
/* Borrows auth; owns one independent transfer and a private temporary
 * directory. Poll after auth_poll on the main thread. No HTTP in workers. */
media_download *media_download_create(auth_client *auth);
void media_download_destroy(media_download *m);
bool media_download_request(media_download *m, auth_media_kind kind, const char *id, bool video);
/* Public avatar URL, bounded to 16 MiB; same asynchronous global rate budget. */
bool media_download_avatar_request(media_download *m, const char *reference);
bool media_download_poll(media_download *m);
void media_download_cancel(media_download *m);
media_state media_download_state(const media_download *m);
size_t media_download_bytes(const media_download *m);
const char *media_download_error(const media_download *m);
const char *media_download_mime(const media_download *m);
uint64_t media_download_generation(const media_download *m);
long media_download_http_status(const media_download *m);
/* READY only: transfers the owned path. Caller must unlink/free it before
 * destroying the download context (which removes the containing directory). */
char *media_download_take_file(media_download *m);
/* Creates another private file in the same context, for decoded embed blobs.
 * Caller owns its fd and path, with the same unlink/free obligation. */
int media_download_temp(media_download *m, char **path);
#endif
