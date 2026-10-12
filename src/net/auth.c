#define _POSIX_C_SOURCE 200809L
#include "net/auth.h"

#include <curl/curl.h>
#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESPONSE_LIMIT (1024U * 1024U)

struct auth_client {
    CURL *easy;
    CURLM *multi;
    struct curl_slist *headers;
    bool attached;
    auth_state state;
    char *base;
    char *username;
    char *password;
    char *server_password;
    char *payload;
    char *response;
    size_t response_length;
    bool overflow;
    char message[192];
    char name[128];
    char user[128];
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
    detach(client);
    forget_inputs(client);
    clear_cookies(client);
    client->state = AUTH_ERROR;
    client->name[0] = client->user[0] = '\0';
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
        if (curl_url_get(parsed, forbidden[i], &part, 0) == CURLUE_OK) {
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

static bool request(auth_client *client, auth_state state, const char *path,
                    json_t *body, bool post)
{
    detach(client);
    free(client->response);
    client->response = NULL;
    client->response_length = 0;
    client->overflow = false;
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
    curl_easy_setopt(client->easy, CURLOPT_HTTPHEADER, client->headers);
    if (post) {
        curl_easy_setopt(client->easy, CURLOPT_POSTFIELDS, client->payload ? client->payload : "");
        curl_easy_setopt(client->easy, CURLOPT_POSTFIELDSIZE, (long)(client->payload ? strlen(client->payload) : 0));
    } else {
        curl_easy_setopt(client->easy, CURLOPT_HTTPGET, 1L);
    }
    if (curl_multi_add_handle(client->multi, client->easy) != CURLM_OK) goto error;
    client->attached = true;
    client->state = state;
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
    if (!client->easy || !client->multi) {
        if (client->easy) curl_easy_cleanup(client->easy);
        if (client->multi) curl_multi_cleanup(client->multi);
        free(client);
        return NULL;
    }
    curl_easy_setopt(client->easy, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(client->easy, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(client->easy, CURLOPT_WRITEDATA, client);
    curl_easy_setopt(client->easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(client->easy, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(client->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(client->easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(client->easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(client->easy, CURLOPT_USERAGENT, "papo-tui/0.1.0-dev");
    snprintf(client->message, sizeof(client->message), "Preencha os dados para entrar.");
    return client;
}

bool auth_start(auth_client *client, const char *url, const char *username,
                const char *password, const char *server_password)
{
    if (auth_busy(client) || client->state == AUTH_READY) return false;
    detach(client);
    forget_inputs(client);
    clear_cookies(client);
    curl_free(client->base);
    client->base = NULL;
    client->name[0] = client->user[0] = '\0';
    if (!username || !*username || !password || !*password) {
        fail(client, "Preencha usuário e senha.");
        return false;
    }
    char error[192];
    client->base = auth_normalize_url(url, error, sizeof(error));
    if (!client->base) {
        fail(client, error);
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

static void complete(auth_client *client, CURLcode result, long status)
{
    auth_state previous = client->state;
    detach(client);
    if (result != CURLE_OK) {
        const char *message = "Não foi possível conectar. Confira a URL e tente novamente.";
        if (client->overflow) message = "O servidor enviou uma resposta grande demais.";
        else if (result == CURLE_OPERATION_TIMEDOUT) message = "O servidor demorou para responder. Tente novamente.";
        else if (result == CURLE_PEER_FAILED_VERIFICATION || result == CURLE_SSL_CONNECT_ERROR)
            message = "Falha ao verificar a conexão segura com o servidor.";
        fail(client, message);
        return;
    }
    long expected = (previous == AUTH_SERVER_PASSWORD || previous == AUTH_LOGOUT) ? 204 : 200;
    if (status != expected) {
        const char *message = "O servidor não aceitou a solicitação. Tente novamente.";
        if (status == 401) {
            message = previous == AUTH_SERVER_PASSWORD ? "Senha do servidor incorreta." : "Usuário ou senha inválidos, ou sessão não autorizada.";
        } else if (status == 403) message = "O servidor recusou o acesso a esta conta.";
        else if (status == 404 && previous == AUTH_SERVER) message = "Instância não inicializada ou URL incompatível com o Papo.";
        else if (status >= 300 && status < 400) message = "O servidor redirecionou a conexão. Informe a URL final.";
        fail(client, message);
        return;
    }
    if (previous == AUTH_LOGOUT) {
        clear_cookies(client);
        client->state = AUTH_IDLE;
        client->user[0] = client->name[0] = '\0';
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
    if (previous == AUTH_SERVER) {
        json_t *public_value = json_object_get(body, "public");
        const char *name = json_string_value(json_object_get(body, "name"));
        if (!json_is_boolean(public_value)) {
            fail(client, "Não foi possível identificar a configuração da instância.");
        } else {
            snprintf(client->name, sizeof(client->name), "%s", name ? name : "Papo");
            if (json_is_true(public_value)) user_login(client);
            else if (!*client->server_password) fail(client, "Esta instância é privada. Preencha a senha do servidor.");
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
        if (!username || !*username || !id || !*id ||
            json_is_true(json_object_get(body, "connection_violation"))) {
            fail(client, "Não foi possível confirmar a sessão. Entre novamente.");
        } else {
            snprintf(client->user, sizeof(client->user), "%s", username);
            client->state = AUTH_READY;
            snprintf(client->message, sizeof(client->message), "Login realizado. Você está conectado.");
        }
    }
    json_decref(body);
}

void auth_poll(auth_client *client)
{
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
    return request(client, AUTH_LOGOUT, "/auth/logout", NULL, true);
}

void auth_cancel(auth_client *client)
{
    detach(client);
    forget_inputs(client);
    clear_cookies(client);
    client->state = AUTH_IDLE;
    client->user[0] = client->name[0] = '\0';
    snprintf(client->message, sizeof(client->message), "Conexão cancelada.");
}

void auth_destroy(auth_client *client)
{
    if (!client) return;
    detach(client);
    forget_inputs(client);
    curl_easy_cleanup(client->easy);
    curl_multi_cleanup(client->multi);
    curl_free(client->base);
    free(client->response);
    free(client);
}

auth_state auth_get_state(const auth_client *client) { return client->state; }
bool auth_busy(const auth_client *client)
{
    return client->state == AUTH_SERVER || client->state == AUTH_SERVER_PASSWORD ||
           client->state == AUTH_LOGIN || client->state == AUTH_WHOAMI || client->state == AUTH_LOGOUT;
}
const char *auth_message(const auth_client *client)
{
    switch (client->state) {
        case AUTH_SERVER: return "Consultando a instância…";
        case AUTH_SERVER_PASSWORD: return "Autorizando acesso ao servidor…";
        case AUTH_LOGIN: return "Entrando na sua conta…";
        case AUTH_WHOAMI: return "Confirmando sua sessão…";
        case AUTH_LOGOUT: return "Saindo da conta…";
        default: return client->message;
    }
}
const char *auth_username(const auth_client *client) { return client->user; }
const char *auth_server_name(const auth_client *client) { return client->name; }
