#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/media/media.h"
#include "config/app_mode.h"
#include "config/player_volume.h"
#include "net/clock.h"
#include "net/http/download.h"
#include "ui/components/button.h"
#include "ui/components/theme.h"
#include "ui/input/shortcuts.h"
#include "ui/input/text.h"
#include "ui/media/graphics.h"
#include "ui/media/image_decode.h"
#include "ui/media/media_backend.h"
#include "ui/media/media_cache.h"
#include "ui/media/media_layout.h"
#include "ui/media/media_service.h"
#include <glib.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define CACHE_COUNT 64
#define VISIBLE_COUNT 32
#define HISTORY_IMAGE_HEIGHT 720
#define VIDEO_WORKSPACE_BYTES (12U * 1024U * 1024U)
typedef enum { ENTRY_NEW, ENTRY_READY, ENTRY_ERROR } entry_state;
typedef struct {
    message_media item;
    entry_state state;
    media_resource *resource;
    graphics_image image; /* Borrowed snapshot; cache owns its buffers. */
    uint64_t used;
    uint32_t placement_id;
    uint32_t fragments[32];
    const char *error;
} cache_entry;
typedef struct {
    int entry, y, x, width, height, top, bottom;
    uint32_t background;      /* Shown around the media; transparency blends into it. */
    graphics_image image;     /* Borrowed raster protected by resource + a cache pin. */
    media_resource *resource; /* Owned position reference. */
    image_decode_request request;
} placement;
typedef struct {
    char *path;
    uint64_t token;
    int width, height;
    bool video, audio;
    double volume; /* Level applied when the player starts; negative keeps it. */
    graphics_terminal terminal;
    uint32_t image_id;
} job;
struct media_view {
    auth_client *auth;
    history *model; /* Borrowed while the conversation is active. */
    media_cache *store;
    cache_entry cache[CACHE_COUNT];
    placement visible[VISIBLE_COUNT];
    int visible_count, viewport_rows;
    uint64_t serial, tick, generation, history_revision, dimensions_revision;
    graphics_terminal terminal;
    GThread *thread;
    GMutex lock;
    GCond wake;
    bool started, stopping, pending, result_ready, player_wakeup;
    atomic_uint_fast64_t active;
    atomic_bool player_idle;
    size_t video_reserved;
    job task;
    graphics_image result;
    bool modal_shared, modal_animated;
    uint64_t result_token;
    const char *result_error;
    int pause_commands, mute_commands, seek_commands, volume_commands, loop_commands;
    bool seek_absolute, volume_absolute, speed_set, restart_command;
    double seek_position, volume_level, speed_level;
    double speed_target; /* Last requested audio speed; survives quick key repeats. */
    bool playing, paused, muted, audio_missing, looping, ended, status_changed;
    double position, duration, volume, speed;
    /* An audio player left running after its modal closed. Main thread only;
     * the resource reference keeps the download and the file alive. */
    struct {
        bool active;
        message_media item;
        media_resource *resource;
        char *source; /* Player source, reused so reopening does not restart. */
        uint64_t token;
        char channel[37], created_at[64];
    } audio;
    int mini_y, mini_x, mini_width, mini_play_x, mini_play_w, mini_stop_x, mini_stop_w;
    struct {
        message_media item;
        int y, x, width, pair;
    } audio_rows[VISIBLE_COUNT]; /* Inline bars of the last history paint. */
    int audio_row_count;
    enum { MODAL_NONE, MODAL_LIST, MODAL_LOADING, MODAL_PLAYING, MODAL_ERROR } modal;
    WINDOW *window;
    media_modal_layout layout;
    bool info_open, info_focused;
    int info_scroll, info_rows;
    int selected, modal_entry, modal_width, modal_height;
    message_media selected_item;
    message_media *gallery; /* Owned metadata; resources stay in the shared cache. */
    /* Members are every media of the message the modal can step to; images are the
     * members that are not video or audio. */
    size_t gallery_count, gallery_index, image_count, member_count;
    uint64_t modal_token;
    char *modal_source; /* First source this player session used, kept across resizes. */
    graphics_image modal_image;
    const char *modal_error;
};
static void delete_entry(cache_entry *e)
{
    media_resource_release(e->resource);
    memset(e, 0, sizeof(*e));
}
static bool ensure_resource(media_view *v, cache_entry *e)
{
    if (!e->resource)
        e->resource =
            media_cache_acquire(v->store, e->item.kind, e->item.id, e->item.version, e->item.video);
    return e->resource != NULL;
}
static void free_job(job *j)
{
    free(j->path);
    memset(j, 0, sizeof(*j));
}
static void clear_modal_image(media_view *v)
{
    if (!v->modal_shared)
        graphics_image_free(&v->modal_image);
    else
        memset(&v->modal_image, 0, sizeof(v->modal_image));
    v->modal_shared = false;
}
static void publish(media_view *v, uint64_t token, graphics_image *image, animation *animated,
                    const char *error)
{
    g_mutex_lock(&v->lock);
    (void)animated;
    graphics_image_free(&v->result);
    v->result = *image;
    memset(image, 0, sizeof(*image));
    v->result_token = token;
    v->result_error = error;
    v->result_ready = true;
    g_mutex_unlock(&v->lock);
}
static void player_wakeup(void *context)
{
    media_view *v = context;
    g_mutex_lock(&v->lock);
    v->player_wakeup = true;
    g_cond_signal(&v->wake);
    g_mutex_unlock(&v->lock);
}
static void *worker(void *context)
{
    media_view *v = context;
    job current = {0};
    void *player = NULL;
    const video_player_api *api = NULL;
    bool force_frame = false;
    bool apply_volume = false;
    bool announce = false; /* Audio without a picture reports its start once. */
    for (;;) {
        gint64 frame_started = g_get_monotonic_time();
        g_mutex_lock(&v->lock);
        bool stop = v->stopping;
        bool pending = v->pending;
        job next = {0};
        if (pending) {
            next = v->task;
            memset(&v->task, 0, sizeof(v->task));
            v->pending = false;
        }
        video_controls controls = {.pause = v->pause_commands,
                                   .mute = v->mute_commands,
                                   .seek = v->seek_commands,
                                   .volume = v->volume_commands,
                                   .loop = v->loop_commands,
                                   .seek_absolute = v->seek_absolute,
                                   .volume_absolute = v->volume_absolute,
                                   .speed_set = v->speed_set,
                                   .restart = v->restart_command,
                                   .position = v->seek_position,
                                   .volume_level = v->volume_level,
                                   .speed_level = v->speed_level};
        v->pause_commands = v->mute_commands = v->seek_commands = v->volume_commands = 0;
        v->loop_commands = 0;
        v->seek_absolute = v->volume_absolute = v->speed_set = v->restart_command = false;
        v->player_wakeup = false;
        g_mutex_unlock(&v->lock);
        if (stop) {
            free_job(&next);
            break;
        }
        bool reuse = player && pending && next.video && !strcmp(current.path, next.path);
        if (player && !reuse && (pending || atomic_load(&v->active) != current.token)) {
            api->close(player);
            player = NULL;
            free_job(&current);
            atomic_store(&v->player_idle, true);
        }
        if (pending) {
            if (atomic_load(&v->active) != next.token) {
                free_job(&next);
                continue;
            }
            atomic_store(&v->player_idle, false);
            if (reuse) {
                free_job(&current);
                current = next;
                force_frame = true;
                announce = current.audio;
            } else {
                current = next;
                force_frame = true;
                api = media_video_backend();
                player = api ? api->open(current.path) : NULL;
                if (!player) {
                    graphics_image empty = {0};
                    publish(
                        v, current.token, &empty, NULL,
                        media_service_error() ? media_service_error()
                        : api ? "Não foi possível iniciar o player."
                              : "Módulo de vídeo ausente ou incompatível. Reinstale o aplicativo.");
                    free_job(&current);
                    atomic_store(&v->player_idle, true);
                    continue;
                }
                api->set_wakeup(player, player_wakeup, v);
                apply_volume = current.volume >= 0;
                announce = current.audio;
            }
        }
        if (player) {
            /* The level the user chose reaches the new player before its first
             * frame, so the volume never restarts at the default. */
            if (apply_volume) {
                apply_volume = false;
                controls.volume_absolute = true;
                controls.volume_level = current.volume;
                controls.volume = 0;
            }
            video_info info = {0};
            graphics_image image = {0};
            image_decode_request request = {.width = current.width,
                                            .height = current.height,
                                            .terminal = current.terminal,
                                            .image_id = current.image_id};
            if (!media_service_video_frame(player, &controls, &request, force_frame, &info,
                                           &image) ||
                info.invalid) {
                uint64_t token = current.token;
                bool audio = current.audio;
                graphics_image_free(&image);
                api->close(player);
                player = NULL;
                free_job(&current);
                atomic_store(&v->player_idle, true);
                graphics_image empty = {0};
                publish(v, token, &empty, NULL,
                        audio ? "Áudio inválido ou codec indisponível."
                              : "Vídeo inválido ou codec indisponível.");
                continue;
            }
            g_mutex_lock(&v->lock);
            v->status_changed |= v->paused != info.paused || v->muted != info.muted ||
                                 v->audio_missing != info.audio_missing ||
                                 /* Each new whole second repaints the clock, also for audio
                                  * without frames to trigger a redraw. */
                                 floor(v->position) != floor(info.position) ||
                                 v->volume != info.volume || v->looping != info.looping ||
                                 v->ended != info.ended || v->speed != info.speed;
            v->audio_missing = info.audio_missing;
            v->looping = info.looping;
            v->ended = info.ended;
            v->speed = info.speed;
            v->playing = true;
            v->paused = info.paused;
            v->muted = info.muted;
            v->position = info.position;
            v->duration = info.duration;
            v->volume = info.volume;
            g_mutex_unlock(&v->lock);
            if (image.data) {
                force_frame = false;
                announce = false;
                publish(v, current.token, &image, NULL, NULL);
            } else if (announce && (info.position > 0 || info.duration > 0)) {
                /* Without a cover there is no frame to publish; an empty
                 * result tells the modal that playback started. */
                announce = false;
                publish(v, current.token, &image, NULL, NULL);
            }
        }
        g_mutex_lock(&v->lock);
        if (!v->pending && !v->stopping && !v->player_wakeup && !v->pause_commands &&
            !v->mute_commands && !v->seek_commands && !v->volume_commands && !v->seek_absolute &&
            !v->volume_absolute && !v->loop_commands && !v->speed_set && !v->restart_command) {
            if (!player)
                g_cond_wait(&v->wake, &v->lock);
            else {
                /* IPC cannot forward libmpv callbacks to this process. Poll at
                 * 60 Hz while playing, counting work time in the interval;
                 * there is only one pending publication, so slow terminals
                 * always receive the latest frame instead of a growing queue. */
                gint64 interval = v->paused && !force_frame ? 50000
                                  : current.audio           ? G_USEC_PER_SEC / 10
                                                            : G_USEC_PER_SEC / 60;
                g_cond_wait_until(&v->wake, &v->lock, frame_started + interval);
            }
        }
        g_mutex_unlock(&v->lock);
    }
    if (player)
        api->close(player);
    free_job(&current);
    atomic_store(&v->player_idle, true);
    return NULL;
}
static bool submit_job(media_view *v, const job *request)
{
    char *copy = strdup(request->path);
    if (!copy)
        return false;
    if (!v->started) {
        v->thread = g_thread_try_new("papo-player", worker, v, NULL);
        if (!v->thread) {
            free(copy);
            return false;
        }
        v->started = true;
    }
    g_mutex_lock(&v->lock);
    free_job(&v->task);
    v->task = *request;
    v->task.path = copy;
    v->task.terminal = graphics_terminal_get();
    if (!v->task.image_id)
        v->task.image_id = graphics_image_next_id();
    v->pending = true;
    atomic_store(&v->active, request->token);
    g_cond_signal(&v->wake);
    g_mutex_unlock(&v->lock);
    return true;
}
static bool native_media(const message_media *item)
{
    return item->full_width || !strcmp(item->role, "attachment") || !strcmp(item->role, "image");
}

media_view *media_view_create(auth_client *auth)
{
    media_view *v = calloc(1, sizeof(*v));
    if (!v)
        return NULL;
    v->auth = auth;
    v->modal_entry = -1;
    v->generation = auth_context_generation(auth);
    v->store = media_cache_create(auth);
    if (!v->store) {
        free(v);
        return NULL;
    }
    g_mutex_init(&v->lock);
    g_cond_init(&v->wake);
    atomic_init(&v->active, 0);
    atomic_init(&v->player_idle, true);
    return v;
}
/* Retires the worker's player and drops controls not yet delivered. */
static void stop_player(media_view *v)
{
    atomic_store(&v->active, 0);
    g_mutex_lock(&v->lock);
    v->playing = v->ended = v->looping = false;
    v->pause_commands = v->seek_commands = v->volume_commands = v->mute_commands = 0;
    v->loop_commands = 0;
    v->seek_absolute = v->volume_absolute = v->speed_set = v->restart_command = false;
    g_cond_signal(&v->wake);
    g_mutex_unlock(&v->lock);
}
static void clear_audio(media_view *v)
{
    media_resource_release(v->audio.resource);
    free(v->audio.source);
    memset(&v->audio, 0, sizeof(v->audio));
}
/* Ends the background audio session, if any. */
static void stop_audio(media_view *v)
{
    if (!v || !v->audio.active)
        return;
    clear_audio(v);
    stop_player(v);
}
/* A background audio session keeps the player when another modal closes. */
static void close_modal(media_view *v)
{
    if (!v)
        return;
    free(v->gallery);
    v->gallery = NULL;
    v->gallery_count = v->gallery_index = v->image_count = v->member_count = 0;
    if (!v->audio.active)
        stop_player(v);
    if (v->window) {
        delwin(v->window);
        v->window = NULL;
    }
    v->modal_animated = false;
    clear_modal_image(v);
    v->modal = MODAL_NONE;
    v->modal_entry = -1;
    /* The list and notice modals open without an item; player keys must not
     * reach a background audio through the item shown last. */
    memset(&v->selected_item, 0, sizeof(v->selected_item));
    v->modal_token = 0;
    free(v->modal_source);
    v->modal_source = NULL;
    v->modal_error = NULL;
    v->info_open = v->info_focused = false;
    v->info_scroll = v->info_rows = 0;
    v->modal_width = v->modal_height = 0;
}
/* Esc or a click outside: an audio player that already has its source keeps
 * playing in the background; every other modal closes as before. */
static void dismiss_modal(media_view *v)
{
    media_resource *resource = v->modal_entry >= 0 && v->modal_entry < CACHE_COUNT
                                   ? v->cache[v->modal_entry].resource
                                   : NULL;
    if (v->selected_item.audio && v->modal_token && v->modal_source && resource &&
        v->modal != MODAL_ERROR) {
        stop_audio(v);
        v->audio.active = true;
        v->audio.item = v->selected_item;
        v->audio.resource = resource;
        media_resource_ref(resource);
        v->audio.source = v->modal_source;
        v->modal_source = NULL;
        v->audio.token = v->modal_token;
        json_t *message =
            v->model ? history_find_message(v->model, v->selected_item.message) : NULL;
        const char *channel = json_string_value(json_object_get(message, "channel_id"));
        const char *created = json_string_value(json_object_get(message, "created_at"));
        snprintf(v->audio.channel, sizeof(v->audio.channel), "%s", channel ? channel : "");
        snprintf(v->audio.created_at, sizeof(v->audio.created_at), "%s", created ? created : "");
    }
    close_modal(v);
}
static void clear_positions(media_view *v)
{
    for (int i = 0; i < v->visible_count; ++i)
        media_resource_release(v->visible[i].resource);
    v->visible_count = 0;
    v->audio_row_count = 0;
}
void media_view_reset(media_view *v)
{
    if (!v)
        return;
    ++v->dimensions_revision;
    if (v->visible_count || v->modal != MODAL_NONE)
        graphics_images_clear();
    stop_audio(v);
    close_modal(v);
    v->model = NULL;
    clear_positions(v);
    for (int i = 0; i < CACHE_COUNT; ++i)
        delete_entry(&v->cache[i]);
    media_cache_reset(v->store);
    v->visible_count = 0;
    v->generation = auth_context_generation(v->auth);
}
void media_view_suspend(media_view *v)
{
    if (!v)
        return;
    graphics_images_clear();
    close_modal(v);
    v->model = NULL;
    clear_positions(v);
    for (int i = 0; i < CACHE_COUNT; ++i)
        delete_entry(&v->cache[i]);
    v->visible_count = 0;
    media_cache_begin(v->store);
    /* Background audio survives conversation switches within this server. */
    if (v->audio.active)
        media_resource_use_image(v->audio.resource, false, true);
    uint64_t now = 0;
    net_now_ms(&now);
    media_cache_poll(v->store, now);
}
media_cache *media_view_cache(media_view *v)
{
    return v ? v->store : NULL;
}
uint64_t media_view_dimensions_revision(const media_view *v)
{
    return v ? v->dimensions_revision : 0;
}
void media_view_idle(media_view *v)
{
    if (!v || v->visible_count || v->modal != MODAL_NONE)
        return;
    uint64_t now = 0;
    net_now_ms(&now);
    media_cache_poll(v->store, now);
    g_mutex_lock(&v->lock);
    if (!atomic_load(&v->active) && atomic_load(&v->player_idle)) {
        graphics_image_free(&v->result);
        v->result_ready = false;
        if (v->video_reserved) {
            media_cache_unreserve(v->video_reserved);
            v->video_reserved = 0;
        }
    }
    g_mutex_unlock(&v->lock);
}
void media_view_destroy(media_view *v)
{
    if (!v)
        return;
    g_mutex_lock(&v->lock);
    v->stopping = true;
    atomic_store(&v->active, 0);
    g_cond_signal(&v->wake);
    g_mutex_unlock(&v->lock);
    if (v->started)
        g_thread_join(v->thread);
    media_view_reset(v);
    free_job(&v->task);
    graphics_image_free(&v->result);
    g_cond_clear(&v->wake);
    g_mutex_clear(&v->lock);
    if (v->video_reserved)
        media_cache_unreserve(v->video_reserved);
    media_cache_destroy(v->store);
    free(v);
}
void media_view_update(media_view *v, const json_t *event)
{
    if (!v || v->generation != auth_context_generation(v->auth))
        return;
    const char *type = json_string_value(json_object_get(event, "type"));
    const char *message = json_string_value(json_object_get(event, "message_id"));
    json_t *embeds = json_object_get(event, "embeds");
    if (!type || strcmp(type, "message_embeds_update") || !history_uuid(message) ||
        (!json_is_null(embeds) && !json_is_array(embeds)) || json_array_size(embeds) > 10)
        return;
    for (size_t i = 0; i < json_array_size(embeds); ++i) {
        const char *id = json_string_value(json_object_get(json_array_get(embeds, i), "id"));
        if (!history_uuid(id))
            continue;
        media_cache_invalidate(v->store, AUTH_MEDIA_EMBED, id);
        media_cache_invalidate(v->store, AUTH_MEDIA_VIDEO, id);
    }
    for (int i = 0; i < CACHE_COUNT; ++i) {
        cache_entry *e = &v->cache[i];
        if (e->item.kind != AUTH_MEDIA_EMBED && e->item.kind != AUTH_MEDIA_VIDEO)
            continue;
        bool affected = !strcmp(e->item.message, message);
        for (size_t j = 0; !affected && j < json_array_size(embeds); ++j) {
            const char *id = json_string_value(json_object_get(json_array_get(embeds, j), "id"));
            affected = id && !strcmp(e->item.id, id);
        }
        if (!affected)
            continue;
        if (v->modal_entry == i)
            close_modal(v);
        media_cache_invalidate(v->store, e->item.kind, e->item.id);
        delete_entry(e);
    }
    ++v->dimensions_revision;
}
static bool same(const message_media *a, const message_media *b)
{
    return a->kind == b->kind && !strcmp(a->id, b->id) && !strcmp(a->role, b->role) &&
           !strcmp(a->message, b->message) && a->version == b->version &&
           a->position == b->position;
}
static int cache_find(media_view *v, const message_media *item)
{
    int oldest = -1;
    for (int i = 0; i < CACHE_COUNT; ++i) {
        if (*v->cache[i].item.id && same(&v->cache[i].item, item)) {
            v->cache[i].item = *item;
            return i;
        }
        if (i == v->modal_entry)
            continue;
        bool visible = false;
        for (int j = 0; j < v->visible_count; ++j)
            visible |= v->visible[j].entry == i;
        if (!visible && (oldest < 0 || v->cache[i].used < v->cache[oldest].used))
            oldest = i;
    }
    if (oldest >= 0) {
        delete_entry(&v->cache[oldest]);
        v->cache[oldest].item = *item;
        v->cache[oldest].placement_id = graphics_image_next_id();
    }
    return oldest;
}
static void geometry(int columns, int rows, int *w, int *h)
{
    graphics_terminal t = graphics_terminal_get();
    double width = fmax(1, (double)columns * t.cell_width),
           height = fmax(1, (double)rows * t.cell_height);
    double ratio = fmin(1, sqrt(GRAPHICS_IMAGE_PIXELS / (width * height)));
    *w = (int)fmax(1, width * ratio);
    *h = (int)fmax(1, height * ratio);
}
static void history_geometry(const media_view *v, const message_media *item, int columns, int rows,
                             int *w, int *h)
{
    geometry(columns, native_media(item) && v->viewport_rows ? v->viewport_rows : rows, w, h);
    if (native_media(item) && *h > HISTORY_IMAGE_HEIGHT)
        *h = HISTORY_IMAGE_HEIGHT;
}
static void modal_image_geometry(int source_w, int source_h, int *w, int *h)
{
    double scale = fmin((double)*w / source_w, (double)*h / source_h);
    *w = (int)fmax(1, source_w * scale);
    *h = (int)fmax(1, source_h * scale);
}
void media_view_viewport(media_view *v, int rows)
{
    if (!v)
        return;
    /* Every message ends with a separator row. Keep a fitted attachment fully
     * visible at the bottom of history instead of clipping its first row. */
    if (rows > 1)
        --rows;
    if (rows < 1)
        rows = 1;
    if (v->viewport_rows != rows) {
        v->viewport_rows = rows;
        ++v->dimensions_revision;
    }
}
void media_view_measure(void *context, const message_media *item, int available_width, int *width,
                        int *height)
{
    media_view *v = context;
    if (!v || !native_media(item))
        return;
    graphics_terminal t = graphics_terminal_get();
    if (!t.supported)
        return;
    int source_width = item->source_width, source_height = item->source_height;
    for (int i = 0; i < CACHE_COUNT; ++i) {
        cache_entry *e = &v->cache[i];
        if (!same(&e->item, item) || !e->image.source_width || !e->image.source_height)
            continue;
        source_width = e->image.source_width;
        source_height = e->image.source_height;
        break;
    }
    if (source_width < 1 || source_height < 1)
        return;
    int max_w, max_h;
    history_geometry(v, item, available_width, 6, &max_w, &max_h);
    double scale = fmin(1, fmin((double)max_w / source_width, (double)max_h / source_height));
    int w = (int)fmax(1, source_width * scale), h = (int)fmax(1, source_height * scale);
    *width = (w + t.cell_width - 1) / t.cell_width;
    *height = (h + t.cell_height - 1) / t.cell_height;
}
void media_view_frame(media_view *v)
{
    if (v)
        graphics_frame_begin();
}
void media_view_begin(media_view *v)
{
    if (v) {
        graphics_frame_begin();
        clear_positions(v);
        ++v->tick;
    }
}
void media_view_offer(media_view *v, const message_media *item, int y, int x, int width, int height,
                      int top, int bottom, uint32_t background)
{
    if (!v || v->visible_count == VISIBLE_COUNT)
        return;
    int index = cache_find(v, item);
    if (index < 0)
        return;
    cache_entry *e = &v->cache[index];
    e->used = v->tick;
    placement *p = &v->visible[v->visible_count++];
    *p = (placement){.entry = index,
                     .y = y,
                     .x = x,
                     .width = width,
                     .height = height,
                     .top = top,
                     .bottom = bottom,
                     .background = background};
    p->resource = e->resource;
    media_resource_ref(p->resource);
    graphics_terminal t = graphics_terminal_get();
    if (e->resource && t.supported && height > 0) {
        int w, h;
        history_geometry(v, item, width, height, &w, &h);
        uint64_t now = 0;
        net_now_ms(&now);
        image_decode_request request = {.width = w,
                                        .height = h,
                                        .native = native_media(item),
                                        .terminal = t,
                                        .matte = true,
                                        .background = background,
                                        .top = top * t.cell_height,
                                        .bottom = bottom * t.cell_height};
        p->request = request;
        const graphics_image *image =
            media_resource_image(e->resource, !strcmp(item->role, "author"), request, now);
        if (image) {
            if (e->image.source_width != image->source_width ||
                e->image.source_height != image->source_height)
                ++v->dimensions_revision;
            p->image = *image;
            e->image = *image;
            e->state = ENTRY_READY;
        }
    }
}
static bool live(history *h, const message_media *item)
{
    for (size_t i = 0; i < history_message_count(h); ++i) {
        json_t *m = history_message(h, i);
        const char *id = json_string_value(json_object_get(m, "id"));
        if (!id || strcmp(id, item->message))
            continue;
        json_t *list = json_object_get(m, item->kind == AUTH_MEDIA_ATTACHMENT ||
                                                  item->kind == AUTH_MEDIA_THUMBNAIL
                                              ? "attachments"
                                              : "embeds");
        size_t n;
        json_t *entry;
        json_array_foreach(list, n, entry)
        {
            id = json_string_value(json_object_get(entry, "id"));
            if (id && !strcmp(id, item->id) && message_media_version(entry) == item->version)
                return true;
        }
        return false;
    }
    return false;
}
static bool modal_layout_update(media_view *v)
{
    media_modal_layout layout;
    if (!media_modal_geometry(COLS, LINES, v->info_open && v->modal != MODAL_LIST,
                              v->modal_image.columns, v->modal_image.rows, &layout))
        return false;
    int width, height;
    geometry(layout.area_width, layout.area_height, &width, &height);
    if (v->selected_item.video) {
        double scale = fmin(1, sqrt(VIDEO_FRAME_PIXELS / ((double)width * height)));
        width = (int)fmax(1, width * scale);
        height = (int)fmax(1, height * scale);
    }
    if (v->modal_token && (width != v->modal_width || height != v->modal_height)) {
        bool reload = v->selected_item.video || !v->modal_image.data;
        if (v->modal_image.data && !v->selected_item.video) {
            int source_w = v->modal_image.source_width, source_h = v->modal_image.source_height;
            if (source_w > 0 && source_h > 0) {
                int w = width, h = height;
                modal_image_geometry(source_w, source_h, &w, &h);
                reload = v->modal_image.width != w || v->modal_image.height != h;
            } else
                reload = v->modal_image.width > width || v->modal_image.height > height;
        }
        if (reload) {
            v->modal_token = 0;
            clear_modal_image(v);
            if (v->modal == MODAL_PLAYING)
                v->modal = MODAL_LOADING;
        }
    }
    if (v->window && (getmaxx(v->window) != layout.width || getmaxy(v->window) != layout.height ||
                      getbegx(v->window) != layout.x || getbegy(v->window) != layout.y)) {
        delwin(v->window);
        v->window = NULL;
    }
    v->modal_width = width;
    v->modal_height = height;
    v->layout = layout;
    return true;
}
int media_view_input_delay(media_view *v)
{
    if (!v)
        return 30;
    g_mutex_lock(&v->lock);
    bool paused = v->paused;
    g_mutex_unlock(&v->lock);
    return (v->modal_token && !paused) || media_cache_animating(v->store) ? 8 : 30;
}
bool media_view_poll(media_view *v, history *h)
{
    if (!v)
        return false;
    if (v->generation != auth_context_generation(v->auth) || !auth_has_session(v->auth)) {
        bool changed = v->visible_count || v->modal != MODAL_NONE;
        media_view_reset(v);
        return changed;
    }
    v->model = h;
    uint64_t now = 0;
    net_now_ms(&now);
    graphics_terminal terminal = graphics_terminal_get();
    bool changed = terminal.protocol != v->terminal.protocol ||
                   terminal.cell_width != v->terminal.cell_width ||
                   terminal.cell_height != v->terminal.cell_height ||
                   terminal.supported != v->terminal.supported;
    if (changed)
        ++v->dimensions_revision;
    if (changed && v->modal_token) {
        v->modal_token = 0;
        clear_modal_image(v);
    }
    v->terminal = terminal;
    /* Validate associations only after model changes, not on every keystroke. */
    uint64_t revision = history_messages_revision(h);
    if (revision != v->history_revision) {
        v->history_revision = revision;
        for (int i = 0; i < CACHE_COUNT; ++i)
            if (*v->cache[i].item.id && !live(h, &v->cache[i].item)) {
                if (v->modal_entry == i)
                    close_modal(v);
                delete_entry(&v->cache[i]);
                changed = true;
            }
    }
    int count = 0;
    for (int i = 0; i < v->visible_count; ++i)
        if (*v->cache[v->visible[i].entry].item.id)
            v->visible[count++] = v->visible[i];
        else
            media_resource_release(v->visible[i].resource);
    v->visible_count = count;
    if (v->modal == MODAL_LIST && v->selected >= count)
        v->selected = count ? count - 1 : 0;
    media_cache_begin(v->store);
    if (v->audio.active)
        media_resource_use_image(v->audio.resource, false, true);
    bool modal = v->modal != MODAL_NONE && v->modal != MODAL_LIST && v->modal_entry >= 0;
    if (modal) {
        cache_entry *e = &v->cache[v->modal_entry];
        ensure_resource(v, e);
        media_resource_use_image(e->resource, !strcmp(e->item.role, "author"), true);
        media_resource_keep_image(e->resource, !strcmp(e->item.role, "author"), &v->modal_image);
    }
    if (terminal.supported)
        for (int i = 0; i < v->visible_count; ++i) {
            cache_entry *e = &v->cache[v->visible[i].entry];
            if (!e->item.audio && (!e->item.video || e->item.kind == AUTH_MEDIA_ATTACHMENT)) {
                ensure_resource(v, e);
                media_resource_use_image(e->resource, !strcmp(e->item.role, "author"), false);
                placement *p = &v->visible[i];
                if (p->resource != e->resource) {
                    media_resource_release(p->resource);
                    p->resource = e->resource;
                    media_resource_ref(p->resource);
                }
                media_resource_keep_image(e->resource, !strcmp(e->item.role, "author"), &p->image);
            }
        }
    changed |= media_cache_poll(v->store, now);
    bool background_failed = false;
    g_mutex_lock(&v->lock);
    if (v->result_ready) {
        if (v->modal_token && v->result_token == v->modal_token) {
            if (v->result_error) {
                media_resource *resource = v->modal_entry >= 0 && v->modal_entry < CACHE_COUNT
                                               ? v->cache[v->modal_entry].resource
                                               : NULL;
                /* Only a failed transfer or ingest replaces the player's own
                 * message; a ready-but-undecodable video keeps that message. */
                const char *failure =
                    media_resource_failed(resource)
                        ? media_resource_error(
                              resource, !strcmp(v->cache[v->modal_entry].item.role, "author"))
                        : NULL;
                v->modal_error = failure ? failure : v->result_error;
                v->modal = MODAL_ERROR;
            } else {
                clear_modal_image(v);
                v->modal_image = v->result;
                memset(&v->result, 0, sizeof(v->result));
                v->modal = MODAL_PLAYING;
            }
            changed = true;
        } else if (v->audio.active && v->result_token == v->audio.token && v->result_error)
            background_failed = true;
        graphics_image_free(&v->result);
        v->result_ready = false;
    }
    changed |= v->status_changed;
    v->status_changed = false;
    if (!atomic_load(&v->active) && atomic_load(&v->player_idle) && v->video_reserved) {
        media_cache_unreserve(v->video_reserved);
        v->video_reserved = 0;
    }
    g_mutex_unlock(&v->lock);
    if (background_failed) {
        /* The player failed or the helper restarted: the mini-player leaves. */
        stop_audio(v);
        changed = true;
    }
    if (modal && v->modal != MODAL_ERROR && modal_layout_update(v)) {
        cache_entry *e = &v->cache[v->modal_entry];
        bool author = !strcmp(e->item.role, "author");
        const char *error = media_resource_error(e->resource, author);
        if (error && !(e->item.video && media_resource_path(e->resource))) {
            v->modal_error = error;
            v->modal = MODAL_ERROR;
            return true;
        }
        if (e->item.video) {
            const char *current = media_resource_source(e->resource);
            /* Keep the source the session started with: a download opened
             * through the bridge must not reload the player (losing pause and
             * position) when the file becomes available locally. */
            if (current && !v->modal_source)
                v->modal_source = strdup(current);
            const char *path = v->modal_source ? v->modal_source : current;
            if (path && !v->modal_token && v->modal_width > 0 && v->modal_height > 0) {
                /* Cover RGB/RGBA working buffers and three encoded frames (the
                 * displayed frame, pending result and next publication). Keep the
                 * largest reservation until the worker stops using its surfaces. */
                size_t pixels = (size_t)v->modal_width * (size_t)v->modal_height;
                if (terminal.protocol == GRAPHICS_KITTY && pixels > VIDEO_KITTY_FRAME_PIXELS)
                    pixels = VIDEO_KITTY_FRAME_PIXELS;
                size_t reservation = VIDEO_WORKSPACE_BYTES + pixels * 32U;
                if (reservation > v->video_reserved) {
                    if (!media_cache_reserve(reservation - v->video_reserved))
                        return changed;
                    v->video_reserved = reservation;
                }
                v->modal_token = ++v->serial;
                job request = {.path = (char *)path,
                               .token = v->modal_token,
                               .width = v->modal_width,
                               .height = v->modal_height,
                               .video = true,
                               .audio = e->item.audio,
                               .volume = player_volume_get(),
                               .image_id = v->modal_image.id};
                if (!submit_job(v, &request)) {
                    v->modal_error = "Não foi possível preparar o player.";
                    v->modal = MODAL_ERROR;
                }
                changed = true;
            }
        } else {
            image_decode_request request = {.width = v->modal_width,
                                            .height = v->modal_height,
                                            .native = true,
                                            .terminal = terminal,
                                            .matte = true,
                                            .background = theme_background_rgb(THEME_PANEL)};
            int source_w = 0, source_h = 0;
            if (media_resource_dimensions(e->resource, author, &source_w, &source_h)) {
                /* Fit the canvas first so enlargement preserves the aspect
                 * ratio. Canonical pixels remain bounded by the source size. */
                modal_image_geometry(source_w, source_h, &request.width, &request.height);
                request.native = false;
            }
            const graphics_image *image = media_resource_image(e->resource, author, request, now);
            if (image) {
                changed |= v->modal_image.revision != image->revision;
                clear_modal_image(v);
                v->modal_image = *image;
                v->modal_shared = true;
                v->modal_animated = media_resource_animated(e->resource, author);
                v->modal = MODAL_PLAYING;
            }
        }
        return changed;
    }
    if (v->modal != MODAL_NONE || !terminal.supported)
        return changed;
    for (int i = 0; i < v->visible_count; ++i) {
        placement *p = &v->visible[i];
        cache_entry *e = &v->cache[p->entry];
        if ((e->item.video && e->item.kind != AUTH_MEDIA_ATTACHMENT) || p->height < 1)
            continue;
        int source_w = 0, source_h = 0;
        if (media_resource_dimensions(e->resource, !strcmp(e->item.role, "author"), &source_w,
                                      &source_h) &&
            (source_w != e->image.source_width || source_h != e->image.source_height)) {
            e->image.source_width = source_w;
            e->image.source_height = source_h;
            ++v->dimensions_revision;
            changed = true;
        }
        int w, height;
        history_geometry(v, &e->item, p->width, p->height, &w, &height);
        image_decode_request request = {.width = w,
                                        .height = height,
                                        .native = native_media(&e->item),
                                        .terminal = terminal,
                                        .matte = true,
                                        .background = p->background,
                                        .top = p->top * terminal.cell_height,
                                        .bottom = p->bottom * terminal.cell_height};
        p->request = request;
        const graphics_image *image =
            media_resource_image(e->resource, !strcmp(e->item.role, "author"), request, now);
        const char *error = media_resource_error(e->resource, !strcmp(e->item.role, "author"));
        entry_state state = error ? ENTRY_ERROR : image ? ENTRY_READY : ENTRY_NEW;
        changed |= e->state != state || p->image.revision != (image ? image->revision : 0);
        e->state = state;
        e->error = error;
        p->image = image ? *image : (graphics_image){0};
        if (image) {
            if (e->image.source_width != image->source_width ||
                e->image.source_height != image->source_height)
                ++v->dimensions_revision;
            e->image = *image;
        }
    }
    return changed;
}
static const message_media *embed_video(const message_media *items, size_t count,
                                        const message_media *image)
{
    if (image->kind != AUTH_MEDIA_EMBED || strcmp(image->role, "thumbnail"))
        return NULL;
    for (size_t i = 0; i < count; ++i)
        if (items[i].kind == AUTH_MEDIA_VIDEO && items[i].video && !strcmp(items[i].id, image->id))
            return &items[i];
    return NULL;
}
/* Every media of the message except author avatars and the covers of embed
 * videos, which open their video instead. */
static bool gallery_member(const media_view *v, size_t index)
{
    const message_media *item = &v->gallery[index];
    return strcmp(item->role, "author") && !embed_video(v->gallery, v->gallery_count, item);
}
static void open_message_media(media_view *v, message_media *item)
{
    json_t *message = v->model ? history_find_message(v->model, item->message) : NULL;
    message_blocks blocks;
    if (!json_is_object(message) ||
        !message_blocks_build(&blocks, message, "", "", COLS > 8 ? COLS - 4 : 44, true))
        return;
    const message_media *video = embed_video(blocks.media, blocks.media_count, item);
    if (video)
        *item = *video;
    if (strcmp(item->role, "author")) {
        size_t selected = SIZE_MAX;
        for (size_t i = 0; i < blocks.media_count; ++i)
            if (blocks.media[i].kind == item->kind && !strcmp(blocks.media[i].id, item->id) &&
                !strcmp(blocks.media[i].role, item->role)) {
                selected = i;
                break;
            }
        if (selected != SIZE_MAX) {
            v->gallery = blocks.media;
            v->gallery_count = blocks.media_count;
            v->gallery_index = selected;
            blocks.media = NULL;
            blocks.media_count = 0;
            for (size_t i = 0; i < v->gallery_count; ++i)
                if (gallery_member(v, i)) {
                    ++v->member_count;
                    v->image_count += !v->gallery[i].video;
                }
        }
    }
    message_blocks_free(&blocks);
}
bool media_view_open(media_view *v, const message_media *source)
{
    if (!v || !source)
        return false;
    message_media item = *source;
    /* Reopening the background audio hands its source back to the modal, so
     * the next job reuses the running player instead of restarting it. */
    bool resume = v->audio.active && source->audio && same(source, &v->audio.item);
    close_modal(v);
    open_message_media(v, &item);
    if (item.kind == AUTH_MEDIA_THUMBNAIL)
        item.kind = AUTH_MEDIA_ATTACHMENT;
    if (!resume && item.video) {
        stop_audio(v); /* The media helper runs a single player. */
        g_mutex_lock(&v->lock);
        v->speed_target = 1; /* A new player starts at normal speed. */
        g_mutex_unlock(&v->lock);
    }
    v->selected_item = item;
    v->modal_entry = cache_find(v, &item);
    v->modal = MODAL_LOADING;
    if (resume) {
        if (v->modal_entry >= 0) {
            /* Take the entry's own reference before the session drops its own. */
            ensure_resource(v, &v->cache[v->modal_entry]);
            v->modal_source = v->audio.source;
            v->audio.source = NULL;
        }
        v->audio.active = false;
        clear_audio(v);
        if (v->modal_entry < 0)
            stop_player(v);
    }
    if (v->modal_entry >= 0 && v->selected_item.video && app_mode_get() == APP_MODE_ULTRALIGHT) {
        v->modal_error = v->selected_item.audio ? "Áudios desativados neste modo."
                                                : "Vídeos desativados neste modo.";
        v->modal = MODAL_ERROR;
    } else if (v->modal_entry < 0 || !graphics_terminal_get().supported) {
        v->modal_error = "Este terminal não oferece um protocolo gráfico compatível.";
        v->modal = MODAL_ERROR;
    } else if (v->cache[v->modal_entry].state == ENTRY_ERROR &&
               !(item.video && media_resource_path(v->cache[v->modal_entry].resource))) {
        /* A failed cover can still have a downloaded file for the player.
         * Refetch video bytes only after the user explicitly retries playback. */
        media_resource_retry(v->cache[v->modal_entry].resource);
        delete_entry(&v->cache[v->modal_entry]);
        v->cache[v->modal_entry].item = v->selected_item;
    }
    return true;
}
static void gallery_step(media_view *v, bool forward)
{
    size_t index = v->gallery_index;
    for (size_t i = 0; i < v->gallery_count; ++i) {
        index = forward ? (index + 1) % v->gallery_count
                        : (index + v->gallery_count - 1) % v->gallery_count;
        if (index == v->gallery_index)
            return;
        if (!gallery_member(v, index) || !v->model || !live(v->model, &v->gallery[index]))
            continue;
        message_media item = v->gallery[index];
        bool info_open = v->info_open, info_focused = v->info_focused;
        media_view_open(v, &item);
        v->info_open = info_open;
        v->info_focused = info_focused;
        return;
    }
}
static void select_item(media_view *v, int index)
{
    if (index >= 0 && index < v->visible_count)
        media_view_open(v, &v->cache[v->visible[index].entry].item);
}
bool media_view_modal(const media_view *v)
{
    return v && v->modal != MODAL_NONE;
}
media_context media_view_context(const media_view *v)
{
    if (!v || v->modal == MODAL_NONE)
        return MEDIA_CONTEXT_NONE;
    if (v->modal == MODAL_LIST)
        return MEDIA_CONTEXT_LIST;
    if (v->modal == MODAL_ERROR)
        return MEDIA_CONTEXT_ERROR;
    if (v->modal == MODAL_LOADING)
        return MEDIA_CONTEXT_LOADING;
    return v->selected_item.audio   ? MEDIA_CONTEXT_AUDIO
           : v->selected_item.video ? MEDIA_CONTEXT_VIDEO
                                    : MEDIA_CONTEXT_IMAGE;
}
static const double audio_speeds[] = {0.5, 0.75, 1, 1.25, 1.5, 1.75, 2};
enum { AUDIO_SPEED_COUNT = sizeof(audio_speeds) / sizeof(*audio_speeds) };
/* Called with the lock held: queue the next speed step from the reported one. */
static void step_speed(media_view *v, int direction)
{
    double current = v->speed_target > 0 ? v->speed_target : 1;
    int index = 2;
    for (int i = 0; i < AUDIO_SPEED_COUNT; ++i)
        if (fabs(audio_speeds[i] - current) < 0.01)
            index = i;
    index += direction;
    if (index < 0 || index >= AUDIO_SPEED_COUNT)
        return;
    v->speed_set = true;
    v->speed_level = v->speed_target = audio_speeds[index];
}
/* Called with the lock held: Space at the end of an audio starts it over. */
static void toggle_pause(media_view *v)
{
    if (v->selected_item.audio && v->ended && !v->looping) {
        v->restart_command = true;
        v->pause_commands = 0;
    } else
        ++v->pause_commands;
}
bool media_view_key(media_view *v, int type, uint32_t key)
{
    if (!v)
        return false;
    if (!media_view_modal(v)) {
        if (!shortcut_is(type, key, SHORTCUT_MESSAGES))
            return false;
        if (!v->visible_count) {
            v->modal = MODAL_ERROR;
            v->modal_error = "Nenhuma mídia disponível neste trecho do histórico.";
        } else if (v->visible_count == 1)
            select_item(v, 0);
        else {
            v->modal = MODAL_LIST;
            v->selected = 0;
        }
        return true;
    }
    if (type == OK && key == 27) {
        dismiss_modal(v);
        return true;
    }
    if (v->modal == MODAL_LIST) {
        if (type == KEY_CODE_YES && key == KEY_UP && v->selected)
            --v->selected;
        if (type == KEY_CODE_YES && key == KEY_DOWN && v->selected + 1 < v->visible_count)
            ++v->selected;
        if ((type == OK && (key == '\n' || key == '\r')) ||
            (type == KEY_CODE_YES && key == KEY_ENTER))
            select_item(v, v->selected);
        return true;
    }
    /* The arrows seek inside a video or audio, so those step with PageUp and
     * PageDown; with the properties panel open the same keys scroll it. */
    if (v->member_count > 1 && type == KEY_CODE_YES &&
        ((!v->selected_item.video && (key == KEY_LEFT || key == KEY_RIGHT)) ||
         (!v->info_open && (key == KEY_PPAGE || key == KEY_NPAGE)))) {
        gallery_step(v, key == KEY_RIGHT || key == KEY_NPAGE);
        return true;
    }
    if (v->modal != MODAL_LIST) {
        bool toggle = (type == OK && key == 'i') || shortcut_is(type, key, SHORTCUT_PINNED);
        if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
            v->info_focused = !v->info_focused;
            return true;
        }
        if (v->info_focused && ((type == OK && (key == '\n' || key == '\r')) ||
                                (type == KEY_CODE_YES && key == KEY_ENTER)))
            toggle = true;
        if (toggle) {
            v->info_open = !v->info_open;
            v->info_scroll = 0;
            if (key == 'i' || shortcut_is(type, key, SHORTCUT_PINNED))
                v->info_focused = false;
            return true;
        }
        if (v->info_open && type == KEY_CODE_YES && (key == KEY_PPAGE || key == KEY_NPAGE)) {
            media_view_scroll(v, v->layout.y + 2, v->layout.info_x, key == KEY_PPAGE ? -5 : 5);
            return true;
        }
    }
    if (v->modal == MODAL_ERROR &&
        ((type == OK && (key == '\r' || key == '\n')) ||
         (type == KEY_CODE_YES && key == KEY_ENTER)) &&
        v->modal_entry >= 0 && graphics_terminal_get().supported) {
        media_resource_retry(v->cache[v->modal_entry].resource);
        delete_entry(&v->cache[v->modal_entry]);
        v->cache[v->modal_entry].item = v->selected_item;
        v->modal = MODAL_LOADING;
        v->modal_token = 0;
        free(v->modal_source);
        v->modal_source = NULL;
        return true;
    }
    /* Player controls never reach a background audio behind an image. */
    if (!v->selected_item.video)
        return true;
    if (v->selected_item.audio && type == OK && key == 'x') {
        close_modal(v);
        return true;
    }
    g_mutex_lock(&v->lock);
    if (type == OK && key == ' ')
        toggle_pause(v);
    if (type == OK && key == 'm')
        ++v->mute_commands;
    if (v->selected_item.audio && type == OK && (key == '[' || key == ']'))
        step_speed(v, key == ']' ? 1 : -1);
    if (v->selected_item.audio && type == OK && key == 'l')
        ++v->loop_commands;
    if (type == KEY_CODE_YES && key == KEY_LEFT && v->seek_commands > -3600)
        v->seek_commands -= 5;
    if (type == KEY_CODE_YES && key == KEY_RIGHT && v->seek_commands < 3600)
        v->seek_commands += 5;
    if (type == OK && (key == '<' || key == ',') && v->seek_commands > -3600)
        --v->seek_commands;
    if (type == OK && (key == '>' || key == '.') && v->seek_commands < 3600)
        ++v->seek_commands;
    if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
        /* The level belongs to the client, so it survives the player and the
         * session; the player only applies what it receives. */
        int level = player_volume_get() + (key == KEY_UP ? 5 : -5);
        if (level < PLAYER_VOLUME_MIN)
            level = PLAYER_VOLUME_MIN;
        if (level > PLAYER_VOLUME_MAX)
            level = PLAYER_VOLUME_MAX;
        player_volume_set(level);
        v->volume_absolute = true;
        v->volume_level = level;
        v->volume_commands = 0;
    }
    g_cond_signal(&v->wake);
    g_mutex_unlock(&v->lock);
    return true;
}
bool media_view_scroll(media_view *v, int y, int x, int delta)
{
    if (!v || !media_view_modal(v) || !v->info_open || !modal_layout_update(v) ||
        x < v->layout.info_x || x >= v->layout.info_x + v->layout.info_width || y <= v->layout.y ||
        y >= v->layout.y + v->layout.height - 1)
        return false;
    int maximum = v->info_rows - (v->layout.height - 4);
    if (maximum < 0)
        maximum = 0;
    if (delta < 0)
        v->info_scroll = v->info_scroll < -delta ? 0 : v->info_scroll + delta;
    else
        v->info_scroll = delta > maximum - v->info_scroll ? maximum : v->info_scroll + delta;
    return true;
}
bool media_view_click(media_view *v, int y, int x)
{
    if (!v)
        return false;
    if (media_view_modal(v)) {
        if (!modal_layout_update(v) || !media_modal_contains(&v->layout, y, x)) {
            dismiss_modal(v);
            return true;
        }
        if (v->modal == MODAL_LIST) {
            int row = y - v->layout.y - 2;
            int first =
                v->selected >= v->layout.height - 5 ? v->selected - (v->layout.height - 6) : 0;
            if (x > v->layout.x && x < v->layout.x + v->layout.width - 1 && row >= 0 &&
                row < v->layout.height - 4 && first + row < v->visible_count)
                select_item(v, first + row);
        } else if (y == v->layout.button_y && x >= v->layout.button_x &&
                   x < v->layout.button_x + v->layout.button_width) {
            v->info_open = !v->info_open;
            v->info_scroll = 0;
            v->info_focused = true;
        } else if (v->selected_item.audio && v->modal == MODAL_PLAYING &&
                   y == v->layout.y + v->layout.height - 4) {
            media_audio_layout options;
            if (media_audio_geometry(v->layout.height - 4, 3, v->layout.area_width, &options)) {
                media_audio_control action =
                    media_audio_hit(&options, y - v->layout.y, x - v->layout.x);
                if (action == MEDIA_AUDIO_STOP) {
                    close_modal(v);
                    return true;
                }
                g_mutex_lock(&v->lock);
                if (action == MEDIA_AUDIO_SLOWER || action == MEDIA_AUDIO_FASTER)
                    step_speed(v, action == MEDIA_AUDIO_FASTER ? 1 : -1);
                else if (action == MEDIA_AUDIO_LOOP)
                    ++v->loop_commands;
                g_cond_signal(&v->wake);
                g_mutex_unlock(&v->lock);
            }
        } else if (v->selected_item.video && v->modal == MODAL_PLAYING) {
            media_controls_layout controls;
            if (media_controls_geometry(v->layout.height - 3, 3, v->layout.area_width, &controls)) {
                double fraction;
                media_control action =
                    media_controls_hit(&controls, y - v->layout.y, x - v->layout.x, &fraction);
                g_mutex_lock(&v->lock);
                if (action == MEDIA_CONTROL_PLAY)
                    toggle_pause(v);
                else if (action == MEDIA_CONTROL_SEEK && isfinite(v->duration) && v->duration > 0) {
                    v->seek_absolute = true;
                    v->seek_position = fraction * v->duration;
                    v->seek_commands = 0;
                } else if (action == MEDIA_CONTROL_VOLUME) {
                    int level = (int)lround(fraction * 100);
                    if (level < PLAYER_VOLUME_MIN)
                        level = PLAYER_VOLUME_MIN;
                    if (level > PLAYER_VOLUME_MAX)
                        level = PLAYER_VOLUME_MAX;
                    player_volume_set(level);
                    v->volume_absolute = true;
                    v->volume_level = level;
                    v->volume_commands = 0;
                }
                g_cond_signal(&v->wake);
                g_mutex_unlock(&v->lock);
            }
        }
        return true;
    }
    /* The inline bar's button pauses or resumes the background audio. */
    for (int i = 0; i < v->audio_row_count; ++i)
        if (v->audio.active && y == v->audio_rows[i].y && x >= v->audio_rows[i].x &&
            x < v->audio_rows[i].x + 3 && same(&v->audio_rows[i].item, &v->audio.item)) {
            g_mutex_lock(&v->lock);
            if (v->ended && !v->looping)
                v->restart_command = true;
            else
                ++v->pause_commands;
            g_cond_signal(&v->wake);
            g_mutex_unlock(&v->lock);
            return true;
        }
    for (int i = 0; i < v->visible_count; ++i) {
        placement *p = &v->visible[i];
        int height = p->height - p->top - p->bottom;
        if (height < 1)
            height = 1;
        if (x >= p->x && x < p->x + p->width && y >= p->y && y < p->y + height) {
            select_item(v, i);
            return true;
        }
    }
    return false;
}
/* m:ss, or h:mm:ss for long media; unknown values read as --:--. */
static void format_clock(double seconds, char *out, size_t size)
{
    if (!isfinite(seconds) || seconds < 0) {
        snprintf(out, size, "--:--");
        return;
    }
    long total = seconds > 359999 ? 359999 : (long)seconds;
    if (total >= 3600)
        snprintf(out, size, "%ld:%02ld:%02ld", total / 3600, total / 60 % 60, total % 60);
    else
        snprintf(out, size, "%ld:%02ld", total / 60, total % 60);
}
static void format_speed(double speed, char *out, size_t size)
{
    snprintf(out, size, "%g×", isfinite(speed) && speed > 0 ? speed : 1.0);
}
static void draw_properties(media_view *v, WINDOW *w)
{
    if (!v->info_open)
        return;
    media_modal_layout *g = &v->layout;
    int left = g->info_x - g->x, width = g->info_width - 2, available = g->height - 4;
    if (width < 1 || available < 1)
        return;
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    mvwvline(w, 1, left - 1, ACS_VLINE, g->height - 2);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 1, left, g->info_width, "Propriedades");
    message_media *item = &v->selected_item;
    char bytes[64] = "Não informado", dimensions[64] = "Não informadas";
    if (item->size_known)
        snprintf(bytes, sizeof(bytes), "%" PRId64 " bytes", item->size_bytes);
    int source_w = v->modal_image.source_width ? v->modal_image.source_width : item->source_width;
    int source_h =
        v->modal_image.source_height ? v->modal_image.source_height : item->source_height;
    if (source_w > 0 && source_h > 0)
        snprintf(dimensions, sizeof(dimensions), "%d × %d px", source_w, source_h);
    if (item->audio) {
        g_mutex_lock(&v->lock);
        double duration = v->duration;
        g_mutex_unlock(&v->lock);
        if (isfinite(duration) && duration > 0)
            format_clock(duration, dimensions, sizeof(dimensions));
        else
            snprintf(dimensions, sizeof(dimensions), "Não informada");
    }
    struct {
        const char *name, *value;
    } properties[] = {{"Nome", item->original_file_name},
                      {"MIME", item->mime},
                      {"Tamanho", bytes},
                      {"Moderação", item->moderation},
                      {item->audio ? "Duração" : "Dimensões originais", dimensions},
                      {"ID", item->id},
                      {"Mensagem", item->message},
                      {"Miniatura", item->thumbnail_id},
                      {"Criado em", item->created_at}};
    /* First measure to clamp scrolling, then paint only the visible rows.
     * The shared Unicode renderer sanitizes every remote property value. */
    for (int pass = 0; pass < 2; ++pass) {
        int row = 0;
        for (size_t i = 0; i < sizeof(properties) / sizeof(*properties); ++i) {
            text_buffer buffer;
            text_layout layout;
            const char *value = *properties[i].value ? properties[i].value : "Não informado";
            if (!text_decode(&buffer, value))
                text_decode(&buffer, "Não informado");
            text_measure(&buffer, width, &layout);
            int title = row - v->info_scroll;
            if (pass && title >= 0 && title < available) {
                wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED) | A_BOLD);
                text_label(w, title + 2, left, width, properties[i].name);
            }
            ++row;
            int first = v->info_scroll > row ? v->info_scroll - row : 0;
            int y = row > v->info_scroll ? row - v->info_scroll : 0;
            if (pass && y < available && first < layout.rows) {
                wattrset(w, COLOR_PAIR(THEME_PANEL));
                text_draw(w, &buffer, &layout, first, available - y, y + 2, left);
            }
            row += layout.rows + 1;
        }
        v->info_rows = row;
        int maximum = row > available ? row - available : 0;
        if (v->info_scroll > maximum)
            v->info_scroll = maximum;
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, g->height - 2, left, g->info_width, "PgUp/PgDn: rolar");
}

static double control_fraction(double value)
{
    return isfinite(value) ? fmin(1, fmax(0, value)) : 0;
}

static void draw_control_track(WINDOW *w, int row, int x, int width, double fraction, bool timeline,
                               bool muted, bool known, double downloaded)
{
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    mvwaddch(w, row, x - 1, '[');
    mvwaddch(w, row, x + width, ']');
    int marker = (int)(fraction * (width - 1));
    int filled = (int)ceil(fraction * width);
    double bounded = fmin(1, fmax(0, downloaded));
    int ready = (int)ceil(bounded * width);
    for (int i = 0; i < width; ++i) {
        if (timeline) {
            bool playhead = known && i == marker;
            bool played = known && i < marker;
            bool buffered = i < ready;
            /* Keep the played interval orange so the current position stays
             * visible; the downloaded part ahead of it is white and the
             * remaining part is gray. */
            int pair = playhead || played ? THEME_PANEL_ACCENT
                       : buffered         ? THEME_PANEL_BRIGHT
                                          : THEME_PANEL_MUTED;
            wattrset(w, COLOR_PAIR(pair));
            mvwaddnwstr(w, row, x + i, playhead ? L"●" : (played || buffered) ? L"━" : L"─", 1);
            continue;
        }
        int percent = width > 1 ? i * 100 / (width - 1) : 100;
        if (percent > (int)lround(fraction * 100))
            percent = (int)lround(fraction * 100);
        bool active = known && i < filled;
        int pair = active ? theme_volume_pair(percent) : THEME_PANEL_MUTED;
        wattrset(w, COLOR_PAIR(pair) | (muted ? A_DIM : 0));
        mvwaddnwstr(w, row, x + i, active ? L"█" : L"─", 1);
    }
}

static bool modal_download(media_view *v, size_t *available, size_t *total)
{
    *available = 0;
    *total = 0;
    if (!v || v->modal_entry < 0 || v->modal_entry >= CACHE_COUNT)
        return false;
    return media_resource_progress(v->cache[v->modal_entry].resource, available, total);
}
static unsigned download_percent(size_t available, size_t total)
{
    if (!total)
        return 0;
    size_t percent = available * 100 / total;
    return (unsigned)MIN(percent, 100U);
}
static void draw_video_controls(media_view *v, WINDOW *w)
{
    video_info info;
    g_mutex_lock(&v->lock);
    info = (video_info){.paused = v->paused,
                        .muted = v->muted,
                        .audio_missing = v->audio_missing,
                        .looping = v->looping,
                        .ended = v->ended,
                        .position = v->position,
                        .duration = v->duration,
                        .volume = v->volume,
                        .speed = v->speed};
    g_mutex_unlock(&v->lock);
    media_controls_layout controls;
    if (media_controls_geometry(v->layout.height - 3, 3, v->layout.area_width, &controls)) {
        const char *label =
            controls.play_width == 1 ? (info.paused ? "▶" : "⏸") : (info.paused ? "[▶]" : "[⏸]");
        button_draw(w, controls.row, controls.play_x, controls.play_width, label, BUTTON_NORMAL,
                    BUTTON_PANEL);
        bool known = isfinite(info.duration) && info.duration > 0;
        double position = known ? control_fraction(info.position / info.duration) : 0;
        double volume = control_fraction(info.volume / 100);
        size_t available = 0, total = 0;
        bool downloading = modal_download(v, &available, &total);
        /* The timeline keeps the played interval orange, marks the bytes
         * already on disk ahead of it in white and the rest in gray. Without a
         * known length, the played part is the only safe lower bound for what
         * is already buffered. */
        double downloaded = downloading
                                ? (total ? control_fraction((double)available / (double)total)
                                         : (known ? position : 0.0))
                                : 1.0;
        draw_control_track(w, controls.row, controls.timeline_x, controls.timeline_width, position,
                           true, false, known, downloaded);
        if (controls.volume_label_x >= 0) {
            wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
            mvwaddch(w, controls.row, controls.volume_label_x, 'V');
        }
        draw_control_track(w, controls.row, controls.volume_x, controls.volume_width, volume, false,
                           info.muted, true, 1.0);
        if (controls.percent_x >= 0) {
            char percent[8];
            snprintf(percent, sizeof(percent), "%3d%%", (int)lround(volume * 100));
            wattrset(w, COLOR_PAIR(THEME_PANEL));
            text_label(w, controls.row, controls.percent_x, 4, percent);
        }
    }
    char status[320];
    if (v->selected_item.audio) {
        char position[32], duration[32], speed[16];
        format_clock(info.position, position, sizeof(position));
        format_clock(info.duration > 0 ? info.duration : NAN, duration, sizeof(duration));
        format_speed(info.speed, speed, sizeof(speed));
        snprintf(status, sizeof(status),
                 "%s · %s / %s · %s%s%s%s · Espaço pausa · ←→ 5s%s · [ ] velocidade · "
                 "l repetir · ↑↓ volume · m · x parar",
                 info.ended && !info.looping ? "Fim"
                 : info.paused               ? "Pausado"
                                             : "Reproduzindo",
                 position, duration, speed, info.looping ? " · Repetir" : "",
                 info.muted ? " · Mudo" : "",
                 info.audio_missing ? " · Sem dispositivo de áudio" : "",
                 v->member_count > 1 ? " · PgUp/PgDn mídia" : "");
    } else
        snprintf(status, sizeof(status),
                 "%s · %.0f / %.0f s%s%s · Espaço pausa · ←→ 5s%s · ,/. 1s · ↑↓ volume · m",
                 info.paused ? "Pausado" : "Reproduzindo", info.position, info.duration,
                 info.muted ? " · Mudo" : "",
                 info.audio_missing ? " · Sem dispositivo de áudio" : "",
                 v->member_count > 1 ? " · PgUp/PgDn mídia" : "");
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, v->layout.height - 2, 3, v->layout.area_width, status);
}
static void draw_centered(WINDOW *w, int row, int x, int width, const char *text)
{
    int used = text_label(NULL, 0, 0, width, text);
    text_label(w, row, x + (width - used) / 2, width, text);
}
/* Sound-only player: a card stands in for the missing picture, and the
 * option row sits between the picture area and the transport controls. */
static void draw_audio(media_view *v, WINDOW *w)
{
    g_mutex_lock(&v->lock);
    double position = v->position, duration = v->duration, speed = v->speed;
    bool looping = v->looping;
    g_mutex_unlock(&v->lock);
    int left = v->layout.area_x - v->layout.x, top = v->layout.area_y - v->layout.y;
    int width = v->layout.area_width, rows = v->layout.area_height;
    if (!v->modal_image.data && rows >= 1) {
        const message_media *item = &v->selected_item;
        char details[128], clock[96], now[32], total[32];
        if (item->size_known)
            snprintf(details, sizeof(details), "%s · %" PRId64 " bytes",
                     *item->mime ? item->mime : "áudio", item->size_bytes);
        else
            snprintf(details, sizeof(details), "%s", *item->mime ? item->mime : "áudio");
        format_clock(position, now, sizeof(now));
        format_clock(duration > 0 ? duration : NAN, total, sizeof(total));
        snprintf(clock, sizeof(clock), "%s / %s", now, total);
        const char *name = *item->original_file_name ? item->original_file_name : item->label;
        int first = top + (rows > 5 ? (rows - 5) / 2 : 0);
        wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
        draw_centered(w, first, left, width, "♪");
        if (rows >= 3) {
            wattrset(w, COLOR_PAIR(THEME_PANEL) | A_BOLD);
            draw_centered(w, first + 2, left, width, name);
        }
        if (rows >= 4) {
            wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
            draw_centered(w, first + 3, left, width, details);
        }
        if (rows >= 5) {
            wattrset(w, COLOR_PAIR(THEME_PANEL));
            draw_centered(w, first + 4, left, width, clock);
        }
    }
    media_audio_layout options;
    if (!media_audio_geometry(v->layout.height - 4, 3, width, &options))
        return;
    char label[16];
    format_speed(speed, label, sizeof(label));
    button_draw(w, options.row, options.slower_x, 3, "[−]", BUTTON_NORMAL, BUTTON_PANEL);
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    draw_centered(w, options.row, options.speed_x, MEDIA_AUDIO_SPEED_WIDTH, label);
    button_draw(w, options.row, options.faster_x, 3, "[+]", BUTTON_NORMAL, BUTTON_PANEL);
    if (options.loop_width)
        button_draw(w, options.row, options.loop_x, options.loop_width,
                    looping ? "[Repetir: Sim]" : "[Repetir: Não]",
                    looping ? BUTTON_SELECTED : BUTTON_NORMAL, BUTTON_PANEL);
    if (options.stop_width)
        button_draw(w, options.row, options.stop_x, options.stop_width, "[Parar]", BUTTON_NORMAL,
                    BUTTON_PANEL);
}

void media_view_draw(media_view *v)
{
    if (!v)
        return;
    if (!media_view_modal(v)) {
        for (int i = 0; i < v->visible_count; ++i) {
            placement *p = &v->visible[i];
            cache_entry *e = &v->cache[p->entry];
            if (p->height - p->top - p->bottom < 1 || e->state == ENTRY_READY || p->y >= LINES ||
                p->x >= COLS)
                continue;
            WINDOW *notice = newwin(1, p->width, p->y, p->x);
            if (notice) {
                wbkgd(notice, COLOR_PAIR(THEME_PANEL_MUTED));
                text_label(notice, 0, 0, p->width,
                           e->state == ENTRY_ERROR ? e->error
                           : e->item.video         ? "Carregando prévia do vídeo…"
                                                   : "Carregando imagem…");
                wnoutrefresh(notice);
                delwin(notice);
            }
        }
        return;
    }
    if (!modal_layout_update(v))
        return;
    int width = v->layout.width, height = v->layout.height;
    if (!v->window)
        v->window = newwin(height, width, v->layout.y, v->layout.x);
    if (!v->window)
        return;
    WINDOW *w = v->window;
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 0, 2, width - 4,
               v->modal != MODAL_LIST && v->selected_item.audio ? " Áudio · Esc: segundo plano "
                                                                : " Mídia · Esc: voltar ");
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    if (v->modal == MODAL_LIST) {
        text_label(w, 1, 2, width - 4, "Escolha uma mídia · ↑↓ selecionar · Enter abrir");
        int first = v->selected >= height - 5 ? v->selected - (height - 6) : 0;
        for (int i = first; i < v->visible_count && i - first < height - 4; ++i) {
            wattrset(w, COLOR_PAIR(i == v->selected ? THEME_PANEL_ACCENT : THEME_PANEL));
            text_label(w, 2 + i - first, 2, width - 4, v->cache[v->visible[i].entry].item.label);
        }
    } else {
        int image_width = v->layout.area_width;
        int label_x = v->layout.button_x + v->layout.button_width + 2;
        int label_end = v->info_open ? v->layout.info_x - 1 : v->layout.x + width - 2;
        if (label_end > label_x) {
            char title[MESSAGE_MEDIA_LABEL + 32];
            if (v->selected_item.video && v->member_count > 1) {
                size_t number = 0;
                for (size_t i = 0; i <= v->gallery_index; ++i)
                    number += gallery_member(v, i);
                snprintf(title, sizeof(title), "%zu/%zu · %s", number, v->member_count,
                         v->selected_item.label);
            } else
                snprintf(title, sizeof(title), "%s", v->selected_item.label);
            text_label(w, 1, label_x - v->layout.x, label_end - label_x, title);
        }
        button_draw(w, v->layout.button_y - v->layout.y, v->layout.button_x - v->layout.x,
                    v->layout.button_width, "[Info]",
                    v->info_focused ? BUTTON_FOCUSED
                    : v->info_open  ? BUTTON_SELECTED
                                    : BUTTON_NORMAL,
                    BUTTON_PANEL);
        if (v->modal == MODAL_LOADING) {
            char loading[96];
            size_t available = 0, total = 0;
            if (modal_download(v, &available, &total) && total)
                snprintf(loading, sizeof(loading), "Carregando mídia… %u%%",
                         download_percent(available, total));
            else
                snprintf(loading, sizeof(loading), "Carregando mídia…");
            text_label(w, 3, 3, image_width, loading);
        } else if (v->modal == MODAL_ERROR) {
            text_label(w, 3, 3, image_width,
                       v->modal_error ? v->modal_error : "Mídia indisponível.");
            text_label(w, height - 3, 3, image_width, "Enter: tentar novamente");
        } else if (v->selected_item.audio) {
            draw_audio(v, w);
            draw_video_controls(v, w);
        } else if (v->selected_item.video)
            draw_video_controls(v, w);
        else
            text_label(w, height - 3, 3, image_width, v->modal_animated ? "GIF animado" : "Imagem");
        if (!v->selected_item.video && v->member_count > 1) {
            size_t number = 0;
            for (size_t i = 0; i <= v->gallery_index; ++i)
                number += gallery_member(v, i);
            char hint[128];
            snprintf(hint, sizeof(hint), "%zu/%zu · ←→ PgUp/PgDn %s · i: informações · Esc: voltar",
                     number, v->member_count,
                     v->member_count == v->image_count ? "imagens" : "mídias");
            text_label(w, height - 2, 3, image_width, hint);
        } else if (!v->selected_item.video || v->modal != MODAL_PLAYING)
            text_label(w, height - 2, 3, image_width,
                       v->selected_item.audio
                           ? (v->member_count > 1
                                  ? "Espaço pausa · x parar · PgUp/PgDn mídia · Esc: segundo plano"
                                  : "Espaço pausa · x parar · Esc: segundo plano")
                       : v->selected_item.video
                           ? (v->member_count > 1
                                  ? "Espaço pausa · ←→ 5s · PgUp/PgDn mídia · ↑↓ volume · m"
                                  : "Espaço pausa · ←→ 5s · ,/. 1s · ↑↓ volume · m")
                           : "i: informações · Esc: voltar");
        draw_properties(v, w);
    }
    touchwin(w);
    wnoutrefresh(w);
}
void media_view_prepare(media_view *v)
{
    if (!v || !graphics_terminal_get().supported)
        return;
    if (media_view_modal(v)) {
        /* An audio without a cover has no raster to place. */
        if (v->window && v->modal == MODAL_PLAYING &&
            (v->modal_image.data || !v->selected_item.audio) && modal_layout_update(v)) {
            /* Recalculate after a decode result changes the raster dimensions. */
            if (!v->selected_item.video && media_cache_overlay_count(v->store) &&
                v->modal_entry >= 0 && v->modal_entry < CACHE_COUNT) {
                cache_entry *e = &v->cache[v->modal_entry];
                image_decode_request request = {.terminal = graphics_terminal_get()};
                media_resource_place_image(e->resource, !strcmp(e->item.role, "author"), request,
                                           &v->modal_image, v->modal_image.id, e->fragments,
                                           v->layout.image_y, v->layout.image_x, 0, 0, 0);
            } else if (v->selected_item.video && v->modal_image.protocol == GRAPHICS_KITTY)
                graphics_image_place_scaled(&v->modal_image, v->layout.image_y, v->layout.image_x);
            else
                graphics_image_place(&v->modal_image, v->layout.image_y, v->layout.image_x, 0, 0);
        }
        return;
    }
    for (int i = 0; i < v->visible_count; ++i) {
        placement *p = &v->visible[i];
        cache_entry *e = &v->cache[p->entry];
        graphics_terminal t = graphics_terminal_get();
        if (e->state != ENTRY_READY || e->image.protocol != t.protocol)
            continue;
        const graphics_image *image = &p->image;
        media_resource_place_image(
            e->resource, !strcmp(e->item.role, "author"), p->request, image, e->placement_id,
            e->fragments, p->y, p->x, t.protocol == GRAPHICS_KITTY ? p->top * t.cell_height : 0,
            t.protocol == GRAPHICS_KITTY ? p->bottom * t.cell_height : 0, false);
    }
}

bool media_view_audio(const media_view *v, char channel[37], char message[37], char created_at[64])
{
    if (!v || !v->audio.active)
        return false;
    if (channel)
        snprintf(channel, 37, "%s", v->audio.channel);
    if (message)
        snprintf(message, 37, "%s", v->audio.item.message);
    if (created_at)
        snprintf(created_at, 64, "%s", v->audio.created_at);
    return true;
}
void media_view_audio_stop(media_view *v)
{
    stop_audio(v);
    if (v)
        v->mini_width = 0;
}
bool media_view_audio_resume(media_view *v)
{
    if (!v || !v->audio.active)
        return false;
    message_media item = v->audio.item;
    return media_view_open(v, &item);
}
void media_view_draw_audio(media_view *v, WINDOW *w, int row)
{
    if (!v)
        return;
    v->mini_width = 0;
    if (!v->audio.active || !w)
        return;
    g_mutex_lock(&v->lock);
    bool paused = v->paused || (v->ended && !v->looping);
    double position = v->position, duration = v->duration;
    g_mutex_unlock(&v->lock);
    char now[32], total[32], clock[72];
    format_clock(position, now, sizeof(now));
    format_clock(duration > 0 ? duration : NAN, total, sizeof(total));
    snprintf(clock, sizeof(clock), " %s/%s", now, total);
    const char *play = paused ? "[▶]" : "[⏸]", *stop = "[✕]", *hint = " · Ctrl+X ";
    const message_media *item = &v->audio.item;
    const char *name = *item->original_file_name ? item->original_file_name : item->label;
    int columns = getmaxx(w);
    int play_w = text_label(NULL, 0, 0, 8, play), stop_w = text_label(NULL, 0, 0, 8, stop);
    int clock_w = text_label(NULL, 0, 0, 40, clock), hint_w = text_label(NULL, 0, 0, 20, hint);
    int budget = columns / 2 > 64 ? 64 : columns / 2;
    if (budget < 20)
        budget = columns - 2;
    /* " ♪ name [⏸] 1:23/3:45 · Ctrl+X [✕] ": drop the hint, then the name. */
    int fixed = 3 + 1 + play_w + clock_w + hint_w + stop_w + 1;
    if (fixed > budget) {
        hint = " ";
        hint_w = 1;
        fixed = 3 + 1 + play_w + clock_w + hint_w + stop_w + 1;
    }
    if (fixed > budget)
        return;
    int name_w = text_label(NULL, 0, 0, budget - fixed, name);
    int width = fixed + name_w;
    int x = columns - width - 1;
    if (x < 0)
        return;
    int cursor = x;
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    cursor += text_label(w, row, cursor, 3, " ♪ ");
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    cursor += text_label(w, row, cursor, name_w, name);
    cursor += text_label(w, row, cursor, 1, " ");
    int play_x = cursor;
    cursor += button_draw(w, row, cursor, play_w, play, BUTTON_NORMAL, BUTTON_PANEL);
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    cursor += text_label(w, row, cursor, clock_w, clock);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    cursor += text_label(w, row, cursor, hint_w, hint);
    int stop_x = cursor;
    cursor += button_draw(w, row, cursor, stop_w, stop, BUTTON_NORMAL, BUTTON_PANEL);
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    text_label(w, row, cursor, 1, " ");
    v->mini_y = getbegy(w) + row;
    v->mini_x = getbegx(w) + x;
    v->mini_width = width;
    v->mini_play_x = getbegx(w) + play_x;
    v->mini_play_w = play_w;
    v->mini_stop_x = getbegx(w) + stop_x;
    v->mini_stop_w = stop_w;
}
media_mini_action media_view_audio_click(media_view *v, int y, int x)
{
    if (!v || !v->audio.active || !v->mini_width || y != v->mini_y || x < v->mini_x ||
        x >= v->mini_x + v->mini_width)
        return MEDIA_MINI_NONE;
    if (x >= v->mini_stop_x && x < v->mini_stop_x + v->mini_stop_w) {
        media_view_audio_stop(v);
        return MEDIA_MINI_HANDLED;
    }
    if (x >= v->mini_play_x && x < v->mini_play_x + v->mini_play_w) {
        g_mutex_lock(&v->lock);
        if (v->ended && !v->looping)
            v->restart_command = true;
        else
            ++v->pause_commands;
        g_cond_signal(&v->wake);
        g_mutex_unlock(&v->lock);
        return MEDIA_MINI_HANDLED;
    }
    return MEDIA_MINI_OPEN;
}

void media_view_audio_row(media_view *v, const message_media *item, int y, int x, int width,
                          int pair)
{
    if (!v || !item || !item->audio || v->audio_row_count == VISIBLE_COUNT)
        return;
    int i = v->audio_row_count++;
    v->audio_rows[i].item = *item;
    v->audio_rows[i].y = y;
    v->audio_rows[i].x = x;
    v->audio_rows[i].width = width;
    v->audio_rows[i].pair = pair;
}
/* 3,2 MB: one decimal below 100 units, with the decimal comma of the UI. */
static void format_size(int64_t bytes, char *out, size_t size)
{
    if (bytes < 1024) {
        snprintf(out, size, "%" PRId64 " B", bytes);
        return;
    }
    const char *unit = "KB";
    double value = bytes / 1024.0;
    if (value >= 1024) {
        value /= 1024;
        unit = "MB";
    }
    if (value >= 1024) {
        value /= 1024;
        unit = "GB";
    }
    snprintf(out, size, value >= 100 ? "%.0f %s" : "%.1f %s", value, unit);
    char *point = strchr(out, '.');
    if (point)
        *point = ',';
}
void media_view_draw_rows(media_view *v)
{
    if (!v || media_view_modal(v) || !v->audio_row_count)
        return;
    g_mutex_lock(&v->lock);
    bool paused = v->paused || (v->ended && !v->looping);
    double position = v->position, duration = v->duration;
    g_mutex_unlock(&v->lock);
    for (int i = 0; i < v->audio_row_count; ++i) {
        const message_media *item = &v->audio_rows[i].item;
        int y = v->audio_rows[i].y, x = v->audio_rows[i].x;
        int width = MIN(v->audio_rows[i].width, 72);
        if (y < 0 || y >= LINES || x < 0 || x >= COLS)
            continue;
        width = MIN(width, COLS - x);
        if (width < 8)
            continue;
        int base = v->audio_rows[i].pair;
        int muted = base == THEME_HISTORY_AREA ? THEME_HISTORY_MUTED
                    : base == THEME_EMBED_AREA ? THEME_EMBED_MUTED
                                               : THEME_PANEL_MUTED;
        int accent = base == THEME_HISTORY_AREA ? THEME_HISTORY_ACCENT
                     : base == THEME_EMBED_AREA ? THEME_EMBED_ACCENT
                                                : THEME_PANEL_ACCENT;
        bool current = v->audio.active && same(item, &v->audio.item);
        char right[80];
        if (current) {
            char now[32], total[32];
            format_clock(position, now, sizeof(now));
            format_clock(duration > 0 ? duration : NAN, total, sizeof(total));
            snprintf(right, sizeof(right), "%s / %s", now, total);
        } else if (item->size_known)
            format_size(item->size_bytes, right, sizeof(right));
        else
            snprintf(right, sizeof(right), "Áudio");
        WINDOW *w = newwin(1, width, y, x);
        if (!w)
            continue;
        wbkgd(w, COLOR_PAIR(base));
        werase(w);
        wattrset(w, COLOR_PAIR(accent) | A_BOLD);
        int cursor = text_label(w, 0, 0, 3, current && !paused ? "[⏸]" : "[▶]") + 1;
        int right_w = text_label(NULL, 0, 0, width, right);
        const char *name = *item->original_file_name ? item->original_file_name : item->label;
        /* Keep at least an 8-cell track; narrow rows drop it before the name. */
        int name_room = width - cursor - 2 - 8 - 2 - right_w;
        bool track = name_room >= 6;
        if (!track)
            name_room = width - cursor - 2 - right_w;
        wattrset(w, COLOR_PAIR(base) | A_BOLD);
        cursor += text_label(w, 0, cursor, MAX(0, name_room), name) + 2;
        int track_w = width - cursor - 2 - right_w;
        if (track && track_w > 0) {
            bool known = current && isfinite(duration) && duration > 0;
            double fraction = known ? control_fraction(position / duration) : 0;
            int marker = (int)(fraction * (track_w - 1));
            for (int k = 0; k < track_w; ++k) {
                bool played = known && k < marker, head = known && k == marker;
                wattrset(w, COLOR_PAIR(played || head ? accent : muted));
                mvwaddnwstr(w, 0, cursor + k, head ? L"●" : played ? L"━" : L"─", 1);
            }
        }
        wattrset(w, COLOR_PAIR(current ? base : muted));
        if (right_w <= width)
            text_label(w, 0, width - right_w, right_w, right);
        wnoutrefresh(w);
        delwin(w);
    }
}
