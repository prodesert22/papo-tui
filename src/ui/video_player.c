#include "ui/media_backend.h"
#include <gmodule.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { mpv_handle *handle; mpv_render_context *render; } video_player;
static void close_player(void *context)
{
    video_player *p = context;
    if (!p) return;
    if (p->render) mpv_render_context_free(p->render);
    if (p->handle) mpv_terminate_destroy(p->handle);
    free(p);
}
static void *open_player(const char *path)
{
    video_player *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->handle = mpv_create();
    if (!p->handle) goto error;
    const char *options[][2] = {{"config", "no"}, {"terminal", "no"}, {"load-scripts", "no"},
        {"ytdl", "no"}, {"input-terminal", "no"}, {"input-default-bindings", "no"},
        {"vo", "libmpv"}, {"hwdec", "no"}, {"keep-open", "yes"}, {"access-references", "no"},
        {"audio-fallback-to-null", "yes"}, {"vd-lavc-o", "max_pixels=16777216"},
        {"demuxer", "lavf"}, {"demuxer-lavf-o", "protocol_whitelist=file"}, {"volume", "50"}};
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i)
        if (mpv_set_option_string(p->handle, options[i][0], options[i][1]) < 0) goto error;
    if (mpv_initialize(p->handle) < 0) goto error;
    mpv_render_param parameters[] = {{MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_SW}, {MPV_RENDER_PARAM_INVALID, NULL}};
    if (mpv_render_context_create(&p->render, p->handle, parameters) < 0) goto error;
    const char *args[] = {"loadfile", path, "replace", NULL};
    if (mpv_command(p->handle, args) < 0) goto error;
    return p;
 error:
    close_player(p); return NULL;
}
static void update_player(void *context, const video_controls *controls, video_info *info)
{
    video_player *p = context;
    if (controls->pause % 2) { const char *args[] = {"cycle", "pause", NULL}; mpv_command(p->handle, args); }
    if (controls->mute % 2) { const char *args[] = {"cycle", "mute", NULL}; mpv_command(p->handle, args); }
    if (controls->seek) {
        char amount[32]; snprintf(amount, sizeof(amount), "%d", controls->seek);
        const char *args[] = {"seek", amount, "relative", NULL}; mpv_command(p->handle, args);
    }
    if (controls->volume) {
        char amount[32]; snprintf(amount, sizeof(amount), "%d", controls->volume);
        const char *args[] = {"add", "volume", amount, NULL}; mpv_command(p->handle, args);
    }
    memset(info, 0, sizeof(*info)); info->volume = 50;
    mpv_event *event;
    while ((event = mpv_wait_event(p->handle, 0))->event_id != MPV_EVENT_NONE) {
        if (event->event_id == MPV_EVENT_END_FILE) {
            const mpv_event_end_file *end = event->data;
            info->invalid |= end && end->error < 0;
        }
    }
    int paused = 0, muted = 0;
    mpv_get_property(p->handle, "pause", MPV_FORMAT_FLAG, &paused);
    mpv_get_property(p->handle, "mute", MPV_FORMAT_FLAG, &muted);
    mpv_get_property(p->handle, "time-pos", MPV_FORMAT_DOUBLE, &info->position);
    mpv_get_property(p->handle, "duration", MPV_FORMAT_DOUBLE, &info->duration);
    mpv_get_property(p->handle, "volume", MPV_FORMAT_DOUBLE, &info->volume);
    info->paused = paused != 0; info->muted = muted != 0;
    char *ao = mpv_get_property_string(p->handle, "current-ao");
    info->audio_missing = ao && !strcmp(ao, "null"); mpv_free(ao);
}
static bool changed_player(void *context)
{
    return (mpv_render_context_update(((video_player *)context)->render) & MPV_RENDER_UPDATE_FRAME) != 0;
}
static bool render_player(void *context, unsigned char *rgba, int width, int height)
{
    if (!rgba || width < 1 || height < 1 || width > 640 || height > 360) return false;
    int dimensions[] = {width, height}; size_t stride = (size_t)width * 4;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_SW_SIZE, dimensions}, {MPV_RENDER_PARAM_SW_FORMAT, "rgb0"},
        {MPV_RENDER_PARAM_SW_STRIDE, &stride}, {MPV_RENDER_PARAM_SW_POINTER, rgba}, {MPV_RENDER_PARAM_INVALID, NULL}};
    return mpv_render_context_render(((video_player *)context)->render, params) >= 0;
}
G_MODULE_EXPORT const video_player_api *papo_video_backend(unsigned abi, size_t size)
{
    if (abi != PAPO_MEDIA_BACKEND_ABI || size != sizeof(video_player_api)) return NULL;
    static const video_player_api api = {PAPO_MEDIA_BACKEND_ABI, sizeof(video_player_api), sizeof(video_controls), sizeof(video_info),
        open_player, close_player, update_player, changed_player, render_player};
    return &api;
}
