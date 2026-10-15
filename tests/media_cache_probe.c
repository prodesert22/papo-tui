#define _POSIX_C_SOURCE 200809L
#include "ui/media_cache.h"
#include "net/clock.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void pause_tick(void) { struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL); }
/* Linker fault injection affects project allocations, not system libraries. */
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
char *__real_strdup(const char *text);
static atomic_bool worker_delay, fail_calloc, fail_strdup;
static atomic_uint blocked_workers;
static pthread_t main_thread;
void *__wrap_malloc(size_t size)
{
    if (atomic_load(&worker_delay) && !pthread_equal(pthread_self(), main_thread)) {
        atomic_fetch_add(&blocked_workers, 1);
        while (atomic_load(&worker_delay)) pause_tick();
        atomic_fetch_sub(&blocked_workers, 1);
    }
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size)
{
    if (atomic_exchange(&fail_calloc, false)) return NULL;
    return __real_calloc(count, size);
}
char *__wrap_strdup(const char *text)
{
    if (atomic_exchange(&fail_strdup, false)) return NULL;
    return __real_strdup(text);
}
static auth_client *login(const char *url)
{
    auth_client *a = auth_create();
    assert(a && auth_start(a, url, "josé", "Synthetic!123", ""));
    for (int i = 0; auth_busy(a) && i < 10000; ++i) { auth_poll(a); pause_tick(); }
    assert(auth_has_session(a)); return a;
}
static uint64_t now_ms(void) { uint64_t now = 0; assert(net_now_ms(&now)); return now; }
static image_decode_request request(int width)
{
    return (image_decode_request){.width = width, .height = 360, .native = true,
        .terminal = {.protocol = GRAPHICS_KITTY, .supported = true, .cell_width = 8, .cell_height = 16}};
}
static void bounds(void)
{
    media_cache_stats s; media_cache_get_stats(&s);
    assert(s.memory + s.reserved <= MEDIA_CACHE_MEMORY);
    assert(s.files + s.file_reserved <= MEDIA_CACHE_FILES && s.jobs <= MEDIA_CACHE_QUEUE + 2);
    assert(s.queued <= MEDIA_CACHE_QUEUE && s.workers <= 2 && s.peak_workers <= 2 && s.downloads <= 4);
}
static void pressure_test(auth_client *auth, media_cache *cache)
{
    media_resource *resources[12];
    for (int i = 0; i < 12; ++i) {
        char id[37]; snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012x", 901 + i);
        resources[i] = media_cache_acquire(cache, AUTH_MEDIA_ATTACHMENT, id, 1, false); assert(resources[i]);
    }
    size_t peak = 0, least_animated = 12;
    uint64_t epoch = now_ms();
    for (int cycle = 0; cycle < 3; ++cycle) {
        uint64_t base = epoch + (uint64_t)cycle * 400000, current = base;
        bool ready = false;
        for (int n = 0; n < 30000 && !ready; ++n) {
            current = base + (now_ms() - epoch);
            auth_poll(auth); media_cache_begin(cache);
            for (int i = 0; i < 12; ++i) media_resource_use(resources[i], false);
            media_cache_poll(cache, current);
            ready = true;
            for (int i = 0; i < 12; ++i) {
                const char *error = media_resource_error(resources[i], false);
                if (error) fprintf(stderr, "pressure cycle=%d resource=%d tick=%d error=%s\n", cycle, i, n, error);
                assert(!error);
                ready &= media_resource_image(resources[i], false, request(640), current) != NULL;
            }
            media_cache_stats stats; media_cache_get_stats(&stats);
            if (stats.memory + stats.reserved > peak) peak = stats.memory + stats.reserved;
            bounds(); pause_tick();
        }
        assert(ready);
        size_t animated = 0;
        for (int i = 0; i < 12; ++i) animated += media_resource_animated(resources[i], false);
        if (animated < least_animated) least_animated = animated;
        assert(animated < 12); /* Visible GIFs degrade rather than exceed 256 MiB. */
        media_cache_stats stats;
        for (int n = 0; n < 5000; ++n) {
            media_cache_begin(cache); media_cache_poll(cache, current + 61000 + (uint64_t)n);
            media_cache_get_stats(&stats);
            if (!stats.jobs) break;
            pause_tick();
        }
        assert(!stats.jobs);
        media_cache_begin(cache); media_cache_poll(cache, current + 68000);
        media_cache_get_stats(&stats); assert(!stats.memory && !stats.reserved);
        media_cache_begin(cache); media_cache_poll(cache, current + 370000);
        media_cache_get_stats(&stats); assert(!stats.files && !stats.objects && !stats.file_reserved);
    }
    for (int i = 0; i < 12; ++i) media_resource_release(resources[i]);
    printf("{\"cycles\":3,\"peak_accounted_bytes\":%zu,\"least_animated\":%zu,\"released_each_cycle\":true}\n", peak, least_animated);
}
int main(int argc, char **argv)
{
    main_thread = pthread_self();
    assert(argc == 3 && !curl_global_init(CURL_GLOBAL_DEFAULT));
    auth_client *a = login(argv[1]);
    media_cache *c = media_cache_create(a); assert(c);
    if (!strcmp(argv[2], "pressure")) {
        pressure_test(a, c); media_cache_destroy(c); auth_destroy(a); goto finished;
    }
    atomic_store(&fail_calloc, true);
    assert(!media_cache_acquire(c, AUTH_MEDIA_ATTACHMENT, "00000000-0000-4000-8000-000000000999", 0, false));
    media_resource *r[5];
    char id[37];
    uint64_t started = now_ms();
    for (int i = 0; i < 4; ++i) {
        snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012x", 901 + i);
        r[i] = media_cache_acquire(c, i == 3 ? AUTH_MEDIA_EMBED : AUTH_MEDIA_ATTACHMENT, id, 1, false); assert(r[i]);
    }
    r[4] = media_cache_acquire(c, AUTH_MEDIA_EMBED, id, 1, false);
    assert(r[3] == r[4]);
    bool cancel = !strcmp(argv[2], "cancel"), logout = !strcmp(argv[2], "logout");
    if (cancel || logout) {
        media_cache_begin(c);
        for (int i = 0; i < 5; ++i) media_resource_use(r[i], i == 3);
        media_cache_poll(c, now_ms());
        if (logout) { auth_logout(a); auth_poll(a); }
        media_cache_begin(c); media_cache_poll(c, now_ms());
        for (int i = 0; i < 5; ++i) media_resource_release(r[i]);
        media_cache_destroy(c); auth_destroy(a); goto finished;
    }
    bool cancel_ingest = !strcmp(argv[2], "cancel-ingest"), cancel_decode = !strcmp(argv[2], "cancel-decode");
    if (cancel_ingest || cancel_decode) {
        atomic_store(&worker_delay, cancel_ingest);
        for (int n = 0; n < 5000 && !atomic_load(&blocked_workers); ++n) {
            auth_poll(a); media_cache_begin(c);
            for (int i = 0; i < 5; ++i) media_resource_use(r[i], i == 3);
            media_cache_poll(c, now_ms());
            if (cancel_decode && media_resource_object(r[0], false)) {
                atomic_store(&worker_delay, true); media_resource_image(r[0], false, request(640), now_ms());
            }
            pause_tick();
        }
        assert(atomic_load(&blocked_workers));
        uint64_t reset_start = now_ms(); media_cache_reset(c); assert(now_ms() - reset_start < 100);
        for (int i = 0; i < 5; ++i) media_resource_release(r[i]);
        atomic_store(&worker_delay, false); media_cache_destroy(c); auth_destroy(a); goto finished;
    }
    bool ready = false;
    uint64_t first = 0;
    for (int n = 0; n < 10000 && !ready; ++n) {
        auth_poll(a); media_cache_begin(c);
        for (int i = 0; i < 5; ++i) media_resource_use(r[i], i == 3);
        media_cache_poll(c, now_ms());
        ready = true;
        for (int i = 0; i < 5; ++i) {
            const char *error = media_resource_error(r[i], i == 4);
            assert(!error);
            const graphics_image *image = media_resource_image(r[i], i == 4, request(640), now_ms());
            ready &= image != NULL;
            if (image && !first) first = now_ms();
        }
        bounds(); pause_tick();
    }
    assert(ready);
    const void *object = media_resource_object(r[0], false); assert(object);
    const graphics_image *image = media_resource_image(r[0], false, request(640), now_ms()); assert(image);
    for (int i = 1; i < 5; ++i) {
        assert(object == media_resource_object(r[i], i == 4));
        assert(image == media_resource_image(r[i], i == 4, request(640), now_ms()));
    }
    media_cache_stats s; media_cache_get_stats(&s);
    assert(s.objects == 1 && s.decodes == 1 && s.encodes == 1 && s.deduplicated == 4);
    assert(s.requests == 4);
    if (!strcmp(argv[2], "invalidate")) {
        media_cache_invalidate(c, AUTH_MEDIA_EMBED, id);
        media_resource_release(r[3]); media_resource_release(r[4]);
        r[3] = media_cache_acquire(c, AUTH_MEDIA_EMBED, id, 1, false);
        r[4] = media_cache_acquire(c, AUTH_MEDIA_EMBED, id, 1, false);
        assert(r[3] && r[3] == r[4]);
        bool refreshed = false;
        for (int n = 0; n < 5000 && !refreshed; ++n) {
            auth_poll(a); media_cache_begin(c);
            for (int i = 0; i < 5; ++i) media_resource_use(r[i], false);
            media_cache_poll(c, now_ms());
            refreshed = media_resource_image(r[3], false, request(640), now_ms()) != NULL;
            bounds(); pause_tick();
        }
        assert(refreshed && media_resource_object(r[3], false) == object);
        media_cache_get_stats(&s); assert(s.requests == 5 && s.decodes == 1 && s.encodes == 1);
    }
    size_t bytes = s.memory, files = s.files;
    printf("{\"requests\":%llu,\"objects\":%zu,\"references\":%zu,\"decodes\":%llu,\"encodes\":%llu,\"deduplicated\":%llu,\"memory\":%zu,\"files\":%zu,\"first_ms\":%llu,\"loaded_ms\":%llu,",
        (unsigned long long)s.requests, s.objects, s.references, (unsigned long long)s.decodes,
        (unsigned long long)s.encodes, (unsigned long long)s.deduplicated, s.memory, s.files,
        (unsigned long long)(first - started), (unsigned long long)(now_ms() - started));
    /* A second session cannot reuse this object, even on the same instance. */
    auth_client *b = login(argv[1]); media_cache *other = media_cache_create(b); assert(other);
    media_resource *isolated = media_cache_acquire(other, AUTH_MEDIA_ATTACHMENT,
        "00000000-0000-4000-8000-000000000901", 1, false); assert(isolated);
    bool separate = false;
    for (int n = 0; n < 5000 && !separate; ++n) {
        media_cache_begin(other); media_resource_use(isolated, false); media_cache_poll(other, now_ms());
        separate = media_resource_object(isolated, false) != NULL;
        bounds(); pause_tick();
    }
    assert(separate && media_resource_object(isolated, false) != object);
    media_resource_release(isolated); media_cache_destroy(other); auth_destroy(b);
    media_cache_get_stats(&s); assert(s.memory == bytes && s.files == files && s.objects == 1);
    /* Holding the whole remaining global budget defers another representation. */
    size_t held = MEDIA_CACHE_MEMORY - s.memory - s.reserved;
    assert(media_cache_reserve(held));
    image_decode_request smaller = request(8); smaller.native = false; smaller.height = 8;
    assert(!media_resource_image(r[0], false, smaller, now_ms())); bounds();
    media_cache_unreserve(held);
    bool resumed = false;
    for (int n = 0; n < 5000 && !resumed; ++n) {
        media_cache_begin(c);
        for (int i = 0; i < 5; ++i) media_resource_use(r[i], false);
        media_cache_poll(c, now_ms());
        resumed = media_resource_image(r[0], false, smaller, now_ms()) != NULL;
        bounds(); pause_tick();
    }
    assert(resumed);
    /* Both workers block inside the project allocator while the main thread
     * fills the bounded queue. This proves queue admission independently of CPU
     * speed and gives a deterministic cancellation point during encoding. */
    atomic_store(&worker_delay, true);
    for (int i = 0; i < 42; ++i) {
        image_decode_request varied = request(16 + i); varied.native = false; varied.height = 8;
        media_resource_image(r[0], false, varied, now_ms()); bounds();
    }
    for (int n = 0; n < 1000; ++n) {
        media_cache_get_stats(&s);
        if (s.workers == 2 && atomic_load(&blocked_workers) == 2) break;
        pause_tick();
    }
    assert(s.workers == 2 && s.queued <= 32 && s.jobs >= 32);
    if (!strcmp(argv[2], "cancel-encode")) {
        uint64_t reset_start = now_ms(); media_cache_reset(c); assert(now_ms() - reset_start < 100);
        for (int i = 0; i < 5; ++i) media_resource_release(r[i]);
        atomic_store(&worker_delay, false); media_cache_destroy(c); auth_destroy(a);
        puts("\"cancelled_during_encoding\":true}"); goto finished;
    }
    unsigned queue_peak = s.queued;
    atomic_store(&worker_delay, false);
    bool drained = false;
    for (int n = 0; n < 5000 && !drained; ++n) {
        media_cache_begin(c); for (int i = 0; i < 5; ++i) media_resource_use(r[i], false);
        media_cache_poll(c, now_ms()); media_cache_get_stats(&s); drained = !s.jobs;
        bounds(); pause_tick();
    }
    assert(drained && s.peak_workers == 2);
    atomic_store(&fail_calloc, true);
    image_decode_request failed = request(100); failed.native = false; failed.height = 8;
    assert(!media_resource_image(r[0], false, failed, now_ms())); bounds();
    /* One-minute decoded expiry and five-minute file expiry, without sleeps. */
    uint64_t last = now_ms();
    const graphics_image *kept = media_resource_image(r[0], false, smaller, last); assert(kept);
    media_cache_get_stats(&s); size_t before_expiry = s.memory;
    media_cache_begin(c); for (int i = 0; i < 5; ++i) media_resource_use(r[i], false);
    media_resource_keep_image(r[0], false, kept); media_cache_poll(c, last + 61000);
    media_cache_get_stats(&s); assert(s.memory > 0 && s.memory < before_expiry);
    assert(media_resource_image(r[0], false, smaller, last + 61000) == kept);
    last += 61000;
    media_cache_begin(c); media_cache_poll(c, last + 59000);
    media_cache_get_stats(&s); assert(s.memory > 0);
    media_cache_begin(c); media_cache_poll(c, last + 61000);
    media_cache_get_stats(&s); assert(!s.memory && s.files == files);
    /* A path-copy allocation failure must undo its job/object reference. */
    atomic_store(&fail_strdup, true);
    assert(!media_resource_image(r[0], false, request(640), last + 61000));
    media_cache_get_stats(&s); assert(!s.jobs && !s.reserved); bounds();
    media_cache_begin(c); media_cache_poll(c, last + 362000);
    media_cache_get_stats(&s); assert(!s.files && !s.objects);
    for (int i = 0; i < 5; ++i) media_resource_release(r[i]);
    media_cache_destroy(c); auth_destroy(a);
    printf("\"peak_workers\":2,\"queue_peak\":%u,\"allocation_failures\":true,\"isolation\":true,\"deferred_resumed\":true,\"expiry\":true,\"released\":true}\n", queue_peak);
 finished:
    media_cache_get_stats(&s);
    assert(!s.memory && !s.reserved && !s.files && !s.file_reserved && !s.objects && !s.references && !s.jobs && !s.downloads && !s.workers && !s.queued);
    curl_global_cleanup(); return 0;
}
