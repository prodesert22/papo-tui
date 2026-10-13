#define _POSIX_C_SOURCE 200809L
#include "net/auth.h"
#include "net/rate_limit.h"
#include "net/clock.h"

#include <curl/curl.h>
#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>

#define RESPONSE_LIMIT (1024U * 1024U)

struct auth_client {
    CURLSH *cookies;
    ws_client *websocket;
    CURL *easy;
    CURLM *multi;
    struct curl_slist *headers;
    bool attached;
    bool queued;
    unsigned retry_after;
    auth_state state;
    auth_state operation;
    bool ws_requested;
    uint64_t recheck_at;
    char *base;
    char *username;
    char *password;
    char *server_password;
    char *payload;
    char *response;
    size_t response_length;
    bool overflow;
    bool server_public;
    bool server_password_error;
    char message[192];
    char name[128];
    char user[128];
    char user_id[64];
};

static void erase_free(char **value)
{
    if (*value) {
        volatile unsigned char *p = (volatile unsigned char *)*value;
        size_t length = strlen(*value);
        while (length--) *p++ = 0;
        free(*value);
        *value = NULL;
    }
}

static void forget_inputs(auth_client *client)
{
    erase_free(&client->username);
    erase_free(&client->password);
    erase_free(&client->server_password);
}

static void detach(auth_client *client)
{
    client->queued = false;
    if (client->attached) {
        curl_multi_remove_handle(client->multi, client->easy);
        client->attached = false;
    }
    /* libcurl retains POSTFIELDS: sever its pointer before freeing the buffer. */
    curl_easy_setopt(client->easy, CURLOPT_POSTFIELDS, NULL);
    erase_free(&client->payload);
    curl_slist_free_all(client->headers);
    client->headers = NULL;
    curl_easy_setopt(client->easy, CURLOPT_HTTPHEADER, NULL);
}

static void clear_cookies(auth_client *client)
{
    curl_easy_setopt(client->easy, CURLOPT_COOKIELIST, "ALL");
}

static void fail(auth_client *client, const char *message)
{
    client->server_password_error = false;
    ws_abort(client->websocket);
    detach(client);
    forget_inputs(client);
    clear_cookies(client);
    client->state = AUTH_ERROR;
    client->name[0] = client->user[0] = '\0';
    client->user_id[0] = '\0';
    snprintf(client->message, sizeof(client->message), "%s", message);
}

char *auth_normalize_url(const char *url, char *error, size_t error_size)
{
    CURLU *parsed = curl_url();
    char *scheme = NULL, *host = NULL, *path = NULL, *part = NULL, *result = NULL;
    const char *reason = "Informe uma URL válida com https:// ou http://.";
    if (!parsed || !url || !*url || strlen(url) > 2048 ||
        curl_url_set(parsed, CURLUPART_URL, url, 0) != CURLUE_OK) goto done;
    if (curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK) goto done;
    if (strcmp(scheme, "https") != 0 && strcmp(scheme, "http") != 0) goto done;
    reason = "HTTP é permitido apenas em localhost. Use HTTPS para outros servidores.";
    if (strcmp(scheme, "http") == 0 && strcmp(host, "localhost") != 0 &&
        strcmp(host, "127.0.0.1") != 0 && strcmp(host, "[::1]") != 0) goto done;
    reason = "Use uma URL sem credenciais, query ou fragmento.";
    const CURLUPart forbidden[] = {CURLUPART_USER, CURLUPART_PASSWORD,
                                  CURLUPART_QUERY, CURLUPART_FRAGMENT};
    for (size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
        if (curl_url_get(parsed, forbidden[i], &part, CURLU_GET_EMPTY) == CURLUE_OK) {
            curl_free(part);
            part = NULL;
            goto done;
        }
    }
    if (curl_url_get(parsed, CURLUPART_PATH, &path, 0) != CURLUE_OK) goto done;
    /* Remove trailing slashes, preserving any reverse-proxy path prefix. */
    size_t length = strlen(path);
    while (length && path[length - 1] == '/') path[--length] = '\0';
    if (curl_url_set(parsed, CURLUPART_PATH, path, 0) != CURLUE_OK) goto done;
    if (curl_url_get(parsed, CURLUPART_URL, &result, 0) != CURLUE_OK) goto done;
    length = strlen(result);
    while (length && result[length - 1] == '/') result[--length] = '\0';
done:
    if (!result && error && error_size) snprintf(error, error_size, "%s", reason);
    curl_free(scheme);
    curl_free(host);
    curl_free(path);
    curl_free(part);
    curl_url_cleanup(parsed);
    return result;
}

static size_t receive(char *data, size_t size, size_t count, void *context)
{
    auth_client *client = context;
    if (size && count > SIZE_MAX / size) return 0;
    size_t bytes = size * count;
    if (bytes > RESPONSE_LIMIT - client->response_length) {
        client->overflow = true;
        return 0;
    }
    char *next = realloc(client->response, client->response_length + bytes + 1);
    if (!next) return 0;
    client->response = next;
    memcpy(next + client->response_length, data, bytes);
    client->response_length += bytes;
    next[client->response_length] = '\0';
    return bytes;
}

static size_t receive_header(char *data, size_t size, size_t count, void *context)
{
    auth_client *client = context;
    if (size && count > SIZE_MAX / size) return 0;
    size_t bytes = size * count;
    if (bytes >= 5 && strncasecmp(data, "HTTP/", 5) == 0)
        client->retry_after = PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    if (bytes > 12 && strncasecmp(data, "Retry-After:", 12) == 0)
        client->retry_after = net_rate_retry_after(data + 12, bytes - 12);
    return bytes;
}

static bool request(auth_client *client, auth_state state, const char *path,
                    json_t *body, bool post)
{
    detach(client);
    free(client->response);
    client->response = NULL;
    client->response_length = 0;
    client->overflow = false;
    client->retry_after = PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    size_t length = strlen(client->base) + strlen(path) + 1;
    char *url = malloc(length);
    if (!url) goto error;
    snprintf(url, length, "%s%s", client->base, path);
    CURLcode result = curl_easy_setopt(client->easy, CURLOPT_URL, url);
    free(url);
    if (result != CURLE_OK) goto error;
    client->headers = curl_slist_append(NULL, "Accept: application/json");
    if (!client->headers) goto error;
    if (body) {
        client->payload = json_dumps(body, JSON_COMPACT);
        if (!client->payload) goto error;
        struct curl_slist *headers = curl_slist_append(client->headers, "Content-Type: application/json");
        if (!headers) goto error;
        client->headers = headers;
    }
    if (curl_easy_setopt(client->easy, CURLOPT_HTTPHEADER, client->headers) != CURLE_OK) goto error;
    if (post) {
        if (curl_easy_setopt(client->easy, CURLOPT_POSTFIELDS, client->payload ? client->payload : "") != CURLE_OK ||
            curl_easy_setopt(client->easy, CURLOPT_POSTFIELDSIZE, (long)(client->payload ? strlen(client->payload) : 0)) != CURLE_OK)
            goto error;
    } else {
        if (curl_easy_setopt(client->easy, CURLOPT_HTTPGET, 1L) != CURLE_OK) goto error;
    }
    client->queued = true;
    client->state = state;
    client->operation = state;
    return true;
error:
    fail(client, "Não foi possível preparar a conexão.");
    return false;
}

static bool user_login(auth_client *client)
{
    json_t *body = json_pack("{s:s,s:s}", "username", client->username,
                             "password", client->password);
    if (!body) {
        fail(client, "Não foi possível preparar o login.");
        return false;
    }
    bool result = request(client, AUTH_LOGIN, "/auth/login", body, true);
    json_decref(body);
    forget_inputs(client);
    return result;
}

auth_client *auth_create(void)
{
    auth_client *client = calloc(1, sizeof(*client));
    if (!client) return NULL;
    client->easy = curl_easy_init();
    client->multi = curl_multi_init();
    client->cookies = curl_share_init();
    if (!client->easy || !client->multi || !client->cookies ||
        curl_share_setopt(client->cookies, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) != CURLSHE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_SHARE, client->cookies) != CURLE_OK ||
        !(client->websocket = ws_create(client->cookies))) {
        if (client->easy) curl_easy_cleanup(client->easy);
        if (client->multi) curl_multi_cleanup(client->multi);
        if (client->cookies) curl_share_cleanup(client->cookies);
        free(client);
        return NULL;
    }
#define SET(option, value) do { if (curl_easy_setopt(client->easy, option, value) != CURLE_OK) { auth_destroy(client); return NULL; } } while (0)
    SET(CURLOPT_COOKIEFILE, "");
    SET(CURLOPT_WRITEFUNCTION, receive);
    SET(CURLOPT_WRITEDATA, client);
    SET(CURLOPT_HEADERFUNCTION, receive_header);
    SET(CURLOPT_HEADERDATA, client);
    SET(CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    SET(CURLOPT_TIMEOUT_MS, 15000L);
    SET(CURLOPT_NOSIGNAL, 1L);
    SET(CURLOPT_FOLLOWLOCATION, 0L);
    SET(CURLOPT_SSL_VERIFYPEER, 1L);
    SET(CURLOPT_SSL_VERIFYHOST, 2L);
    SET(CURLOPT_PROTOCOLS_STR, "http,https");
    SET(CURLOPT_USERAGENT, "papo-tui/0.1.0-dev");
    const char *ca_bundle = getenv("CURL_CA_BUNDLE");
    if (ca_bundle && *ca_bundle) SET(CURLOPT_CAINFO, ca_bundle);
#undef SET
    snprintf(client->message, sizeof(client->message), "Preencha os dados para entrar.");
    return client;
}

bool auth_start_with_policy(auth_client *client, const char *url, const char *username,
                            const char *password, const char *server_password, bool allow_local_http)
{
    if (auth_busy(client) || client->state == AUTH_READY) return false;
    detach(client);
    ws_abort(client->websocket);
    client->ws_requested = false;
    client->server_password_error = false;
    forget_inputs(client);
    clear_cookies(client);
    curl_free(client->base);
    client->base = NULL;
    client->name[0] = client->user[0] = '\0';
    client->user_id[0] = '\0';
    if (!username || !*username || !password || !*password) {
        fail(client, "Preencha usuário e senha.");
        return false;
    }
    if (strlen(username) > 1024 || strlen(password) > 4096 ||
        (server_password && strlen(server_password) > 4096)) {
        fail(client, "O usuário ou a senha excedeu o limite do cliente.");
        return false;
    }
    char error[192];
    client->base = auth_normalize_url(url, error, sizeof(error));
    if (!client->base) {
        fail(client, error);
        return false;
    }
    if (!allow_local_http && strncmp(client->base, "http://", 7) == 0) {
        fail(client, "HTTP local exige --allow-local-http. Use HTTPS para acesso remoto.");
        return false;
    }
    client->username = strdup(username);
    client->password = strdup(password);
    client->server_password = strdup(server_password ? server_password : "");
    if (!client->username || !client->password || !client->server_password) {
        fail(client, "Não foi possível preparar o login.");
        return false;
    }
    return request(client, AUTH_SERVER, "/server", NULL, false);
}

bool auth_start(auth_client *client, const char *url, const char *username,
                const char *password, const char *server_password)
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
    if (!client->base) { fail(client, reason); return false; }
    if (!allow_local_http && strncmp(client->base, "http://", 7) == 0) {
        fail(client, "HTTP local exige --allow-local-http. Use HTTPS para acesso remoto.");
        return false;
    }
    return request(client, AUTH_HEALTH, "/health", NULL, false);
}

static void copy_server_name(auth_client *client, const char *name)
{
    if (!name || !*name) name = "Nome não informado";
    size_t length = strlen(name);
    if (length >= sizeof(client->name)) length = sizeof(client->name) - 1;
    while (length && ((unsigned char)name[length] & 0xc0) == 0x80) --length;
    memcpy(client->name, name, length);
    client->name[length] = '\0';
}

static void complete(auth_client *client, CURLcode result, long status)
{
    auth_state previous = client->operation;
    detach(client);
    if (result != CURLE_OK) {
        const char *message = "Não foi possível conectar. Confira a URL e tente novamente.";
        if (client->overflow) message = "O servidor enviou uma resposta grande demais.";
        else if (result == CURLE_COULDNT_RESOLVE_HOST) message = "Nome do servidor não encontrado (DNS). Confira o host.";
        else if (result == CURLE_COULDNT_RESOLVE_PROXY) message = "Não foi possível resolver o proxy (DNS). Confira sua configuração.";
        else if (result == CURLE_COULDNT_CONNECT) {
            long system_error = 0;
            curl_easy_getinfo(client->easy, CURLINFO_OS_ERRNO, &system_error);
            message = system_error == ECONNREFUSED ? "Conexão recusada. Confira o host e a porta." :
                      "Serviço indisponível ou rede inacessível. Confira host e porta.";
        }
        else if (result == CURLE_OPERATION_TIMEDOUT) message = "O servidor demorou para responder. Tente novamente.";
        else if (result == CURLE_PEER_FAILED_VERIFICATION || result == CURLE_SSL_CONNECT_ERROR ||
                 result == CURLE_SSL_CACERT_BADFILE || result == CURLE_SSL_CERTPROBLEM)
            message = "Falha ao verificar a conexão segura com o servidor.";
        fail(client, message);
        return;
    }
    long expected = (previous == AUTH_SERVER_PASSWORD || previous == AUTH_LOGOUT) ? 204 : 200;
    if (status != expected) {
        const char *message = "O servidor não aceitou a solicitação. Tente novamente.";
        if (status == 401) {
            message = previous == AUTH_SERVER_PASSWORD ? "Senha do servidor incorreta." : "Usuário ou senha inválidos, ou sessão não autorizada.";
        } else if (status == 429) {
            net_rate_defer(client->retry_after);
            message = "Limite de requisições atingido. Aguarde antes de tentar novamente.";
        } else if (status == 403) message = "O servidor recusou o acesso a esta conta.";
        else if (status == 404 && (previous == AUTH_SERVER || previous == AUTH_DISCOVERY_SERVER)) message = "Instância não inicializada ou URL incompatível com o Papo.";
        else if (status == 404 && previous == AUTH_HEALTH) message = "Backend incompatível: /health não está disponível nessa URL.";
        else if (status == 502 || status == 503 || status == 504) message = "Serviço indisponível. Tente novamente ou corrija a URL.";
        else if (status >= 500 && (previous == AUTH_HEALTH || previous == AUTH_DISCOVERY_SERVER))
            message = "Serviço indisponível. Tente novamente ou corrija a URL.";
        else if (status >= 300 && status < 400) message = "O servidor redirecionou a conexão. Informe a URL final.";
        fail(client, message);
        if (status == 401 && previous == AUTH_SERVER_PASSWORD) client->server_password_error = true;
        return;
    }
    if (previous == AUTH_HEALTH) {
        request(client, AUTH_DISCOVERY_SERVER, "/server", NULL, false);
        return;
    }
    if (previous == AUTH_LOGOUT) {
        ws_abort(client->websocket);
        clear_cookies(client);
        client->state = AUTH_IDLE;
        client->user[0] = client->name[0] = '\0';
        client->user_id[0] = '\0';
        snprintf(client->message, sizeof(client->message), "Você saiu da conta.");
        return;
    }
    if (previous == AUTH_SERVER_PASSWORD) {
        user_login(client);
        return;
    }
    json_error_t error;
    json_t *body = json_loadb(client->response ? client->response : "",
                             client->response_length, JSON_REJECT_DUPLICATES, &error);
    if (!json_is_object(body)) {
        json_decref(body);
        fail(client, "O servidor retornou uma resposta incompatível com o Papo.");
        return;
    }
    if (previous == AUTH_SERVER || previous == AUTH_DISCOVERY_SERVER) {
        json_t *public_value = json_object_get(body, "public");
        json_t *name_value = json_object_get(body, "name");
        const char *name = json_string_value(name_value);
        if (!json_is_boolean(public_value) || (name_value && (!json_is_string(name_value) || json_string_length(name_value) != strlen(name)))) {
            fail(client, "Backend incompatível: não foi possível identificar a configuração da instância.");
        } else {
            copy_server_name(client, name);
            client->server_public = json_is_true(public_value);
            if (previous == AUTH_DISCOVERY_SERVER) {
                clear_cookies(client); /* Discovery cookies never become authentication. */
                client->state = AUTH_IDENTIFIED;
                snprintf(client->message, sizeof(client->message), "%s", client->server_public ?
                         "Instância pública identificada." : "Instância privada identificada; login exige senha do servidor.");
            } else if (json_is_true(public_value)) user_login(client);
            else if (!*client->server_password) {
                fail(client, "Esta instância é privada. Preencha a senha do servidor.");
                client->server_password_error = true;
            }
            else {
                json_t *payload = json_pack("{s:s}", "server_password", client->server_password);
                if (!payload) fail(client, "Não foi possível preparar a autorização.");
                else {
                    request(client, AUTH_SERVER_PASSWORD, "/auth/login_server", payload, true);
                    json_decref(payload);
                    erase_free(&client->server_password);
                }
            }
        }
    } else if (previous == AUTH_LOGIN) {
        if (json_is_true(json_object_get(body, "connection_violation")))
            fail(client, "A sessão foi revogada pelo servidor. Entre novamente.");
        else request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false);
    } else if (previous == AUTH_WHOAMI) {
        const char *username = json_string_value(json_object_get(body, "username"));
        const char *id = json_string_value(json_object_get(body, "id"));
        if (!username || !*username || !id || !*id || strlen(id) >= sizeof(client->user_id) ||
            (*client->user_id && strcmp(id, client->user_id) != 0) ||
            json_is_true(json_object_get(body, "connection_violation"))) {
            fail(client, "Não foi possível confirmar a sessão. Entre novamente.");
        } else {
            snprintf(client->user, sizeof(client->user), "%s", username);
            snprintf(client->user_id, sizeof(client->user_id), "%s", id);
            uint64_t now;
            if (!net_now_ms(&now)) fail(client, "Não foi possível acompanhar a sessão.");
            else {
                client->recheck_at = now + 30000;
                client->state = AUTH_READY;
                snprintf(client->message, sizeof(client->message), "Login realizado. Você está conectado.");
            }
        }
    }
    json_decref(body);
}

void auth_poll(auth_client *client)
{
    ws_poll(client->websocket);
    if (client->state == AUTH_READY && client->ws_requested && !client->queued && !client->attached) {
        uint64_t now;
        if (!net_now_ms(&now)) { fail(client, "Não foi possível acompanhar a sessão."); return; }
        if (now >= client->recheck_at && request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false))
            client->state = AUTH_READY; /* Background REST validation keeps logout available. */
    }
    if (client->queued) {
        int admitted = net_rate_try_acquire();
        if (admitted == 0) return;
        if (admitted < 0 || curl_multi_add_handle(client->multi, client->easy) != CURLM_OK) {
            fail(client, "Não foi possível iniciar a conexão.");
            return;
        }
        client->queued = false;
        client->attached = true;
    }
    if (!client->attached) return;
    int running;
    if (curl_multi_perform(client->multi, &running) != CURLM_OK) {
        fail(client, "A conexão foi interrompida. Tente novamente.");
        return;
    }
    int remaining;
    CURLMsg *message;
    while ((message = curl_multi_info_read(client->multi, &remaining))) {
        if (message->msg == CURLMSG_DONE) {
            CURLcode result = message->data.result;
            long status = 0;
            curl_easy_getinfo(client->easy, CURLINFO_RESPONSE_CODE, &status);
            complete(client, result, status);
            break; /* The next request may have been scheduled on the same handle. */
        }
    }
}

bool auth_logout(auth_client *client)
{
    if (client->state != AUTH_READY) return false;
    ws_close(client->websocket);
    return request(client, AUTH_LOGOUT, "/auth/logout", NULL, true);
}

void auth_cancel(auth_client *client)
{
    client->server_password_error = false;
    ws_abort(client->websocket);
    detach(client);
    forget_inputs(client);
    clear_cookies(client);
    client->state = AUTH_IDLE;
    client->user[0] = client->name[0] = '\0';
    client->user_id[0] = '\0';
    snprintf(client->message, sizeof(client->message), "Conexão cancelada.");
}

void auth_destroy(auth_client *client)
{
    if (!client) return;
    detach(client);
    ws_destroy(client->websocket);
    forget_inputs(client);
    curl_easy_cleanup(client->easy);
    curl_multi_cleanup(client->multi);
    curl_share_cleanup(client->cookies);
    curl_free(client->base);
    free(client->response);
    free(client);
}

auth_state auth_get_state(const auth_client *client) { return client->state; }
bool auth_busy(const auth_client *client)
{
    return client->state == AUTH_SERVER || client->state == AUTH_SERVER_PASSWORD ||
           client->state == AUTH_LOGIN || client->state == AUTH_WHOAMI || client->state == AUTH_LOGOUT ||
           client->state == AUTH_HEALTH || client->state == AUTH_DISCOVERY_SERVER;
}
const char *auth_message(const auth_client *client)
{
    switch (client->state) {
        case AUTH_SERVER: return "Consultando a instância…";
        case AUTH_HEALTH: return "Consultando a disponibilidade da instância…";
        case AUTH_DISCOVERY_SERVER: return "Identificando a instância…";
        case AUTH_SERVER_PASSWORD: return "Autorizando acesso ao servidor…";
        case AUTH_LOGIN: return "Entrando na sua conta…";
        case AUTH_WHOAMI: return "Confirmando sua sessão…";
        case AUTH_LOGOUT: return "Saindo da conta…";
        case AUTH_READY:
            switch (ws_get_state(client->websocket)) {
                case WS_QUEUED: case WS_CONNECTING: return "Login realizado. Conectando tempo real…";
                case WS_OPEN: return "Login realizado. Tempo real conectado.";
                case WS_ERROR: return ws_message(client->websocket);
                case WS_CLOSED: return "Login realizado. Tempo real encerrado.";
                default: return client->message;
            }
        default: return client->message;
    }
}
const char *auth_username(const auth_client *client) { return client->user; }
const char *auth_server_name(const auth_client *client) { return client->name; }
bool auth_server_public(const auth_client *client) { return client->server_public; }
bool auth_server_password_error(const auth_client *client) { return client->server_password_error; }
bool auth_connect_ws(auth_client *client)
{
    if (client->state != AUTH_READY) return false;
    client->ws_requested = true;
    return ws_connect(client->websocket, client->base);
}
ws_state auth_ws_state(const auth_client *client) { return ws_get_state(client->websocket); }
const char *auth_ws_message(const auth_client *client) { return ws_message(client->websocket); }
long auth_ws_http_status(const auth_client *client) { return ws_http_status(client->websocket); }
json_t *auth_next_event(auth_client *client)
{
    return client->state == AUTH_READY ? ws_pop(client->websocket) : NULL;
}
bool auth_send_event(auth_client *client, const json_t *event)
{
    return client->state == AUTH_READY && ws_send(client->websocket, event);
}
