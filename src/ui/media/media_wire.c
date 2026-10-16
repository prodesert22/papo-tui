#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "ui/media/media_wire.h"
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/mman.h>
#endif
typedef struct {
    int width, height, protocol, cell_width, cell_height, top, bottom, left, right, max_side;
    uint32_t image_id, background, flags;
    uint64_t frame;
} wire_request;
typedef struct {
    int width, height, cell_width, cell_height, source_width, source_height;
    int columns, rows, crop_top, crop_bottom, protocol;
    uint32_t id;
    uint64_t revision;
} wire_image;
typedef struct {
    int width, height, source_width, source_height;
    uint64_t count, bytes, duration;
} wire_animation;
typedef struct {
    void *data;
    size_t size;
    bool mapped;
} wire_storage;
static void storage_free(void *context)
{
    wire_storage *storage = context;
#ifndef _WIN32
    if (storage->mapped)
        munmap(storage->data, storage->size);
    else
#endif
        g_free(storage->data);
    g_free(storage);
}
static wire_storage *storage_new(size_t size)
{
    wire_storage *storage = g_new0(wire_storage, 1);
    storage->size = size;
#ifndef _WIN32
    if (size >= 512U * 1024U) {
        storage->data =
            mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (storage->data != MAP_FAILED)
            storage->mapped = true;
        else
            storage->data = NULL;
    }
#endif
    if (!storage->data)
        storage->data = g_malloc(size ? size : 1);
    return storage;
}
GBytes *media_wire_buffer(size_t size, void **data)
{
    wire_storage *storage = storage_new(size);
    *data = storage->data;
    return g_bytes_new_with_free_func(storage->data, size, storage_free, storage);
}
GBytes *media_wire_store(GVariant *value)
{
    wire_storage *storage = storage_new(g_variant_get_size(value));
    g_variant_store(value, storage->data);
    return g_bytes_new_with_free_func(storage->data, storage->size, storage_free, storage);
}
GVariant *media_wire_bytes(const void *data, size_t size)
{
    if (size < 512U * 1024U)
        return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, size, 1);
    wire_storage *storage = storage_new(size);
    memcpy(storage->data, data, size);
    GBytes *bytes = g_bytes_new_with_free_func(storage->data, size, storage_free, storage);
    GVariant *value = g_variant_new_from_bytes(G_VARIANT_TYPE("ay"), bytes, true);
    g_bytes_unref(bytes);
    return value;
}
bool media_wire_copy(GVariant *v, void *out, size_t length)
{
    if (!v || !g_variant_is_of_type(v, G_VARIANT_TYPE("ay")))
        return false;
    gsize size;
    const void *data = g_variant_get_fixed_array(v, &size, 1);
    if (size != length)
        return false;
    if (size)
        memcpy(out, data, size);
    return true;
}
GVariant *media_wire_request(const image_decode_request *r)
{
    wire_request w = {0};
    w.width = r->width;
    w.height = r->height;
    w.protocol = r->terminal.protocol;
    w.cell_width = r->terminal.cell_width;
    w.cell_height = r->terminal.cell_height;
    w.top = r->top;
    w.bottom = r->bottom;
    w.left = r->left;
    w.right = r->right;
    w.max_side = r->max_side;
    w.image_id = r->image_id;
    w.background = r->background;
    w.frame = r->frame;
    w.flags = (r->preview ? 1U : 0U) | (r->native ? 2U : 0U) | (r->decode_only ? 4U : 0U) |
              (r->retain_static ? 8U : 0U) | (r->static_only ? 16U : 0U) | (r->matte ? 32U : 0U) |
              (r->center ? 64U : 0U) | (r->first_frame ? 128U : 0U) | (r->cover ? 256U : 0U) |
              (r->region ? 512U : 0U) | (r->video_poster ? 1024U : 0U);
    return g_variant_new("(@ays)", media_wire_bytes(&w, sizeof(w)), r->path ? r->path : "");
}
bool media_wire_read_request(GVariant *v, image_decode_request *r, char **path)
{
    memset(r, 0, sizeof(*r));
    *path = NULL;
    if (!g_variant_is_of_type(v, G_VARIANT_TYPE("(ays)")))
        return false;
    GVariant *meta = g_variant_get_child_value(v, 0);
    wire_request w;
    bool ok = media_wire_copy(meta, &w, sizeof(w));
    g_variant_unref(meta);
    if (!ok || w.protocol < GRAPHICS_TEXT || w.protocol > GRAPHICS_CHAFA || w.width < 0 ||
        w.height < 0 || w.width > 16384 || w.height > 16384 || w.cell_width < 0 ||
        w.cell_width > 128 || w.cell_height < 0 || w.cell_height > 128 ||
        w.frame >= ANIMATION_FRAMES || w.top < 0 || w.bottom < 0 || w.left < 0 || w.right < 0 ||
        w.top > 16384 || w.bottom > 16384 || w.left > 16384 || w.right > 16384 ||
        (w.flags & ~2047U))
        return false;
    GVariant *name = g_variant_get_child_value(v, 1);
    gsize length;
    const char *text = g_variant_get_string(name, &length);
    if (length > 4096) {
        g_variant_unref(name);
        return false;
    }
    *path = g_strdup(text);
    g_variant_unref(name);
    r->path = *path;
    r->width = w.width;
    r->height = w.height;
    r->terminal = (graphics_terminal){(graphics_protocol)w.protocol, w.protocol != GRAPHICS_TEXT,
                                      w.cell_width, w.cell_height, false};
    r->top = w.top;
    r->bottom = w.bottom;
    r->left = w.left;
    r->right = w.right;
    r->max_side = w.max_side;
    r->image_id = w.image_id;
    r->background = w.background;
    r->frame = w.frame;
    r->preview = w.flags & 1;
    r->native = w.flags & 2;
    r->decode_only = w.flags & 4;
    r->retain_static = w.flags & 8;
    r->static_only = w.flags & 16;
    r->matte = w.flags & 32;
    r->center = w.flags & 64;
    r->first_frame = w.flags & 128;
    r->cover = w.flags & 256;
    r->region = w.flags & 512;
    r->video_poster = w.flags & 1024;
    return true;
}
GVariant *media_wire_image(const graphics_image *i)
{
    wire_image w = {0};
    w.width = i->width;
    w.height = i->height;
    w.cell_width = i->cell_width;
    w.cell_height = i->cell_height;
    w.source_width = i->source_width;
    w.source_height = i->source_height;
    w.columns = i->columns;
    w.rows = i->rows;
    w.crop_top = i->crop_top;
    w.crop_bottom = i->crop_bottom;
    w.protocol = i->protocol;
    w.id = i->id;
    w.revision = i->revision;
    GVariantBuilder crops;
    g_variant_builder_init(&crops, G_VARIANT_TYPE("av"));
    for (size_t n = 0; n < i->crop_count; ++n)
        g_variant_builder_add(&crops, "v", media_wire_image(&i->crops[n]));
    return g_variant_new("(@ay@ay@av)", media_wire_bytes(&w, sizeof(w)),
                         media_wire_bytes(i->data, i->length), g_variant_builder_end(&crops));
}
static bool read_image(GVariant *v, graphics_image *i, unsigned depth, size_t *total)
{
    memset(i, 0, sizeof(*i));
    if (depth > 1 || !g_variant_is_of_type(v, G_VARIANT_TYPE("(ayayav)")))
        return false;
    GVariant *meta = g_variant_get_child_value(v, 0);
    wire_image w;
    bool ok = media_wire_copy(meta, &w, sizeof(w));
    g_variant_unref(meta);
    if (!ok || w.protocol < GRAPHICS_TEXT || w.protocol > GRAPHICS_CHAFA || w.width < 0 ||
        w.height < 0 || (w.width && !graphics_image_capacity(w.width, w.height)) || w.columns < 0 ||
        w.rows < 0 || w.columns > 16384 || w.rows > 16384 || w.cell_width < 0 || w.cell_height < 0)
        return false;
    GVariant *data = g_variant_get_child_value(v, 1);
    gsize size;
    const void *bytes = g_variant_get_fixed_array(data, &size, 1);
    if (size > MEDIA_WIRE_LIMIT - *total) {
        g_variant_unref(data);
        return false;
    }
    if (w.protocol == GRAPHICS_CHAFA &&
        size != (size_t)w.columns * (size_t)w.rows * sizeof(graphics_cell)) {
        g_variant_unref(data);
        return false;
    }
    i->data = size ? malloc(size) : NULL;
    if (size && !i->data) {
        g_variant_unref(data);
        return false;
    }
    if (size)
        memcpy(i->data, bytes, size);
    i->length = size;
    *total += size;
    g_variant_unref(data);
    i->width = w.width;
    i->height = w.height;
    i->cell_width = w.cell_width;
    i->cell_height = w.cell_height;
    i->source_width = w.source_width;
    i->source_height = w.source_height;
    i->columns = w.columns;
    i->rows = w.rows;
    i->crop_top = w.crop_top;
    i->crop_bottom = w.crop_bottom;
    i->protocol = (graphics_protocol)w.protocol;
    i->id = w.id;
    i->revision = w.revision;
    GVariant *crops = g_variant_get_child_value(v, 2);
    size_t count = g_variant_n_children(crops);
    if (count > 20 || (depth && count))
        ok = false;
    else if (count) {
        i->crops = calloc(count, sizeof(*i->crops));
        ok = i->crops != NULL;
        if (ok)
            i->crop_count = count;
    }
    for (size_t n = 0; ok && n < count; ++n) {
        GVariant *box = g_variant_get_child_value(crops, n), *child = g_variant_get_variant(box);
        ok = read_image(child, &i->crops[n], depth + 1, total);
        g_variant_unref(child);
        g_variant_unref(box);
    }
    g_variant_unref(crops);
    if (!ok)
        graphics_image_free(i);
    return ok;
}
bool media_wire_read_image(GVariant *v, graphics_image *i)
{
    size_t total = 0;
    return read_image(v, i, 0, &total);
}
GVariant *media_wire_animation(const animation *a)
{
    wire_animation w = {0};
    GVariantBuilder frames;
    g_variant_builder_init(&frames, G_VARIANT_TYPE("a(ubay)"));
    if (a) {
        w.width = a->width;
        w.height = a->height;
        w.source_width = a->source_width;
        w.source_height = a->source_height;
        w.count = a->count;
        w.bytes = a->bytes;
        w.duration = a->duration;
        for (size_t n = 0; n < a->count; ++n) {
            size_t size =
                (size_t)a->width * (size_t)a->height * (a->rgba[n] ? 4U : 3U) + IMAGE_PIXEL_PADDING;
            g_variant_builder_add(&frames, "(ub@ay)", a->delay[n], a->rgba[n],
                                  media_wire_bytes(a->pixels[n], size));
        }
    }
    return g_variant_new("(@ay@a(ubay))", media_wire_bytes(&w, sizeof(w)),
                         g_variant_builder_end(&frames));
}
bool media_wire_read_animation(GVariant *v, animation **out)
{
    *out = NULL;
    if (!g_variant_is_of_type(v, G_VARIANT_TYPE("(aya(ubay))")))
        return false;
    GVariant *meta = g_variant_get_child_value(v, 0);
    wire_animation w;
    bool ok = media_wire_copy(meta, &w, sizeof(w));
    g_variant_unref(meta);
    if (!ok || w.count > ANIMATION_FRAMES || w.bytes > ANIMATION_BYTES)
        return false;
    GVariant *list = g_variant_get_child_value(v, 1);
    if (g_variant_n_children(list) != w.count) {
        g_variant_unref(list);
        return false;
    }
    if (!w.count) {
        g_variant_unref(list);
        return true;
    }
    if (!graphics_image_capacity(w.width, w.height)) {
        g_variant_unref(list);
        return false;
    }
    animation *a = calloc(1, sizeof(*a));
    if (!a) {
        g_variant_unref(list);
        return false;
    }
    atomic_init(&a->references, 1);
    a->width = w.width;
    a->height = w.height;
    a->source_width = w.source_width;
    a->source_height = w.source_height;
    a->duration = w.duration;
    for (size_t n = 0; ok && n < w.count; ++n) {
        GVariant *frame = g_variant_get_child_value(list, n), *data;
        gboolean rgba;
        guint32 delay;
        g_variant_get(frame, "(ub@ay)", &delay, &rgba, &data);
        size_t size = (size_t)w.width * (size_t)w.height * (rgba ? 4U : 3U) + IMAGE_PIXEL_PADDING;
        if (size > ANIMATION_BYTES - a->bytes)
            ok = false;
        else {
            a->pixels[n] = malloc(size);
            ok = a->pixels[n] && media_wire_copy(data, a->pixels[n], size);
            a->count = n + 1;
            a->bytes += size;
            a->delay[n] = delay;
            a->rgba[n] = rgba;
        }
        g_variant_unref(data);
        g_variant_unref(frame);
    }
    g_variant_unref(list);
    if (!ok || a->bytes != w.bytes) {
        animation_free(a);
        return false;
    }
    *out = a;
    return true;
}
