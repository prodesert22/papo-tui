#define _POSIX_C_SOURCE 200809L
#include "net/clock.h"
#include "ui/media/media_cache.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

static void pause_tick(void)
{
    struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}
/* Linker fault injection affects project allocations, not system libraries. */
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
char *__real_strdup(const char *text);
static atomic_bool worker_delay, fail_calloc, fail_strdup;
static atomic_uint blocked_workers;
static pthread_t main_thread;
#ifdef __GLIBC__
int __real_malloc_trim(size_t pad);
static atomic_uint trim_calls;
int __wrap_malloc_trim(size_t pad)
{
    assert(!pthread_equal(pthread_self(), main_thread));
    atomic_fetch_add(&trim_calls, 1);
    return __real_malloc_trim(pad);
}
#endif
void *__wrap_malloc(size_t size)
{
    if (atomic_load(&worker_delay) && !pthread_equal(pthread_self(), main_thread)) {
        atomic_fetch_add(&blocked_workers, 1);
        while (atomic_load(&worker_delay))
            pause_tick();
        atomic_fetch_sub(&blocked_workers, 1);
    }
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size)
{
    if (atomic_exchange(&fail_calloc, false))
        return NULL;
    if (atomic_load(&worker_delay) && !pthread_equal(pthread_self(), main_thread)) {
        atomic_fetch_add(&blocked_workers, 1);
        while (atomic_load(&worker_delay))
            pause_tick();
        atomic_fetch_sub(&blocked_workers, 1);
    }
    return __real_calloc(count, size);
}
char *__wrap_strdup(const char *text)
{
    if (atomic_exchange(&fail_strdup, false))
        return NULL;
    return __real_strdup(text);
}
static auth_client *login(const char *url)
{
    auth_client *a = auth_create();
    assert(a && auth_start(a, url, "josé", "Synthetic!123", ""));
    for (int i = 0; auth_busy(a) && i < 10000; ++i) {
        auth_poll(a);
        pause_tick();
    }
    assert(auth_has_session(a));
    return a;
}
static uint64_t now_ms(void)
{
    uint64_t now = 0;
    assert(net_now_ms(&now));
    return now;
}
static image_decode_request request(int width)
{
    return (image_decode_request){
        .width = width,
        .height = 360,
        .native = true,
        .terminal = {
            .protocol = GRAPHICS_KITTY, .supported = true, .cell_width = 8, .cell_height = 16}};
}
static void bounds(void)
{
    media_cache_stats s;
    media_cache_get_stats(&s);
    assert(s.memory + s.reserved <= MEDIA_CACHE_MEMORY);
    assert(s.files + s.file_reserved <= MEDIA_CACHE_FILES &&
           s.jobs <= MEDIA_CACHE_QUEUE + MEDIA_CACHE_WORKERS);
    assert(s.queued <= MEDIA_CACHE_QUEUE && s.workers <= MEDIA_CACHE_WORKERS &&
           s.peak_workers <= MEDIA_CACHE_WORKERS && s.downloads <= 4);
}
static size_t rss_kib(void)
{
    FILE *file = fopen("/proc/self/smaps_rollup", "r");
    size_t rss = 0;
    char line[256];
    if (file) {
        while (fgets(line, sizeof(line), file))
            if (sscanf(line, "Rss: %zu kB", &rss) == 1)
                break;
        fclose(file);
    }
    return rss;
}
static void pressure_test(auth_client *auth, media_cache *cache)
{
    media_resource *resources[12];
    for (int i = 0; i < 12; ++i) {
        char id[37];
        snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012x", 901 + i);
        resources[i] = media_cache_acquire(cache, AUTH_MEDIA_ATTACHMENT, id, 1, false);
        assert(resources[i]);
    }
    size_t peak = 0, least_animated = 12, baseline = rss_kib(), reclaimed_rss = 0;
    uint64_t epoch = now_ms();
    for (int cycle = 0; cycle < 3; ++cycle) {
        uint64_t base = epoch + (uint64_t)cycle * 400000, current = base;
        bool ready = false;
        for (int n = 0; n < 30000 && !ready; ++n) {
            current = base + (now_ms() - epoch);
            auth_poll(auth);
            media_cache_begin(cache);
            for (int i = 0; i < 12; ++i)
                media_resource_use(resources[i], false);
            media_cache_poll(cache, current);
            ready = true;
            for (int i = 0; i < 12; ++i) {
                const char *error = media_resource_error(resources[i], false);
                if (error)
                    fprintf(stderr, "pressure cycle=%d resource=%d tick=%d error=%s\n", cycle, i, n,
                            error);
                assert(!error);
                ready &= media_resource_image(resources[i], false, request(640), current) != NULL;
            }
            media_cache_stats stats;
            media_cache_get_stats(&stats);
            if (stats.memory + stats.reserved > peak)
                peak = stats.memory + stats.reserved;
            bounds();
            pause_tick();
        }
        assert(ready);
        media_cache_stats before_expiry;
        media_cache_get_stats(&before_expiry);
        size_t animated = 0;
        for (int i = 0; i < 12; ++i)
            animated += media_resource_animated(resources[i], false);
        if (animated < least_animated)
            least_animated = animated;
        assert(animated < 12); /* Visible GIFs degrade rather than exceed 256 MiB. */
        media_cache_stats stats;
        for (int n = 0; n < 5000; ++n) {
            media_cache_begin(cache);
            media_cache_poll(cache, current + 61000 + (uint64_t)n);
            media_cache_get_stats(&stats);
            if (!stats.jobs)
                break;
            pause_tick();
        }
        assert(!stats.jobs);
        media_cache_begin(cache);
        media_cache_poll(cache, current + 68000);
        media_cache_get_stats(&stats);
        assert(!stats.memory && !stats.reserved);
        media_cache_begin(cache);
        media_cache_poll(cache, current + 370000);
        media_cache_get_stats(&stats);
        assert(!stats.files && !stats.objects && !stats.file_reserved);
#ifdef __GLIBC__
        for (int n = 0; n < 12000; ++n) {
            media_cache_begin(cache);
            media_cache_poll(cache, current + 370000 + (uint64_t)n);
            media_cache_get_stats(&stats);
            if (stats.trim_attempts > before_expiry.trim_attempts && !stats.jobs &&
                stats.reclaim_pending < 16U * 1024U * 1024U)
                break;
            pause_tick();
        }
        assert(stats.trim_attempts > before_expiry.trim_attempts && !stats.jobs &&
               stats.reclaim_pending < 16U * 1024U * 1024U);
        assert(atomic_load(&trim_calls) == stats.trim_attempts);
        size_t rss = rss_kib();
        if (rss > reclaimed_rss)
            reclaimed_rss = rss;
#ifndef PAPO_TEST_SANITIZERS
        /* First use also loads codecs, which are outside the image budget. */
        if (baseline && rss > baseline + 64U * 1024U) {
            struct mallinfo2 info = mallinfo2();
            fprintf(
                stderr,
                "RSS baseline=%zu after=%zu attempts=%llu heap_used=%zu heap_free=%zu mmap=%zu\n",
                baseline, rss, (unsigned long long)stats.trim_attempts, info.uordblks,
                info.fordblks, info.hblkhd);
        }
        assert(!baseline || rss <= baseline + 64U * 1024U);
#endif
#endif
    }
    for (int i = 0; i < 12; ++i)
        media_resource_release(resources[i]);
    media_cache_stats final;
    media_cache_get_stats(&final);
    printf("{\"cycles\":3,\"peak_accounted_bytes\":%zu,\"least_animated\":%zu,\"released_each_"
           "cycle\":true,"
           "\"trim_attempts\":%llu,\"baseline_rss_kib\":%zu,\"maximum_rss_after_trim_kib\":%zu}\n",
           peak, least_animated, (unsigned long long) final.trim_attempts, baseline, reclaimed_rss);
}
static void resize_test(auth_client *auth, media_cache *cache, bool cancel_upgrade)
{
    media_resource *r[2];
    for (int i = 0; i < 2; ++i) {
        char id[37];
        snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012x", 901 + i);
        r[i] = media_cache_acquire(cache, AUTH_MEDIA_ATTACHMENT, id, 1, false);
        assert(r[i]);
    }
    image_decode_request small = request(160), large = request(1600);
    small.height = 80;
    large.height = 1000;
    const graphics_image *image = NULL;
    for (int n = 0; n < 10000; ++n) {
        auth_poll(auth);
        media_cache_begin(cache);
        for (int i = 0; i < 2; ++i)
            media_resource_use(r[i], false);
        media_cache_poll(cache, now_ms());
        image = media_resource_image(r[0], false, small, now_ms());
        const graphics_image *second = media_resource_image(r[1], false, small, now_ms());
        bounds();
        if (image && second) {
            assert(image == second);
            break;
        }
        pause_tick();
    }
    assert(image && image->width == 160 && image->height == 80);
    const void *identity = media_resource_object(r[0], false);
    assert(identity == media_resource_object(r[1], false));
    media_cache_stats before;
    media_cache_get_stats(&before);
    assert(before.decodes == 1 && before.encodes == 1 && !before.jobs);
    size_t held = MEDIA_CACHE_MEMORY - before.memory;
    assert(media_cache_reserve(held));
    assert(!media_resource_image(r[0], false, large, now_ms()));
    media_cache_stats blocked;
    media_cache_get_stats(&blocked);
    assert(blocked.decodes == 1 && !blocked.jobs);
    bounds();
    media_cache_unreserve(held);
    if (cancel_upgrade) {
        atomic_store(&worker_delay, true);
        assert(!media_resource_image(r[0], false, large, now_ms()));
        media_cache_poll(cache, now_ms()); /* Submit this visibility batch. */
        for (int n = 0; n < 5000 && !atomic_load(&blocked_workers); ++n)
            pause_tick();
        assert(atomic_load(&blocked_workers));
        uint64_t started = now_ms();
        media_cache_reset(cache);
        assert(now_ms() - started < 100);
        atomic_store(&worker_delay, false);
    } else {
        image = NULL;
        for (int n = 0; n < 10000; ++n) {
            media_cache_begin(cache);
            for (int i = 0; i < 2; ++i)
                media_resource_use(r[i], true);
            media_cache_poll(cache, now_ms());
            image = media_resource_image(r[0], false, large, now_ms());
            const graphics_image *second = media_resource_image(r[1], false, large, now_ms());
            bounds();
            if (image && second) {
                assert(image == second);
                break;
            }
            pause_tick();
        }
        assert(image && image->width == 1000 && image->height == 500);
        assert(identity == media_resource_object(r[0], false));
        for (int n = 0; n < 10000; ++n) {
            media_cache_begin(cache);
            media_resource_use(r[0], false);
            media_cache_poll(cache, now_ms());
            image = media_resource_image(r[0], false, small, now_ms());
            media_cache_stats stats;
            media_cache_get_stats(&stats);
            bounds();
            if (image && !stats.jobs)
                break;
            pause_tick();
        }
        assert(image && image->width == 160 && image->height == 80);
        media_cache_stats after;
        media_cache_get_stats(&after);
        assert(after.decodes == 2 && after.encodes == 3 && after.objects == 1);
        assert(after.memory > before.memory + 1400000);
        media_cache_begin(cache);
        media_cache_poll(cache, now_ms() + 61000);
        media_cache_get_stats(&after);
        assert(!after.memory && !after.reserved);
    }
    for (int i = 0; i < 2; ++i)
        media_resource_release(r[i]);
    printf("{\"resize\":true,\"cancelled_upgrade\":%s,\"shared_object\":true,\"deferred_resumed\":"
           "true}\n",
           cancel_upgrade ? "true" : "false");
}
static void batch_test(media_cache *cache)
{
    const char *path = getenv("PAPO_TEST_LOCAL_IMAGE");
    assert(path);
    media_resource *r = media_cache_acquire_file(cache, path, 1);
    assert(r);
    for (int n = 0; n < 5000 && !media_resource_object(r, false); ++n) {
        media_cache_begin(cache);
        media_resource_use_image(r, false, false);
        media_cache_poll(cache, now_ms());
        pause_tick();
    }
    assert(media_resource_object(r, false));
    image_decode_request small = request(160), large = request(1000);
    small.height = 80;
    large.height = 500;
    assert(!media_resource_image(r, false, small, now_ms()));
    assert(!media_resource_image(r, false, large, now_ms()));
    media_cache_stats stats;
    media_cache_get_stats(&stats);
    assert(stats.decodes == 1 && stats.jobs == 1 && stats.queued == 1 && !stats.workers);
    bool ready = false;
    for (int n = 0; n < 10000 && !ready; ++n) {
        media_cache_begin(cache);
        media_resource_use_image(r, false, false);
        media_cache_poll(cache, now_ms());
        ready = media_resource_image(r, false, large, now_ms()) &&
                media_resource_image(r, false, small, now_ms());
        bounds();
        pause_tick();
    }
    assert(ready);
    media_cache_get_stats(&stats);
    assert(stats.decodes == 1 && stats.encodes == 2 && stats.objects == 1 && !stats.requests);
    media_resource_release(r);
    puts("{\"batch\":true,\"decodes\":1,\"encodes\":2,\"largest_size_shared\":true}");
}
static void unified_test(media_cache *cache, bool retain_avatars)
{
    const char *path = getenv("PAPO_TEST_LOCAL_IMAGE");
    assert(path);
    media_resource *r[5] = {
        media_cache_acquire(cache, AUTH_MEDIA_ATTACHMENT, "00000000-0000-4000-8000-000000000901", 1,
                            false),
        media_cache_acquire_url(cache, "/webhooks/00000000-0000-4000-8000-000000000900/avatar", 1),
        media_cache_acquire_file(cache, path, 1), media_cache_acquire_unicode(cache, "😄"),
        media_cache_acquire_unicode(cache, "😄")};
    for (int i = 0; i < 5; ++i)
        assert(r[i]);
    assert(r[3] == r[4]);
    if (retain_avatars) {
        media_resource_cache_for_session(r[1]);
        media_resource_cache_for_session(r[2]);
    }
    image_decode_request image = request(48);
    image.height = 48;
    image.native = false;
    image.center = image.cover = image.matte = true;
    image.background = 0x15181e;
    bool ready = false;
    for (int n = 0; n < 10000 && !ready; ++n) {
        media_cache_begin(cache);
        for (int i = 0; i < 5; ++i)
            media_resource_use_image(r[i], false, false);
        media_cache_poll(cache, now_ms());
        ready = true;
        for (int i = 0; i < 5; ++i)
            ready &= media_resource_image(r[i], false, image, now_ms()) != NULL;
        bounds();
        pause_tick();
    }
    assert(ready);
    const void *object = media_resource_object(r[0], false);
    const graphics_image *encoded = media_resource_image(r[0], false, image, now_ms());
    for (int i = 1; i < 3; ++i) {
        assert(media_resource_object(r[i], false) == object);
        assert(media_resource_image(r[i], false, image, now_ms()) == encoded);
    }
    assert(media_resource_object(r[3], false) != object);
    media_cache_stats stats;
    media_cache_get_stats(&stats);
    assert(stats.objects == 2 && stats.decodes == 1 && stats.encodes == 2 && stats.requests == 2);
    if (retain_avatars) {
        uint64_t later = now_ms() + 600000;
        media_cache_begin(cache);
        media_cache_begin(cache);
        media_cache_poll(cache, later);
        assert(!media_resource_object(r[0], false)); /* Ordinary media still expires. */
        for (int i = 1; i < 3; ++i) {
            assert(media_resource_object(r[i], false) == object);
            assert(media_resource_image(r[i], false, image, later) == encoded);
        }
        media_cache_get_stats(&stats);
        assert(stats.requests == 2 && stats.decodes == 1 && stats.encodes == 2);
        /* Retention does not pin avatars against the shared memory budget. */
        media_cache_begin(cache);
        media_cache_begin(cache);
        assert(media_cache_reserve(MEDIA_CACHE_MEMORY));
        bounds();
        media_cache_get_stats(&stats);
        assert(!stats.memory);
        media_cache_unreserve(MEDIA_CACHE_MEMORY);
    }
    for (int i = 0; i < 5; ++i)
        media_resource_release(r[i]);
    printf("{\"sources\":4,\"objects\":2,\"decodes\":1,\"encodes\":2,\"requests\":2,\"shared\":"
           "true,\"avatars_retained\":%s}\n",
           retain_avatars ? "true" : "false");
}
int main(int argc, char **argv)
{
    main_thread = pthread_self();
    assert(argc == 3 && !curl_global_init(CURL_GLOBAL_DEFAULT));
    auth_client *a = login(argv[1]);
    media_cache *c = media_cache_create(a);
    assert(c);
    if (!strcmp(argv[2], "batch")) {
        batch_test(c);
        media_cache_destroy(c);
        auth_destroy(a);
        goto finished;
    }
    if (!strcmp(argv[2], "unified") || !strcmp(argv[2], "avatars")) {
        unified_test(c, !strcmp(argv[2], "avatars"));
        media_cache_destroy(c);
        auth_destroy(a);
        goto finished;
    }
    if (!strcmp(argv[2], "pressure")) {
        pressure_test(a, c);
        media_cache_destroy(c);
        auth_destroy(a);
        goto finished;
    }
    if (!strcmp(argv[2], "resize") || !strcmp(argv[2], "cancel-upgrade")) {
        resize_test(a, c, !strcmp(argv[2], "cancel-upgrade"));
        media_cache_destroy(c);
        auth_destroy(a);
        goto finished;
    }
    atomic_store(&fail_calloc, true);
    assert(!media_cache_acquire(c, AUTH_MEDIA_ATTACHMENT, "00000000-0000-4000-8000-000000000999", 0,
                                false));
    media_resource *r[5];
    char id[37];
    uint64_t started = now_ms();
    for (int i = 0; i < 4; ++i) {
        snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012x", 901 + i);
        r[i] =
            media_cache_acquire(c, i == 3 ? AUTH_MEDIA_EMBED : AUTH_MEDIA_ATTACHMENT, id, 1, false);
        assert(r[i]);
    }
    r[4] = media_cache_acquire(c, AUTH_MEDIA_EMBED, id, 1, false);
    assert(r[3] == r[4]);
    bool cancel = !strcmp(argv[2], "cancel"), logout = !strcmp(argv[2], "logout");
    if (cancel || logout) {
        media_cache_begin(c);
        for (int i = 0; i < 5; ++i)
            media_resource_use(r[i], i == 3);
        media_cache_poll(c, now_ms());
        if (logout) {
            auth_logout(a);
            auth_poll(a);
        }
        media_cache_begin(c);
        media_cache_poll(c, now_ms());
        for (int i = 0; i < 5; ++i)
            media_resource_release(r[i]);
        media_cache_destroy(c);
        auth_destroy(a);
        goto finished;
    }
    bool cancel_ingest = !strcmp(argv[2], "cancel-ingest"),
         cancel_decode = !strcmp(argv[2], "cancel-decode");
    if (cancel_ingest || cancel_decode) {
        atomic_store(&worker_delay, cancel_ingest);
        for (int n = 0; n < 5000 && !atomic_load(&blocked_workers); ++n) {
            auth_poll(a);
            media_cache_begin(c);
            for (int i = 0; i < 5; ++i)
                media_resource_use(r[i], i == 3);
            media_cache_poll(c, now_ms());
            if (cancel_decode && media_resource_object(r[0], false)) {
                atomic_store(&worker_delay, true);
                media_resource_image(r[0], false, request(640), now_ms());
            }
            pause_tick();
        }
        assert(atomic_load(&blocked_workers));
        uint64_t reset_start = now_ms();
        media_cache_reset(c);
        assert(now_ms() - reset_start < 100);
        for (int i = 0; i < 5; ++i)
            media_resource_release(r[i]);
        atomic_store(&worker_delay, false);
        media_cache_destroy(c);
        auth_destroy(a);
        goto finished;
    }
    bool ready = false;
    uint64_t first = 0;
    for (int n = 0; n < 10000 && !ready; ++n) {
        auth_poll(a);
        media_cache_begin(c);
        for (int i = 0; i < 5; ++i)
            media_resource_use(r[i], i == 3);
        media_cache_poll(c, now_ms());
        ready = true;
        for (int i = 0; i < 5; ++i) {
            const char *error = media_resource_error(r[i], i == 4);
            assert(!error);
            const graphics_image *image =
                media_resource_image(r[i], i == 4, request(640), now_ms());
            ready &= image != NULL;
            if (image && !first)
                first = now_ms();
        }
        bounds();
        pause_tick();
    }
    assert(ready);
    const void *object = media_resource_object(r[0], false);
    assert(object);
    const graphics_image *image = media_resource_image(r[0], false, request(640), now_ms());
    assert(image);
    for (int i = 1; i < 5; ++i) {
        assert(object == media_resource_object(r[i], i == 4));
        assert(image == media_resource_image(r[i], i == 4, request(640), now_ms()));
    }
    media_cache_stats s;
    media_cache_get_stats(&s);
    assert(s.objects == 1 && s.decodes == 1 && s.encodes == 1 && s.deduplicated == 4);
    assert(s.requests == 4);
    if (!strcmp(argv[2], "invalidate")) {
        media_cache_invalidate(c, AUTH_MEDIA_EMBED, id);
        media_resource_release(r[3]);
        media_resource_release(r[4]);
        r[3] = media_cache_acquire(c, AUTH_MEDIA_EMBED, id, 1, false);
        r[4] = media_cache_acquire(c, AUTH_MEDIA_EMBED, id, 1, false);
        assert(r[3] && r[3] == r[4]);
        bool refreshed = false;
        for (int n = 0; n < 5000 && !refreshed; ++n) {
            auth_poll(a);
            media_cache_begin(c);
            for (int i = 0; i < 5; ++i)
                media_resource_use(r[i], false);
            media_cache_poll(c, now_ms());
            refreshed = media_resource_image(r[3], false, request(640), now_ms()) != NULL;
            bounds();
            pause_tick();
        }
        assert(refreshed && media_resource_object(r[3], false) == object);
        media_cache_get_stats(&s);
        assert(s.requests == 5 && s.decodes == 1 && s.encodes == 1);
    }
    size_t bytes = s.memory, files = s.files;
    printf(
        "{\"requests\":%llu,\"objects\":%zu,\"references\":%zu,\"decodes\":%llu,\"encodes\":%llu,"
        "\"deduplicated\":%llu,\"memory\":%zu,\"files\":%zu,\"first_ms\":%llu,\"loaded_ms\":%llu,",
        (unsigned long long)s.requests, s.objects, s.references, (unsigned long long)s.decodes,
        (unsigned long long)s.encodes, (unsigned long long)s.deduplicated, s.memory, s.files,
        (unsigned long long)(first - started), (unsigned long long)(now_ms() - started));
    /* A second session cannot reuse this object, even on the same instance. */
    auth_client *b = login(argv[1]);
    media_cache *other = media_cache_create(b);
    assert(other);
    media_resource *isolated = media_cache_acquire(
        other, AUTH_MEDIA_ATTACHMENT, "00000000-0000-4000-8000-000000000901", 1, false);
    assert(isolated);
    bool separate = false;
    for (int n = 0; n < 5000 && !separate; ++n) {
        media_cache_begin(other);
        media_resource_use(isolated, false);
        media_cache_poll(other, now_ms());
        separate = media_resource_object(isolated, false) != NULL;
        bounds();
        pause_tick();
    }
    assert(separate && media_resource_object(isolated, false) != object);
    media_resource_release(isolated);
    media_cache_destroy(other);
    auth_destroy(b);
    media_cache_get_stats(&s);
    assert(s.memory == bytes && s.files == files && s.objects == 1);
    /* Holding the whole remaining global budget defers another representation. */
    size_t held = MEDIA_CACHE_MEMORY - s.memory - s.reserved;
    assert(media_cache_reserve(held));
    image_decode_request smaller = request(8);
    smaller.native = false;
    smaller.height = 8;
    assert(!media_resource_image(r[0], false, smaller, now_ms()));
    bounds();
    media_cache_unreserve(held);
    bool resumed = false;
    for (int n = 0; n < 5000 && !resumed; ++n) {
        media_cache_begin(c);
        for (int i = 0; i < 5; ++i)
            media_resource_use(r[i], false);
        media_cache_poll(c, now_ms());
        resumed = media_resource_image(r[0], false, smaller, now_ms()) != NULL;
        bounds();
        pause_tick();
    }
    assert(resumed);
    /* All image workers block inside the project allocator while the main thread
     * fills the bounded queue. This proves queue admission independently of CPU
     * speed and gives a deterministic cancellation point during encoding. */
    atomic_store(&worker_delay, true);
    for (int i = 0; i < 42; ++i) {
        image_decode_request varied = request(16 + i);
        varied.native = false;
        varied.height = 8;
        media_resource_image(r[0], false, varied, now_ms());
        bounds();
    }
    media_cache_poll(c, now_ms()); /* Dispatch the staged batch. */
    for (int n = 0; n < 1000; ++n) {
        media_cache_get_stats(&s);
        if (s.workers == MEDIA_CACHE_WORKERS &&
            atomic_load(&blocked_workers) == MEDIA_CACHE_WORKERS)
            break;
        pause_tick();
    }
    assert(s.workers == MEDIA_CACHE_WORKERS && s.queued <= 32 && s.jobs >= 32);
    if (!strcmp(argv[2], "cancel-encode")) {
        uint64_t reset_start = now_ms();
        media_cache_reset(c);
        assert(now_ms() - reset_start < 100);
        for (int i = 0; i < 5; ++i)
            media_resource_release(r[i]);
        atomic_store(&worker_delay, false);
        media_cache_destroy(c);
        auth_destroy(a);
        puts("\"cancelled_during_encoding\":true}");
        goto finished;
    }
    unsigned queue_peak = s.queued;
    atomic_store(&worker_delay, false);
    bool drained = false;
    for (int n = 0; n < 5000 && !drained; ++n) {
        media_cache_begin(c);
        for (int i = 0; i < 5; ++i)
            media_resource_use(r[i], false);
        media_cache_poll(c, now_ms());
        media_cache_get_stats(&s);
        drained = !s.jobs;
        bounds();
        pause_tick();
    }
    assert(drained && s.peak_workers == MEDIA_CACHE_WORKERS);
    atomic_store(&fail_calloc, true);
    image_decode_request failed = request(100);
    failed.native = false;
    failed.height = 8;
    assert(!media_resource_image(r[0], false, failed, now_ms()));
    bounds();
    /* One-minute decoded expiry and five-minute file expiry, without sleeps. */
    uint64_t last = now_ms();
    const graphics_image *kept = media_resource_image(r[0], false, smaller, last);
    assert(kept);
    media_cache_get_stats(&s);
    size_t before_expiry = s.memory;
    media_cache_begin(c);
    for (int i = 0; i < 5; ++i)
        media_resource_use(r[i], false);
    media_resource_keep_image(r[0], false, kept);
    media_cache_poll(c, last + 61000);
    media_cache_get_stats(&s);
    assert(s.memory > 0 && s.memory < before_expiry);
    assert(media_resource_image(r[0], false, smaller, last + 61000) == kept);
    last += 61000;
    media_cache_begin(c);
    media_cache_poll(c, last + 59000);
    media_cache_get_stats(&s);
    assert(s.memory > 0);
    media_cache_begin(c);
    media_cache_poll(c, last + 61000);
    media_cache_get_stats(&s);
    assert(!s.memory && s.files == files);
    /* A path-copy allocation failure must undo its job/object reference. */
    atomic_store(&fail_strdup, true);
    assert(!media_resource_image(r[0], false, request(640), last + 61000));
    media_cache_get_stats(&s);
    assert(!s.jobs && !s.reserved);
    bounds();
    media_cache_begin(c);
    media_cache_poll(c, last + 362000);
    media_cache_get_stats(&s);
    assert(!s.files && !s.objects);
    for (int i = 0; i < 5; ++i)
        media_resource_release(r[i]);
    media_cache_destroy(c);
    auth_destroy(a);
    printf("\"peak_workers\":4,\"queue_peak\":%u,\"allocation_failures\":true,\"isolation\":true,"
           "\"deferred_resumed\":true,\"expiry\":true,\"released\":true}\n",
           queue_peak);
finished:
    media_cache_get_stats(&s);
    assert(!s.memory && !s.reserved && !s.files && !s.file_reserved && !s.objects &&
           !s.references && !s.jobs && !s.downloads && !s.workers && !s.queued);
    curl_global_cleanup();
    return 0;
}
