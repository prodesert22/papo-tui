#define _POSIX_C_SOURCE 200809L
#include "ui/media/emoji_image.h"
#include "ui/media/media_wire.h"
#include "ui/media/resources.h"
#include <chafa.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
static bool io(void *data, size_t length, bool writing)
{
    unsigned char *p = data;
    while (length) {
        size_t n = writing ? fwrite(p, 1, length, stdout) : fread(p, 1, length, stdin);
        if (!n)
            return false;
        p += n;
        length -= n;
    }
    return !writing || fflush(stdout) == 0;
}
typedef struct {
    guint64 id;
    GVariant *payload;
    unsigned operation;
    atomic_bool cancelled;
} worker_job;
static GMutex jobs_lock, output_lock, video_lock;
static GHashTable *jobs;
static atomic_bool stopping;
static _Thread_local worker_job *current_job;
static _Thread_local const char *failure;
static bool cancelled(void *context)
{
    worker_job *job = context;
    return atomic_load(&stopping) || atomic_load(&job->cancelled);
}
static void *player;
static uint64_t player_id;
static const video_player_api *video;
static GVariant *execute(unsigned op, GVariant *payload)
{
    graphics_image image = {0};
    animation *frames = NULL;
    image_decode_request request = {0};
    char *path = NULL;
    GVariant *result = NULL, *a = NULL, *b = NULL;
    failure = NULL;
    if (op == MEDIA_RPC_DECODE) {
        if (!media_image_backend()) {
            failure = "image-module";
            goto done;
        }
        if (!media_wire_read_request(payload, &request, &path))
            goto done;
        if (image_decode_file(&request, &image, &frames, cancelled, current_job))
            result = g_variant_new("(vv)", media_wire_image(&image), media_wire_animation(frames));
    } else if (op == MEDIA_RPC_FRAME) {
        if (!g_variant_is_of_type(payload, G_VARIANT_TYPE("(vv)")))
            goto done;
        g_variant_get(payload, "(vv)", &a, &b);
        if (!media_wire_read_request(a, &request, &path) || !media_wire_read_animation(b, &frames))
            goto done;
        request.animation = frames;
        if (image_encode_frame(&request, &image))
            result = media_wire_image(&image);
    } else if (op == MEDIA_RPC_ENCODE) {
        if (!g_variant_is_of_type(payload, G_VARIANT_TYPE("(vay)")))
            goto done;
        g_variant_get(payload, "(v@ay)", &a, &b);
        if (!media_wire_read_request(a, &request, &path) ||
            !graphics_image_capacity(request.width, request.height))
            goto done;
        gsize length;
        const unsigned char *pixels = g_variant_get_fixed_array(b, &length, 1);
        if (length != (size_t)request.width * (size_t)request.height * 3)
            goto done;
        bool ok = request.region
                      ? graphics_image_encode_region_id(
                            pixels, request.width, request.height, request.top, request.bottom,
                            request.left, request.right, request.terminal, request.image_id, &image)
                  : request.preview
                      ? graphics_image_encode_preview(pixels, request.width, request.height,
                                                      request.terminal, request.image_id, &image)
                      : graphics_image_encode_id(pixels, request.width, request.height,
                                                 request.terminal, request.image_id, &image);
        if (ok)
            result = media_wire_image(&image);
    } else if (op == MEDIA_RPC_GLYPH) {
        if (!g_variant_is_of_type(payload, G_VARIANT_TYPE("(vs)")))
            goto done;
        const char *text;
        g_variant_get(payload, "(v&s)", &a, &text);
        if (media_wire_read_request(a, &request, &path) && strlen(text) <= 8192 &&
            emoji_image_encode_region(
                text, request.width, request.height, request.background, request.terminal,
                request.image_id, &image, request.region ? request.top : 0,
                request.region ? request.bottom : 0, request.region ? request.left : 0,
                request.region ? request.right : 0))
            result = media_wire_image(&image);
    } else if (op == MEDIA_RPC_VIDEO_OPEN) {
        if (!g_variant_is_of_type(payload, G_VARIANT_TYPE_STRING))
            goto done;
        video = media_video_backend();
        if (!video)
            failure = "video-module";
        if (player && video)
            video->close(player);
        player = NULL;
        if (video)
            player = video->open(g_variant_get_string(payload, NULL));
        if (player)
            result = g_variant_new_uint64(player_id = current_job->id);
    } else if (op >= MEDIA_RPC_VIDEO_CLOSE && op <= MEDIA_RPC_VIDEO_RENDER) {
        if (!g_variant_is_of_type(payload, G_VARIANT_TYPE("(tv)")))
            goto done;
        guint64 id;
        g_variant_get(payload, "(tv)", &id, &a);
        if (!player || !video || id != player_id)
            goto done;
        if (op == MEDIA_RPC_VIDEO_CLOSE) {
            video->close(player);
            player = NULL;
            result = g_variant_new_boolean(true);
        } else if (op == MEDIA_RPC_VIDEO_UPDATE) {
            video_controls controls = {0};
            video_info info = {0};
            if (media_wire_copy(a, &controls, sizeof(controls))) {
                video->update(player, &controls, &info);
                result = media_wire_bytes(&info, sizeof(info));
            }
        } else if (op == MEDIA_RPC_VIDEO_CHANGED)
            result = g_variant_new_boolean(video->changed(player));
        else if (g_variant_is_of_type(a, G_VARIANT_TYPE("(ii)"))) {
            int w, h;
            g_variant_get(a, "(ii)", &w, &h);
            if (w > 0 && h > 0 && (size_t)w * (size_t)h <= VIDEO_FRAME_PIXELS) {
                size_t size = (size_t)w * (size_t)h * 4;
                unsigned char *rgba = malloc(size);
                if (rgba && video->render(player, rgba, w, h))
                    result = media_wire_bytes(rgba, size);
                free(rgba);
            }
        }
    } else if (op == MEDIA_RPC_DETECT) {
        gchar **environment = g_get_environ();
        ChafaTermInfo *info = chafa_term_db_detect(chafa_term_db_get_default(), environment);
        const char *tmux = getenv("TMUX"), *screen = getenv("STY");
        bool iterm = info && chafa_term_info_get_best_pixel_mode(info) == CHAFA_PIXEL_MODE_ITERM2 &&
                     chafa_term_info_get_passthrough_type(info) == CHAFA_PASSTHROUGH_NONE &&
                     (!tmux || !*tmux) && (!screen || !*screen);
        if (info)
            chafa_term_info_unref(info);
        g_strfreev(environment);
        result = g_variant_new_boolean(iterm);
    }
done:
    if (a)
        g_variant_unref(a);
    if (b)
        g_variant_unref(b);
    g_free(path);
    graphics_image_free(&image);
    animation_free(frames);
    return result;
}
static void run_job(gpointer data, gpointer unused)
{
    (void)unused;
    worker_job *job = data;
    current_job = job;
    bool is_video =
        job->operation >= MEDIA_RPC_VIDEO_OPEN && job->operation <= MEDIA_RPC_VIDEO_RENDER;
    if (is_video)
        g_mutex_lock(&video_lock);
    failure = NULL;
    GVariant *answer = cancelled(job) ? NULL : execute(job->operation, job->payload);
    if (is_video)
        g_mutex_unlock(&video_lock);
    GVariant *response =
        g_variant_ref_sink(g_variant_new("(tbv)", job->id, answer != NULL,
                                         answer    ? answer
                                         : failure ? g_variant_new_string(failure)
                                                   : g_variant_new_boolean(false)));
    gsize size = g_variant_get_size(response);
    guint32 header = GUINT32_TO_BE((guint32)size);
    g_mutex_lock(&output_lock);
    bool ok = size <= MEDIA_WIRE_LIMIT && io(&header, sizeof(header), true) &&
              io((void *)g_variant_get_data(response), size, true);
    g_mutex_unlock(&output_lock);
    if (!ok)
        atomic_store(&stopping, true);
    g_variant_unref(response);
    g_variant_unref(job->payload);
    g_mutex_lock(&jobs_lock);
    g_hash_table_remove(jobs, &job->id);
    g_mutex_unlock(&jobs_lock);
    free(job);
    current_job = NULL;
}
int main(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#else
    signal(SIGPIPE, SIG_DFL);
#endif
    media_resources_init();
    chafa_set_n_threads(1);
    jobs = g_hash_table_new(g_int64_hash, g_int64_equal);
    GError *error = NULL;
    GThreadPool *pool = g_thread_pool_new(run_job, NULL, 4, true, &error);
    if (!pool) {
        g_clear_error(&error);
        g_hash_table_destroy(jobs);
        return 2;
    }
    while (!atomic_load(&stopping)) {
        guint32 length;
        if (!io(&length, sizeof(length), false))
            break;
        length = GUINT32_FROM_BE(length);
        if (!length || length > MEDIA_WIRE_LIMIT)
            break;
        void *buffer = malloc(length);
        if (!buffer)
            break;
        if (!io(buffer, length, false)) {
            free(buffer);
            break;
        }
        GVariant *request = g_variant_ref_sink(g_variant_new_from_data(
            G_VARIANT_TYPE("(uuttv)"), buffer, length, false, free, buffer));
        if (!g_variant_is_normal_form(request)) {
            g_variant_unref(request);
            break;
        }
        guint32 version, op;
        guint64 generation, id;
        GVariant *payload;
        g_variant_get(request, "(uuttv)", &version, &op, &generation, &id, &payload);
        (void)generation;
        g_variant_unref(request);
        if (version != MEDIA_WIRE_VERSION) {
            g_variant_unref(payload);
            break;
        }
        if (op == MEDIA_RPC_CANCEL) {
            g_mutex_lock(&jobs_lock);
            worker_job *job = g_hash_table_lookup(jobs, &id);
            if (job)
                atomic_store(&job->cancelled, true);
            g_mutex_unlock(&jobs_lock);
            g_variant_unref(payload);
            continue;
        }
        worker_job *job = calloc(1, sizeof(*job));
        if (!job) {
            g_variant_unref(payload);
            break;
        }
        job->id = id;
        job->payload = payload;
        job->operation = op;
        atomic_init(&job->cancelled, false);
        g_mutex_lock(&jobs_lock);
        bool valid = !g_hash_table_contains(jobs, &id) && g_hash_table_size(jobs) < 40;
        if (valid)
            g_hash_table_insert(jobs, &job->id, job);
        g_mutex_unlock(&jobs_lock);
        if (!valid) {
            g_variant_unref(payload);
            free(job);
            break;
        }
        if (!g_thread_pool_push(pool, job, &error)) {
            g_clear_error(&error);
            run_job(job, NULL);
        }
    }
    atomic_store(&stopping, true);
    g_thread_pool_free(pool, false, true);
    if (player && video)
        video->close(player);
    g_hash_table_destroy(jobs);
    return 0;
}
