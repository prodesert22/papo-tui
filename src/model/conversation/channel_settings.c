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
const char *history_channel_notifications(const history *h, const char *id)
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
bool history_channel_notifications_pending(const history *h)
{
    return h && (h->notification_queued || h->pending == CHANNEL_SETTINGS);
}
bool history_set_channel_notifications(history *h, const char *id, const char *mode)
{
    if (!h || !auth_has_session(h->auth) || h->context != auth_context_generation(h->auth) ||
        !history_uuid(id) || !valid_mode(mode) || !settings_channel(h, id) ||
        history_channel_notifications_pending(h) ||
        permissions_channel(auth_permissions(h->auth), id, PERMISSION_READ_CHANNEL) !=
            PERMISSION_ALLOWED)
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
        id = history_internal_str(c, "id");
        if (!history_channel_unread(NULL, c))
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
