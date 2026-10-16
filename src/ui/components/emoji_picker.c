#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/components/emoji_picker.h"
#include "model/text/emoji_catalog.h"
#include "ui/components/theme.h"
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct emoji_picker {
    chat_assets *assets;
    icon_view *icons;
    WINDOW *window;
    text_buffer search;
    uint32_t *matches;
    char **unicode_search;
    size_t count, selected, first;
    uint64_t revision, generation;
    bool open, server;
    int focus, columns, rows, left, top, width, height;
    char channel[37], selected_key[160], error[160];
};
static char *fold(const char *text)
{
    char *lower = g_utf8_casefold(text, -1);
    char *normal = lower ? g_utf8_normalize(lower, -1, G_NORMALIZE_ALL) : NULL;
    g_free(lower);
    if (!normal)
        return NULL;
    char *result = g_malloc(strlen(normal) + 1), *out = result;
    for (const char *p = normal; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_combining_class(c))
            continue;
        out += g_unichar_to_utf8(c, out);
    }
    *out = 0;
    g_free(normal);
    return result;
}
static const char *item_name(emoji_picker *p, uint32_t id)
{
    if (id & ICON_CUSTOM) {
        const server_emoji *e = chat_assets_emoji(p->assets, (id & ~ICON_CUSTOM) - 1);
        return e ? e->name : "";
    }
    const unicode_emoji *e = emoji_catalog_get(id - 1);
    return e ? e->name : "";
}
static const char *item_key(emoji_picker *p, uint32_t id)
{
    if (id & ICON_CUSTOM) {
        const server_emoji *e = chat_assets_emoji(p->assets, (id & ~ICON_CUSTOM) - 1);
        return e ? e->id : "";
    }
    const unicode_emoji *e = emoji_catalog_get(id - 1);
    return e ? e->text : "";
}
static void remember(emoji_picker *p)
{
    snprintf(p->selected_key, sizeof(p->selected_key), "%s",
             p->selected < p->count ? item_key(p, p->matches[p->selected]) : "");
}
static void update_matches(emoji_picker *p)
{
    char query[TEXT_UTF8_BYTES];
    text_encode(&p->search, query);
    char *normalized = fold(query);
    p->count = 0;
    size_t count = p->server ? chat_assets_emoji_count(p->assets) : emoji_catalog_count();
    for (size_t i = 0; i < count; ++i) {
        size_t index = p->server ? i : emoji_catalog_order(i);
        uint32_t id = (uint32_t)(index + 1) | (p->server ? ICON_CUSTOM : 0);
        const unicode_emoji *e = !p->server ? emoji_catalog_get(index) : NULL;
        char *candidate = e ? p->unicode_search[index] : fold(item_name(p, id));
        if (candidate && normalized && strstr(candidate, normalized))
            p->matches[p->count++] = id;
        if (!e)
            g_free(candidate);
    }
    g_free(normalized);
    p->selected = 0;
    for (size_t i = 0; i < p->count; ++i)
        if (!strcmp(item_key(p, p->matches[i]), p->selected_key)) {
            p->selected = i;
            break;
        }
    if (p->first > p->selected)
        p->first = p->selected;
    p->revision = chat_assets_revision(p->assets);
    remember(p);
}
emoji_picker *emoji_picker_create(chat_assets *assets, icon_view *icons)
{
    emoji_picker *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->matches = calloc(emoji_catalog_count() + 500, sizeof(*p->matches));
    if (!p->matches) {
        free(p);
        return NULL;
    }
    p->unicode_search = calloc(emoji_catalog_count(), sizeof(*p->unicode_search));
    if (!p->unicode_search) {
        free(p->matches);
        free(p);
        return NULL;
    }
    for (size_t i = 0; i < emoji_catalog_count(); ++i) {
        const unicode_emoji *e = emoji_catalog_get(i);
        char *words = g_strdup_printf("%s %s %s %s", e->name, e->english, e->keywords, e->text);
        if (words)
            p->unicode_search[i] = fold(words);
        g_free(words);
    }
    p->assets = assets;
    p->icons = icons;
    return p;
}
void emoji_picker_destroy(emoji_picker *p)
{
    if (p) {
        if (p->window)
            delwin(p->window);
        for (size_t i = 0; i < emoji_catalog_count(); ++i)
            g_free(p->unicode_search[i]);
        free(p->unicode_search);
        free(p->matches);
        free(p);
    }
}
bool emoji_picker_open(const emoji_picker *p)
{
    return p && p->open;
}
void emoji_picker_close(emoji_picker *p)
{
    if (!p)
        return;
    p->open = false;
    if (p->window) {
        delwin(p->window);
        p->window = NULL;
    }
}
void emoji_picker_show(emoji_picker *p, const char *channel, uint64_t generation)
{
    if (!p)
        return;
    p->open = true;
    p->focus = 0;
    p->error[0] = 0;
    snprintf(p->channel, sizeof(p->channel), "%s", channel);
    p->generation = generation;
    chat_assets_emojis_request(p->assets, false);
    update_matches(p);
}
bool emoji_picker_update(emoji_picker *p, chat_assets *assets, const char *channel,
                         uint64_t generation)
{
    if (!p)
        return false;
    bool open = p->open;
    if (p->assets != assets || strcmp(p->channel, channel) || p->generation != generation)
        emoji_picker_close(p);
    p->assets = assets;
    if (p->open && p->revision != chat_assets_revision(assets)) {
        update_matches(p);
        return true;
    }
    return open != p->open;
}
static void move_selection(emoji_picker *p, int delta)
{
    if (delta < 0)
        p->selected = p->selected < (size_t)-delta ? 0 : p->selected - (size_t)-delta;
    else if (p->count)
        p->selected =
            (size_t)delta >= p->count - p->selected ? p->count - 1 : p->selected + (size_t)delta;
    remember(p);
}
static bool choose(emoji_picker *p, text_buffer *editor)
{
    if (!editor || p->selected >= p->count)
        return false;
    char value[256];
    uint32_t id = p->matches[p->selected];
    if (id & ICON_CUSTOM)
        snprintf(value, sizeof(value), ":%s:", item_name(p, id));
    else
        snprintf(value, sizeof(value), "%s", item_key(p, id));
    text_buffer insertion;
    if (!text_decode(&insertion, value) || !text_insert_buffer(editor, &insertion)) {
        snprintf(p->error, sizeof(p->error), "O emoji excede o limite do editor.");
        return false;
    }
    emoji_picker_close(p);
    return true;
}
bool emoji_picker_key(emoji_picker *p, text_buffer *editor, int type, uint32_t key, bool *inserted)
{
    *inserted = false;
    if (!emoji_picker_open(p))
        return false;
    if ((type == OK && key == 27) || (type == KEY_CODE_YES && key == KEY_F(4))) {
        emoji_picker_close(p);
        return true;
    }
    if (type == KEY_CODE_YES && key == KEY_F(5)) {
        chat_assets_emojis_request(p->assets, true);
        return true;
    }
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
        p->focus = (p->focus + (type == KEY_CODE_YES ? 2 : 1)) % 3;
        return true;
    }
    if (p->focus == 1 && type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
        p->server = key == KEY_RIGHT;
        p->selected_key[0] = 0;
        p->first = 0;
        update_matches(p);
        return true;
    }
    if ((type == OK && (key == '\n' || key == '\r')) ||
        (type == KEY_CODE_YES && key == KEY_ENTER)) {
        if (p->focus != 1)
            *inserted = choose(p, editor);
        else
            p->focus = 2;
        return true;
    }
    if (type == KEY_CODE_YES &&
        (key == KEY_UP || key == KEY_DOWN || key == KEY_PPAGE || key == KEY_NPAGE ||
         ((key == KEY_LEFT || key == KEY_RIGHT || key == KEY_HOME || key == KEY_END) &&
          p->focus == 2))) {
        p->focus = 2;
        int columns = p->columns > 0 ? p->columns : 1;
        if (key == KEY_HOME)
            p->selected = 0;
        else if (key == KEY_END && p->count)
            p->selected = p->count - 1;
        else
            move_selection(p, key == KEY_LEFT    ? -1
                              : key == KEY_RIGHT ? 1
                              : key == KEY_UP    ? -columns
                              : key == KEY_DOWN  ? columns
                                                 : (key == KEY_PPAGE ? -1 : 1) * columns *
                                                       (p->rows > 0 ? p->rows : 1));
        remember(p);
        return true;
    }
    if (type == OK && key >= 32)
        p->focus = 0;
    if (p->focus == 0) {
        if (text_edit(&p->search, type, key, p->width > 12 ? p->width - 12 : 20, false)) {
            p->error[0] = p->selected_key[0] = 0;
            p->first = 0;
            update_matches(p);
        }
    }
    return true;
}
bool emoji_picker_mouse(emoji_picker *p, text_buffer *editor, const MEVENT *event, bool *inserted)
{
    *inserted = false;
    if (!emoji_picker_open(p))
        return false;
    int y = event->y - p->top, x = event->x - p->left;
    bool click = event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED);
    if (x < 0 || y < 0 || x >= p->width || y >= p->height) {
        if (click)
            emoji_picker_close(p);
        return true;
    }
    if (event->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
        p->focus = 2;
        move_selection(p, (event->bstate & BUTTON4_PRESSED ? -1 : 1) * p->columns);
        return true;
    }
    if (!click)
        return true;
    if (y == 1) {
        p->focus = 0;
        text_click(&p->search, p->width - 12, 0, x - 9);
    } else if (y == 2 && x >= 2 && x < 24) {
        p->server = x >= 13;
        p->focus = 1;
        p->selected_key[0] = 0;
        p->first = 0;
        update_matches(p);
    } else if (y >= 4 && y < 4 + p->rows * 2 && x >= 2 && x < 2 + p->columns * 6) {
        size_t index = p->first + (size_t)((y - 4) / 2 * p->columns + (x - 2) / 6);
        if (index < p->count) {
            p->selected = index;
            remember(p);
            *inserted = choose(p, editor);
        }
    }
    return true;
}
void emoji_picker_draw(emoji_picker *p)
{
    if (!emoji_picker_open(p) || COLS < 30 || LINES < 12)
        return;
    int width = COLS - 8;
    if (width > 68)
        width = 68;
    int height = LINES - 4;
    if (height > 24)
        height = 24;
    int left = (COLS - width) / 2, top = (LINES - height) / 2;
    if (p->window &&
        (width != p->width || height != p->height || left != p->left || top != p->top)) {
        delwin(p->window);
        p->window = NULL;
    }
    p->width = width;
    p->height = height;
    p->left = left;
    p->top = top;
    p->columns = (width - 4) / 6;
    p->rows = (height - 7) / 2;
    if (p->rows < 1)
        p->rows = 1;
    size_t page = (size_t)(p->columns * p->rows);
    if (p->selected < p->first)
        p->first = p->selected / (size_t)p->columns * (size_t)p->columns;
    if (p->selected >= p->first + page)
        p->first = (p->selected / (size_t)p->columns - (size_t)p->rows + 1) * (size_t)p->columns;
    if (!p->window)
        p->window = newwin(height, width, top, left);
    if (!p->window)
        return;
    WINDOW *w = p->window;
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 0, 2, width - 4, " Emojis · Esc voltar ");
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, 1, 2, 7, "Buscar:");
    wattrset(w, COLOR_PAIR(p->focus == 0 ? THEME_FOCUS_AREA : THEME_PANEL));
    mvwhline(w, 1, 9, ' ', width - 12);
    text_layout search;
    text_measure(&p->search, width - 12, &search);
    text_draw(w, &p->search, &search, search.cursor_row, 1, 1, 9);
    wattrset(w, COLOR_PAIR(!p->server ? THEME_PANEL_ACCENT : THEME_PANEL_MUTED) |
                    (p->focus == 1 ? A_BOLD : 0));
    text_label(w, 2, 2, 10, "[Unicode]");
    wattrset(w, COLOR_PAIR(p->server ? THEME_PANEL_ACCENT : THEME_PANEL_MUTED) |
                    (p->focus == 1 ? A_BOLD : 0));
    text_label(w, 2, 13, 11, "[Servidor]");
    for (size_t i = p->first; i < p->count && i < p->first + page; ++i) {
        int y = 4 + (int)((i - p->first) / (size_t)p->columns) * 2;
        int x = 2 + (int)((i - p->first) % (size_t)p->columns) * 6;
        int pair = i == p->selected ? THEME_FOCUS_AREA : THEME_PANEL;
        wattrset(w, COLOR_PAIR(pair));
        mvwhline(w, y, x, ' ', 5);
        icons_scope(p->icons, "emoji-picker", 0);
        if (!icons_paint(p->icons, w, p->matches[i], y, x + 1, pair, false, p->matches[i])) {
            if (p->matches[i] & ICON_CUSTOM)
                text_label(w, y, x, 5, item_name(p, p->matches[i]));
            else
                text_label(w, y, x + 1, 2, item_key(p, p->matches[i]));
        }
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (!p->count)
        text_label(w, 4, 2, width - 4, "Nenhum emoji encontrado");
    text_label(w, height - 3, 2, width - 4,
               *p->error                ? p->error
               : p->selected < p->count ? item_name(p, p->matches[p->selected])
                                        : "");
    text_label(w, height - 2, 2, width - 4,
               p->server ? chat_assets_notice(p->assets)
                         : "↑↓←→ escolher · Enter inserir · F5 atualizar");
    wnoutrefresh(w);
    if (p->focus == 0) {
        curs_set(1);
        setsyx(top + 1, left + 9 + search.cursor_column);
    } else
        curs_set(0);
}

bool emoji_picker_cursor(const emoji_picker *p)
{
    return p && p->open && p->focus == 0;
}
