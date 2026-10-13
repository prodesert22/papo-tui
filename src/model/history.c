#define _POSIX_C_SOURCE 200809L
#include "model/history.h"
#include "net/session_time.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum request_kind { NONE, CHANNELS, MESSAGES, AUTHORS };
struct history {
    auth_client *auth;
    json_t *channels, *messages, *authors;
    size_t selected, bytes;
    uint64_t context, selection, request_context, request_selection;
    enum request_kind pending;
    bool want_channels, want_messages, want_authors, older, more, evicted;
    enum request_kind failed;
    char cursor_time[65], cursor_id[37], status[192];
};

bool history_uuid(const char *value)
{
    if (!value || strlen(value) != 36) return false;
    for (size_t i = 0; i < 36; ++i)
        if (i == 8 || i == 13 || i == 18 || i == 23 ? value[i] != '-' :
            !((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f') ||
              (value[i] >= 'A' && value[i] <= 'F'))) return false;
    return true;
}
static const char *str(json_t *obj, const char *key) { return json_string_value(json_object_get(obj, key)); }
static bool string_field(json_t *obj, const char *key, size_t limit, bool optional)
{
    json_t *v = json_object_get(obj, key);
    if (optional && (!v || json_is_null(v))) return true;
    const char *s = json_string_value(v);
    return s && json_string_length(v) <= limit && strlen(s) == json_string_length(v);
}
static void status(history *h, const char *value) { snprintf(h->status, sizeof(h->status), "%s", value); }
static void clear_messages(history *h)
{
    json_decref(h->messages); h->messages = NULL;
    json_decref(h->authors); h->authors = NULL;
    h->bytes = 0; h->more = h->evicted = false;
    h->failed = NONE;
    h->cursor_id[0] = h->cursor_time[0] = '\0';
    h->want_authors = h->want_messages = false;
}
history *history_create(auth_client *auth)
{
    history *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->auth = auth; h->selected = SIZE_MAX;
    h->context = auth_context_generation(auth); h->want_channels = true;
    status(h, "Carregando canais…");
    return h;
}
void history_destroy(history *h)
{
    if (!h) return;
    if (h->pending != NONE) auth_cancel_read(h->auth);
    clear_messages(h); json_decref(h->channels); free(h);
}
size_t history_channel_count(const history *h) { return json_array_size(h->channels); }
json_t *history_channel(const history *h, size_t i) { return json_array_get(h->channels, i); }
size_t history_selected(const history *h) { return h->selected; }
size_t history_message_count(const history *h) { return json_array_size(h->messages); }
json_t *history_message(const history *h, size_t i) { return json_array_get(h->messages, i); }
const char *history_status(const history *h) { return h->status; }
bool history_has_more(const history *h) { return h->more; }
bool history_evicted(const history *h) { return h->evicted; }
bool history_loading(const history *h)
{
    return auth_has_session(h->auth) &&
           (h->want_messages || h->want_channels || h->pending == MESSAGES || h->pending == CHANNELS);
}
const char *history_author(const history *h, const char *id)
{
    const char *name = id ? json_string_value(json_object_get(h->authors, id)) : NULL;
    if (name) return name;
    if (id && auth_has_session(h->auth) && !strcmp(id, auth_user_id(h->auth))) return auth_username(h->auth);
    return "Autor indisponível";
}
void history_reload_channels(history *h)
{
    if (h->pending != NONE) { auth_cancel_read(h->auth); h->pending = NONE; }
    ++h->selection; clear_messages(h); json_decref(h->channels); h->channels = NULL;
    h->selected = SIZE_MAX; h->want_channels = true;
    status(h, "Carregando canais…");
}
bool history_select(history *h, size_t index)
{
    json_t *c = history_channel(h, index);
    if (!c || !strcmp(str(c, "type"), "category")) return false;
    if (h->pending != NONE) { auth_cancel_read(h->auth); h->pending = NONE; }
    ++h->selection; clear_messages(h); h->selected = index; h->older = false;
    if (!strcmp(str(c, "type"), "voice")) status(h, "Chamadas de voz ainda não estão disponíveis.");
    else { h->want_messages = true; status(h, "Carregando histórico…"); }
    return true;
}
void history_recent(history *h)
{
    if (h->selected != SIZE_MAX) history_select(h, h->selected);
}
void history_older(history *h)
{
    if (!h->more || h->want_messages || h->pending != NONE) return;
    h->older = true; h->want_messages = true; status(h, "Carregando mensagens anteriores…");
}
bool history_retry(history *h)
{
    if (h->pending != NONE) return true;
    bool inplace = h->failed == AUTHORS || h->failed == MESSAGES;
    if (h->failed == AUTHORS) h->want_authors = true;
    else if (h->failed == MESSAGES) h->want_messages = true;
    else if (h->selected != SIZE_MAX) history_recent(h);
    else history_reload_channels(h);
    h->failed = NONE;
    status(h, "Carregando…");
    return inplace;
}
static int channel_compare(const void *a, const void *b)
{
    json_t *ca = *(json_t *const *)a, *cb = *(json_t *const *)b;
    json_int_t pa = json_integer_value(json_object_get(ca, "position"));
    json_int_t pb = json_integer_value(json_object_get(cb, "position"));
    return pa < pb ? -1 : pa > pb ? 1 : strcmp(str(ca, "id"), str(cb, "id"));
}
static bool channels_response(history *h, json_t *body)
{
    json_t *array = json_object_get(body, "channels");
    size_t count = json_array_size(array);
    if (!json_is_array(array) || count > HISTORY_CHANNEL_LIMIT) return false;
    json_t *sorted[HISTORY_CHANNEL_LIMIT];
    for (size_t i = 0; i < count; ++i) {
        json_t *c = sorted[i] = json_array_get(array, i);
        if (!history_uuid(str(c, "id")) || !string_field(c, "name", 2048, false) ||
            !string_field(c, "topic", 2048, true) || !string_field(c, "type", 16, false) ||
            !json_is_integer(json_object_get(c, "position"))) return false;
        const char *type = str(c, "type");
        if (strcmp(type, "text") && strcmp(type, "voice") && strcmp(type, "category")) return false;
        for (size_t j = 0; j < i; ++j) if (!strcmp(str(sorted[j], "id"), str(c, "id"))) return false;
    }
    qsort(sorted, count, sizeof(*sorted), channel_compare);
    json_t *next = json_array();
    if (!next) return false;
    for (size_t i = 0; i < count; ++i)
        if (json_array_append(next, sorted[i])) { json_decref(next); return false; }
    json_decref(h->channels); h->channels = next;
    status(h, count ? "Escolha um canal para ler." : "Nenhum canal disponível.");
    return true;
}
static bool message_valid(json_t *m, const char *channel)
{
    uint64_t time;
    if (!history_uuid(str(m, "id")) || !history_uuid(str(m, "author_id")) ||
        !str(m, "channel_id") || strcmp(str(m, "channel_id"), channel) ||
        !string_field(m, "created_at", 64, false) || !session_time_parse(str(m, "created_at"), &time) ||
        !string_field(m, "content", 32768, true) || !string_field(m, "edited_at", 64, true) ||
        !string_field(m, "reply_to", 36, true)) return false;
    if (str(m, "reply_to") && !history_uuid(str(m, "reply_to"))) return false;
    if (str(m, "edited_at") && !session_time_parse(str(m, "edited_at"), &time)) return false;
    const char *lists[] = {"attachments", "embeds", "reactions", "user_reactions"};
    for (size_t i = 0; i < 4; ++i) {
        json_t *a = json_object_get(m, lists[i]);
        if (a && !json_is_null(a) && !json_is_array(a)) return false;
        if (i < 2 && json_array_size(a) > 10) return false;
    }
    return true;
}
static size_t encoded_size(json_t *m)
{
    char *encoded = json_dumps(m, JSON_COMPACT);
    if (!encoded) return SIZE_MAX;
    size_t length = strlen(encoded); free(encoded); return length;
}
static bool contains(json_t *array, const char *id)
{
    size_t i; json_t *m;
    json_array_foreach(array, i, m) if (!strcmp(str(m, "id"), id)) return true;
    return false;
}
/* Compare absolute seconds through the existing RFC3339 parser, retaining
 * every fractional digit for ordering. Padding with zeros handles mixed
 * precision; offsets are already accounted for by session_time_parse(). */
static int message_compare(json_t *a, json_t *b)
{
    const char *at = str(a, "created_at"), *bt = str(b, "created_at");
    uint64_t ams, bms;
    if (!session_time_parse(at, &ams) || !session_time_parse(bt, &bms)) return 0;
    if (ams / 1000 != bms / 1000) return ams / 1000 < bms / 1000 ? -1 : 1;
    const char *af = at + 19, *bf = bt + 19;
    if (*af == '.') ++af;
    if (*bf == '.') ++bf;
    while ((*af >= '0' && *af <= '9') || (*bf >= '0' && *bf <= '9')) {
        bool ad = *af >= '0' && *af <= '9', bd = *bf >= '0' && *bf <= '9';
        char av = ad ? *af++ : '0', bv = bd ? *bf++ : '0';
        if (av != bv) return av < bv ? -1 : 1;
    }
    return strcmp(str(a, "id"), str(b, "id"));
}
bool history_confirm(history *h, json_t *message, uint64_t generation)
{
    if (!h || !auth_has_session(h->auth) || generation != h->context ||
        generation != auth_context_generation(h->auth)) return false;
    const char *channel = str(history_channel(h, h->selected), "id");
    if (!channel || !message_valid(message, channel)) return false;
    if (contains(h->messages, str(message, "id"))) return true;
    json_t *confirmed = json_deep_copy(message), *next = json_array();
    if (!confirmed || !next) { json_decref(confirmed); json_decref(next); return false; }
    size_t count = history_message_count(h), bytes = 0;
    bool inserted = false;
    for (size_t i = 0; i < count; ++i) {
        json_t *m = history_message(h, i);
        if (!inserted && message_compare(confirmed, m) < 0) {
            if (json_array_append(next, confirmed)) goto fail;
            inserted = true;
        }
        if (json_array_append(next, m)) goto fail;
    }
    if (!inserted && json_array_append(next, confirmed)) goto fail;
    size_t i; json_t *m;
    json_array_foreach(next, i, m) {
        size_t size = encoded_size(m);
        if (size == SIZE_MAX || SIZE_MAX - bytes < size) goto fail;
        bytes += size;
    }
    bool evicted = false;
    while (json_array_size(next) > HISTORY_MESSAGE_LIMIT || bytes > HISTORY_BYTE_LIMIT) {
        size_t size = encoded_size(json_array_get(next, 0));
        if (size == SIZE_MAX || json_array_remove(next, 0)) goto fail;
        bytes -= size; evicted = true;
    }
    json_decref(h->messages); h->messages = next; h->bytes = bytes;
    h->evicted |= evicted;
    if (evicted && json_array_size(next)) {
        h->more = true;
        json_t *oldest = json_array_get(next, 0);
        snprintf(h->cursor_time, sizeof(h->cursor_time), "%s", str(oldest, "created_at"));
        snprintf(h->cursor_id, sizeof(h->cursor_id), "%s", str(oldest, "id"));
    }
    h->want_authors = true;
    status(h, "Mensagem confirmada. Ctrl+R: consultar novas mensagens.");
    json_decref(confirmed);
    return true;
fail:
    json_decref(confirmed); json_decref(next); return false;
}
static bool messages_response(history *h, json_t *body)
{
    const char *channel = str(history_channel(h, h->selected), "id");
    json_t *page = json_object_get(body, "messages"), *more = json_object_get(body, "has_more");
    size_t count = json_array_size(page);
    if (!channel || !str(body, "channel_id") || strcmp(channel, str(body, "channel_id")) ||
        !json_is_array(page) || count > 100 || !json_is_boolean(more)) return false;
    for (size_t i = 0; i < count; ++i) if (!message_valid(json_array_get(page, i), channel)) return false;
    /* Preserve the server's exact timestamp precision and ordering. Reverse its
     * descending page; do not sort timestamps lexically or truncate fractions. */
    if (count && h->older && !strcmp(h->cursor_id, str(json_array_get(page, count - 1), "id"))) return false;
    json_t *next = json_array();
    if (!next) return false;
    size_t added = 0, bytes = 0;
    for (size_t i = count; i > 0; --i) {
        json_t *m = json_array_get(page, i - 1);
        if (contains(next, str(m, "id")) || contains(h->messages, str(m, "id"))) continue;
        if (json_array_append(next, m)) goto fail;
        ++added;
    }
    if (h->older && count && !added) goto fail;
    size_t i; json_t *m;
    json_array_foreach(h->messages, i, m) if (json_array_append(next, m)) goto fail;
    json_array_foreach(next, i, m) {
        size_t size = encoded_size(m);
        if (size == SIZE_MAX || SIZE_MAX - bytes < size) goto fail;
        bytes += size;
    }
    while (json_array_size(next) > HISTORY_MESSAGE_LIMIT || bytes > HISTORY_BYTE_LIMIT) {
        size_t last = json_array_size(next) - 1;
        size_t size = encoded_size(json_array_get(next, last));
        if (size == SIZE_MAX) goto fail;
        bytes -= size; json_array_remove(next, last);
    }
    bool evicted = json_array_size(next) < added + history_message_count(h);
    json_decref(h->messages); h->messages = next; h->bytes = bytes; h->evicted |= evicted;
    h->more = count && json_is_true(more); /* Empty pages terminate safely. */
    if (count) {
        m = json_array_get(page, count - 1);
        snprintf(h->cursor_time, sizeof(h->cursor_time), "%s", str(m, "created_at"));
        snprintf(h->cursor_id, sizeof(h->cursor_id), "%s", str(m, "id"));
        if (!h->older) {
            json_t *c = history_channel(h, h->selected), *latest = json_array_get(page, 0);
            const char *listed = str(json_object_get(c, "last_message"), "id");
            /* Only clear the known indicator when this GET covers its ID.
             * Older pages never regress the cursor or guess a newer read. */
            if (listed && !strcmp(listed, str(latest, "id")))
                json_object_set(c, "last_read_message", json_object_get(latest, "id"));
        }
    }
    h->want_authors = history_message_count(h) > 0;
    status(h, history_message_count(h) ? h->more ? "PageUp no início: carregar anteriores." : "Início do histórico alcançado." : "Nenhuma mensagem neste canal.");
    return true;
fail:
    json_decref(next); return false;
}
static bool authors_response(history *h, json_t *body)
{
    if (!json_is_array(body) || json_array_size(body) > 1000) return false;
    json_t *names = json_object();
    if (!names) return false;
    size_t i; json_t *u;
    json_array_foreach(body, i, u) {
        if (!history_uuid(str(u, "id")) || !string_field(u, "username", 1024, false) ||
            !string_field(u, "nickname", 1024, true)) { json_decref(names); return false; }
        const char *name = str(u, "nickname");
        if (!name || !*name) name = str(u, "username");
        if (json_object_set_new(names, str(u, "id"), json_string(name))) { json_decref(names); return false; }
    }
    json_decref(h->authors); h->authors = names; return true;
}
static bool request_messages(history *h)
{
    const char *id = str(history_channel(h, h->selected), "id");
    if (!id) return false;
    char path[512];
    if (h->older) {
        char *timestamp = curl_easy_escape(NULL, h->cursor_time, 0);
        if (!timestamp) return false;
        snprintf(path, sizeof(path), "/channels/%s/messages?order=desc&since=%s&last_id=%s", id, timestamp, h->cursor_id);
        curl_free(timestamp);
    } else snprintf(path, sizeof(path), "/channels/%s/messages?order=desc", id);
    return auth_get_json(h->auth, path);
}
static bool request_authors(history *h)
{
    json_t *ids = json_array(), *seen = json_object();
    if (!ids || !seen) { json_decref(ids); json_decref(seen); return false; }
    size_t i; json_t *m; bool ok = true;
    json_array_foreach(h->messages, i, m) {
        const char *id = str(m, "author_id");
        if (!json_object_get(seen, id) && (json_array_append_new(ids, json_string(id)) ||
            json_object_set_new(seen, id, json_true()))) { ok = false; break; }
    }
    if (ok) ok = auth_user_summaries(h->auth, ids);
    json_decref(ids); json_decref(seen); return ok;
}
bool history_poll(history *h)
{
    bool changed = false;
    if (h->context != auth_context_generation(h->auth)) {
        h->context = auth_context_generation(h->auth); history_reload_channels(h); h->pending = NONE; changed = true;
    }
    if (auth_response_ready(h->auth)) {
        long code; uint64_t generation;
        json_t *body = auth_take_response(h->auth, &code, &generation);
        if (h->pending != NONE && generation == h->context && h->request_context == h->context &&
            h->request_selection == h->selection) {
            bool ok = code == 200 && (h->pending == CHANNELS ? channels_response(h, body) :
                         h->pending == MESSAGES ? messages_response(h, body) : authors_response(h, body));
            if (!ok) {
                h->failed = h->pending;
                if (h->pending == AUTHORS) status(h, "Autores indisponíveis. Ctrl+R: tentar novamente.");
                else status(h, code == 403 ? "Sem permissão para ler este canal." : code == 404 ? "Canal removido ou indisponível." :
                     code == 429 ? "Limite de requisições atingido. Aguarde e use Ctrl+R." : "Falha ao carregar. Ctrl+R: tentar novamente.");
            } else h->failed = NONE;
            changed = true;
        }
        json_decref(body); h->pending = NONE;
    }
    if (h->pending == NONE && auth_has_session(h->auth) && !auth_busy(h->auth)) {
        enum request_kind kind = h->want_channels ? CHANNELS : h->want_messages ? MESSAGES : h->want_authors ? AUTHORS : NONE;
        bool started = kind == CHANNELS ? auth_get_json(h->auth, "/channels") :
                       kind == MESSAGES ? request_messages(h) : kind == AUTHORS ? request_authors(h) : false;
        if (started) {
            h->pending = kind; h->request_context = h->context; h->request_selection = h->selection;
            if (kind == CHANNELS) h->want_channels = false;
            else if (kind == MESSAGES) h->want_messages = false;
            else h->want_authors = false;
            changed = true;
        } else if (kind != NONE && auth_get_state(h->auth) == AUTH_READY && !auth_response_ready(h->auth)) {
            h->failed = kind;
            h->want_channels = h->want_messages = h->want_authors = false;
            status(h, "Não foi possível preparar a consulta. Ctrl+R: tentar novamente.");
            changed = true;
        }
    }
    return changed;
}
