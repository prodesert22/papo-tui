#define _POSIX_C_SOURCE 200809L
#include "ui/media.h"
#include "ui/graphics.h"
#include "ui/image_decode.h"
#include "ui/media_layout.h"
#include "ui/media_cache.h"
#include "ui/button.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "net/media.h"
#include "net/clock.h"
#include "ui/media_backend.h"
#include <pthread.h>
#include <math.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#define CACHE_COUNT 64
#define VISIBLE_COUNT 32
#define VIDEO_RESERVATION (12U * 1024U * 1024U)
typedef enum { ENTRY_NEW, ENTRY_READY, ENTRY_ERROR } entry_state;
typedef struct {
    message_media item;
    entry_state state;
    media_resource *resource;
    graphics_image image; /* Borrowed snapshot; cache owns its buffers. */
    uint64_t used;
    uint32_t placement_id;
    const char *error;
} cache_entry;
typedef struct {
    int entry, y, x, width, height, top, bottom;
    graphics_image image; /* Borrowed raster protected by resource + a cache pin. */
    media_resource *resource; /* Owned position reference. */
} placement;
typedef struct {
    char *path;
    uint64_t token;
    int width, height;
    bool video;
    graphics_terminal terminal;
    uint32_t image_id;
} job;
struct media_view {
    auth_client *auth;
    media_cache *store;
    cache_entry cache[CACHE_COUNT];
    placement visible[VISIBLE_COUNT];
    int visible_count, viewport_rows;
    uint64_t serial, tick, generation, history_revision, dimensions_revision;
    graphics_terminal terminal;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    bool started, stopping, pending, result_ready;
    atomic_uint_fast64_t active;
    atomic_bool player_idle;
    bool video_reserved;
    job task;
    graphics_image result;
    bool modal_shared, modal_animated;
    uint64_t result_token;
    const char *result_error;
    int pause_commands, mute_commands, seek_commands, volume_commands;
    bool playing, paused, muted, audio_missing, status_changed;
    double position, duration, volume;
    enum { MODAL_NONE, MODAL_LIST, MODAL_LOADING, MODAL_PLAYING, MODAL_ERROR } modal;
    WINDOW *window;
    media_modal_layout layout;
    bool info_open, info_focused;
    int info_scroll, info_rows;
    int selected, modal_entry, modal_width, modal_height;
    message_media selected_item;
    uint64_t modal_token;
    graphics_image modal_image;
    const char *modal_error;
};
static void delete_entry(cache_entry *e)
{
    media_resource_release(e->resource); memset(e, 0, sizeof(*e));
}
static bool ensure_resource(media_view *v, cache_entry *e)
{
    if (!e->resource) e->resource = media_cache_acquire(v->store, e->item.kind, e->item.id, e->item.version, e->item.video);
    return e->resource != NULL;
}
static void free_job(job *j) { free(j->path); memset(j, 0, sizeof(*j)); }
static void clear_modal_image(media_view *v)
{
    if (!v->modal_shared) graphics_image_free(&v->modal_image);
    else memset(&v->modal_image, 0, sizeof(v->modal_image));
    v->modal_shared = false;
}
static void publish(media_view *v, uint64_t token, graphics_image *image, animation *animated, const char *error)
{
    pthread_mutex_lock(&v->lock);
    (void)animated;
    graphics_image_free(&v->result); v->result = *image; memset(image, 0, sizeof(*image));
    v->result_token = token; v->result_error = error; v->result_ready = true;
    pthread_mutex_unlock(&v->lock);
}
static void *worker(void *context)
{
    media_view *v = context;
    job current = {0};
    void *player = NULL;
    const video_player_api *api = NULL;
    uint64_t last_frame = 0;
    bool force_frame = false;
    for (;;) {
        pthread_mutex_lock(&v->lock);
        bool stop = v->stopping;
        bool pending = v->pending;
        job next = {0};
        if (pending) { next = v->task; memset(&v->task, 0, sizeof(v->task)); v->pending = false; }
        int pause = v->pause_commands, mute = v->mute_commands, seek = v->seek_commands, volume = v->volume_commands;
        v->pause_commands = v->mute_commands = v->seek_commands = v->volume_commands = 0;
        pthread_mutex_unlock(&v->lock);
        if (stop) { free_job(&next); break; }
        bool reuse = player && pending && next.video && !strcmp(current.path, next.path);
        if (player && !reuse && (pending || atomic_load(&v->active) != current.token)) {
            api->close(player); player = NULL;
            free_job(&current); atomic_store(&v->player_idle, true);
        }
        if (pending) {
            if (atomic_load(&v->active) != next.token) { free_job(&next); continue; }
            atomic_store(&v->player_idle, false);
            if (reuse) { free_job(&current); current = next; last_frame = 0; force_frame = true; }
            else {
            current = next;
            force_frame = true;
            api = media_video_backend();
            player = api ? api->open(current.path) : NULL;
            if (!player) {
                graphics_image empty = {0};
                publish(v, current.token, &empty, NULL, api ? "Não foi possível iniciar o player." :
                    "Módulo de vídeo ausente ou incompatível. Reinstale o aplicativo.");
                free_job(&current); atomic_store(&v->player_idle, true); continue;
            }
            }
        }
        if (player) {
            video_controls controls = {pause, mute, seek, volume};
            video_info info;
            api->update(player, &controls, &info);
            if (info.invalid) {
                graphics_image empty = {0}; publish(v, current.token, &empty, NULL, "Vídeo inválido ou codec indisponível.");
            }
            pthread_mutex_lock(&v->lock);
            v->status_changed |= v->paused != info.paused || v->muted != info.muted ||
                v->audio_missing != info.audio_missing || fabs(v->position - info.position) >= 1 || v->volume != info.volume;
            v->audio_missing = info.audio_missing;
            v->playing = true; v->paused = info.paused; v->muted = info.muted;
            v->position = info.position; v->duration = info.duration; v->volume = info.volume;
            pthread_mutex_unlock(&v->lock);
            uint64_t now = 0;
            if (net_now_ms(&now) && now - last_frame >= 67 && (force_frame || api->changed(player))) {
                last_frame = now; force_frame = false;
                size_t pixels = (size_t)current.width * (size_t)current.height;
                unsigned char *rgba = malloc(pixels * 4), *rgb = malloc(pixels * 3);
                graphics_image image = {0};
                if (rgba && rgb) {
                    if (api->render(player, rgba, current.width, current.height)) {
                        for (size_t i = 0; i < pixels; ++i) memcpy(rgb + i * 3, rgba + i * 4, 3);
                        if (graphics_image_encode_id(rgb, current.width, current.height, current.terminal, current.image_id, &image)) publish(v, current.token, &image, NULL, NULL);
                    }
                }
                free(rgba); free(rgb);
            }
        }
        pthread_mutex_lock(&v->lock);
        if (!v->pending && !v->stopping) {
            if (!player) pthread_cond_wait(&v->wake, &v->lock);
            else {
                struct timespec deadline; clock_gettime(CLOCK_REALTIME, &deadline);
                deadline.tv_nsec += 67000000;
                if (deadline.tv_nsec >= 1000000000) { deadline.tv_nsec -= 1000000000; ++deadline.tv_sec; }
                pthread_cond_timedwait(&v->wake, &v->lock, &deadline);
            }
        }
        pthread_mutex_unlock(&v->lock);
    }
    if (player) api->close(player);
    free_job(&current); atomic_store(&v->player_idle, true); return NULL;
}
static bool submit_job(media_view *v, const job *request)
{
    char *copy = strdup(request->path);
    if (!copy) return false;
    if (!v->started) {
        if (pthread_create(&v->thread, NULL, worker, v)) { free(copy); return false; }
        v->started = true;
    }
    pthread_mutex_lock(&v->lock);
    free_job(&v->task);
    v->task = *request; v->task.path = copy;
    v->task.terminal = graphics_terminal_get();
    if (!v->task.image_id) v->task.image_id = graphics_image_next_id();
    v->pending = true; atomic_store(&v->active, request->token); pthread_cond_signal(&v->wake);
    pthread_mutex_unlock(&v->lock); return true;
}
static bool native_media(const message_media *item)
{
    return item->full_width || !strcmp(item->role, "attachment") || !strcmp(item->role, "image");
}

media_view *media_view_create(auth_client *auth)
{
    media_view *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->auth = auth; v->modal_entry = -1;
    v->generation = auth_context_generation(auth);
    v->store = media_cache_create(auth);
    if (!v->store) { free(v); return NULL; }
    if (pthread_mutex_init(&v->lock, NULL)) { media_cache_destroy(v->store); free(v); return NULL; }
    if (pthread_cond_init(&v->wake, NULL)) { pthread_mutex_destroy(&v->lock); media_cache_destroy(v->store); free(v); return NULL; }
    atomic_init(&v->active, 0); atomic_init(&v->player_idle, true);
    return v;
}
static void close_modal(media_view *v)
{
    if (!v) return;
    atomic_store(&v->active, 0);
    pthread_mutex_lock(&v->lock);
    v->playing = false; v->pause_commands = v->seek_commands = v->volume_commands = v->mute_commands = 0;
    pthread_cond_signal(&v->wake); pthread_mutex_unlock(&v->lock);
    if (v->window) { delwin(v->window); v->window = NULL; }
    v->modal_animated = false; clear_modal_image(v); v->modal = MODAL_NONE; v->modal_entry = -1;
    v->modal_token = 0; v->modal_error = NULL;
    v->info_open = v->info_focused = false; v->info_scroll = v->info_rows = 0;
    v->modal_width = v->modal_height = 0;
}
static void clear_positions(media_view *v)
{
    for (int i = 0; i < v->visible_count; ++i) media_resource_release(v->visible[i].resource);
    v->visible_count = 0;
}
void media_view_reset(media_view *v)
{
    if (!v) return;
    if (v->visible_count || v->modal != MODAL_NONE) graphics_images_clear();
    close_modal(v); clear_positions(v);
    for (int i = 0; i < CACHE_COUNT; ++i) delete_entry(&v->cache[i]);
    media_cache_reset(v->store);
    v->visible_count = 0; v->generation = auth_context_generation(v->auth);
}
void media_view_suspend(media_view *v)
{
    if (!v) return;
    graphics_images_clear(); close_modal(v); clear_positions(v);
    for (int i = 0; i < CACHE_COUNT; ++i) delete_entry(&v->cache[i]);
    v->visible_count = 0;
    media_cache_begin(v->store);
    uint64_t now = 0; net_now_ms(&now); media_cache_poll(v->store, now);
}
uint64_t media_view_dimensions_revision(const media_view *v) { return v ? v->dimensions_revision : 0; }
void media_view_idle(media_view *v)
{
    if (!v || v->visible_count || v->modal != MODAL_NONE) return;
    uint64_t now = 0; net_now_ms(&now); media_cache_poll(v->store, now);
    pthread_mutex_lock(&v->lock);
    if (!atomic_load(&v->active) && atomic_load(&v->player_idle)) {
        graphics_image_free(&v->result); v->result_ready = false;
        if (v->video_reserved) { media_cache_unreserve(VIDEO_RESERVATION); v->video_reserved = false; }
    }
    pthread_mutex_unlock(&v->lock);
}
void media_view_destroy(media_view *v)
{
    if (!v) return;
    pthread_mutex_lock(&v->lock); v->stopping = true; atomic_store(&v->active, 0);
    pthread_cond_signal(&v->wake); pthread_mutex_unlock(&v->lock);
    if (v->started) pthread_join(v->thread, NULL);
    media_view_reset(v); free_job(&v->task); graphics_image_free(&v->result);
    pthread_cond_destroy(&v->wake); pthread_mutex_destroy(&v->lock);
    if (v->video_reserved) media_cache_unreserve(VIDEO_RESERVATION);
    media_cache_destroy(v->store); free(v);
}
void media_view_update(media_view *v, const json_t *event)
{
    if (!v || v->generation != auth_context_generation(v->auth)) return;
    const char *type = json_string_value(json_object_get(event, "type"));
    const char *message = json_string_value(json_object_get(event, "message_id"));
    json_t *embeds = json_object_get(event, "embeds");
    if (!type || strcmp(type, "message_embeds_update") || !history_uuid(message) ||
        (!json_is_null(embeds) && !json_is_array(embeds)) || json_array_size(embeds) > 10) return;
    for (size_t i = 0; i < json_array_size(embeds); ++i) {
        const char *id = json_string_value(json_object_get(json_array_get(embeds, i), "id"));
        if (!history_uuid(id)) continue;
        media_cache_invalidate(v->store, AUTH_MEDIA_EMBED, id); media_cache_invalidate(v->store, AUTH_MEDIA_VIDEO, id);
    }
    for (int i = 0; i < CACHE_COUNT; ++i) {
        cache_entry *e = &v->cache[i];
        if (e->item.kind != AUTH_MEDIA_EMBED && e->item.kind != AUTH_MEDIA_VIDEO) continue;
        bool affected = !strcmp(e->item.message, message);
        for (size_t j = 0; !affected && j < json_array_size(embeds); ++j) {
            const char *id = json_string_value(json_object_get(json_array_get(embeds, j), "id"));
            affected = id && !strcmp(e->item.id, id);
        }
        if (!affected) continue;
        if (v->modal_entry == i) close_modal(v);
        media_cache_invalidate(v->store, e->item.kind, e->item.id); delete_entry(e);
    }
    ++v->dimensions_revision;
}
static bool same(const message_media *a, const message_media *b)
{
    return a->kind == b->kind && !strcmp(a->id, b->id) && !strcmp(a->role, b->role) && !strcmp(a->message, b->message) && a->version == b->version && a->position == b->position;
}
static int cache_find(media_view *v, const message_media *item)
{
    int oldest = -1;
    for (int i = 0; i < CACHE_COUNT; ++i) {
        if (*v->cache[i].item.id && same(&v->cache[i].item, item)) { v->cache[i].item = *item; return i; }
        if (i == v->modal_entry) continue;
        bool visible = false;
        for (int j = 0; j < v->visible_count; ++j) visible |= v->visible[j].entry == i;
        if (!visible && (oldest < 0 || v->cache[i].used < v->cache[oldest].used)) oldest = i;
    }
    if (oldest >= 0) { delete_entry(&v->cache[oldest]); v->cache[oldest].item = *item; v->cache[oldest].placement_id = graphics_image_next_id(); }
    return oldest;
}
static void geometry(int columns, int rows, int *w, int *h)
{
    graphics_terminal t = graphics_terminal_get();
    double width = fmax(1, (double)columns * t.cell_width), height = fmax(1, (double)rows * t.cell_height);
    double ratio = fmin(1, sqrt(GRAPHICS_IMAGE_PIXELS / (width * height)));
    *w = (int)fmax(1, width * ratio); *h = (int)fmax(1, height * ratio);
}
void media_view_viewport(media_view *v, int rows)
{
    if (!v) return;
    /* Every message ends with a separator row. Keep a fitted attachment fully
     * visible at the bottom of history instead of clipping its first row. */
    if (rows > 1) --rows;
    if (rows < 1) rows = 1;
    if (v->viewport_rows != rows) { v->viewport_rows = rows; ++v->dimensions_revision; }
}
void media_view_measure(void *context, const message_media *item, int available_width, int *width, int *height)
{
    media_view *v = context;
    if (!v || !native_media(item)) return;
    graphics_terminal t = graphics_terminal_get();
    if (!t.supported) return;
    int source_width = item->source_width, source_height = item->source_height;
    for (int i = 0; i < CACHE_COUNT; ++i) {
        cache_entry *e = &v->cache[i];
        if (!same(&e->item, item) || !e->image.source_width || !e->image.source_height) continue;
        source_width = e->image.source_width; source_height = e->image.source_height; break;
    }
    if (source_width < 1 || source_height < 1) return;
    int max_w, max_h; geometry(available_width, v->viewport_rows ? v->viewport_rows : 6, &max_w, &max_h);
    double scale = fmin(1, fmin((double)max_w / source_width, (double)max_h / source_height));
    int w = (int)fmax(1, source_width * scale), h = (int)fmax(1, source_height * scale);
    *width = (w + t.cell_width - 1) / t.cell_width;
    *height = (h + t.cell_height - 1) / t.cell_height;
}
void media_view_frame(media_view *v) { if (v) graphics_frame_begin(); }
void media_view_begin(media_view *v) { if (v) { graphics_frame_begin(); clear_positions(v); ++v->tick; } }
void media_view_offer(media_view *v, const message_media *item, int y, int x, int width, int height, int top, int bottom)
{
    if (!v || v->visible_count == VISIBLE_COUNT) return;
    int index = cache_find(v, item);
    if (index < 0) return;
    cache_entry *e = &v->cache[index]; e->used = v->tick;
    placement *p = &v->visible[v->visible_count++];
    *p = (placement){.entry = index, .y = y, .x = x, .width = width, .height = height, .top = top, .bottom = bottom};
    p->resource = e->resource; media_resource_ref(p->resource);
    graphics_terminal t = graphics_terminal_get();
    if (e->resource && !e->item.video && t.supported && height > 0) {
        int w, h; geometry(width, native_media(item) && v->viewport_rows ? v->viewport_rows : height, &w, &h);
        uint64_t now = 0; net_now_ms(&now);
        image_decode_request request = {.width = w, .height = h, .native = native_media(item), .terminal = t,
            .top = top * t.cell_height, .bottom = bottom * t.cell_height};
        const graphics_image *image = media_resource_image(e->resource, !strcmp(item->role, "author"), request, now);
        if (image) {
            if (e->image.source_width != image->source_width || e->image.source_height != image->source_height) ++v->dimensions_revision;
            p->image = *image; e->image = *image; e->state = ENTRY_READY;
        }
    }
}
static bool live(history *h, const message_media *item)
{
    for (size_t i = 0; i < history_message_count(h); ++i) {
        json_t *m = history_message(h, i);
        const char *id = json_string_value(json_object_get(m, "id"));
        if (!id || strcmp(id, item->message)) continue;
        json_t *list = json_object_get(m, item->kind == AUTH_MEDIA_ATTACHMENT || item->kind == AUTH_MEDIA_THUMBNAIL ? "attachments" : "embeds");
        size_t n; json_t *entry;
        json_array_foreach(list, n, entry) {
            id = json_string_value(json_object_get(entry, "id"));
            if (id && !strcmp(id, item->id) && message_media_version(entry) == item->version) return true;
        }
        return false;
    }
    return false;
}
static bool modal_layout_update(media_view *v)
{
    media_modal_layout layout;
    if (!media_modal_geometry(COLS, LINES, v->info_open && v->modal != MODAL_LIST,
                              v->modal_image.columns, v->modal_image.rows, &layout)) return false;
    int width, height; geometry(layout.area_width, layout.area_height, &width, &height);
    if (v->selected_item.video) {
        if (width > 640) width = 640;
        if (height > 360) height = 360;
    }
    if (v->modal_token && (width != v->modal_width || height != v->modal_height)) {
        bool reload = v->selected_item.video || !v->modal_image.data;
        if (v->modal_image.data && !v->selected_item.video) {
            int source_w = v->modal_image.source_width, source_h = v->modal_image.source_height;
            if (source_w > 0 && source_h > 0) {
                double scale = fmin(1, fmin((double)width / source_w, (double)height / source_h));
                int w = (int)(source_w * scale), h = (int)(source_h * scale);
                if (w < 1) w = 1;
                if (h < 1) h = 1;
                reload = v->modal_image.width != w || v->modal_image.height != h;
            } else reload = v->modal_image.width > width || v->modal_image.height > height;
        }
        if (reload) {
            v->modal_token = 0; clear_modal_image(v);
            if (v->modal == MODAL_PLAYING) v->modal = MODAL_LOADING;
        }
    }
    if (v->window && (getmaxx(v->window) != layout.width || getmaxy(v->window) != layout.height ||
                      getbegx(v->window) != layout.x || getbegy(v->window) != layout.y)) {
        delwin(v->window); v->window = NULL;
    }
    v->modal_width = width; v->modal_height = height; v->layout = layout;
    return true;
}
bool media_view_poll(media_view *v, history *h)
{
    if (!v) return false;
    if (v->generation != auth_context_generation(v->auth) || !auth_has_session(v->auth)) {
        bool changed = v->visible_count || v->modal != MODAL_NONE;
        media_view_reset(v); return changed;
    }
    uint64_t now = 0; net_now_ms(&now);
    graphics_terminal terminal = graphics_terminal_get();
    bool changed = terminal.protocol != v->terminal.protocol || terminal.cell_width != v->terminal.cell_width ||
                   terminal.cell_height != v->terminal.cell_height || terminal.supported != v->terminal.supported;
    if (changed) ++v->dimensions_revision;
    if (changed && v->modal_token) { v->modal_token = 0; clear_modal_image(v); }
    v->terminal = terminal;
    /* Validate associations only after model changes, not on every keystroke. */
    uint64_t revision = history_messages_revision(h);
    if (revision != v->history_revision) {
        v->history_revision = revision;
        for (int i = 0; i < CACHE_COUNT; ++i) if (*v->cache[i].item.id && !live(h, &v->cache[i].item)) {
            if (v->modal_entry == i) close_modal(v);
            delete_entry(&v->cache[i]); changed = true;
        }
    }
    int count = 0;
    for (int i = 0; i < v->visible_count; ++i)
        if (*v->cache[v->visible[i].entry].item.id) v->visible[count++] = v->visible[i];
        else media_resource_release(v->visible[i].resource);
    v->visible_count = count;
    if (v->modal == MODAL_LIST && v->selected >= count) v->selected = count ? count - 1 : 0;
    media_cache_begin(v->store);
    bool modal = v->modal != MODAL_NONE && v->modal != MODAL_LIST && v->modal_entry >= 0;
    if (modal) {
        cache_entry *e = &v->cache[v->modal_entry];
        ensure_resource(v, e); media_resource_use_image(e->resource, !strcmp(e->item.role, "author"), true);
        media_resource_keep_image(e->resource, !strcmp(e->item.role, "author"), &v->modal_image);
    }
    if (terminal.supported) for (int i = 0; i < v->visible_count; ++i) {
        cache_entry *e = &v->cache[v->visible[i].entry];
        if (!e->item.video) {
            ensure_resource(v, e); media_resource_use_image(e->resource, !strcmp(e->item.role, "author"), false);
            placement *p = &v->visible[i];
            if (p->resource != e->resource) {
                media_resource_release(p->resource); p->resource = e->resource; media_resource_ref(p->resource);
            }
            media_resource_keep_image(e->resource, !strcmp(e->item.role, "author"), &p->image);
        }
    }
    changed |= media_cache_poll(v->store, now);
    pthread_mutex_lock(&v->lock);
    if (v->result_ready) {
        if (v->modal_token && v->result_token == v->modal_token) {
            if (v->result_error) { v->modal_error = v->result_error; v->modal = MODAL_ERROR; }
            else { clear_modal_image(v); v->modal_image = v->result; memset(&v->result, 0, sizeof(v->result)); v->modal = MODAL_PLAYING; }
            changed = true;
        }
        graphics_image_free(&v->result); v->result_ready = false;
    }
    changed |= v->status_changed; v->status_changed = false;
    if (!atomic_load(&v->active) && atomic_load(&v->player_idle) && v->video_reserved) {
        media_cache_unreserve(VIDEO_RESERVATION); v->video_reserved = false;
    }
    pthread_mutex_unlock(&v->lock);
    if (modal && v->modal != MODAL_ERROR && modal_layout_update(v)) {
        cache_entry *e = &v->cache[v->modal_entry];
        bool author = !strcmp(e->item.role, "author");
        const char *error = media_resource_error(e->resource, author);
        if (error) { v->modal_error = error; v->modal = MODAL_ERROR; return true; }
        if (e->item.video) {
            const char *path = media_resource_path(e->resource);
            if (path && !v->modal_token && v->modal_width > 0 && v->modal_height > 0) {
                if (!v->video_reserved) {
                    if (!media_cache_reserve(VIDEO_RESERVATION)) return changed;
                    v->video_reserved = true;
                }
                v->modal_token = ++v->serial;
                job request = {.path = (char *)path, .token = v->modal_token, .width = v->modal_width,
                    .height = v->modal_height, .video = true, .image_id = v->modal_image.id};
                if (!submit_job(v, &request)) { v->modal_error = "Não foi possível preparar o player."; v->modal = MODAL_ERROR; }
                changed = true;
            }
        } else {
            image_decode_request request = {.width = v->modal_width, .height = v->modal_height, .native = true, .terminal = terminal};
            const graphics_image *image = media_resource_image(e->resource, author, request, now);
            if (image) {
                changed |= v->modal_image.revision != image->revision;
                clear_modal_image(v); v->modal_image = *image; v->modal_shared = true;
                v->modal_animated = media_resource_animated(e->resource, author); v->modal = MODAL_PLAYING;
            }
        }
        return changed;
    }
    if (v->modal != MODAL_NONE || !terminal.supported) return changed;
    for (int i = 0; i < v->visible_count; ++i) {
        placement *p = &v->visible[i]; cache_entry *e = &v->cache[p->entry];
        if (e->item.video || p->height < 1) continue;
        int source_w = 0, source_h = 0;
        if (media_resource_dimensions(e->resource, !strcmp(e->item.role, "author"), &source_w, &source_h) &&
            (source_w != e->image.source_width || source_h != e->image.source_height)) {
            e->image.source_width = source_w; e->image.source_height = source_h; ++v->dimensions_revision; changed = true;
        }
        int w, height; geometry(p->width, native_media(&e->item) && v->viewport_rows ? v->viewport_rows : p->height, &w, &height);
        image_decode_request request = {.width = w, .height = height, .native = native_media(&e->item), .terminal = terminal,
            .top = p->top * terminal.cell_height, .bottom = p->bottom * terminal.cell_height};
        const graphics_image *image = media_resource_image(e->resource, !strcmp(e->item.role, "author"), request, now);
        const char *error = media_resource_error(e->resource, !strcmp(e->item.role, "author"));
        entry_state state = error ? ENTRY_ERROR : image ? ENTRY_READY : ENTRY_NEW;
        changed |= e->state != state || p->image.revision != (image ? image->revision : 0);
        e->state = state; e->error = error;
        p->image = image ? *image : (graphics_image){0};
        if (image) {
            if (e->image.source_width != image->source_width || e->image.source_height != image->source_height) ++v->dimensions_revision;
            e->image = *image;
        }
    }
    return changed;
}
static void select_item(media_view *v, int index)
{
    if (index < 0 || index >= v->visible_count) return;
    message_media item = v->cache[v->visible[index].entry].item;
    close_modal(v);
    if (item.kind == AUTH_MEDIA_THUMBNAIL) item.kind = AUTH_MEDIA_ATTACHMENT;
    v->selected_item = item;
    v->modal_entry = cache_find(v, &item);
    v->modal = MODAL_LOADING;
    if (v->modal_entry < 0 || !graphics_terminal_get().supported) { v->modal_error = "Este terminal não oferece um protocolo gráfico compatível."; v->modal = MODAL_ERROR; }
    else if (v->cache[v->modal_entry].state == ENTRY_ERROR) { media_resource_retry(v->cache[v->modal_entry].resource); delete_entry(&v->cache[v->modal_entry]); v->cache[v->modal_entry].item = v->selected_item; }
}
bool media_view_modal(const media_view *v) { return v && v->modal != MODAL_NONE; }
bool media_view_key(media_view *v, int type, wint_t key)
{
    if (!v) return false;
    if (!media_view_modal(v)) {
        if (type != KEY_CODE_YES || key != KEY_F(7)) return false;
        if (!v->visible_count) { v->modal = MODAL_ERROR; v->modal_error = "Nenhuma mídia disponível neste trecho do histórico."; }
        else if (v->visible_count == 1) select_item(v, 0);
        else { v->modal = MODAL_LIST; v->selected = 0; }
        return true;
    }
    if (type == OK && key == 27) { close_modal(v); return true; }
    if (v->modal == MODAL_LIST) {
        if (type == KEY_CODE_YES && key == KEY_UP && v->selected) --v->selected;
        if (type == KEY_CODE_YES && key == KEY_DOWN && v->selected + 1 < v->visible_count) ++v->selected;
        if ((type == OK && (key == '\n' || key == '\r')) || (type == KEY_CODE_YES && key == KEY_ENTER)) select_item(v, v->selected);
        return true;
    }
    if (v->modal != MODAL_LIST) {
        bool toggle = (type == OK && key == 'i') || (type == KEY_CODE_YES && key == KEY_F(8));
        if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
            v->info_focused = !v->info_focused; return true;
        }
        if (v->info_focused && ((type == OK && (key == '\n' || key == '\r')) ||
                               (type == KEY_CODE_YES && key == KEY_ENTER))) toggle = true;
        if (toggle) {
            v->info_open = !v->info_open; v->info_scroll = 0;
            if (key == 'i' || key == KEY_F(8)) v->info_focused = false;
            return true;
        }
        if (v->info_open && type == KEY_CODE_YES && (key == KEY_PPAGE || key == KEY_NPAGE)) {
            media_view_scroll(v, v->layout.y + 2, v->layout.info_x, key == KEY_PPAGE ? -5 : 5);
            return true;
        }
    }
    if (v->modal == MODAL_ERROR && ((type == OK && (key == '\r' || key == '\n')) ||
                                  (type == KEY_CODE_YES && key == KEY_ENTER)) &&
        v->modal_entry >= 0 && graphics_terminal_get().supported) {
        media_resource_retry(v->cache[v->modal_entry].resource);
        delete_entry(&v->cache[v->modal_entry]); v->cache[v->modal_entry].item = v->selected_item;
        v->modal = MODAL_LOADING; v->modal_token = 0; return true;
    }
    pthread_mutex_lock(&v->lock);
    if (type == OK && key == ' ') ++v->pause_commands;
    if (type == OK && key == 'm') ++v->mute_commands;
    if (type == KEY_CODE_YES && key == KEY_LEFT && v->seek_commands > -3600) v->seek_commands -= 5;
    if (type == KEY_CODE_YES && key == KEY_RIGHT && v->seek_commands < 3600) v->seek_commands += 5;
    if (type == KEY_CODE_YES && key == KEY_UP && v->volume_commands < 100) v->volume_commands += 5;
    if (type == KEY_CODE_YES && key == KEY_DOWN && v->volume_commands > -100) v->volume_commands -= 5;
    pthread_cond_signal(&v->wake); pthread_mutex_unlock(&v->lock);
    return true;
}
bool media_view_scroll(media_view *v, int y, int x, int delta)
{
    if (!v || !media_view_modal(v) || !v->info_open || !modal_layout_update(v) ||
        x < v->layout.info_x || x >= v->layout.info_x + v->layout.info_width ||
        y <= v->layout.y || y >= v->layout.y + v->layout.height - 1) return false;
    int maximum = v->info_rows - (v->layout.height - 4);
    if (maximum < 0) maximum = 0;
    if (delta < 0) v->info_scroll = v->info_scroll < -delta ? 0 : v->info_scroll + delta;
    else v->info_scroll = delta > maximum - v->info_scroll ? maximum : v->info_scroll + delta;
    return true;
}
bool media_view_click(media_view *v, int y, int x)
{
    if (!v) return false;
    if (media_view_modal(v)) {
        if (!modal_layout_update(v) || !media_modal_contains(&v->layout, y, x)) { close_modal(v); return true; }
        if (v->modal == MODAL_LIST) {
            int row = y - v->layout.y - 2;
            int first = v->selected >= v->layout.height - 5 ? v->selected - (v->layout.height - 6) : 0;
            if (x > v->layout.x && x < v->layout.x + v->layout.width - 1 &&
                row >= 0 && row < v->layout.height - 4 && first + row < v->visible_count) select_item(v, first + row);
        } else if (y == v->layout.button_y && x >= v->layout.button_x &&
                   x < v->layout.button_x + v->layout.button_width) {
            v->info_open = !v->info_open; v->info_scroll = 0; v->info_focused = true;
        }
        return true;
    }
    for (int i = 0; i < v->visible_count; ++i) {
        placement *p = &v->visible[i];
        int height = p->height - p->top - p->bottom;
        if (height < 1) height = 1;
        if (x >= p->x && x < p->x + p->width && y >= p->y && y < p->y + height) { select_item(v, i); return true; }
    }
    return false;
}
static void draw_properties(media_view *v, WINDOW *w)
{
    if (!v->info_open) return;
    media_modal_layout *g = &v->layout;
    int left = g->info_x - g->x, width = g->info_width - 2, available = g->height - 4;
    if (width < 1 || available < 1) return;
    wattrset(w, COLOR_PAIR(PANEL_MUTED)); mvwvline(w, 1, left - 1, ACS_VLINE, g->height - 2);
    wattrset(w, COLOR_PAIR(PANEL_ACCENT) | A_BOLD); text_label(w, 1, left, g->info_width, "Propriedades");
    message_media *item = &v->selected_item;
    char bytes[64] = "Não informado", dimensions[64] = "Não informadas";
    if (item->size_known) snprintf(bytes, sizeof(bytes), "%" PRId64 " bytes", item->size_bytes);
    int source_w = v->modal_image.source_width ? v->modal_image.source_width : item->source_width;
    int source_h = v->modal_image.source_height ? v->modal_image.source_height : item->source_height;
    if (source_w > 0 && source_h > 0) snprintf(dimensions, sizeof(dimensions), "%d × %d px", source_w, source_h);
    struct { const char *name, *value; } properties[] = {
        {"Nome", item->original_file_name}, {"MIME", item->mime}, {"Tamanho", bytes}, {"Moderação", item->moderation},
        {"Dimensões originais", dimensions}, {"ID", item->id}, {"Mensagem", item->message},
        {"Miniatura", item->thumbnail_id}, {"Criado em", item->created_at}
    };
    /* First measure to clamp scrolling, then paint only the visible rows.
     * The shared Unicode renderer sanitizes every remote property value. */
    for (int pass = 0; pass < 2; ++pass) {
        int row = 0;
        for (size_t i = 0; i < sizeof(properties) / sizeof(*properties); ++i) {
            text_buffer buffer; text_layout layout;
            const char *value = *properties[i].value ? properties[i].value : "Não informado";
            if (!text_decode(&buffer, value)) text_decode(&buffer, "Não informado");
            text_measure(&buffer, width, &layout);
            int title = row - v->info_scroll;
            if (pass && title >= 0 && title < available) {
                wattrset(w, COLOR_PAIR(PANEL_MUTED) | A_BOLD);
                text_label(w, title + 2, left, width, properties[i].name);
            }
            ++row;
            int first = v->info_scroll > row ? v->info_scroll - row : 0;
            int y = row > v->info_scroll ? row - v->info_scroll : 0;
            if (pass && y < available && first < layout.rows) {
                wattrset(w, COLOR_PAIR(PANEL)); text_draw(w, &buffer, &layout, first, available - y, y + 2, left);
            }
            row += layout.rows + 1;
        }
        v->info_rows = row;
        int maximum = row > available ? row - available : 0;
        if (v->info_scroll > maximum) v->info_scroll = maximum;
    }
    wattrset(w, COLOR_PAIR(PANEL_MUTED));
    text_label(w, g->height - 2, left, g->info_width, "PgUp/PgDn: rolar");
}

void media_view_draw(media_view *v)
{
    if (!v) return;
    if (!media_view_modal(v)) {
        for (int i = 0; i < v->visible_count; ++i) {
            placement *p = &v->visible[i]; cache_entry *e = &v->cache[p->entry];
            if (p->height - p->top - p->bottom < 1 || e->state == ENTRY_READY || e->item.video || p->y >= LINES || p->x >= COLS) continue;
            WINDOW *notice = newwin(1, p->width, p->y, p->x);
            if (notice) {
                wbkgd(notice, COLOR_PAIR(PANEL_MUTED));
                text_label(notice, 0, 0, p->width, e->state == ENTRY_ERROR ? e->error : "Carregando imagem…");
                wnoutrefresh(notice); delwin(notice);
            }
        }
        return;
    }
    if (!modal_layout_update(v)) return;
    int width = v->layout.width, height = v->layout.height;
    if (!v->window) v->window = newwin(height, width, v->layout.y, v->layout.x);
    if (!v->window) return;
    WINDOW *w = v->window;
    wbkgd(w, COLOR_PAIR(PANEL)); werase(w); wattrset(w, COLOR_PAIR(PANEL_MUTED)); box(w, 0, 0);
    wattrset(w, COLOR_PAIR(PANEL_ACCENT) | A_BOLD); text_label(w, 0, 2, width - 4, " Mídia · Esc: voltar ");
    wattrset(w, COLOR_PAIR(PANEL));
    if (v->modal == MODAL_LIST) {
        text_label(w, 1, 2, width - 4, "Escolha uma mídia · ↑↓ selecionar · Enter abrir");
        int first = v->selected >= height - 5 ? v->selected - (height - 6) : 0;
        for (int i = first; i < v->visible_count && i - first < height - 4; ++i) {
            wattrset(w, COLOR_PAIR(i == v->selected ? PANEL_ACCENT : PANEL));
            text_label(w, 2 + i - first, 2, width - 4, v->cache[v->visible[i].entry].item.label);
        }
    } else {
        int image_width = v->layout.area_width;
        int label_x = v->layout.button_x + v->layout.button_width + 2;
        int label_end = v->info_open ? v->layout.info_x - 1 : v->layout.x + width - 2;
        if (label_end > label_x)
            text_label(w, 1, label_x - v->layout.x, label_end - label_x, v->selected_item.label);
        button_draw(w, v->layout.button_y - v->layout.y, v->layout.button_x - v->layout.x,
                    v->layout.button_width, "[Info]",
                    v->info_focused ? BUTTON_FOCUSED : v->info_open ? BUTTON_SELECTED : BUTTON_NORMAL, BUTTON_PANEL);
        if (v->modal == MODAL_LOADING) {
            text_label(w, 3, 3, image_width, "Carregando mídia…");
        } else if (v->modal == MODAL_ERROR) {
            text_label(w, 3, 3, image_width, v->modal_error ? v->modal_error : "Mídia indisponível.");
            text_label(w, height - 3, 3, image_width, "Enter: tentar novamente");
        } else {
            char status[160];
            pthread_mutex_lock(&v->lock);
            snprintf(status, sizeof(status), "%s · %.0f / %.0f s · Volume %.0f%%%s", v->paused ? "Pausado" : "Reproduzindo", v->position, v->duration, v->volume, v->audio_missing ? " · Sem dispositivo de áudio" : v->muted ? " · Mudo" : "");
            pthread_mutex_unlock(&v->lock);
            text_label(w, height - 3, 3, image_width, v->selected_item.video ? status : v->modal_animated ? "GIF animado" : "Imagem");
        }
        text_label(w, height - 2, 3, image_width, v->selected_item.video ?
                   "Espaço pausa · ←→ 5s · ↑↓ volume · m" : "i: informações · Esc: voltar");
        draw_properties(v, w);
    }
    touchwin(w); wnoutrefresh(w);
}
void media_view_prepare(media_view *v)
{
    if (!v || !graphics_terminal_get().supported) return;
    if (media_view_modal(v)) {
        if (v->window && v->modal == MODAL_PLAYING && modal_layout_update(v)) {
            /* Recalculate after a decode result changes the raster dimensions. */
            graphics_image_place(&v->modal_image, v->layout.image_y, v->layout.image_x, 0, 0);
        }
        return;
    }
    for (int i = 0; i < v->visible_count; ++i) {
        placement *p = &v->visible[i]; cache_entry *e = &v->cache[p->entry];
        graphics_terminal t = graphics_terminal_get();
        if (e->state != ENTRY_READY || e->image.protocol != t.protocol) continue;
        const graphics_image *image = &p->image;
        graphics_image_place_id(image, e->placement_id, p->y, p->x,
            t.protocol == GRAPHICS_KITTY ? p->top * t.cell_height : 0,
            t.protocol == GRAPHICS_KITTY ? p->bottom * t.cell_height : 0);
    }
}
