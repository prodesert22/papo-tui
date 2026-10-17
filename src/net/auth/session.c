#include "net/auth/private.h"

char *auth_internal_cookie_snapshot(auth_client *client)
{
    struct curl_slist *cookies = NULL;
    char *snapshot = NULL;
    if (curl_easy_getinfo(client->easy, CURLINFO_COOKIELIST, &cookies) != CURLE_OK)
        return NULL;
    bool ambiguous = false;
    for (struct curl_slist *item = cookies; item; item = item->next) {
        const char *name = item->data;
        for (int i = 0; name && i < 5; ++i) {
            const char *tab = strchr(name, '\t');
            name = tab ? tab + 1 : NULL;
        }
        if (name && strncmp(name, "Auth\t", 5) == 0 && name[5]) {
            if (snapshot)
                ambiguous = true;
            else
                snapshot = strdup(name + 5);
        }
    }
    for (struct curl_slist *item = cookies; item; item = item->next) {
        if (!item->data)
            continue;
        volatile char *p = item->data;
        size_t length = strlen(item->data);
        while (length--)
            *p++ = 0;
    }
    curl_slist_free_all(cookies);
    if (ambiguous)
        auth_internal_erase_free(&snapshot);
    return snapshot;
}

static bool begin_refresh(auth_client *client)
{
    auth_internal_erase_free(&client->refresh_cookie);
    client->refresh_cookie = auth_internal_cookie_snapshot(client);
    if (!client->refresh_cookie) {
        auth_internal_fail(client, "A sessão não possui um cookie válido. Entre novamente.");
        return false;
    }
    client->after_refresh = true;
    return auth_internal_request(client, AUTH_REFRESH, "/auth/refresh", NULL, true);
}

bool auth_refresh(auth_client *client)
{
    /* Explicit session renewal takes precedence over an internal metadata read.
     * Preserve its refresh intent, external response ownership and global budget. */
    if (auth_has_session(client) &&
        (client->operation == AUTH_PERMISSION_ROLES ||
         client->operation == AUTH_PERMISSION_CHANNELS ||
         client->operation == AUTH_PERMISSION_SERVER) &&
        (client->queued || client->attached)) {
        client->permission_wanted |= client->operation == AUTH_PERMISSION_ROLES      ? 2U
                                     : client->operation == AUTH_PERMISSION_CHANNELS ? 4U
                                                                                     : 1U;
        auth_internal_detach(client);
        client->state = AUTH_READY;
    }
    if (!auth_has_session(client) || client->queued || client->attached ||
        client->state != AUTH_READY || client->api_path || client->logout_requested)
        return false;
    ws_state state = ws_get_state(client->websocket);
    if (state == WS_QUEUED || state == WS_CONNECTING)
        ws_abort(client->websocket);
    return begin_refresh(client);
}

bool auth_internal_recover_session(auth_client *client)
{
    client->session_rejected = true; /* Cleared only by a verified whoami. */
    client->authenticated = false;
    client->recovering = client->after_refresh = true;
    ws_abort(client->websocket);
    return begin_refresh(client);
}

void auth_set_session_gate(auth_client *client, auth_session_gate gate, void *context)
{
    client->session_gate = gate;
    client->gate_context = context;
}

void auth_session_free(char **snapshot)
{
    auth_internal_erase_free(snapshot);
}

bool auth_session_rejected(const auth_client *client)
{
    return client->session_rejected;
}

char *auth_session_export(auth_client *client)
{
    if (!auth_has_session(client) || auth_busy(client))
        return NULL;
    struct curl_slist *cookies = NULL;
    if (curl_easy_getinfo(client->easy, CURLINFO_COOKIELIST, &cookies) != CURLE_OK)
        return NULL;
    const char *row = NULL;
    bool ambiguous = false;
    for (struct curl_slist *item = cookies; item; item = item->next)
        if (auth_internal_session_cookie_valid(item->data, client->base)) {
            if (row)
                ambiguous = true;
            row = item->data;
        }
    char *result = NULL;
    if (row && !ambiguous) {
        json_t *obj = json_pack("{s:i,s:s,s:s,s:s}", "version", 1, "url", client->base, "user_id",
                                client->user_id, "cookie", row);
        if (obj) {
            result = json_dumps(obj, JSON_COMPACT | JSON_SORT_KEYS);
            volatile char *secret =
                (volatile char *)json_string_value(json_object_get(obj, "cookie"));
            if (secret)
                for (size_t i = 0; i < strlen(row); ++i)
                    secret[i] = 0;
            json_decref(obj);
        }
    }
    for (struct curl_slist *item = cookies; item; item = item->next) {
        if (!item->data)
            continue;
        volatile char *secret = item->data;
        for (size_t i = 0, n = strlen(item->data); i < n; ++i)
            secret[i] = 0;
    }
    curl_slist_free_all(cookies);
    return result;
}

bool auth_session_restore(auth_client *client, const char *snapshot, const char *expected_url,
                          bool allow_local_http)
{
    if (auth_busy(client) || auth_has_session(client) || !snapshot || strlen(snapshot) > 16384)
        return false;
    json_error_t error;
    json_t *obj = json_loads(snapshot, JSON_REJECT_DUPLICATES, &error);
    const char *url = auth_internal_json_text(obj, "url", 2049),
               *id = auth_internal_json_text(obj, "user_id", sizeof(client->user_id));
    const char *cookie = auth_internal_json_text(obj, "cookie", 8193);
    char message[192], *base = url ? auth_normalize_url(url, message, sizeof(message)) : NULL;
    bool safe_fields = base && expected_url && !strcmp(base, expected_url) && !strcmp(base, url) &&
                       id && json_integer_value(json_object_get(obj, "version")) == 1 &&
                       auth_internal_session_cookie_valid(cookie, base);
    bool policy = base && (allow_local_http || strncmp(base, "http://", 7));
    bool ok = safe_fields && policy;
    if (ok) {
        auth_internal_detach(client);
        ws_abort(client->websocket);
        auth_internal_forget_inputs(client);
        auth_internal_clear_api(client);
        auth_internal_clear_identity(client);
        auth_internal_clear_cookies(client);
        curl_free(client->base);
        client->base = base;
        base = NULL;
        client->restoring = true;
        client->session_rejected = client->logout_requested = client->ws_requested = false;
        client->name[0] = 0;
        snprintf(client->user_id, sizeof(client->user_id), "%s", id);
        ok = curl_easy_setopt(client->easy, CURLOPT_COOKIELIST, cookie) == CURLE_OK &&
             auth_internal_request(client, AUTH_SERVER, "/server", NULL, false);
    }
    if (cookie) {
        volatile char *secret = (volatile char *)cookie;
        for (size_t i = 0, n = strlen(cookie); i < n; ++i)
            secret[i] = 0;
    }
    curl_free(base);
    json_decref(obj);
    if (!ok) {
        /* HTTP policy failure is reversible with --allow-local-http; malformed
         * or expired snapshots must never be retried from persistent storage. */
        client->session_rejected = !safe_fields;
        if (safe_fields && !policy)
            auth_internal_input_fail(
                client, AUTH_INPUT_URL,
                "HTTP local exige --allow-local-http. Use HTTPS para acesso remoto.");
        else
            auth_internal_fail(client, "Sessão salva indisponível. Entre novamente.");
    }
    return ok;
}
