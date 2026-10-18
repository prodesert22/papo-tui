#include "ui/screens/inbox.h"
#include "net/auth/session_time.h"
#include "net/clock.h"
#include "ui/components/button.h"
#include "ui/components/mention_popup.h"
#include "ui/components/theme.h"
#include "ui/input/mouse.h"
#include "ui/screens/message_browser.h"
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
typedef struct {
    json_t *value; /* Borrowed until the next model update. */
    bool event;
    const char *time;
} notification_row;
struct r8_modal {
    history *h;
    message_browser *browser;
    auth_client *auth;
    profile_config *config;
    WINDOW *window;
    r8_modal_kind kind;
    bool open, detail, detail_event, prefs;
    bool locating, location_started, location_failed;
    uint64_t location_frame;
    char destination_time[65], location_channel[37], location_message[37], location_time[65];
    char location_notice[192];
    size_t selected, first, detail_scroll;
    int focus, field_first;
    char detail_id[37], detail_message[37], destination[37], destination_message[37],
        selected_id[37], notice[192];
    size_t users[10000], count; /* Shared indices for non-overlapping directory selectors. */
    text_buffer filter;
    notification_row rows[1256]; /* 1000 persisted rows + 256 ephemeral events. */
    int conversation_filter;     /* 0 all, 1 channels, 2 DMs, 3 specific UUID. */
    char filter_channel[37];
    bool oldest_first;
    bool choosing_conversation;
    int choice_kind, choice_focus, choice_field_first;
    size_t choice_selected, choice_first, choice_count;
    char choice_id[37];
    text_buffer choice_query;
};
static const char *s(const json_t *o, const char *k)
{
    const char *v = json_string_value(json_object_get(o, k));
    return v ? v : "";
}
const char *r8_presence_label(const char *state)
{
    return !strcmp(state, "online")    ? "online"
           : !strcmp(state, "away")    ? "ausente"
           : !strcmp(state, "busy")    ? "ocupado"
           : !strcmp(state, "offline") ? "offline"
                                       : "?";
}
r8_modal *r8_modal_create(history *h, auth_client *auth, profile_config *config)
{
    r8_modal *m = calloc(1, sizeof(*m));
    if (m) {
        m->h = h;
        m->auth = auth;
        m->config = config;
    }
    return m;
}
void r8_modal_close(r8_modal *m)
{
    if (!m)
        return;
    if (m->locating) {
        history_cancel_jump(m->h);
        inbox_cancel_open(history_inbox(m->h));
    }
    m->locating = m->location_started = m->location_failed = false;
    if (m->browser)
        message_browser_close(m->browser);
    if (m->window) {
        delwin(m->window);
        m->window = NULL;
    }
    if (m->kind == R8_USERS)
        history_users_active(m->h, false);
    m->open = false;
}
void r8_modal_destroy(r8_modal *m)
{
    if (m) {
        r8_modal_close(m);
        message_browser_destroy(m->browser);
        free(m);
    }
}
void r8_modal_open(r8_modal *m, r8_modal_kind kind)
{
    r8_modal_close(m);
    m->kind = kind;
    m->open = true;
    m->selected = m->first = m->detail_scroll = 0;
    m->focus = 0;
    m->conversation_filter = 0;
    m->filter_channel[0] = 0;
    m->oldest_first = false;
    m->choosing_conversation = false;
    m->detail = m->prefs = false;
    m->detail_id[0] = m->detail_message[0] = m->selected_id[0] = m->notice[0] = 0;
    r8_modal_destination_clear(m);
    text_decode(&m->filter, "");
    if (kind == R8_PINNED || kind == R8_SEARCH) {
        if (!m->browser)
            m->browser = message_browser_create(m->h, m->auth);
        message_browser_open(m->browser, kind == R8_PINNED);
        m->open = message_browser_is_open(m->browser);
    } else if (kind == R8_USERS)
        history_users_refresh(m->h);
    else if (kind == R8_NOTIFICATIONS)
        inbox_refresh(history_inbox(m->h));
}
bool r8_modal_is_open(const r8_modal *m)
{
    return m && m->open &&
           ((m->kind != R8_PINNED && m->kind != R8_SEARCH) || message_browser_is_open(m->browser));
}
static json_t *notification(r8_modal *m, size_t index, bool *event)
{
    *event = m->rows[index].event;
    return m->rows[index].value;
}
static json_t *message(r8_modal *m, json_t *n)
{
    json_t *v = history_find_message(m->h, s(n, "message_id"));
    return json_is_object(v) ? v : inbox_message_preview(history_inbox(m->h), s(n, "message_id"));
}
static const char *author(r8_modal *m, json_t *n, bool event)
{
    json_t *v = message(m, n);
    json_t *cached = inbox_message_preview(history_inbox(m->h), s(n, "message_id"));
    if (history_message_webhook(cached) && *s(json_object_get(cached, "external_author"), "name"))
        return history_message_author(m->h, cached);
    if (v && history_message_webhook(v) &&
        json_is_object(history_find_message(m->h, s(n, "message_id"))))
        return history_message_author(m->h, v);
    const char *id = s(n, event ? "user_id" : "author_id");
    if (!*id)
        id = s(v, "author_id");
    if (*id)
        return history_author(m->h, id);
    const char *channel =
        event ? inbox_message_channel(history_inbox(m->h), s(n, "message_id")) : s(n, "channel_id");
    if (history_uuid(channel) &&
        !strcmp(channel, s(history_channel(m->h, history_selected(m->h)), "id"))) {
        history_reference_request(m->h, s(n, "message_id"));
        json_t *reference = history_find_message(m->h, s(n, "message_id"));
        if (json_is_false(reference))
            return "Mensagem não disponível";
        if (!json_is_object(reference))
            return history_reference_notice(m->h);
    }
    return history_message_webhook(v) ? "Webhook" : "Autor ainda não carregado";
}
static const char *channel_id(r8_modal *m, json_t *n, bool event)
{
    return event ? inbox_message_channel(history_inbox(m->h), s(n, "message_id"))
                 : s(n, "channel_id");
}
static unsigned author_color(r8_modal *m, json_t *n, bool event)
{
    json_t *v = message(m, n);
    if (history_message_webhook(v) ||
        history_message_webhook(inbox_message_preview(history_inbox(m->h), s(n, "message_id"))))
        return 0x1ffffff;
    const char *id = s(n, event ? "user_id" : "author_id");
    return history_member_color(m->h, *id ? id : s(v, "author_id"));
}
static json_t *channel(r8_modal *m, const char *id)
{
    for (size_t i = 0; i < history_channel_count(m->h); ++i) {
        json_t *v = history_channel(m->h, i);
        if (!strcmp(s(v, "id"), id))
            return v;
    }
    return NULL;
}
static void projected(r8_modal *m, const char *raw, char output[TEXT_UTF8_BYTES])
{
    text_buffer text, display;
    text_decode(&text, raw);
    text_mentions(&text, history_mention_name, m->h);
    text_project(&text, &display);
    text_encode(&display, output);
}
static const char *timestamp(r8_modal *m, const notification_row *row)
{
    (void)m;
    return row->time;
}
static int chronological(const void *a, const void *b)
{
    const notification_row *left = a, *right = b;
    const char *at = left->time, *bt = right->time;
    int order = 0;
    /* Events without timestamps stay in a separate, explicitly undated group. */
    if (!*at || !*bt) {
        order = !*at - !*bt;
        if (order)
            return order;
    } else
        session_time_compare(at, bt, &order);
    if (!order && left->event != right->event)
        return left->event ? 1 : -1;
    if (!order)
        order = strcmp(s(left->value, "id"), s(right->value, "id"));
    return -order;
}
static void filter_label(r8_modal *m, char *label, size_t capacity)
{
    json_t *c = channel(m, m->filter_channel);
    snprintf(label, capacity, "Filtro: %s%s",
             m->conversation_filter == 0               ? "Todos"
             : m->conversation_filter == 1             ? "Canais"
             : m->conversation_filter == 2             ? "DMs"
             : json_is_true(json_object_get(c, "_dm")) ? "DM: "
                                                       : "# ",
             m->conversation_filter == 3 ? (c ? s(c, "name") : "indisponível") : "");
}
static int name_button_column(const char *label, int width)
{
    return 3 + text_label(NULL, 0, 0, (width - 10) / 2, label);
}
static int date_button_column(int width)
{
    return 10 + (width - 10) / 2;
}
static void choices_update(r8_modal *m)
{
    char query[TEXT_UTF8_BYTES];
    text_encode(&m->choice_query, query);
    m->choice_count = 0;
    for (size_t i = 0; i < history_channel_count(m->h) && m->choice_count < G_N_ELEMENTS(m->users);
         ++i) {
        json_t *c = history_channel(m->h, i);
        bool dm = json_is_true(json_object_get(c, "_dm"));
        if (!strcmp(s(c, "type"), "category") || dm != (m->choice_kind == 2))
            continue;
        json_t *user = json_object_get(c, "user");
        if (*query && !mention_name_matches(s(c, "name"), query) &&
            !(dm && (mention_name_matches(s(user, "username"), query) ||
                     mention_name_matches(s(user, "nickname"), query))))
            continue;
        if (*m->choice_id && !strcmp(s(c, "id"), m->choice_id))
            m->choice_selected = m->choice_count;
        m->users[m->choice_count++] = i;
    }
    if (m->choice_selected >= m->choice_count)
        m->choice_selected = m->choice_count ? m->choice_count - 1 : 0;
}
static int filter_kind(r8_modal *m)
{
    return m->conversation_filter == 3
               ? (json_is_true(json_object_get(channel(m, m->filter_channel), "_dm")) ? 2 : 1)
               : m->conversation_filter;
}
static void choices_open(r8_modal *m, int kind)
{
    m->choosing_conversation = true;
    m->choice_kind = kind;
    m->choice_focus = m->choice_field_first = 0;
    m->choice_selected = m->choice_first = 0;
    snprintf(m->choice_id, sizeof(m->choice_id), "%s", m->filter_channel);
    text_decode(&m->choice_query, "");
}
static void choices_select(r8_modal *m, bool all)
{
    if (!all && !m->choice_count)
        return;
    json_t *c = all ? NULL : history_channel(m->h, m->users[m->choice_selected]);
    if (!all && !history_uuid(s(c, "id")))
        return;
    m->conversation_filter = all ? m->choice_kind : 3;
    snprintf(m->filter_channel, sizeof(m->filter_channel), "%s", all ? "" : s(c, "id"));
    m->choosing_conversation = false;
    m->focus = 0;
    m->selected = m->first = 0;
    m->selected_id[0] = 0;
}
static void cycle_filter(r8_modal *m, bool backwards)
{
    int next = (filter_kind(m) + (backwards ? 2 : 1)) % 3;
    m->conversation_filter = next;
    m->filter_channel[0] = 0;
    m->selected = m->first = 0;
    m->selected_id[0] = 0;
}
static void update(r8_modal *m)
{
    if (m->choosing_conversation) {
        choices_update(m);
        return;
    }
    inbox *b = history_inbox(m->h);
    if (m->kind != R8_USERS) {
        char query[TEXT_UTF8_BYTES];
        text_encode(&m->filter, query);
        m->count = 0;
        size_t persisted = inbox_notification_count(b), total = persisted + inbox_event_count(b);
        for (size_t i = 0; i < total && m->count < G_N_ELEMENTS(m->rows); ++i) {
            bool event = i >= persisted;
            json_t *n =
                event ? inbox_notification_event(b, i - persisted) : inbox_notification(b, i);
            const char *id = channel_id(m, n, event);
            json_t *c = channel(m, id);
            bool dm = json_is_true(json_object_get(c, "_dm"));
            if ((m->conversation_filter == 1 && (!c || dm)) ||
                (m->conversation_filter == 2 && (!c || !dm)) ||
                (m->conversation_filter == 3 && strcmp(id, m->filter_channel)))
                continue;
            if (*query) {
                char content[TEXT_UTF8_BYTES];
                projected(m, s(n, "message_content"), content);
                if (!mention_name_matches(content, query) &&
                    !mention_name_matches(author(m, n, event), query) &&
                    !mention_name_matches(s(c, "name"), query))
                    continue;
            }
            m->rows[m->count++] = (notification_row){
                n, event, event ? s(message(m, n), "created_at") : s(n, "created_at")};
        }
        qsort(m->rows, m->count, sizeof(m->rows[0]), chronological);
        size_t dated = 0;
        while (dated < m->count && *m->rows[dated].time)
            ++dated;
        if (m->oldest_first)
            for (size_t i = 0; i < dated / 2; ++i) {
                notification_row swap = m->rows[i];
                m->rows[i] = m->rows[dated - i - 1];
                m->rows[dated - i - 1] = swap;
            }
    } else {
        char query[TEXT_UTF8_BYTES];
        text_encode(&m->filter, query);
        m->count = 0;
        for (size_t i = 0; i < history_user_count(m->h) && m->count < 10000; ++i) {
            json_t *u = history_user(m->h, i);
            if (!strcmp(s(u, "id"), auth_user_id(m->auth)) ||
                json_is_true(json_object_get(u, "banned")))
                continue;
            if (!*query || mention_name_matches(s(u, "username"), query) ||
                mention_name_matches(s(u, "nickname"), query))
                m->users[m->count++] = i;
        }
    }
    if (*m->selected_id)
        for (size_t i = 0; i < m->count; ++i) {
            bool event;
            json_t *v =
                m->kind == R8_USERS ? history_user(m->h, m->users[i]) : notification(m, i, &event);
            if (!strcmp(s(v, "id"), m->selected_id)) {
                m->selected = i;
                break;
            }
        }
    if (m->selected >= m->count)
        m->selected = m->count ? m->count - 1 : 0;
}
static json_t *detail(r8_modal *m, bool *event)
{
    inbox *b = history_inbox(m->h);
    for (size_t i = 0; i < inbox_notification_count(b); ++i) {
        json_t *n = inbox_notification(b, i);
        if (!strcmp(s(n, "id"), m->detail_id) || (m->detail_event && *m->detail_message &&
                                                  !strcmp(s(n, "message_id"), m->detail_message))) {
            *event = false;
            return n;
        }
    }
    for (size_t i = 0; i < inbox_event_count(b); ++i) {
        json_t *n = inbox_notification_event(b, i);
        if (!strcmp(s(n, "id"), m->detail_id)) {
            *event = true;
            return n;
        }
    }
    *event = m->detail_event;
    return NULL;
}
static void begin_location(r8_modal *m)
{
    m->locating = true;
    m->location_started = m->location_failed = false;
    m->location_notice[0] = 0;
    snprintf(m->location_channel, sizeof(m->location_channel), "%s", m->destination);
    snprintf(m->location_message, sizeof(m->location_message), "%s", m->destination_message);
    snprintf(m->location_time, sizeof(m->location_time), "%s", m->destination_time);
}
static void activate(r8_modal *m)
{
    inbox *b = history_inbox(m->h);
    if (m->prefs) {
        if (!m->config)
            return;
        bool desktop = m->config->desktop_notifications, preview = m->config->notification_preview;
        if (m->focus == 0)
            desktop = !desktop;
        else
            preview = !preview;
        if (!profiles_set_notifications(m->config, desktop, preview))
            snprintf(m->notice, sizeof(m->notice), "%s", m->config->message);
        else
            m->notice[0] = 0;
    } else if (m->detail) {
        bool event;
        json_t *n = detail(m, &event);
        const char *id = event ? inbox_message_channel(b, s(n, "message_id")) : s(n, "channel_id");
        if (history_uuid(id)) {
            snprintf(m->destination, sizeof(m->destination), "%s", id);
            snprintf(m->destination_message, sizeof(m->destination_message), "%s",
                     s(n, "message_id"));
            /* Notification time is not the message creation time. Only use an
             * observed message's own timestamp as a cursor hint. */
            snprintf(m->destination_time, sizeof(m->destination_time), "%s",
                     s(message(m, n), "created_at"));
            if (*m->destination_message)
                begin_location(m);
            else
                r8_modal_close(m);
        } else
            snprintf(m->notice, sizeof(m->notice),
                     "Destino ainda indisponível. Ctrl+R: atualizar.");
    } else if (m->kind == R8_USERS) {
        json_t *u = m->count ? history_user(m->h, m->users[m->selected]) : NULL;
        if (u && inbox_open_dm(b, s(u, "id")))
            r8_modal_close(m);
    } else if (m->focus == 1) {
        m->prefs = true;
        m->focus = 0;
    } else if (m->focus == 2) {
        inbox_older(b);
    } else if (m->focus == 3) {
        m->focus = 0;
    } else if (m->focus == 4 || m->focus == 6) {
        if (!m->conversation_filter)
            cycle_filter(m, false);
        choices_open(m, filter_kind(m));
    } else if (m->focus == 5) {
        m->oldest_first = !m->oldest_first;
        m->selected = m->first = 0;
    } else if (m->kind == R8_NOTIFICATIONS && m->count) {
        bool event;
        json_t *n = notification(m, m->selected, &event);
        snprintf(m->detail_id, sizeof(m->detail_id), "%s", s(n, "id"));
        snprintf(m->detail_message, sizeof(m->detail_message), "%s", s(n, "message_id"));
        m->detail_event = event;
        m->detail = true;
        m->detail_scroll = 0;
        /* Explicit user opening is evidence of focus even on terminals without
         * reporting. Automatic rendering of history requires confirmed focus. */
        inbox_view(b, m->detail_id, event);
    }
}
static void choices_key(r8_modal *m, int type, uint32_t key)
{
    m->choice_id[0] = 0;
    if (type == KEY_CODE_YES && key == KEY_MOUSE) {
        MEVENT event;
        if (ui_mouse_get(&event) != OK || !m->window)
            return;
        int y = event.y, x = event.x;
        if (!wmouse_trafo(m->window, &y, &x, false)) {
            if (event.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) {
                m->choosing_conversation = false;
                m->focus = 4;
            }
            return;
        }
        if (event.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
            m->choice_focus = 1;
            if ((event.bstate & BUTTON4_PRESSED) && m->choice_selected)
                --m->choice_selected;
            else if ((event.bstate & BUTTON5_PRESSED) && m->choice_selected + 1 < m->choice_count)
                ++m->choice_selected;
        } else if (event.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) {
            if (y == 2) {
                m->choice_focus = 0;
                text_click(&m->choice_query, TEXT_LIMIT * 2 + 1, 0, x - 8 + m->choice_field_first);
            } else if (y == 3) {
                int kind = x < getmaxx(m->window) / 2 ? 1 : 2;
                if (m->choice_kind != kind) {
                    int focus = m->choice_focus;
                    choices_open(m, kind);
                    m->choice_focus = focus;
                }
            } else if (y == getmaxy(m->window) - 4)
                choices_select(m, true);
            else if (y >= 4 && y < getmaxy(m->window) - 5 &&
                     m->choice_first + (size_t)(y - 4) < m->choice_count) {
                m->choice_selected = m->choice_first + (size_t)(y - 4);
                choices_select(m, false);
            }
        }
        return;
    }
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
        m->choice_focus = (m->choice_focus + (type == OK ? 1 : 2)) % 3;
        return;
    }
    if ((type == OK && (key == '\n' || key == '\r')) ||
        (type == KEY_CODE_YES && key == KEY_ENTER)) {
        choices_select(m, m->choice_focus == 2);
        return;
    }
    if (type == KEY_CODE_YES && m->choice_focus == 1 && (key == KEY_LEFT || key == KEY_RIGHT)) {
        choices_open(m, m->choice_kind == 1 ? 2 : 1);
        m->choice_focus = 1;
        return;
    }
    if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
        if (!m->choice_focus) {
            if (key == KEY_DOWN)
                m->choice_focus = 1;
        } else if (m->choice_focus == 2)
            m->choice_focus = key == KEY_UP ? 1 : 0;
        else if (key == KEY_UP) {
            if (m->choice_selected)
                --m->choice_selected;
            else
                m->choice_focus = 0;
        } else if (m->choice_selected + 1 < m->choice_count)
            ++m->choice_selected;
        return;
    }
    if (type == KEY_CODE_YES && m->choice_focus == 1 &&
        (key == KEY_HOME || key == KEY_END || key == KEY_PPAGE || key == KEY_NPAGE)) {
        if (key == KEY_HOME)
            m->choice_selected = 0;
        else if (key == KEY_END)
            m->choice_selected = m->choice_count ? m->choice_count - 1 : 0;
        else if (key == KEY_PPAGE)
            m->choice_selected = m->choice_selected > 5 ? m->choice_selected - 5 : 0;
        else if (m->choice_count)
            m->choice_selected = MIN(m->choice_selected + 5, m->choice_count - 1);
        return;
    }
    if (!m->choice_focus ||
        (type == OK && (iswprint(key) || key == 3 || key == 21 || key == 127)) ||
        (type == KEY_CODE_YES && (key == KEY_BACKSPACE || key == KEY_DC))) {
        if (text_edit(&m->choice_query, type, key, TEXT_LIMIT * 2 + 1, false)) {
            m->choice_focus = 0;
            m->choice_selected = m->choice_first = 0;
        }
    }
}
bool r8_modal_key(r8_modal *m, int type, uint32_t key)
{
    if (!m || !m->open)
        return false;
    if (m->locating) {
        if (type == OK && key == 27) {
            history_cancel_jump(m->h);
            inbox_cancel_open(history_inbox(m->h));
            m->locating = m->location_started = m->location_failed = false;
            r8_modal_destination_clear(m);
        } else if (m->location_failed &&
                   ((type == OK && (key == '\n' || key == '\r' || key == 18)) ||
                    (type == KEY_CODE_YES && key == KEY_ENTER))) {
            snprintf(m->destination, sizeof(m->destination), "%s", m->location_channel);
            snprintf(m->destination_message, sizeof(m->destination_message), "%s",
                     m->location_message);
            snprintf(m->destination_time, sizeof(m->destination_time), "%s", m->location_time);
            begin_location(m);
        }
        return true;
    }
    if (m->kind == R8_PINNED || m->kind == R8_SEARCH) {
        message_browser_key(m->browser, type, key);
        if (*message_browser_destination(m->browser)) {
            snprintf(m->destination, sizeof(m->destination), "%s",
                     message_browser_destination(m->browser));
            snprintf(m->destination_message, sizeof(m->destination_message), "%s",
                     message_browser_destination_message(m->browser));
            snprintf(m->destination_time, sizeof(m->destination_time), "%s",
                     message_browser_destination_time(m->browser));
            message_browser_destination_clear(m->browser);
            begin_location(m);
        }
        return true;
    }
    update(m);
    m->selected_id[0] = 0;
    if (type == KEY_CODE_YES && key == KEY_RESIZE) {
        if (m->window) {
            delwin(m->window);
            m->window = NULL;
        }
        return true;
    }
    if (type == OK && key == 27) {
        if (m->choosing_conversation) {
            m->choosing_conversation = false;
            m->focus = 4;
        } else if (m->prefs) {
            m->prefs = false;
            m->focus = 1;
        } else if (m->detail) {
            m->detail = false;
            m->focus = 0;
        } else
            r8_modal_close(m);
        return true;
    }
    if (type == OK && key == 18) {
        inbox_retry(history_inbox(m->h));
        if (m->kind == R8_USERS)
            history_users_refresh(m->h);
        m->notice[0] = 0;
        return true;
    }
    if (m->choosing_conversation) {
        choices_key(m, type, key);
        return true;
    }
    if (type == KEY_CODE_YES && key == KEY_MOUSE) {
        MEVENT e;
        if (ui_mouse_get(&e) != OK || !m->window)
            return true;
        int y = e.y, x = e.x;
        if (!wmouse_trafo(m->window, &y, &x, false)) {
            if (e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))
                r8_modal_close(m);
            return true;
        }
        if (e.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
            if (m->kind == R8_NOTIFICATIONS && !m->detail && !m->prefs) {
                m->focus = 0;
                if ((e.bstate & BUTTON4_PRESSED) && m->selected)
                    --m->selected;
                else if ((e.bstate & BUTTON5_PRESSED) && m->selected + 1 < m->count)
                    ++m->selected;
                return true;
            }
            return r8_modal_key(m, KEY_CODE_YES, e.bstate & BUTTON4_PRESSED ? KEY_UP : KEY_DOWN);
        }
        if (!(e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)))
            return true;
        if (m->kind == R8_NOTIFICATIONS && !m->detail && !m->prefs && y == 2) {
            m->focus = 3;
            text_click(&m->filter, TEXT_LIMIT * 2 + 1, 0, x - 10 + m->field_first);
        } else if (m->kind == R8_NOTIFICATIONS && !m->detail && !m->prefs && y == 3) {
            char label[2200];
            filter_label(m, label, sizeof(label));
            int name_x = name_button_column(label, getmaxx(m->window));
            if (x >= name_x + 6 && x < date_button_column(getmaxx(m->window)))
                return true;
            m->focus = x < name_x ? 4 : x < name_x + 6 ? 6 : 5;
            if (m->focus == 4)
                cycle_filter(m, false);
            else
                activate(m);
        } else if (m->prefs && (y == 3 || y == 5)) {
            m->focus = y == 3 ? 0 : 1;
            activate(m);
        } else if (m->detail && y == getmaxy(m->window) - 4)
            activate(m);
        else if (!m->detail && !m->prefs && y == getmaxy(m->window) - 4 &&
                 m->kind == R8_NOTIFICATIONS) {
            m->focus = x < 20 ? 1 : 2;
            activate(m);
        } else if (!m->detail && !m->prefs && y >= 4 &&
                   y < getmaxy(m->window) - (m->kind == R8_NOTIFICATIONS ? 6 : 5) &&
                   m->first + (size_t)(y - 4) < m->count) {
            m->selected = m->first + (size_t)(y - 4);
            m->focus = 0;
            activate(m);
        }
        return true;
    }
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
        if (m->kind == R8_NOTIFICATIONS && !m->prefs && !m->detail) {
            const int order[] = {0, 1, 2, 3, 4, 6, 5};
            for (size_t i = 0; i < G_N_ELEMENTS(order); ++i)
                if (m->focus == order[i]) {
                    m->focus = order[(i + (type == OK ? 1 : G_N_ELEMENTS(order) - 1)) %
                                     G_N_ELEMENTS(order)];
                    break;
                }
        } else {
            int n = m->prefs ? 2 : 1;
            m->focus = (m->focus + (type == OK ? 1 : n - 1)) % n;
        }
        return true;
    }
    bool enter =
        (type == OK && (key == '\n' || key == '\r')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (enter || (type == OK && key == ' ' && m->prefs)) {
        activate(m);
        return true;
    }
    if (m->kind == R8_HELP)
        return true;
    if (m->detail) {
        if (type == KEY_CODE_YES && (key == KEY_DOWN || key == KEY_NPAGE))
            m->detail_scroll += key == KEY_DOWN ? 1 : 5;
        if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_PPAGE))
            m->detail_scroll = m->detail_scroll > 5 ? m->detail_scroll - 5 : 0;
        return true;
    }
    if (m->prefs) {
        if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN))
            m->focus = key == KEY_UP ? 0 : 1;
        return true;
    }
    if (m->kind == R8_NOTIFICATIONS && type == KEY_CODE_YES &&
        (key == KEY_LEFT || key == KEY_RIGHT) && m->focus != 3) {
        if (m->focus == 0 || m->focus == 4) {
            m->focus = 4;
            cycle_filter(m, key == KEY_LEFT);
        } else if (m->focus == 5) {
            m->oldest_first = !m->oldest_first;
            m->selected = m->first = 0;
        } else if (m->focus == 1 || m->focus == 2)
            m->focus = m->focus == 1 ? 2 : 1;
        else if (m->focus == 6)
            m->focus = key == KEY_LEFT ? 4 : 5;
        return true;
    }
    if (m->kind == R8_NOTIFICATIONS && type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
        if (m->focus == 3) {
            if (key == KEY_DOWN)
                m->focus = 4;
            return true;
        }
        if (m->focus == 4 || m->focus == 5 || m->focus == 6) {
            m->focus = key == KEY_UP ? 3 : 0;
            return true;
        }
        if (m->focus == 1 || m->focus == 2) {
            m->focus = key == KEY_UP ? 0 : 3;
            return true;
        }
        if (key == KEY_UP && !m->selected) {
            m->focus = 4;
            return true;
        }
    }
    if (m->kind == R8_NOTIFICATIONS && type == KEY_CODE_YES && m->focus != 3 &&
        (key == KEY_HOME || key == KEY_END || key == KEY_PPAGE || key == KEY_NPAGE)) {
        m->focus = 0;
    }
    if (m->kind == R8_NOTIFICATIONS && m->focus == 3 &&
        (type != KEY_CODE_YES ||
         (key != KEY_UP && key != KEY_DOWN && key != KEY_NPAGE && key != KEY_PPAGE))) {
        if (text_edit(&m->filter, type, key, TEXT_LIMIT * 2 + 1, false))
            m->selected = m->first = 0;
        return true;
    }
    if (m->kind == R8_NOTIFICATIONS && m->focus == 3 && type == KEY_CODE_YES &&
        (key == KEY_PPAGE || key == KEY_NPAGE))
        m->focus = 0;
    if (type == KEY_CODE_YES && key == KEY_UP && m->selected)
        --m->selected;
    else if (type == KEY_CODE_YES && key == KEY_DOWN && m->selected + 1 < m->count)
        ++m->selected;
    else if (type == KEY_CODE_YES && key == KEY_HOME)
        m->selected = 0;
    else if (type == KEY_CODE_YES && key == KEY_END && m->count)
        m->selected = m->count - 1;
    else if (type == KEY_CODE_YES && key == KEY_PPAGE)
        m->selected = m->selected > 5 ? m->selected - 5 : 0;
    else if (type == KEY_CODE_YES && key == KEY_NPAGE && m->count) {
        m->selected += 5;
        if (m->selected >= m->count)
            m->selected = m->count - 1;
    } else if ((m->kind == R8_USERS || m->kind == R8_NOTIFICATIONS) &&
               ((type == OK && iswprint(key)) ||
                (type == KEY_CODE_YES &&
                 (m->kind == R8_USERS || key == KEY_BACKSPACE || key == KEY_DC)) ||
                (type == OK && (key == 3 || key == 21 || key == 127)))) {
        if (text_edit(&m->filter, type, key, m->kind == R8_NOTIFICATIONS ? TEXT_LIMIT * 2 + 1 : 60,
                      false)) {
            m->selected = m->first = 0;
            if (m->kind == R8_NOTIFICATIONS)
                m->focus = 3;
        }
    }
    return true;
}
static void choices_draw(r8_modal *m, WINDOW *w, int width, int height)
{
    wattrset(w, COLOR_PAIR(m->choice_focus == 0 ? THEME_FOCUSED : THEME_PANEL));
    text_label(w, 2, 2, 6, "Nome: ");
    text_draw_field(w, &m->choice_query, &m->choice_field_first, 2, 8, width - 10, false);
    button_draw(w, 3, 2, width / 2 - 3, "[Canais]",
                m->choice_kind == 1 ? BUTTON_SELECTED : BUTTON_NORMAL, BUTTON_PANEL);
    button_draw(w, 3, width / 2, width - width / 2 - 2, "[DMs]",
                m->choice_kind == 2 ? BUTTON_SELECTED : BUTTON_NORMAL, BUTTON_PANEL);
    int rows = height - 9;
    if (m->choice_selected < m->choice_first)
        m->choice_first = m->choice_selected;
    if (m->choice_selected >= m->choice_first + (size_t)rows)
        m->choice_first = m->choice_selected - (size_t)rows + 1;
    for (int row = 0; row < rows && m->choice_first + (size_t)row < m->choice_count; ++row) {
        size_t index = m->choice_first + (size_t)row;
        json_t *c = history_channel(m->h, m->users[index]);
        char label[3200];
        if (m->choice_kind == 2)
            snprintf(label, sizeof(label), "%s (@%s)", s(c, "name"),
                     s(json_object_get(c, "user"), "username"));
        else
            snprintf(label, sizeof(label), "%s %s", !strcmp(s(c, "type"), "voice") ? "♪" : "#",
                     s(c, "name"));
        int pair = index != m->choice_selected ? THEME_PANEL
                   : m->choice_focus == 1      ? THEME_FOCUSED
                   : m->choice_focus == 0      ? THEME_PANEL_ACTIVE
                                               : THEME_PANEL;
        if (m->choice_kind == 2)
            pair = theme_member_pair(
                history_member_color(m->h, s(json_object_get(c, "user"), "id")), pair);
        wattrset(w, COLOR_PAIR(pair));
        text_label(w, row + 4, 2, width - 4, label);
        if (index == m->choice_selected)
            snprintf(m->choice_id, sizeof(m->choice_id), "%s", s(c, "id"));
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (!m->choice_count)
        text_label(w, 4, 2, width - 4,
                   m->choice_kind == 2 ? "Nenhuma DM encontrada." : "Nenhum canal encontrado.");
    char total[96];
    snprintf(total, sizeof(total), "%zu %s", m->choice_count,
             m->choice_kind == 2    ? (m->choice_count == 1 ? "DM encontrada" : "DMs encontradas")
             : m->choice_count == 1 ? "canal encontrado"
                                    : "canais encontrados");
    text_label(w, height - 5, 2, width - 4, total);
    button_draw(w, height - 4, 2, width - 4,
                m->choice_kind == 2 ? "[Todas as DMs]" : "[Todos os canais]",
                m->choice_focus == 2 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
}
WINDOW *r8_modal_draw(r8_modal *m)
{
    if (!m || !m->open || COLS < 44 || LINES < 14)
        return NULL;
    if (!m->locating && (m->kind == R8_PINNED || m->kind == R8_SEARCH))
        return message_browser_draw(m->browser);
    int width = COLS - 4;
    if (width > 90)
        width = 90;
    int height = LINES - 2;
    if (height > 25)
        height = 25;
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
    WINDOW *w = m->window;
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    box(w, 0, 0);
    if (m->locating) {
        char loading[192];
        static const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        snprintf(loading, sizeof(loading), "%s Localizando mensagem…",
                 frames[m->location_frame % G_N_ELEMENTS(frames)]);
        wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
        text_label(w, 1, 2, width - 4, "Ir para mensagem");
        text_label(w, height / 2 - 1, 2, width - 4,
                   m->location_failed ? m->location_notice : loading);
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, height / 2 + 1, 2, width - 4,
                   m->location_failed ? "Enter: tentar novamente"
                                      : "Buscando no histórico da conversa");
        text_label(w, height - 2, 2, width - 4, "Esc: voltar");
        wnoutrefresh(w);
        return w;
    }
    update(m);
    const char *title = m->choosing_conversation
                            ? (m->choice_kind == 2 ? "Selecionar DM" : "Selecionar canal")
                        : m->kind == R8_USERS ? "Nova DM"
                        : m->kind == R8_HELP  ? "Ajuda"
                        : m->prefs            ? "Preferências"
                        : m->detail           ? "Detalhes da notificação"
                                              : "Notificações";
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 1, 2, width - 4, title);
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    if (m->choosing_conversation) {
        choices_draw(m, w, width, height);
    } else if (m->kind == R8_HELP) {
        const char *lines[] = {"Tab/Shift+Tab: percorrer áreas",
                               "Canais/DMs: ←→ escolher, Enter ativar",
                               "↑↓: navegar · Enter: abrir texto/DM",
                               "Voz: Shift+Enter/F4 abre histórico",
                               "F2: configurações · F9: notificações · F10: membros",
                               "F3: buscar mensagens · F8: mensagens fixadas",
                               "Ctrl+D na lista DMs: fechar conversa",
                               "Ctrl+R: atualizar ou reconectar",
                               "F6: conta · Ctrl+L: sair da conta",
                               "Esc: voltar · Ctrl+Q: sair"};
        for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]) && (int)i + 3 < height - 2; ++i)
            text_label(w, (int)i + 3, 2, width - 4, lines[i]);
    } else if (m->prefs) {
        button_draw(w, 3, 2, width - 4,
                    m->config && m->config->desktop_notifications ? "[x] Avisos no desktop"
                                                                  : "[ ] Avisos no desktop",
                    m->focus == 0 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        button_draw(w, 5, 2, width - 4,
                    m->config && m->config->notification_preview ? "[x] Mostrar prévia"
                                                                 : "[ ] Mostrar prévia",
                    m->focus == 1 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    } else if (m->detail) {
        bool event;
        json_t *n = detail(m, &event);
        wattrset(w, COLOR_PAIR(theme_member_pair(author_color(m, n, event), THEME_PANEL)));
        text_label(w, 3, 2, width - 4, author(m, n, event));
        wattrset(w, COLOR_PAIR(THEME_PANEL));
        text_buffer content;
        text_layout layout;
        text_decode(&content,
                    n ? s(n, "message_content") : "Notificação indisponível. Atualize a lista.");
        text_mentions(&content, history_mention_name, m->h);
        text_measure(&content, width - 4, &layout);
        size_t max = layout.rows > height - 8 ? (size_t)(layout.rows - (height - 8)) : 0;
        if (m->detail_scroll > max)
            m->detail_scroll = max;
        text_draw(w, &content, &layout, (int)m->detail_scroll, height - 8, 4, 2);
        button_draw(w, height - 4, 2, width - 4, "[Abrir conversa]", BUTTON_FOCUSED, BUTTON_PANEL);
    } else {
        if (m->kind == R8_USERS)
            text_draw_field(w, &m->filter, &m->field_first, 2, 2, width - 4, false);
        else {
            wattrset(w, COLOR_PAIR(m->focus == 3 ? THEME_FOCUSED : THEME_PANEL));
            text_label(w, 2, 2, 8, "Buscar: ");
            text_draw_field(w, &m->filter, &m->field_first, 2, 10, width - 12, false);
            char label[2200];
            filter_label(m, label, sizeof(label));
            int name_x = name_button_column(label, width), date_x = date_button_column(width);
            button_draw(w, 3, 2, name_x - 3, label, m->focus == 4 ? BUTTON_FOCUSED : BUTTON_NORMAL,
                        BUTTON_PANEL);
            button_draw(w, 3, name_x, 6, "[Nome]", m->focus == 6 ? BUTTON_FOCUSED : BUTTON_NORMAL,
                        BUTTON_PANEL);
            button_draw(w, 3, date_x, width - date_x - 2,
                        width < 60        ? (m->oldest_first ? "Antigas ↑" : "Recentes ↓")
                        : m->oldest_first ? "Data: antigas ↑"
                                          : "Data: recentes ↓",
                        m->focus == 5 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        }
        int rows = height - (m->kind == R8_NOTIFICATIONS ? 10 : 9);
        if (rows < 1)
            rows = 1;
        if (m->selected < m->first)
            m->first = m->selected;
        if (m->selected >= m->first + (size_t)rows)
            m->first = m->selected - (size_t)rows + 1;
        for (int row = 0; row < rows && m->first + (size_t)row < m->count; ++row) {
            size_t i = m->first + (size_t)row;
            char label[TEXT_UTF8_BYTES + 4096];
            char member[2200];
            int member_x = 2;
            unsigned color;
            json_t *v;
            if (m->kind == R8_USERS) {
                v = history_user(m->h, m->users[i]);
                snprintf(member, sizeof(member), "%s (@%s)",
                         *s(v, "nickname") ? s(v, "nickname") : s(v, "username"), s(v, "username"));
                color = history_member_color(m->h, s(v, "id"));
                snprintf(label, sizeof(label), "%s (@%s) · %s",
                         *s(v, "nickname") ? s(v, "nickname") : s(v, "username"), s(v, "username"),
                         r8_presence_label(inbox_presence(history_inbox(m->h), s(v, "id"))));
            } else {
                bool event;
                v = notification(m, i, &event);
                snprintf(member, sizeof(member), "%s", author(m, v, event));
                member_x += event ? 8 : json_is_true(json_object_get(v, "read")) ? 0 : 2;
                color = author_color(m, v, event);
                char content[TEXT_UTF8_BYTES];
                projected(m, s(v, "message_content"), content);
                const char *stamp = timestamp(m, &m->rows[i]);
                char date[32] = "sem data";
                GDateTime *time = g_date_time_new_from_iso8601(stamp, NULL);
                if (time) {
                    GDateTime *local = g_date_time_to_local(time);
                    char *formatted = local ? g_date_time_format(local, "%d/%m %H:%M") : NULL;
                    if (formatted)
                        snprintf(date, sizeof(date), "%s", formatted);
                    g_free(formatted);
                    if (local)
                        g_date_time_unref(local);
                    g_date_time_unref(time);
                }
                json_t *c = channel(m, channel_id(m, v, event));
                snprintf(label, sizeof(label), "%s%s · %s · %s%s · %s",
                         event                                      ? "Aviso · "
                         : json_is_true(json_object_get(v, "read")) ? ""
                                                                    : "* ",
                         author(m, v, event), content,
                         json_is_true(json_object_get(c, "_dm")) ? "DM: " : "# ",
                         c ? s(c, "name") : "destino indisponível", date);
            }
            int pair = i == m->selected && m->focus == 0 ? THEME_FOCUSED : THEME_PANEL;
            wattrset(w, COLOR_PAIR(pair));
            text_label(w, row + 4, 2, width - 4, label);
            wattrset(w, COLOR_PAIR(theme_member_pair(color, pair)));
            text_label(w, row + 4, member_x, width - member_x - 2, member);
            if (i == m->selected)
                snprintf(m->selected_id, sizeof(m->selected_id), "%s", s(v, "id"));
        }
        wattrset(w, COLOR_PAIR(THEME_PANEL));
        if (!m->count)
            text_label(w, 4, 2, width - 4,
                       m->kind == R8_USERS ? "Nenhum usuário disponível neste filtro."
                                           : "Nenhuma notificação disponível.");
        if (m->kind == R8_NOTIFICATIONS) {
            char total[128];
            snprintf(total, sizeof(total), "%zu resultados · %zu não lidas conhecidas%s", m->count,
                     inbox_unread(history_inbox(m->h)),
                     inbox_has_more(history_inbox(m->h)) ? " · há mais páginas" : "");
            text_label(w, height - 5, 2, width - 4, total);
            button_draw(w, height - 4, 2, 17, "[Preferências]",
                        m->focus == 1 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
            button_draw(w, height - 4, 21, width - 23, "[Mais antigas]",
                        m->focus == 2 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        }
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    const char *notice = *m->notice            ? m->notice
                         : m->kind == R8_USERS ? history_users_notice(m->h)
                                               : inbox_notice(history_inbox(m->h));
    text_label(w, height - 3, 2, width - 4, notice);
    text_label(w, height - 2, 2, width - 4,
               m->choosing_conversation ? "Digite buscar · ↑↓ escolher · Enter aplicar · Esc voltar"
               : m->kind == R8_NOTIFICATIONS && !m->detail && !m->prefs
                   ? (m->focus == 4 ? "←→ filtro · Enter buscar nome · ↑ busca · ↓ lista · Esc"
                                    : "↑↓ lista · Digite buscar · Tab ações · ←→ filtro · Esc")
                   : "↑↓ escolher · Enter abrir · Ctrl+R tentar · Esc voltar");
    wnoutrefresh(w);
    return w;
}
const char *r8_modal_destination(const r8_modal *m)
{
    return m->destination;
}
const char *r8_modal_destination_message(const r8_modal *m)
{
    return m->destination_message;
}
const char *r8_modal_destination_time(const r8_modal *m)
{
    return m->destination_time;
}
bool r8_modal_locating(const r8_modal *m)
{
    return m && m->open && m->locating;
}
void r8_modal_location_started(r8_modal *m)
{
    if (m && m->locating)
        m->location_started = true;
}
void r8_modal_location_error(r8_modal *m, const char *error)
{
    if (m && m->locating) {
        m->location_failed = true;
        snprintf(m->location_notice, sizeof(m->location_notice), "%s", error);
    }
}
bool r8_modal_poll(r8_modal *m)
{
    if (!r8_modal_locating(m) || m->location_failed)
        return false;
    if (m->location_started && *history_jump_error(m->h)) {
        r8_modal_location_error(m, history_jump_error(m->h));
        return true;
    }
    uint64_t now = 0;
    net_now_ms(&now);
    uint64_t frame = now / 100;
    if (frame == m->location_frame)
        return false;
    m->location_frame = frame;
    return true;
}
void r8_modal_location_complete(r8_modal *m, const char *message)
{
    if (r8_modal_locating(m) && !strcmp(message, m->location_message))
        r8_modal_close(m);
}
bool r8_modal_cursor(r8_modal *m)
{
    if (r8_modal_locating(m))
        return false;
    if (m && (m->kind == R8_PINNED || m->kind == R8_SEARCH))
        return message_browser_cursor(m->browser);
    if (!m || !m->open || m->kind != R8_NOTIFICATIONS || m->detail || m->prefs || !m->window ||
        COLS < 44 || LINES < 14)
        return false;
    if (m->choosing_conversation ? m->choice_focus != 0 : m->focus != 3)
        return false;
    text_buffer *input = m->choosing_conversation ? &m->choice_query : &m->filter;
    int start = m->choosing_conversation ? 8 : 10;
    int first = m->choosing_conversation ? m->choice_field_first : m->field_first;
    text_layout layout;
    text_measure(input, TEXT_LIMIT * 2 + 1, &layout);
    int column = start + layout.cursor_column - first;
    if (column < start || column >= getmaxx(m->window) - 2)
        return false;
    setsyx(getbegy(m->window) + 2, getbegx(m->window) + column);
    return true;
}
void r8_modal_destination_clear(r8_modal *m)
{
    m->destination[0] = 0;
    m->destination_message[0] = 0;
    m->destination_time[0] = 0;
    message_browser_destination_clear(m->browser);
}
