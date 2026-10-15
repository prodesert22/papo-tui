#define _POSIX_C_SOURCE 200809L
#include "ui/media_cache.h"
#include "ui/media_backend.h"
#include "ui/emoji_image.h"
#include "net/clock.h"
#include <glib.h>
#include <chafa.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

#define RESOURCE_LIMIT 1024U
#define TRIM_BYTES (16U * 1024U * 1024U)
#define TRIM_INTERVAL_MS 5000U

typedef struct image_object image_object;
typedef struct variant variant;
typedef struct task task;
typedef struct { char *path; char hash[65]; size_t bytes; } blob;
struct variant {
    variant *next;
    image_decode_request request;
    graphics_image image;
    task *job;
    uint64_t used, presented;
    size_t frame;
    bool pinned, failed, visible;
};
struct image_object {
    media_cache *cache;
    char hash[72];
    char *path, *glyph;
    size_t bytes, refs;
    uint64_t used, epoch;
    bool pinned, failed;
    const char *error;
    animation *frames;
    task *job;
    variant *variants;
};
struct media_resource {
    media_cache *cache;
    char key[128], id[37];
    auth_media_kind kind;
    uint64_t revision, used;
    size_t refs, visible_refs;
    enum { SOURCE_REST, SOURCE_URL, SOURCE_FILE, SOURCE_UNICODE } source_type;
    char *source;
    bool video, wanted, modal;
    unsigned expired;
    enum { RESOURCE_NEW, RESOURCE_FETCH, RESOURCE_INGEST, RESOURCE_READY, RESOURCE_ERROR } state;
    const char *error;
    char *video_path;
    image_object *objects[2];
    task *job;
};
struct media_cache {
    media_cache *next;
    auth_client *auth;
    uint64_t generation, now, maintenance;
    GHashTable *objects, *resources;
    GList *tasks;
    media_download *downloads[MEDIA_CACHE_DOWNLOADS];
    media_resource *fetching[MEDIA_CACHE_DOWNLOADS];
};
struct task {
    media_cache *cache;
    media_resource *resource;
    image_object *object;
    variant *variant;
    enum { TASK_INGEST, TASK_DECODE, TASK_ENCODE, TASK_TRIM } kind;
    atomic_bool cancelled, done;
    char *path, *input, *glyph;
    char id[37];
    bool embed, priority, ok, submitted;
    size_t reservation, file_reservation;
    image_decode_request request;
    animation *frames, *output_frames;
    graphics_image image;
    blob blobs[2];
    const char *error;
    bool trim_attempted;
};
static atomic_uint workers, peak_workers;
static struct {
    media_cache *caches;
    GThreadPool *pool;
    media_cache_stats stats;
    size_t released;
    uint64_t last_trim;
} shared;

static void note_released(size_t bytes)
{
    size_t room = MEDIA_CACHE_MEMORY - shared.released;
    shared.released += bytes < room ? bytes : room;
}
static void release_memory(size_t bytes)
{
    shared.stats.memory -= bytes;
    note_released(bytes);
}
static void unlink_path(char **path)
{
    if (*path) { unlink(*path); free(*path); *path = NULL; }
}
static void drop_variants(image_object *o)
{
    variant **link = &o->variants;
    while (*link) {
        variant *v = *link;
        if (v->job || v->pinned) { link = &v->next; continue; }
        release_memory(graphics_image_bytes(&v->image) + sizeof(*v));
        graphics_image_free(&v->image); *link = v->next; free(v);
    }
}
static void object_ref(image_object *o) { if (o) { ++o->refs; ++shared.stats.references; } }
static void object_unref(image_object *o)
{
    if (!o) return;
    --shared.stats.references;
    if (--o->refs) return;
    for (variant *v = o->variants; v; v = v->next) v->pinned = false;
    drop_variants(o);
    release_memory(o->frames ? o->frames->bytes + sizeof(animation) : 0);
    animation_free(o->frames);
    if (o->path) shared.stats.files -= o->bytes;
    unlink_path(&o->path); free(o->glyph); --shared.stats.objects; free(o);
}
void media_resource_ref(media_resource *r) { if (r) ++r->refs; }
void media_resource_release(media_resource *r)
{
    if (!r || --r->refs) return;
    object_unref(r->objects[0]); object_unref(r->objects[1]);
    unlink_path(&r->video_path); free(r->source); free(r);
}
void media_resource_retry(media_resource *r)
{
    if (!r) return;
    if (r->state == RESOURCE_ERROR) { r->state = RESOURCE_NEW; r->error = NULL; }
    for (int i = 0; i < 2; ++i) if (r->objects[i]) {
        r->objects[i]->failed = false;
        for (variant *v = r->objects[i]->variants; v; v = v->next) v->failed = false;
    }
}
static void resource_destroy(gpointer p) { media_resource_release(p); }
static void object_destroy(gpointer p) { object_unref(p); }
static bool cancelled(void *context) { return atomic_load(&((task *)context)->cancelled); }
static bool hash_blob(task *t, blob *b)
{
    FILE *f = fopen(b->path, "rb");
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    if (!f || !sum) { if (f) fclose(f); if (sum) g_checksum_free(sum); return false; }
    unsigned char buffer[64 * 1024]; size_t n;
    bool ok = true;
    while ((n = fread(buffer, 1, sizeof(buffer), f))) {
        if (cancelled(t) || n > MEDIA_IMAGE_LIMIT - b->bytes) { ok = false; break; }
        b->bytes += n; g_checksum_update(sum, buffer, (gssize)n);
    }
    ok &= !ferror(f) && b->bytes > 0;
    if (ok) snprintf(b->hash, sizeof(b->hash), "%s", g_checksum_get_string(sum));
    fclose(f); g_checksum_free(sum); return ok;
}
static bool copy_input(task *t)
{
    FILE *source = fopen(t->input, "rb"), *out = fopen(t->path, "wb");
    bool ok = source && out;
    unsigned char bytes[64 * 1024]; size_t total = 0, n;
    if (ok) while ((n = fread(bytes, 1, sizeof(bytes), source))) {
        if (cancelled(t) || n > t->file_reservation - total || fwrite(bytes, 1, n, out) != n) { ok = false; break; }
        total += n;
    }
    if (source) { ok &= !ferror(source); fclose(source); }
    if (out && fclose(out)) ok = false;
    return ok && total > 0;
}
static bool extract_blob(task *t, json_t *value, blob *b)
{
    if (!json_is_string(value)) return false;
    size_t n = json_string_length(value);
    if (!n || n % 4 || n > (MEDIA_IMAGE_LIMIT + 2U) / 3U * 4U ||
        strlen(json_string_value(value)) != n || cancelled(t)) return false;
    const char *encoded = json_string_value(value);
    size_t padding = (encoded[n - 1] == '=') + (encoded[n - 2] == '=');
    for (size_t i = 0; i < n - padding; ++i)
        if (!g_ascii_isalnum(encoded[i]) && encoded[i] != '+' && encoded[i] != '/') return false;
    unsigned char *bytes = malloc(n / 4 * 3 + 1);
    if (!bytes) return false;
    int state = 0; guint save = 0;
    size_t length = g_base64_decode_step(encoded, n, bytes, &state, &save);
    bool ok = length > 0 && length == n / 4 * 3 - padding && length <= MEDIA_IMAGE_LIMIT;
    if (ok) {
        size_t size = strlen(t->path) + 16;
        b->path = malloc(size);
        if (b->path) snprintf(b->path, size, "%s-XXXXXX", t->path);
        int fd = b->path ? mkstemp(b->path) : -1;
        FILE *f = fd >= 0 ? fdopen(fd, "wb") : NULL;
        ok = f && !cancelled(t) && fwrite(bytes, 1, (size_t)length, f) == (size_t)length;
        if (f) { if (fclose(f)) ok = false; }
        else if (fd >= 0) close(fd);
    }
    free(bytes);
    if (ok) ok = hash_blob(t, b);
    if (!ok) unlink_path(&b->path);
    return ok;
}
static void worker(gpointer data, gpointer unused)
{
    (void)unused;
    task *t = data;
    unsigned running = atomic_fetch_add(&workers, 1) + 1;
    unsigned peak = atomic_load(&peak_workers);
    while (running > peak && !atomic_compare_exchange_weak(&peak_workers, &peak, running)) {}
    if (!cancelled(t)) {
        if (t->kind == TASK_INGEST) {
            if (t->input && !copy_input(t)) { t->ok = false; }
            else if (t->embed) {
                json_error_t error;
                json_t *body = json_load_file(t->path, JSON_REJECT_DUPLICATES, &error);
                const char *id = json_string_value(json_object_get(body, "id"));
                if (id && !strcmp(id, t->id)) {
                    extract_blob(t, json_object_get(body, "image_data"), &t->blobs[0]);
                    extract_blob(t, json_object_get(body, "author_image_data"), &t->blobs[1]);
                    t->ok = t->blobs[0].path || t->blobs[1].path;
                }
                json_decref(body);
            } else {
                t->blobs[0].path = t->path; t->path = NULL;
                t->ok = hash_blob(t, &t->blobs[0]);
            }
        } else if (t->kind == TASK_DECODE) {
            if (!media_image_backend()) t->error = "Módulo de imagens ausente ou incompatível. Reinstale o aplicativo.";
            else t->ok = image_decode_file(&t->request, &t->image, &t->output_frames, cancelled, t);
        } else if (t->kind == TASK_TRIM) {
#ifdef __GLIBC__
            t->trim_attempted = true; t->ok = malloc_trim(0) != 0;
#endif
        } else if (t->glyph) {
            t->ok = emoji_image_encode(t->glyph, t->request.width, t->request.height, t->request.background,
                                      t->request.terminal, t->request.image_id, &t->image);
        } else {
            t->ok = image_encode_frame(&t->request, &t->image);
        }
    }
    atomic_fetch_sub(&workers, 1);
    atomic_store(&t->done, true);
}
static gint priority(gconstpointer a, gconstpointer b, gpointer unused)
{
    (void)unused;
    const task *x = a, *y = b;
    return (int)y->priority - (int)x->priority;
}
static bool object_busy(const image_object *o)
{
    if (o->job) return true;
    for (variant *v = o->variants; v; v = v->next) if (v->job) return true;
    return false;
}
static void remove_variant(image_object *o, variant *v)
{
    variant **link = &o->variants;
    while (*link && *link != v) link = &(*link)->next;
    if (*link) *link = v->next;
    release_memory(graphics_image_bytes(&v->image) + sizeof(*v));
    graphics_image_free(&v->image); free(v);
}
static bool make_room(size_t memory, size_t files)
{
    while (memory > MEDIA_CACHE_MEMORY - shared.stats.memory - shared.stats.reserved) {
        variant *oldest = NULL;
        image_object *owner = NULL;
        image_object *decoded = NULL, *animated = NULL;
        for (media_cache *c = shared.caches; c; c = c->next) {
            GHashTableIter it; gpointer p; g_hash_table_iter_init(&it, c->objects);
            while (g_hash_table_iter_next(&it, NULL, &p)) {
                image_object *o = p;
                for (variant *v = o->variants; v; v = v->next)
                    if (!v->pinned && !v->job && (!oldest || v->used < oldest->used)) { oldest = v; owner = o; }
                if (!object_busy(o) && o->frames) {
                    if (!o->pinned && (!decoded || o->used < decoded->used)) decoded = o;
                    if (o->frames->count > 1 && atomic_load(&o->frames->references) == 1 &&
                        (!animated || o->used < animated->used)) animated = o;
                }
            }
        }
        if (oldest) {
            remove_variant(owner, oldest);
        } else if (decoded) {
            release_memory(decoded->frames->bytes + sizeof(animation)); animation_free(decoded->frames); decoded->frames = NULL;
        } else if (animated) {
            animation *a = animated->frames;
            size_t first = (size_t)a->width * a->height * (a->rgba[0] ? 4U : 3U) + IMAGE_PIXEL_PADDING;
            for (size_t i = 1; i < a->count; ++i) { free(a->pixels[i]); a->pixels[i] = NULL; }
            release_memory(a->bytes - first);
            a->bytes = first; a->count = 1; a->duration = a->delay[0];
        } else return false;
    }
    while (files > MEDIA_CACHE_FILES - shared.stats.files - shared.stats.file_reserved) {
        image_object *oldest = NULL;
        media_resource *unused = NULL;
        for (media_cache *c = shared.caches; c; c = c->next) {
            GHashTableIter it; gpointer p; g_hash_table_iter_init(&it, c->objects);
            while (g_hash_table_iter_next(&it, NULL, &p)) {
                image_object *o = p;
                if (!o->pinned && o->refs == 1 && !object_busy(o) && (!oldest || o->used < oldest->used)) oldest = o;
            }
            g_hash_table_iter_init(&it, c->resources);
            while (g_hash_table_iter_next(&it, NULL, &p)) {
                media_resource *r = p;
                if (!r->wanted && r->refs == 1 && !r->job && r->state != RESOURCE_FETCH &&
                    (!unused || r->used < unused->used)) unused = r;
            }
        }
        if (oldest) g_hash_table_remove(oldest->cache->objects, oldest->hash);
        else if (unused) g_hash_table_remove(unused->cache->resources, unused->key);
        else return false;
    }
    return true;
}
static unsigned queued_tasks(void)
{
    unsigned pending = g_thread_pool_unprocessed(shared.pool);
    for (media_cache *c = shared.caches; c; c = c->next)
        for (GList *l = c->tasks; l; l = l->next) pending += !((task *)l->data)->submitted;
    return pending;
}
static task *task_new(media_cache *c, size_t reservation)
{
    if (queued_tasks() >= MEDIA_CACHE_QUEUE || shared.stats.jobs >= MEDIA_CACHE_QUEUE + MEDIA_CACHE_WORKERS ||
        !make_room(reservation, 0)) return NULL;
    task *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->cache = c; t->reservation = reservation;
    atomic_init(&t->cancelled, false); atomic_init(&t->done, false);
    return t;
}
static bool task_push(task *t)
{
    /* Publish a whole visibility pass together. Reservation/deduplication
     * happen before any worker starts consuming this frame's image jobs. */
    t->cache->tasks = g_list_prepend(t->cache->tasks, t);
    shared.stats.reserved += t->reservation; ++shared.stats.jobs;
    return true;
}
static void flush_tasks(media_cache *c)
{
    for (GList *l = g_list_last(c->tasks); l; l = l->prev) {
        task *t = l->data;
        if (t->submitted) continue;
        t->submitted = true;
        GError *error = NULL;
        bool ok = g_thread_pool_push(shared.pool, t, &error);
        if (error) g_error_free(error);
        /* GLib retains the item even when thread creation fails. */
        if (!ok) atomic_store(&t->cancelled, true);
    }
}
static void queue_trim(media_cache *c)
{
#ifdef __GLIBC__
    /* No terminal-thread heap walk. Share the existing bounded pool and wait
     * for media work to drain; large releases coalesce across all sessions. */
    uint64_t now;
    if (shared.released < TRIM_BYTES || shared.stats.jobs || shared.stats.downloads || shared.stats.reserved ||
        !net_now_ms(&now) || now < shared.last_trim || now - shared.last_trim < TRIM_INTERVAL_MS) return;
    task *t = task_new(c, 0);
    if (!t) return;
    t->kind = TASK_TRIM;
    if (task_push(t)) { shared.released = 0; shared.last_trim = now; }
#else
    (void)c;
#endif
}
static void task_free(task *t)
{
    if (t->kind == TASK_INGEST) unlink_path(&t->path);
    else free(t->path);
    for (int i = 0; i < 2; ++i) unlink_path(&t->blobs[i].path);
    if (t->output_frames) note_released(t->output_frames->bytes);
    animation_free(t->frames); animation_free(t->output_frames); graphics_image_free(&t->image);
    free(t->input); free(t->glyph);
    media_resource_release(t->resource); object_unref(t->object);
    shared.stats.file_reserved -= t->file_reservation;
    shared.stats.reserved -= t->reservation; --shared.stats.jobs; free(t);
}
static image_object *intern(media_cache *c, blob *b)
{
    if (!b->path || !*b->hash) return NULL;
    image_object *o = g_hash_table_lookup(c->objects, b->hash);
    if (o) { ++shared.stats.deduplicated; object_ref(o); return o; }
    if (b->bytes > MEDIA_CACHE_FILES - shared.stats.files - shared.stats.file_reserved) return NULL;
    o = calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->cache = c; snprintf(o->hash, sizeof(o->hash), "%s", b->hash);
    o->path = b->path; b->path = NULL; o->bytes = b->bytes; o->used = c->now;
    shared.stats.files += o->bytes; ++shared.stats.objects;
    object_ref(o); g_hash_table_insert(c->objects, o->hash, o);
    object_ref(o); return o;
}
static bool collect(media_cache *c)
{
    bool changed = false;
    for (GList *l = c->tasks, *next; l; l = next) {
        next = l->next; task *t = l->data;
        if (!atomic_load(&t->done)) continue;
        bool ok = t->ok && !cancelled(t);
        shared.stats.file_reserved -= t->file_reservation; t->file_reservation = 0;
        if (t->kind == TASK_INGEST) {
            media_resource *r = t->resource;
            if (r->job == t) {
                r->job = NULL;
                if (ok) for (int i = 0; i < 2; ++i) {
                    object_unref(r->objects[i]); r->objects[i] = intern(c, &t->blobs[i]);
                }
                if (ok) r->expired = 0;
                r->state = cancelled(t) ? RESOURCE_NEW : ok ? RESOURCE_READY : RESOURCE_ERROR;
                r->error = ok ? NULL : "Imagem inválida ou indisponível.";
                /* Full file budget is retryable after maintenance. */
                if (ok && !r->objects[0] && !r->objects[1]) r->state = RESOURCE_NEW;
            }
        } else if (t->kind == TASK_DECODE) {
            image_object *o = t->object;
            if (o->job == t) {
                o->job = NULL; o->failed = !ok && !cancelled(t); o->error = t->error;
                if (ok) {
                    if (o->frames) {
                        release_memory(o->frames->bytes + sizeof(animation)); animation_free(o->frames);
                    } else o->epoch = c->now;
                    o->frames = t->output_frames; t->output_frames = NULL;
                    shared.stats.memory += o->frames->bytes + sizeof(animation);
                    for (variant *v = o->variants; v; v = v->next) {
                        v->frame = SIZE_MAX; v->presented = 0; v->failed = false;
                    }
                }
            }
        } else if (t->kind == TASK_TRIM) {
            if (t->trim_attempted) { ++shared.stats.trim_attempts; shared.stats.trims += t->ok; }
        } else {
            variant *v = t->variant;
            if (v->job == t) {
                v->job = NULL; v->failed = !ok && !cancelled(t);
                if (ok) {
                    shared.stats.memory -= graphics_image_bytes(&v->image);
                    graphics_image_free(&v->image); v->image = t->image; memset(&t->image, 0, sizeof(t->image));
                    shared.stats.memory += graphics_image_bytes(&v->image);
                    v->frame = t->request.frame; v->presented = c->now;
                }
            }
        }
        c->tasks = g_list_delete_link(c->tasks, l); task_free(t); changed = true;
    }
    return changed;
}
media_cache *media_cache_create(auth_client *auth)
{
    media_cache *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (!shared.pool) {
        /* Set library globals before image workers can run. */
        chafa_set_n_threads(1);
        GError *error = NULL;
        shared.pool = g_thread_pool_new(worker, NULL, MEDIA_CACHE_WORKERS, true, &error);
        if (error) {
            g_error_free(error);
            if (shared.pool) g_thread_pool_free(shared.pool, true, true);
            shared.pool = NULL;
        }
        if (!shared.pool) { free(c); return NULL; }
        g_thread_pool_set_sort_function(shared.pool, priority, NULL);
    }
    c->auth = auth; c->generation = auth_context_generation(auth);
    c->objects = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, object_destroy);
    c->resources = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, resource_destroy);
    c->next = shared.caches; shared.caches = c;
    for (int i = 0; i < MEDIA_CACHE_DOWNLOADS; ++i) {
        c->downloads[i] = media_download_create(auth);
        if (!c->downloads[i]) { media_cache_destroy(c); return NULL; }
    }
    return c;
}
void media_cache_invalidate(media_cache *c, auth_media_kind kind, const char *id)
{
    if (!c || !id) return;
    GHashTableIter it; gpointer p; g_hash_table_iter_init(&it, c->resources);
    while (g_hash_table_iter_next(&it, NULL, &p)) {
        media_resource *r = p;
        if (r->kind != kind || strcmp(r->id, id)) continue;
        r->wanted = false;
        if (r->job) atomic_store(&r->job->cancelled, true);
        g_hash_table_iter_remove(&it);
    }
}
void media_cache_reset(media_cache *c)
{
    if (!c) return;
    for (int i = 0; i < MEDIA_CACHE_DOWNLOADS; ++i) {
        media_download_cancel(c->downloads[i]);
        if (c->fetching[i]) { c->fetching[i]->state = RESOURCE_NEW; media_resource_release(c->fetching[i]); c->fetching[i] = NULL; --shared.stats.downloads; }
    }
    for (GList *l = c->tasks; l; l = l->next) {
        task *t = l->data;
        if (t->kind != TASK_TRIM) atomic_store(&t->cancelled, true);
    }
    collect(c);
    g_hash_table_remove_all(c->resources); g_hash_table_remove_all(c->objects);
    c->generation = auth_context_generation(c->auth);
    queue_trim(c);
}
void media_cache_destroy(media_cache *c)
{
    if (!c) return;
    media_cache_reset(c);
    /* Normal logout/reset only cancels. Teardown waits before destroying the
     * temp directories and worker publication contexts. */
    flush_tasks(c);
    while (c->tasks) { collect(c); if (c->tasks) g_usleep(1000); }
    for (int i = 0; i < MEDIA_CACHE_DOWNLOADS; ++i) media_download_destroy(c->downloads[i]);
    g_hash_table_destroy(c->resources); g_hash_table_destroy(c->objects);
    media_cache **link = &shared.caches;
    while (*link && *link != c) link = &(*link)->next;
    if (*link) *link = c->next;
    free(c);
    if (!shared.caches) { g_thread_pool_free(shared.pool, false, true); shared.pool = NULL; }
}
static media_resource *acquire(media_cache *c, const char *key, auth_media_kind kind, const char *id,
                               uint64_t revision, bool video, int type, const char *source)
{
    media_resource *r = g_hash_table_lookup(c->resources, key);
    if (r) { media_resource_ref(r); return r; }
    if (g_hash_table_size(c->resources) >= RESOURCE_LIMIT) {
        media_resource *oldest = NULL; GHashTableIter it; gpointer p;
        g_hash_table_iter_init(&it, c->resources);
        while (g_hash_table_iter_next(&it, NULL, &p)) {
            media_resource *entry = p;
            if (entry->refs == 1 && !entry->wanted && !entry->job && entry->state != RESOURCE_FETCH &&
                (!oldest || entry->used < oldest->used)) oldest = entry;
        }
        if (!oldest) return NULL;
        g_hash_table_remove(c->resources, oldest->key);
    }
    r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->cache = c; r->kind = kind; r->video = video; r->revision = revision; r->source_type = type;
    r->source = source ? strdup(source) : NULL;
    if (source && !r->source) { free(r); return NULL; }
    snprintf(r->key, sizeof(r->key), "%s", key); snprintf(r->id, sizeof(r->id), "%s", id);
    r->refs = 2; g_hash_table_insert(c->resources, r->key, r); return r;
}
media_resource *media_cache_acquire(media_cache *c, auth_media_kind kind, const char *id, uint64_t revision, bool video)
{
    if (!c || !id || strlen(id) != 36) return NULL;
    char key[128]; snprintf(key, sizeof(key), "%d:%s:%llu", (int)kind, id, (unsigned long long)revision);
    return acquire(c, key, kind, id, revision, video, SOURCE_REST, NULL);
}
static media_resource *acquire_source(media_cache *c, int type, const char *source, uint64_t revision)
{
    if (!c || !source || !*source || strlen(source) > 4096) return NULL;
    char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, source, -1);
    if (!digest) return NULL;
    char key[128]; snprintf(key, sizeof(key), "source:%d:%s:%llu", type, digest, (unsigned long long)revision);
    g_free(digest);
    return acquire(c, key, AUTH_MEDIA_ATTACHMENT, "", revision, false, type, source);
}
media_resource *media_cache_acquire_url(media_cache *c, const char *url, uint64_t revision)
{ return acquire_source(c, SOURCE_URL, url, revision); }
media_resource *media_cache_acquire_file(media_cache *c, const char *path, uint64_t revision)
{ return acquire_source(c, SOURCE_FILE, path, revision); }
media_resource *media_cache_acquire_unicode(media_cache *c, const char *text)
{ return acquire_source(c, SOURCE_UNICODE, text, 0); }
void media_resource_hold(media_resource *r, bool visible)
{
    if (!r) return;
    if (visible) ++r->visible_refs;
    else if (r->visible_refs) --r->visible_refs;
}
void media_cache_begin(media_cache *c)
{
    if (!c) return;
    GHashTableIter it; gpointer p;
    g_hash_table_iter_init(&it, c->objects);
    while (g_hash_table_iter_next(&it, NULL, &p)) {
        image_object *o = p; o->pinned = false;
        for (variant *v = o->variants; v; v = v->next) {
            v->pinned = v->visible; v->visible = false;
        }
    }
    g_hash_table_iter_init(&it, c->resources);
    while (g_hash_table_iter_next(&it, NULL, &p)) {
        media_resource *r = p; r->wanted = r->modal = false;
        if (r->visible_refs) media_resource_use_image(r, false, false);
    }
}
void media_resource_use(media_resource *r, bool modal)
{
    if (!r) return;
    r->wanted = true; r->modal |= modal; r->used = r->cache->now;
    for (int i = 0; i < 2; ++i) if (r->objects[i]) { r->objects[i]->pinned = true; r->objects[i]->used = r->used; }
}
void media_resource_use_image(media_resource *r, bool author, bool modal)
{
    if (!r) return;
    unsigned index = author ? 1 : 0;
    r->wanted = true; r->modal |= modal; r->used = r->cache->now;
    if (r->expired & (1U << index)) { r->state = RESOURCE_NEW; r->expired &= ~(1U << index); }
    image_object *o = r->objects[index];
    if (o) { o->pinned = true; o->used = r->used; }
}
void media_resource_keep_image(media_resource *r, bool author, const graphics_image *image)
{
    image_object *o = r ? r->objects[author ? 1 : 0] : NULL;
    if (!o || !image || !image->id) return;
    for (variant *v = o->variants; v; v = v->next) if (v->request.image_id == image->id) {
        v->pinned = true; v->used = r->cache->now; return;
    }
}
static bool maintain(uint64_t now)
{
    bool changed = false;
    for (media_cache *c = shared.caches; c; c = c->next) {
        if (now < c->maintenance || now - c->maintenance < 1000) continue;
        c->maintenance = now;
        GHashTableIter it; gpointer p;
        g_hash_table_iter_init(&it, c->resources);
        while (g_hash_table_iter_next(&it, NULL, &p)) {
            media_resource *r = p;
            for (int i = 0; i < 2; ++i) {
                image_object *o = r->objects[i];
                if (o && !o->pinned && !object_busy(o) && now >= o->used && now - o->used >= 300000) {
                    r->objects[i] = NULL; r->expired |= 1U << i; object_unref(o);
                }
            }
            if (!r->wanted && !r->job && r->state != RESOURCE_FETCH &&
                (now >= r->used && now - r->used >= 300000)) {
                /* Handles still held by a position can request again. */
                object_unref(r->objects[0]); object_unref(r->objects[1]); r->objects[0] = r->objects[1] = NULL;
                unlink_path(&r->video_path); r->state = RESOURCE_NEW;
                if (r->refs == 1) g_hash_table_iter_remove(&it);
            }
        }
        g_hash_table_iter_init(&it, c->objects);
        while (g_hash_table_iter_next(&it, NULL, &p)) {
            image_object *o = p;
            for (variant *v = o->variants, *next; v; v = next) {
                next = v->next;
                if (!v->pinned && !v->job && now >= v->used && now - v->used >= 60000) {
                    remove_variant(o, v); changed = true;
                }
            }
            /* Drop unneeded representations before shared decoded frames. */
            if (now >= o->used && now - o->used >= 60000) {
                drop_variants(o);
                if (!o->pinned && !o->job && o->frames) {
                    bool busy = false;
                    for (variant *v = o->variants; v; v = v->next) busy |= v->job != NULL;
                    if (!busy) { release_memory(o->frames->bytes + sizeof(animation)); animation_free(o->frames); o->frames = NULL; changed = true; }
                }
            }
            if (!o->pinned && o->refs == 1 && !o->job && (now >= o->used && now - o->used >= 300000)) {
                g_hash_table_iter_remove(&it); changed = true;
            }
        }
    }
    return changed;
}
bool media_cache_poll(media_cache *c, uint64_t now)
{
    if (!c) return false;
    c->now = now;
    if (c->generation != auth_context_generation(c->auth) || !auth_has_session(c->auth)) { media_cache_reset(c); return true; }
    for (GList *l = c->tasks; l; l = l->next) {
        task *t = l->data;
        if ((t->resource && !t->resource->wanted) || (t->object && !t->object->pinned)) atomic_store(&t->cancelled, true);
    }
    bool changed = collect(c);
    GHashTableIter videos; gpointer value;
    g_hash_table_iter_init(&videos, c->resources);
    while (g_hash_table_iter_next(&videos, NULL, &value)) {
        media_resource *r = value;
        if (r->video && !r->wanted && r->video_path) { unlink_path(&r->video_path); r->state = RESOURCE_NEW; }
    }
    changed |= maintain(now);
    for (int i = 0; i < MEDIA_CACHE_DOWNLOADS; ++i) {
        media_resource *r = c->fetching[i];
        if (!r) continue;
        media_download *d = c->downloads[i];
        if (!r->wanted) {
            media_download_cancel(d); r->state = RESOURCE_NEW;
        } else {
            media_download_poll(d);
            if (media_download_state(d) == MEDIA_FAILED) { r->state = RESOURCE_ERROR; r->error = media_download_error(d); }
            else if (media_download_state(d) == MEDIA_READY) {
                if (r->video) { r->video_path = media_download_take_file(d); r->state = RESOURCE_READY; }
                else {
                    size_t file_reserve = media_download_bytes(d);
                    if (r->kind == AUTH_MEDIA_EMBED) {
                        file_reserve = (file_reserve / 4 + 1) * 3;
                        if (file_reserve > 2U * MEDIA_IMAGE_LIMIT) file_reserve = 2U * MEDIA_IMAGE_LIMIT;
                    }
                    if (!make_room(0, file_reserve)) continue;
                    task *t = task_new(c, 0);
                    if (!t) continue;
                    t->file_reservation = file_reserve; shared.stats.file_reserved += file_reserve;
                    t->kind = TASK_INGEST; t->path = media_download_take_file(d); t->embed = r->kind == AUTH_MEDIA_EMBED;
                    snprintf(t->id, sizeof(t->id), "%s", r->id);
                    t->resource = r; media_resource_ref(r); t->priority = r->modal; r->job = t; r->state = RESOURCE_INGEST;
                    task_push(t);
                }
            } else continue;
        }
        c->fetching[i] = NULL; media_resource_release(r); --shared.stats.downloads; changed = true;
    }
    GHashTableIter local; gpointer local_value;
    g_hash_table_iter_init(&local, c->resources);
    while (g_hash_table_iter_next(&local, NULL, &local_value)) {
        media_resource *r = local_value;
        if (!r->wanted || r->state != RESOURCE_NEW) continue;
        if (r->source_type == SOURCE_UNICODE) {
            char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, r->source, -1);
            if (!digest) continue;
            char key[72]; snprintf(key, sizeof(key), "glyph:%s", digest);
            image_object *o = g_hash_table_lookup(c->objects, key);
            if (!o) {
                o = calloc(1, sizeof(*o));
                if (o) o->glyph = strdup(r->source);
                if (o && o->glyph) {
                    o->cache = c; snprintf(o->hash, sizeof(o->hash), "%s", key);
                    object_ref(o); g_hash_table_insert(c->objects, o->hash, o); ++shared.stats.objects;
                } else { free(o); o = NULL; }
            }
            g_free(digest);
            if (o) { object_ref(o); r->objects[0] = o; r->state = RESOURCE_READY; changed = true; }
        } else if (r->source_type == SOURCE_FILE) {
            struct stat info;
            if (stat(r->source, &info) || info.st_size < 1 || info.st_size > MEDIA_IMAGE_LIMIT) {
                r->state = RESOURCE_ERROR; r->error = "Arquivo de imagem indisponível."; changed = true; continue;
            }
            size_t bytes = (size_t)info.st_size;
            if (!make_room(0, bytes)) continue;
            task *t = task_new(c, 0);
            if (!t) continue;
            t->input = strdup(r->source);
            int fd = t->input ? media_download_temp(c->downloads[0], &t->path) : -1;
            if (fd < 0) { free(t->input); free(t); continue; }
            close(fd);
            t->file_reservation = bytes; shared.stats.file_reserved += bytes;
            t->kind = TASK_INGEST; t->resource = r; media_resource_ref(r);
            r->job = t; r->state = RESOURCE_INGEST; task_push(t);
        }
    }
    bool waiting_modal = false;
    GHashTableIter priority_it; gpointer priority_value;
    g_hash_table_iter_init(&priority_it, c->resources);
    while (g_hash_table_iter_next(&priority_it, NULL, &priority_value)) {
        media_resource *r = priority_value;
        waiting_modal |= r->wanted && r->modal && r->state == RESOURCE_NEW;
    }
    if (waiting_modal && shared.stats.downloads == MEDIA_CACHE_DOWNLOADS) {
        for (int i = 0; i < MEDIA_CACHE_DOWNLOADS; ++i) {
            media_resource *r = c->fetching[i];
            if (!r || r->modal) continue;
            media_download_cancel(c->downloads[i]); r->state = RESOURCE_NEW;
            c->fetching[i] = NULL; media_resource_release(r); --shared.stats.downloads; break;
        }
    }
    for (int i = 0; i < MEDIA_CACHE_DOWNLOADS && shared.stats.downloads < MEDIA_CACHE_DOWNLOADS; ++i) if (!c->fetching[i]) {
        media_resource *best = NULL; GHashTableIter it; gpointer p;
        g_hash_table_iter_init(&it, c->resources);
        while (g_hash_table_iter_next(&it, NULL, &p)) {
            media_resource *r = p;
            if (r->wanted && r->state == RESOURCE_NEW && (r->source_type == SOURCE_REST || r->source_type == SOURCE_URL) && (!best || r->modal > best->modal)) best = r;
        }
        if (!best) break;
        bool started = best->source_type == SOURCE_URL ? media_download_avatar_request(c->downloads[i], best->source) :
            media_download_request(c->downloads[i], best->kind, best->id, best->video);
        if (started) {
            c->fetching[i] = best; media_resource_ref(best); best->state = RESOURCE_FETCH;
            ++shared.stats.requests; ++shared.stats.downloads;
            media_download_poll(c->downloads[i]);
        } else { best->state = RESOURCE_ERROR; best->error = "Não foi possível preparar o download."; }
    }
    queue_trim(c);
    flush_tasks(c);
    return changed;
}
static bool equal_request(const image_decode_request *a, const image_decode_request *b)
{
    return a->width == b->width && a->height == b->height && a->native == b->native &&
        a->cover == b->cover && a->center == b->center && a->matte == b->matte && a->background == b->background &&
        a->top == b->top && a->bottom == b->bottom && a->terminal.protocol == b->terminal.protocol &&
        a->terminal.cell_width == b->terminal.cell_width && a->terminal.cell_height == b->terminal.cell_height;
}
const graphics_image *media_resource_image(media_resource *r, bool author, image_decode_request request, uint64_t now)
{
    if (!r || r->video || !graphics_image_capacity(request.width, request.height)) return NULL;
    image_object *o = r->objects[author ? 1 : 0];
    if (!o || o->failed) return NULL;
    o->pinned = true; o->used = now;
    int decode_width = request.width, decode_height = request.height;
    int needed_width = request.width, needed_height = request.height;
    if (o->frames && request.max_side > 0 && (o->frames->source_width > request.max_side || o->frames->source_height > request.max_side)) return NULL;
    if (o->frames) {
        double ratio = fmin(1, fmin((double)request.width / o->frames->source_width,
                                   (double)request.height / o->frames->source_height));
        needed_width = (int)(o->frames->source_width * ratio);
        needed_height = (int)(o->frames->source_height * ratio);
        if (needed_width < 1) needed_width = 1;
        if (needed_height < 1) needed_height = 1;
    }
    bool enlarge = o->frames && (needed_width > o->frames->width || needed_height > o->frames->height);
    /* Requests collected in one visibility pass share the largest decode
     * admitted for that content before the batch reaches the workers. */
    if (o->job && !o->job->submitted && o->job->kind == TASK_DECODE) {
        task *pending = o->job;
        int w = pending->request.width > request.width ? pending->request.width : request.width;
        int h = pending->request.height > request.height ? pending->request.height : request.height;
        size_t raster = image_frame_bytes(w, h);
        size_t reserve = (pending->request.static_only || raster > ANIMATION_BYTES ? raster : ANIMATION_BYTES) +
            sizeof(animation) + (size_t)w * h * 4 + IMAGE_PIXEL_PADDING;
        size_t extra = reserve > pending->reservation ? reserve - pending->reservation : 0;
        if (!extra || make_room(extra, 0)) {
            pending->request.width = w; pending->request.height = h;
            pending->reservation += extra; shared.stats.reserved += extra;
        }
    }
    if (o->job && r->modal) {
        if (!o->job->submitted) o->job->priority = true;
        else g_thread_pool_move_to_front(shared.pool, o->job);
    }
    if (!o->glyph && (!o->frames || enlarge)) {
        /* Never replace canonical pixels while an encoder still borrows them.
         * Retain existing graphics until the larger decode is published. */
        if (!object_busy(o)) {
            size_t raster = image_frame_bytes(needed_width, needed_height);
            size_t workspace = (size_t)needed_width * needed_height * 4 + IMAGE_PIXEL_PADDING;
            size_t reserve = (raster > ANIMATION_BYTES ? raster : ANIMATION_BYTES) + sizeof(animation) + workspace;
            task *t = task_new(r->cache, reserve);
            bool static_only = false;
            if (!t) { reserve = raster + sizeof(animation) + workspace; t = task_new(r->cache, reserve); static_only = true; }
            if (t) {
                t->kind = TASK_DECODE; t->priority = r->modal; t->object = o; object_ref(o);
                t->path = strdup(o->path);
                t->request = (image_decode_request){.path = t->path, .width = decode_width, .height = decode_height, .native = true,
                    .decode_only = true, .retain_static = true, .static_only = static_only};
                if (!t->path) { object_unref(o); free(t); return NULL; }
                o->job = t; task_push(t); ++shared.stats.decodes;
            }
        }
        if (!o->frames) return NULL;
    }
    if (request.native) {
        request.width = needed_width; request.height = needed_height;
    }
    if (request.terminal.protocol == GRAPHICS_KITTY) request.top = request.bottom = 0;
    variant *v;
    for (v = o->variants; v; v = v->next) if (equal_request(&v->request, &request)) break;
    if (!v) {
        if (!make_room(sizeof(*v), 0)) return NULL;
        v = calloc(1, sizeof(*v));
        if (!v) return NULL;
        v->request = request; v->request.path = NULL; v->request.animation = NULL;
        v->request.preview = false; v->request.image_id = graphics_image_next_id();
        shared.stats.memory += sizeof(*v);
        v->next = o->variants; o->variants = v;
    }
    v->pinned = true; v->used = now;
    if (r->visible_refs) v->visible = true;
    if (v->job && r->modal) {
        if (!v->job->submitted) v->job->priority = true;
        else g_thread_pool_move_to_front(shared.pool, v->job);
    }
    size_t frame = animation_frame_at(o->frames, o->epoch, now);
    if (!o->job && !enlarge && !v->job && !v->failed && (!v->image.data || (frame != v->frame && now - v->presented >= 67))) {
        size_t workspace = (size_t)request.width * request.height * 7 + IMAGE_PIXEL_PADDING * 2;
        if (o->frames && request.cover) {
            double scale = fmax((double)request.width / o->frames->source_width, (double)request.height / o->frames->source_height);
            double pixels = fmax(1, o->frames->source_width * scale) * fmax(1, o->frames->source_height * scale);
            if (pixels > GRAPHICS_IMAGE_PIXELS) return NULL;
            workspace += (size_t)pixels * 4;
        }
        task *t = task_new(r->cache, graphics_image_capacity(request.width, request.height) + workspace);
        if (t) {
            t->kind = TASK_ENCODE; t->object = o; object_ref(o); t->variant = v; t->priority = r->modal;
            if (o->glyph) t->glyph = strdup(o->glyph);
            if (o->glyph && !t->glyph) { object_unref(o); free(t); return NULL; }
            t->frames = o->frames; animation_ref(t->frames);
            t->request = v->request; t->request.animation = t->frames; t->request.frame = animation_frame_at(t->frames, o->epoch, now);
            v->job = t; task_push(t); ++shared.stats.encodes;
        }
    }
    return v->image.data ? &v->image : NULL;
}
const graphics_image *media_resource_fallback_image(media_resource *r, image_decode_request request)
{
    image_object *o = r ? r->objects[0] : NULL;
    if (!o) return NULL;
    if (o->frames && request.max_side > 0 && (o->frames->source_width > request.max_side || o->frames->source_height > request.max_side)) return NULL;
    if (request.terminal.protocol == GRAPHICS_KITTY) request.top = request.bottom = 0;
    for (variant *v = o->variants; v; v = v->next) {
        image_decode_request candidate = request;
        candidate.background = v->request.background;
        if (v->image.data && equal_request(&candidate, &v->request)) {
            v->pinned = true; v->used = r->cache->now;
            if (r->visible_refs) v->visible = true;
            return &v->image;
        }
    }
    return NULL;
}
const char *media_resource_path(const media_resource *r) { return r ? r->video_path : NULL; }
const char *media_resource_error(const media_resource *r, bool author)
{
    if (!r) return "Memória insuficiente para carregar a mídia.";
    if (r->state == RESOURCE_ERROR) return r->error;
    if (r->state != RESOURCE_READY || r->video) return NULL;
    image_object *o = r->objects[author ? 1 : 0];
    if (o) for (variant *v = o->variants; v; v = v->next)
        if (v->pinned && v->failed) return "Não foi possível preparar a imagem. Tente novamente.";
    return !o ? "Imagem do embed indisponível." : o->failed ?
        (o->error ? o->error : "Imagem inválida, grande demais ou não suportada.") : NULL;
}
bool media_resource_dimensions(const media_resource *r, bool author, int *width, int *height)
{
    image_object *o = r ? r->objects[author ? 1 : 0] : NULL;
    if (!o || !o->frames) return false;
    *width = o->frames->source_width; *height = o->frames->source_height; return true;
}
bool media_resource_animated(const media_resource *r, bool author)
{
    image_object *o = r ? r->objects[author ? 1 : 0] : NULL;
    return o && o->frames && o->frames->count > 1;
}
const void *media_resource_object(const media_resource *r, bool author) { return r ? r->objects[author ? 1 : 0] : NULL; }
void media_cache_get_stats(media_cache_stats *stats)
{
    if (!stats) return;
    *stats = shared.stats; stats->workers = atomic_load(&workers); stats->peak_workers = atomic_load(&peak_workers);
    stats->reclaim_pending = shared.released;
    stats->queued = shared.pool ? queued_tasks() : 0;
}

bool media_cache_reserve(size_t bytes)
{
    if (!make_room(bytes, 0)) return false;
    shared.stats.reserved += bytes; return true;
}
void media_cache_unreserve(size_t bytes) { shared.stats.reserved -= bytes; }
