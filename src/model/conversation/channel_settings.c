#include "model/conversation/private.h"

static bool valid_mode(const char *mode)
{
    return mode && (!strcmp(mode, "all") || !strcmp(mode, "only_mentions") || !strcmp(mode, "off"));
}
static json_t *settings_channel(const history *h, const char *id)
{
    json_t *c = json_array_get(h->channels, history_internal_find(h->channels, id));
    return c && !json_is_true(json_object_get(c, "_dm")) &&
                   strcmp(history_internal_str(c, "type"), "category")
               ? c
               : NULL;
}
/* The mode saved on the server, or its default when none was saved. */
static const char *server_notifications(const history *h, const char *id)
{
    json_t *c = h && history_uuid(id) ? settings_channel(h, id) : NULL;
    if (!c)
        return NULL;
    const char *mode = history_internal_str(c, "notification_settings");
    return !json_object_get(c, "notification_settings") ? "only_mentions"
           : history_internal_string_field(c, "notification_settings", 13, false) &&
                   valid_mode(mode)
               ? mode
               : NULL;
}
const char *history_channel_notifications(const history *h, const char *id)
{
    const char *server = server_notifications(h, id);
    const char *local = server ? inbox_local_notifications(h->inbox, id) : NULL;
    return local ? local : server;
}
bool history_channel_notifications_synced(const history *h, const char *id)
{
    return !h || !inbox_local_notifications(h->inbox, id);
}
static bool settings_allowed(const history *h, const char *id)
{
    return h && auth_has_session(h->auth) && h->context == auth_context_generation(h->auth) &&
           history_uuid(id) && settings_channel(h, id) &&
           permissions_channel(auth_permissions(h->auth), id, PERMISSION_READ_CHANNEL) ==
               PERMISSION_ALLOWED;
}
bool history_refresh_channel_settings(history *h)
{
    if (!h || !auth_has_session(h->auth) || !h->channels || h->recovering || h->want_channels ||
        h->pending == CHANNELS)
        return false;
    h->want_channels = h->background_channels = true;
    return true;
}
bool history_set_channel_notifications_synced(history *h, const char *id, bool synced)
{
    if (!settings_allowed(h, id) || synced == history_channel_notifications_synced(h, id))
        return false;
    /* Turning sync off keeps the current mode on this device; turning it on
     * returns to the server's preference without sending the device mode. */
    if (!inbox_set_local_notifications(h->inbox, id, synced ? NULL : server_notifications(h, id))) {
        history_internal_notice(h, HISTORY_NOTICE_MUTATION,
                                "Não foi possível salvar a preferência; arquivo preservado.");
        return false;
    }
    history_internal_notice(h, HISTORY_NOTICE_MUTATION,
                            synced ? "Notificações do canal sincronizadas com o servidor."
                                   : "Notificações do canal salvas só neste dispositivo.");
    return true;
}
bool history_channel_notifications_pending(const history *h)
{
    return h && (h->notification_queued || h->pending == CHANNEL_SETTINGS);
}
bool history_set_channel_notifications(history *h, const char *id, const char *mode)
{
    if (!settings_allowed(h, id) || !valid_mode(mode))
        return false;
    if (!history_channel_notifications_synced(h, id)) {
        bool saved = inbox_set_local_notifications(h->inbox, id, mode);
        history_internal_notice(h, HISTORY_NOTICE_MUTATION,
                                saved
                                    ? "Notificações do canal salvas neste dispositivo."
                                    : "Não foi possível salvar a preferência; arquivo preservado.");
        return saved;
    }
    if (history_channel_notifications_pending(h))
        return false;
    snprintf(h->notification_channel, sizeof(h->notification_channel), "%s", id);
    snprintf(h->notification_mode, sizeof(h->notification_mode), "%s", mode);
    h->notification_queued = true;
    history_internal_notice(h, HISTORY_NOTICE_MUTATION, "Salvando notificações do canal…");
    return true;
}
bool history_internal_channel_settings_dispatch(history *h)
{
    if (!h->notification_queued)
        return false;
    const char *id = h->notification_channel;
    if (!settings_channel(h, id) ||
        permissions_channel(auth_permissions(h->auth), id, PERMISSION_READ_CHANNEL) !=
            PERMISSION_ALLOWED) {
        h->notification_queued = false;
        history_internal_notice(h, HISTORY_NOTICE_MUTATION,
                                "Canal indisponível; configuração cancelada.");
        return false;
    }
    char path[160];
    snprintf(path, sizeof(path), "/channels/%s/user/%s/settings", id, auth_user_id(h->auth));
    json_t *body = json_pack("{s:s}", "notification_settings", h->notification_mode);
    bool started = body && auth_mutate_json(h->auth, "POST", path, body);
    json_decref(body);
    if (started) {
        h->notification_queued = false;
        h->pending = CHANNEL_SETTINGS;
        h->request_context = h->context;
    }
    return started;
}
void history_internal_channel_settings_response(history *h, json_t *body, long code, bool uncertain)
{
    const char *mode = history_internal_str(body, "notification_settings");
    const char *id = history_internal_str(body, "channel_id");
    const char *user = history_internal_str(body, "user_id");
    bool ok = code == 200 && id && user && valid_mode(mode) &&
              history_internal_string_field(body, "channel_id", 36, false) &&
              history_internal_string_field(body, "user_id", 36, false) &&
              history_internal_string_field(body, "notification_settings", 13, false) &&
              !strcmp(id, h->notification_channel) && !strcmp(user, auth_user_id(h->auth)) &&
              !strcmp(mode, h->notification_mode);
    json_t *c = settings_channel(h, h->notification_channel);
    if (ok && c)
        ok = !json_object_set_new(c, "notification_settings", json_string(mode));
    if (ok || uncertain || !code || code == 200)
        h->want_channels = true; /* Reconcile metadata only; never replay the mutation. */
    history_internal_notice(
        h, HISTORY_NOTICE_MUTATION,
        ok            ? "Notificações do canal atualizadas."
        : code == 429 ? "Limite de requisições. Aguarde e escolha a opção novamente."
        : code == 403 ? "Sem permissão para alterar as notificações deste canal."
        : code == 404 ? "Canal indisponível. Configuração não confirmada."
        : uncertain || !code || code == 200
            ? "Resultado não confirmado. Atualize os canais antes de tentar novamente."
            : "Falha ao salvar notificações. Escolha novamente para tentar.");
    h->notification_channel[0] = h->notification_mode[0] = 0;
}
void history_internal_notification_reads(history *h)
{
    const char *id;
    json_t *cursor;
    json_object_foreach(h->read_messages, id, cursor) inbox_read_through(h->inbox, id, cursor);
    for (size_t i = 0; i < history_channel_count(h); ++i) {
        json_t *c = history_channel(h, i), *latest = json_object_get(c, "last_message");
        const char *read, *time;
        id = history_internal_str(c, "id");
        if (history_internal_server_read(h, c, &read, &time))
            inbox_read_until(h->inbox, id, read, time);
        else if (!history_channel_unread(NULL, c))
            inbox_read_through(h->inbox, id, latest);
    }
}
bool history_instance_unread(const history *h)
{
    if (!h)
        return false;
    if (inbox_has_activity(h->inbox) || inbox_unseen_conversations(h->inbox))
        return true;
    for (size_t i = 0; i < history_channel_count(h); ++i) {
        json_t *c = history_channel(h, i);
        if (history_channel_visible(h, i) &&
            !inbox_conversation_muted(h->inbox, history_internal_str(c, "id")) &&
            history_channel_unread(h, c))
            return true;
    }
    return false;
}
/* Reads are registered explicitly: listing messages does not move the server's
 * read position. PUT /channels/{id}/read advances it (forward only) and marks
 * the user's notifications in that channel up to the message read. */
static bool cursor_before(const char *at, const char *a, const char *bt, const char *b)
{
    int ordering;
    if (!session_time_compare(at, bt, &ordering))
        return false;
    return ordering < 0 || (!ordering && strcasecmp(a, b) < 0);
}
static json_t *read_channel(const history *h, const char *id)
{
    for (size_t i = 0; i < history_channel_count(h); ++i)
        if (!strcmp(history_internal_str(history_channel(h, i), "id"), id))
            return history_channel(h, i);
    return NULL;
}
bool history_internal_read_needed(const history *h, const json_t *channel)
{
    json_t *latest = json_object_get(channel, "last_message");
    const char *last = history_internal_str(latest, "id"),
               *time = history_internal_str(latest, "created_at"),
               *type = history_internal_str(channel, "type"),
               *read = history_internal_str(channel, "last_read_message"), *read_time;
    if (!history_uuid(last) || (type && !strcmp(type, "category")))
        return false;
    /* The channel summary may omit created_at: compare identifiers then. */
    bool behind = time && history_internal_server_read(h, channel, &read, &read_time)
                      ? cursor_before(read_time, read, time, last)
                      : !read || strcasecmp(read, last);
    return behind ||
           (time && inbox_unread_before(h->inbox, history_internal_str(channel, "id"), last, time));
}
bool history_internal_read_send(history *h, const char *channel, const char *id, const char *time)
{
    char path[96];
    if (!time)
        time = "";
    if (!history_uuid(channel) || !history_uuid(id) || strlen(time) >= sizeof(h->read_time))
        return false;
    snprintf(path, sizeof(path), "/channels/%s/read", channel);
    json_t *body = json_pack("{s:s}", "message_id", id);
    bool started = body && auth_mutate_json(h->auth, "PUT", path, body);
    json_decref(body);
    /* Recorded even when it cannot start: never repeated automatically. */
    json_t *sent = json_string(id);
    if (!sent || json_object_set_new(h->sent_reads, channel, sent))
        return false;
    if (!started)
        return false;
    snprintf(h->read_channel, sizeof(h->read_channel), "%s", channel);
    snprintf(h->read_message, sizeof(h->read_message), "%s", id);
    snprintf(h->read_time, sizeof(h->read_time), "%s", time);
    h->pending = CHANNEL_READ;
    h->request_context = h->context;
    return true;
}
/* Notification records before a known read position stay unread on the
 * server until a read is registered there; register it once per position.
 * Never fetches history. */
bool history_internal_read_dispatch(history *h)
{
    if (!h->r8)
        return false;
    for (size_t i = 0; i < history_channel_count(h); ++i) {
        json_t *c = history_channel(h, i),
               *local = json_object_get(h->read_messages, history_internal_str(c, "id"));
        const char *channel = history_internal_str(c, "id"), *id = NULL, *time = NULL;
        if (!history_internal_server_read(h, c, &id, &time))
            id = time = NULL;
        if (local && (!id || cursor_before(time, id, history_internal_str(local, "created_at"),
                                           history_internal_str(local, "id")))) {
            id = history_internal_str(local, "id");
            time = history_internal_str(local, "created_at");
        }
        const char *sent = json_string_value(json_object_get(h->sent_reads, channel));
        if (!id || !time || (sent && !strcasecmp(sent, id)) ||
            !inbox_unread_before(h->inbox, channel, id, time))
            continue;
        if (history_internal_read_send(h, channel, id, time))
            return true;
    }
    return false;
}
void history_internal_read_response(history *h, long code)
{
    json_t *c = read_channel(h, h->read_channel);
    if (code == 204 && c) {
        json_t *latest = json_object_get(c, "last_message");
        const char *read, *time;
        const char *last = history_internal_str(latest, "id");
        if (*h->read_time && last && !strcasecmp(h->read_message, last)) {
            json_t *message = json_pack("{s:s,s:s,s:s}", "id", h->read_message, "created_at",
                                        h->read_time, "channel_id", h->read_channel);
            if (message)
                history_internal_remember_read(h, message);
            json_decref(message);
        }
        if (!*h->read_time)
            json_object_set_new(c, "last_read_message", json_string(h->read_message));
        else if (!history_internal_server_read(h, c, &read, &time) ||
                 cursor_before(time, read, h->read_time, h->read_message)) {
            json_object_set_new(c, "last_read_message", json_string(h->read_message));
            json_object_set_new(c, "last_read_at", json_string(h->read_time));
        }
        /* Reload only what the server changed with this read. */
        if (json_is_true(json_object_get(c, "_dm")))
            inbox_read_conversation(h->inbox, h->read_channel);
        if (*h->read_time &&
            inbox_unread_before(h->inbox, h->read_channel, h->read_message, h->read_time)) {
            inbox_server_read(h->inbox, h->read_channel, h->read_message, h->read_time);
            inbox_refresh(h->inbox);
        }
    }
    h->read_channel[0] = h->read_message[0] = h->read_time[0] = 0;
}
