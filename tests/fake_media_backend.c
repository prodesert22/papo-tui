#include "ui/media_backend.h"
#include <gmodule.h>
G_MODULE_EXPORT const image_codec_api *papo_image_backend(const image_codec_host *host)
{
    (void)host;
    static const image_codec_api api = {.abi = PAPO_MEDIA_BACKEND_ABI + 1, .size = sizeof(image_codec_api)};
    return &api;
}
G_MODULE_EXPORT const video_player_api *papo_video_backend(unsigned abi, size_t size)
{
    (void)abi; (void)size;
    static const video_player_api api = {.abi = PAPO_MEDIA_BACKEND_ABI + 1, .size = sizeof(video_player_api)};
    return &api;
}
