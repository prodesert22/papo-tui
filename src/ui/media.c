#define _POSIX_C_SOURCE 200809L
#include "ui/media.h"
#include "ui/graphics.h"
#include "ui/image_decode.h"
#include "ui/media_layout.h"
#include "ui/button.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "net/media.h"
#include "net/clock.h"
#include <libavutil/log.h>
#include <libavutil/base64.h>
#include <mpv/client.h>
#include <mpv/render.h>
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
#define CACHE_BYTES (64U * 1024U * 1024U)
typedef enum { ENTRY_NEW, ENTRY_FETCH, ENTRY_FILE, ENTRY_JOB, ENTRY_READY, ENTRY_ERROR } entry_state;
typedef struct {
    message_media item;
    entry_state state;
    char *path;
    graphics_image image;
    uint64_t token, used;
    int width, height;
    animation *animation;
    uint64_t epoch, presented;
    size_t frame, requested_frame;
    bool encoding;
    const char *error;
} cache_entry;
typedef struct { int entry, y, x, width, height, top, bottom; } placement;
typedef struct {
    char *path;
    uint64_t token;
    int width, height;
    bool video;
    graphics_terminal terminal;
    uint32_t image_id;
    bool preview, native;
    animation *animation;
    size_t frame;
    int top, bottom;
} job;
struct media_view {
    auth_client *auth;
    media_download *download;
    cache_entry cache[CACHE_COUNT];
    placement visible[VISIBLE_COUNT];
    int visible_count, fetching;
    uint64_t serial, tick, generation;
    graphics_terminal terminal;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    bool started, stopping, pending, result_ready;
    atomic_uint_fast64_t active;
    job task;
    graphics_image result;
    animation *result_animation, *modal_animation;
    uint64_t modal_epoch, modal_presented;
    size_t modal_frame, modal_requested_frame;
    bool modal_encoding;
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
    if (e->path) { unlink(e->path); free(e->path); }
    animation_free(e->animation); graphics_image_free(&e->image); memset(e, 0, sizeof(*e));
}
static void free_job(job *j) { animation_free(j->animation); free(j->path); memset(j, 0, sizeof(*j)); }
typedef struct { media_view *view; uint64_t token; } image_guard;
static bool image_interrupt(void *context)
{
    image_guard *guard = context;
    return atomic_load(&guard->view->active) != guard->token;
}
static image_decode_request decode_request(const job *j)
{
    return (image_decode_request){.path = j->path, .width = j->width, .height = j->height,
        .terminal = j->terminal, .image_id = j->image_id, .preview = j->preview,
        .native = j->native, .animation = j->animation, .frame = j->frame, .top = j->top, .bottom = j->bottom};
}
static mpv_handle *player_create(mpv_render_context **render)
{
    mpv_handle *player = mpv_create();
    if (!player) return NULL;
    const char *options[][2] = {{"config", "no"}, {"terminal", "no"}, {"load-scripts", "no"},
        {"ytdl", "no"}, {"input-terminal", "no"}, {"input-default-bindings", "no"},
        {"vo", "libmpv"}, {"hwdec", "no"}, {"keep-open", "yes"}, {"access-references", "no"},
        {"audio-fallback-to-null", "yes"}, {"vd-lavc-o", "max_pixels=16777216"},
        {"demuxer", "lavf"}, {"demuxer-lavf-o", "protocol_whitelist=file"}, {"volume", "50"}};
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i)
        if (mpv_set_option_string(player, options[i][0], options[i][1]) < 0) goto error;
    if (mpv_initialize(player) < 0) goto error;
    mpv_render_param parameters[] = {{MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_SW}, {MPV_RENDER_PARAM_INVALID, NULL}};
    if (mpv_render_context_create(render, player, parameters) < 0) goto error;
    return player;
 error:
    mpv_terminate_destroy(player); return NULL;
}
static void publish(media_view *v, uint64_t token, graphics_image *image, animation *animated, const char *error)
{
    pthread_mutex_lock(&v->lock);
    animation_free(v->result_animation); v->result_animation = animated;
    graphics_image_free(&v->result); v->result = *image; memset(image, 0, sizeof(*image));
    v->result_token = token; v->result_error = error; v->result_ready = true;
    pthread_mutex_unlock(&v->lock);
}
static void *worker(void *context)
{
    media_view *v = context;
    job current = {0};
    mpv_handle *player = NULL;
    mpv_render_context *render = NULL;
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
            mpv_render_context_free(render); mpv_terminate_destroy(player); player = NULL; render = NULL;
            free_job(&current);
        }
        if (pending) {
            if (atomic_load(&v->active) != next.token) { free_job(&next); continue; }
            if (!next.video) {
                graphics_image image = {0};
                animation *animated = NULL;
                bool ok;
                image_decode_request request = decode_request(&next);
                image_guard guard = {v, next.token};
                if (next.animation) ok = image_encode_frame(&request, &image);
                else ok = image_decode_file(&request, &image, &animated, image_interrupt, &guard);
                publish(v, next.token, &image, animated, ok ? NULL : "Imagem inválida, grande demais ou não suportada.");
                free_job(&next); continue;
            }
            if (reuse) { free_job(&current); current = next; last_frame = 0; force_frame = true; }
            else {
            current = next;
            force_frame = true;
            player = player_create(&render);
            const char *args[] = {"loadfile", current.path, "replace", NULL};
            if (!player || mpv_command(player, args) < 0) {
                graphics_image empty = {0}; publish(v, current.token, &empty, NULL, "Não foi possível iniciar o player.");
                if (render) mpv_render_context_free(render);
                if (player) mpv_terminate_destroy(player);
                render = NULL; player = NULL; free_job(&current); continue;
            }
            }
        }
        if (player) {
            if (pause % 2) { const char *args[] = {"cycle", "pause", NULL}; mpv_command(player, args); }
            if (mute % 2) { const char *args[] = {"cycle", "mute", NULL}; mpv_command(player, args); }
            if (seek) { char amount[32]; snprintf(amount, sizeof(amount), "%d", seek); const char *args[] = {"seek", amount, "relative", NULL}; mpv_command(player, args); }
            if (volume) { char amount[32]; snprintf(amount, sizeof(amount), "%d", volume); const char *args[] = {"add", "volume", amount, NULL}; mpv_command(player, args); }
            mpv_event *event;
            while ((event = mpv_wait_event(player, 0))->event_id != MPV_EVENT_NONE) {
                if (event->event_id == MPV_EVENT_END_FILE) {
                    const mpv_event_end_file *end = event->data;
                    if (end && end->error < 0) { graphics_image empty = {0}; publish(v, current.token, &empty, NULL, "Vídeo inválido ou codec indisponível."); }
                }
            }
            int paused = 0, muted = 0;
            double position = 0, duration = 0, volume_value = 50;
            mpv_get_property(player, "pause", MPV_FORMAT_FLAG, &paused);
            mpv_get_property(player, "mute", MPV_FORMAT_FLAG, &muted);
            mpv_get_property(player, "time-pos", MPV_FORMAT_DOUBLE, &position);
            mpv_get_property(player, "duration", MPV_FORMAT_DOUBLE, &duration);
            mpv_get_property(player, "volume", MPV_FORMAT_DOUBLE, &volume_value);
            char *ao = mpv_get_property_string(player, "current-ao");
            pthread_mutex_lock(&v->lock);
            bool missing = ao && !strcmp(ao, "null");
            v->status_changed |= v->paused != (paused != 0) || v->muted != (muted != 0) ||
                v->audio_missing != missing || fabs(v->position - position) >= 1 || v->volume != volume_value;
            v->audio_missing = missing;
            v->playing = true; v->paused = paused != 0; v->muted = muted != 0;
            v->position = position; v->duration = duration; v->volume = volume_value;
            pthread_mutex_unlock(&v->lock);
            mpv_free(ao);
            uint64_t now = 0;
            if (net_now_ms(&now) && now - last_frame >= 67 && (force_frame || (mpv_render_context_update(render) & MPV_RENDER_UPDATE_FRAME))) {
                last_frame = now; force_frame = false;
                size_t pixels = (size_t)current.width * (size_t)current.height;
                unsigned char *rgba = malloc(pixels * 4), *rgb = malloc(pixels * 3);
                graphics_image image = {0};
                if (rgba && rgb) {
                    int dimensions[] = {current.width, current.height}; size_t stride = (size_t)current.width * 4;
                    mpv_render_param params[] = {{MPV_RENDER_PARAM_SW_SIZE, dimensions}, {MPV_RENDER_PARAM_SW_FORMAT, "rgb0"},
                        {MPV_RENDER_PARAM_SW_STRIDE, &stride}, {MPV_RENDER_PARAM_SW_POINTER, rgba}, {MPV_RENDER_PARAM_INVALID, NULL}};
                    if (mpv_render_context_render(render, params) >= 0) {
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
    if (render) mpv_render_context_free(render);
    if (player) mpv_terminate_destroy(player);
    free_job(&current); return NULL;
}
static bool submit_job(media_view *v, const job *request)
{
    char *copy = strdup(request->path);
    if (!copy) return false;
    pthread_mutex_lock(&v->lock);
    free_job(&v->task);
    v->task = *request; v->task.path = copy;
    v->task.terminal = graphics_terminal_get();
    if (!v->task.image_id) v->task.image_id = graphics_image_next_id();
    animation_ref(v->task.animation);
    v->pending = true; atomic_store(&v->active, request->token); pthread_cond_signal(&v->wake);
    pthread_mutex_unlock(&v->lock); return true;
}
static bool native_media(const message_media *item)
{
    return !strcmp(item->role, "attachment") || !strcmp(item->role, "image");
}

media_view *media_view_create(auth_client *auth)
{
    media_view *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->auth = auth; v->fetching = v->modal_entry = -1;
    v->generation = auth_context_generation(auth);
    v->download = media_download_create(auth);
    if (!v->download) { free(v); return NULL; }
    if (pthread_mutex_init(&v->lock, NULL)) { media_download_destroy(v->download); free(v); return NULL; }
    if (pthread_cond_init(&v->wake, NULL)) { pthread_mutex_destroy(&v->lock); media_download_destroy(v->download); free(v); return NULL; }
    atomic_init(&v->active, 0);
    av_log_set_level(AV_LOG_QUIET);
    if (pthread_create(&v->thread, NULL, worker, v)) {
        pthread_cond_destroy(&v->wake); pthread_mutex_destroy(&v->lock);
        media_download_destroy(v->download); free(v); return NULL;
    }
    v->started = true; return v;
}
static void close_modal(media_view *v)
{
    if (!v) return;
    atomic_store(&v->active, 0);
    if (v->fetching >= 0 && v->fetching == v->modal_entry) {
        media_download_cancel(v->download); v->cache[v->fetching].state = ENTRY_NEW; v->fetching = -1;
    }
    if (v->modal_entry >= 0 && v->cache[v->modal_entry].item.video) {
        cache_entry *e = &v->cache[v->modal_entry];
        if (e->path) { unlink(e->path); free(e->path); e->path = NULL; }
        e->state = ENTRY_NEW;
    }
    for (int i = 0; i < CACHE_COUNT; ++i) {
        cache_entry *e = &v->cache[i];
        if (e->encoding || e->state == ENTRY_JOB) { e->encoding = false; e->token = 0; if (e->state == ENTRY_JOB) e->state = ENTRY_FILE; }
    }
    pthread_mutex_lock(&v->lock);
    v->playing = false; v->pause_commands = v->seek_commands = v->volume_commands = v->mute_commands = 0;
    pthread_cond_signal(&v->wake); pthread_mutex_unlock(&v->lock);
    if (v->window) { delwin(v->window); v->window = NULL; }
    animation_free(v->modal_animation); v->modal_animation = NULL; v->modal_encoding = false;
    graphics_image_free(&v->modal_image); v->modal = MODAL_NONE; v->modal_entry = -1;
    v->modal_token = 0; v->modal_error = NULL;
    v->info_open = v->info_focused = false; v->info_scroll = v->info_rows = 0;
    v->modal_width = v->modal_height = 0;
}
void media_view_reset(media_view *v)
{
    if (!v) return;
    graphics_images_clear();
    close_modal(v); media_download_cancel(v->download); v->fetching = -1;
    for (int i = 0; i < CACHE_COUNT; ++i) delete_entry(&v->cache[i]);
    v->visible_count = 0; v->generation = auth_context_generation(v->auth);
}
void media_view_destroy(media_view *v)
{
    if (!v) return;
    pthread_mutex_lock(&v->lock); v->stopping = true; atomic_store(&v->active, 0);
    pthread_cond_signal(&v->wake); pthread_mutex_unlock(&v->lock);
    if (v->started) pthread_join(v->thread, NULL);
    media_view_reset(v); free_job(&v->task); graphics_image_free(&v->result); animation_free(v->result_animation);
    pthread_cond_destroy(&v->wake); pthread_mutex_destroy(&v->lock);
    media_download_destroy(v->download); free(v);
}
static bool same(const message_media *a, const message_media *b)
{
    return a->kind == b->kind && !strcmp(a->id, b->id) && !strcmp(a->role, b->role) && !strcmp(a->message, b->message) && a->version == b->version;
}
static int cache_find(media_view *v, const message_media *item)
{
    int oldest = -1;
    for (int i = 0; i < CACHE_COUNT; ++i) {
        if (*v->cache[i].item.id && same(&v->cache[i].item, item)) return i;
        if (i == v->fetching || i == v->modal_entry || v->cache[i].state == ENTRY_JOB) continue;
        bool visible = false;
        for (int j = 0; j < v->visible_count; ++j) visible |= v->visible[j].entry == i;
        if (!visible && (oldest < 0 || v->cache[i].used < v->cache[oldest].used)) oldest = i;
    }
    if (oldest >= 0) { delete_entry(&v->cache[oldest]); v->cache[oldest].item = *item; }
    return oldest;
}
void media_view_measure(void *context, const message_media *item, int available_width, int *width, int *height)
{
    media_view *v = context;
    if (!v || !native_media(item)) return;
    graphics_terminal t = graphics_terminal_get();
    if (!t.supported) return;
    for (int i = 0; i < CACHE_COUNT; ++i) {
        cache_entry *e = &v->cache[i];
        if (!same(&e->item, item) || !e->image.source_width || !e->image.source_height) continue;
        int max_w = available_width * t.cell_width;
        if (max_w > 640) max_w = 640;
        double scale = fmin(1, fmin((double)max_w / e->image.source_width, 360.0 / e->image.source_height));
        int w = (int)(e->image.source_width * scale), h = (int)(e->image.source_height * scale);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        *width = (w + t.cell_width - 1) / t.cell_width;
        *height = (h + t.cell_height - 1) / t.cell_height;
        return;
    }
}
void media_view_begin(media_view *v) { if (v) { graphics_frame_begin(); v->visible_count = 0; ++v->tick; } }
void media_view_offer(media_view *v, const message_media *item, int y, int x, int width, int height, int top, int bottom)
{
    if (!v || v->visible_count == VISIBLE_COUNT) return;
    int index = cache_find(v, item);
    if (index < 0) return;
    cache_entry *e = &v->cache[index]; e->used = v->tick;
    v->visible[v->visible_count++] = (placement){index, y, x, width, height, top, bottom};
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
static bool blob_file(media_view *v, json_t *data, char **path)
{
    if (!json_is_string(data) || json_string_length(data) != strlen(json_string_value(data))) return false;
    size_t size = json_string_length(data);
    if (!size || size > (MEDIA_IMAGE_LIMIT + 2U) / 3U * 4U || size % 4) return false;
    unsigned char *bytes = malloc(size / 4 * 3 + 1);
    if (!bytes) return false;
    int length = av_base64_decode(bytes, json_string_value(data), (int)(size / 4 * 3 + 1));
    if (length <= 0 || (unsigned)length > MEDIA_IMAGE_LIMIT) { free(bytes); return false; }
    int fd = media_download_temp(v->download, path);
    FILE *file = fd >= 0 ? fdopen(fd, "wb") : NULL;
    bool ok = false;
    if (file) { ok = fwrite(bytes, 1, (size_t)length, file) == (size_t)length; if (fclose(file)) ok = false; }
    else if (fd >= 0) close(fd);
    free(bytes);
    if (!ok && *path) { unlink(*path); free(*path); *path = NULL; }
    return ok;
}
static void take_download(media_view *v)
{
    int index = v->fetching;
    if (index < 0) return;
    cache_entry *e = &v->cache[index];
    media_state state = media_download_state(v->download);
    if (state == MEDIA_FAILED) { e->error = media_download_error(v->download); e->state = ENTRY_ERROR; v->fetching = -1; return; }
    if (state != MEDIA_READY) return;
    char *path = media_download_take_file(v->download);
    if (e->item.kind != AUTH_MEDIA_EMBED) { e->path = path; e->state = ENTRY_FILE; }
    else {
        json_error_t error;
        json_t *body = json_load_file(path, JSON_REJECT_DUPLICATES, &error);
        const char *id = json_string_value(json_object_get(body, "id"));
        bool valid = id && !strcmp(id, e->item.id);
        for (int i = 0; i < CACHE_COUNT; ++i) {
            cache_entry *other = &v->cache[i];
            if (other->item.kind != AUTH_MEDIA_EMBED || strcmp(e->item.id, other->item.id) || other->path) continue;
            json_t *data = json_object_get(body, !strcmp(other->item.role, "author") ? "author_image_data" : "image_data");
            if (valid && blob_file(v, data, &other->path)) other->state = ENTRY_FILE;
            else { other->state = ENTRY_ERROR; other->error = "Imagem do embed indisponível."; }
        }
        json_decref(body); unlink(path); free(path);
    }
    v->fetching = -1;
}
static bool request_entry(media_view *v, int index)
{
    cache_entry *e = &v->cache[index];
    if (!media_download_request(v->download, e->item.kind, e->item.id, e->item.video)) {
        e->state = ENTRY_ERROR; e->error = "Não foi possível preparar o download."; return false;
    }
    v->fetching = index; e->state = ENTRY_FETCH; return true;
}
static void geometry(int columns, int rows, int *w, int *h)
{
    graphics_terminal t = graphics_terminal_get();
    *w = columns * t.cell_width; *h = rows * t.cell_height;
    if (*w > 640) *w = 640;
    if (*h > 360) *h = 360;

}
static bool modal_layout_update(media_view *v)
{
    media_modal_layout layout;
    if (!media_modal_geometry(COLS, LINES, v->info_open && v->modal != MODAL_LIST,
                              v->modal_image.columns, v->modal_image.rows, &layout)) return false;
    int width, height; geometry(layout.area_width, layout.area_height, &width, &height);
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
            v->modal_token = 0; v->modal_encoding = false;
            animation_free(v->modal_animation); v->modal_animation = NULL;
            graphics_image_free(&v->modal_image);
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
        bool changed = v->visible_count || v->modal != MODAL_NONE || v->fetching >= 0;
        media_view_reset(v); return changed;
    }
    graphics_terminal terminal = graphics_terminal_get();
    bool changed = terminal.protocol != v->terminal.protocol || terminal.cell_width != v->terminal.cell_width ||
                   terminal.cell_height != v->terminal.cell_height || terminal.supported != v->terminal.supported;
    if (changed && v->modal_token) {
        v->modal_token = 0; animation_free(v->modal_animation); v->modal_animation = NULL; v->modal_encoding = false; graphics_image_free(&v->modal_image);
        if (v->modal == MODAL_PLAYING) v->modal = MODAL_LOADING;
    }
    v->terminal = terminal;
    changed |= media_download_poll(v->download);
    for (int i = 0; i < CACHE_COUNT; ++i) if (*v->cache[i].item.id && !live(h, &v->cache[i].item)) {
        if (v->modal_entry == i) close_modal(v);
        if (v->fetching == i) { media_download_cancel(v->download); v->fetching = -1; }
        if (v->cache[i].state == ENTRY_JOB) atomic_store(&v->active, 0);
        delete_entry(&v->cache[i]); changed = true;
    }
    int visible = 0;
    for (int i = 0; i < v->visible_count; ++i)
        if (*v->cache[v->visible[i].entry].item.id) v->visible[visible++] = v->visible[i];
    v->visible_count = visible;
    if (v->modal == MODAL_LIST && v->selected >= visible) v->selected = visible ? visible - 1 : 0;
    take_download(v);
    pthread_mutex_lock(&v->lock);
    uint64_t now = 0; net_now_ms(&now);
    if (v->result_ready) {
        changed = true;
        if (v->modal_token && v->result_token == v->modal_token) {
            v->modal_encoding = false; v->modal_frame = v->modal_requested_frame; v->modal_presented = now;
            if (v->result_animation) { animation_free(v->modal_animation); v->modal_animation = v->result_animation; v->result_animation = NULL; v->modal_epoch = now; }
            if (v->result_error) { v->modal_error = v->result_error; v->modal = MODAL_ERROR; }
            else { graphics_image_free(&v->modal_image); v->modal_image = v->result; memset(&v->result, 0, sizeof(v->result)); v->modal = MODAL_PLAYING; }
        } else for (int i = 0; i < CACHE_COUNT; ++i) if (v->cache[i].token && v->cache[i].token == v->result_token) {
            cache_entry *e = &v->cache[i];
            e->encoding = false; e->frame = e->requested_frame; e->presented = now;
            if (v->result_animation) { animation_free(e->animation); e->animation = v->result_animation; v->result_animation = NULL; e->epoch = now; }
            if (v->result_error) { e->error = v->result_error; e->state = ENTRY_ERROR; }
            else { graphics_image_free(&e->image); e->image = v->result; memset(&v->result, 0, sizeof(v->result)); e->state = ENTRY_READY; }
            break;
        }
        animation_free(v->result_animation); v->result_animation = NULL;
        graphics_image_free(&v->result); v->result_ready = false;
    }
    changed |= v->status_changed; v->status_changed = false;
    bool idle = !v->pending;
    pthread_mutex_unlock(&v->lock);
    size_t bytes = 0;
    for (int i = 0; i < CACHE_COUNT; ++i) if (!v->cache[i].item.video) {
        struct stat st;
        bytes += graphics_image_bytes(&v->cache[i].image) + (v->cache[i].animation ? v->cache[i].animation->bytes : 0);
        if (v->cache[i].path && !stat(v->cache[i].path, &st)) bytes += (size_t)st.st_size;
    }
    while (bytes > CACHE_BYTES) {
        int oldest = -1;
        for (int i = 0; i < CACHE_COUNT; ++i) {
            cache_entry *e = &v->cache[i];
            if (i == v->fetching || i == v->modal_entry || e->item.video || (e->state == ENTRY_JOB || e->encoding) || (!e->path && !e->image.length)) continue;
            if (oldest < 0 || e->used < v->cache[oldest].used) oldest = i;
        }
        if (oldest < 0) break;
        cache_entry *e = &v->cache[oldest]; struct stat st;
        bytes -= graphics_image_bytes(&e->image) + (e->animation ? e->animation->bytes : 0);
        animation_free(e->animation); e->animation = NULL;
        if (e->path && !stat(e->path, &st)) bytes -= (size_t)st.st_size;
        if (e->path) { unlink(e->path); free(e->path); e->path = NULL; }
        graphics_image_free(&e->image); e->state = ENTRY_ERROR; e->error = "Limite do cache atingido. F7: tentar novamente.";
        changed = true;
    }
    if (v->modal != MODAL_NONE && v->modal != MODAL_LIST && v->modal != MODAL_ERROR && v->modal_entry >= 0) {
        if (!modal_layout_update(v)) return changed;
        cache_entry *e = &v->cache[v->modal_entry];
        if (e->state == ENTRY_ERROR) { v->modal_error = e->error; v->modal = MODAL_ERROR; changed = true; }
        else if (!e->path && v->fetching < 0) changed |= request_entry(v, v->modal_entry);
        else if (e->path && (!v->modal_token || (v->modal_animation && !v->modal_encoding && now - v->modal_presented >= 67 && animation_frame_at(v->modal_animation, v->modal_epoch, now) != v->modal_frame))) {
            int w = v->modal_width, height = v->modal_height;
            if (w > 0 && height > 0) {
                v->modal_token = ++v->serial;
                v->modal_requested_frame = v->modal_animation ? animation_frame_at(v->modal_animation, v->modal_epoch, now) : 0;
                job task = {.path = e->path, .token = v->modal_token, .width = w, .height = height,
                    .video = e->item.video, .native = !e->item.video, .animation = v->modal_animation,
                    .frame = v->modal_requested_frame, .image_id = v->modal_image.id};
                v->modal_encoding = true;
                if (!submit_job(v, &task)) { v->modal_error = "Não foi possível preparar o player."; v->modal = MODAL_ERROR; }
                changed = true;
            }
        }
        return changed;
    }
    if (v->modal != MODAL_NONE || !graphics_terminal_get().supported) return changed;
    bool in_worker = !idle;
    for (int i = 0; i < CACHE_COUNT; ++i) in_worker |= v->cache[i].state == ENTRY_JOB || v->cache[i].encoding;
    for (int i = 0; i < v->visible_count; ++i) {
        placement *p = &v->visible[i]; cache_entry *e = &v->cache[p->entry];
        if (!*e->item.id || e->item.video || p->height < 1) continue;
        if (e->state == ENTRY_NEW && v->fetching < 0) { changed |= request_entry(v, p->entry); break; }
        int w, height; geometry(p->width, native_media(&e->item) ? 360 : p->height, &w, &height);
        if (native_media(&e->item)) height = 360;
        graphics_terminal term = graphics_terminal_get();
        int top = p->top * term.cell_height, bottom = p->bottom * term.cell_height;
        if (w < 1 || height < 1 || top + bottom >= height) continue;
        bool geometry_changed = e->width != w || e->height != height;
        if (native_media(&e->item) && e->image.source_width && e->image.source_height) {
            double ratio = fmin(1, fmin((double)w / e->image.source_width, (double)height / e->image.source_height));
            int desired_w = (int)(e->image.source_width * ratio), desired_h = (int)(e->image.source_height * ratio);
            if (desired_w < 1) desired_w = 1;
            if (desired_h < 1) desired_h = 1;
            geometry_changed = e->image.width != desired_w ||
                e->image.height + e->image.crop_top + e->image.crop_bottom != desired_h;
        }
        bool resize = geometry_changed || e->image.protocol != term.protocol ||
                      e->image.cell_width != term.cell_width || e->image.cell_height != term.cell_height;
        size_t frame = e->animation ? animation_frame_at(e->animation, e->epoch, now) : 0;
        bool crop = term.protocol != GRAPHICS_KITTY && e->state == ENTRY_READY && !graphics_image_crop(&e->image, top, bottom);
        bool animate = e->animation && frame != e->frame && now - e->presented >= 67;
        if (!in_worker && e->path && (e->state == ENTRY_FILE || (e->state == ENTRY_READY && (resize || crop || animate)))) {
            e->token = ++v->serial; e->width = w; e->height = height;
            e->requested_frame = resize ? 0 : frame;
            job task = {.path = e->path, .token = e->token, .width = w, .height = height,
                .preview = true, .native = native_media(&e->item), .animation = resize ? NULL : e->animation,
                .frame = frame, .top = e->image.data ? top : 0, .bottom = e->image.data ? bottom : 0,
                .image_id = resize ? 0 : e->image.id};
            if (submit_job(v, &task)) { e->encoding = true; if (!e->image.data) e->state = ENTRY_JOB; }
            else { e->state = ENTRY_ERROR; e->error = "Memória insuficiente para exibir a imagem."; }
            break;
        }
    }
    return changed;
}
static void select_item(media_view *v, int index)
{
    if (index < 0 || index >= v->visible_count) return;
    message_media item = v->cache[v->visible[index].entry].item;
    close_modal(v);
    if (v->fetching >= 0) { v->cache[v->fetching].state = ENTRY_NEW; media_download_cancel(v->download); v->fetching = -1; }
    for (int i = 0; i < CACHE_COUNT; ++i) {
        cache_entry *e = &v->cache[i];
        if (e->encoding || e->state == ENTRY_JOB) { e->encoding = false; e->token = 0; if (e->state == ENTRY_JOB) e->state = ENTRY_FILE; }
    }
    if (item.kind == AUTH_MEDIA_THUMBNAIL) item.kind = AUTH_MEDIA_ATTACHMENT;
    v->selected_item = item;
    v->modal_entry = cache_find(v, &item);
    v->modal = MODAL_LOADING;
    if (v->modal_entry < 0 || !graphics_terminal_get().supported) { v->modal_error = "Este terminal não oferece um protocolo gráfico compatível."; v->modal = MODAL_ERROR; }
    else if (v->cache[v->modal_entry].state == ENTRY_ERROR) { delete_entry(&v->cache[v->modal_entry]); v->cache[v->modal_entry].item = v->selected_item; }
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
            char notice[128]; snprintf(notice, sizeof(notice), "Carregando mídia… %zu KiB", media_download_bytes(v->download) / 1024);
            text_label(w, 3, 3, image_width, notice);
        } else if (v->modal == MODAL_ERROR) {
            text_label(w, 3, 3, image_width, v->modal_error ? v->modal_error : "Mídia indisponível.");
            text_label(w, height - 3, 3, image_width, "Enter: tentar novamente");
        } else {
            char status[160];
            pthread_mutex_lock(&v->lock);
            snprintf(status, sizeof(status), "%s · %.0f / %.0f s · Volume %.0f%%%s", v->paused ? "Pausado" : "Reproduzindo", v->position, v->duration, v->volume, v->audio_missing ? " · Sem dispositivo de áudio" : v->muted ? " · Mudo" : "");
            pthread_mutex_unlock(&v->lock);
            text_label(w, height - 3, 3, image_width, v->selected_item.video ? status : v->modal_animation ? "GIF animado" : "Imagem");
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
        const graphics_image *image = t.protocol == GRAPHICS_KITTY ? &e->image :
            graphics_image_crop(&e->image, p->top * t.cell_height, p->bottom * t.cell_height);
        graphics_image_place(image, p->y, p->x,
            t.protocol == GRAPHICS_KITTY ? p->top * t.cell_height : 0,
            t.protocol == GRAPHICS_KITTY ? p->bottom * t.cell_height : 0);
    }
}
