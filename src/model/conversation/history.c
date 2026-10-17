#include "model/conversation/private.h"

bool history_uuid(const char *value)
{
    if (!value || strlen(value) != 36)
        return false;
    for (size_t i = 0; i < 36; ++i)
        if (i == 8 || i == 13 || i == 18 || i == 23
                ? value[i] != '-'
                : !((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f') ||
                    (value[i] >= 'A' && value[i] <= 'F')))
            return false;
    return true;
}

const char *history_internal_str(const json_t *obj, const char *key)
{
    return json_string_value(json_object_get(obj, key));
}

bool history_internal_string_field(json_t *obj, const char *key, size_t limit, bool optional)
{
    json_t *v = json_object_get(obj, key);
    if (optional && (!v || json_is_null(v)))
        return true;
    const char *s = json_string_value(v);
    return s && json_string_length(v) <= limit && strlen(s) == json_string_length(v);
}

static void cost_free(gpointer p)
{
    message_cost *cost = p;
    json_decref(cost->message);
    free(cost);
}

message_cost *history_internal_measure_message(history *h, json_t *m, bool changed)
{
    message_cost *cost = g_hash_table_lookup(h->costs, m);
    if (cost && !changed)
        return cost;
    size_t bytes = json_dumpb(m, NULL, 0, JSON_COMPACT);
    if (!bytes)
        return NULL;
    if (!cost) {
        cost = calloc(1, sizeof(*cost));
        if (!cost)
            return NULL;
        cost->message = json_incref(m);
        g_hash_table_insert(h->costs, m, cost);
    }
    cost->bytes = bytes;
    cost->revision = ++h->cost_sequence;
    return cost;
}

uint64_t history_message_revision(history *h, size_t index)
{
    json_t *m = history_message(h, index);
    message_cost *cost = m ? history_internal_measure_message(h, m, false) : NULL;
    return cost ? cost->revision : 0;
}

void history_internal_notice(history *h, history_notice_kind kind, const char *value)
{
    if (!strcmp(h->notices[kind], value))
        return;
    snprintf(h->notices[kind], sizeof(h->notices[kind]), "%s", value);
    h->notice_revisions[kind] = ++h->notice_sequence;
}

void history_internal_status(history *h, const char *value)
{
    snprintf(h->status, sizeof(h->status), "%s", value);
    history_internal_notice(h, HISTORY_NOTICE_READ, value);
}

void history_internal_event_status(history *h, const char *value, const char *compact)
{
    snprintf(h->notices[HISTORY_NOTICE_EVENT], sizeof(h->notices[HISTORY_NOTICE_EVENT]), "%s",
             value);
    h->notice_revisions[HISTORY_NOTICE_EVENT] = ++h->notice_sequence;
    h->event_compact = compact;
}

static bool save_reads(history *h)
{
    if (!h->read_loaded || !h->read_dirty)
        return false;
    bool failed = !read_state_save(h->read_url, h->read_user, h->read_messages);
    bool changed = failed != h->read_save_failed;
    if (failed && changed)
        history_internal_event_status(
            h, "Não foi possível salvar a leitura local. Será mantida apenas nesta sessão.",
            "Leitura local não salva");
    if (!failed) {
        h->read_dirty = false;
        if (h->read_save_failed && h->event_compact &&
            !strcmp(h->event_compact, "Leitura local não salva"))
            history_internal_event_status(h, "", "");
    }
    h->read_save_failed = failed;
    return changed;
}

static void load_reads(history *h)
{
    const char *url = auth_instance_url(h->auth), *user = auth_user_id(h->auth);
    if (!h->r8 || h->read_loaded || !*url || strlen(url) >= sizeof(h->read_url) ||
        !history_uuid(user))
        return;
    json_t *saved = read_state_load(url, user);
    if (!saved)
        return;
    json_decref(h->read_messages);
    h->read_messages = saved;
    snprintf(h->read_url, sizeof(h->read_url), "%s", url);
    snprintf(h->read_user, sizeof(h->read_user), "%s", user);
    h->read_loaded = true;
}

static void clear_messages(history *h)
{
    history_internal_actions_reset(h);
    h->typing_count = 0;
    h->typing_sent = 0;
    h->read_attempt[0] = h->read_visible[0] = 0;
    h->read_after = 0;
    ++h->messages_revision;
    json_decref(h->journal);
    h->journal = NULL;
    h->journal_bytes = 0;
    json_decref(h->patches);
    h->patches = NULL;
    json_decref(h->fresh);
    h->fresh = NULL;
    json_decref(h->boundary);
    h->boundary = NULL;
    h->recovering = h->ascending = false;
    h->retry_at = 0;
    h->failures = 0;
    json_decref(h->messages);
    h->messages = NULL;
    if (h->costs)
        g_hash_table_remove_all(h->costs);
    h->bytes = 0;
    h->more = h->newer_more = h->evicted = false;
    h->older = h->newer = false;
    h->failed = NONE;
    h->cursor_id[0] = h->cursor_time[0] = '\0';
    h->newer_id[0] = h->newer_time[0] = h->page_id[0] = h->page_time[0] = '\0';
    h->want_authors = h->want_messages = false;
    history_internal_notice(h, HISTORY_NOTICE_READ, "");
    history_internal_notice(h, HISTORY_NOTICE_EVENT, "");
}

history *history_create(auth_client *auth)
{
    history *h = calloc(1, sizeof(*h));
    if (!h)
        return NULL;
    h->costs = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, cost_free);
    h->assets = chat_assets_create();
    h->auth = auth;
    h->selected = SIZE_MAX;
    h->active = true;
    h->inbox = inbox_create(auth);
    h->context = auth_context_generation(auth);
    h->want_channels = true;
    h->users = json_object();
    h->directory = json_array();
    h->queried = json_object();
    h->read_messages = json_object();
    if (!h->assets || !h->users || !h->directory || !h->queried || !h->inbox || !h->read_messages) {
        history_destroy(h);
        return NULL;
    }
    h->epoch = auth_ws_epoch(auth);
    history_internal_status(h, "Carregando canais…");
    return h;
}

void history_set_media_updates(history *h, void (*update)(void *, const json_t *), void *context)
{
    if (h) {
        h->media_update = update;
        h->media_context = context;
    }
}

void history_destroy(history *h)
{
    if (!h)
        return;
    save_reads(h);
    if (h->pending != NONE)
        auth_cancel_read(h->auth);
    chat_assets_destroy(h->assets);
    clear_messages(h);
    inbox_destroy(h->inbox);
    json_decref(h->channels);
    json_decref(h->read_messages);
    json_decref(h->users);
    json_decref(h->directory);
    json_decref(h->queried);
    json_decref(h->summary_ids);
    g_hash_table_destroy(h->costs);
    history_internal_actions_destroy(h);
    free(h);
}

void history_enable_r8(history *h)
{
    h->r8 = true;
    load_reads(h);
}

inbox *history_inbox(history *h)
{
    return h->inbox;
}

void history_active(history *h, bool active)
{
    if (h->active == active)
        return;
    h->active = active;
    if (!active) {
        if (h->pending != NONE && h->pending != INBOX && !history_internal_actions_mutating(h)) {
            auth_cancel_read(h->auth);
            h->pending = NONE;
        }
        chat_assets_cancel(h->assets);
        ++h->selection;
        clear_messages(h);
        h->selected = SIZE_MAX;
        h->users_active = false;
    } else if (h->r8)
        inbox_refresh(h->inbox);
}

void history_activity(history *h)
{
    uint64_t now;
    if (!h->active || !auth_has_session(h->auth) || auth_ws_state(h->auth) != WS_OPEN ||
        !net_now_ms(&now) || (h->activity_sent && now - h->activity_sent < 15000))
        return;
    json_t *e = json_pack("{s:s}", "type", "presence_activity");
    if (e && auth_send_event(h->auth, e))
        h->activity_sent = now;
    json_decref(e);
}

size_t history_channel_count(const history *h)
{
    return json_array_size(h->channels) + (h->r8 ? inbox_dm_count(h->inbox) : 0);
}

json_t *history_channel(const history *h, size_t i)
{
    size_t n = json_array_size(h->channels);
    return i < n                    ? json_array_get(h->channels, i)
           : h->r8 && i != SIZE_MAX ? inbox_dm(h->inbox, i - n)
                                    : NULL;
}

void history_internal_restore_selection(history *h, const char *id)
{
    if (!*id)
        return;
    h->selected = SIZE_MAX;
    for (size_t i = 0; i < history_channel_count(h); ++i)
        if (!strcmp(id, history_internal_str(history_channel(h, i), "id"))) {
            h->selected = i;
            break;
        }
    if (h->selected == SIZE_MAX) {
        ++h->selection;
        clear_messages(h);
    }
}

chat_assets *history_assets(history *h)
{
    return h ? h->assets : NULL;
}

size_t history_selected(const history *h)
{
    return h->selected;
}

size_t history_message_count(const history *h)
{
    return json_array_size(h->messages);
}

json_t *history_message(const history *h, size_t i)
{
    return json_array_get(h->messages, i);
}

uint64_t history_messages_revision(const history *h)
{
    return h->messages_revision;
}

const char *history_status(const history *h)
{
    const char *notice = auth_permissions_notice(h->auth);
    return h->selected == SIZE_MAX && *notice ? notice : h->status;
}

const char *history_notice(const history *h, history_notice_kind kind, uint64_t *revision)
{
    if (!h || kind < 0 || kind >= HISTORY_NOTICE_COUNT)
        return "";
    if (revision)
        *revision = h->notice_revisions[kind];
    return h->notices[kind];
}

const char *history_event_compact(const history *h)
{
    return h->event_compact ? h->event_compact : "";
}

bool history_has_more(const history *h)
{
    return h->more;
}

bool history_has_newer(const history *h)
{
    return h->newer_more;
}

bool history_evicted(const history *h)
{
    return h->evicted;
}

bool history_recovering(const history *h)
{
    return h->recovering;
}

uint64_t history_insertions(const history *h)
{
    return h->insertions;
}

bool history_channel_unread(const history *h, const json_t *channel)
{
    const char *type = history_internal_str(channel, "type");
    if (!channel || (type && !strcmp(type, "category")))
        return false;
    json_t *latest = json_object_get(channel, "last_message");
    const char *last = history_internal_str(latest, "id"),
               *read = history_internal_str(channel, "last_read_message");
    if (!history_uuid(last))
        return false;
    json_t *local =
        h ? json_object_get(h->read_messages, history_internal_str(channel, "id")) : NULL;
    if (local) {
        if (!strcasecmp(last, history_internal_str(local, "id")))
            return false;
        int ordering;
        if (session_time_compare(history_internal_str(local, "created_at"),
                                 history_internal_str(latest, "created_at"), &ordering) &&
            (ordering > 0 ||
             (!ordering && strcasecmp(history_internal_str(local, "id"), last) > 0)))
            return false;
    }
    if (read && *read)
        return strcasecmp(last, read) != 0;
    int ordering;
    if (session_time_compare(history_internal_str(channel, "last_read_at"),
                             history_internal_str(latest, "created_at"), &ordering))
        return ordering < 0;
    return true;
}

void history_read_visible(history *h, const char *id)
{
    if (!id) {
        h->read_visible[0] = 0;
        return;
    }
    json_t *channel = history_channel(h, h->selected);
    const char *latest = history_internal_str(json_object_get(channel, "last_message"), "id");
    if (!h->r8 || !h->active || h->recovering || h->newer_more ||
        (h->failed != NONE && h->failed != AUTHORS) || h->want_messages || h->pending == MESSAGES ||
        !history_uuid(id) || !latest || strcmp(id, latest) || !strcmp(id, h->read_attempt) ||
        !history_channel_unread(h, channel))
        return;
    snprintf(h->read_visible, sizeof(h->read_visible), "%s", id);
}

bool history_loading(const history *h)
{
    return auth_has_session(h->auth) &&
           (h->want_messages || h->want_channels || h->want_authors || h->pending == MESSAGES ||
            h->pending == CHANNELS || h->pending == AUTHORS);
}

int history_paging_direction(const history *h)
{
    if (h->recovering || (!h->want_messages && h->pending != MESSAGES))
        return 0;
    return h->older ? -1 : h->newer ? 1 : 0;
}

void history_reload_channels(history *h)
{
    if (h->pending != NONE && h->pending != INBOX && !history_internal_actions_mutating(h)) {
        auth_cancel_read(h->auth);
        h->pending = NONE;
    }
    chat_assets_cancel(h->assets);
    ++h->selection;
    clear_messages(h);
    json_decref(h->channels);
    h->channels = NULL;
    h->selected = SIZE_MAX;
    h->want_channels = true;
    history_internal_status(h, "Carregando canais…");
}

bool history_select(history *h, size_t index)
{
    json_t *c = history_channel(h, index);
    if (!c || !history_channel_visible(h, index) ||
        !strcmp(history_internal_str(c, "type"), "category"))
        return false;
    if (!h->active)
        return false;
    if (h->pending == INBOX && auth_get_state(h->auth) != AUTH_API_MUTATION) {
        auth_cancel_read(h->auth);
        inbox_cancel_read(h->inbox);
        h->pending = NONE;
    }
    if (h->pending != NONE && h->pending != INBOX && !history_internal_actions_mutating(h)) {
        auth_cancel_read(h->auth);
        h->pending = NONE;
    }
    chat_assets_cancel(h->assets);
    ++h->selection;
    clear_messages(h);
    h->selected = index;
    h->older = false;
    h->want_messages = true;
    history_internal_status(h, "Carregando histórico…");
    return true;
}

void history_recent(history *h)
{
    if (h->selected != SIZE_MAX)
        history_select(h, h->selected);
}

bool history_retry(history *h)
{
    auth_permissions_refresh(h->auth);
    if (history_interactions_retry(h))
        return true;
    if (h->pending != NONE) {
        /* A background inbox/author request must not consume the user's retry. */
        if (h->failed != NONE)
            h->retry_requested = true;
        return true;
    }
    if (h->recovering) {
        h->retry_at = 0;
        history_recover(h);
        return true;
    }
    bool inplace = h->failed == AUTHORS || h->failed == MESSAGES;
    if (h->failed == AUTHORS)
        h->want_authors = true;
    else if (h->failed == MESSAGES)
        h->want_messages = true;
    else if (h->selected != SIZE_MAX)
        history_recent(h);
    else
        history_reload_channels(h);
    h->failed = NONE;
    history_internal_status(h, "Carregando…");
    return inplace;
}

static int channel_compare(const void *a, const void *b)
{
    json_t *ca = *(json_t *const *)a, *cb = *(json_t *const *)b;
    json_int_t pa = json_integer_value(json_object_get(ca, "position"));
    json_int_t pb = json_integer_value(json_object_get(cb, "position"));
    return pa < pb   ? -1
           : pa > pb ? 1
                     : strcmp(history_internal_str(ca, "id"), history_internal_str(cb, "id"));
}

bool history_internal_channels_response(history *h, json_t *body)
{
    char selected_id[37] = "";
    const char *current = history_internal_str(history_channel(h, h->selected), "id");
    if (current)
        snprintf(selected_id, sizeof(selected_id), "%s", current);
    json_t *array = json_object_get(body, "channels");
    size_t count = json_array_size(array);
    if (!json_is_array(array) || count > HISTORY_CHANNEL_LIMIT)
        return false;
    json_t *sorted[HISTORY_CHANNEL_LIMIT];
    for (size_t i = 0; i < count; ++i) {
        json_t *c = sorted[i] = json_array_get(array, i);
        if (!history_uuid(history_internal_str(c, "id")) ||
            !history_internal_string_field(c, "name", 2048, false) ||
            !history_internal_string_field(c, "topic", 2048, true) ||
            !history_internal_string_field(c, "type", 16, false) ||
            !json_is_integer(json_object_get(c, "position")))
            return false;
        const char *type = history_internal_str(c, "type");
        if (strcmp(type, "text") && strcmp(type, "voice") && strcmp(type, "category"))
            return false;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(history_internal_str(sorted[j], "id"), history_internal_str(c, "id")))
                return false;
    }
    qsort(sorted, count, sizeof(*sorted), channel_compare);
    json_t *next = json_array();
    if (!next)
        return false;
    for (size_t i = 0; i < count; ++i)
        if (json_array_append_new(next, json_deep_copy(sorted[i]))) {
            json_decref(next);
            return false;
        }
    json_decref(h->channels);
    h->channels = next;
    h->selected = SIZE_MAX;
    for (size_t i = 0; i < history_channel_count(h); ++i)
        if (!strcmp(selected_id, history_internal_str(history_channel(h, i), "id")))
            h->selected = i;
    if (*selected_id && h->selected == SIZE_MAX)
        clear_messages(h);
    if (!h->messages || h->selected == SIZE_MAX) {
        history_internal_status(h,
                                count ? "Escolha um canal para ler." : "Nenhum canal disponível.");
        if (count)
            history_internal_notice(h, HISTORY_NOTICE_READ, "");
    }
    return true;
}

bool history_channel_visible(const history *h, size_t index)
{
    json_t *channel = history_channel(h, index);
    if (!channel)
        return false;
    if (!strcmp(history_internal_str(channel, "type"), "category"))
        return true;
    return permissions_channel(auth_permissions(h->auth), history_internal_str(channel, "id"),
                               PERMISSION_READ_CHANNEL) == PERMISSION_ALLOWED;
}
permission_decision history_permission(const history *h, channel_permission permission)
{
    json_t *channel = h ? history_channel(h, h->selected) : NULL;
    return channel ? permissions_channel(auth_permissions(h->auth),
                                         history_internal_str(channel, "id"), permission)
                   : PERMISSION_UNKNOWN;
}
static bool sync_permissions(history *h)
{
    permissions *p = auth_permissions(h->auth);
    uint64_t revision = permissions_revision(p);
    if (h->permission_revision == revision)
        return false;
    uint64_t channels = permissions_channel_revision(p);
    if (h->permission_channels != channels) {
        const json_t *metadata = permissions_channels(p);
        if (metadata && !h->recovering) {
            /* Recovery owns its channel -> history sequence. A parallel metadata
             * snapshot must not consume its queued channel refresh. */
            json_t *array = json_array();
            const char *id;
            json_t *value;
            bool ok = array != NULL;
            json_object_foreach((json_t *)metadata, id, value)
            {
                (void)id;
                if (json_array_append(array, value))
                    ok = false;
            }
            json_t *body = json_pack("{s:O}", "channels", array ? array : json_null());
            if (ok && body && history_internal_channels_response(h, body))
                h->want_channels = false;
            json_decref(body);
            json_decref(array);
        }
        h->permission_channels = channels;
    }
    h->permission_revision = revision;
    if (h->selected != SIZE_MAX &&
        history_permission(h, PERMISSION_READ_CHANNEL) == PERMISSION_DENIED) {
        if (h->pending != NONE && h->pending != INBOX && !history_internal_actions_mutating(h)) {
            auth_cancel_read(h->auth);
            h->pending = NONE;
        }
        ++h->selection;
        clear_messages(h);
        h->selected = SIZE_MAX;
        history_internal_notice(h, HISTORY_NOTICE_EVENT,
                                "Acesso ao canal indisponível; rascunho preservado.");
    }
    return true;
}

/* Bit 0: model changed; bit 1: yield before dispatching another request. */
static int process_response(history *h, uint64_t maintenance_now)
{
    if (!auth_response_ready(h->auth))
        return 0;
    bool changed = false;

    bool uncertain = auth_api_uncertain(h->auth);
    bool messages_ready = h->pending == MESSAGES;
    long code;
    uint64_t generation;
    json_t *body = auth_take_response(h->auth, &code, &generation);
    if (h->pending == ACTIONS) {
        if (generation == h->context && h->request_context == h->context) {
            history_internal_actions_response(h, body, code, uncertain);
            if (!h->recovering) {
                history_internal_replay(h);
                history_internal_clear_journal(h);
            }
        }
        json_decref(body);
        if (h->pending == ACTIONS)
            h->pending = NONE;
        return 3;
    }
    if (h->pending == INBOX) {
        char current[37] = "";
        if (history_channel(h, h->selected))
            snprintf(current, sizeof(current), "%s",
                     history_internal_str(history_channel(h, h->selected), "id"));
        if (generation == h->context && h->request_context == h->context) {
            inbox_response(h->inbox, body, code, uncertain);
            history_internal_restore_selection(h, current);
            for (size_t i = 0; i < inbox_dm_count(h->inbox); ++i) {
                permissions_dm(auth_permissions(h->auth),
                               history_internal_str(inbox_dm(h->inbox, i), "id"), true);
                history_internal_cache_user(h, json_object_get(inbox_dm(h->inbox, i), "user"),
                                            false);
            }
            h->want_authors = true;
        }
        json_decref(body);
        h->pending = NONE;
        return 3;
    }
    if (h->pending != NONE && generation == h->context && h->request_context == h->context &&
        (h->pending == USERS || h->pending == EMOJIS || h->pending == PROFILES ||
         h->request_selection == h->selection)) {
        enum request_kind kind = h->pending;
        if (kind == EMOJIS || kind == PROFILES) {
            if (kind == EMOJIS)
                chat_assets_emojis_response(h->assets, body, code);
            else
                chat_assets_profiles_response(h->assets, body, code);
            json_decref(body);
            h->pending = NONE;
            return 3;
        }
        bool was_recovering = h->recovering;
        if (kind == USERS) {
            h->users_failed = code != 200 || !history_internal_users_response(h, body);
            json_decref(body);
            h->pending = NONE;
            return 3;
        }
        bool ok = code == 200 &&
                  (kind == CHANNELS   ? history_internal_channels_response(h, body)
                   : kind == MESSAGES ? h->recovering ? history_internal_recovery_response(h, body)
                                                      : history_internal_messages_response(h, body)
                                      : history_internal_authors_response(h, body));
        if (ok && kind != AUTHORS && !(was_recovering && kind == MESSAGES)) {
            ok = history_internal_replay(h);
            if (!h->recovering)
                history_internal_clear_journal(h);
        }
        if (ok && kind == CHANNELS && h->recovering) {
            json_t *channel = history_channel(h, h->selected);
            if (channel && (!strcmp(history_internal_str(channel, "type"), "text") ||
                            !strcmp(history_internal_str(channel, "type"), "voice")))
                h->want_messages = true;
            else {
                h->recovering = false;
                history_internal_clear_journal(h);
            }
        }
        if (kind == AUTHORS) {
            if (ok) {
                if (h->failed == NONE || h->failed == AUTHORS)
                    history_internal_notice(h, HISTORY_NOTICE_READ, "");
                size_t i;
                json_t *id;
                json_array_foreach(
                    h->summary_ids, i,
                    id) if (json_object_set_new(h->queried, json_string_value(id),
                                                json_integer((json_int_t)maintenance_now))) ok =
                    false;
                /* Resolve another batch when a message contains more than
                 * 1000 distinct names. Omitted IDs are cached only after a
                 * successful response; cancellation and failures can retry. */
                h->want_authors = ok;
            }
            json_decref(h->summary_ids);
            h->summary_ids = NULL;
        }
        if (!ok) {
            json_t *channel = history_channel(h, h->selected);
            if (code == 403 && json_is_true(json_object_get(channel, "_dm")))
                permissions_dm(auth_permissions(h->auth), history_internal_str(channel, "id"),
                               false);
            h->failed = kind;
            if (h->recovering && (code == 0 || code == 429 || code >= 500)) {
                uint64_t now;
                if (net_now_ms(&now)) {
                    h->retry_at = now + net_retry_ms(h->failures, (unsigned)rand());
                    if (h->failures < 5)
                        ++h->failures;
                    if (kind == CHANNELS)
                        h->want_channels = true;
                    else
                        h->want_messages = true;
                }
                history_internal_status(
                    h, "Recuperação pendente. Nova tentativa automática; Ctrl+R: tentar agora.");
            } else {
                h->recovering = false;
                if (kind == AUTHORS)
                    history_internal_status(h, "Autores indisponíveis. Ctrl+R: tentar novamente.");
                else
                    history_internal_status(
                        h, code == 403   ? "Sem permissão para ler este canal."
                           : code == 404 ? "Canal removido ou indisponível."
                           : code == 429 ? "Limite de requisições atingido. Aguarde e use Ctrl+R."
                                         : "Falha ao carregar. Ctrl+R: tentar novamente.");
            }
        } else if (h->failed == kind)
            h->failed = NONE;
        changed = true;
    }
    json_decref(body);
    h->pending = NONE;
    /* Give the UI a pass to mark visible authors before scheduling
     * background reads on the single REST connection. */
    if (messages_ready)
        return 3;

    return changed ? 1 : 0;
}

bool history_poll(history *h)
{
    uint64_t maintenance_now = 0;
    net_now_ms(&maintenance_now);
    bool changed = history_maintain(h, maintenance_now);
    if (h->context != auth_context_generation(h->auth)) {
        history_internal_actions_destroy(h);
        save_reads(h);
        h->read_loaded = h->read_dirty = h->read_save_failed = false;
        h->read_url[0] = h->read_user[0] = 0;
        h->read_save_after = 0;
        h->context = auth_context_generation(h->auth);
        chat_assets_destroy(h->assets);
        h->assets = chat_assets_create();
        inbox_reset(h->inbox);
        h->activity_sent = 0;
        json_object_clear(h->read_messages);
        json_object_clear(h->users);
        json_array_clear(h->directory);
        json_object_clear(h->queried);
        ++h->users_revision;
        h->users_complete = h->users_failed = h->users_partial = false;
        h->user_bytes = 0;
        h->users_id[0] = h->users_time[0] = 0;
        history_reload_channels(h);
        h->pending = NONE;
        changed = true;
        h->epoch = auth_ws_epoch(h->auth);
    }
    load_reads(h);
    changed |= sync_permissions(h);
    uint64_t epoch = auth_ws_epoch(h->auth);
    bool connected = auth_has_session(h->auth) && auth_ws_state(h->auth) == WS_OPEN;
    if ((h->typing_connected && !connected) || epoch != h->epoch) {
        if (h->r8)
            inbox_recover(h->inbox);
        h->typing_count = 0;
        h->typing_sent = 0;
        json_object_clear(h->queried);
        const char *id;
        json_t *user;
        json_object_foreach(h->users, id, user)
        {
            const char *phrase = history_internal_str(user, "typing");
            if (phrase) {
                size_t n = strlen(phrase);
                h->user_bytes -= n;
                json_object_set_new(
                    user, "_cost",
                    json_integer(json_integer_value(json_object_get(user, "_cost")) -
                                 (json_int_t)n));
            }
            json_object_del(user, "typing");
            json_object_del(user, "_presence");
        }
        changed = true;
    }
    h->typing_connected = connected;
    if (epoch != h->epoch) {
        /* Initial snapshots are deferred until the first upgrade. A later
         * upgrade requires authoritative recovery of the displayed window. */
        if (h->active && (h->epoch || h->channels || h->messages))
            history_recover(h);
        h->epoch = epoch;
        changed = true;
    }
    json_t *event;
    while ((event = auth_next_event(h->auth))) {
        history_event(h, event, h->context);
        json_decref(event);
        changed = true;
    }
    changed |= sync_permissions(h);
    int response = process_response(h, maintenance_now);
    changed |= (response & 1) != 0;
    if (h->retry_requested && h->pending == NONE) {
        h->retry_requested = false;
        history_retry(h);
        changed = true;
    }
    if (response & 2)
        return changed;
    uint64_t now;
    if (!net_now_ms(&now))
        return changed;
    if (h->read_dirty && now >= h->read_save_after) {
        changed |= save_reads(h);
        h->read_save_after = now + 2000;
    }
    for (size_t i = 0; i < h->typing_count;) {
        if (now >= h->typing[i].expires) {
            history_internal_typing_remove(h, h->typing[i].id);
            changed = true;
        } else
            ++i;
    }
    ws_state ws = auth_ws_state(h->auth);
    if (ws == WS_QUEUED || ws == WS_CONNECTING)
        return changed;
    if (h->recovering && ws != WS_OPEN)
        return changed;
    if (now < h->retry_at)
        return changed;
    if (h->pending == NONE && auth_has_session(h->auth) && !auth_busy(h->auth)) {
        if (*h->read_visible) {
            json_t *channel = history_channel(h, h->selected);
            const char *latest =
                history_internal_str(json_object_get(channel, "last_message"), "id");
            if (!h->active || (h->failed != NONE && h->failed != AUTHORS) || !latest ||
                strcmp(latest, h->read_visible) || !history_channel_unread(h, channel))
                h->read_visible[0] = 0;
            else if (!h->want_channels && now >= h->read_after) {
                snprintf(h->read_attempt, sizeof(h->read_attempt), "%s", h->read_visible);
                h->read_visible[0] = 0;
                h->read_after = now + 2000;
                h->older = h->newer = false;
                h->want_messages = true;
            }
        }
        if (history_internal_actions_dispatch(h, true))
            return true;
        if (h->r8 && !h->want_channels && !h->want_messages && inbox_dispatch(h->inbox)) {
            h->pending = INBOX;
            h->request_context = h->context;
            return true;
        }
        /* Visible avatars already have author IDs; they must not wait for
         * names from the rest of the history or the entire emoji catalogue. */
        enum request_kind kind = h->want_channels                         ? CHANNELS
                                 : h->want_messages                       ? MESSAGES
                                 : chat_assets_profiles_wanted(h->assets) ? PROFILES
                                 : h->want_authors                        ? AUTHORS
                                 : chat_assets_emojis_wanted(h->assets)   ? EMOJIS
                                 : h->users_active && !h->users_complete && !h->users_failed ? USERS
                                                                                             : NONE;
        if ((kind == NONE || kind == EMOJIS || kind == USERS) &&
            history_internal_actions_dispatch(h, false))
            return true;
        bool started = kind == CHANNELS   ? auth_get_json(h->auth, "/channels")
                       : kind == MESSAGES ? history_internal_request_messages(h)
                       : kind == AUTHORS  ? history_internal_request_authors(h)
                       : kind == USERS    ? history_internal_request_users(h)
                       : kind == EMOJIS   ? auth_emojis(h->auth, chat_assets_emojis_path(h->assets))
                       : kind == PROFILES
                           ? auth_user_profiles(h->auth, chat_assets_profiles_request(h->assets))
                           : false;
        if (started && kind == AUTHORS && !auth_busy(h->auth)) {
            h->want_authors = false;
            if (h->failed == NONE || h->failed == AUTHORS)
                history_internal_notice(h, HISTORY_NOTICE_READ, "");
            json_decref(h->summary_ids);
            h->summary_ids = NULL;
            return changed;
        }
        if (started) {
            h->pending = kind;
            h->request_context = h->context;
            h->request_selection = h->selection;
            if (!h->recovering)
                history_internal_clear_journal(h);
            if (kind == CHANNELS)
                h->want_channels = false;
            else if (kind == MESSAGES)
                h->want_messages = false;
            else if (kind == AUTHORS)
                h->want_authors = false;
            changed = true;
        } else if (kind != NONE && auth_get_state(h->auth) == AUTH_READY &&
                   !auth_response_ready(h->auth)) {
            if (kind == USERS) {
                h->users_failed = true;
                return true;
            }
            if (kind == EMOJIS || kind == PROFILES) {
                if (kind == EMOJIS)
                    chat_assets_emojis_response(h->assets, NULL, 0);
                else
                    chat_assets_profiles_response(h->assets, NULL, 0);
                return true;
            }
            h->failed = kind;
            h->want_channels = h->want_messages = h->want_authors = false;
            history_internal_status(
                h, "Não foi possível preparar a consulta. Ctrl+R: tentar novamente.");
            changed = true;
        }
    }
    return changed;
}
