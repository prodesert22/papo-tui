#include "model/conversation/discovery.h"
#include "model/conversation/private.h"

/* Auxiliary reads belong to history's single REST dispatcher. The UI never
 * takes transport responses. Cancellation only touches this owner's request. */
enum discovery_request { DISCOVERY_IDLE, DISCOVERY_SEARCH, DISCOVERY_PINS };
struct history_discovery {
    json_t *pins, *results, *filters;
    bool pins_valid, pins_visible, want_pins, want_search, more, limited;
    enum discovery_request request;
    char cursor_time[65], cursor_id[37];
    char pin_notice[192], search_notice[192];
};
static const char *s(const json_t *o, const char *key)
{
    const char *v = json_string_value(json_object_get(o, key));
    return v ? v : "";
}
static struct history_discovery *discovery(history *h)
{
    if (!h->discovery) {
        struct history_discovery *d = calloc(1, sizeof(*d));
        if (!d)
            return NULL;
        d->pins = json_array();
        d->results = json_array();
        if (!d->pins || !d->results) {
            json_decref(d->pins);
            json_decref(d->results);
            free(d);
            return NULL;
        }
        h->discovery = d;
    }
    return h->discovery;
}
void history_internal_discovery_destroy(history *h)
{
    struct history_discovery *d = h->discovery;
    if (!d)
        return;
    json_decref(d->pins);
    json_decref(d->results);
    json_decref(d->filters);
    free(d);
    h->discovery = NULL;
}
static void cancel(history *h)
{
    if (h->pending == DISCOVERY) {
        auth_cancel_read(h->auth);
        h->pending = NONE;
    }
    if (h->discovery)
        h->discovery->request = DISCOVERY_IDLE;
}
void history_discovery_close(history *h)
{
    cancel(h);
    if (h->discovery) {
        h->discovery->pins_visible = false;
        h->discovery->want_pins = h->discovery->want_search = false;
    }
}
void history_internal_discovery_recover(history *h)
{
    if (h->discovery && (h->discovery->request == DISCOVERY_SEARCH || h->discovery->want_search))
        snprintf(h->discovery->search_notice, sizeof(h->discovery->search_notice),
                 "Busca interrompida. Ctrl+R: tentar novamente.");
    cancel(h);
    if (h->discovery) {
        h->discovery->want_search = false;
        history_internal_pins_invalidate(h);
    }
}
static bool date_valid(const char *value)
{
    if (strlen(value) != 10 || value[4] != '-' || value[7] != '-')
        return false;
    for (size_t i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && !g_ascii_isdigit(value[i]))
            return false;
    unsigned y = (unsigned)strtoul(value, NULL, 10);
    unsigned m = (unsigned)strtoul(value + 5, NULL, 10);
    unsigned d = (unsigned)strtoul(value + 8, NULL, 10);
    return y && y <= 9999 && g_date_valid_dmy(d, m, y);
}
static bool valid_filters(const json_t *filters)
{
    if (!json_is_object(filters))
        return false;
    bool criterion = false;
    const char *key;
    json_t *v;
    json_object_foreach((json_t *)filters, key, v)
    {
        const char *value = json_string_value(v);
        if (!strcmp(key, "contains_attachment")) {
            if (!json_is_boolean(v))
                return false;
        } else {
            if (!value || !*value)
                return false;
            if (!strcmp(key, "text")) {
                if (strlen(value) > 8192)
                    return false;
                bool nonspace = false;
                for (const char *p = value; *p; p = g_utf8_next_char(p))
                    nonspace |= !g_unichar_isspace(g_utf8_get_char(p));
                if (!nonspace)
                    return false;
            } else if (!strcmp(key, "author") || !strcmp(key, "channel_id") ||
                       !strcmp(key, "mention")) {
                if (!history_uuid(value))
                    return false;
            } else if (!strcmp(key, "has")) {
                if (strcmp(value, "link"))
                    return false;
            } else if (!strcmp(key, "order")) {
                if (strcmp(value, "asc") && strcmp(value, "desc"))
                    return false;
            } else if (!strcmp(key, "date_start") || !strcmp(key, "date_end")) {
                if (!date_valid(value))
                    return false;
            } else
                return false;
        }
        criterion |= strcmp(key, "order") != 0;
    }
    return criterion && (!*s(filters, "date_start") || !*s(filters, "date_end") ||
                         strcmp(s(filters, "date_start"), s(filters, "date_end")) <= 0);
}
bool history_search(history *h, const json_t *filters)
{
    if (!h || !h->active || !auth_has_session(h->auth))
        return false;
    struct history_discovery *d = discovery(h);
    if (!d)
        return false;
    if (!valid_filters(filters)) {
        snprintf(d->search_notice, sizeof(d->search_notice),
                 "Confira os filtros e datas (AAAA-MM-DD); informe ao menos um critério.");
        return false;
    }
    json_t *copy = json_deep_copy(filters);
    if (!copy)
        return false;
    cancel(h);
    json_decref(d->filters);
    d->filters = copy;
    json_array_clear(d->results);
    d->cursor_time[0] = d->cursor_id[0] = 0;
    d->more = d->limited = false;
    d->want_search = true;
    snprintf(d->search_notice, sizeof(d->search_notice), "Buscando mensagens…");
    return true;
}
void history_search_more(history *h)
{
    struct history_discovery *d = h->discovery;
    if (d && d->filters && d->more && !d->limited && d->request != DISCOVERY_SEARCH) {
        d->want_search = true;
        snprintf(d->search_notice, sizeof(d->search_notice), "Buscando mensagens…");
    }
}
json_t *history_search_results(history *h)
{
    return h->discovery ? h->discovery->results : NULL;
}
bool history_search_has_more(history *h)
{
    return h->discovery && h->discovery->more && !h->discovery->limited;
}
const char *history_search_notice(history *h)
{
    return h->discovery ? h->discovery->search_notice
                        : "Informe texto ou filtros e selecione Buscar.";
}
void history_pins_open(history *h)
{
    struct history_discovery *d = discovery(h);
    if (!d)
        return;
    d->pins_visible = true;
    if (!d->pins_valid && d->request != DISCOVERY_PINS)
        history_pins_refresh(h);
}
void history_pins_refresh(history *h)
{
    struct history_discovery *d = discovery(h);
    if (d) {
        d->pins_visible = true;
        if (!history_uuid(s(history_channel(h, h->selected), "id"))) {
            d->want_pins = false;
            snprintf(d->pin_notice, sizeof(d->pin_notice),
                     "Abra uma conversa para ver as fixadas.");
            return;
        }
        d->want_pins = true;
        snprintf(d->pin_notice, sizeof(d->pin_notice), "Carregando fixadas…");
    }
}
json_t *history_pins(history *h)
{
    return h->discovery ? h->discovery->pins : NULL;
}
int history_pin_state(history *h, const char *message)
{
    struct history_discovery *d = h->discovery;
    if (!d || !d->pins_valid)
        return -1;
    return history_internal_contains(d->pins, message) ? 1 : 0;
}
const char *history_pins_notice(history *h)
{
    return h->discovery ? h->discovery->pin_notice : "";
}
void history_internal_pins_invalidate(history *h)
{
    struct history_discovery *d = h->discovery;
    if (d) {
        d->pins_valid = false;
        bool visible = d->pins_visible && history_uuid(s(history_channel(h, h->selected), "id"));
        d->want_pins |= visible;
        if (visible)
            snprintf(d->pin_notice, sizeof(d->pin_notice), "Atualizando fixadas…");
    }
}
bool history_internal_discovery_event(history *h, const json_t *event)
{
    const char *type = s(event, "type");
    if (!strcmp(type, "message_pin")) {
        if (!history_uuid(s(event, "message_id")) ||
            !json_is_boolean(json_object_get(event, "is_pinned")))
            return false;
        /* No channel is provided. Reconcile only the currently requested list;
         * never assign an unknown UUID to a guessed conversation. */
        history_internal_pins_invalidate(h);
    } else if (h->discovery &&
               history_internal_contains(h->discovery->pins, !strcmp(type, "message_embeds_update")
                                                                 ? s(event, "message_id")
                                                                 : s(event, "id")))
        history_internal_pins_invalidate(h);
    return true;
}
bool history_internal_discovery_dispatch(history *h)
{
    struct history_discovery *d = h->discovery;
    if (!d || !h->active || d->request != DISCOVERY_IDLE)
        return false;
    bool started = false;
    enum discovery_request request = DISCOVERY_IDLE;
    if (d->want_search) {
        request = DISCOVERY_SEARCH;
        started = auth_search_messages(h->auth, d->filters, d->cursor_time, d->cursor_id);
    } else if (d->want_pins && d->pins_visible) {
        const char *channel = s(history_channel(h, h->selected), "id");
        if (!history_uuid(channel))
            return false;
        char path[100];
        snprintf(path, sizeof(path), "/channels/%s/pinned", channel);
        request = DISCOVERY_PINS;
        started = auth_get_json(h->auth, path);
    }
    if (!started) {
        if (request != DISCOVERY_IDLE && auth_get_state(h->auth) == AUTH_READY &&
            !auth_busy(h->auth) && !auth_response_ready(h->auth)) {
            char *notice = request == DISCOVERY_SEARCH ? d->search_notice : d->pin_notice;
            snprintf(notice, 192,
                     "Não foi possível preparar a consulta. Ctrl+R: tentar novamente.");
            if (request == DISCOVERY_SEARCH)
                d->want_search = false;
            else
                d->want_pins = false;
            return true;
        }
        return false;
    }
    d->request = request;
    if (request == DISCOVERY_SEARCH)
        d->want_search = false;
    else
        d->want_pins = false;
    h->pending = DISCOVERY;
    h->request_context = h->context;
    h->request_selection = h->selection;
    return true;
}
static bool result_valid(json_t *v)
{
    uint64_t time;
    return json_is_object(v) && !strcmp(s(v, "type"), "message") && history_uuid(s(v, "id")) &&
           history_uuid(s(v, "channel_id")) && strlen(s(v, "created_at")) < 65 &&
           session_time_parse(s(v, "created_at"), &time) &&
           history_internal_string_field(v, "author_id", 36, true) &&
           (!*s(v, "author_id") || history_uuid(s(v, "author_id"))) &&
           history_internal_string_field(v, "content", 32768, true) &&
           history_internal_string_field(v, "author_username", 1024, true) &&
           history_internal_string_field(v, "channel_name", 2048, true);
}
static bool search_page(history *h, json_t *body)
{
    struct history_discovery *d = h->discovery;
    json_t *page = json_object_get(body, "results");
    if (!json_is_array(page) || json_array_size(page) > 100 ||
        !json_is_boolean(json_object_get(body, "has_more")) ||
        (!json_array_size(page) && json_is_true(json_object_get(body, "has_more"))))
        return false;
    const char *time = d->cursor_time, *id = d->cursor_id;
    bool asc = !strcmp(s(d->filters, "order"), "asc");
    size_t i;
    json_t *v;
    json_array_foreach(page, i, v)
    {
        if (!result_valid(v))
            return false;
        if (*id) {
            int order = history_internal_time_compare(s(v, "created_at"), time);
            if (!order)
                order = strcmp(s(v, "id"), id);
            if (asc ? order <= 0 : order >= 0)
                return false;
        }
        time = s(v, "created_at");
        id = s(v, "id");
    }
    json_t *next = json_deep_copy(d->results);
    if (!next)
        return false;
    size_t bytes = history_internal_encoded_size(next);
    if (bytes == SIZE_MAX) {
        json_decref(next);
        return false;
    }
    bool limited = false;
    json_array_foreach(page, i, v)
    {
        size_t cost = history_internal_encoded_size(v);
        if (cost == SIZE_MAX) {
            json_decref(next);
            return false;
        }
        if (json_array_size(next) >= HISTORY_MESSAGE_LIMIT || cost > HISTORY_BYTE_LIMIT ||
            bytes > HISTORY_BYTE_LIMIT - cost) {
            limited = true;
            break;
        }
        if (!history_internal_contains(next, s(v, "id")) && json_array_append(next, v)) {
            json_decref(next);
            return false;
        }
        bytes += cost;
    }
    json_decref(d->results);
    d->results = next;
    h->want_authors = true;
    if (json_array_size(page)) {
        snprintf(d->cursor_time, sizeof(d->cursor_time), "%s", time);
        snprintf(d->cursor_id, sizeof(d->cursor_id), "%s", id);
    }
    d->more = json_is_true(json_object_get(body, "has_more"));
    d->limited = limited || (d->more && json_array_size(next) >= HISTORY_MESSAGE_LIMIT);
    snprintf(d->search_notice, sizeof(d->search_notice), "%s",
             d->limited              ? "Lista parcial: limite local atingido. Refine a busca."
             : json_array_size(next) ? ""
                                     : "Nenhuma mensagem encontrada.");
    return true;
}
static bool pins_page(history *h, json_t *body)
{
    struct history_discovery *d = h->discovery;
    const char *channel = s(history_channel(h, h->selected), "id");
    json_t *page = json_object_get(body, "pinned");
    if (strcmp(s(body, "channel_id"), channel) || !json_is_array(page) ||
        json_array_size(page) > 100 || history_internal_encoded_size(page) > HISTORY_BYTE_LIMIT)
        return false;
    size_t i;
    json_t *v;
    json_array_foreach(page, i, v)
    {
        if (!history_internal_message_valid(v, channel))
            return false;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(s(json_array_get(page, j), "id"), s(v, "id")))
                return false;
    }
    json_t *copy = json_deep_copy(page);
    if (!copy)
        return false;
    json_decref(d->pins);
    d->pins = copy;
    /* An event arriving during this read queues a second authoritative read. */
    d->pins_valid = !d->want_pins;
    h->want_authors = true;
    snprintf(d->pin_notice, sizeof(d->pin_notice), "%s",
             json_array_size(page) ? "" : "Nenhuma mensagem fixada nesta conversa.");
    return true;
}
void history_internal_discovery_response(history *h, json_t *body, long code)
{
    struct history_discovery *d = h->discovery;
    if (!d)
        return;
    enum discovery_request request = d->request;
    d->request = DISCOVERY_IDLE;
    bool ok =
        code == 200 && (request == DISCOVERY_SEARCH ? search_page(h, body) : pins_page(h, body));
    if (!ok) {
        char *notice = request == DISCOVERY_SEARCH ? d->search_notice : d->pin_notice;
        snprintf(notice, 192, "%s",
                 code == 429   ? "Limite de requisições. Aguarde e use Ctrl+R."
                 : code == 403 ? "Sem permissão para consultar estas mensagens."
                 : code == 404 ? "Conversa não disponível."
                               : "Falha na consulta. Ctrl+R: tentar novamente.");
        /* A failed request needs explicit retry, including a refresh queued by WS. */
        if (request == DISCOVERY_PINS)
            d->want_pins = false;
    }
}
