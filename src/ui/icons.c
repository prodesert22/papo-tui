#define _POSIX_C_SOURCE 200809L
#include "ui/icons.h"
#include "ui/emoji_image.h"
#include "ui/message_blocks.h"
#include "ui/theme.h"
#include "model/emoji_catalog.h"
#include "model/history.h"
#include "net/clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ICON_ENTRIES 8192

typedef struct { uint64_t key, used; uint32_t id; } occurrence;
typedef struct {
    media_resource *resource;
    image_decode_request request;
    uint32_t occurrence;
    uint64_t revision;
    int y, x, top, bottom;
    bool editor;
} icon_placement;
struct icon_view {
    chat_assets *assets;
    media_cache *store; /* Borrowed from the active instance's media view. */
    uint64_t generation, tick;
    occurrence *occurrences;
    icon_placement *placements;
    size_t placement_count, placement_capacity;
    char scope[160]; size_t offset;
    bool font;
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
bool icons_render_unicode(const char *text, int width, int height, uint32_t bg,
                           graphics_terminal terminal, uint32_t id, graphics_image *image)
{ return emoji_image_encode(text, width, height, bg, terminal, id, image); }
static void release_placement(icon_placement *p)
{
    media_resource_hold(p->resource, false);
    media_resource_release(p->resource);
}
icon_view *icons_create(chat_assets *assets, media_cache *store)
{
    icon_view *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->occurrences = calloc(ICON_ENTRIES, sizeof(*v->occurrences));
    if (!v->occurrences) { free(v); return NULL; }
    v->assets = assets; v->store = store; v->font = emoji_image_available(); return v;
}
void icons_begin(icon_view *v)
{
    if (!v) return;
    for (size_t i = 0; i < v->placement_count; ++i) release_placement(&v->placements[i]);
    v->placement_count = 0; ++v->tick;
}
void icons_begin_editor(icon_view *v)
{
    if (!v) return;
    ++v->tick;
    size_t count = 0;
    for (size_t i = 0; i < v->placement_count; ++i) {
        icon_placement p = v->placements[i];
        if (p.editor) release_placement(&p);
        else v->placements[count++] = p;
    }
    v->placement_count = count;
}
void icons_destroy(icon_view *v)
{
    if (!v) return;
    icons_begin(v); free(v->occurrences); free(v->placements); free(v);
}
void icons_assets(icon_view *v, chat_assets *assets, media_cache *store, uint64_t generation)
{
    if (!v || (v->assets == assets && v->store == store && v->generation == generation)) return;
    icons_begin(v); v->assets = assets; v->store = store; v->generation = generation;
}
void icons_scope(icon_view *v, const char *scope, size_t offset)
{ if (v) { snprintf(v->scope, sizeof(v->scope), "%s", scope); v->offset = offset; } }
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
static bool offer(icon_view *v, media_resource *resource, image_decode_request request,
                  int y, int x, int top, int bottom, size_t offset)
{
    if (!resource) return false;
    if (y < 0 || x < 0 || top < 0 || bottom < 0 || !graphics_terminal_get().supported) {
        media_resource_release(resource); return false;
    }
    if (v->placement_count == v->placement_capacity) {
        size_t capacity = v->placement_capacity ? v->placement_capacity * 2 : 64;
        icon_placement *p = capacity <= ICON_ENTRIES ? realloc(v->placements, capacity * sizeof(*p)) : NULL;
        if (!p) { media_resource_release(resource); return false; }
        v->placements = p; v->placement_capacity = capacity;
    }
    uint64_t key = hash(v->scope) ^ ((uint64_t)(v->offset + offset) * UINT64_C(1099511628211));
    uint32_t id = occurrence_id(v, key);
    if (!id) { media_resource_release(resource); return false; }
    request.top = top; request.bottom = bottom;
    media_resource_hold(resource, true); media_resource_use_image(resource, false, false);
    icon_placement *p = &v->placements[v->placement_count++];
    *p = (icon_placement){.resource=resource,.request=request,.occurrence=id,.y=y,.x=x,.top=top,.bottom=bottom,
        .editor=!strncmp(v->scope,"editor:",7)||!strcmp(v->scope,"emoji-button")};
    uint64_t now = 0; net_now_ms(&now);
    return media_resource_image(resource, false, request, now) != NULL;
}
bool icons_paint(void *context, WINDOW *w, uint32_t icon, int y, int x, int pair, bool selected, size_t offset)
{
    icon_view *v = context; graphics_terminal terminal = graphics_terminal_get();
    if (!v || !v->store || !terminal.supported || terminal.cell_width <= 0 || terminal.cell_height <= 0) return false;
    media_resource *resource = NULL;
    if (icon & ICON_CUSTOM) {
        const server_emoji *e = chat_assets_emoji(v->assets, (icon & ~ICON_CUSTOM) - 1);
        if (e && e->path) resource = media_cache_acquire_file(v->store, e->path, e->revision);
    } else {
        const unicode_emoji *e = emoji_catalog_get(icon - 1);
        if (!e || !v->font) return false;
        resource = media_cache_acquire_unicode(v->store, e->text);
    }
    image_decode_request request = {.width=terminal.cell_width*2,.height=terminal.cell_height,.terminal=terminal,
        .matte=true,.center=true,.background=background(selected?TEXT_SELECTION:pair),.max_side=512};
    return offer(v, resource, request, getbegy(w)+y, getbegx(w)+x, 0, 0, offset);
}
static bool avatar_image(icon_view *v, const author_avatar *avatar, const char *message,
                         int y, int x, int top, int bottom, int pair)
{
    if (!avatar || (!avatar->path && !avatar->url) || !v->store) return false;
    graphics_terminal terminal = graphics_terminal_get();
    int rows = (MESSAGE_AVATAR_PIXELS + terminal.cell_height - 1) / terminal.cell_height;
    int crop_top = top * terminal.cell_height;
    int crop_bottom = MESSAGE_AVATAR_PIXELS - (rows - bottom) * terminal.cell_height;
    if (crop_bottom < 0) crop_bottom = 0;
    if (crop_top + crop_bottom >= MESSAGE_AVATAR_PIXELS) return false;
    media_resource *resource = avatar->url ? media_cache_acquire_url(v->store, avatar->url, avatar->revision) :
        media_cache_acquire_file(v->store, avatar->path, avatar->revision);
    media_resource_cache_for_session(resource);
    image_decode_request request = {.width=MESSAGE_AVATAR_PIXELS,.height=MESSAGE_AVATAR_PIXELS,.terminal=terminal,
        .cover=true,.center=true,.matte=true,.background=background(pair),.max_side=512};
    icons_scope(v, message, 0);
    return offer(v, resource, request, y, x, crop_top, crop_bottom, SIZE_MAX);
}
bool icons_avatar(icon_view *v, const char *id, const char *message, int y, int x, int top, int bottom, int pair)
{
    if (!v || !graphics_terminal_get().supported) return false;
    return avatar_image(v, chat_assets_avatar(v->assets,id), message,y,x,top,bottom,pair);
}
bool icons_message_avatar(icon_view *v, const json_t *message, int y, int x, int top, int bottom, int pair)
{
    if (!v || !graphics_terminal_get().supported) return false;
    const author_avatar *avatar = history_message_webhook(message) ? chat_assets_webhook_avatar(v->assets,message) :
        chat_assets_avatar(v->assets,json_string_value(json_object_get(message,"author_id")));
    return avatar_image(v,avatar,json_string_value(json_object_get(message,"id")),y,x,top,bottom,pair);
}
static const graphics_image *placement_image(icon_placement *p, uint64_t now)
{
    const graphics_image *image = media_resource_image(p->resource,false,p->request,now);
    return image ? image : media_resource_fallback_image(p->resource,p->request);
}
bool icons_poll(icon_view *v)
{
    if (!v) return false;
    uint64_t now = 0; net_now_ms(&now); bool changed = false;
    for (size_t i = 0; i < v->placement_count; ++i) {
        const graphics_image *image = placement_image(&v->placements[i],now);
        changed |= image && image->revision != v->placements[i].revision;
    }
    return changed;
}
void icons_prepare(icon_view *v)
{
    if (!v) return;
    uint64_t now = 0; net_now_ms(&now);
    for (size_t i = 0; i < v->placement_count; ++i) {
        icon_placement *p = &v->placements[i];
        const graphics_image *image = placement_image(p,now);
        if (!image) continue;
        p->revision = image->revision;
        graphics_image_place_id(image,p->occurrence,p->y,p->x,
            image->protocol==GRAPHICS_KITTY?p->top:0, image->protocol==GRAPHICS_KITTY?p->bottom:0);
    }
}
