#include "net/auth/private.h"

permissions *auth_permissions(auth_client *client)
{
    return client ? client->permissions : NULL;
}
void auth_permissions_refresh(auth_client *client)
{
    if (!client)
        return;
    client->permission_wanted |= 7U;
    client->permission_due = 0;
    if (auth_has_session(client))
        client->recheck_at = 0;
}
const char *auth_permissions_notice(const auth_client *client)
{
    return client->permission_failed   ? "Permissões indisponíveis. Ctrl+R: tentar novamente."
           : client->permission_wanted ? "Atualizando permissões…"
                                       : "";
}
void auth_internal_permissions_observe(void *context, const json_t *event)
{
    auth_client *client = context;
    if (!auth_has_session(client))
        return;
    if (permissions_event(client->permissions, event)) {
        /* Membership changed locally already. Reconcile missing definitions and
         * identity without waiting for the periodic metadata read. */
        client->permission_wanted |= 2U;
        client->recheck_at = 0;
    }
}
void auth_internal_permissions_poll(auth_client *client, uint64_t now)
{
    if (!auth_has_session(client) || (client->ws_managed && !client->ws_active) || client->queued ||
        client->attached || client->state != AUTH_READY || client->response_ready ||
        client->send_ready || client->api_path)
        return;
    if (!client->permission_wanted && now >= client->permission_due)
        client->permission_wanted = 7U;
    unsigned bit = client->permission_wanted & 2U   ? 2U
                   : client->permission_wanted & 4U ? 4U
                                                    : client->permission_wanted & 1U;
    if (!bit)
        return;
    auth_state state = bit == 2U   ? AUTH_PERMISSION_ROLES
                       : bit == 4U ? AUTH_PERMISSION_CHANNELS
                                   : AUTH_PERMISSION_SERVER;
    const char *path = bit == 2U ? "/roles" : bit == 4U ? "/channels" : "/server";
    client->permission_wanted &= ~bit;
    client->permission_due = now + 30000;
    /* These internal reads never occupy the external caller's response slot. */
    auth_internal_request(client, state, path, NULL, false);
}
void auth_internal_permissions_response(auth_client *client, CURLcode result, long status)
{
    permission_source source = client->operation == AUTH_PERMISSION_ROLES ? PERMISSIONS_ROLES
                               : client->operation == AUTH_PERMISSION_CHANNELS
                                   ? PERMISSIONS_CHANNELS
                                   : PERMISSIONS_SERVER;
    json_error_t error;
    json_t *body = result == CURLE_OK && status == 200
                       ? json_loadb(client->response ? client->response : "",
                                    client->response_length, JSON_REJECT_DUPLICATES, &error)
                       : NULL;
    bool ok = body && permissions_update(client->permissions, source, body);
    if (!ok)
        permissions_invalidate(client->permissions, source);
    unsigned bit = source == PERMISSIONS_ROLES ? 2U : source == PERMISSIONS_CHANNELS ? 4U : 1U;
    if (ok) {
        client->permission_failed &= ~bit;
        client->permission_retried &= ~bit;
    } else
        client->permission_failed |= bit;
    json_decref(body);
    client->state = AUTH_READY;
    if (status == 429)
        net_rate_defer(client->retry_after);
    if (status == 401) {
        if (client->permission_retried & bit)
            auth_internal_fail(
                client, "Não foi possível confirmar permissões após renovar. Entre novamente.");
        else {
            client->permission_retried |= bit;
            auth_internal_recover_session(client);
        }
    } else if (status == 403 && auth_internal_problem_is(client, "banned"))
        auth_internal_fail(client, "O servidor bloqueou esta conta. Entre novamente.");
    free(client->response);
    client->response = NULL;
    client->response_length = 0;
}
bool auth_guard_message(auth_client *client, const json_t *message, channel_permission permission,
                        bool edit, bool deletion)
{
    const char *channel = json_string_value(json_object_get(message, "channel_id"));
    if (!client->queued || client->operation != AUTH_API_MUTATION || !channel)
        return false;
    client->permission_guard = permission;
    client->permission_edit = edit;
    client->permission_delete = deletion;
    json_decref(client->permission_message);
    client->permission_message = json_pack(
        "{s:s,s:O,s:O}", "channel_id", channel, "author_id",
        json_object_get(message, "author_id") ? json_object_get(message, "author_id") : json_null(),
        "webhook_id",
        json_object_get(message, "webhook_id") ? json_object_get(message, "webhook_id")
                                               : json_null());
    if (!client->permission_message) {
        auth_internal_complete(client, CURLE_OK, 403);
        return false;
    }
    return true;
}
bool auth_internal_permission_admitted(auth_client *client)
{
    permission_decision decision = PERMISSION_ALLOWED;
    if (client->operation == AUTH_MESSAGE_SEND)
        decision = permissions_channel(client->permissions, client->send_channel,
                                       PERMISSION_SEND_MESSAGES);
    else if (client->operation == AUTH_API_MUTATION && client->permission_message) {
        if (client->permission_edit || client->permission_delete)
            decision = permissions_message(client->permissions, client->permission_message,
                                           client->permission_edit);
        else
            decision = permissions_channel(
                client->permissions,
                json_string_value(json_object_get(client->permission_message, "channel_id")),
                client->permission_guard);
    }
    if (client->operation == AUTH_API_GET && client->api_path &&
        !strncmp(client->api_path, "/channels/", 10) && strlen(client->api_path) >= 55 &&
        !strncmp(client->api_path + 46, "/messages", 9)) {
        char id[37];
        memcpy(id, client->api_path + 10, 36);
        id[36] = 0;
        if (g_uuid_string_is_valid(id))
            decision = permissions_channel(client->permissions, id, PERMISSION_READ_CHANNEL);
    }
    if (decision == PERMISSION_ALLOWED)
        return true;
    auth_internal_complete(client, CURLE_OK, 403);
    return false;
}
