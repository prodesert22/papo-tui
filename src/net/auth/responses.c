#include "net/auth/private.h"

static void complete_send(auth_client *client, CURLcode result, long status, bool transmitted)
{
    client->send_receipt.status = status;
    client->send_receipt.transmitted = transmitted;
    client->send_receipt.outcome = AUTH_SEND_REJECTED;
    json_error_t error;
    json_t *body = result == CURLE_OK && status == 201
                       ? json_loadb(client->response ? client->response : "",
                                    client->response_length, JSON_REJECT_DUPLICATES, &error)
                       : NULL;
    const char *id = auth_internal_json_text(body, "id", 64);
    const char *channel = auth_internal_json_text(body, "channel_id", 64);
    const char *author = auth_internal_json_text(body, "author_id", 64);
    if (result == CURLE_OK && status == 201 && id && channel && author &&
        strcmp(channel, client->send_channel) == 0 && strcmp(author, client->user_id) == 0) {
        client->send_receipt.outcome = AUTH_SEND_CONFIRMED;
        client->sent_message = body;
        body = NULL;
    } else if ((result != CURLE_OK && transmitted) || status >= 500 ||
               (status >= 200 && status < 400))
        client->send_receipt.outcome = AUTH_SEND_UNCERTAIN;
    json_decref(body);
    client->send_ready = true;
    client->state = AUTH_READY;
    if (status == 403)
        auth_permissions_refresh(client);
    if (status == 429)
        net_rate_defer(client->retry_after);
    if (result == CURLE_OK && status == 401)
        auth_internal_recover_session(client); /* Never resubmit the POST. */
    else if (result == CURLE_OK && status == 403 && auth_internal_problem_is(client, "banned"))
        auth_internal_fail(
            client, "O servidor bloqueou esta conta. Entre novamente após resolver o acesso.");
    if (!client->attached && !client->queued) {
        free(client->response);
        client->response = NULL;
        client->response_length = 0;
    }
}

static void complete_api(auth_client *client, CURLcode result, long status, bool transmitted)
{
    bool mutation = client->operation == AUTH_API_MUTATION;
    if (!mutation && result == CURLE_OK && status == 401) {
        if (!client->api_retried) {
            client->api_retried = true;
            auth_internal_recover_session(client);
        } else
            auth_internal_fail(
                client, "Não foi possível confirmar a sessão após renovar. Entre novamente.");
        return;
    }
    if (result == CURLE_OK && status == 403 && auth_internal_problem_is(client, "banned")) {
        auth_internal_fail(
            client, "O servidor bloqueou esta conta. Entre novamente após resolver o acesso.");
        return;
    }
    if (result == CURLE_OK && status == 403)
        auth_permissions_refresh(client);
    if (result == CURLE_OK && status == 429)
        net_rate_defer(client->retry_after);
    json_error_t error;
    json_t *body = result == CURLE_OK ? json_loadb(client->response ? client->response : "",
                                                   client->response_length,
                                                   JSON_REJECT_DUPLICATES | JSON_DECODE_ANY, &error)
                                      : NULL;
    if (status == 200 && (json_is_true(json_object_get(body, "connection_violation")) ||
                          (strcmp(client->api_path, "/auth/whoami") == 0 &&
                           (!auth_internal_json_text(body, "id", sizeof(client->user_id)) ||
                            strcmp(auth_internal_json_text(body, "id", sizeof(client->user_id)),
                                   client->user_id) != 0)))) {
        json_decref(body);
        auth_internal_fail(client,
                           "A identidade da sessão não pôde ser confirmada. Entre novamente.");
        return;
    }
    client->api_uncertain =
        mutation && ((result != CURLE_OK && transmitted) || status >= 500 ||
                     (status >= 200 && status < 300 && status != 204 && !json_is_object(body)));
    if (result != CURLE_OK || (!body && status == 200))
        status = 0;
    if (!mutation && result == CURLE_OK && status == 200) {
        if (!strcmp(client->api_path, "/channels"))
            permissions_update(client->permissions, PERMISSIONS_CHANNELS, body);
        else if (!strcmp(client->api_path, "/roles"))
            permissions_update(client->permissions, PERMISSIONS_ROLES, body);
        else if (!strcmp(client->api_path, "/server"))
            permissions_update(client->permissions, PERMISSIONS_SERVER, body);
        else if (!strcmp(client->api_path, "/auth/whoami"))
            permissions_identity(client->permissions, body, client->permission_serial);
    }
    json_decref(client->permission_message);
    client->permission_message = NULL;
    free(client->api_path);
    client->api_path = NULL;
    json_decref(client->api_body);
    client->api_body = NULL;
    client->api_retried = false;
    free(client->response);
    client->response = NULL;
    client->response_length = 0;
    client->api_method = NULL;
    client->api_response = body;
    client->api_status = status;
    client->response_generation = client->generation;
    client->response_ready = true;
    client->state = AUTH_READY;
    snprintf(client->message, sizeof(client->message), "%s",
             status == 200   ? "Consulta concluída."
             : status == 429 ? "Limite de requisições atingido. Aguarde antes de tentar novamente."
                             : "Não foi possível concluir a consulta autenticada.");
    if (mutation && result == CURLE_OK && status == 401)
        auth_internal_recover_session(client);
}

static void complete_server(auth_client *client, json_t *body, auth_state previous)
{
    json_t *public_value = json_object_get(body, "public");
    json_t *name_value = json_object_get(body, "name");
    const char *name = json_string_value(name_value);
    if (!json_is_boolean(public_value) ||
        (name_value &&
         (!json_is_string(name_value) || json_string_length(name_value) != strlen(name)))) {
        auth_internal_fail(
            client,
            "Backend incompatível: não foi possível identificar a configuração da instância.");
    } else {
        permissions_update(client->permissions, PERMISSIONS_SERVER, body);
        auth_internal_copy_server_name(client, name);
        client->server_public = json_is_true(public_value);
        if (previous == AUTH_DISCOVERY_SERVER) {
            auth_internal_clear_cookies(
                client); /* Discovery cookies never become authentication. */
            client->state = AUTH_IDENTIFIED;
            snprintf(client->message, sizeof(client->message), "%s",
                     client->server_public
                         ? "Instância pública identificada."
                         : "Instância privada identificada; login exige senha do servidor.");
        } else if (client->restoring)
            auth_internal_request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false);
        else if (json_is_true(public_value))
            auth_internal_user_login(client);
        else if (!*client->server_password) {
            auth_internal_fail(client, "Esta instância é privada. Preencha a senha do servidor.");
            client->server_password_error = true;
            client->input_error = AUTH_INPUT_SERVER_PASSWORD;
        } else {
            json_t *payload = json_pack("{s:s}", "server_password", client->server_password);
            if (!payload)
                auth_internal_fail(client, "Não foi possível preparar a autorização.");
            else {
                auth_internal_request(client, AUTH_SERVER_PASSWORD, "/auth/login_server", payload,
                                      true);
                json_decref(payload);
                auth_internal_erase_free(&client->server_password);
            }
        }
    }
}

static void complete_login(auth_client *client, json_t *body)
{
    if (json_is_true(json_object_get(body, "connection_violation")))
        auth_internal_fail(client, "A sessão foi revogada pelo servidor. Entre novamente.");
    else {
        const char *expected_id =
            auth_internal_json_text(json_object_get(body, "user"), "id", sizeof(client->user_id));
        if (expected_id)
            snprintf(client->user_id, sizeof(client->user_id), "%s", expected_id);
        auth_internal_request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false);
    }
}

static void complete_register(auth_client *client, json_t *body)
{
    if (!auth_internal_json_text(body, "id", sizeof(client->user_id)) ||
        !auth_internal_json_text(body, "username", sizeof(client->user)))
        auth_internal_fail(
            client,
            "Cadastro não confirmado pelo servidor. Tente entrar antes de enviar novamente.");
    else {
        auth_internal_clear_cookies(client); /* Registration never authenticates a user. */
        auth_internal_clear_identity(client);
        client->state = AUTH_REGISTERED;
        snprintf(client->message, sizeof(client->message),
                 "Conta criada. Faça login com sua nova conta.");
    }
}

static void complete_refresh(auth_client *client, json_t *body)
{
    json_t *connection = json_object_get(body, "connection");
    const char *id = auth_internal_json_text(connection, "id", sizeof(client->user_id));
    const char *expires = auth_internal_json_text(connection, "expires_at", 65);
    if (!id || !expires || !session_time_schedule(expires, &client->refresh_at) ||
        !session_time_expiration(expires, &client->expires_at))
        auth_internal_fail(client,
                           "Não foi possível confirmar a renovação da sessão. Entre novamente.");
    else
        auth_internal_request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false);
}

static void complete_whoami(auth_client *client, json_t *body)
{
    const char *username = auth_internal_json_text(body, "username", sizeof(client->user));
    const char *id = auth_internal_json_text(body, "id", sizeof(client->user_id));
    if (!username || !id || (*client->user_id && strcmp(id, client->user_id) != 0) ||
        json_is_true(json_object_get(body, "connection_violation"))) {
        client->session_rejected = true;
        auth_internal_fail(client, "Não foi possível confirmar a sessão. Entre novamente.");
    } else {
        permissions_identity(client->permissions, body, client->permission_serial);
        if (!client->authenticated)
            auth_permissions_refresh(client);
        snprintf(client->user, sizeof(client->user), "%s", username);
        snprintf(client->user_id, sizeof(client->user_id), "%s", id);
        uint64_t now;
        if (!net_now_ms(&now))
            auth_internal_fail(client, "Não foi possível acompanhar a sessão.");
        else {
            /* Start early enough for the existing 15s REST timeout. A
             * rate-limited or busy queue must not keep WS trusted forever. */
            client->recheck_at =
                client->permission_serial != permissions_event_serial(client->permissions)
                    ? now
                    : now + (client->ws_managed ? 15000 : 30000);
            client->validation_deadline = now + 30000;
            if (!client->refresh_at)
                client->refresh_at = now + 30000;
            client->authenticated = true;
            client->validation_wait = false;
            if (client->ws_validate) {
                client->ws_validated = true;
                client->ws_validate = false;
            }
            client->restoring = client->session_rejected = false;
            client->recovering = client->after_refresh = false;
            client->state = AUTH_READY;
            snprintf(client->message, sizeof(client->message),
                     "Login realizado. Você está conectado.");
            if (client->api_path)
                auth_internal_request(client, AUTH_API_GET, client->api_path, client->api_body,
                                      client->api_body != NULL);
            if (client->ws_requested && ws_get_state(client->websocket) == WS_IDLE &&
                !client->ws_managed)
                auth_connect_ws(client);
        }
    }
}

void auth_internal_complete(auth_client *client, CURLcode result, long status)
{
    auth_state previous = client->operation;
    bool transmitted = (previous == AUTH_MESSAGE_SEND || previous == AUTH_API_MUTATION) &&
                       auth_internal_send_transmitted(client);
    auth_internal_detach(client);
    if (previous == AUTH_PERMISSION_SERVER || previous == AUTH_PERMISSION_ROLES ||
        previous == AUTH_PERMISSION_CHANNELS) {
        auth_internal_permissions_response(client, result, status);
        return;
    }
    if (previous == AUTH_MESSAGE_SEND) {
        complete_send(client, result, status, transmitted);
        return;
    }
    if (previous == AUTH_API_GET || previous == AUTH_API_MUTATION) {
        complete_api(client, result, status, transmitted);
        return;
    }
    /* A failed read proves unavailability, not revocation. Preserve an already
     * verified context; refresh is deliberately excluded because its POST may
     * have rotated a cookie even when its response was lost. */
    if (client->ws_managed && *client->user_id &&
        (client->authenticated || client->recovering || client->after_refresh) &&
        previous == AUTH_WHOAMI && !client->logout_requested &&
        (result == CURLE_COULDNT_CONNECT || result == CURLE_COULDNT_RESOLVE_HOST ||
         result == CURLE_OPERATION_TIMEDOUT || result == CURLE_RECV_ERROR ||
         result == CURLE_GOT_NOTHING || result == CURLE_SEND_ERROR ||
         result == CURLE_PARTIAL_FILE || result == CURLE_COULDNT_RESOLVE_PROXY ||
         (result == CURLE_OK && (status == 429 || status >= 500)))) {
        uint64_t now;
        if (!net_now_ms(&now)) {
            auth_internal_fail(client, "Não foi possível acompanhar a sessão.");
            return;
        }
        if (status == 429)
            net_rate_defer(client->retry_after);
        ws_abort(client->websocket);
        auth_internal_reconnect_later(client, now);
        client->validation_wait = true;
        client->state = AUTH_READY;
        snprintf(client->message, sizeof(client->message), "Rede indisponível. Reconectando…");
        return;
    }
    if (result != CURLE_OK) {
        const char *message = "Não foi possível conectar. Confira a URL e tente novamente.";
        if (client->overflow)
            message = "O servidor enviou uma resposta grande demais.";
        else if (result == CURLE_COULDNT_RESOLVE_HOST)
            message = "Nome do servidor não encontrado (DNS). Confira o host.";
        else if (result == CURLE_COULDNT_RESOLVE_PROXY)
            message = "Não foi possível resolver o proxy (DNS). Confira sua configuração.";
        else if (result == CURLE_COULDNT_CONNECT) {
            long system_error = 0;
            curl_easy_getinfo(client->easy, CURLINFO_OS_ERRNO, &system_error);
            message = system_error == ECONNREFUSED
                          ? "Conexão recusada. Confira o host e a porta."
                          : "Serviço indisponível ou rede inacessível. Confira host e porta.";
        } else if (result == CURLE_OPERATION_TIMEDOUT)
            message = "O servidor demorou para responder. Tente novamente.";
        else if (result == CURLE_PEER_FAILED_VERIFICATION || result == CURLE_SSL_CONNECT_ERROR ||
                 result == CURLE_SSL_CACERT_BADFILE || result == CURLE_SSL_CERTPROBLEM)
            message = "Falha ao verificar a conexão segura com o servidor.";
        if (previous == AUTH_REGISTER)
            message = "Cadastro não confirmado. Tente entrar antes de enviar novamente.";
        if (status == 401 || status == 403)
            client->session_rejected = true;
        auth_internal_fail(client, message);
        return;
    }
    long expected = previous == AUTH_REGISTER                                       ? 201
                    : (previous == AUTH_SERVER_PASSWORD || previous == AUTH_LOGOUT) ? 204
                                                                                    : 200;
    if (status != expected) {
        if (status == 401 && previous == AUTH_WHOAMI && !client->after_refresh) {
            auth_internal_recover_session(client);
            return;
        }
        if (status == 401 && !client->server_public &&
            (previous == AUTH_REGISTER ||
             (previous == AUTH_LOGIN && auth_internal_problem_is(client, "unauthorized")))) {
            auth_internal_input_fail(
                client, AUTH_INPUT_SERVER_PASSWORD,
                "Autorização privada expirou. Informe a senha do servidor e confirme novamente.");
            return;
        }
        const char *message = "O servidor não aceitou a solicitação. Tente novamente.";
        if (status == 401) {
            message = previous == AUTH_SERVER_PASSWORD
                          ? "Senha do servidor incorreta."
                          : "Usuário ou senha inválidos, ou sessão não autorizada.";
            if (previous == AUTH_REFRESH || previous == AUTH_WHOAMI)
                message = "Sua sessão expirou ou foi revogada. Entre novamente.";
        } else if (status == 429) {
            net_rate_defer(client->retry_after);
            message = "Limite de requisições atingido. Aguarde antes de tentar novamente.";
        } else if (status == 403)
            message = "O servidor recusou o acesso a esta conta.";
        else if (status == 409 && previous == AUTH_REGISTER)
            message = "Nome de usuário já está em uso. Escolha outro.";
        else if (status == 400 && previous == AUTH_REGISTER)
            message = "Cadastro recusado. Confira usuário e a política de senha desta instância.";
        else if (status == 404 && (previous == AUTH_SERVER || previous == AUTH_DISCOVERY_SERVER))
            message = "Instância não inicializada ou URL incompatível com o Papo.";
        else if (status == 404 && previous == AUTH_HEALTH)
            message = "Backend incompatível: /health não está disponível nessa URL.";
        else if (status == 502 || status == 503 || status == 504)
            message = "Serviço indisponível. Tente novamente ou corrija a URL.";
        else if (status >= 500 && (previous == AUTH_HEALTH || previous == AUTH_DISCOVERY_SERVER))
            message = "Serviço indisponível. Tente novamente ou corrija a URL.";
        else if (status >= 300 && status < 400)
            message = "O servidor redirecionou a conexão. Informe a URL final.";
        if (status == 401 || status == 403)
            client->session_rejected = true;
        auth_internal_fail(client, message);
        if (status == 401 && previous == AUTH_SERVER_PASSWORD) {
            client->server_password_error = true;
            client->input_error = AUTH_INPUT_SERVER_PASSWORD;
        } else if (status == 409 && previous == AUTH_REGISTER)
            client->input_error = AUTH_INPUT_USERNAME;
        else if (status == 401 && previous == AUTH_LOGIN)
            client->input_error = AUTH_INPUT_PASSWORD;
        return;
    }
    if (previous == AUTH_REFRESH) {
        char *current = auth_internal_cookie_snapshot(client);
        bool changed =
            current && client->refresh_cookie && strcmp(current, client->refresh_cookie) != 0;
        auth_internal_erase_free(&current);
        auth_internal_erase_free(&client->refresh_cookie);
        if (!changed) {
            auth_internal_fail(client,
                               "O servidor não confirmou a rotação do cookie. Entre novamente.");
            return;
        }
    }
    if (client->logout_requested && previous == AUTH_REFRESH) {
        auth_internal_request(client, AUTH_LOGOUT, "/auth/logout", NULL, true);
        return;
    }
    if (previous == AUTH_HEALTH) {
        auth_internal_request(client, AUTH_DISCOVERY_SERVER, "/server", NULL, false);
        return;
    }
    if (previous == AUTH_LOGOUT) {
        auth_internal_logout_done(client, true);
        return;
    }
    if (previous == AUTH_SERVER_PASSWORD) {
        auth_internal_user_login(client);
        return;
    }
    json_error_t error;
    json_t *body = json_loadb(client->response ? client->response : "", client->response_length,
                              JSON_REJECT_DUPLICATES, &error);
    if (!json_is_object(body)) {
        json_decref(body);
        auth_internal_fail(client, "O servidor retornou uma resposta incompatível com o Papo.");
        return;
    }
    switch (previous) {
    case AUTH_DISCOVERY_SERVER:
    case AUTH_SERVER:
        complete_server(client, body, previous);
        break;
    case AUTH_LOGIN:
        complete_login(client, body);
        break;
    case AUTH_REGISTER:
        complete_register(client, body);
        break;
    case AUTH_REFRESH:
        complete_refresh(client, body);
        break;
    case AUTH_WHOAMI:
        complete_whoami(client, body);
        break;
    default:
        break;
    }

    json_decref(body);
}
