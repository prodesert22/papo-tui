#include "ui/media_backend.h"
#include <stdlib.h>
#include <string.h>

void animation_ref(animation *a) { if (a) atomic_fetch_add(&a->references, 1); }
void animation_free(animation *a)
{
    if (!a || atomic_fetch_sub(&a->references, 1) != 1) return;
    for (size_t i = 0; i < a->count; ++i) free(a->pixels[i]);
    free(a);
}
size_t animation_frame_at(const animation *a, uint64_t epoch, uint64_t now)
{
    if (!a || !a->duration) return 0;
    uint64_t at = now >= epoch ? (now - epoch) % a->duration : 0;
    for (size_t i = 0; i < a->count; ++i) {
        if (at < a->delay[i]) return i;
        at -= a->delay[i];
    }
    return 0;
}
bool image_decode_file(const image_decode_request *request, graphics_image *out, animation **frames,
                       bool (*cancelled)(void *), void *context)
{
    memset(out, 0, sizeof(*out)); *frames = NULL;
    if (cancelled && cancelled(context)) return false;
    const image_codec_api *api = media_image_backend();
    return api && api->decode(request, out, frames, cancelled, context);
}
bool image_encode_frame(const image_decode_request *request, graphics_image *out)
{
    memset(out, 0, sizeof(*out));
    const image_codec_api *api = media_image_backend();
    return api && api->encode_frame(request, out);
}
