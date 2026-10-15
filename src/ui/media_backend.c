#include "ui/media_backend.h"
#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <string.h>

static GModule *open_backend(const char *name)
{
    if (!g_module_supported()) return NULL;
    /* Never search the working directory, PATH or a remotely supplied path. */
    char *executable = g_file_read_link("/proc/self/exe", NULL);
    char *directory = executable ? g_path_get_dirname(executable) : NULL;
    char *local = directory ? g_build_filename(directory, "papo-backends", name, NULL) : NULL;
    GModule *module = local ? g_module_open(local, G_MODULE_BIND_LOCAL) : NULL;
    if (!local) {
        char *installed = g_build_filename(PAPO_BACKEND_INSTALL_DIR, name, NULL);
        module = g_module_open(installed, G_MODULE_BIND_LOCAL);
        g_free(installed);
    }
    g_free(local); g_free(directory); g_free(executable);
    return module;
}
static gpointer load_image(gpointer unused)
{
    (void)unused;
    GModule *module = open_backend(PAPO_IMAGE_MODULE);
    gpointer symbol = NULL;
    image_codec_entry entry = NULL;
    G_STATIC_ASSERT(sizeof(entry) == sizeof(symbol));
    if (!module || !g_module_symbol(module, "papo_image_backend", &symbol)) goto failed;
    memcpy(&entry, &symbol, sizeof(entry));
    static const image_codec_host host = {
        PAPO_MEDIA_BACKEND_ABI, sizeof(image_codec_host), sizeof(image_decode_request), sizeof(animation), sizeof(graphics_image),
        graphics_image_encode_id, graphics_image_encode_preview, animation_free, malloc, calloc, free
    };
    const image_codec_api *api = entry(&host);
    if (!api || api->abi != PAPO_MEDIA_BACKEND_ABI || api->size != sizeof(*api) ||
        !api->decode || !api->encode_frame) goto failed;
    g_module_make_resident(module); g_module_close(module);
    return (gpointer)api;
 failed:
    if (module) g_module_close(module);
    return NULL;
}
static gpointer load_video(gpointer unused)
{
    (void)unused;
    GModule *module = open_backend(PAPO_VIDEO_MODULE);
    gpointer symbol = NULL;
    video_player_entry entry = NULL;
    G_STATIC_ASSERT(sizeof(entry) == sizeof(symbol));
    if (!module || !g_module_symbol(module, "papo_video_backend", &symbol)) goto failed;
    memcpy(&entry, &symbol, sizeof(entry));
    const video_player_api *api = entry(PAPO_MEDIA_BACKEND_ABI, sizeof(video_player_api));
    if (!api || api->abi != PAPO_MEDIA_BACKEND_ABI || api->size != sizeof(*api) ||
        api->controls_size != sizeof(video_controls) || api->info_size != sizeof(video_info) ||
        !api->open || !api->close || !api->update || !api->changed || !api->render) goto failed;
    g_module_make_resident(module); g_module_close(module);
    return (gpointer)api;
 failed:
    if (module) g_module_close(module);
    return NULL;
}
const image_codec_api *media_image_backend(void)
{
    static GOnce once = G_ONCE_INIT;
    return g_once(&once, load_image, NULL);
}
const video_player_api *media_video_backend(void)
{
    static GOnce once = G_ONCE_INIT;
    return g_once(&once, load_video, NULL);
}
