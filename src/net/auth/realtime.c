#include "net/auth/private.h"

void auth_internal_reconnect_later(auth_client *client, uint64_t now)
{
    client->ws_retry_at = now + net_retry_ms(client->ws_failures, (unsigned)rand());
    if (client->ws_failures < 5)
        ++client->ws_failures;
    client->ws_validate = true;
    client->ws_validated = false;
    client->ws_stable_at = 0;
}

void auth_poll(auth_client *client)
{
    uint64_t tick;
    if (!net_now_ms(&tick)) {
        auth_internal_fail(client, "Não foi possível acompanhar a sessão.");
        return;
    }
    if (client->ws_managed && *client->user_id && client->expires_at &&
        tick >= client->expires_at && !client->logout_requested) {
        auth_internal_fail(client, "Sua sessão expirou sem renovação. Entre novamente.");
        return;
    }
    if (client->ws_managed && client->validation_deadline && tick >= client->validation_deadline &&
        ws_get_state(client->websocket) == WS_OPEN && !client->logout_requested) {
        ws_abort(client->websocket);
        auth_internal_reconnect_later(client, tick);
        client->validation_wait = true;
    }
    if (client->ws_managed && client->ws_active && client->ws_requested && *client->user_id &&
        !client->logout_requested && !client->queued && !client->attached &&
        client->state == AUTH_READY && tick >= client->ws_retry_at) {
        ws_state state = ws_get_state(client->websocket);
        if (state == WS_ERROR || state == WS_CLOSED ||
            (state == WS_IDLE && (client->ws_epoch || client->ws_validate))) {
            if (client->ws_validated && auth_has_session(client)) {
                client->ws_validated = false;
                ws_connect(client->websocket, client->base);
            } else {
                client->ws_validate = true;
                if (auth_internal_request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false))
                    client->state = AUTH_READY;
            }
        }
    }
    if (client->state == AUTH_READY && client->authenticated && !client->queued &&
        !client->attached) {
        uint64_t now;
        if (!net_now_ms(&now)) {
            auth_internal_fail(client, "Não foi possível acompanhar a sessão.");
            return;
        }
        if (client->validation_wait ||
            (client->ws_managed && client->ws_requested &&
             ws_get_state(client->websocket) != WS_OPEN)) { /* Recovery validates before refresh. */
        } else if (now >= client->refresh_at)
            auth_refresh(client);
        else if (now >= client->recheck_at &&
                 auth_internal_request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false))
            client->state = AUTH_READY; /* Background REST validation keeps logout available. */
    }
    ws_state ws_before = ws_get_state(client->websocket);
    if (client->operation != AUTH_REFRESH || (!client->queued && !client->attached) ||
        (ws_before != WS_QUEUED && ws_before != WS_CONNECTING))
        ws_poll(client->websocket);
    ws_state ws_after = ws_get_state(client->websocket);
    if (ws_before != ws_after && ws_after == WS_OPEN) {
        ++client->ws_epoch;
        client->ws_stable_at = tick + 30000;
        client->ws_retry_at = 0;
    }
    if (ws_after == WS_OPEN && client->ws_stable_at && tick >= client->ws_stable_at) {
        client->ws_failures = 0;
        client->ws_stable_at = 0;
    }
    if (client->ws_managed && *client->user_id && ws_before != ws_after &&
        (ws_after == WS_ERROR || ws_after == WS_CLOSED))
        auth_internal_reconnect_later(client, tick);
    if (client->authenticated && ws_before != ws_after &&
        (ws_after == WS_ERROR || ws_after == WS_CLOSED))
        client->recheck_at = 0;
    if (client->queued) {
        if (!client->gate_passed && client->session_gate &&
            (client->operation == AUTH_REFRESH || client->operation == AUTH_LOGOUT)) {
            int permit = client->session_gate(client->gate_context, client);
            if (!permit)
                return;
            if (permit < 0) {
                auth_internal_fail(client,
                                   "Não foi possível proteger a sessão salva. Tente novamente.");
                return;
            }
            client->gate_passed = true;
        }
        int admitted = net_rate_try_acquire();
        if (admitted == 0)
            return;
        if (admitted < 0 || curl_multi_add_handle(client->multi, client->easy) != CURLM_OK) {
            if (client->operation == AUTH_MESSAGE_SEND || client->operation == AUTH_API_MUTATION)
                auth_internal_complete(client, CURLE_FAILED_INIT, 0);
            else
                auth_internal_fail(client, "Não foi possível iniciar a conexão.");
            return;
        }
        client->queued = false;
        client->attached = true;
    }
    if (!client->attached)
        return;
    int running;
    if (curl_multi_perform(client->multi, &running) != CURLM_OK) {
        if (client->operation == AUTH_MESSAGE_SEND || client->operation == AUTH_API_MUTATION)
            auth_internal_complete(client, CURLE_RECV_ERROR, 0);
        else
            auth_internal_fail(client, "A conexão foi interrompida. Tente novamente.");
        return;
    }
    int remaining;
    CURLMsg *message;
    while ((message = curl_multi_info_read(client->multi, &remaining))) {
        if (message->msg == CURLMSG_DONE) {
            CURLcode result = message->data.result;
            long status = 0;
            curl_easy_getinfo(client->easy, CURLINFO_RESPONSE_CODE, &status);
            auth_internal_complete(client, result, status);
            break; /* The next request may have been scheduled on the same handle. */
        }
    }
}

bool auth_connect_ws(auth_client *client)
{
    if (!auth_has_session(client) || client->state == AUTH_REFRESH || client->after_refresh)
        return false;
    client->ws_requested = true;
    ws_state state = ws_get_state(client->websocket);
    if (state == WS_OPEN || state == WS_QUEUED || state == WS_CONNECTING)
        return true;
    if (client->ws_managed &&
        (client->ws_epoch || client->ws_validate || state == WS_ERROR || state == WS_CLOSED)) {
        client->ws_retry_at = 0;
        client->ws_validate = true;
        client->ws_validated = false;
        return true;
    }
    return ws_connect(client->websocket, client->base);
}

void auth_realtime_active(auth_client *client, bool active)
{
    if (!client)
        return;
    if (!client->ws_managed && client->validation_deadline >= 15000)
        client->recheck_at = client->validation_deadline - 15000;
    client->ws_managed = true;
    client->ws_active = active;
    ws_state state = ws_get_state(client->websocket);
    if (!active && (state == WS_QUEUED || state == WS_CONNECTING)) {
        ws_abort(client->websocket);
        client->ws_validate = true;
    }
}

bool auth_ws_reconnecting(const auth_client *client)
{
    return client->ws_managed && client->ws_requested &&
           (client->ws_validate || client->ws_validated || client->validation_wait ||
            ws_get_state(client->websocket) != WS_OPEN);
}

ws_state auth_ws_state(const auth_client *client)
{
    return ws_get_state(client->websocket);
}

const char *auth_ws_message(const auth_client *client)
{
    return ws_message(client->websocket);
}

long auth_ws_http_status(const auth_client *client)
{
    return ws_http_status(client->websocket);
}

json_t *auth_next_event(auth_client *client)
{
    return auth_has_session(client) ? ws_pop(client->websocket) : NULL;
}

bool auth_send_event(auth_client *client, const json_t *event)
{
    return auth_has_session(client) && ws_send(client->websocket, event);
}
