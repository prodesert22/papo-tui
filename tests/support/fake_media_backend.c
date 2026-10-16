#include "ui/media/media_backend.h"
#include <gmodule.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
static bool crash_decode(const image_decode_request *request, graphics_image *out,
                         animation **frames, bool (*cancelled)(void *), void *context)
{
    (void)request;
    (void)out;
    (void)frames;
    const char *gate = getenv("PAPO_TEST_BACKEND_CRASH_GATE");
    for (int i = 0; i < 2000; ++i) {
        if (cancelled && cancelled(context))
            return false;
        if (gate && access(gate, F_OK) == 0) {
            raise(SIGABRT);
            return false;
        }
        g_usleep(5000);
    }
    return false;
}
static bool unused_encode(const image_decode_request *request, graphics_image *out)
{
    (void)request;
    (void)out;
    return false;
}
G_MODULE_EXPORT const image_codec_api *papo_image_backend(const image_codec_host *host)
{
    if (host && getenv("PAPO_TEST_BACKEND_CRASH_GATE")) {
        static const image_codec_api crash = {PAPO_MEDIA_BACKEND_ABI, sizeof(image_codec_api),
                                              crash_decode, unused_encode};
        return &crash;
    }
    static const image_codec_api api = {.abi = PAPO_MEDIA_BACKEND_ABI + 1,
                                        .size = sizeof(image_codec_api)};
    return &api;
}
G_MODULE_EXPORT const video_player_api *papo_video_backend(unsigned abi, size_t size)
{
    (void)abi;
    (void)size;
    static const video_player_api api = {.abi = PAPO_MEDIA_BACKEND_ABI + 1,
                                         .size = sizeof(video_player_api)};
    return &api;
}
