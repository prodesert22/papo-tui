#include "net/auth/private.h"
#include <glib.h>

bool auth_get_json(auth_client *client, const char *path)
{
    if (!auth_has_session(client) || client->state != AUTH_READY || client->queued ||
        client->attached || client->api_path || client->response_ready || !path || path[0] != '/' ||
        path[1] == '/' || strlen(path) > 4096)
        return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        if (*p < 32 || *p == 127 || *p == '#')
            return false;
    client->api_path = strdup(path);
    if (!client->api_path)
        return false;
    client->api_retried = false;
    return auth_internal_request(client, AUTH_API_GET, client->api_path, NULL, false);
}

bool auth_response_ready(const auth_client *client)
{
    return client->response_ready;
}

bool auth_api_uncertain(const auth_client *client)
{
    return client->api_uncertain;
}

bool auth_mutate_json(auth_client *client, const char *method, const char *path, const json_t *body)
{
    if (!method || (strcmp(method, "POST") && strcmp(method, "PUT") && strcmp(method, "DELETE")) ||
        !auth_has_session(client) || client->state != AUTH_READY || client->queued ||
        client->attached || client->api_path || client->response_ready || !path || path[0] != '/' ||
        path[1] == '/' || strlen(path) > 4096 ||
        (strcmp(method, "DELETE") && !json_is_object(body)))
        return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        if (*p < 32 || *p == 127 || *p == '#')
            return false;
    char *copy = strdup(path);
    json_t *payload = body ? json_deep_copy(body) : NULL;
    if (!copy || (body && !payload)) {
        free(copy);
        json_decref(payload);
        return false;
    }
    client->api_path = copy;
    client->api_body = payload;
    client->api_method = !strcmp(method, "POST")  ? "POST"
                         : !strcmp(method, "PUT") ? "PUT"
                                                  : "DELETE";
    client->api_uncertain = false;
    return auth_internal_request(client, AUTH_API_MUTATION, copy, payload, payload != NULL);
}

void auth_cancel_read(auth_client *client)
{
    if (client->operation == AUTH_API_MUTATION && (client->queued || client->attached))
        return;
    if (!client->api_path && !client->response_ready)
        return;
    if (client->operation == AUTH_API_GET && (client->queued || client->attached)) {
        auth_internal_detach(client);
        free(client->response);
        client->response = NULL;
        client->response_length = 0;
        client->overflow = false;
        if (client->authenticated && !client->logout_requested)
            client->state = AUTH_READY;
    }
    auth_internal_clear_api(client);
}

static bool user_batch(auth_client *client, const json_t *ids, const char *path, size_t limit)
{
    if (!auth_has_session(client) || client->state != AUTH_READY || client->queued ||
        client->attached || client->api_path || client->response_ready || !json_is_array(ids) ||
        !json_array_size(ids) || json_array_size(ids) > limit)
        return false;
    size_t index;
    json_t *id;
    json_array_foreach(ids, index, id)
    {
        const char *value = json_string_value(id);
        if (!value || json_string_length(id) != 36)
            return false;
        for (size_t i = 0; i < 36; ++i)
            if (i == 8 || i == 13 || i == 18 || i == 23 ? value[i] != '-'
                                                        : !((value[i] >= '0' && value[i] <= '9') ||
                                                            (value[i] >= 'a' && value[i] <= 'f') ||
                                                            (value[i] >= 'A' && value[i] <= 'F')))
                return false;
    }
    json_t *copy = json_deep_copy(ids);
    if (!copy)
        return false;
    json_t *body = json_pack("{s:o}", "ids", copy);
    if (!body)
        return false;
    client->api_path = strdup(path);
    if (!client->api_path) {
        json_decref(body);
        return false;
    }
    client->api_body = body;
    client->api_retried = false;
    return auth_internal_request(client, AUTH_API_GET, client->api_path, body, true);
}

bool auth_user_summaries(auth_client *client, const json_t *ids)
{
    return user_batch(client, ids, "/users/user_summary_batch", 1000);
}

bool auth_user_profiles(auth_client *client, const json_t *ids)
{
    return user_batch(client, ids, "/users/profile_batch", 4);
}

bool auth_emojis(auth_client *client, const char *path)
{
    return path && !strncmp(path, "/emojis?order=asc", 17) && auth_get_json(client, path);
}

json_t *auth_take_response(auth_client *client, long *status, uint64_t *generation)
{
    if (!client->response_ready)
        return NULL;
    json_t *body = client->api_response;
    client->api_response = NULL;
    client->response_ready = false;
    if (status)
        *status = client->api_status;
    if (generation)
        *generation = client->response_generation;
    return body;
}

static bool api_uuid(const char *id)
{
    if (!id || strlen(id) != 36)
        return false;
    for (size_t i = 0; i < 36; ++i)
        if (i == 8 || i == 13 || i == 18 || i == 23 ? id[i] != '-' : !g_ascii_isxdigit(id[i]))
            return false;
    return true;
}

bool auth_search_messages(auth_client *client, const json_t *filters, const char *since,
                          const char *last_id)
{
    if (!json_is_object(filters) || !auth_has_session(client) || client->state != AUTH_READY ||
        client->queued || client->attached || client->api_path || client->response_ready ||
        (!!(since && *since) != !!(last_id && *last_id)) ||
        (last_id && *last_id && !api_uuid(last_id)))
        return false;
    char path[512] = "/search";
    if (since && *since) {
        if (strlen(since) > 64)
            return false;
        char *encoded = curl_easy_escape(NULL, since, 0);
        if (!encoded)
            return false;
        snprintf(path, sizeof(path), "/search?since=%s&last_id=%s", encoded, last_id);
        curl_free(encoded);
    }
    json_t *copy = json_deep_copy(filters);
    char *endpoint = strdup(path);
    if (!copy || !endpoint) {
        json_decref(copy);
        free(endpoint);
        return false;
    }
    client->api_path = endpoint;
    client->api_body = copy;
    client->api_retried = false;
    return auth_internal_request(client, AUTH_API_GET, endpoint, copy, true);
}

bool auth_pin_message(auth_client *client, const char *channel, const char *message, bool remove)
{
    if (!api_uuid(channel) || !api_uuid(message) || !auth_has_session(client) ||
        client->state != AUTH_READY || client->queued || client->attached || client->api_path ||
        client->response_ready)
        return false;
    if (permissions_role(client->permissions, PERMISSION_PIN_MESSAGE) != PERMISSION_ALLOWED ||
        permissions_channel(client->permissions, channel, PERMISSION_READ_CHANNEL) !=
            PERMISSION_ALLOWED)
        return false;
    char path[160];
    snprintf(path, sizeof(path), "/channels/%s/messages/%s/pin", channel, message);
    client->api_path = strdup(path);
    if (!client->api_path)
        return false;
    client->api_method = remove ? "DELETE" : "POST";
    client->api_uncertain = false;
    return auth_internal_request(client, AUTH_API_MUTATION, client->api_path, NULL, !remove);
}
