#ifndef PAPO_MEDIA_CACHE_H
#define PAPO_MEDIA_CACHE_H
#include "net/http/download.h"
#include "ui/media/image_decode.h"
#define MEDIA_CACHE_MEMORY (256U * 1024U * 1024U)
#define MEDIA_CACHE_FILES (128U * 1024U * 1024U)
#define MEDIA_CACHE_QUEUE 32U
#define MEDIA_CACHE_DOWNLOADS 4
#define MEDIA_CACHE_WORKERS 4

typedef struct media_cache media_cache;
typedef struct media_resource media_resource;
typedef struct {
    size_t memory, reserved, files, file_reserved, objects, references, jobs, downloads;
    unsigned workers, peak_workers, queued;
    uint64_t requests, decodes, encodes, deduplicated;
    uint64_t trim_attempts, trims;
    size_t reclaim_pending;
} media_cache_stats;
/* All calls and reference changes are on the main thread. A cache is scoped to
 * one auth client/generation; all caches share the process budget and pool.
 * Release all resource handles before destroy; destroy before auth. Reset
 * invalidates associations: old handles may only be released afterwards. Workers own immutable
 * input and never use auth/curses. */
media_cache *media_cache_create(auth_client *auth);
void media_cache_destroy(media_cache *cache);
void media_cache_invalidate(media_cache *cache, auth_media_kind kind, const char *id);
void media_cache_reset(media_cache *cache);
/* Owned resource reference. Embed roles share the same resource/HTTP request. */
media_resource *media_cache_acquire(media_cache *cache, auth_media_kind kind, const char *id,
                                    uint64_t revision, bool video);
/* Other image sources enter the same hash, decode, variant and worker cache.
 * Path strings are copied; input files stay immutable until ingest completes. */
media_resource *media_cache_acquire_url(media_cache *cache, const char *url, uint64_t revision);
media_resource *media_cache_acquire_file(media_cache *cache, const char *path, uint64_t revision);
media_resource *media_cache_acquire_unicode(media_cache *cache, const char *text);
/* A presentation keeps one balanced visible lease per occurrence. */
void media_resource_hold(media_resource *resource, bool visible);
/* Reuse avatars across scrolling/idle periods. This skips age-based expiry;
 * global memory/file budgets and resource limits still apply. */
void media_resource_cache_for_session(media_resource *resource);
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
/* A compatible cached variant may cover a background change while its
 * replacement is prepared. The returned variant is pinned for this frame. */
const graphics_image *media_resource_fallback_image(media_resource *resource,
                                                    image_decode_request request);
/* One floating panel per composed frame. Background occurrences retain their
 * uncovered rectangles; panel icons bypass the crop. Worker/cache ownership
 * and budgets are identical to ordinary variants. Fragment IDs belong to the caller. */
void media_cache_overlay(media_cache *cache, int y, int x, int rows, int columns);
void media_resource_place_image(media_resource *resource, bool author, image_decode_request request,
                                const graphics_image *image, uint32_t id, uint32_t fragments[4],
                                int y, int x, int top, int bottom, bool overlay);
const char *media_resource_path(const media_resource *resource);
const char *media_resource_error(const media_resource *resource, bool author);
bool media_resource_dimensions(const media_resource *resource, bool author, int *width,
                               int *height);
bool media_resource_animated(const media_resource *resource, bool author);
/* Borrowed identity for diagnostics/tests; identical bytes have this same pointer. */
const void *media_resource_object(const media_resource *resource, bool author);
/* Main-thread reservations also cover the video worker/publication buffers. */
bool media_cache_reserve(size_t bytes);
void media_cache_unreserve(size_t bytes);
void media_cache_get_stats(media_cache_stats *stats);
#endif
