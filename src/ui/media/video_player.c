#include "ui/media/media_backend.h"
#include <gmodule.h>
#include <limits.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    mpv_handle *handle;
    mpv_render_context *render;
    video_info info;
    double volume_target;
} video_player;
enum {
    PROP_PAUSE = 1,
    PROP_MUTE,
    PROP_POSITION,
    PROP_DURATION,
    PROP_VOLUME,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_AUDIO
};
static void close_player(void *context)
{
    video_player *p = context;
    if (!p)
        return;
    if (p->render)
        mpv_render_context_free(p->render);
    if (p->handle)
        mpv_terminate_destroy(p->handle);
    free(p);
}
static void *open_player(const char *path)
{
    video_player *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->handle = mpv_create();
    if (!p->handle)
        goto error;
    const char *options[][2] = {{"config", "no"},
                                {"terminal", "no"},
                                {"load-scripts", "no"},
                                {"ytdl", "no"},
                                {"input-terminal", "no"},
                                {"input-default-bindings", "no"},
                                {"vo", "libmpv"},
                                {"hwdec", "auto-copy"},
                                {"keep-open", "yes"},
                                {"access-references", "no"},
                                {"audio-fallback-to-null", "yes"},
                                {"vd-lavc-o", "max_pixels=16777216"},
                                {"demuxer", "lavf"},
                                {"demuxer-lavf-o", "protocol_whitelist=file"},
                                {"volume", "50"}};
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        int result = mpv_set_option_string(p->handle, options[i][0], options[i][1]);
        /* Minimal libmpv builds omit scripting entirely. A missing switch that
         * disables that absent feature is already satisfied. Other failures
         * still reject initialization, including the file-only media policy. */
        bool absent_script_option =
            result == MPV_ERROR_OPTION_NOT_FOUND &&
            (!strcmp(options[i][0], "load-scripts") || !strcmp(options[i][0], "ytdl"));
        if (result < 0 && !absent_script_option)
            goto error;
    }
    if (mpv_initialize(p->handle) < 0)
        goto error;
    p->info.volume = p->volume_target = 50;
    const struct {
        const char *name;
        mpv_format format;
    } properties[] = {{"pause", MPV_FORMAT_FLAG},      {"mute", MPV_FORMAT_FLAG},
                      {"time-pos", MPV_FORMAT_DOUBLE}, {"duration", MPV_FORMAT_DOUBLE},
                      {"volume", MPV_FORMAT_DOUBLE},   {"dwidth", MPV_FORMAT_INT64},
                      {"dheight", MPV_FORMAT_INT64},   {"current-ao", MPV_FORMAT_STRING}};
    for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); ++i)
        if (mpv_observe_property(p->handle, i + 1, properties[i].name, properties[i].format) < 0)
            goto error;
    mpv_render_param parameters[] = {{MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_SW},
                                     {MPV_RENDER_PARAM_INVALID, NULL}};
    if (mpv_render_context_create(&p->render, p->handle, parameters) < 0)
        goto error;
    const char *args[] = {"loadfile", path, "replace", NULL};
    if (mpv_command_async(p->handle, 0, args) < 0)
        goto error;
    return p;
error:
    close_player(p);
    return NULL;
}
static void read_events(video_player *p)
{
    mpv_event *event;
    while ((event = mpv_wait_event(p->handle, 0))->event_id != MPV_EVENT_NONE) {
        if (event->event_id == MPV_EVENT_END_FILE) {
            const mpv_event_end_file *end = event->data;
            p->info.invalid |= end && end->error < 0;
        } else if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            const mpv_event_property *property = event->data;
            if (!property || !property->data)
                continue;
            if (property->format == MPV_FORMAT_FLAG) {
                bool enabled = *(int *)property->data != 0;
                if (event->reply_userdata == PROP_PAUSE)
                    p->info.paused = enabled;
                else if (event->reply_userdata == PROP_MUTE)
                    p->info.muted = enabled;
            } else if (property->format == MPV_FORMAT_DOUBLE) {
                double value = *(double *)property->data;
                switch (event->reply_userdata) {
                case PROP_POSITION:
                    p->info.position = value;
                    break;
                case PROP_DURATION:
                    p->info.duration = value;
                    break;
                case PROP_VOLUME:
                    p->info.volume = value;
                    break;
                }
            } else if (property->format == MPV_FORMAT_INT64) {
                int64_t value = *(int64_t *)property->data;
                int dimension = value > 0 && value <= INT_MAX ? (int)value : 0;
                if (event->reply_userdata == PROP_WIDTH)
                    p->info.width = dimension;
                else if (event->reply_userdata == PROP_HEIGHT)
                    p->info.height = dimension;
            } else if (property->format == MPV_FORMAT_STRING &&
                       event->reply_userdata == PROP_AUDIO) {
                const char *ao = *(char **)property->data;
                p->info.audio_missing = ao && !strcmp(ao, "null");
            }
        }
    }
}
static void update_player(void *context, const video_controls *controls, video_info *info)
{
    video_player *p = context;
    /* Synchronous client calls on the render thread can wait for that very
     * thread to render. Only consume events and enqueue asynchronous calls. */
    read_events(p);
    if (controls->pause % 2) {
        const char *args[] = {"cycle", "pause", NULL};
        mpv_command_async(p->handle, 0, args);
    }
    if (controls->mute % 2) {
        const char *args[] = {"cycle", "mute", NULL};
        mpv_command_async(p->handle, 0, args);
    }
    if (controls->seek_absolute) {
        char position[64];
        snprintf(position, sizeof(position), "%.17g", controls->position);
        const char *args[] = {"seek", position, "absolute+exact", NULL};
        mpv_command_async(p->handle, 0, args);
    }
    if (controls->seek) {
        char amount[32];
        snprintf(amount, sizeof(amount), "%d", controls->seek);
        const char *args[] = {"seek", amount, "relative+exact", NULL};
        mpv_command_async(p->handle, 0, args);
    }
    if (controls->volume_absolute || controls->volume) {
        double level = controls->volume_absolute ? controls->volume_level : p->volume_target;
        level += controls->volume;
        if (!(level >= 0))
            level = 0;
        if (level > 100)
            level = 100;
        /* Retain the requested value across quick key repeats until the
         * corresponding property notification arrives. */
        if (mpv_set_property_async(p->handle, 0, "volume", MPV_FORMAT_DOUBLE, &level) >= 0) {
            p->volume_target = level;
            /* Report the dispatched level right away: the property
             * notification follows one event later. */
            p->info.volume = level;
        }
    }
    *info = p->info;
}
static bool changed_player(void *context)
{
    return (mpv_render_context_update(((video_player *)context)->render) &
            MPV_RENDER_UPDATE_FRAME) != 0;
}
static bool render_player(void *context, unsigned char *rgba, int width, int height)
{
    if (!rgba || width < 1 || height < 1 || (size_t)width > VIDEO_FRAME_PIXELS / (size_t)height)
        return false;
    int dimensions[] = {width, height};
    size_t stride = (size_t)width * 4;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_SW_SIZE, dimensions},
                                 {MPV_RENDER_PARAM_SW_FORMAT, "rgb0"},
                                 {MPV_RENDER_PARAM_SW_STRIDE, &stride},
                                 {MPV_RENDER_PARAM_SW_POINTER, rgba},
                                 {MPV_RENDER_PARAM_INVALID, NULL}};
    return mpv_render_context_render(((video_player *)context)->render, params) >= 0;
}
static void set_wakeup(void *context, void (*callback)(void *), void *data)
{
    video_player *p = context;
    mpv_set_wakeup_callback(p->handle, callback, data);
    mpv_render_context_set_update_callback(p->render, callback, data);
}
G_MODULE_EXPORT const video_player_api *papo_video_backend(unsigned abi, size_t size)
{
    if (abi != PAPO_MEDIA_BACKEND_ABI || size != sizeof(video_player_api))
        return NULL;
    static const video_player_api api = {PAPO_MEDIA_BACKEND_ABI,
                                         sizeof(video_player_api),
                                         sizeof(video_controls),
                                         sizeof(video_info),
                                         open_player,
                                         close_player,
                                         update_player,
                                         changed_player,
                                         render_player,
                                         set_wakeup};
    return &api;
}
