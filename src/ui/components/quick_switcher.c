#include "ui/components/quick_switcher.h"
#include "model/text/translation.h"
#include "ui/components/theme.h"
#include "ui/input/mouse.h"
#include "ui/input/shortcuts.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#define SEARCH_ROW 2
#define LIST_ROW 4

static int priority(const switcher_entry *entry)
{
    return entry->mentions ? 0 : entry->unread ? 1 : 2;
}

static int compare(const void *a, const void *b)
{
    const switcher_entry *left = a, *right = b;
    int order = priority(left) - priority(right);
    if (order)
        return order;
    /* ISO 8601 times from the server sort as text; newer first. */
    order = strcmp(right->last, left->last);
    if (order)
        return order;
    order = g_utf8_collate(left->name, right->name);
    return order ? order : (left->instance > right->instance) - (left->instance < right->instance);
}

/* Lowercase ASCII transliteration, so "geral" finds "Géral" and "sao" "São". */
static char *fold(const char *text)
{
    char *ascii = g_str_to_ascii(text ? text : "", NULL);
    char *folded = ascii ? g_ascii_strdown(ascii, -1) : NULL;
    g_free(ascii);
    return folded;
}

static void filter_entries(quick_switcher *s)
{
    char query[TEXT_UTF8_BYTES];
    if (!text_encode(&s->search, query))
        query[0] = 0;
    char *needle = fold(g_strstrip(query));
    s->visible_count = 0;
    for (size_t i = 0; i < s->count; ++i) {
        bool match = !needle || !*needle;
        if (!match) {
            char *name = fold(s->entries[i].name), *server = fold(s->entries[i].server);
            match = (name && strstr(name, needle)) || (server && strstr(server, needle));
            g_free(name);
            g_free(server);
        }
        if (match)
            s->visible[s->visible_count++] = i;
    }
    g_free(needle);
    s->selected = s->first = 0;
}

bool quick_switcher_open(quick_switcher *s, switcher_entry *entries, size_t count)
{
    quick_switcher_close(s);
    size_t *visible = calloc(count ? count : 1, sizeof(*visible));
    if (!visible) {
        free(entries);
        return false;
    }
    if (count)
        qsort(entries, count, sizeof(*entries), compare);
    s->entries = entries;
    s->count = count;
    s->visible = visible;
    text_clear(&s->search);
    s->search_first = 0;
    s->chosen = false;
    s->open = true;
    filter_entries(s);
    return true;
}

void quick_switcher_close(quick_switcher *s)
{
    if (s->window)
        delwin(s->window);
    s->window = NULL;
    s->open = false;
    /* A chosen entry stays readable until taken. */
    if (!s->chosen) {
        free(s->entries);
        s->entries = NULL;
        s->count = 0;
    }
    free(s->visible);
    s->visible = NULL;
    s->visible_count = 0;
}

void quick_switcher_destroy(quick_switcher *s)
{
    s->chosen = false;
    quick_switcher_close(s);
    *s = (quick_switcher){0};
}

bool quick_switcher_take(quick_switcher *s, switcher_entry *entry)
{
    if (!s->chosen)
        return false;
    s->chosen = false;
    bool found = s->entries && s->selected < s->count;
    if (found)
        *entry = s->entries[s->selected];
    free(s->entries);
    s->entries = NULL;
    s->count = 0;
    return found;
}

static void choose(quick_switcher *s, size_t visible_index)
{
    if (visible_index >= s->visible_count)
        return;
    /* selected now names the entry itself, which outlives the visible list. */
    s->selected = s->visible[visible_index];
    s->chosen = true;
    quick_switcher_close(s);
}

static void move_selection(quick_switcher *s, long delta)
{
    if (!s->visible_count)
        return;
    long next = (long)s->selected + delta;
    if (next < 0)
        next = 0;
    if (next >= (long)s->visible_count)
        next = (long)s->visible_count - 1;
    s->selected = (size_t)next;
}

void quick_switcher_paste(quick_switcher *s, text_paste *paste)
{
    if (!s->open) {
        text_paste_cancel(paste);
        return;
    }
    for (size_t i = 0; i < paste->text.length; ++i)
        if (paste->text.data[i] == '\n' || paste->text.data[i] == '\r')
            paste->text.data[i] = ' ';
    if (text_paste_commit(paste, &s->search))
        filter_entries(s);
}

bool quick_switcher_key(quick_switcher *s, int type, uint32_t key)
{
    if (!s->open)
        return false;
    if ((type == OK && key == 27) || shortcut_is(type, key, SHORTCUT_SWITCHER)) {
        quick_switcher_close(s);
        return true;
    }
    if ((type == OK && (key == '\r' || key == '\n')) ||
        (type == KEY_CODE_YES && key == KEY_ENTER)) {
        choose(s, s->selected);
        return true;
    }
    int page = s->rows > 1 ? s->rows - 1 : 1;
    if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
        move_selection(s, key == KEY_UP ? -1 : 1);
        return true;
    }
    if (type == KEY_CODE_YES && (key == KEY_PPAGE || key == KEY_NPAGE)) {
        move_selection(s, key == KEY_PPAGE ? -page : page);
        return true;
    }
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
        move_selection(s, type == OK ? 1 : -1);
        return true;
    }
    if (type == KEY_CODE_YES && key == KEY_MOUSE) {
        MEVENT event;
        if (ui_mouse_get(&event) != OK || !s->window)
            return true;
        int y = event.y, x = event.x;
        if (!wmouse_trafo(s->window, &y, &x, false)) {
            if (event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED))
                quick_switcher_close(s);
        } else if (event.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED))
            move_selection(s, event.bstate & BUTTON4_PRESSED ? -3 : 3);
        else if ((event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED)) && y >= LIST_ROW &&
                 y < LIST_ROW + s->rows && s->first + (size_t)(y - LIST_ROW) < s->visible_count)
            choose(s, s->first + (size_t)(y - LIST_ROW));
        else if ((event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED)) && y == SEARCH_ROW &&
                 x >= 10)
            text_click(&s->search, TEXT_LIMIT * 2 + 1, 0, s->search_first + x - 10);
        return true;
    }
    text_buffer before = s->search;
    int width = s->window ? getmaxx(s->window) - 12 : 24;
    if (text_edit(&s->search, type, key, width, false) &&
        (before.length != s->search.length ||
         memcmp(before.data, s->search.data, s->search.length * sizeof(*s->search.data))))
        filter_entries(s);
    return true; /* Never route switcher input to the editor underneath. */
}

static int badge(WINDOW *w, int y, int right, const switcher_entry *entry)
{
    char label[32] = "";
    if (entry->mentions)
        snprintf(label, sizeof(label), "@%zu", entry->mentions);
    else if (entry->unread)
        snprintf(label, sizeof(label), "•"); /* The list's unread dot. */
    if (!*label)
        return 0;
    int width = text_label(NULL, 0, 0, 16, label);
    wattrset(w, COLOR_PAIR(THEME_NOTIFICATION) | A_BOLD);
    text_label(w, y, right - width, width, label);
    return width + 1;
}

WINDOW *quick_switcher_draw(quick_switcher *s)
{
    if (!s->open || COLS < 30 || LINES < 10)
        return NULL;
    int width = COLS - 4 < 76 ? COLS - 4 : 76;
    int height = LINES - 4 < 22 ? LINES - 4 : 22;
    if (height < 9)
        height = 9;
    int top = (LINES - height) / 3, left = (COLS - width) / 2;
    if (s->window && (getmaxx(s->window) != width || getmaxy(s->window) != height ||
                      getbegy(s->window) != top || getbegx(s->window) != left)) {
        delwin(s->window);
        s->window = NULL;
    }
    if (!s->window)
        s->window = newwin(height, width, top, left);
    if (!s->window)
        return NULL;
    WINDOW *w = s->window;
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 1, 2, width - 4, _("Ir para conversa"));
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, SEARCH_ROW, 2, 8, _("Buscar:"));
    wattrset(w, COLOR_PAIR(THEME_FOCUSED));
    mvwhline(w, SEARCH_ROW, 10, ' ', width - 12);
    text_draw_field(w, &s->search, &s->search_first, SEARCH_ROW, 10, width - 12, false);
    int cursor = getcurx(w);
    if (!s->search.length) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, SEARCH_ROW, 10, width - 12, _("nome do canal, DM ou servidor"));
    }
    mvwchgat(w, SEARCH_ROW, cursor, 1, A_REVERSE, THEME_FOCUSED, NULL);
    wattrset(w, COLOR_PAIR(THEME_BORDER));
    mvwhline(w, LIST_ROW - 1, 1, ACS_HLINE, width - 2);
    s->rows = height - LIST_ROW - 3;
    if (s->rows < 1)
        s->rows = 1;
    if (s->selected < s->first)
        s->first = s->selected;
    if (s->selected >= s->first + (size_t)s->rows)
        s->first = s->selected - (size_t)s->rows + 1;
    for (int row = 0; row < s->rows && s->first + (size_t)row < s->visible_count; ++row) {
        size_t index = s->first + (size_t)row;
        const switcher_entry *entry = &s->entries[s->visible[index]];
        bool selected = index == s->selected;
        int y = LIST_ROW + row;
        int pair = selected ? THEME_FOCUSED : THEME_PANEL;
        wattrset(w, COLOR_PAIR(pair) | (selected && !has_colors() ? A_REVERSE : 0));
        mvwhline(w, y, 1, ' ', width - 2);
        int right = width - 2;
        right -= badge(w, y, right, entry);
        /* The server name takes at most a third of the row, right-aligned. */
        int server_width = text_label(NULL, 0, 0, (width - 4) / 3, entry->server);
        wattrset(w, COLOR_PAIR(selected ? THEME_FOCUSED : THEME_PANEL_MUTED));
        text_label(w, y, right - server_width, server_width, entry->server);
        right -= server_width + 1;
        char label[300];
        snprintf(label, sizeof(label), "%s%s%s",
                 entry->dm      ? "@ "
                 : entry->voice ? "♪ "
                                : "# ",
                 entry->name, entry->muted ? _(" (silenciado)") : "");
        wattrset(w, COLOR_PAIR(pair) | (entry->unread || entry->mentions ? A_BOLD : 0) |
                        (selected && !has_colors() ? A_REVERSE : 0));
        text_label(w, y, 2, right - 2, label);
    }
    if (!s->visible_count) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, LIST_ROW, 2, width - 4,
                   s->count ? _("Nenhuma conversa encontrada.") : _("Nenhuma conversa carregada."));
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, height - 2, 2, width - 4, _("Enter: abrir · ↑↓: escolher · Esc: fechar"));
    wnoutrefresh(w);
    return w;
}
