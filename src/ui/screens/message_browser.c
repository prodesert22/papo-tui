#include "ui/screens/message_browser.h"
#include "ui/components/button.h"
#include "ui/components/mention_popup.h"
#include "ui/components/theme.h"
#include "ui/input/mouse.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

enum { RESULTS, QUERY, SUBMIT, FILTERS, MORE };
typedef struct {
    char author[37], channel[37], mention[37];
    text_buffer start, end;
    bool link, ascending;
    int attachments; /* 0 any, 1 with, 2 without. */
} search_settings;
struct message_browser {
    history *h;
    auth_client *auth;
    WINDOW *window;
    bool open, pins, detail, editing_filters, unpin_pending, submitted;
    int focus, field_first, settings_focus, settings_first;
    int date_first[2];
    size_t selected, first, detail_scroll, count, rows[HISTORY_MESSAGE_LIMIT];
    char selected_id[37], detail_id[37], unpin_id[37], destination[37], destination_message[37];
    char destination_time[65];
    char notice[192];
    text_buffer query, picker_query;
    search_settings settings, draft;
    json_t *submitted_filters; /* Owned immutable snapshot for refresh/dirty indication. */
    json_t *location_items; /* Owned reference to the list before a destination channel switch. */
    int picker, picker_focus, picker_field_first;
    size_t picker_selected, picker_first, picker_count, choices[10000];
    char picker_id[37];
    text_layout measured;
};
static const char *s(const json_t *v, const char *key)
{
    const char *value = json_string_value(json_object_get(v, key));
    return value ? value : "";
}
static json_t *conversation(message_browser *m, const char *id)
{
    for (size_t i = 0; i < history_channel_count(m->h); ++i) {
        json_t *c = history_channel(m->h, i);
        if (!strcmp(s(c, "id"), id))
            return c;
    }
    return NULL;
}
static json_t *items(message_browser *m)
{
    if (m->location_items)
        return m->location_items;
    return m->pins ? history_pins(m->h) : m->submitted ? history_search_results(m->h) : NULL;
}
static const char *author(message_browser *m, json_t *v)
{
    if (m->pins)
        return history_message_author(m->h, v);
    return *s(v, "author_username") ? s(v, "author_username")
                                    : history_author(m->h, s(v, "author_id"));
}
static void content(message_browser *m, json_t *v, text_buffer *text)
{
    const char *raw = s(v, "content");
    if (!*raw)
        raw =
            json_array_size(json_object_get(v, "attachments")) ? "[Anexo]" : "[Mensagem sem texto]";
    /* Same bounded, control-sanitizing text and mention projection as notifications. */
    text_decode_chunk(text, &raw);
    text_mentions(text, history_mention_name, m->h);
}
static json_t *find_detail(message_browser *m)
{
    json_t *array = items(m);
    size_t i;
    json_t *v;
    json_array_foreach(array, i, v) if (!strcmp(s(v, "id"), m->detail_id)) return v;
    return NULL;
}
static json_t *make_filters(message_browser *m)
{
    json_t *v = json_object();
    char text[TEXT_UTF8_BYTES], start[TEXT_UTF8_BYTES], end[TEXT_UTF8_BYTES];
    text_encode(&m->query, text);
    text_encode(&m->settings.start, start);
    text_encode(&m->settings.end, end);
    g_strstrip(text);
    bool ok =
        v && !json_object_set_new(v, "order", json_string(m->settings.ascending ? "asc" : "desc"));
    const char *keys[] = {"text", "author", "channel_id", "mention", "date_start", "date_end"};
    const char *values[] = {
        text, m->settings.author, m->settings.channel, m->settings.mention, start, end};
    for (size_t i = 0; i < G_N_ELEMENTS(keys); ++i)
        if (*values[i])
            ok &= !json_object_set_new(v, keys[i], json_string(values[i]));
    if (m->settings.link)
        ok &= !json_object_set_new(v, "has", json_string("link"));
    if (m->settings.attachments)
        ok &= !json_object_set_new(v, "contains_attachment",
                                   json_boolean(m->settings.attachments == 1));
    if (!ok) {
        json_decref(v);
        return NULL;
    }
    return v;
}
static bool dirty(message_browser *m)
{
    if (m->pins || !m->submitted)
        return false;
    json_t *v = make_filters(m);
    bool changed = !v || !json_equal(v, m->submitted_filters);
    json_decref(v);
    return changed;
}
static void submit(message_browser *m)
{
    json_t *v = make_filters(m);
    if (v && history_search(m->h, v)) {
        json_decref(m->location_items);
        m->location_items = NULL;
        json_decref(m->submitted_filters);
        m->submitted_filters = v;
        m->submitted = true;
        m->first = m->selected = 0;
        m->selected_id[0] = m->notice[0] = 0;
        m->focus = RESULTS;
    } else {
        json_decref(v);
        snprintf(m->notice, sizeof(m->notice), "%s", history_search_notice(m->h));
    }
}
message_browser *message_browser_create(history *h, auth_client *auth)
{
    message_browser *m = calloc(1, sizeof(*m));
    if (m) {
        m->h = h;
        m->auth = auth;
    }
    return m;
}
void message_browser_close(message_browser *m)
{
    if (!m)
        return;
    if (m->picker && m->picker != 2)
        history_users_active(m->h, false);
    if (m->open)
        history_discovery_close(m->h);
    json_decref(m->location_items);
    m->location_items = NULL;
    if (m->window)
        delwin(m->window);
    m->window = NULL;
    m->open = false;
}
void message_browser_destroy(message_browser *m)
{
    if (m) {
        message_browser_close(m);
        json_decref(m->submitted_filters);
        free(m);
    }
}
void message_browser_open(message_browser *m, bool pins)
{
    if (!m)
        return;
    message_browser_close(m);
    m->open = true;
    m->pins = pins;
    m->detail = m->editing_filters = m->unpin_pending = m->submitted = false;
    m->picker = m->field_first = 0;
    m->first = m->selected = m->count = m->detail_scroll = 0;
    m->selected_id[0] = m->detail_id[0] = m->destination[0] = m->destination_message[0] =
        m->notice[0] = 0;
    m->focus = QUERY;
    text_clear(&m->query);
    memset(&m->settings, 0, sizeof(m->settings));
    json_decref(m->submitted_filters);
    m->submitted_filters = NULL;
    if (pins)
        history_pins_refresh(m->h);
}
bool message_browser_is_open(const message_browser *m)
{
    return m && m->open;
}
static void update(message_browser *m)
{
    if (m->unpin_pending) {
        char id[37];
        bool success;
        if (history_take_pin_result(m->h, id, &success) && !strcmp(id, m->unpin_id)) {
            m->unpin_pending = false;
            if (success) {
                m->detail = false;
                m->selected_id[0] = 0;
                m->focus = RESULTS;
            }
        }
    }
    m->count = 0;
    char query[TEXT_UTF8_BYTES];
    text_encode(&m->query, query);
    size_t i;
    json_t *v;
    json_array_foreach(items(m), i, v)
    {
        if (m->count >= G_N_ELEMENTS(m->rows))
            break;
        if (m->pins && *query) {
            text_buffer text, display;
            char projected[TEXT_UTF8_BYTES];
            content(m, v, &text);
            text_project(&text, &display);
            text_encode(&display, projected);
            if (!mention_name_matches(projected, query) &&
                !mention_name_matches(s(v, "content"), query) &&
                !mention_name_matches(author(m, v), query) &&
                !mention_name_matches(history_author_username(m->h, s(v, "author_id")), query))
                continue;
        }
        if (*m->selected_id && !strcmp(s(v, "id"), m->selected_id))
            m->selected = m->count;
        m->rows[m->count++] = i;
    }
    if (m->selected >= m->count)
        m->selected = m->count ? m->count - 1 : 0;
}
static void picker_update(message_browser *m)
{
    char query[TEXT_UTF8_BYTES];
    text_encode(&m->picker_query, query);
    m->picker_count = 0;
    size_t total = m->picker == 2 ? history_channel_count(m->h) : history_user_count(m->h);
    for (size_t i = 0; i < total && m->picker_count < G_N_ELEMENTS(m->choices); ++i) {
        json_t *v = m->picker == 2 ? history_channel(m->h, i) : history_user(m->h, i);
        if (m->picker == 2 && !strcmp(s(v, "type"), "category"))
            continue;
        json_t *u = m->picker == 2 ? json_object_get(v, "user") : v;
        if (*query && !mention_name_matches(s(v, "name"), query) &&
            !mention_name_matches(s(u, "username"), query) &&
            !mention_name_matches(s(u, "nickname"), query))
            continue;
        if (*m->picker_id && !strcmp(s(v, "id"), m->picker_id))
            m->picker_selected = m->picker_count;
        m->choices[m->picker_count++] = i;
    }
    if (m->picker_selected >= m->picker_count)
        m->picker_selected = m->picker_count ? m->picker_count - 1 : 0;
}
static void picker_close(message_browser *m)
{
    if (m->picker != 2)
        history_users_active(m->h, false);
    m->picker = 0;
}
static void picker_choose(message_browser *m, bool clear)
{
    if (!clear && !m->picker_count)
        return;
    json_t *v = clear            ? NULL
                : m->picker == 2 ? history_channel(m->h, m->choices[m->picker_selected])
                                 : history_user(m->h, m->choices[m->picker_selected]);
    char *target = m->picker == 1   ? m->draft.author
                   : m->picker == 2 ? m->draft.channel
                                    : m->draft.mention;
    snprintf(target, 37, "%s", clear ? "" : s(v, "id"));
    picker_close(m);
}
static void setting_activate(message_browser *m)
{
    int focus = m->settings_focus;
    if (focus < 3) {
        m->picker = focus + 1;
        m->picker_focus = m->picker_field_first = 0;
        m->picker_selected = m->picker_first = 0;
        m->picker_id[0] = 0;
        text_clear(&m->picker_query);
        if (m->picker != 2) {
            if (!history_user_count(m->h))
                history_users_refresh(m->h);
            else
                history_users_active(m->h, true);
        }
    } else if (focus == 3)
        m->draft.link = !m->draft.link;
    else if (focus == 6)
        m->draft.attachments = (m->draft.attachments + 1) % 3;
    else if (focus == 7)
        m->draft.ascending = !m->draft.ascending;
    else if (focus == 8) {
        m->settings = m->draft;
        m->editing_filters = false;
        m->focus = FILTERS;
    } else if (focus == 9)
        m->editing_filters = false;
    else if (focus == 10)
        memset(&m->draft, 0, sizeof(m->draft));
}
static void activate(message_browser *m)
{
    if (m->detail) {
        json_t *v = find_detail(m);
        if (!v) {
            snprintf(m->notice, sizeof(m->notice), "Mensagem não disponível. Atualize a lista.");
            return;
        }
        if (m->pins && m->focus == 1) {
            if (!m->unpin_pending && history_pin_message(m->h, s(v, "id"), true)) {
                m->notice[0] = 0;
                m->unpin_pending = true;
                snprintf(m->unpin_id, sizeof(m->unpin_id), "%s", s(v, "id"));
            } else if (!m->unpin_pending)
                snprintf(m->notice, sizeof(m->notice),
                         "Aguarde a consulta ou atualize com Ctrl+R.");
            return;
        }
        if (history_uuid(s(v, "channel_id")) && history_uuid(s(v, "id"))) {
            if (!m->pins && !m->location_items)
                m->location_items = json_incref(items(m));
            snprintf(m->destination, sizeof(m->destination), "%s", s(v, "channel_id"));
            snprintf(m->destination_message, sizeof(m->destination_message), "%s", s(v, "id"));
            snprintf(m->destination_time, sizeof(m->destination_time), "%s", s(v, "created_at"));
        }
    } else if (m->focus == FILTERS && !m->pins) {
        m->draft = m->settings;
        m->settings_first = m->settings_focus = m->date_first[0] = m->date_first[1] = 0;
        m->editing_filters = true;
    } else if ((m->focus == SUBMIT || m->focus == QUERY) && !m->pins)
        submit(m);
    else if (m->focus == MORE && !m->pins) {
        if (dirty(m))
            snprintf(m->notice, sizeof(m->notice), "Execute Buscar para aplicar as alterações.");
        else {
            if (history_search_has_more(m->h)) {
                json_decref(m->location_items);
                m->location_items = NULL;
            }
            history_search_more(m->h);
        }
    } else if (m->count) {
        json_t *v = json_array_get(items(m), m->rows[m->selected]);
        snprintf(m->detail_id, sizeof(m->detail_id), "%s", s(v, "id"));
        m->detail = true;
        m->detail_scroll = 0;
        m->focus = 0;
    }
}
static void move_selection(size_t *selected, size_t count, uint32_t key)
{
    if (key == KEY_HOME)
        *selected = 0;
    else if (key == KEY_END)
        *selected = count ? count - 1 : 0;
    else if (key == KEY_UP || key == KEY_PPAGE)
        *selected =
            *selected > (key == KEY_UP ? 1U : 5U) ? *selected - (key == KEY_UP ? 1U : 5U) : 0;
    else if (key == KEY_DOWN || key == KEY_NPAGE)
        *selected = count ? MIN(*selected + (key == KEY_DOWN ? 1U : 5U), count - 1) : 0;
}
static bool navigation(uint32_t key)
{
    return key == KEY_UP || key == KEY_DOWN || key == KEY_PPAGE || key == KEY_NPAGE ||
           key == KEY_HOME || key == KEY_END;
}
static void mouse(message_browser *m)
{
    MEVENT e;
    if (ui_mouse_get(&e) != OK || !m->window)
        return;
    int y = e.y, x = e.x, height = getmaxy(m->window), width = getmaxx(m->window);
    if (!wmouse_trafo(m->window, &y, &x, false)) {
        if (e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) {
            if (m->picker)
                picker_close(m);
            else if (m->editing_filters)
                m->editing_filters = false;
            else
                message_browser_close(m);
        }
        return;
    }
    if (e.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
        uint32_t key = e.bstate & BUTTON4_PRESSED ? KEY_UP : KEY_DOWN;
        if (m->picker) {
            m->picker_focus = 1;
            m->picker_id[0] = 0;
            move_selection(&m->picker_selected, m->picker_count, key);
        } else if (m->editing_filters)
            m->settings_focus = CLAMP(m->settings_focus + (key == KEY_UP ? -1 : 1), 0, 10);
        else if (m->detail)
            m->detail_scroll = key == KEY_UP ? (m->detail_scroll ? m->detail_scroll - 1 : 0)
                                             : m->detail_scroll + 1;
        else {
            m->focus = RESULTS;
            m->selected_id[0] = 0;
            move_selection(&m->selected, m->count, key);
        }
        return;
    }
    if (!(e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)))
        return;
    if (m->picker) {
        if (y == 2) {
            m->picker_focus = 0;
            text_click(&m->picker_query, TEXT_LIMIT * 2 + 1, 0, x - 8 + m->picker_field_first);
        } else if (y == height - 4)
            picker_choose(m, true);
        else if (y >= 4 && y < height - 5 && m->picker_first + (size_t)(y - 4) < m->picker_count) {
            m->picker_selected = m->picker_first + (size_t)(y - 4);
            picker_choose(m, false);
        }
    } else if (m->editing_filters) {
        int index = y - 3 + m->settings_first;
        if (y >= 3 && y < height - 4 && index >= 0 && index <= 10) {
            m->settings_focus = index;
            if (index == 4 || index == 5)
                text_click(index == 4 ? &m->draft.start : &m->draft.end, TEXT_LIMIT * 2 + 1, 0,
                           x - 16 + m->date_first[index - 4]);
            else
                setting_activate(m);
        }
    } else if (m->detail) {
        if (y == height - 4) {
            m->focus = m->pins && x >= width / 2 ? 1 : 0;
            activate(m);
        }
    } else if (y == 2) {
        m->focus = QUERY;
        text_click(&m->query, TEXT_LIMIT * 2 + 1, 0, x - 10 + m->field_first);
    } else if (!m->pins && y == 3) {
        m->focus = x < width / 2 ? SUBMIT : FILTERS;
        activate(m);
    } else if (!m->pins && y == height - 4) {
        m->focus = MORE;
        activate(m);
    } else if (y >= 4 && y < height - 5 && m->first + (size_t)(y - 4) < m->count) {
        m->selected = m->first + (size_t)(y - 4);
        m->focus = RESULTS;
        activate(m);
    }
}
void message_browser_key(message_browser *m, int type, uint32_t key)
{
    if (!m || !m->open)
        return;
    update(m);
    if (m->picker)
        picker_update(m);
    if (type == KEY_CODE_YES && key == KEY_RESIZE)
        return;
    if (type == OK && key == 27) {
        m->notice[0] = 0;
        if (m->picker)
            picker_close(m);
        else if (m->editing_filters)
            m->editing_filters = false;
        else if (m->detail) {
            m->detail = false;
            m->focus = RESULTS;
        } else
            message_browser_close(m);
        return;
    }
    if (type == OK && key == 18) {
        m->notice[0] = 0;
        if (m->picker && m->picker != 2)
            history_users_refresh(m->h);
        else if (m->pins)
            history_pins_refresh(m->h);
        else if (!m->editing_filters && !m->picker)
            submit(m);
        return;
    }
    if (type == KEY_CODE_YES && key == KEY_MOUSE) {
        mouse(m);
        return;
    }
    bool tab = (type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB);
    bool enter =
        (type == OK && (key == '\n' || key == '\r')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (m->picker) {
        if (tab)
            m->picker_focus = (m->picker_focus + (type == OK ? 1 : 2)) % 3;
        else if (enter)
            picker_choose(m, m->picker_focus == 2);
        else if (type == KEY_CODE_YES && navigation(key) &&
                 (m->picker_focus == 1 || key == KEY_UP || key == KEY_DOWN)) {
            m->picker_id[0] = 0;
            if (m->picker_focus == 0 && key == KEY_DOWN)
                m->picker_focus = 1;
            else if (m->picker_focus == 1 && key == KEY_UP && !m->picker_selected)
                m->picker_focus = 0;
            else
                move_selection(&m->picker_selected, m->picker_count, key);
        } else if (!m->picker_focus &&
                   text_edit(&m->picker_query, type, key, TEXT_LIMIT * 2 + 1, false)) {
            m->picker_selected = m->picker_first = 0;
            m->picker_id[0] = 0;
        }
        return;
    }
    if (m->editing_filters) {
        if (tab || (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN))) {
            int delta = tab ? (type == OK ? 1 : 10) : key == KEY_DOWN ? 1 : 10;
            m->settings_focus = (m->settings_focus + delta) % 11;
        } else if (enter ||
                   (type == OK && key == ' ' && m->settings_focus != 4 && m->settings_focus != 5) ||
                   (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT) &&
                    (m->settings_focus == 3 || m->settings_focus == 6 || m->settings_focus == 7)))
            setting_activate(m);
        else if (m->settings_focus == 4 || m->settings_focus == 5)
            text_edit(m->settings_focus == 4 ? &m->draft.start : &m->draft.end, type, key,
                      TEXT_LIMIT * 2 + 1, false);
        return;
    }
    if (m->detail) {
        if (tab && m->pins)
            m->focus = 1 - m->focus;
        else if (m->pins && type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT))
            m->focus = key == KEY_RIGHT ? 1 : 0;
        else if (enter)
            activate(m);
        else if (type == KEY_CODE_YES && navigation(key)) {
            if (key == KEY_HOME)
                m->detail_scroll = 0;
            else if (key == KEY_END)
                m->detail_scroll = TEXT_LIMIT;
            else if (key == KEY_UP || key == KEY_PPAGE)
                m->detail_scroll = m->detail_scroll > (key == KEY_UP ? 1U : 5U)
                                       ? m->detail_scroll - (key == KEY_UP ? 1U : 5U)
                                       : 0;
            else
                m->detail_scroll += key == KEY_DOWN ? 1 : 5;
        }
        return;
    }
    if (tab) {
        const int order[] = {QUERY, SUBMIT, FILTERS, RESULTS, MORE};
        if (m->pins)
            m->focus = m->focus == QUERY ? RESULTS : QUERY;
        else
            for (size_t i = 0; i < G_N_ELEMENTS(order); ++i)
                if (m->focus == order[i]) {
                    m->focus = order[(i + (type == OK ? 1 : G_N_ELEMENTS(order) - 1)) %
                                     G_N_ELEMENTS(order)];
                    break;
                }
    } else if (enter)
        activate(m);
    else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT) &&
             (m->focus == SUBMIT || m->focus == FILTERS))
        m->focus = key == KEY_RIGHT ? FILTERS : SUBMIT;
    else if (type == KEY_CODE_YES && (key == KEY_DOWN || key == KEY_UP)) {
        if (m->focus == QUERY) {
            if (key == KEY_DOWN)
                m->focus = m->pins ? RESULTS : SUBMIT;
        } else if (m->focus == SUBMIT || m->focus == FILTERS)
            m->focus = key == KEY_DOWN ? RESULTS : QUERY;
        else if (m->focus == MORE) {
            if (key == KEY_UP)
                m->focus = RESULTS;
        } else if (key == KEY_UP && !m->selected)
            m->focus = m->pins ? QUERY : SUBMIT;
        else {
            m->selected_id[0] = 0;
            move_selection(&m->selected, m->count, key);
        }
    } else if (type == KEY_CODE_YES && navigation(key) && m->focus == RESULTS) {
        m->selected_id[0] = 0;
        move_selection(&m->selected, m->count, key);
    } else if (m->focus == QUERY || (type == OK && iswprint(key))) {
        if (text_edit(&m->query, type, key, TEXT_LIMIT * 2 + 1, false)) {
            m->focus = QUERY;
            m->notice[0] = 0;
            if (m->pins) {
                m->selected = m->first = 0;
                m->selected_id[0] = 0;
            }
        }
    }
}
static void keep_visible(size_t selected, size_t *first, int visible)
{
    if (selected < *first)
        *first = selected;
    if (selected >= *first + (size_t)visible)
        *first = selected - (size_t)visible + 1;
}
static void picker_draw(message_browser *m, int width, int height)
{
    picker_update(m);
    WINDOW *w = m->window;

    wattrset(w, COLOR_PAIR(m->picker_focus == 0 ? THEME_FOCUSED : THEME_PANEL));
    text_label(w, 2, 2, 6, "Nome: ");
    text_draw_field(w, &m->picker_query, &m->picker_field_first, 2, 8, width - 10, false);
    keep_visible(m->picker_selected, &m->picker_first, MAX(1, height - 9));
    for (int y = 4; y < height - 5 && m->picker_first + (size_t)(y - 4) < m->picker_count; ++y) {
        size_t index = m->picker_first + (size_t)(y - 4);
        json_t *v = m->picker == 2 ? history_channel(m->h, m->choices[index])
                                   : history_user(m->h, m->choices[index]);
        char label[2200];
        if (m->picker == 2)
            snprintf(label, sizeof(label), "%s%s",
                     json_is_true(json_object_get(v, "_dm")) ? "DM: " : "# ", s(v, "name"));
        else
            snprintf(label, sizeof(label), "%s @%s", s(v, "nickname"), s(v, "username"));
        wattrset(w, COLOR_PAIR(index == m->picker_selected ? THEME_PANEL_ACTIVE : THEME_PANEL));
        text_label(w, y, 2, width - 4, label);
    }
    if (m->picker_count) {
        json_t *v = m->picker == 2 ? history_channel(m->h, m->choices[m->picker_selected])
                                   : history_user(m->h, m->choices[m->picker_selected]);
        snprintf(m->picker_id, sizeof(m->picker_id), "%s", s(v, "id"));
    } else
        text_label(w, 4, 2, width - 4, "Nenhum nome encontrado.");
    button_draw(w, height - 4, 2, width - 4, "[Qualquer / limpar filtro]",
                m->picker_focus == 2 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
}
static void settings_draw(message_browser *m, int width, int height)
{
    WINDOW *w = m->window;

    text_label(w, 2, 2, width - 4, "Datas inclusivas: AAAA-MM-DD");
    int visible = MAX(1, height - 7);
    if (m->settings_focus < m->settings_first)
        m->settings_first = m->settings_focus;
    if (m->settings_focus >= m->settings_first + visible)
        m->settings_first = m->settings_focus - visible + 1;
    for (int y = 3, i = m->settings_first; y < height - 4 && i <= 10; ++y, ++i) {
        char label[2200];
        if (i == 0)
            snprintf(label, sizeof(label), "Autor: %s",
                     *m->draft.author ? history_author(m->h, m->draft.author) : "Qualquer");
        else if (i == 1)
            snprintf(label, sizeof(label), "Conversa: %s",
                     *m->draft.channel ? s(conversation(m, m->draft.channel), "name")
                                       : "Servidor inteiro");
        else if (i == 2)
            snprintf(label, sizeof(label), "Menciona: %s",
                     *m->draft.mention ? history_author(m->h, m->draft.mention) : "Qualquer");
        else if (i == 3)
            snprintf(label, sizeof(label), "[%c] Contém link", m->draft.link ? 'x' : ' ');
        else if (i == 6)
            snprintf(label, sizeof(label), "Anexos: %s",
                     m->draft.attachments == 1   ? "Com anexos"
                     : m->draft.attachments == 2 ? "Sem anexos"
                                                 : "Qualquer");
        else if (i == 7)
            snprintf(label, sizeof(label), "Ordem: %s",
                     m->draft.ascending ? "Mais antigas" : "Mais recentes");
        else if (i >= 8)
            snprintf(label, sizeof(label), "%s",
                     i == 8   ? "[Aplicar]"
                     : i == 9 ? "[Cancelar]"
                              : "[Limpar filtros]");
        else {
            wattrset(w, COLOR_PAIR(m->settings_focus == i ? THEME_FOCUSED : THEME_PANEL));
            text_label(w, y, 2, 14, i == 4 ? "Data inicial: " : "Data final: ");
            text_draw_field(w, i == 4 ? &m->draft.start : &m->draft.end, &m->date_first[i - 4], y,
                            16, width - 18, false);
            continue;
        }
        button_draw(w, y, 2, width - 4, label,
                    m->settings_focus == i ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    }
}
WINDOW *message_browser_draw(message_browser *m)
{
    if (!m || !m->open || COLS < 44 || LINES < 14)
        return NULL;
    int width = MIN(COLS - 4, 90), height = MIN(LINES - 2, 25);
    int top = (LINES - height) / 2, left = (COLS - width) / 2;
    if (m->window && (getmaxx(m->window) != width || getmaxy(m->window) != height ||
                      getbegx(m->window) != left || getbegy(m->window) != top)) {
        delwin(m->window);
        m->window = NULL;
    }
    if (!m->window)
        m->window = newwin(height, width, top, left);
    if (!m->window)
        return NULL;
    update(m);
    WINDOW *w = m->window;
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    const char *title = m->picker == 1       ? "Selecionar autor"
                        : m->picker == 2     ? "Selecionar conversa"
                        : m->picker == 3     ? "Selecionar menção"
                        : m->editing_filters ? "Filtros da busca"
                        : m->detail          ? "Detalhes da mensagem"
                        : m->pins            ? "Mensagens fixadas"
                                             : "Buscar mensagens no servidor";
    text_label(w, 1, 2, width - 4, title);
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    if (m->picker)
        picker_draw(m, width, height);
    else if (m->editing_filters)
        settings_draw(m, width, height);
    else if (m->detail) {
        json_t *v = find_detail(m);
        char metadata[2200];
        snprintf(metadata, sizeof(metadata), "%s · %s",
                 m->pins ? s(conversation(m, s(v, "channel_id")), "name") : s(v, "channel_name"),
                 s(v, "created_at"));
        text_label(w, 2, 2, width - 4, metadata);
        text_label(w, 3, 2, width - 4,
                   v ? author(m, v) : "Mensagem não disponível. Atualize a lista.");
        text_buffer text;
        content(m, v, &text);
        text_measure(&text, width - 4, &m->measured);
        size_t max = m->measured.rows > height - 8 ? (size_t)(m->measured.rows - (height - 8)) : 0;
        m->detail_scroll = MIN(m->detail_scroll, max);
        text_draw(w, &text, &m->measured, (int)m->detail_scroll, height - 8, 4, 2);
        int button_width = m->pins ? width / 2 - 2 : width - 4;
        button_draw(w, height - 4, 2, button_width, "[Ir para mensagem]",
                    m->focus == 0 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        if (m->pins)
            button_draw(w, height - 4, width / 2, width / 2 - 2,
                        m->unpin_pending ? "[Aguarde…]" : "[Desafixar]",
                        m->focus == 1 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    } else {
        wattrset(w, COLOR_PAIR(m->focus == QUERY ? THEME_FOCUSED : THEME_PANEL));
        text_label(w, 2, 2, 8, "Buscar: ");
        text_draw_field(w, &m->query, &m->field_first, 2, 10, width - 12, false);
        if (!m->pins) {
            button_draw(w, 3, 2, width / 2 - 2, "[Buscar]",
                        m->focus == SUBMIT ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
            button_draw(w, 3, width / 2, width / 2 - 2, "[Filtros]",
                        m->focus == FILTERS ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        }
        keep_visible(m->selected, &m->first, MAX(1, height - 9));
        for (int y = 4; y < height - 5 && m->first + (size_t)(y - 4) < m->count; ++y) {
            size_t index = m->first + (size_t)(y - 4);
            json_t *v = json_array_get(items(m), m->rows[index]);
            text_buffer text, display;
            char encoded[TEXT_UTF8_BYTES], label[TEXT_UTF8_BYTES + 2400];
            content(m, v, &text);
            text_project(&text, &display);
            text_encode(&display, encoded);
            snprintf(label, sizeof(label), "%s · %s · %s · %.10s", author(m, v), encoded,
                     m->pins ? s(conversation(m, s(v, "channel_id")), "name")
                             : s(v, "channel_name"),
                     s(v, "created_at"));
            wattrset(w, COLOR_PAIR(index == m->selected && m->focus == RESULTS ? THEME_PANEL_ACTIVE
                                                                               : THEME_PANEL));
            text_label(w, y, 2, width - 4, label);
        }
        if (m->count) {
            json_t *v = json_array_get(items(m), m->rows[m->selected]);
            snprintf(m->selected_id, sizeof(m->selected_id), "%s", s(v, "id"));
        } else
            text_label(w, 4, 2, width - 4,
                       m->pins        ? "Nenhuma fixada corresponde à busca."
                       : m->submitted ? "Nenhuma mensagem encontrada."
                                      : "Informe texto ou filtros e selecione Buscar.");
        char total[128];
        snprintf(total, sizeof(total), "%zu resultado(s)%s", m->count,
                 dirty(m) ? " · Alterações pendentes: Buscar para aplicar" : "");
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, height - 5, 2, width - 4, total);
        if (!m->pins)
            button_draw(w, height - 4, 2, width - 4,
                        history_search_has_more(m->h) ? "[Carregar mais]" : "[Sem mais resultados]",
                        m->focus == MORE ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    const char *notice = *m->notice ? m->notice
                         : m->pins  ? history_pins_notice(m->h)
                                    : history_search_notice(m->h);
    if (m->picker && m->picker != 2)
        notice = history_users_notice(m->h);
    if (m->detail && m->pins) {
        const char *mutation = history_notice(m->h, HISTORY_NOTICE_MUTATION, NULL);
        if (*mutation && !*m->notice)
            notice = mutation;
    }
    text_label(w, height - 3, 2, width - 4, notice);
    text_label(w, height - 2, 2, width - 4,
               m->detail && m->pins ? "←→/Tab: ações · ↑↓ rolar · Enter: abrir · Esc: voltar"
               : !m->pins && !m->editing_filters && !m->picker && !m->detail
                   ? "↑↓: áreas · ←→: botões · Enter: abrir · Esc: voltar"
                   : "Tab: áreas · ↑↓ rolar · Enter: abrir/aplicar · Esc: voltar");
    wnoutrefresh(w);
    return w;
}
bool message_browser_cursor(message_browser *m)
{
    if (!m || !m->open || !m->window || m->detail)
        return false;
    text_buffer *input = NULL;
    int y = 2, x = 10, first = m->field_first;
    if (m->picker && !m->picker_focus) {
        input = &m->picker_query;
        x = 8;
        first = m->picker_field_first;
    } else if (!m->picker && m->editing_filters &&
               (m->settings_focus == 4 || m->settings_focus == 5)) {
        input = m->settings_focus == 4 ? &m->draft.start : &m->draft.end;
        x = 16;
        y = 3 + m->settings_focus - m->settings_first;
        first = m->date_first[m->settings_focus - 4];
    } else if (!m->picker && !m->editing_filters && m->focus == QUERY)
        input = &m->query;
    if (!input)
        return false;
    text_measure(input, TEXT_LIMIT * 2 + 1, &m->measured);
    int column = x + m->measured.cursor_column - first;
    if (column < x || column >= getmaxx(m->window) - 2)
        return false;
    setsyx(getbegy(m->window) + y, getbegx(m->window) + column);
    return true;
}
const char *message_browser_destination(const message_browser *m)
{
    return m ? m->destination : "";
}
const char *message_browser_destination_message(const message_browser *m)
{
    return m ? m->destination_message : "";
}
const char *message_browser_destination_time(const message_browser *m)
{
    return m ? m->destination_time : "";
}
void message_browser_destination_clear(message_browser *m)
{
    if (m)
        m->destination[0] = m->destination_message[0] = m->destination_time[0] = 0;
}
