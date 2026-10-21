#ifndef PAPO_MEDIA_STREAM_H
#define PAPO_MEDIA_STREAM_H

#include <stdbool.h>
#include <stddef.h>

/* Loopback HTTP bridge for media that is still being downloaded.
 *
 * The media owner keeps using the authenticated HTTP client (cookies, TLS
 * policy and the global request budget); this server only republishes the
 * bytes already written to the private temporary file to the local player.
 * The URL carries an unguessable token, the listener is bound to 127.0.0.1
 * and no credential crosses it. Files that can be parsed from the beginning
 * are streamed sequentially while they grow; MP4 files whose index is stored
 * at the end keep byte ranges and wait for the missing tail. Once complete,
 * every file serves ranges. */
typedef struct media_stream_server media_stream_server;

media_stream_server *media_stream_server_create(void);
void media_stream_server_destroy(media_stream_server *server);
/* Publishes `path` (which may still be empty) and returns its loopback URL.
 * The returned string is borrowed until withdraw or destroy. */
const char *media_stream_publish(media_stream_server *server, const char *path, size_t total);
/* Borrowed URL of a published path, or NULL when it is no longer served. */
const char *media_stream_url(const media_stream_server *server, const char *path);
/* Bytes already written and the expected total (zero while unknown). */
void media_stream_progress(media_stream_server *server, const char *path, size_t available,
                           size_t total);
/* No more bytes will arrive; the complete body is available. A failed
 * transfer is withdrawn instead, which closes its responses early. */
void media_stream_finish(media_stream_server *server, const char *path);
void media_stream_withdraw(media_stream_server *server, const char *path);

#endif
