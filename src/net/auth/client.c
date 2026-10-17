#include "net/auth/private.h"
#include "net/tls.h"
#include "platform/runtime.h"

void auth_internal_erase_free(char **value)
{
    if (*value) {
        volatile unsigned char *p = (volatile unsigned char *)*value;
        size_t length = strlen(*value);
        while (length--)
            *p++ = 0;
        free(*value);
        *value = NULL;
    }
}

void auth_internal_forget_inputs(auth_client *client)
{
    auth_internal_erase_free(&client->username);
    auth_internal_erase_free(&client->password);
    auth_internal_erase_free(&client->server_password);
}

void auth_internal_clear_cookies(auth_client *client)
{
    curl_easy_setopt(client->easy, CURLOPT_COOKIELIST, "ALL");
}

void auth_internal_clear_api(auth_client *client)
{
    free(client->api_path);
    client->api_path = NULL;
    json_decref(client->api_body);
    client->api_body = NULL;
    json_decref(client->api_response);
    client->api_response = NULL;
    client->response_ready = client->api_retried = false;
    client->api_method = NULL;
    json_decref(client->permission_message);
    client->permission_message = NULL;
    client->api_uncertain = false;
}

void auth_internal_clear_send(auth_client *client)
{
    json_decref(client->sent_message);
    client->sent_message = NULL;
    client->send_ready = false;
    client->send_has_attachments = false;
    client->send_channel[0] = '\0';
}

void auth_internal_clear_identity(auth_client *client)
{
    auth_internal_clear_send(client);
    permissions_reset(client->permissions);
    client->permission_wanted = 0;
    client->permission_due = 0;
    client->permission_failed = client->permission_retried = 0;
    client->authenticated = client->recovering = client->after_refresh = false;
    client->refresh_at = client->recheck_at = 0;
    client->validation_deadline = 0;
    client->expires_at = client->ws_retry_at = client->ws_stable_at = 0;
    client->ws_failures = 0;
    client->ws_validate = client->ws_validated = client->validation_wait = false;
    client->ws_epoch = 0;
    client->ws_requested = false;
    client->user[0] = client->user_id[0] = '\0';
    auth_internal_erase_free(&client->refresh_cookie);
    ++client->generation;
}

void auth_internal_logout_done(auth_client *client, bool revoked)
{
    ws_abort(client->websocket);
    auth_internal_detach(client);
    auth_internal_forget_inputs(client);
    auth_internal_clear_cookies(client);
    auth_internal_clear_api(client);
    auth_internal_clear_identity(client);
    client->logout_requested = client->ws_requested = false;
    client->server_password_error = false;
    client->input_error = AUTH_INPUT_NONE;
    client->name[0] = '\0';
    client->state = AUTH_IDLE;
    snprintf(client->message, sizeof(client->message), "%s",
             revoked ? "Você saiu da conta."
                     : "Sessão local encerrada; revogação remota não confirmada.");
}

void auth_internal_fail(auth_client *client, const char *message)
{
    if (client->logout_requested) {
        auth_internal_logout_done(client, false);
        return;
    }
    client->server_password_error = false;
    client->input_error = AUTH_INPUT_NONE;
    ws_abort(client->websocket);
    auth_internal_detach(client);
    auth_internal_forget_inputs(client);
    auth_internal_clear_cookies(client);
    auth_internal_clear_api(client);
    auth_internal_clear_identity(client);
    client->state = AUTH_ERROR;
    client->name[0] = client->user[0] = '\0';
    client->user_id[0] = '\0';
    snprintf(client->message, sizeof(client->message), "%s", message);
}

bool auth_internal_input_fail(auth_client *client, auth_input_error field, const char *message)
{
    auth_internal_fail(client, message);
    client->input_error = field;
    client->server_password_error = field == AUTH_INPUT_SERVER_PASSWORD;
    return false;
}

bool auth_internal_user_login(auth_client *client)
{
    json_t *body =
        json_pack("{s:s,s:s}", "username", client->username, "password", client->password);
    if (!body) {
        auth_internal_fail(client, "Não foi possível preparar o login.");
        return false;
    }
    bool result =
        auth_internal_request(client, client->registering ? AUTH_REGISTER : AUTH_LOGIN,
                              client->registering ? "/auth/register" : "/auth/login", body, true);
    json_decref(body);
    auth_internal_forget_inputs(client);
    return result;
}

auth_client *auth_create(void)
{
    /* Jitter needs variation across processes; it is not secret material. */
    static bool seeded;
    if (!seeded) {
        unsigned seed = (unsigned)g_get_real_time();
        papo_random_bytes(&seed, sizeof(seed));
        srand(seed);
        seeded = true;
    }
    auth_client *client = calloc(1, sizeof(*client));
    if (!client)
        return NULL;
    client->permissions = permissions_create();
    client->easy = curl_easy_init();
    client->multi = curl_multi_init();
    client->cookies = curl_share_init();
    if (!client->permissions || !client->easy || !client->multi || !client->cookies ||
        curl_share_setopt(client->cookies, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) != CURLSHE_OK ||
        curl_share_setopt(client->cookies, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT) != CURLSHE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_SHARE, client->cookies) != CURLE_OK ||
        !(client->websocket = ws_create(client->cookies))) {
        if (client->easy)
            curl_easy_cleanup(client->easy);
        if (client->multi)
            curl_multi_cleanup(client->multi);
        if (client->cookies)
            curl_share_cleanup(client->cookies);
        permissions_destroy(client->permissions);
        free(client);
        return NULL;
    }
#define SET(option, value)                                                                         \
    do {                                                                                           \
        if (curl_easy_setopt(client->easy, option, value) != CURLE_OK) {                           \
            auth_destroy(client);                                                                  \
            return NULL;                                                                           \
        }                                                                                          \
    } while (0)
    SET(CURLOPT_COOKIEFILE, "");
    SET(CURLOPT_WRITEFUNCTION, auth_internal_receive);
    SET(CURLOPT_WRITEDATA, client);
    SET(CURLOPT_HEADERFUNCTION, auth_internal_receive_header);
    SET(CURLOPT_HEADERDATA, client);
    SET(CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    SET(CURLOPT_TIMEOUT_MS, 15000L);
    SET(CURLOPT_NOSIGNAL, 1L);
    SET(CURLOPT_FOLLOWLOCATION, 0L);
    SET(CURLOPT_SSL_VERIFYPEER, 1L);
    SET(CURLOPT_SSL_VERIFYHOST, 2L);
    SET(CURLOPT_PROTOCOLS_STR, "http,https");
    SET(CURLOPT_USERAGENT, "papo-tui/" PAPO_VERSION);
    const char *ca_bundle = net_ca_bundle();
    if (ca_bundle && *ca_bundle)
        SET(CURLOPT_CAINFO, ca_bundle);
#undef SET
    ws_observe(client->websocket, auth_internal_permissions_observe, client);
    snprintf(client->message, sizeof(client->message), "Preencha os dados para entrar.");
    return client;
}

static bool start_account(auth_client *client, const char *url, const char *username,
                          const char *password, const char *server_password, bool allow_local_http,
                          bool registering)
{
    if (auth_busy(client) || auth_has_session(client))
        return false;
    auth_internal_detach(client);
    ws_abort(client->websocket);
    client->ws_requested = false;
    client->server_password_error = false;
    client->input_error = AUTH_INPUT_NONE;
    client->registering = registering;
    client->restoring = client->session_rejected = false;
    client->logout_requested = false;
    auth_internal_forget_inputs(client);
    auth_internal_clear_cookies(client);
    auth_internal_clear_api(client);
    auth_internal_clear_identity(client);
    curl_free(client->base);
    client->base = NULL;
    client->name[0] = client->user[0] = '\0';
    client->user_id[0] = '\0';
    if (!username || !*username)
        return auth_internal_input_fail(client, AUTH_INPUT_USERNAME, "Preencha usuário e senha.");
    if (!password || !*password)
        return auth_internal_input_fail(client, AUTH_INPUT_PASSWORD, "Preencha usuário e senha.");
    if (strlen(username) > 1024 || strlen(password) > 4096 ||
        (server_password && strlen(server_password) > 4096)) {
        auth_internal_fail(client, "O usuário ou a senha excedeu o limite do cliente.");
        return false;
    }
    if (registering && !auth_internal_registration_valid(client, username, password))
        return false;
    char error[192];
    client->base = auth_normalize_url(url, error, sizeof(error));
    if (!client->base) {
        return auth_internal_input_fail(client, AUTH_INPUT_URL, error);
    }
    if (!allow_local_http && strncmp(client->base, "http://", 7) == 0) {
        return auth_internal_input_fail(
            client, AUTH_INPUT_URL,
            "HTTP local exige --allow-local-http. Use HTTPS para acesso remoto.");
    }
    client->username = strdup(username);
    client->password = strdup(password);
    client->server_password = strdup(server_password ? server_password : "");
    if (!client->username || !client->password || !client->server_password) {
        auth_internal_fail(client, "Não foi possível preparar o login.");
        return false;
    }
    return auth_internal_request(client, AUTH_SERVER, "/server", NULL, false);
}

bool auth_start_with_policy(auth_client *client, const char *url, const char *username,
                            const char *password, const char *server_password,
                            bool allow_local_http)
{
    return start_account(client, url, username, password, server_password, allow_local_http, false);
}

bool auth_register_with_policy(auth_client *client, const char *url, const char *username,
                               const char *password, const char *server_password,
                               bool allow_local_http)
{
    return start_account(client, url, username, password, server_password, allow_local_http, true);
}

bool auth_start(auth_client *client, const char *url, const char *username, const char *password,
                const char *server_password)
{
    /* Developer probes explicitly exercise local HTTP through this API.
     * Interactive entry points always use auth_start_with_policy(). */
    return auth_start_with_policy(client, url, username, password, server_password, true);
}

bool auth_discover(auth_client *client, const char *url, bool allow_local_http)
{
    auth_cancel(client);
    curl_free(client->base);
    char reason[192];
    client->base = auth_normalize_url(url, reason, sizeof(reason));
    if (!client->base) {
        auth_internal_fail(client, reason);
        return false;
    }
    if (!allow_local_http && strncmp(client->base, "http://", 7) == 0) {
        auth_internal_fail(client,
                           "HTTP local exige --allow-local-http. Use HTTPS para acesso remoto.");
        return false;
    }
    return auth_internal_request(client, AUTH_HEALTH, "/health", NULL, false);
}

bool auth_logout(auth_client *client)
{
    if ((!auth_has_session(client) && !*client->user_id) || client->logout_requested)
        return false;
    client->logout_requested = true;
    permissions_reset(client->permissions);
    client->authenticated = false;
    ++client->generation;
    auth_internal_clear_api(client);
    auth_internal_forget_inputs(client);
    ws_close(client->websocket);
    if (client->operation == AUTH_REFRESH && (client->queued || client->attached)) {
        /* Wait for a rotation already in progress so logout uses its cookie. */
        client->state = AUTH_LOGOUT;
        return true;
    }
    return auth_internal_request(client, AUTH_LOGOUT, "/auth/logout", NULL, true);
}

void auth_cancel(auth_client *client)
{
    if (client->logout_requested) {
        auth_internal_logout_done(client, false);
        return;
    }
    bool unknown_registration = client->operation == AUTH_REGISTER && client->attached;
    client->server_password_error = false;
    ws_abort(client->websocket);
    auth_internal_detach(client);
    auth_internal_forget_inputs(client);
    auth_internal_clear_cookies(client);
    auth_internal_clear_api(client);
    auth_internal_clear_identity(client);
    client->state = AUTH_IDLE;
    client->user[0] = client->name[0] = '\0';
    client->user_id[0] = '\0';
    client->ws_requested = false;
    client->input_error = AUTH_INPUT_NONE;
    snprintf(client->message, sizeof(client->message), "%s",
             unknown_registration ? "Envio cancelado. Tente entrar antes de repetir o cadastro."
                                  : "Conexão cancelada.");
}

void auth_destroy(auth_client *client)
{
    if (!client)
        return;
    auth_internal_detach(client);
    ws_destroy(client->websocket);
    auth_internal_forget_inputs(client);
    auth_internal_clear_api(client);
    auth_internal_clear_send(client);
    auth_internal_erase_free(&client->refresh_cookie);
    curl_easy_cleanup(client->easy);
    curl_multi_cleanup(client->multi);
    curl_share_cleanup(client->cookies);
    curl_free(client->base);
    permissions_destroy(client->permissions);
    free(client->response);
    free(client);
}

auth_state auth_get_state(const auth_client *client)
{
    return client->state;
}

bool auth_busy(const auth_client *client)
{
    return (client->validation_wait && client->recovering) || client->queued || client->attached ||
           client->state == AUTH_SERVER || client->state == AUTH_SERVER_PASSWORD ||
           client->state == AUTH_LOGIN || client->state == AUTH_WHOAMI ||
           client->state == AUTH_LOGOUT || client->state == AUTH_HEALTH ||
           client->state == AUTH_DISCOVERY_SERVER || client->state == AUTH_REGISTER ||
           client->state == AUTH_REFRESH || client->state == AUTH_API_GET ||
           client->state == AUTH_MESSAGE_SEND;
}

const char *auth_message(const auth_client *client)
{
    switch (client->state) {
    case AUTH_SERVER:
        return "Consultando a instância…";
    case AUTH_HEALTH:
        return "Consultando a disponibilidade da instância…";
    case AUTH_DISCOVERY_SERVER:
        return "Identificando a instância…";
    case AUTH_SERVER_PASSWORD:
        return "Autorizando acesso ao servidor…";
    case AUTH_LOGIN:
        return "Entrando na sua conta…";
    case AUTH_REGISTER:
        return "Criando sua conta…";
    case AUTH_REFRESH:
        return "Renovando sua sessão…";
    case AUTH_MESSAGE_SEND:
        return "Enviando sua mensagem…";
    case AUTH_API_GET:
        return "Consultando com a sessão autenticada…";
    case AUTH_WHOAMI:
        return "Confirmando sua sessão…";
    case AUTH_LOGOUT:
        return "Saindo da conta…";
    case AUTH_READY:
        if (client->ws_managed && client->ws_requested &&
            (client->ws_validate || client->ws_validated || client->validation_wait))
            return client->ws_active ? "Login realizado. Reconectando tempo real…"
                                     : "Tempo real aguardando esta aba ficar ativa.";
        switch (ws_get_state(client->websocket)) {
        case WS_QUEUED:
        case WS_CONNECTING:
            return "Login realizado. Conectando tempo real…";
        case WS_OPEN:
            return "Login realizado. Tempo real conectado.";
        case WS_ERROR:
            return ws_message(client->websocket);
        case WS_CLOSED:
            return "Login realizado. Tempo real encerrado.";
        default:
            return client->message;
        }
    default:
        return client->message;
    }
}

const char *auth_username(const auth_client *client)
{
    return client->user;
}

const char *auth_server_name(const auth_client *client)
{
    return client->name;
}

bool auth_server_public(const auth_client *client)
{
    return client->server_public;
}

bool auth_server_password_error(const auth_client *client)
{
    return client->server_password_error;
}

auth_input_error auth_error_input(const auth_client *client)
{
    return client->input_error;
}

bool auth_has_session(const auth_client *client)
{
    return client->authenticated && !client->recovering && !client->logout_requested;
}

const char *auth_user_id(const auth_client *client)
{
    return auth_has_session(client) ? client->user_id : "";
}

const char *auth_instance_url(const auth_client *client)
{
    return client->base ? client->base : "";
}

uint64_t auth_context_generation(const auth_client *client)
{
    return client->generation;
}

uint64_t auth_ws_epoch(const auth_client *client)
{
    return client->ws_epoch;
}
