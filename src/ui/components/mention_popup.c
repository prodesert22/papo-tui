#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/components/mention_popup.h"
#include "ui/components/theme.h"
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wctype.h>
typedef struct {
    size_t index, recent;
    const char *name;
} candidate;
struct mention_popup {
    history *h;
    WINDOW *window;
    bool open, dismissed;
    size_t start, end, selected, first, count;
    uint64_t users_revision, messages_revision;
    json_t *recent; /* Owned author ranks in the loaded active history. */
    char query[TEXT_UTF8_BYTES], selected_id[37], channel_id[37], error[128];
    candidate matches[10000];
};
static const char *str(json_t *u, const char *key)
{
    const char *s = json_string_value(json_object_get(u, key));
    return s ? s : "";
}
static bool contains_folded(const text_buffer *name, const text_buffer *query)
{
    for (size_t i = 0; i + query->length <= name->length; ++i) {
        size_t j = 0;
        while (j < query->length && towlower(name->data[i + j]) == towlower(query->data[j]))
            ++j;
        if (j == query->length)
            return true;
    }
    return false;
}
static void remove_separators(text_buffer *text)
{
    size_t length = 0;
    for (size_t i = 0; i < text->length; ++i)
        if (iswalnum(text->data[i]))
            text->data[length++] = text->data[i];
    text->length = length;
    text->data[length] = 0;
}
bool mention_name_matches(const char *name, const char *query)
{
    text_buffer a, b;
    if (!text_decode(&a, name) || !text_decode(&b, query))
        return false;
    if (contains_folded(&a, &b))
        return true;
    /* Normalize only the comparison; names and wire identities stay intact. */
    remove_separators(&a);
    remove_separators(&b);
    return b.length && contains_folded(&a, &b);
}
static int compare(const void *a, const void *b)
{
    const candidate *x = a, *y = b;
    if (x->recent != y->recent)
        return x->recent > y->recent ? -1 : 1;
    int n = strcasecmp(x->name, y->name);
    return n ? n : x->index < y->index ? -1 : x->index != y->index;
}
static bool update_recent(mention_popup *p)
{
    json_t *next = json_object();
    if (!next)
        return false;
    /* The model is chronologically ordered, including exact timestamp/UUID
     * ties. A later message replaces an author's previous rank. */
    for (size_t i = 0; i < history_message_count(p->h); ++i) {
        const char *id = str(history_message(p->h, i), "author_id");
        if (!id || !*id)
            continue;
        if (json_object_set_new(next, id, json_integer((json_int_t)i + 1))) {
            json_decref(next);
            return false;
        }
    }
    json_decref(p->recent);
    p->recent = next;
    return true;
}
static void hide(mention_popup *p)
{
    if (p->window) {
        delwin(p->window);
        p->window = NULL;
    }
    p->open = false;
    history_users_active(p->h, false);
}
mention_popup *mention_popup_create(history *h)
{
    mention_popup *p = calloc(1, sizeof(*p));
    if (p)
        p->h = h;
    return p;
}
void mention_popup_destroy(mention_popup *p)
{
    if (p) {
        hide(p);
        json_decref(p->recent);
        free(p);
    }
}
bool mention_popup_open(const mention_popup *p)
{
    return p && p->open;
}
void mention_popup_close(mention_popup *p)
{
    if (p) {
        hide(p);
        p->dismissed = true;
    }
}
static bool word(utf32_t c)
{
    return iswalnum(c) || c == '_' || c == '-' || c == '.';
}
bool mention_popup_update(mention_popup *p, text_buffer *b, bool enabled)
{
    if (!p)
        return false;
    bool was = p->open;
    size_t old_count = p->count;
    const char *channel = str(history_channel(p->h, history_selected(p->h)), "id");
    if (strcmp(channel, p->channel_id)) {
        hide(p);
        p->dismissed = false;
        p->query[0] = p->selected_id[0] = p->error[0] = 0;
        p->start = SIZE_MAX;
        snprintf(p->channel_id, sizeof(p->channel_id), "%s", channel);
    }
    if (!b || !enabled || text_selection(b, NULL, NULL)) {
        hide(p);
        return was;
    }
    size_t at = b->cursor;
    while (at && word(b->data[at - 1]))
        --at;
    if (!at || b->data[at - 1] != '@') {
        hide(p);
        p->dismissed = false;
        p->query[0] = 0;
        return was;
    }
    size_t start = at - 1;
    if (start && (word(b->data[start - 1]) || b->data[start - 1] == '@' ||
                  b->data[start - 1] == '/' || b->data[start - 1] == ':')) {
        hide(p);
        return was;
    }
    text_buffer query = {0};
    query.length = b->cursor - at;
    memcpy(query.data, b->data + at, query.length * sizeof(query.data[0]));
    char q[TEXT_UTF8_BYTES];
    if (!text_encode(&query, q)) {
        hide(p);
        return was;
    }
    bool changed = strcmp(q, p->query) || p->start != start;
    if (changed) {
        p->dismissed = false;
        p->selected = p->first = 0;
        p->selected_id[0] = 0;
        p->error[0] = 0;
    }
    snprintf(p->query, sizeof(p->query), "%s", q);
    p->start = start;
    p->end = b->cursor;
    while (p->end < b->length && word(b->data[p->end]))
        ++p->end;
    if (p->dismissed) {
        hide(p);
        return was;
    }
    p->open = true;
    history_users_active(p->h, true);
    uint64_t revision = history_users_revision(p->h);
    uint64_t messages_revision = history_messages_revision(p->h);
    bool users_changed = revision != p->users_revision;
    bool messages_changed = !p->recent || messages_revision != p->messages_revision;
    if (was && !changed && !users_changed && !messages_changed)
        return false;
    if (messages_changed && !update_recent(p)) {
        snprintf(p->error, sizeof(p->error), "Não foi possível ordenar usuários.");
        return true;
    }
    p->messages_revision = messages_revision;
    p->users_revision = revision;
    p->count = 0;
    char *folded = g_utf8_casefold(q, -1), *normalized = history_search_normalize(q);
    if (!folded || !normalized) {
        g_free(folded);
        g_free(normalized);
        return true;
    }
    for (size_t i = 0; i < history_user_count(p->h) && p->count < 10000; ++i) {
        json_t *u = history_user(p->h, i);
        if (strstr(str(u, "_fold_name"), folded) || strstr(str(u, "_fold_nick"), folded) ||
            (*normalized && (strstr(str(u, "_normal_name"), normalized) ||
                             strstr(str(u, "_normal_nick"), normalized))))
            p->matches[p->count++] = (candidate){
                .index = i,
                .name = str(u, "username"),
                .recent = (size_t)json_integer_value(json_object_get(p->recent, str(u, "id")))};
    }
    g_free(folded);
    g_free(normalized);
    qsort(p->matches, p->count, sizeof(p->matches[0]), compare);
    for (size_t i = 0; i < p->count; ++i)
        if (!strcmp(str(history_user(p->h, p->matches[i].index), "id"), p->selected_id))
            p->selected = i;
    if (p->selected >= p->count)
        p->selected = p->count ? p->count - 1 : 0;
    if (p->count)
        snprintf(p->selected_id, sizeof(p->selected_id), "%s",
                 str(history_user(p->h, p->matches[p->selected].index), "id"));
    return was != p->open || changed || old_count != p->count || users_changed || messages_changed;
}
static void move_selection(mention_popup *p, int delta)
{
    if (!p->count)
        return;
    long next = (long)p->selected + delta;
    if (next < 0)
        next = 0;
    if (next >= (long)p->count)
        next = (long)p->count - 1;
    p->selected = (size_t)next;
    snprintf(p->selected_id, sizeof(p->selected_id), "%s",
             str(history_user(p->h, p->matches[p->selected].index), "id"));
}
static bool choose(mention_popup *p, text_buffer *b)
{
    if (!p->count)
        return false;
    const char *id = str(history_user(p->h, p->matches[p->selected].index), "id");
    if (!history_uuid(id) || p->end > b->length)
        return false;
    char token[64];
    snprintf(token, sizeof(token), "@mention(<@%s>) ", id);
    text_buffer insertion;
    if (!text_decode(&insertion, token) ||
        b->length - (p->end - p->start) + insertion.length > TEXT_LIMIT) {
        snprintf(p->error, sizeof(p->error), "Menção excede o limite de texto.");
        return false;
    }
    memmove(b->data + p->start + insertion.length, b->data + p->end,
            (b->length - p->end + 1) * sizeof(b->data[0]));
    memcpy(b->data + p->start, insertion.data, insertion.length * sizeof(b->data[0]));
    b->length = b->length - (p->end - p->start) + insertion.length;
    b->cursor = p->start + insertion.length;
    text_unselect(b);
    b->goal_column = -1;
    b->trailing = false;
    mention_popup_close(p);
    return true;
}
bool mention_popup_key(mention_popup *p, text_buffer *b, int type, uint32_t key, bool *accepted)
{
    *accepted = false;
    if (!p || !p->open)
        return false;
    if (type == OK && key == 27) {
        mention_popup_close(p);
        return true;
    }
    if (type == OK && key == 18) {
        history_users_retry(p->h);
        return true;
    }
    if ((type == OK && (key == '\t' || key == '\n' || key == '\r')) ||
        (type == KEY_CODE_YES && key == KEY_ENTER)) {
        *accepted = choose(p, b);
        return true;
    }
    if (type == KEY_CODE_YES &&
        (key == KEY_UP || key == KEY_DOWN || key == KEY_PPAGE || key == KEY_NPAGE)) {
        move_selection(p, key == KEY_UP ? -1 : key == KEY_DOWN ? 1 : key == KEY_PPAGE ? -8 : 8);
        return true;
    }
    return false;
}
bool mention_popup_mouse(mention_popup *p, text_buffer *b, const MEVENT *e, bool *accepted)
{
    *accepted = false;
    if (!p || !p->open)
        return false;
    int y = e->y, x = e->x;
    if (!p->window || !wmouse_trafo(p->window, &y, &x, false)) {
        if (e->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))
            mention_popup_close(p);
        return false;
    }
    if (e->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
        size_t rows = (size_t)getmaxy(p->window) - 3;
        size_t last = p->count > rows ? p->count - rows : 0;
        if (e->bstate & BUTTON4_PRESSED)
            p->first = p->first > 3 ? p->first - 3 : 0;
        else
            p->first = p->first + 3 < last ? p->first + 3 : last;
        if (p->selected < p->first)
            p->selected = p->first;
        if (p->selected >= p->first + rows)
            p->selected = p->first + rows - 1;
        move_selection(p, 0);
        return true;
    }
    if ((e->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) && y >= 1 && y < getmaxy(p->window) - 2 &&
        p->first + (size_t)y - 1 < p->count) {
        p->selected = p->first + (size_t)y - 1;
        *accepted = choose(p, b);
    }
    return true;
}
WINDOW *mention_popup_draw(mention_popup *p, int cy, int cx, int left, int available)
{
    if (!p || !p->open)
        return NULL;
    int width = available < 48 ? available : 48;
    if (width < 12)
        return NULL;
    int rows = p->count < 8 ? (int)p->count : 8;
    if (rows < 1)
        rows = 1;
    if (rows > LINES - 5)
        rows = LINES - 5;
    int height = rows + 3, x = cx, y = cy - height;
    if (x + width > left + available)
        x = left + available - width;
    if (x < left)
        x = left;
    if (y < 1)
        y = cy + 1;
    if (y + height > LINES - 1)
        y = LINES - height - 1;
    if (p->window)
        delwin(p->window);
    p->window = newwin(height, width, y, x);
    if (!p->window)
        return NULL;
    leaveok(p->window, true);
    wbkgd(p->window, COLOR_PAIR(THEME_PANEL));
    werase(p->window);
    wattrset(p->window, COLOR_PAIR(THEME_BORDER));
    box(p->window, 0, 0);
    wattrset(p->window, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(p->window, 0, 2, width - 4, " Menções ");
    if (p->selected < p->first)
        p->first = p->selected;
    if (p->selected >= p->first + (size_t)rows)
        p->first = p->selected - (size_t)rows + 1;
    if (p->first + (size_t)rows > p->count)
        p->first = p->count > (size_t)rows ? p->count - (size_t)rows : 0;
    for (int row = 0; row < rows; ++row) {
        size_t i = p->first + (size_t)row;
        wattrset(p->window, COLOR_PAIR(i == p->selected ? THEME_FOCUSED : THEME_PANEL) |
                                (i == p->selected ? A_BOLD : 0));
        if (i < p->count) {
            json_t *u = history_user(p->h, p->matches[i].index);
            char label[2200];
            snprintf(label, sizeof(label), "%s%s@%s", *str(u, "nickname") ? str(u, "nickname") : "",
                     *str(u, "nickname") ? " · " : "", str(u, "username"));
            text_label(p->window, row + 1, 1, width - 2, label);
        } else
            text_label(p->window, row + 1, 1, width - 2,
                       !strncmp(history_users_notice(p->h), "Carregando", 10)
                           ? "Carregando…"
                           : "Nenhum usuário encontrado.");
    }
    wattrset(p->window, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(p->window, height - 2, 1, width - 2,
               *p->error ? p->error : history_users_notice(p->h));
    wnoutrefresh(p->window);
    /* The floating window must not take the editor's terminal cursor. */
    setsyx(cy, cx);
    return p->window;
}
