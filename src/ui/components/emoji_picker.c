#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/components/emoji_picker.h"
#include "model/text/emoji_catalog.h"
#include "model/text/emoji_usage.h"
#include "ui/components/theme.h"
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    uint32_t id;
    uint64_t uses;
    size_t order;
    int quality;
} emoji_match;
struct emoji_picker {
    chat_assets *assets;
    icon_view *icons;
    WINDOW *window;
    text_buffer search;
    emoji_match *matches;
    emoji_usage *usage;
    char **unicode_search;
    size_t count, selected, first;
    uint64_t revision, generation;
    bool open, server, suggesting, dismissed, reaction;
    json_t *reaction_choice;
    size_t start, end;
    int focus, columns, rows, left, top, width, height, tile_width, tile_height, image_rows;
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
        out += g_unichar_to_utf8(c == '_' || c == '-' ? ' ' : c, out);
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
             p->selected < p->count ? item_key(p, p->matches[p->selected].id) : "");
}
static void usage_key(emoji_picker *p, uint32_t id, char key[164])
{
    snprintf(key, 164, "%c:%s", id & ICON_CUSTOM ? 'c' : 'u', item_key(p, id));
}
static int compare_matches(const void *a, const void *b)
{
    const emoji_match *x = a, *y = b;
    if (x->uses != y->uses)
        return x->uses > y->uses ? -1 : 1;
    if (x->quality != y->quality)
        return x->quality < y->quality ? -1 : 1;
    return x->order < y->order ? -1 : x->order != y->order;
}
static bool prepare_search(emoji_picker *p)
{
    if (p->unicode_search)
        return true;
    size_t count = emoji_catalog_count();
    char **index = calloc(count, sizeof(*index));
    if (!index)
        return false;
    for (size_t i = 0; i < count; ++i) {
        const unicode_emoji *e = emoji_catalog_get(i);
        char *words = g_strdup_printf("%s %s %s %s", e->name, e->english, e->keywords, e->text);
        index[i] = words ? fold(words) : NULL;
        g_free(words);
        if (!index[i]) {
            for (size_t j = 0; j < i; ++j)
                g_free(index[j]);
            free(index);
            return false;
        }
    }
    p->unicode_search = index;
    return true;
}
static void update_matches(emoji_picker *p)
{
    if ((p->suggesting || !p->server) && !prepare_search(p)) {
        p->count = 0;
        snprintf(p->error, sizeof(p->error), "Não foi possível carregar a busca de emojis.");
        return;
    }
    char query[TEXT_UTF8_BYTES];
    text_encode(&p->search, query);
    char *normalized = fold(query);
    p->count = 0;
    size_t unicode_count = emoji_catalog_count(), custom_count = chat_assets_emoji_count(p->assets);
    size_t count = p->suggesting ? unicode_count + custom_count
                   : p->server   ? custom_count
                                 : unicode_count;
    for (size_t i = 0; i < count; ++i) {
        bool custom = p->suggesting ? i >= unicode_count : p->server;
        size_t index = custom ? (p->suggesting ? i - unicode_count : i) : emoji_catalog_order(i);
        uint32_t id = (uint32_t)(index + 1) | (custom ? ICON_CUSTOM : 0);
        const unicode_emoji *e = !custom ? emoji_catalog_get(index) : NULL;
        char *candidate = e ? p->unicode_search[index] : fold(item_name(p, id));
        if (candidate && normalized && strstr(candidate, normalized)) {
            char key[164];
            usage_key(p, id, key);
            char *name = *normalized ? fold(item_name(p, id)) : NULL;
            char *english = *normalized && e ? fold(e->english) : NULL;
            int quality = 3;
            if (!*normalized || (name && !strcmp(name, normalized)) ||
                (english && !strcmp(english, normalized)))
                quality = 0;
            else if ((name && g_str_has_prefix(name, normalized)) ||
                     (english && g_str_has_prefix(english, normalized)))
                quality = 1;
            else if ((name && strstr(name, normalized)) || (english && strstr(english, normalized)))
                quality = 2;
            g_free(name);
            g_free(english);
            p->matches[p->count++] =
                (emoji_match){id, emoji_usage_count(p->usage, key), i, quality};
        }
        if (!e)
            g_free(candidate);
    }
    g_free(normalized);
    qsort(p->matches, p->count, sizeof(*p->matches), compare_matches);
    p->selected = 0;
    for (size_t i = 0; i < p->count; ++i)
        if (!strcmp(item_key(p, p->matches[i].id), p->selected_key)) {
            p->selected = i;
            break;
        }
    if (p->first > p->selected)
        p->first = p->selected;
    p->revision = chat_assets_emoji_revision(p->assets);
    remember(p);
}
emoji_picker *emoji_picker_create(chat_assets *assets, icon_view *icons, const char *instance,
                                  const char *user)
{
    emoji_picker *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->matches = calloc(emoji_catalog_count() + 500, sizeof(*p->matches));
    if (!p->matches) {
        free(p);
        return NULL;
    }
    p->assets = assets;
    p->icons = icons;
    p->usage = emoji_usage_create(instance, user);
    p->start = SIZE_MAX;
    return p;
}
void emoji_picker_destroy(emoji_picker *p)
{
    if (p) {
        if (p->window)
            delwin(p->window);
        if (p->unicode_search)
            for (size_t i = 0; i < emoji_catalog_count(); ++i)
                g_free(p->unicode_search[i]);
        free(p->unicode_search);
        free(p->matches);
        json_decref(p->reaction_choice);
        emoji_usage_destroy(p->usage);
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
    p->open = p->suggesting = p->reaction = false;
    if (p->window) {
        delwin(p->window);
        p->window = NULL;
    }
}
void emoji_picker_show(emoji_picker *p, const char *channel, uint64_t generation)
{
    if (!p)
        return;
    emoji_picker_close(p);
    p->open = true;
    p->reaction = false;
    json_decref(p->reaction_choice);
    p->reaction_choice = NULL;
    p->focus = 0;
    text_clear(&p->search);
    p->selected_key[0] = 0;
    p->selected = p->first = 0;
    p->error[0] = 0;
    snprintf(p->channel, sizeof(p->channel), "%s", channel);
    p->generation = generation;
    chat_assets_emojis_request(p->assets, false);
    update_matches(p);
}
void emoji_picker_reaction(emoji_picker *p, const char *channel, uint64_t generation)
{
    emoji_picker_show(p, channel, generation);
    if (p)
        p->reaction = true;
}
json_t *emoji_picker_take_reaction(emoji_picker *p)
{
    json_t *choice = p ? p->reaction_choice : NULL;
    if (p)
        p->reaction_choice = NULL;
    return choice;
}

bool emoji_picker_update(emoji_picker *p, chat_assets *assets, const char *channel,
                         uint64_t generation)
{
    if (!p)
        return false;
    bool open = p->open || p->suggesting;
    if (p->assets != assets || strcmp(p->channel, channel) || p->generation != generation) {
        emoji_picker_close(p);
        p->dismissed = false;
        p->start = SIZE_MAX;
        snprintf(p->channel, sizeof(p->channel), "%s", channel);
        p->generation = generation;
    }
    p->assets = assets;
    if ((p->open || p->suggesting) && p->revision != chat_assets_emoji_revision(assets)) {
        update_matches(p);
        return true;
    }
    return open != (p->open || p->suggesting);
}
bool emoji_picker_suggesting(const emoji_picker *p)
{
    return p && p->suggesting;
}
void emoji_picker_dismiss(emoji_picker *p)
{
    if (p) {
        emoji_picker_close(p);
        p->dismissed = true;
    }
}
static bool word(uint32_t c)
{
    return g_unichar_isalnum(c) || c == '_' || c == '-' || c == '+' || c == '.';
}
bool emoji_picker_suggest(emoji_picker *p, text_buffer *b, bool enabled)
{
    if (!p || p->open)
        return false;
    bool was = p->suggesting;
    if (!b || !enabled || text_selection(b, NULL, NULL)) {
        emoji_picker_close(p);
        return was;
    }
    size_t at = b->cursor;
    while (at && word(b->data[at - 1]))
        --at;
    if (!at || b->data[at - 1] != ':' ||
        (at > 1 && (word(b->data[at - 2]) || b->data[at - 2] == ':' || b->data[at - 2] == '/' ||
                    b->data[at - 2] == '\\')) ||
        icons_literal(b->data, at - 1)) {
        emoji_picker_close(p);
        p->dismissed = false;
        p->start = SIZE_MAX;
        return was;
    }
    text_buffer query = {0};
    query.length = query.cursor = b->cursor - at;
    memcpy(query.data, b->data + at, query.length * sizeof(*query.data));
    bool changed = p->start != at - 1 || query.length != p->search.length ||
                   memcmp(query.data, p->search.data, query.length * sizeof(*query.data));
    if (changed) {
        p->dismissed = false;
        p->selected = p->first = 0;
        p->selected_key[0] = p->error[0] = 0;
    }
    p->search = query;
    p->start = at - 1;
    p->end = b->cursor;
    while (p->end < b->length && word(b->data[p->end]))
        ++p->end;
    if (p->end < b->length && b->data[p->end] == ':')
        ++p->end;
    if (p->dismissed)
        return was;
    p->suggesting = true;
    chat_assets_emojis_request(p->assets, false);
    if (changed || !was)
        update_matches(p);
    return changed || !was;
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
    if (p->selected >= p->count)
        return false;
    uint32_t id = p->matches[p->selected].id;
    if (p->reaction) {
        json_t *choice =
            json_pack("{s:s}", id & ICON_CUSTOM ? "emoji_id" : "unicode", item_key(p, id));
        if (!choice)
            return false;
        json_decref(p->reaction_choice);
        p->reaction_choice = choice;
        emoji_picker_close(p);
        return true;
    }
    if (!editor)
        return false;
    char value[256];
    if (id & ICON_CUSTOM)
        snprintf(value, sizeof(value), ":%s:", item_name(p, id));
    else
        snprintf(value, sizeof(value), "%s", item_key(p, id));
    text_buffer insertion;
    text_buffer next = *editor;
    if (p->suggesting) {
        if (p->start > p->end || p->end > next.length)
            return false;
        next.anchor = p->start;
        next.cursor = p->end;
        next.selecting = true;
    }
    if (!text_decode(&insertion, value) || !text_insert_buffer(&next, &insertion)) {
        snprintf(p->error, sizeof(p->error), "O emoji excede o limite do editor.");
        return false;
    }
    *editor = next;
    char key[164];
    usage_key(p, id, key);
    emoji_usage_record(p->usage, key);
    emoji_picker_close(p);
    return true;
}
bool emoji_picker_key(emoji_picker *p, text_buffer *editor, int type, uint32_t key, bool *inserted)
{
    *inserted = false;
    if (emoji_picker_suggesting(p)) {
        if (type == OK && key == 27) {
            emoji_picker_dismiss(p);
            return true;
        }
        if ((type == OK && (key == '\t' || key == '\n' || key == '\r')) ||
            (type == KEY_CODE_YES && key == KEY_ENTER)) {
            *inserted = choose(p, editor);
            return true;
        }
        if (type == KEY_CODE_YES &&
            (key == KEY_UP || key == KEY_DOWN || key == KEY_PPAGE || key == KEY_NPAGE)) {
            move_selection(p, key == KEY_UP      ? -1
                              : key == KEY_DOWN  ? 1
                              : key == KEY_PPAGE ? -p->rows
                                                 : p->rows);
            return true;
        }
        return false;
    }
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
    if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
        if (p->focus == 0 || p->focus == 1) {
            p->focus = key == KEY_DOWN ? p->focus + 1 : 0;
            return true;
        }
        if (key == KEY_UP && p->selected < (size_t)p->columns) {
            p->focus = 1;
            return true;
        }
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
    if (emoji_picker_suggesting(p)) {
        int y = event->y - p->top, x = event->x - p->left;
        bool click = event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED);
        if (x < 0 || y < 0 || x >= p->width || y >= p->height) {
            if (click)
                emoji_picker_dismiss(p);
            return false;
        }
        if (event->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED))
            move_selection(p, event->bstate & BUTTON4_PRESSED ? -3 : 3);
        else if (click && y >= 1 && y < p->height - 2) {
            size_t index = p->first + (size_t)((y - 1) / p->tile_height);
            if (index < p->count) {
                p->selected = index;
                *inserted = choose(p, editor);
            }
        }
        return true;
    }
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
    } else if (y >= 4 && y < 4 + p->rows * p->tile_height && x >= 2 &&
               x < 2 + p->columns * p->tile_width) {
        size_t index =
            p->first + (size_t)((y - 4) / p->tile_height * p->columns + (x - 2) / p->tile_width);
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
    graphics_terminal terminal = graphics_terminal_get();
    int cell_width = terminal.cell_width > 0 ? terminal.cell_width : 8;
    int cell_height = terminal.cell_height > 0 ? terminal.cell_height : 16;
    int image_columns = (32 + cell_width - 1) / cell_width;
    p->image_rows = (32 + cell_height - 1) / cell_height;
    p->tile_width = image_columns + 2 > 12 ? image_columns + 2 : 12;
    p->tile_height = p->image_rows + 2;
    p->columns = (width - 4) / p->tile_width;
    if (p->columns < 1)
        p->columns = 1;
    p->tile_width = (width - 4) / p->columns;
    p->rows = (height - 7) / p->tile_height;
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
    icons_overlay(p->icons, top, left, height, width);
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 0, 2, width - 4, p->reaction ? " Reagir · Esc voltar " : " Emojis · Esc voltar ");
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
        int y = 4 + (int)((i - p->first) / (size_t)p->columns) * p->tile_height;
        int x = 2 + (int)((i - p->first) % (size_t)p->columns) * p->tile_width;
        int image_x = x + (p->tile_width - image_columns) / 2;
        uint32_t id = p->matches[i].id;
        int pair = i == p->selected ? THEME_FOCUS_AREA : THEME_PANEL;
        wattrset(w, COLOR_PAIR(pair));
        for (int row = 0; row <= p->image_rows; ++row)
            mvwhline(w, y + row, x, ' ', p->tile_width - 1);
        icons_scope(p->icons, "emoji-picker", 0);
        if (!icons_paint_size(p->icons, w, id, y, image_x, pair, id, 32)) {
            if (id & ICON_CUSTOM)
                text_label(w, y, x, p->tile_width - 1, ":emoji:");
            else
                text_label(w, y, image_x, 2, item_key(p, id));
        }
        text_label(w, y + p->image_rows, x, p->tile_width - 1, item_name(p, id));
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (!p->count)
        text_label(w, 4, 2, width - 4, "Nenhum emoji encontrado");
    text_label(w, height - 3, 2, width - 4,
               *p->error                ? p->error
               : p->selected < p->count ? item_name(p, p->matches[p->selected].id)
                                        : "");
    text_label(w, height - 2, 2, width - 4,
               p->server     ? chat_assets_notice(p->assets)
               : p->reaction ? "↑↓←→ escolher · Enter reagir · F5 atualizar"
                             : "↑↓←→ escolher · Enter inserir · F5 atualizar");
    wnoutrefresh(w);
    if (p->focus == 0) {
        curs_set(1);
        setsyx(top + 1, left + 9 + search.cursor_column);
    } else
        curs_set(0);
}

void emoji_picker_draw_suggestions(emoji_picker *p, int cy, int cx, int left, int available)
{
    if (!emoji_picker_suggesting(p) || available < 16 || LINES < 10)
        return;
    graphics_terminal terminal = graphics_terminal_get();
    int cw = terminal.cell_width > 0 ? terminal.cell_width : 8;
    int ch = terminal.cell_height > 0 ? terminal.cell_height : 16;
    int image_columns = (32 + cw - 1) / cw;
    p->tile_height = (32 + ch - 1) / ch;
    if (p->tile_height < 2)
        p->tile_height = 2;
    p->columns = 1;
    p->rows = (LINES - 5) / p->tile_height;
    if (p->rows > 6)
        p->rows = 6;
    if (p->count && p->rows > (int)p->count)
        p->rows = (int)p->count;
    if (p->rows < 1)
        p->rows = 1;
    p->height = p->rows * p->tile_height + 3;
    p->width = available < 48 ? available : 48;
    p->left = cx + p->width > left + available ? left + available - p->width : cx;
    if (p->left < left)
        p->left = left;
    p->top = cy - p->height;
    if (p->top < 1)
        p->top = cy + 1;
    if (p->top + p->height > LINES - 1)
        p->top = LINES - p->height - 1;
    if (p->selected < p->first)
        p->first = p->selected;
    if (p->selected >= p->first + (size_t)p->rows)
        p->first = p->selected - (size_t)p->rows + 1;
    if (p->window)
        delwin(p->window);
    WINDOW *w = p->window = newwin(p->height, p->width, p->top, p->left);
    if (!w)
        return;
    icons_overlay(p->icons, p->top, p->left, p->height, p->width);
    leaveok(w, true);
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 0, 2, p->width - 4, " Emojis · mais usados ");
    icons_scope(p->icons, "emoji-popup", 0);
    for (int row = 0; row < p->rows; ++row) {
        size_t i = p->first + (size_t)row;
        if (i >= p->count)
            break;
        uint32_t id = p->matches[i].id;
        int pair = i == p->selected ? THEME_FOCUS_AREA : THEME_PANEL;
        int y = 1 + row * p->tile_height;
        wattrset(w, COLOR_PAIR(pair));
        for (int line = 0; line < p->tile_height; ++line)
            mvwhline(w, y + line, 1, ' ', p->width - 2);
        if (!icons_paint_size(p->icons, w, id, y, 2, pair, id, 32) && !(id & ICON_CUSTOM))
            text_label(w, y, 2, 2, item_key(p, id));
        int x = image_columns + 3;
        text_label(w, y, x, p->width - x - 2, item_name(p, id));
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, y + 1, x, p->width - x - 2, id & ICON_CUSTOM ? "Servidor" : "Unicode");
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (!p->count)
        text_label(w, 1, 2, p->width - 4, "Nenhum emoji encontrado");
    text_label(w, p->height - 2, 1, p->width - 2,
               *p->error ? p->error : "↑↓ escolher · Tab/Enter inserir · Esc fechar");
    wnoutrefresh(w);
    setsyx(cy, cx);
}

bool emoji_picker_cursor(const emoji_picker *p)
{
    return p && p->open && p->focus == 0;
}
