#define _POSIX_C_SOURCE 200809L
#include "model/history.h"
#include "model/mentions.h"
#include "net/session_time.h"
#include "net/clock.h"
#include "net/retry.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum request_kind { NONE, CHANNELS, MESSAGES, AUTHORS, USERS, EMOJIS, PROFILES };
#define TYPING_LIMIT 128U
#define TYPING_TIMEOUT_MS 6000U
#define TYPING_SEND_MS 2000U
struct history {
    auth_client *auth;
    chat_assets *assets;
    json_t *users, *directory, *queried, *summary_ids;
    size_t user_bytes;
    uint64_t users_revision, messages_revision;
    bool users_active, users_complete, users_failed, users_partial;
    char users_time[65], users_id[37], users_notice[192];
    json_t *channels, *messages;
    json_t *journal, *patches, *fresh, *boundary;
    size_t journal_bytes;
    bool replaying, recovering, ascending;
    uint64_t epoch, insertions, retry_at;
    unsigned failures;
    char recovery_time[65], recovery_id[37];
    size_t selected, bytes;
    uint64_t context, selection, request_context, request_selection;
    enum request_kind pending;
    bool want_channels, want_messages, want_authors, older, more, evicted;
    enum request_kind failed;
    char cursor_time[65], cursor_id[37], status[192];
    char notices[HISTORY_NOTICE_COUNT][192];
    uint64_t notice_sequence, notice_revisions[HISTORY_NOTICE_COUNT];
    const char *event_compact; /* Static diagnostic label, never remote text. */
    struct { char id[37]; uint64_t expires; } typing[TYPING_LIMIT];
    size_t typing_count;
    uint64_t typing_sent;
    bool typing_connected;
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
static bool remember(history *h, const json_t *event);
static void notice(history *h, history_notice_kind kind, const char *value)
{
    if (!strcmp(h->notices[kind], value)) return;
    snprintf(h->notices[kind], sizeof(h->notices[kind]), "%s", value);
    h->notice_revisions[kind] = ++h->notice_sequence;
}
static void status(history *h, const char *value)
{
    snprintf(h->status, sizeof(h->status), "%s", value);
    notice(h, HISTORY_NOTICE_READ, value);
}
static void event_status(history *h, const char *value, const char *compact)
{
    snprintf(h->notices[HISTORY_NOTICE_EVENT], sizeof(h->notices[HISTORY_NOTICE_EVENT]), "%s", value);
    h->notice_revisions[HISTORY_NOTICE_EVENT] = ++h->notice_sequence;
    h->event_compact = compact;
}
static void clear_messages(history *h)
{
    h->typing_count = 0; h->typing_sent = 0;
    ++h->messages_revision;
    json_decref(h->journal); h->journal = NULL; h->journal_bytes = 0;
    json_decref(h->patches); h->patches = NULL;
    json_decref(h->fresh); h->fresh = NULL;
    json_decref(h->boundary); h->boundary = NULL;
    h->recovering = h->ascending = false; h->retry_at = 0; h->failures = 0;
    json_decref(h->messages); h->messages = NULL;
    h->bytes = 0; h->more = h->evicted = false;
    h->failed = NONE;
    h->cursor_id[0] = h->cursor_time[0] = '\0';
    h->want_authors = h->want_messages = false;
    notice(h, HISTORY_NOTICE_READ, "");
    notice(h, HISTORY_NOTICE_EVENT, "");
}
history *history_create(auth_client *auth)
{
    history *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->auth = auth; h->assets = chat_assets_create(); h->selected = SIZE_MAX;
    h->context = auth_context_generation(auth); h->want_channels = true;
    h->users = json_object(); h->directory = json_array(); h->queried = json_object();
    if (!h->assets || !h->users || !h->directory || !h->queried) { history_destroy(h); return NULL; }
    h->epoch = auth_ws_epoch(auth);
    status(h, "Carregando canais…");
    return h;
}
void history_destroy(history *h)
{
    if (!h) return;
    if (h->pending != NONE) auth_cancel_read(h->auth);
    chat_assets_destroy(h->assets); clear_messages(h); json_decref(h->channels); json_decref(h->users); json_decref(h->directory); json_decref(h->queried); json_decref(h->summary_ids); free(h);
}
chat_assets *history_assets(history *h) { return h ? h->assets : NULL; }
size_t history_channel_count(const history *h) { return json_array_size(h->channels); }
json_t *history_channel(const history *h, size_t i) { return json_array_get(h->channels, i); }
size_t history_selected(const history *h) { return h->selected; }
size_t history_message_count(const history *h) { return json_array_size(h->messages); }
json_t *history_message(const history *h, size_t i) { return json_array_get(h->messages, i); }
uint64_t history_messages_revision(const history *h) { return h->messages_revision; }
const char *history_status(const history *h) { return h->status; }
const char *history_notice(const history *h, history_notice_kind kind, uint64_t *revision)
{
    if (!h || kind < 0 || kind >= HISTORY_NOTICE_COUNT) return "";
    if (revision) *revision = h->notice_revisions[kind];
    return h->notices[kind];
}
const char *history_event_compact(const history *h)
{ return h->event_compact ? h->event_compact : ""; }
bool history_has_more(const history *h) { return h->more; }
bool history_evicted(const history *h) { return h->evicted; }
bool history_recovering(const history *h) { return h->recovering; }
uint64_t history_insertions(const history *h) { return h->insertions; }
bool history_loading(const history *h)
{
    return auth_has_session(h->auth) &&
           (h->want_messages || h->want_channels || h->pending == MESSAGES || h->pending == CHANNELS);
}
const char *history_author(const history *h, const char *id)
{
    const char *name = history_mention_name((void *)h, id);
    return name ? name : "Autor indisponível";
}
bool history_message_webhook(const json_t *message)
{
    return history_uuid(str((json_t *)message, "webhook_id"));
}
const char *history_message_author(const history *h, const json_t *message)
{
    if (!history_message_webhook(message)) return history_author(h, str((json_t *)message, "author_id"));
    json_t *external = json_object_get(message, "external_author");
    const char *name = str(external, "name");
    return name && *name && strlen(name) <= 512 && strlen(name) == json_string_length(json_object_get(external, "name")) ? name : "Webhook";
}
void history_users_active(history *h, bool active) { h->users_active = active; }
void history_users_retry(history *h) { h->users_failed = false; }
size_t history_user_count(const history *h) { return json_array_size(h->directory); }
uint64_t history_users_revision(const history *h) { return h->users_revision; }
json_t *history_user(const history *h, size_t i)
{
    return json_object_get(h->users, json_string_value(json_array_get(h->directory, i)));
}
const char *history_users_notice(const history *h)
{
    if (h->users_failed) return "Falha ao listar usuários. Ctrl+R: tentar novamente.";
    if (h->users_partial) return "Lista parcial: limite local atingido.";
    return h->users_complete ? "Tab / Enter / clique: mencionar" : "Carregando usuários…";
}
const char *history_mention_name(void *context, const char *id)
{
    history *h = context;
    if (!h || !id) return NULL;
    json_t *user = json_object_get(h->users, id);
    const char *name = str(user, "nickname");
    if (name && *name) return name;
    name = str(user, "username");
    if (name && *name) return name;
    return !strcmp(id, auth_user_id(h->auth)) ? auth_username(h->auth) : NULL;
}
static bool user_resolved(const history *h, const char *id)
{
    if (!h || !id || !*id) return false;
    const char *name = str(json_object_get(h->users, id), "username");
    return name && *name;
}
static bool cache_user(history *h, json_t *u, bool presence)
{
    const char *id = str(u, "id"), *name = str(u, "username");
    if (!history_uuid(id) || !name || !string_field(u, "username", 1024, false) ||
        !string_field(u, "nickname", 1024, true) || !string_field(u, "typing", 256, true))
        return false;
    json_t *prior = json_object_get(h->users, id);
    bool live = presence || json_is_true(json_object_get(prior, "_presence"));
    json_t *metadata = presence ? u : live ? prior : u;
    const char *nickname = str(metadata, "nickname"), *phrase = str(metadata, "typing");
    size_t old = prior ? strlen(str(prior, "username")) +
                             (str(prior, "nickname") ? strlen(str(prior, "nickname")) : 0) +
                             (str(prior, "typing") ? strlen(str(prior, "typing")) : 0) + 512
                       : 0;
    size_t cost = strlen(name) + (nickname ? strlen(nickname) : 0) + (phrase ? strlen(phrase) : 0) + 512;
    if ((!prior && json_object_size(h->users) >= 10000) ||
        cost > 16U * 1024U * 1024U - (h->user_bytes - old)) {
        h->users_partial = h->users_complete = true;
        return true;
    }
    json_t *copy = json_pack("{s:s,s:s,s:O,s:O,s:b}", "id", id, "username", name, "nickname",
                             json_object_get(metadata, "nickname") ? json_object_get(metadata, "nickname") : json_null(),
                             "typing", json_object_get(metadata, "typing") ? json_object_get(metadata, "typing") : json_null(),
                             "_presence", live);
    if (!copy) return false;
    bool ok = json_object_set_new(h->users, id, copy) == 0;
    if (ok) {
        h->user_bytes = h->user_bytes - old + cost;
        ++h->users_revision;
    }
    return ok;
}
static void typing_remove(history *h, const char *id)
{
    for (size_t i = 0; i < h->typing_count; ++i) if (!strcmp(h->typing[i].id, id)) {
        --h->typing_count;
        memmove(&h->typing[i], &h->typing[i + 1], (h->typing_count - i) * sizeof(h->typing[0]));
        break;
    }
}
void history_typing_text(const history *h, char *text, size_t capacity)
{
    if (!text || !capacity) return;
    *text = '\0';
    if (!h || !h->typing_count) return;
    if (h->typing_count > 4) { snprintf(text, capacity, "Várias pessoas estão digitando…"); return; }
    size_t used = 0;
    for (size_t i = 0; i < h->typing_count; ++i) {
        const char *name = history_mention_name((void *)h, h->typing[i].id);
        int wrote = snprintf(text + used, capacity - used, "%s%s", i ? ", " : "",
                             name ? name : "Usuário indisponível");
        if (wrote < 0 || (size_t)wrote >= capacity - used) return;
        used += (size_t)wrote;
    }
    const char *phrase = h->typing_count == 1 ? str(json_object_get(h->users, h->typing[0].id), "typing") : NULL;
    snprintf(text + used, capacity - used, " %s", phrase && *phrase ? phrase :
             h->typing_count == 1 ? "está digitando…" : "estão digitando…");
}
void history_typing_activity(history *h)
{
    uint64_t now;
    json_t *channel = h ? history_channel(h, h->selected) : NULL;
    if (!h || !auth_has_session(h->auth) || auth_ws_state(h->auth) != WS_OPEN ||
        h->context != auth_context_generation(h->auth) ||
        !str(channel, "type") || strcmp(str(channel, "type"), "text") ||
        !net_now_ms(&now) || (h->typing_sent && now - h->typing_sent < TYPING_SEND_MS)) return;
    json_t *event = json_pack("{s:s,s:s}", "type", "typing", "channel_id", str(channel, "id"));
    if (event && auth_send_event(h->auth, event)) h->typing_sent = now;
    json_decref(event);
}
static bool typing_event(history *h, const json_t *event)
{
    const char *channel = str((json_t *)event, "channel_id"), *id = str((json_t *)event, "user_id");
    if (!string_field((json_t *)event, "channel_id", 36, false) || !history_uuid(channel) ||
        !string_field((json_t *)event, "user_id", 36, false) || !history_uuid(id) ||
        !json_is_boolean(json_object_get(event, "is_typing"))) return false;
    json_t *active = history_channel(h, h->selected);
    if (auth_ws_state(h->auth) != WS_OPEN || !str(active, "id") || strcmp(channel, str(active, "id")) ||
        strcmp(str(active, "type"), "text") || !strcmp(id, auth_user_id(h->auth))) return true;
    if (json_is_false(json_object_get(event, "is_typing"))) { typing_remove(h, id); return true; }
    uint64_t now;
    if (!net_now_ms(&now)) return true;
    size_t i;
    for (i = 0; i < h->typing_count; ++i) if (!strcmp(h->typing[i].id, id)) break;
    if (i == TYPING_LIMIT) return true;
    if (i == h->typing_count) {
        snprintf(h->typing[i].id, sizeof(h->typing[i].id), "%s", id);
        ++h->typing_count;
    }
    h->typing[i].expires = now + TYPING_TIMEOUT_MS;
    json_t *user = json_object_get(h->users, id);
    if ((!user_resolved(h, id) || !json_object_get(user, "typing")) && !json_object_get(h->queried, id))
        h->want_authors = true;
    return true;
}
static bool presence_event(history *h, const json_t *event)
{
    const char *id = str((json_t *)event, "user_id"), *state = str((json_t *)event, "status");
    if (!string_field((json_t *)event, "user_id", 36, false) || !history_uuid(id) ||
        !string_field((json_t *)event, "status", 16, false) ||
        (strcmp(state, "online") && strcmp(state, "offline") && strcmp(state, "away") && strcmp(state, "busy")) ||
        !string_field((json_t *)event, "nickname", 1024, true) ||
        !string_field((json_t *)event, "typing", 256, true)) return false;
    if (!strcmp(state, "offline")) { typing_remove(h, id); return true; }
    json_t *prior = json_object_get(h->users, id);
    json_t *user = json_pack("{s:s,s:s,s:O,s:O,s:b}", "id", id, "username",
                            prior ? str(prior, "username") : "", "nickname",
                            json_object_get(event, "nickname") ? json_object_get(event, "nickname") : json_null(),
                            "typing", json_object_get(event, "typing") ? json_object_get(event, "typing") : json_null(),
                            "_presence", true);
    bool ok = user && cache_user(h, user, true);
    json_decref(user);
    return ok;
}
static bool users_response(history *h, json_t *body)
{
    json_t *users = json_object_get(body, "users"), *more = json_object_get(body, "has_more");
    size_t count = json_array_size(users);
    if (!json_is_array(users) || count > 100 || !json_is_boolean(more)) return false;
    for (size_t i = 0; i < count; ++i) {
        json_t *u = json_array_get(users, i);
        uint64_t stamp;
        if (!str(u, "created_at") || strlen(str(u, "created_at")) >= sizeof(h->users_time) ||
            !session_time_parse(str(u, "created_at"), &stamp) || !cache_user(h, u, false))
            return false;
        const char *id = str(u, "id");
        bool found = false;
        for (size_t j = 0; j < json_array_size(h->directory); ++j)
            if (!strcmp(id, json_string_value(json_array_get(h->directory, j)))) {
                found = true;
                break;
            }
        if (!found && !h->users_partial && json_array_append_new(h->directory, json_string(id))) return false;
    }
    if (count) {
        json_t *last = json_array_get(users, count - 1);
        if (!strcmp(h->users_id, str(last, "id"))) return false;
        snprintf(h->users_time, sizeof(h->users_time), "%s", str(last, "created_at"));
        snprintf(h->users_id, sizeof(h->users_id), "%s", str(last, "id"));
    }
    h->users_complete |= !count || json_is_false(more);
    return true;
}
static bool request_users(history *h)
{
    char path[512] = "/users?order=asc";
    if (*h->users_id) {
        char *time = curl_easy_escape(NULL, h->users_time, 0);
        if (!time) return false;
        snprintf(path, sizeof(path), "/users?order=asc&since=%s&last_id=%s", time, h->users_id);
        curl_free(time);
    }
    return auth_get_json(h->auth, path);
}

void history_reload_channels(history *h)
{
    if (h->pending != NONE) { auth_cancel_read(h->auth); h->pending = NONE; }
    chat_assets_cancel(h->assets);
    ++h->selection; clear_messages(h); json_decref(h->channels); h->channels = NULL;
    h->selected = SIZE_MAX; h->want_channels = true;
    status(h, "Carregando canais…");
}
bool history_select(history *h, size_t index)
{
    json_t *c = history_channel(h, index);
    if (!c || !strcmp(str(c, "type"), "category")) return false;
    if (h->pending != NONE) { auth_cancel_read(h->auth); h->pending = NONE; }
    chat_assets_cancel(h->assets);
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
    if (h->recovering) { h->retry_at = 0; history_recover(h); return true; }
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
    char selected_id[37] = "";
    const char *current = str(history_channel(h, h->selected), "id");
    if (current) snprintf(selected_id, sizeof(selected_id), "%s", current);
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
        if (json_array_append_new(next, json_deep_copy(sorted[i]))) { json_decref(next); return false; }
    json_decref(h->channels); h->channels = next;
    h->selected = SIZE_MAX;
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(selected_id, str(sorted[i], "id"))) h->selected = i;
    if (*selected_id && h->selected == SIZE_MAX) clear_messages(h);
    status(h, count ? "Escolha um canal para ler." : "Nenhum canal disponível.");
    if (count) notice(h, HISTORY_NOTICE_READ, "");
    return true;
}
static bool message_valid(json_t *m, const char *channel)
{
    uint64_t time;
    /* Backend webhooks (papo-backend#26): Message.author_id is nullable in
     * REST (null) and empty ("") in WS MessageOutbound for webhook messages.
     * Accept a missing/null/empty author and validate UUID only when present.
     * webhook_id/external_author are optional display-only attribution. */
    const char *author_id = str(m, "author_id");
    bool author_missing = !author_id || !*author_id;
    json_t *author_value = json_object_get(m, "author_id");
    if (!author_missing && (!string_field(m, "author_id", 36, false) || !history_uuid(author_id)))
        return false;
    if (author_value && !json_is_null(author_value) && !json_is_string(author_value)) return false;
    json_t *webhook_value = json_object_get(m, "webhook_id");
    if (webhook_value && !json_is_null(webhook_value)) {
        const char *webhook_id = json_string_value(webhook_value);
        if (!webhook_id || !*webhook_id) return false;
        if (json_string_length(webhook_value) > 36 || strlen(webhook_id) != json_string_length(webhook_value) ||
            !history_uuid(webhook_id))
            return false;
    }
    json_t *external = json_object_get(m, "external_author");
    if (external && !json_is_null(external) && !json_is_object(external)) return false;
    if (!string_field(m, "id", 36, false) ||
        !string_field(m, "channel_id", 36, false) ||
        !history_uuid(str(m, "id")) ||
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
static int time_compare(const char *at, const char *bt)
{
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
    return 0;
}
static int message_compare(json_t *a, json_t *b)
{
    int time = time_compare(str(a, "created_at"), str(b, "created_at"));
    return time ? time : strcmp(str(a, "id"), str(b, "id"));
}
/* Models own their JSON. Build replacements before publishing them so an
 * allocation failure cannot leave half of a merge visible. */
static size_t find(json_t *array, const char *id)
{
    size_t i; json_t *m;
    json_array_foreach(array, i, m) if (!strcmp(str(m, "id"), id)) return i;
    return SIZE_MAX;
}
static bool trim(history *h, json_t *array, bool oldest)
{
    if (array == h->messages) ++h->messages_revision;
    size_t bytes = 0, i; json_t *m;
    json_array_foreach(array, i, m) {
        size_t n = encoded_size(m);
        if (n == SIZE_MAX || n > SIZE_MAX - bytes) return false;
        bytes += n;
    }
    while (json_array_size(array) > HISTORY_MESSAGE_LIMIT || bytes > HISTORY_BYTE_LIMIT) {
        size_t index = oldest ? 0 : json_array_size(array) - 1;
        size_t n = encoded_size(json_array_get(array, index));
        if (n == SIZE_MAX || json_array_remove(array, index)) return false;
        bytes -= n; h->evicted = true;
    }
    if (array == h->messages) h->bytes = bytes;
    return true;
}
static bool upsert(history *h, json_t **array, json_t *value, bool oldest, bool protect_edit)
{
    if (!*array && !(*array = json_array())) return false;
    size_t index = find(*array, str(value, "id"));
    json_t *existing = json_array_get(*array, index);
    json_t *merged = existing ? json_deep_copy(existing) : json_object();
    json_t *copy = json_deep_copy(value);
    if (!merged || !copy || json_object_update(merged, copy)) {
        json_decref(merged); json_decref(copy); return false;
    }
    json_decref(copy);
    json_object_del(merged, "type");
    /* A partial creation/late POST cannot erase an already received edit. */
    if (protect_edit && existing && str(existing, "edited_at") &&
        (!str(value, "edited_at") || time_compare(str(existing, "edited_at"), str(value, "edited_at")) > 0)) {
        if (json_object_set(merged, "content", json_object_get(existing, "content")) ||
            json_object_set(merged, "edited_at", json_object_get(existing, "edited_at"))) {
            json_decref(merged); return false;
        }
    }
    json_t *next = json_copy(*array);
    if (!next) { json_decref(merged); return false; }
    bool ok;
    if (existing) ok = json_array_set(next, index, merged) == 0;
    else {
        size_t count = json_array_size(next), at = 0;
        while (at < count && message_compare(json_array_get(next, at), merged) < 0) ++at;
        ok = json_array_insert(next, at, merged) == 0;
    }
    json_decref(merged);
    if (!ok || !trim(h, next, oldest)) { json_decref(next); return false; }
    json_decref(*array); *array = next;
    return true;
}
/* Embed snapshots have no edit timestamp. Apply them independently so an old
 * text edit cannot suppress a newer embed event (or regress newer REST text). */
static bool apply_patch(json_t *message, json_t *patch)
{
    if (!message || !json_is_object(patch)) return true;
    if (json_object_get(patch, "edited_at") && (!str(message, "edited_at") ||
        time_compare(str(patch, "edited_at"), str(message, "edited_at")) >= 0)) {
        if (json_object_set(message, "content", json_object_get(patch, "content")) ||
            json_object_set(message, "edited_at", json_object_get(patch, "edited_at"))) return false;
    }
    json_t *embeds = json_object_get(patch, "embeds");
    return !embeds || json_object_set(message, "embeds", embeds) == 0;
}
static bool patched(history *h, json_t *message)
{
    json_t *patch = json_object_get(h->patches, str(message, "id"));
    if (json_is_null(patch)) return true; /* deletion wins over late POST */
    if (!upsert(h, &h->messages, message, true, true)) return false;
    if (patch) {
        json_t *m = json_array_get(h->messages, find(h->messages, str(message, "id")));
        if (!apply_patch(m, patch)) return false;
    }
    if (h->evicted && json_array_size(h->messages)) {
        json_t *first = json_array_get(h->messages, 0);
        h->more = true;
        snprintf(h->cursor_id, sizeof(h->cursor_id), "%s", str(first, "id"));
        snprintf(h->cursor_time, sizeof(h->cursor_time), "%s", str(first, "created_at"));
    }
    return trim(h, h->messages, true);
}
bool history_confirm(history *h, json_t *message, uint64_t generation)
{
    if (!h || !auth_has_session(h->auth) || generation != h->context ||
        generation != auth_context_generation(h->auth)) return false;
    const char *channel = str(history_channel(h, h->selected), "id");
    if (!channel || !message_valid(message, channel)) return false;
    if (!h->replaying && (h->recovering || h->pending == MESSAGES)) {
        json_t *event = json_deep_copy(message);
        if (!event || json_object_set_new(event, "type", json_string("message"))) { json_decref(event); return false; }
        bool ok = remember(h, event); json_decref(event);
        if (!ok) return false;
    }
    bool added = !contains(h->messages, str(message, "id"));
    if (!patched(h, message)) return false;
    if (added && contains(h->messages, str(message, "id"))) ++h->insertions;
    h->want_authors = true;
    /* The editor owns the send notice; confirmation must not replace reads. */
    return true;
}
static bool replay(history *h)
{
    h->replaying = true;
    size_t i; json_t *event;
    json_t *journal = json_incref(h->journal);
    bool ok = true;
    json_array_foreach(journal, i, event)
        if (!history_event(h, event, h->context)) { ok = false; break; }
    json_decref(journal);
    h->replaying = false;
    if (!ok) return false;
    return true;
}
static void clear_journal(history *h)
{
    json_decref(h->journal); h->journal = NULL; h->journal_bytes = 0;
}
void history_recover(history *h)
{
    if (!h || !auth_has_session(h->auth)) return;
    if (h->pending != NONE) auth_cancel_read(h->auth);
    h->pending = NONE; ++h->selection;
    json_decref(h->fresh); h->fresh = NULL;
    json_decref(h->boundary); h->boundary = NULL;
    clear_journal(h);
    /* Old edits are superseded by the new authoritative window. Keep deletion
     * markers while a message POST may still produce a delayed confirmation. */
    if (!auth_busy(h->auth)) { json_decref(h->patches); h->patches = NULL; }
    h->evicted = false;
    h->recovering = true; h->want_channels = true; h->want_messages = h->want_authors = false;
    h->ascending = false; h->retry_at = 0;
    h->recovery_id[0] = h->recovery_time[0] = '\0';
    size_t count = history_message_count(h);
    if (count) {
        h->boundary = json_deep_copy(history_message(h, 0));
        if (!h->boundary) {
            h->recovering = h->want_channels = false; h->failed = CHANNELS;
            status(h, "Não foi possível preparar a recuperação. Ctrl+R: tentar novamente.");
            return;
        }
        json_t *latest = history_message(h, count - 1);
        snprintf(h->recovery_id, sizeof(h->recovery_id), "%s", str(latest, "id"));
        snprintf(h->recovery_time, sizeof(h->recovery_time), "%s", str(latest, "created_at"));
        h->ascending = true;
    }
    status(h, "Recuperando canais e histórico…");
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
    json_t *next = h->messages ? json_copy(h->messages) : json_array();
    if (!next) return false;
    for (size_t i = count; i > 0; --i) {
        json_t *m = json_array_get(page, i - 1);
        if (json_is_null(json_object_get(h->patches, str(m, "id")))) continue;
        if (!upsert(h, &next, m, false, true)) goto fail;
        json_t *patch = json_object_get(h->patches, str(m, "id"));
        json_t *merged = json_array_get(next, find(next, str(m, "id")));
        if (!apply_patch(merged, patch)) goto fail;
    }
    json_decref(h->messages); h->messages = next;
    if (!trim(h, h->messages, false)) return false;
    h->more = count && json_is_true(more); /* Empty pages terminate safely. */
    if (count) {
        json_t *m = json_array_get(page, count - 1);
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
    notice(h, HISTORY_NOTICE_READ, "");
    return true;
fail:
    json_decref(next); return false;
}
static bool remember(history *h, const json_t *event)
{
    if (h->replaying) return true;
    if (!h->journal && !(h->journal = json_array())) return false;
    size_t size = encoded_size((json_t *)event);
    if (size == SIZE_MAX) return false;
    if (json_array_size(h->journal) >= 256 || size > 1024U * 1024U - h->journal_bytes) {
        history_recover(h);
        status(h, "Eventos excederam o limite. Recuperando…");
        return false;
    }
    json_t *copy = json_deep_copy(event);
    if (!copy || json_array_append_new(h->journal, copy)) return false;
    h->journal_bytes += size;
    return true;
}
bool history_event(history *h, const json_t *event, uint64_t generation)
{
    if (!h || !auth_has_session(h->auth) || generation != h->context ||
        generation != auth_context_generation(h->auth)) return false;
    const char *type = str((json_t *)event, "type");
    if (!type || !string_field((json_t *)event, "type", 128, false)) goto invalid;
    if (!strcmp(type, "avatar_update")) {
        if (!string_field((json_t *)event, "user_id", 36, false) || !history_uuid(str((json_t *)event, "user_id"))) goto invalid;
        chat_assets_avatar_invalidate(h->assets, str((json_t *)event, "user_id")); return true;
    }
    if (!strcmp(type, "heartbeat_ack")) return true;
    if (!strcmp(type, "typing")) {
        if (!typing_event(h, event)) goto invalid;
        return true;
    }
    if (!strcmp(type, "presence_update")) {
        if (!presence_event(h, event)) goto invalid;
        return true;
    }
    /* This snapshot has no nickname/typing metadata. Resolve active typers
     * through summaries rather than fetching all online users. */
    if (!strcmp(type, "presence_sync")) return true;
    if (!strcmp(type, "error")) { event_status(h, "O servidor informou um erro de tempo real.", "Erro remoto"); return true; }
    bool message = !strcmp(type, "message"), edit = !strcmp(type, "message_edit"),
         del = !strcmp(type, "message_delete");
    bool embeds = !strcmp(type, "message_embeds_update");
    bool create = !strcmp(type, "channel_create"), update = !strcmp(type, "channel_update"),
         remove = !strcmp(type, "channel_delete");
    if (!(message || edit || del || embeds || create || update || remove)) {
        event_status(h, "Evento de tempo real não suportado ignorado.", "Evento ignorado"); return true;
    }
    const char *channel = str((json_t *)event, "channel_id"),
               *id = str((json_t *)event, embeds ? "message_id" : "id");
    if (!string_field((json_t *)event, "channel_id", 36, false) || !history_uuid(channel) ||
        ((message || edit || del || embeds) &&
         (!string_field((json_t *)event, embeds ? "message_id" : "id", 36, false) || !history_uuid(id)))) goto invalid;
    if (embeds) {
        json_t *list = json_object_get(event, "embeds");
        /* The development backend emits null when its current list is empty,
         * as REST also allows. An absent field is still an invalid event. */
        if ((!json_is_array(list) && !json_is_null(list)) || json_array_size(list) > 10) goto invalid;
        size_t i; json_t *item;
        json_array_foreach(list, i, item) if (!json_is_object(item)) goto invalid;
    }
    if (message && !message_valid((json_t *)event, channel)) goto invalid;
    uint64_t timestamp;
    if (edit && (!json_object_get(event, "content") || !string_field((json_t *)event, "content", 32768, true) ||
        !string_field((json_t *)event, "edited_at", 64, false) ||
        !session_time_parse(str((json_t *)event, "edited_at"), &timestamp))) goto invalid;
    if ((create || update) && (!string_field((json_t *)event, "name", 2048, false) ||
        !string_field((json_t *)event, "topic", 2048, true) ||
        !json_is_integer(json_object_get(event, "position")))) goto invalid;
    const char *ctype = str((json_t *)event, "channel_type");
    if (create && (!ctype || (strcmp(ctype, "text") && strcmp(ctype, "voice") && strcmp(ctype, "category")))) goto invalid;
    bool active = !strcmp(channel, str(history_channel(h, h->selected), "id") ?
                         str(history_channel(h, h->selected), "id") : "");
    bool relevant = create || update || remove || active;
    if (relevant && !h->replaying && (h->recovering || h->pending == CHANNELS || h->pending == MESSAGES))
        if (!remember(h, event)) return false;
    if (create || update || remove) {
        size_t index = find(h->channels, channel);
        json_t *next = h->channels ? json_deep_copy(h->channels) : json_array();
        if (!next) goto allocation;
        bool was_active = active;
        if (remove) {
            if (index != SIZE_MAX && json_array_remove(next, index)) { json_decref(next); goto allocation; }
        } else if (index != SIZE_MAX || create) {
            json_t *c = index != SIZE_MAX ? json_array_get(next, index) : json_object();
            if (!c) { json_decref(next); goto allocation; }
            bool ok = !json_object_set_new(c, "id", json_string(channel)) &&
                      !json_object_set(c, "name", json_object_get(event, "name")) &&
                      !json_object_set(c, "position", json_object_get(event, "position"));
            if (json_object_get(event, "topic")) ok &= !json_object_set(c, "topic", json_object_get(event, "topic"));
            if (create) ok &= !json_object_set_new(c, "type", json_string(ctype));
            if (index == SIZE_MAX) { ok &= !json_array_append(next, c); json_decref(c); }
            if (!ok) { json_decref(next); goto allocation; }
        }
        json_t *body = json_pack("{s:o}", "channels", next);
        if (!body) goto allocation;
        bool ok = channels_response(h, body); json_decref(body);
        if (!ok) goto invalid;
        if (remove && was_active) {
            if (h->pending != NONE) auth_cancel_read(h->auth);
            h->pending = NONE; ++h->selection;
            status(h, "Canal removido ou indisponível. Rascunho preservado.");
        }
        return true;
    }
    if (message) {
        size_t ci = find(h->channels, channel);
        json_t *c = json_array_get(h->channels, ci);
        if (c) {
            json_t *author_field = json_object_get(event, "author_id");
            json_t *copy = json_pack("{s:O,s:O,s:O,s:O}", "id", json_object_get(event, "id"),
                                    "author_id", author_field ? author_field : json_null(),
                                    "created_at", json_object_get(event, "created_at"),
                                    "content", json_object_get(event, "content") ? json_object_get(event, "content") : json_null());
            if (!copy || json_object_set_new(c, "last_message", copy)) goto allocation;
        }
    }
    if (!active) return true;
    if (message && !h->replaying) {
        const char *author_typing = str((json_t *)event, "author_id");
        if (author_typing && *author_typing) typing_remove(h, author_typing);
    }
    if (edit || del || embeds) {
        json_t *previous = json_object_get(h->patches, id);
        json_t *existing = json_array_get(h->messages, find(h->messages, id));
        if (edit && str(existing, "edited_at") &&
            time_compare(str(existing, "edited_at"), str((json_t *)event, "edited_at")) > 0) return true;
        if (!del && json_is_null(previous)) return true;
        if (edit && str(previous, "edited_at") &&
            time_compare(str(previous, "edited_at"), str((json_t *)event, "edited_at")) > 0) return true;
        if (!h->patches && !(h->patches = json_object())) goto allocation;
        if (!json_object_get(h->patches, id) && json_object_size(h->patches) >= 256) {
            history_recover(h); status(h, "Marcas excederam o limite. Recuperando…"); return false;
        }
        json_t *patch;
        if (del) patch = json_null();
        else if (embeds) patch = json_is_null(json_object_get(event, "embeds")) ?
            json_pack("{s:[]}", "embeds") : json_pack("{s:O}", "embeds", json_object_get(event, "embeds"));
        else patch = json_pack("{s:O,s:O}", "content", json_object_get(event, "content"),
                               "edited_at", json_object_get(event, "edited_at"));
        if (!patch) goto allocation;
        json_t *copy = json_deep_copy(patch);
        json_decref(patch);
        if (copy && !del && json_is_object(previous)) {
            json_t *combined = json_deep_copy(previous);
            if (combined && json_object_update(combined, copy)) { json_decref(combined); combined = NULL; }
            json_decref(copy); copy = combined;
        }
        if (!copy || json_object_set_new(h->patches, id, copy)) goto allocation;
        size_t patch_bytes = encoded_size(h->patches);
        if (patch_bytes == SIZE_MAX) goto allocation;
        if (patch_bytes > 1024U * 1024U) {
            json_object_del(h->patches, id); history_recover(h);
            status(h, "Marcas excederam o limite. Recuperando…"); return false;
        }
        size_t index = find(h->messages, id);
        if (index != SIZE_MAX) {
            if (del) { if (json_array_remove(h->messages, index)) goto allocation; }
            else if (!apply_patch(json_array_get(h->messages, index), json_object_get(h->patches, id))) goto allocation;
            if (!trim(h, h->messages, true)) goto allocation;
        }
    } else if (message) {
        bool added = !contains(h->messages, id);
        if (!patched(h, (json_t *)event)) goto allocation;
        if (added && contains(h->messages, id) && !h->replaying) ++h->insertions;
        h->want_authors = true;
    }
    if (active && (edit || embeds)) h->want_authors = true;
    return true;
invalid:
    event_status(h, "Evento de tempo real inválido ignorado.", "Evento inválido"); return false;
allocation:
    event_status(h, "Não foi possível aplicar o evento. Ctrl+R: recuperar.", "Falha no evento"); return false;
}
static bool recovery_response(history *h, json_t *body)
{
    const char *channel = str(history_channel(h, h->selected), "id");
    json_t *page = json_object_get(body, "messages"), *more = json_object_get(body, "has_more");
    size_t count = json_array_size(page);
    if (!channel || !str(body, "channel_id") || strcmp(channel, str(body, "channel_id")) ||
        !json_is_array(page) || count > 100 || !json_is_boolean(more)) return false;
    for (size_t i = 0; i < count; ++i) if (!message_valid(json_array_get(page, i), channel)) return false;
    json_t *last = count ? json_array_get(page, count - 1) : NULL;
    if (last && !strcmp(h->recovery_id, str(last, "id"))) return false;
    for (size_t i = 0; i < count; ++i)
        if (!upsert(h, &h->fresh, json_array_get(page, i), true, false)) return false;
    bool next = count && json_is_true(more);
    if (h->ascending) {
        if (next && json_array_size(h->fresh) < HISTORY_MESSAGE_LIMIT) {
            snprintf(h->recovery_id, sizeof(h->recovery_id), "%s", str(last, "id"));
            snprintf(h->recovery_time, sizeof(h->recovery_time), "%s", str(last, "created_at"));
        } else {
            h->ascending = false; h->recovery_id[0] = h->recovery_time[0] = '\0';
            /* The descending pass is authoritative for the complete retained
             * window. Do not let a capped forward pass crowd out recent data. */
            json_decref(h->fresh); h->fresh = NULL; h->evicted = false;
        }
        h->want_messages = true; return true;
    }
    if (next && h->boundary && message_compare(last, h->boundary) > 0 &&
        json_array_size(h->fresh) < HISTORY_MESSAGE_LIMIT && !h->evicted) {
        snprintf(h->recovery_id, sizeof(h->recovery_id), "%s", str(last, "id"));
        snprintf(h->recovery_time, sizeof(h->recovery_time), "%s", str(last, "created_at"));
        h->want_messages = true; return true;
    }
    /* An authoritative replacement removes deletions lost while offline. Live
     * events received throughout recovery are then reapplied in arrival order. */
    json_t *old = h->messages;
    h->messages = h->fresh ? h->fresh : json_array(); h->fresh = NULL;
    if (!h->messages) { h->messages = old; return false; }
    size_t i; json_t *m;
    json_array_foreach(h->messages, i, m)
        if (!contains(old, str(m, "id"))) ++h->insertions;
    json_decref(old);
    h->more = next; h->older = false;
    if (last) {
        snprintf(h->cursor_id, sizeof(h->cursor_id), "%s", str(last, "id"));
        snprintf(h->cursor_time, sizeof(h->cursor_time), "%s", str(last, "created_at"));
    }
    if (!replay(h) || !trim(h, h->messages, true)) return false;
    clear_journal(h);
    json_decref(h->boundary); h->boundary = NULL;
    h->recovering = false; h->failures = 0; h->want_authors = history_message_count(h) > 0;
    status(h, "Tempo real conectado. Histórico recuperado.");
    notice(h, HISTORY_NOTICE_READ, "");
    return true;
}
static bool authors_response(history *h, json_t *body)
{
    if (!json_is_array(body) || json_array_size(body) > 1000) return false;
    size_t i;
    json_t *u;
    json_array_foreach(body, i, u) if (!cache_user(h, u, false)) return false;
    return true;
}

static bool request_messages(history *h)
{
    const char *id = str(history_channel(h, h->selected), "id");
    if (!id) return false;
    char path[512];
    if (h->recovering && *h->recovery_id) {
        char *timestamp = curl_easy_escape(NULL, h->recovery_time, 0);
        if (!timestamp) return false;
        snprintf(path, sizeof(path), "/channels/%s/messages?order=%s&since=%s&last_id=%s", id,
                 h->ascending ? "asc" : "desc", timestamp, h->recovery_id);
        curl_free(timestamp);
    } else if (h->older && !h->recovering) {
        char *timestamp = curl_easy_escape(NULL, h->cursor_time, 0);
        if (!timestamp) return false;
        snprintf(path, sizeof(path), "/channels/%s/messages?order=desc&since=%s&last_id=%s", id, timestamp, h->cursor_id);
        curl_free(timestamp);
    } else snprintf(path, sizeof(path), "/channels/%s/messages?order=desc", id);
    return auth_get_json(h->auth, path);
}
typedef struct {
    history *model;
    json_t *ids, *seen;
    bool ok;
} mention_batch;
static void collect_mention(void *context, const mention_range *range)
{
    mention_batch *batch = context;
    history *h = batch->model;
    const char *id = range->id;
    if (!batch->ok || user_resolved(h, id) || json_object_get(h->queried, id) ||
        json_object_get(batch->seen, id) || json_array_size(batch->ids) >= 1000 ||
        json_object_size(h->queried) + json_array_size(batch->ids) >= 10000) return;
    batch->ok = !json_array_append_new(batch->ids, json_string(id)) &&
                !json_object_set_new(batch->seen, id, json_true());
}
static bool request_authors(history *h)
{
    json_t *ids = json_array(), *seen = json_object();
    if (!ids || !seen) {
        json_decref(ids);
        json_decref(seen);
        return false;
    }
    size_t i;
    json_t *m;
    bool ok = true;
    for (size_t j = 0; j < h->typing_count; ++j) {
        const char *id = h->typing[j].id;
        json_t *user = json_object_get(h->users, id);
        if ((!user_resolved(h, id) || !json_object_get(user, "typing")) &&
            !json_object_get(h->queried, id) && json_object_size(h->queried) + json_array_size(ids) < 10000) {
            ok &= !json_array_append_new(ids, json_string(id)) && !json_object_set_new(seen, id, json_true());
        }
    }
    json_array_foreach(h->messages, i, m)
    {
        const char *id = str(m, "author_id");
        /* Webhook messages carry author_id null (REST) or "" (WS); they are
         * display-only and never resolve via user_summary_batch. */
        if (id && *id && history_uuid(id) && !user_resolved(h, id) && !json_object_get(h->queried, id) &&
            !json_object_get(seen, id) && json_array_size(ids) < 1000 &&
            json_object_size(h->queried) + json_array_size(ids) < 10000) {
            ok &= !json_array_append_new(ids, json_string(id)) && !json_object_set_new(seen, id, json_true());
        }
        mention_batch batch = {h, ids, seen, ok};
        mention_visit_message(m, collect_mention, &batch);
        ok = batch.ok;
    }
    if (ok && json_array_size(ids)) ok = auth_user_summaries(h->auth, ids);
    if (ok) {
        json_decref(h->summary_ids);
        h->summary_ids = json_incref(ids);
    }
    json_decref(ids);
    json_decref(seen);
    return ok;
}

bool history_poll(history *h)
{
    bool changed = false;
    if (h->context != auth_context_generation(h->auth)) {
        h->context = auth_context_generation(h->auth);
        chat_assets_destroy(h->assets); h->assets = chat_assets_create();
        json_object_clear(h->users); json_array_clear(h->directory); json_object_clear(h->queried);
        ++h->users_revision;
        h->users_complete = h->users_failed = h->users_partial = false; h->user_bytes = 0;
        h->users_id[0] = h->users_time[0] = 0;
        history_reload_channels(h); h->pending = NONE; changed = true;
        h->epoch = auth_ws_epoch(h->auth);
    }
    changed |= chat_assets_webhooks_poll(h->assets, h->auth);
    uint64_t epoch = auth_ws_epoch(h->auth);
    bool connected = auth_has_session(h->auth) && auth_ws_state(h->auth) == WS_OPEN;
    if ((h->typing_connected && !connected) || epoch != h->epoch) {
        h->typing_count = 0; h->typing_sent = 0;
        json_object_clear(h->queried);
        const char *id; json_t *user;
        json_object_foreach(h->users, id, user) {
            const char *phrase = str(user, "typing");
            if (phrase) h->user_bytes -= strlen(phrase);
            json_object_del(user, "typing"); json_object_del(user, "_presence");
        }
        changed = true;
    }
    h->typing_connected = connected;
    if (epoch != h->epoch) {
        /* Initial snapshots are deferred until the first upgrade. A later
         * upgrade requires authoritative recovery of the displayed window. */
        if (h->epoch || h->channels || h->messages) history_recover(h);
        h->epoch = epoch; changed = true;
    }
    json_t *event;
    while ((event = auth_next_event(h->auth))) {
        history_event(h, event, h->context); json_decref(event); changed = true;
    }
    if (auth_response_ready(h->auth)) {
        long code; uint64_t generation;
        json_t *body = auth_take_response(h->auth, &code, &generation);
        if (h->pending != NONE && generation == h->context && h->request_context == h->context &&
            (h->pending == USERS || h->pending == EMOJIS || h->pending == PROFILES || h->request_selection == h->selection)) {
            enum request_kind kind = h->pending;
            if (kind == EMOJIS || kind == PROFILES) {
                if (kind == EMOJIS) chat_assets_emojis_response(h->assets, body, code);
                else chat_assets_profiles_response(h->assets, body, code);
                json_decref(body); h->pending = NONE; return true;
            }
            bool was_recovering = h->recovering;
            if (kind == USERS) {
                h->users_failed = code != 200 || !users_response(h, body);
                json_decref(body); h->pending = NONE; return true;
            }
            bool ok = code == 200 && (kind == CHANNELS ? channels_response(h, body) :
                         kind == MESSAGES ? h->recovering ? recovery_response(h, body) : messages_response(h, body) : authors_response(h, body));
            if (ok && kind != AUTHORS && !(was_recovering && kind == MESSAGES)) {
                ok = replay(h);
                if (!h->recovering) clear_journal(h);
            }
            if (ok && kind == CHANNELS && h->recovering) {
                json_t *channel = history_channel(h, h->selected);
                if (channel && !strcmp(str(channel, "type"), "text")) h->want_messages = true;
                else { h->recovering = false; clear_journal(h); }
            }
            if (kind == AUTHORS) {
                if (ok) {
                    notice(h, HISTORY_NOTICE_READ, "");
                    size_t i; json_t *id;
                    json_array_foreach(h->summary_ids, i, id)
                        if (json_object_set_new(h->queried, json_string_value(id), json_true())) ok = false;
                    /* Resolve another batch when a message contains more than
                     * 1000 distinct names. Omitted IDs are cached only after a
                     * successful response; cancellation and failures can retry. */
                    h->want_authors = ok;
                }
                json_decref(h->summary_ids); h->summary_ids = NULL;
            }
            if (!ok) {
                h->failed = kind;
                if (h->recovering && (code == 0 || code == 429 || code >= 500)) {
                    uint64_t now;
                    if (net_now_ms(&now)) {
                        h->retry_at = now + net_retry_ms(h->failures, (unsigned)rand());
                        if (h->failures < 5) ++h->failures;
                        if (kind == CHANNELS) h->want_channels = true;
                        else h->want_messages = true;
                    }
                    status(h, "Recuperação pendente. Nova tentativa automática; Ctrl+R: tentar agora.");
                } else {
                    h->recovering = false;
                    if (kind == AUTHORS) status(h, "Autores indisponíveis. Ctrl+R: tentar novamente.");
                    else status(h, code == 403 ? "Sem permissão para ler este canal." : code == 404 ? "Canal removido ou indisponível." :
                         code == 429 ? "Limite de requisições atingido. Aguarde e use Ctrl+R." : "Falha ao carregar. Ctrl+R: tentar novamente.");
                }
            } else h->failed = NONE;
            changed = true;
        }
        json_decref(body); h->pending = NONE;
    }
    uint64_t now;
    if (!net_now_ms(&now)) return changed;
    for (size_t i = 0; i < h->typing_count;) {
        if (now >= h->typing[i].expires) { typing_remove(h, h->typing[i].id); changed = true; }
        else ++i;
    }
    ws_state ws = auth_ws_state(h->auth);
    if (ws == WS_QUEUED || ws == WS_CONNECTING) return changed;
    if (h->recovering && ws != WS_OPEN) return changed;
    if (now < h->retry_at) return changed;
    if (h->pending == NONE && auth_has_session(h->auth) && !auth_busy(h->auth)) {
        enum request_kind kind = h->want_channels ? CHANNELS : h->want_messages ? MESSAGES : h->want_authors ? AUTHORS : chat_assets_emojis_wanted(h->assets) ? EMOJIS : chat_assets_profiles_wanted(h->assets) ? PROFILES : h->users_active && !h->users_complete && !h->users_failed ? USERS : NONE;
        bool started = kind == CHANNELS ? auth_get_json(h->auth, "/channels") :
                       kind == MESSAGES ? request_messages(h) : kind == AUTHORS ? request_authors(h) : kind == USERS ? request_users(h) :
                       kind == EMOJIS ? auth_emojis(h->auth, chat_assets_emojis_path(h->assets)) :
                       kind == PROFILES ? auth_user_profiles(h->auth, chat_assets_profiles_request(h->assets)) : false;
        if (started && kind == AUTHORS && !auth_busy(h->auth)) {
            h->want_authors = false;
            notice(h, HISTORY_NOTICE_READ, "");
            json_decref(h->summary_ids); h->summary_ids = NULL;
            return changed;
        }
        if (started) {
            h->pending = kind; h->request_context = h->context; h->request_selection = h->selection;
            if (!h->recovering) clear_journal(h);
            if (kind == CHANNELS) h->want_channels = false;
            else if (kind == MESSAGES) h->want_messages = false;
            else if (kind == AUTHORS) h->want_authors = false;
            changed = true;
        } else if (kind != NONE && auth_get_state(h->auth) == AUTH_READY && !auth_response_ready(h->auth)) {
            if (kind == USERS) { h->users_failed = true; return true; }
            if (kind == EMOJIS || kind == PROFILES) {
                if (kind == EMOJIS) chat_assets_emojis_response(h->assets, NULL, 0);
                else chat_assets_profiles_response(h->assets, NULL, 0);
                return true;
            }
            h->failed = kind;
            h->want_channels = h->want_messages = h->want_authors = false;
            status(h, "Não foi possível preparar a consulta. Ctrl+R: tentar novamente.");
            changed = true;
        }
    }
    return changed;
}
