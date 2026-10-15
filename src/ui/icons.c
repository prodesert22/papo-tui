#define _POSIX_C_SOURCE 200809L
#include "ui/icons.h"
#include "ui/image_decode.h"
#include "ui/message_blocks.h"
#include "ui/theme.h"
#include "model/emoji_catalog.h"
#include "model/history.h"
#include "net/clock.h"
#include <pango/pangocairo.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define ICON_ENTRIES 8192
#define ICON_BYTES (32U * 1024U * 1024U)
typedef struct {
    char key[224];
    char *path;
    const char *text;
    graphics_image image;
    animation *frames;
    uint64_t revision, token, used, epoch, presented;
    size_t frame, fallback;
    bool has_fallback;
    int width, height;
    uint32_t background;
    bool failed, busy;
} icon_entry;
typedef struct { size_t entry; uint32_t occurrence; int y, x, top, bottom; bool editor; } icon_placement;
typedef struct { uint64_t key, used; uint32_t id; } occurrence;
typedef struct {
    char *path, *text;
    uint64_t token;
    uint32_t background, image_id;
    int width, height;
    bool cover;
    graphics_terminal terminal;
    animation *frames;
    size_t frame;
} icon_job;
struct icon_view {
    chat_assets *assets;
    uint64_t generation, tick, serial;
    icon_entry *entries;
    occurrence *occurrences;
    icon_placement *placements;
    size_t placement_count, placement_capacity;
    char scope[160]; size_t offset;
    bool font, started, stopping, pending, ready, working;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    atomic_bool cancel;
    icon_job job;
    graphics_image result;
    animation *result_frames;
    uint64_t result_token;
    bool result_ok;
};
static uint64_t hash(const char *s)
{
    uint64_t n = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) { n ^= *p; n *= UINT64_C(1099511628211); }
    return n;
}
static uint32_t background(int pair)
{
    if (pair == FOCUS_AREA || pair == FOCUS_MUTED || pair == PROFILE_BUTTON_FOCUS) return 0x1d2026;
    if (pair == HISTORY_AREA || pair == HISTORY_ACCENT || pair == HISTORY_MUTED) return 0x15171b;
    if (pair == EMBED_AREA || pair == EMBED_ACCENT || pair == EMBED_MUTED) return 0x1d2026;
    if (pair == TEXT_SELECTION) return 0xffb347;
    return 0x15181e;
}
static bool render_unicode(PangoFontMap *map, const char *text, int width, int height, uint32_t bg,
                           graphics_terminal terminal, uint32_t id, graphics_image *image)
{
    if (!text || width < 1 || height < 1 || width > 640 || height > 360) return false;
    bool ok = false;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);
    PangoContext *context = pango_font_map_create_context(map);
    PangoLayout *layout = pango_layout_new(context);
    PangoFontDescription *font = pango_font_description_from_string("Noto Color Emoji");
    pango_font_description_set_absolute_size(font, height * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    PangoRectangle ink; pango_layout_get_pixel_extents(layout, &ink, NULL);
    if (cairo_status(cr) == CAIRO_STATUS_SUCCESS && ink.width > 0 && ink.height > 0 && !pango_layout_get_unknown_glyphs_count(layout)) {
        cairo_set_source_rgb(cr, ((bg >> 16) & 255) / 255., ((bg >> 8) & 255) / 255., (bg & 255) / 255.);
        cairo_paint(cr);
        double sx = (double)width / ink.width, sy = (double)height / ink.height;
        double scale = sx < sy ? sx : sy;
        cairo_translate(cr, (width - ink.width * scale) / 2, (height - ink.height * scale) / 2);
        cairo_scale(cr, scale, scale); cairo_move_to(cr, -ink.x, -ink.y);
        pango_cairo_show_layout(cr, layout); cairo_surface_flush(surface);
        unsigned char *rgb = calloc(1, image_frame_bytes(width, height));
        if (rgb) {
            const unsigned char *pixels = cairo_image_surface_get_data(surface);
            int stride = cairo_image_surface_get_stride(surface);
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                uint32_t pixel; memcpy(&pixel, pixels + y * stride + x * 4, sizeof(pixel));
                size_t at = ((size_t)y * (size_t)width + (size_t)x) * 3;
                rgb[at] = (unsigned char)(pixel >> 16); rgb[at + 1] = (unsigned char)(pixel >> 8); rgb[at + 2] = (unsigned char)pixel;
            }
            ok = graphics_image_encode_id(rgb, width, height, terminal, id, image); free(rgb);
        }
    }
    pango_font_description_free(font); g_object_unref(layout); g_object_unref(context);
    cairo_destroy(cr); cairo_surface_destroy(surface); return ok;
}
bool icons_render_unicode(const char *text, int width, int height, uint32_t bg,
                           graphics_terminal terminal, uint32_t id, graphics_image *image)
{
    PangoFontMap *map = pango_cairo_font_map_new();
    bool ok = render_unicode(map, text, width, height, bg, terminal, id, image);
    g_object_unref(map); return ok;
}
static bool interrupted(void *context) { return atomic_load(&((icon_view *)context)->cancel); }
static void free_job(icon_job *j)
{
    free(j->path); free(j->text); animation_free(j->frames); memset(j, 0, sizeof(*j));
}
static void *worker(void *context)
{
    icon_view *v = context;
    PangoFontMap *fontmap = pango_cairo_font_map_new();
    pthread_mutex_lock(&v->lock);
    for (;;) {
        while (!v->pending && !v->stopping) pthread_cond_wait(&v->wake, &v->lock);
        if (v->stopping) break;
        icon_job j = v->job; memset(&v->job, 0, sizeof(v->job)); v->pending = false;
        pthread_mutex_unlock(&v->lock);
        graphics_image image = {0}; animation *frames = NULL;
        bool ok;
        if (j.text) ok = render_unicode(fontmap, j.text, j.width, j.height, j.background, j.terminal, j.image_id, &image);
        else {
            image_decode_request request = {.path = j.path, .width = j.width, .height = j.height,
                .terminal = j.terminal, .image_id = j.image_id, .preview = true,
                .animation = j.frames, .frame = j.frame, .matte = true, .background = j.background,
                .center = true, .max_side = 512, .cover = j.cover};
            if (j.frames) ok = image_encode_frame(&request, &image);
            else {
                ok = image_decode_file(&request, &image, &frames, interrupted, v);
                if (!ok && !interrupted(v)) {
                    request.first_frame = true;
                    ok = image_decode_file(&request, &image, &frames, interrupted, v);
                }
            }
        }
        uint64_t token = j.token; free_job(&j);
        pthread_mutex_lock(&v->lock);
        if (v->stopping) { graphics_image_free(&image); animation_free(frames); break; }
        v->result = image; v->result_frames = frames; v->result_token = token;
        v->result_ok = ok; v->ready = true;
        while (v->ready && !v->stopping) pthread_cond_wait(&v->wake, &v->lock);
    }
    pthread_mutex_unlock(&v->lock); g_object_unref(fontmap); return NULL;
}
static void entry_clear(icon_entry *e)
{
    free(e->path); animation_free(e->frames); graphics_image_free(&e->image); memset(e, 0, sizeof(*e));
}
icon_view *icons_create(chat_assets *assets)
{
    icon_view *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->entries = calloc(ICON_ENTRIES, sizeof(*v->entries));
    v->occurrences = calloc(ICON_ENTRIES, sizeof(*v->occurrences));
    if (!v->entries || !v->occurrences) { free(v->entries); free(v->occurrences); free(v); return NULL; }
    if (pthread_mutex_init(&v->lock, NULL)) { free(v->entries); free(v->occurrences); free(v); return NULL; }
    if (pthread_cond_init(&v->wake, NULL)) { pthread_mutex_destroy(&v->lock); free(v->entries); free(v->occurrences); free(v); return NULL; }
    atomic_init(&v->cancel, false); v->assets = assets;
    PangoFontMap *map = pango_cairo_font_map_new();
    PangoFontFamily **families = NULL; int count = 0;
    pango_font_map_list_families(map, &families, &count);
    for (int i = 0; i < count; ++i) if (!strcmp(pango_font_family_get_name(families[i]), "Noto Color Emoji")) v->font = true;
    g_free(families); g_object_unref(map);
    if (pthread_create(&v->thread, NULL, worker, v)) { icons_destroy(v); return NULL; }
    v->started = true; return v;
}
void icons_destroy(icon_view *v)
{
    if (!v) return;
    atomic_store(&v->cancel, true);
    pthread_mutex_lock(&v->lock); v->stopping = true; pthread_cond_signal(&v->wake); pthread_mutex_unlock(&v->lock);
    if (v->started) pthread_join(v->thread, NULL);
    for (size_t i = 0; i < ICON_ENTRIES; ++i) entry_clear(&v->entries[i]);
    free_job(&v->job); graphics_image_free(&v->result); animation_free(v->result_frames);
    pthread_cond_destroy(&v->wake); pthread_mutex_destroy(&v->lock);
    free(v->placements); free(v->occurrences); free(v->entries); free(v);
}
void icons_assets(icon_view *v, chat_assets *assets, uint64_t generation)
{
    if (!v || (v->assets == assets && v->generation == generation)) return;
    for (size_t i = 0; i < ICON_ENTRIES; ++i) entry_clear(&v->entries[i]);
    v->placement_count = 0; v->assets = assets; v->generation = generation;
}
void icons_begin(icon_view *v) { if (v) { ++v->tick; v->placement_count = 0; } }
void icons_begin_editor(icon_view *v)
{
    if (!v) return;
    ++v->tick;
    size_t count = 0;
    for (size_t i = 0; i < v->placement_count; ++i) {
        icon_placement p = v->placements[i];
        if (p.editor) continue;
        v->entries[p.entry].used = v->tick;
        v->placements[count++] = p;
    }
    v->placement_count = count;
}
void icons_scope(icon_view *v, const char *scope, size_t offset)
{
    if (v) { snprintf(v->scope, sizeof(v->scope), "%s", scope); v->offset = offset; }
}
static bool literal(const uint32_t *text, size_t at)
{
    size_t delimiter = 0;
    for (size_t i = 0; i < at;) {
        if (text[i] != '`' || (i && text[i - 1] == '\\')) { ++i; continue; }
        size_t end = i + 1; while (end < at && text[end] == '`') ++end;
        size_t count = end - i;
        if (!delimiter) delimiter = count; else if (count == delimiter) delimiter = 0;
        i = end;
    }
    if (delimiter) return true;
    size_t start = at;
    while (start && text[start - 1] > ' ') --start;
    for (size_t i = start; i + 2 < at; ++i) if (text[i] == ':' && text[i + 1] == '/' && text[i + 2] == '/') return true;
    return false;
}
size_t icons_resolve(void *context, const uint32_t *text, size_t length, size_t at, uint32_t *icon)
{
    icon_view *v = context;
    if (!v || at >= length) return 0;
    if (text[at] == ':' && !literal(text, at)) {
        chat_assets_emojis_request(v->assets, false);
        size_t longest = 0;
        for (size_t i = 0; i < chat_assets_emoji_count(v->assets); ++i) {
            const server_emoji *e = chat_assets_emoji(v->assets, i);
            text_buffer name;
            if (!text_decode(&name, e->name) || name.length + 2 > length - at) continue;
            if (text[at + name.length + 1] == ':' && name.length + 2 > longest &&
                !memcmp(text + at + 1, name.data, name.length * sizeof(*text)) && graphics_terminal_get().supported && e->path) {
                longest = name.length + 2; *icon = ICON_CUSTOM | (uint32_t)(i + 1);
            }
        }
        return longest;
    }
    if (text[at] < 0x23 || (text[at] > 0x39 && text[at] < 0xa9) || !v->font || !graphics_terminal_get().supported) return 0;
    size_t consumed;
    const unicode_emoji *e = emoji_catalog_match(text + at, length - at, &consumed);
    if (!e || literal(text, at)) return 0;
    /* Catalogue pointers are stable; avoid exposing a second sequence table. */
    *icon = (uint32_t)(emoji_catalog_index(e) + 1); return consumed;
}
static size_t entry_bytes(const icon_entry *e) { return graphics_image_bytes(&e->image) + (e->frames ? e->frames->bytes : 0); }
static icon_entry *get_entry(icon_view *v, const char *key, uint64_t revision, const char *path,
                              const char *text, int width, int height, uint32_t bg)
{
    graphics_terminal terminal = graphics_terminal_get();
    icon_entry *slot = NULL;
    for (size_t i = 0; i < ICON_ENTRIES; ++i) {
        icon_entry *e = &v->entries[i];
        if (!strcmp(e->key, key)) {
            if (e->revision != revision || e->width != width || e->height != height || e->background != bg ||
                (e->image.data && (e->image.protocol != terminal.protocol ||
                 e->image.cell_width != terminal.cell_width || e->image.cell_height != terminal.cell_height))) {
                if (e->busy) return NULL;
                entry_clear(e); slot = e; break;
            }
            e->used = v->tick; return e;
        }
        if (!*e->key && !slot) slot = e;
    }
    if (!slot) for (size_t i = 0; i < ICON_ENTRIES; ++i) {
        icon_entry *e = &v->entries[i];
        if (!e->busy && e->used < v->tick && (!slot || e->used < slot->used)) slot = e;
    }
    if (!slot) return NULL;
    entry_clear(slot); snprintf(slot->key, sizeof(slot->key), "%s", key);
    slot->revision = revision; slot->path = path ? strdup(path) : NULL; slot->text = text;
    slot->width = width; slot->height = height; slot->background = bg; slot->used = v->tick;
    slot->failed = path && !slot->path;
    for (size_t i = 0; i < ICON_ENTRIES; ++i) {
        icon_entry *old = &v->entries[i];
        if (old == slot || !old->image.data || old->revision != revision || old->width != width || old->height != height) continue;
        if ((text && old->text == text) || (path && old->path && !strcmp(path, old->path))) {
            slot->has_fallback = true; slot->fallback = i; break;
        }
    }
    return slot;
}
static uint32_t occurrence_id(icon_view *v, uint64_t key)
{
    occurrence *slot = NULL;
    for (size_t i = 0; i < ICON_ENTRIES; ++i) {
        occurrence *o = &v->occurrences[i];
        if (o->id && o->key == key) { o->used = v->tick; return o->id; }
        if (!o->id && !slot) slot = o;
    }
    if (!slot) for (size_t i = 0; i < ICON_ENTRIES; ++i) {
        occurrence *o = &v->occurrences[i]; if (o->used < v->tick && (!slot || o->used < slot->used)) slot = o;
    }
    if (!slot) return 0;
    *slot = (occurrence){key, v->tick, graphics_image_next_id()}; return slot->id;
}
static icon_entry *fallback_entry(icon_view *v, const icon_entry *e)
{
    if (!e->has_fallback || e->fallback >= ICON_ENTRIES) return NULL;
    icon_entry *old = &v->entries[e->fallback];
    if (!old->image.data || old->revision != e->revision || old->width != e->width || old->height != e->height) return NULL;
    return (e->text && old->text == e->text) || (e->path && old->path && !strcmp(e->path, old->path)) ? old : NULL;
}
static bool offer(icon_view *v, icon_entry *e, int y, int x, int top, int bottom, size_t offset)
{
    if (!e || y < 0 || x < 0 || top < 0 || bottom < 0 || !graphics_terminal_get().supported) return false;
    if (v->placement_count == v->placement_capacity) {
        size_t capacity = v->placement_capacity ? v->placement_capacity * 2 : 64;
        if (capacity > ICON_ENTRIES) return false;
        icon_placement *p = realloc(v->placements, capacity * sizeof(*p));
        if (!p) return false;
        v->placements = p; v->placement_capacity = capacity;
    }
    uint64_t key = hash(v->scope) ^ ((uint64_t)(v->offset + offset) * UINT64_C(1099511628211));
    uint32_t occurrence = occurrence_id(v, key);
    if (!occurrence) return false;
    v->placements[v->placement_count++] = (icon_placement){(size_t)(e - v->entries), occurrence, y, x, top, bottom,
        !strncmp(v->scope, "editor:", 7) || !strcmp(v->scope, "emoji-button")};
    if (!e->image.data) {
        icon_entry *old = fallback_entry(v, e);
        if (old) { old->used = v->tick; return true; }
    }
    return e->image.data != NULL;
}
bool icons_paint(void *context, WINDOW *w, uint32_t icon, int y, int x, int pair, bool selected, size_t offset)
{
    icon_view *v = context;
    graphics_terminal terminal = graphics_terminal_get();
    if (!v || !terminal.supported || terminal.cell_width <= 0 || terminal.cell_height <= 0) return false;
    char key[224]; const char *path = NULL, *text = NULL; uint64_t revision = 1;
    if (icon & ICON_CUSTOM) {
        const server_emoji *e = chat_assets_emoji(v->assets, (icon & ~ICON_CUSTOM) - 1);
        if (!e || !e->path) return false;
        path = e->path; revision = e->revision; snprintf(key, sizeof(key), "emoji:%s:%x", e->id, background(pair));
    } else {
        const unicode_emoji *e = emoji_catalog_get(icon - 1);
        if (!e || !v->font) return false;
        text = e->text; snprintf(key, sizeof(key), "unicode:%u:%x", icon, background(pair));
    }
    uint32_t bg = background(selected ? TEXT_SELECTION : pair);
    icon_entry *e = get_entry(v, key, revision, path, text, terminal.cell_width * 2, terminal.cell_height, bg);
    return offer(v, e, getbegy(w) + y, getbegx(w) + x, 0, 0, offset);
}
static bool avatar_image(icon_view *v, const author_avatar *avatar, const char *message,
                         int y, int x, int top, int bottom, int pair)
{
    if (!avatar || !avatar->path) return false;
    graphics_terminal t = graphics_terminal_get(); char key[224];
    snprintf(key, sizeof(key), "avatar:%s:%x", avatar->id, background(pair));
    /* The image itself fills 48x48; no surrounding header-colored canvas. */
    icon_entry *e = get_entry(v, key, avatar->revision, avatar->path, NULL,
                              MESSAGE_AVATAR_PIXELS, MESSAGE_AVATAR_PIXELS, background(pair));
    int crop_top = top * t.cell_height;
    int rows = (MESSAGE_AVATAR_PIXELS + t.cell_height - 1) / t.cell_height;
    int crop_bottom = MESSAGE_AVATAR_PIXELS - (rows - bottom) * t.cell_height;
    if (crop_bottom < 0) crop_bottom = 0;
    if (crop_top + crop_bottom >= MESSAGE_AVATAR_PIXELS) return false;
    icons_scope(v, message, 0);
    return offer(v, e, y, x, crop_top, crop_bottom, SIZE_MAX);
}
bool icons_avatar(icon_view *v, const char *id, const char *message, int y, int x, int top, int bottom, int pair)
{
    if (!v || !graphics_terminal_get().supported) return false;
    return avatar_image(v, chat_assets_avatar(v->assets, id), message, y, x, top, bottom, pair);
}
bool icons_message_avatar(icon_view *v, const json_t *message, int y, int x, int top, int bottom, int pair)
{
    if (!v || !graphics_terminal_get().supported) return false;
    const author_avatar *avatar = history_message_webhook(message) ? chat_assets_webhook_avatar(v->assets, message) :
        chat_assets_avatar(v->assets, json_string_value(json_object_get(message, "author_id")));
    return avatar_image(v, avatar, json_string_value(json_object_get(message, "id")), y, x, top, bottom, pair);
}
bool icons_poll(icon_view *v)
{
    if (!v) return false;
    bool changed = false; uint64_t now = 0; net_now_ms(&now);
    pthread_mutex_lock(&v->lock);
    if (v->ready) {
        icon_entry *target = NULL;
        for (size_t i = 0; i < ICON_ENTRIES; ++i) if (v->entries[i].token == v->result_token && v->entries[i].busy) { target = &v->entries[i]; break; }
        if (target) {
            graphics_image_free(&target->image); target->image = v->result; memset(&v->result, 0, sizeof(v->result));
            if (v->result_frames) { animation_free(target->frames); target->frames = v->result_frames; v->result_frames = NULL; target->epoch = now; }
            target->failed = !v->result_ok; target->busy = false; target->presented = now; changed = true;
        }
        graphics_image_free(&v->result); animation_free(v->result_frames); v->result_frames = NULL;
        v->ready = v->working = false; pthread_cond_signal(&v->wake);
    }
    size_t bytes = 0;
    for (size_t i = 0; i < ICON_ENTRIES; ++i) bytes += entry_bytes(&v->entries[i]);
    while (bytes > ICON_BYTES) {
        icon_entry *oldest = NULL;
        for (size_t i = 0; i < ICON_ENTRIES; ++i) {
            icon_entry *e = &v->entries[i];
            if (!e->busy && e->used < v->tick && entry_bytes(e) && (!oldest || e->used < oldest->used)) oldest = e;
        }
        if (!oldest) {
            for (size_t i = 0; i < ICON_ENTRIES; ++i) if (v->entries[i].frames && !v->entries[i].busy) {
                bytes -= v->entries[i].frames->bytes; animation_free(v->entries[i].frames); v->entries[i].frames = NULL;
                if (bytes <= ICON_BYTES) break;
            }
            if (bytes > ICON_BYTES) for (size_t i = 0; i < ICON_ENTRIES; ++i) {
                icon_entry *e = &v->entries[i];
                if (e->busy || !e->image.data) continue;
                bytes -= graphics_image_bytes(&e->image); graphics_image_free(&e->image);
                e->failed = true; e->has_fallback = false; changed = true;
                if (bytes <= ICON_BYTES) break;
            }
            break;
        }
        bytes -= entry_bytes(oldest); entry_clear(oldest);
    }
    if (!v->working) for (size_t i = 0; i < v->placement_count; ++i) {
        icon_entry *e = &v->entries[v->placements[i].entry];
        if (!*e->key || e->failed || e->busy) continue;
        size_t frame = e->frames ? animation_frame_at(e->frames, e->epoch, now) : 0;
        if (e->image.data && (!e->frames || frame == e->frame || now - e->presented < 67)) continue;
        icon_job j = {.token = ++v->serial, .background = e->background, .width = e->width, .height = e->height,
            .cover = !strncmp(e->key, "avatar:", 7),
            .terminal = graphics_terminal_get(), .image_id = e->image.id ? e->image.id : graphics_image_next_id(),
            .frames = e->frames, .frame = frame};
        if (e->path) j.path = strdup(e->path);
        if (e->text) j.text = strdup(e->text);
        if ((!j.path && !j.text) || e->width > 640 || e->height > 360) { free(j.path); free(j.text); e->failed = true; continue; }
        animation_ref(j.frames); v->job = j; e->token = j.token; e->busy = true; e->frame = frame;
        v->working = v->pending = true; pthread_cond_signal(&v->wake); break;
    }
    pthread_mutex_unlock(&v->lock); return changed;
}
void icons_prepare(icon_view *v)
{
    if (!v) return;
    for (size_t i = 0; i < v->placement_count; ++i) {
        icon_placement *p = &v->placements[i]; icon_entry *e = &v->entries[p->entry];
        if (!e->image.data) {
            icon_entry *fallback = fallback_entry(v, e);
            if (fallback) e = fallback;
        }
        if (e->image.data) graphics_image_place_id(&e->image, p->occurrence, p->y, p->x, p->top, p->bottom);
    }
}
