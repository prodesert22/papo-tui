#include "ui/media/media_service.h"
#include "config/app_mode.h"
#include "ui/media/media_process.h"
#include "ui/media/media_wire.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
static atomic_int detected_iterm;
static atomic_bool detecting;
static _Thread_local const char *last_error;
const char *media_service_error(void)
{
    return last_error;
}
void media_service_stop(void)
{
    media_process_stop();
    atomic_store(&detected_iterm, 0);
    atomic_store(&detecting, false);
}
static GVariant *rpc(unsigned op, GVariant *payload, bool (*cancel)(void *), void *context)
{
    last_error = NULL;
    GVariant *v = media_process_call(op, payload, cancel, context);
    if (!v)
        last_error = "Auxiliar de mídia ausente ou indisponível. Reinstale o aplicativo.";
    return v;
}
static GVariant *result(GVariant *response)
{
    if (!response)
        return NULL;
    gboolean success;
    guint64 id;
    GVariant *value;
    g_variant_get(response, "(tbv)", &id, &success, &value);
    (void)id;
    g_variant_unref(response);
    if (!success) {
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
            const char *message = g_variant_get_string(value, NULL);
            if (!strcmp(message, "image-module"))
                last_error = "Módulo de imagens ausente ou incompatível. Reinstale o aplicativo.";
            if (!strcmp(message, "video-module"))
                last_error = "Módulo de vídeo ausente ou incompatível. Reinstale o aplicativo.";
        }
        g_variant_unref(value);
        return NULL;
    }
    return value;
}
static bool decode_proxy(const image_decode_request *r, graphics_image *out, animation **frames,
                         bool (*cancel)(void *), void *context)
{
    memset(out, 0, sizeof(*out));
    *frames = NULL;
    GVariant *v = result(rpc(MEDIA_RPC_DECODE, media_wire_request(r), cancel, context));
    if (!v)
        return false;
    if (!g_variant_is_of_type(v, G_VARIANT_TYPE("(vv)"))) {
        g_variant_unref(v);
        return false;
    }
    GVariant *image, *animated;
    g_variant_get(v, "(vv)", &image, &animated);
    bool ok = media_wire_read_image(image, out) && media_wire_read_animation(animated, frames);
    g_variant_unref(image);
    g_variant_unref(animated);
    g_variant_unref(v);
    if (!ok) {
        graphics_image_free(out);
        animation_free(*frames);
        *frames = NULL;
    }
    return ok;
}
static bool frame_proxy(const image_decode_request *r, graphics_image *out)
{
    memset(out, 0, sizeof(*out));
    if (!r->animation || r->frame >= r->animation->count)
        return false;
    /* Encode only this immutable frame, rather than copying the complete GIF
     * across IPC on every tick. Canonical animation ownership stays in cache. */
    animation single = {0};
    const animation *source = r->animation;
    single.width = source->width;
    single.height = source->height;
    single.source_width = source->source_width;
    single.source_height = source->source_height;
    single.count = 1;
    single.duration = source->delay[r->frame];
    single.delay[0] = source->delay[r->frame];
    single.rgba[0] = source->rgba[r->frame];
    single.pixels[0] = source->pixels[r->frame];
    single.bytes = (size_t)single.width * (size_t)single.height * (single.rgba[0] ? 4U : 3U) +
                   IMAGE_PIXEL_PADDING;
    image_decode_request request = *r;
    request.frame = 0;
    GVariant *v = result(
        rpc(MEDIA_RPC_FRAME,
            g_variant_new("(vv)", media_wire_request(&request), media_wire_animation(&single)),
            NULL, NULL));
    if (!v)
        return false;
    bool ok = media_wire_read_image(v, out);
    g_variant_unref(v);
    return ok;
}
const image_codec_api *media_image_backend(void)
{
    static const image_codec_api api = {PAPO_MEDIA_BACKEND_ABI, sizeof(api), decode_proxy,
                                        frame_proxy};
    return app_mode_get() == APP_MODE_ULTRALIGHT ? NULL : &api;
}
bool graphics_image_encode_id(const unsigned char *pixels, int width, int height,
                              graphics_terminal t, uint32_t id, graphics_image *out)
{
    memset(out, 0, sizeof(*out));
    if (!pixels || !graphics_image_capacity(width, height))
        return false;
    image_decode_request r = {.width = width, .height = height, .terminal = t, .image_id = id};
    GVariant *v =
        result(rpc(MEDIA_RPC_ENCODE,
                   g_variant_new("(v@ay)", media_wire_request(&r),
                                 media_wire_bytes(pixels, (size_t)width * (size_t)height * 3)),
                   NULL, NULL));
    if (!v)
        return false;
    bool ok = media_wire_read_image(v, out);
    g_variant_unref(v);
    return ok;
}
bool graphics_image_encode(const unsigned char *p, int w, int h, graphics_terminal t,
                           graphics_image *out)
{
    return graphics_image_encode_id(p, w, h, t, graphics_image_next_id(), out);
}
bool graphics_image_encode_preview(const unsigned char *p, int w, int h, graphics_terminal t,
                                   uint32_t id, graphics_image *out)
{
    image_decode_request r = {
        .width = w, .height = h, .terminal = t, .image_id = id, .preview = true};
    memset(out, 0, sizeof(*out));
    if (!p || !graphics_image_capacity(w, h))
        return false;
    GVariant *v = result(rpc(MEDIA_RPC_ENCODE,
                             g_variant_new("(v@ay)", media_wire_request(&r),
                                           media_wire_bytes(p, (size_t)w * (size_t)h * 3)),
                             NULL, NULL));
    if (!v)
        return false;
    bool ok = media_wire_read_image(v, out);
    g_variant_unref(v);
    return ok;
}
bool media_service_glyph(const char *text, int w, int h, uint32_t bg, graphics_terminal t,
                         uint32_t id, graphics_image *out)
{
    image_decode_request r = {
        .width = w, .height = h, .terminal = t, .image_id = id, .background = bg};
    GVariant *v = result(
        rpc(MEDIA_RPC_GLYPH, g_variant_new("(vs)", media_wire_request(&r), text), NULL, NULL));
    memset(out, 0, sizeof(*out));
    if (!v)
        return false;
    bool ok = media_wire_read_image(v, out);
    g_variant_unref(v);
    return ok;
}
typedef struct {
    uint64_t generation, id;
    bool broken;
} remote_player;
static GVariant *player_call(remote_player *p, unsigned op, GVariant *payload)
{
    if (!p || p->broken || p->generation != media_process_generation() ||
        app_mode_get() != APP_MODE_NORMAL) {
        g_variant_ref_sink(payload);
        g_variant_unref(payload);
        return NULL;
    }
    GVariant *v = result(rpc(op, g_variant_new("(tv)", (guint64)p->id, payload), NULL, NULL));
    if (!v)
        p->broken = true;
    return v;
}
static void *player_open(const char *path)
{
    if (app_mode_get() != APP_MODE_NORMAL)
        return NULL;
    uint64_t generation = media_process_generation();
    GVariant *v = result(rpc(MEDIA_RPC_VIDEO_OPEN, g_variant_new_string(path), NULL, NULL));
    if (!v)
        return NULL;
    remote_player *p = calloc(1, sizeof(*p));
    if (p) {
        p->generation = generation;
        p->id = g_variant_get_uint64(v);
    }
    g_variant_unref(v);
    return p;
}
static void player_close(void *handle)
{
    remote_player *p = handle;
    GVariant *v = player_call(p, MEDIA_RPC_VIDEO_CLOSE, g_variant_new_boolean(true));
    if (v)
        g_variant_unref(v);
    free(p);
}
static void player_update(void *handle, const video_controls *controls, video_info *info)
{
    memset(info, 0, sizeof(*info));
    GVariant *v =
        player_call(handle, MEDIA_RPC_VIDEO_UPDATE, media_wire_bytes(controls, sizeof(*controls)));
    if (!v || !media_wire_copy(v, info, sizeof(*info)))
        info->invalid = true;
    if (v)
        g_variant_unref(v);
}
static bool player_changed(void *handle)
{
    GVariant *v = player_call(handle, MEDIA_RPC_VIDEO_CHANGED, g_variant_new_boolean(true));
    bool changed = v && g_variant_get_boolean(v);
    if (v)
        g_variant_unref(v);
    return changed;
}
static bool player_render(void *handle, unsigned char *rgba, int w, int h)
{
    if (w < 1 || h < 1 || (size_t)w * (size_t)h > VIDEO_FRAME_PIXELS)
        return false;
    GVariant *v = player_call(handle, MEDIA_RPC_VIDEO_RENDER, g_variant_new("(ii)", w, h));
    bool ok = v && media_wire_copy(v, rgba, (size_t)w * (size_t)h * 4);
    if (v)
        g_variant_unref(v);
    return ok;
}
static void player_wakeup(void *handle, void (*wake)(void *), void *context)
{
    (void)handle;
    if (wake)
        wake(context);
}
const video_player_api *media_video_backend(void)
{
    static const video_player_api api = {
        PAPO_MEDIA_BACKEND_ABI, sizeof(api),    sizeof(video_controls),
        sizeof(video_info),     player_open,    player_close,
        player_update,          player_changed, player_render,
        player_wakeup};
    return app_mode_get() == APP_MODE_NORMAL ? &api : NULL;
}
static gpointer detect(gpointer unused)
{
    (void)unused;
    uint64_t generation = media_process_generation();
    GVariant *v = result(rpc(MEDIA_RPC_DETECT, g_variant_new_boolean(true), NULL, NULL));
    if (v) {
        if (generation == media_process_generation())
            atomic_store(&detected_iterm, g_variant_get_boolean(v) ? 1 : -1);
        g_variant_unref(v);
    }
    return NULL;
}
bool media_service_iterm2(void)
{
    if (app_mode_get() != APP_MODE_NORMAL)
        return false;
    bool expected = false;
    if (atomic_compare_exchange_strong(&detecting, &expected, true)) {
        GThread *thread = g_thread_new("media-detect", detect, NULL);
        g_thread_unref(thread);
    }
    return atomic_load(&detected_iterm) == 1;
}
