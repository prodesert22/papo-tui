#include "net/auth/private.h"
#include <glib.h>

static void set_notice(auth_client *client, auth_password_outcome outcome, const char *text,
                       const char *detail)
{
    client->password_outcome = outcome;
    if (detail && *detail)
        snprintf(client->password_notice, sizeof(client->password_notice), "%s: %s", text, detail);
    else
        snprintf(client->password_notice, sizeof(client->password_notice), "%s", text);
    /* Never end inside a UTF-8 sequence after truncation. */
    size_t length = strlen(client->password_notice);
    while (length && !g_utf8_validate(client->password_notice, (gssize)length, NULL))
        client->password_notice[--length] = '\0';
}

static void drop_response(auth_client *client)
{
    free(client->response);
    client->response = NULL;
    client->response_length = 0;
}

static bool user_uuid(const char *id)
{
    if (strlen(id) != 36)
        return false;
    for (size_t i = 0; i < 36; ++i)
        if (i == 8 || i == 13 || i == 18 || i == 23 ? id[i] != '-' : !g_ascii_isxdigit(id[i]))
            return false;
    return true;
}

auth_password_outcome auth_password_state(const auth_client *client)
{
    return client->password_outcome;
}

const char *auth_password_notice(const auth_client *client)
{
    return client->password_notice;
}

bool auth_password_reset(auth_client *client, const char *url, const char *token,
                         const char *password, bool allow_local_http)
{
    if (auth_busy(client) || auth_has_session(client))
        return false;
    auth_internal_detach(client);
    ws_abort(client->websocket);
    client->ws_requested = false;
    client->server_password_error = false;
    client->input_error = AUTH_INPUT_NONE;
    client->registering = client->restoring = client->session_rejected = false;
    client->logout_requested = false;
    auth_internal_forget_inputs(client);
    auth_internal_clear_cookies(client);
    auth_internal_clear_api(client);
    auth_internal_clear_identity(client);
    curl_free(client->base);
    client->base = NULL;
    client->name[0] = client->user[0] = client->user_id[0] = '\0';
    char parsed[257], host[256], reason[192];
    if (!token || !auth_reset_token(token, parsed, sizeof(parsed), host, sizeof(host)))
        return auth_internal_input_fail(client, AUTH_INPUT_RESET_LINK,
                                        "Cole o link de redefinição recebido da administração.");
    if (!password || strlen(password) > 4096) {
        memset(parsed, 0, sizeof(parsed));
        return auth_internal_input_fail(client, AUTH_INPUT_PASSWORD,
                                        "A senha excedeu o limite do cliente.");
    }
    if (!auth_validate_password(password, reason, sizeof(reason))) {
        memset(parsed, 0, sizeof(parsed));
        return auth_internal_input_fail(client, AUTH_INPUT_PASSWORD, reason);
    }
    client->base = auth_normalize_url(url, reason, sizeof(reason));
    if (!client->base) {
        memset(parsed, 0, sizeof(parsed));
        return auth_internal_input_fail(client, AUTH_INPUT_URL, reason);
    }
    if (!allow_local_http && strncmp(client->base, "http://", 7) == 0) {
        memset(parsed, 0, sizeof(parsed));
        return auth_internal_input_fail(
            client, AUTH_INPUT_URL,
            "HTTP local exige --allow-local-http. Use HTTPS para acesso remoto.");
    }
    json_t *body = json_pack("{s:s,s:s}", "token", parsed, "password", password);
    memset(parsed, 0, sizeof(parsed));
    if (!body) {
        auth_internal_fail(client, "Não foi possível preparar a redefinição.");
        return false;
    }
    bool started =
        auth_internal_request(client, AUTH_PASSWORD_RESET, "/auth/password_reset", body, true);
    json_decref(body);
    return started;
}

bool auth_change_password(auth_client *client, const char *password)
{
    if (!auth_has_session(client) || client->state != AUTH_READY || client->queued ||
        client->attached || client->api_path || client->response_ready ||
        client->logout_requested || !user_uuid(client->user_id))
        return false;
    char reason[192];
    if (!password || strlen(password) > 4096) {
        set_notice(client, AUTH_PASSWORD_REFUSED, "A senha excedeu o limite do cliente.", NULL);
        return false;
    }
    if (!auth_validate_password(password, reason, sizeof(reason))) {
        set_notice(client, AUTH_PASSWORD_REFUSED, reason, NULL);
        return false;
    }
    auth_internal_erase_free(&client->new_password);
    client->new_password = strdup(password);
    if (!client->new_password) {
        set_notice(client, AUTH_PASSWORD_REFUSED, "Não foi possível preparar a troca de senha.",
                   NULL);
        return false;
    }
    char path[96];
    snprintf(path, sizeof(path), "/users/%s/reset", client->user_id);
    set_notice(client, AUTH_PASSWORD_PENDING, "Alterando a senha…", NULL);
    /* The server only accepts the new password after this bodyless request. */
    return auth_internal_request(client, AUTH_PASSWORD_PREPARE, path, NULL, true);
}

/* The server revoked every session of the account, this one included. */
static void password_updated(auth_client *client)
{
    ws_abort(client->websocket);
    auth_internal_detach(client);
    auth_internal_forget_inputs(client);
    auth_internal_clear_cookies(client);
    auth_internal_clear_api(client);
    auth_internal_clear_identity(client);
    drop_response(client);
    /* The saved session is revoked too: it must not be restored. */
    client->session_rejected = true;
    client->logout_requested = client->ws_requested = false;
    client->server_password_error = false;
    client->input_error = AUTH_INPUT_NONE;
    client->name[0] = '\0';
    client->state = AUTH_PASSWORD_UPDATED;
    snprintf(client->message, sizeof(client->message), "%s",
             "Senha alterada. As sessões da conta foram encerradas; entre com a nova senha.");
}

void auth_internal_password_complete(auth_client *client, auth_state previous, CURLcode result,
                                     long status, bool transmitted)
{
    client->state = AUTH_READY;
    if (previous == AUTH_PASSWORD_PREPARE && result == CURLE_OK && status == 200) {
        char path[96];
        snprintf(path, sizeof(path), "/users/%s/password", client->user_id);
        json_t *body =
            client->new_password ? json_pack("{s:s}", "password", client->new_password) : NULL;
        auth_internal_erase_free(&client->new_password);
        if (!body) {
            drop_response(client);
            set_notice(client, AUTH_PASSWORD_REFUSED, "Não foi possível preparar a troca de senha.",
                       NULL);
            return;
        }
        auth_internal_request(client, AUTH_PASSWORD_CHANGE, path, body, true);
        json_decref(body);
        return;
    }
    auth_internal_erase_free(&client->new_password);
    if (previous == AUTH_PASSWORD_CHANGE && result == CURLE_OK && status == 200) {
        password_updated(client);
        return;
    }
    if (result == CURLE_OK && status == 403 && auth_internal_problem_is(client, "banned")) {
        auth_internal_fail(
            client, "O servidor bloqueou esta conta. Entre novamente após resolver o acesso.");
        return;
    }
    if (result == CURLE_OK && status == 429)
        net_rate_defer(client->retry_after);
    bool change = previous == AUTH_PASSWORD_CHANGE;
    if (change && ((result != CURLE_OK && transmitted) ||
                   (result == CURLE_OK && (status >= 500 || (status >= 200 && status < 300))))) {
        drop_response(client);
        set_notice(client, AUTH_PASSWORD_UNCONFIRMED,
                   "Troca de senha não confirmada. Se a nova senha funcionar ao entrar, ela já "
                   "vale",
                   NULL);
        /* If it took effect, the next session check finds this session revoked. */
        client->recheck_at = 0;
        return;
    }
    char detail[160];
    auth_internal_problem_detail(client, detail, sizeof(detail));
    drop_response(client);
    if (result != CURLE_OK)
        set_notice(client, AUTH_PASSWORD_REFUSED,
                   "Não foi possível conectar para trocar a senha. Tente de novo", NULL);
    else if (status == 401)
        set_notice(client, AUTH_PASSWORD_REFUSED,
                   "A sessão precisou ser confirmada de novo. Tente trocar a senha outra vez",
                   NULL);
    else if (status == 400)
        set_notice(client, AUTH_PASSWORD_REFUSED, "O servidor recusou a nova senha", detail);
    else if (status == 409)
        set_notice(client, AUTH_PASSWORD_REFUSED,
                   "O servidor não preparou a troca de senha. Tente de novo", NULL);
    else if (status == 429)
        set_notice(client, AUTH_PASSWORD_REFUSED, "Muitas tentativas; aguarde e tente de novo",
                   NULL);
    else if (previous == AUTH_PASSWORD_PREPARE && status >= 500)
        set_notice(client, AUTH_PASSWORD_REFUSED,
                   "O servidor não preparou a troca de senha. Tente de novo", NULL);
    else {
        char text[96];
        snprintf(text, sizeof(text), "O servidor recusou a troca de senha (HTTP %ld)", status);
        set_notice(client, AUTH_PASSWORD_REFUSED, text, detail);
    }
    if (result == CURLE_OK && status == 401)
        auth_internal_recover_session(client); /* Never resubmits the change. */
}

void auth_internal_reset_complete(auth_client *client, CURLcode result, long status,
                                  bool transmitted)
{
    auth_internal_forget_inputs(client);
    if (result == CURLE_OK && status == 200) {
        drop_response(client);
        auth_internal_clear_cookies(client);
        client->input_error = AUTH_INPUT_NONE;
        client->server_password_error = false;
        client->state = AUTH_PASSWORD_UPDATED;
        snprintf(client->message, sizeof(client->message), "%s",
                 "Senha redefinida. Entre com a nova senha.");
        return;
    }
    char detail[128], text[192];
    auth_internal_problem_detail(client, detail, sizeof(detail));
    auth_input_error field = AUTH_INPUT_NONE;
    if (result != CURLE_OK)
        snprintf(text, sizeof(text), "%s",
                 transmitted ? "Redefinição não confirmada. Tente entrar com a nova senha antes "
                               "de pedir outro link."
                             : auth_internal_connection_message(client, result));
    else if (status == 410) {
        snprintf(text, sizeof(text), "%s",
                 "Link inválido, expirado ou já usado. Peça um novo link à administração.");
        field = AUTH_INPUT_RESET_LINK;
    } else if (status == 400) {
        if (*detail)
            snprintf(text, sizeof(text), "O servidor recusou a nova senha: %s", detail);
        else
            snprintf(text, sizeof(text), "%s", "O servidor recusou a nova senha.");
        field = AUTH_INPUT_PASSWORD;
    } else if (status == 429) {
        net_rate_defer(client->retry_after);
        snprintf(text, sizeof(text), "%s",
                 "Limite de requisições atingido. Aguarde antes de tentar novamente.");
    } else if (status == 404) {
        snprintf(text, sizeof(text), "%s",
                 "Instância sem redefinição de senha ou URL incompatível com o Papo.");
        field = AUTH_INPUT_URL;
    } else if (status >= 500)
        snprintf(text, sizeof(text), "%s",
                 "O servidor não confirmou a redefinição. Tente entrar com a nova senha antes de "
                 "repetir.");
    else if (status >= 300 && status < 400)
        snprintf(text, sizeof(text), "%s",
                 "O servidor redirecionou a conexão. Informe a URL final.");
    else
        snprintf(text, sizeof(text), "O servidor recusou a redefinição (HTTP %ld).", status);
    size_t length = strlen(text);
    while (length && !g_utf8_validate(text, (gssize)length, NULL))
        text[--length] = '\0';
    auth_internal_fail(client, text);
    client->input_error = field;
}
