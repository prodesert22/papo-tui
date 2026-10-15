#ifndef PAPO_MEDIA_CACHE_H
#define PAPO_MEDIA_CACHE_H
#include "net/media.h"
#include "ui/image_decode.h"
#define MEDIA_CACHE_MEMORY (256U * 1024U * 1024U)
#define MEDIA_CACHE_FILES (128U * 1024U * 1024U)
#define MEDIA_CACHE_QUEUE 32U
#define MEDIA_CACHE_DOWNLOADS 4

typedef struct media_cache media_cache;
typedef struct media_resource media_resource;
typedef struct {
    size_t memory, reserved, files, file_reserved, objects, references, jobs, downloads;
    unsigned workers, peak_workers, queued;
    uint64_t requests, decodes, encodes, deduplicated;
} media_cache_stats;
/* All calls and reference changes are on the main thread. A cache is scoped to
 * one auth client/generation; all caches share the process budget and pool.
 * Release all resource handles before destroy; destroy before auth. Reset
 * invalidates associations: old handles may only be released afterwards. Workers own immutable input and never use auth/curses. */
media_cache *media_cache_create(auth_client *auth);
void media_cache_destroy(media_cache *cache);
void media_cache_invalidate(media_cache *cache, auth_media_kind kind, const char *id);
void media_cache_reset(media_cache *cache);
/* Owned resource reference. Embed roles share the same resource/HTTP request. */
media_resource *media_cache_acquire(media_cache *cache, auth_media_kind kind,
                                   const char *id, uint64_t revision, bool video);
void media_resource_ref(media_resource *resource);
void media_resource_keep_image(media_resource *resource, bool author, const graphics_image *image);
void media_resource_retry(media_resource *resource);
void media_resource_release(media_resource *resource);
void media_cache_begin(media_cache *cache);
void media_resource_use_image(media_resource *resource, bool author, bool modal);
void media_resource_use(media_resource *resource, bool modal);
bool media_cache_poll(media_cache *cache, uint64_t now);
/* Borrowed until the next poll. Missing representations are queued once;
 * admission failure defers them until capacity becomes available. */
const graphics_image *media_resource_image(media_resource *resource, bool author,
                                           image_decode_request request, uint64_t now);
const char *media_resource_path(const media_resource *resource);
const char *media_resource_error(const media_resource *resource, bool author);
bool media_resource_dimensions(const media_resource *resource, bool author, int *width, int *height);
bool media_resource_animated(const media_resource *resource, bool author);
/* Borrowed identity for diagnostics/tests; identical bytes have this same pointer. */
const void *media_resource_object(const media_resource *resource, bool author);
/* Main-thread reservations also cover the video worker/publication buffers. */
bool media_cache_reserve(size_t bytes);
void media_cache_unreserve(size_t bytes);
void media_cache_get_stats(media_cache_stats *stats);
#endif
