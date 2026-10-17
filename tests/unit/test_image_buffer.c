#define _GNU_SOURCE
#include "ui/media/media_backend.h"
#include <assert.h>
#include <gmodule.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Guard every codec allocation, including access by unsanitized SIMD assembly.
 * Retain malloc's alignment, but put an inaccessible page immediately after it. */
static struct {
    void *data, *mapping;
    size_t length;
} regions[16];
static void *allocate(size_t bytes)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE), aligned = (bytes + 15U) & ~(size_t)15U;
    size_t usable = (aligned + page - 1) / page * page;
    void *base =
        mmap(NULL, usable + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(base != MAP_FAILED && !mprotect((char *)base + usable, page, PROT_NONE));
    void *data = (char *)base + usable - aligned;
    for (size_t i = 0; i < G_N_ELEMENTS(regions); ++i)
        if (!regions[i].data) {
            regions[i].data = data;
            regions[i].mapping = base;
            regions[i].length = usable + page;
            return data;
        }
    assert(!"allocation registry exhausted");
    return NULL;
}
static void *allocate_zero(size_t count, size_t bytes)
{
    assert(!bytes || count <= SIZE_MAX / bytes);
    void *data = allocate(count * bytes);
    memset(data, 0, count * bytes);
    return data;
}
static void release(void *data)
{
    if (!data)
        return;
    for (size_t i = 0; i < G_N_ELEMENTS(regions); ++i)
        if (regions[i].data == data) {
            assert(!munmap(regions[i].mapping, regions[i].length));
            memset(&regions[i], 0, sizeof(regions[i]));
            return;
        }
    assert(!"unknown or already released buffer");
}
static void release_frames(animation *frames)
{
    if (!frames || atomic_fetch_sub(&frames->references, 1) != 1)
        return;
    for (size_t i = 0; i < frames->count; ++i)
        release(frames->pixels[i]);
    release(frames);
}
static bool encode(const unsigned char *pixels, int width, int height, graphics_terminal terminal,
                   uint32_t id, graphics_image *image)
{
    (void)terminal;
    (void)id;
    assert(pixels && width > 0 && height > 0);
    image->width = width;
    image->height = height;
    return true;
}
static bool region(const unsigned char *pixels, int width, int height, int top, int bottom,
                   int left, int right, graphics_terminal terminal, uint32_t id,
                   graphics_image *image)
{
    assert(top >= 0 && bottom >= 0 && top + bottom < height);
    assert(left >= 0 && right >= 0 && left + right < width);
    return encode(pixels + ((size_t)top * width + left) * 3, width - left - right,
                  height - top - bottom, terminal, id, image);
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    GModule *module = g_module_open(argv[1], G_MODULE_BIND_LOCAL);
    assert(module);
    gpointer symbol = NULL;
    assert(g_module_symbol(module, "papo_image_backend", &symbol));
    image_codec_entry entry;
    memcpy(&entry, &symbol, sizeof(entry));
    image_codec_host host = {PAPO_MEDIA_BACKEND_ABI,
                             sizeof(image_codec_host),
                             sizeof(image_decode_request),
                             sizeof(animation),
                             sizeof(graphics_image),
                             encode,
                             encode,
                             region,
                             release_frames,
                             allocate,
                             allocate_zero,
                             release};
    const image_codec_api *api = entry(&host);
    assert(api);
    image_decode_request request = {.path = argv[2],
                                    .width = 1969,
                                    .height = 1325,
                                    .native = true,
                                    .decode_only = true,
                                    .retain_static = true};
    graphics_image image = {0};
    animation *frames = NULL;
    assert(api->decode(&request, &image, &frames, NULL, NULL));
    assert(frames && frames->width == 1969 && frames->height == 1085);
    request.animation = frames;
    request.width = frames->width;
    request.height = frames->height;
    request.terminal =
        (graphics_terminal){.protocol = GRAPHICS_KITTY, .cell_width = 11, .cell_height = 25};
    assert(api->encode_frame(&request, &image));
    assert(image.width == 1967 && image.height == 1085);
    release_frames(frames);
    for (size_t i = 0; i < G_N_ELEMENTS(regions); ++i)
        assert(!regions[i].data);
    /* Match the production helper: dependency globals live until process exit. */
    g_module_make_resident(module);
    g_module_close(module);
    puts("Unaligned RGB scaling respects protected allocation boundaries");
    return 0;
}
