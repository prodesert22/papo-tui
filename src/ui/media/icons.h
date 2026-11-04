#ifndef PAPO_ICONS_H
#define PAPO_ICONS_H
#include "model/assets/catalog.h"
#include "ui/input/text.h"
#include "ui/media/graphics.h"
#include "ui/media/media_cache.h"
#define ICON_CUSTOM UINT32_C(0x80000000)
typedef struct icon_view icon_view;
icon_view *icons_create(chat_assets *assets, media_cache *store);
void icons_destroy(icon_view *v);
void icons_assets(icon_view *v, chat_assets *assets, media_cache *store, uint64_t generation);
void icons_begin(icon_view *v);
/* Keep chat placements while replacing only composer icons. */
void icons_begin_editor(icon_view *v);
void icons_begin_overlay(icon_view *v);
void icons_begin_navigation(icon_view *v);
bool icons_notification(icon_view *v, WINDOW *w, int y, int x, int columns, size_t count, int pair);
void icons_overlay(icon_view *v, int y, int x, int rows, int columns);
/* appeared reports an icon showing its first image: transparent pixels would
 * reveal the text drawn in its place, so the caller repaints that text. */
bool icons_poll(icon_view *v, bool *appeared);
void icons_prepare(icon_view *v);
void icons_scope(icon_view *v, const char *scope, size_t offset);
size_t icons_resolve(void *context, const uint32_t *text, size_t length, size_t at, uint32_t *icon);
bool icons_paint(void *context, WINDOW *w, uint32_t icon, int y, int x, int pair, bool selected,
                 size_t offset);
bool icons_paint_size(icon_view *v, WINDOW *w, uint32_t icon, int y, int x, int pair, size_t offset,
                      int pixels);
bool icons_paint_clipped(icon_view *v, WINDOW *w, uint32_t icon, int y, int x, int pair,
                         size_t offset, int pixels, int top, int bottom);
bool icons_literal(const uint32_t *text, size_t at);
/* The text an icon stands for: the emoji itself, or :name: for a custom one. */
bool icons_text(icon_view *v, uint32_t icon, char *out, size_t size);
bool icons_avatar(icon_view *v, const char *id, const char *message, int y, int x, int top,
                  int bottom, int pair);
/* A picture fetched from a public https address, scaled to fit columns x rows;
 * `slot` tells apart several pictures drawn under one scope. */
bool icons_url_picture(icon_view *v, WINDOW *w, const char *url, const char *scope, size_t slot,
                       int y, int x, int columns, int rows, int pair);
bool icons_member_picture(icon_view *v, const char *id, bool banner, const char *scope, int y,
                          int x, int columns, int rows, int pair);
bool icons_message_avatar(icon_view *v, const json_t *message, int y, int x, int top, int bottom,
                          int pair);
/* Same worker implementation as the picker/history; useful for decoder checks. */
bool icons_render_unicode(const char *text, int width, int height, uint32_t background,
                          graphics_terminal terminal, uint32_t id, graphics_image *image);
#endif
