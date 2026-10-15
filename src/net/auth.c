#define _POSIX_C_SOURCE 200809L
#include "net/auth.h"
#include <time.h>
#include <strings.h>
#include "net/rate_limit.h"
#include "net/clock.h"
#include "net/session_time.h"
#include "net/retry.h"

#include <curl/curl.h>
#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <wchar.h>
#include <wctype.h>
#include <unistd.h>

#define RESPONSE_LIMIT (1024U * 1024U)

struct auth_client {
    CURLSH *cookies;
    ws_client *websocket;
    CURL *easy;
    CURLM *multi;
    struct curl_slist *headers;
    curl_mime *mime;
    bool send_ready;
    auth_send_receipt send_receipt;
    json_t *sent_message;
    char send_channel[64];
    bool attached;
    bool queued;
    unsigned retry_after;
    auth_state state;
    auth_state operation;
    bool ws_requested;
    bool ws_managed, ws_active, ws_validate, ws_validated, validation_wait;
    uint64_t ws_retry_at, ws_stable_at, ws_epoch, expires_at;
    unsigned ws_failures;
    bool restoring, session_rejected, gate_passed;
    auth_session_gate session_gate;
    void *gate_context;
    uint64_t recheck_at;
    uint64_t validation_deadline;
    uint64_t refresh_at, generation;
    bool registering, authenticated, recovering, after_refresh, logout_requested;
    bool api_retried, response_ready;
    bool api_uncertain;
    const char *api_method; /* Static method literal; no borrowed input. */
    auth_input_error input_error;
    char *api_path;
    char *refresh_cookie;
    json_t *api_response;
    json_t *api_body;
    long api_status;
    uint64_t response_generation;
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
    char user[1025];
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
    curl_easy_setopt(client->easy, CURLOPT_MIMEPOST, NULL);
    curl_mime_free(client->mime);
    client->mime = NULL;
    curl_easy_setopt(client->easy, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(client->easy, CURLOPT_CUSTOMREQUEST, NULL);
    erase_free(&client->payload);
    curl_slist_free_all(client->headers);
    client->headers = NULL;
    curl_easy_setopt(client->easy, CURLOPT_HTTPHEADER, NULL);
}

static void clear_cookies(auth_client *client)
{
    curl_easy_setopt(client->easy, CURLOPT_COOKIELIST, "ALL");
}

static void clear_api(auth_client *client)
{
    free(client->api_path);
    client->api_path = NULL;
    json_decref(client->api_body);
    client->api_body = NULL;
    json_decref(client->api_response);
    client->api_response = NULL;
    client->response_ready = client->api_retried = false;
    client->api_method = NULL;
    client->api_uncertain = false;
}

static void clear_send(auth_client *client)
{
    json_decref(client->sent_message);
    client->sent_message = NULL;
    client->send_ready = false;
    client->send_channel[0] = '\0';
}

static void clear_identity(auth_client *client)
{
    clear_send(client);
    client->authenticated = client->recovering = client->after_refresh = false;
    client->refresh_at = client->recheck_at = 0;
    client->validation_deadline = 0;
    client->expires_at = client->ws_retry_at = client->ws_stable_at = 0;
    client->ws_failures = 0;
    client->ws_validate = client->ws_validated = client->validation_wait = false;
    client->ws_epoch = 0; client->ws_requested = false;
    client->user[0] = client->user_id[0] = '\0';
    erase_free(&client->refresh_cookie);
    ++client->generation;
}

static void logout_done(auth_client *client, bool revoked)
{
    ws_abort(client->websocket);
    detach(client);
    forget_inputs(client);
    clear_cookies(client);
    clear_api(client);
    clear_identity(client);
    client->logout_requested = client->ws_requested = false;
    client->server_password_error = false;
    client->input_error = AUTH_INPUT_NONE;
    client->name[0] = '\0';
    client->state = AUTH_IDLE;
    snprintf(client->message, sizeof(client->message), "%s", revoked ? "Você saiu da conta." :
             "Sessão local encerrada; revogação remota não confirmada.");
}

static void fail(auth_client *client, const char *message)
{
    if (client->logout_requested) { logout_done(client, false); return; }
    client->server_password_error = false;
    client->input_error = AUTH_INPUT_NONE;
    ws_abort(client->websocket);
    detach(client);
    forget_inputs(client);
    clear_cookies(client);
    clear_api(client);
    clear_identity(client);
    client->state = AUTH_ERROR;
    client->name[0] = client->user[0] = '\0';
    client->user_id[0] = '\0';
    snprintf(client->message, sizeof(client->message), "%s", message);
}

static bool input_fail(auth_client *client, auth_input_error field, const char *message)
{
    fail(client, message);
    client->input_error = field;
    client->server_password_error = field == AUTH_INPUT_SERVER_PASSWORD;
    return false;
}

static const char *json_text(json_t *object, const char *key, size_t capacity)
{
    json_t *value = json_object_get(object, key);
    const char *text = json_string_value(value);
    return text && *text && json_string_length(value) == strlen(text) && strlen(text) < capacity ? text : NULL;
}

static bool problem_is(auth_client *client, const char *kind)
{
    json_error_t error;
    json_t *body = json_loadb(client->response ? client->response : "", client->response_length, JSON_REJECT_DUPLICATES, &error);
    const char *type = json_text(body, "type", 2049);
    const char *last = type ? strrchr(type, '/') : NULL;
    bool matches = last && strcmp(last + 1, kind) == 0;
    json_decref(body);
    return matches;
}

static bool registration_error(auth_input_error *field, char *message, size_t capacity,
                               auth_input_error error, const char *reason)
{
    if (field) *field = error;
    if (message && capacity) snprintf(message, capacity, "%s", reason);
    return false;
}

bool auth_validate_registration(const char *username, const char *password,
                                auth_input_error *field, char *message, size_t capacity)
{
    if (field) *field = AUTH_INPUT_NONE;
    if (message && capacity) *message = '\0';
    if (!username || !*username)
        return registration_error(field, message, capacity, AUTH_INPUT_USERNAME, "Preencha o usuário.");
    if (!password || !*password)
        return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD, "Preencha a senha.");

    size_t characters = 0;
    mbstate_t state = {0};
    while (*username) {
        wchar_t character;
        size_t bytes = mbrtowc(&character, username, MB_CUR_MAX, &state);
        if (!bytes || bytes == (size_t)-1 || bytes == (size_t)-2 || !iswprint(character))
            return registration_error(field, message, capacity, AUTH_INPUT_USERNAME, "Informe um usuário válido, sem controles de terminal.");
        ++characters; username += bytes;
    }
    if (characters < 3) return registration_error(field, message, capacity, AUTH_INPUT_USERNAME, "O usuário precisa ter pelo menos 3 caracteres.");
    characters = 0;
    bool upper = false, special = false;
    memset(&state, 0, sizeof(state));
    while (*password) {
        wchar_t character;
        size_t bytes = mbrtowc(&character, password, MB_CUR_MAX, &state);
        if (!bytes || bytes == (size_t)-1 || bytes == (size_t)-2 || !iswprint(character))
            return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD, "Informe uma senha válida, sem controles de terminal.");
        upper |= iswupper(character) != 0;
        special |= iswalnum(character) == 0;
        ++characters; password += bytes;
    }
    if (characters < 8) return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD, "A senha precisa ter pelo menos 8 caracteres.");
    if (!upper) return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD, "A senha precisa de uma letra maiúscula.");
    if (!special) return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD, "A senha precisa de um caractere especial.");
    return true;
}

static bool registration_valid(auth_client *client, const char *username, const char *password)
{
    char message[192];
    auth_input_error field;
    if (auth_validate_registration(username, password, &field, message, sizeof(message))) return true;
    return input_fail(client, field, message);
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
    /* A fresh HTTP/1.1 connection avoids libcurl's stale-connection/stream
     * retries for this non-idempotent mutation. Other REST calls keep defaults. */
    bool mutation = state == AUTH_MESSAGE_SEND || state == AUTH_API_MUTATION;
    if (curl_easy_setopt(client->easy, CURLOPT_FRESH_CONNECT, mutation ? 1L : 0L) != CURLE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_FORBID_REUSE, mutation ? 1L : 0L) != CURLE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_HTTP_VERSION,
                         (long)(mutation ? CURL_HTTP_VERSION_1_1 : CURL_HTTP_VERSION_NONE)) != CURLE_OK) goto error;
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
    if (state == AUTH_API_MUTATION &&
        curl_easy_setopt(client->easy, CURLOPT_CUSTOMREQUEST, client->api_method) != CURLE_OK) goto error;
    client->queued = true;
    client->state = state;
    client->operation = state;
    client->gate_passed = false;
    return true;
error:
    if (state == AUTH_API_MUTATION) {
        detach(client); clear_api(client);
        client->api_status=0; client->response_generation=client->generation;
        client->response_ready=true; client->state=AUTH_READY;
        snprintf(client->message,sizeof(client->message),"Não foi possível preparar a operação.");
        return true; /* Accepted with an unsent local rejection. */
    }
    if (state == AUTH_MESSAGE_SEND) {
        detach(client);
        client->send_receipt.outcome = AUTH_SEND_REJECTED;
        client->send_ready = true;
        client->state = AUTH_READY;
        return false;
    }
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
    bool result = request(client, client->registering ? AUTH_REGISTER : AUTH_LOGIN,
                          client->registering ? "/auth/register" : "/auth/login", body, true);
    json_decref(body);
    forget_inputs(client);
    return result;
}

auth_client *auth_create(void)
{
    /* Jitter needs variation across processes; it is not secret material. */
    static bool seeded;
    if (!seeded) {
        struct timespec now = {0};
        clock_gettime(CLOCK_REALTIME, &now);
        srand((unsigned)now.tv_sec ^ (unsigned)now.tv_nsec ^ (unsigned)getpid());
        seeded = true;
    }
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

static bool start_account(auth_client *client, const char *url, const char *username,
                          const char *password, const char *server_password, bool allow_local_http, bool registering)
{
    if (auth_busy(client) || auth_has_session(client)) return false;
    detach(client);
    ws_abort(client->websocket);
    client->ws_requested = false;
    client->server_password_error = false;
    client->input_error = AUTH_INPUT_NONE;
    client->registering = registering;
    client->restoring = client->session_rejected = false;
    client->logout_requested = false;
    forget_inputs(client);
    clear_cookies(client);
    clear_api(client);
    clear_identity(client);
    curl_free(client->base);
    client->base = NULL;
    client->name[0] = client->user[0] = '\0';
    client->user_id[0] = '\0';
    if (!username || !*username) return input_fail(client, AUTH_INPUT_USERNAME, "Preencha usuário e senha.");
    if (!password || !*password) return input_fail(client, AUTH_INPUT_PASSWORD, "Preencha usuário e senha.");
    if (strlen(username) > 1024 || strlen(password) > 4096 ||
        (server_password && strlen(server_password) > 4096)) {
        fail(client, "O usuário ou a senha excedeu o limite do cliente.");
        return false;
    }
    if (registering && !registration_valid(client, username, password)) return false;
    char error[192];
    client->base = auth_normalize_url(url, error, sizeof(error));
    if (!client->base) {
        return input_fail(client, AUTH_INPUT_URL, error);
    }
    if (!allow_local_http && strncmp(client->base, "http://", 7) == 0) {
        return input_fail(client, AUTH_INPUT_URL, "HTTP local exige --allow-local-http. Use HTTPS para acesso remoto.");
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

bool auth_start_with_policy(auth_client *client, const char *url, const char *username,
                            const char *password, const char *server_password, bool allow_local_http)
{
    return start_account(client, url, username, password, server_password, allow_local_http, false);
}

bool auth_register_with_policy(auth_client *client, const char *url, const char *username,
                               const char *password, const char *server_password, bool allow_local_http)
{
    return start_account(client, url, username, password, server_password, allow_local_http, true);
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

/* Inspect libcurl's owned cookie engine through its Netscape-list API. The
 * opaque Auth value is used only to verify rotation and is never exposed. */
static char *cookie_snapshot(auth_client *client)
{
    struct curl_slist *cookies = NULL;
    char *snapshot = NULL;
    if (curl_easy_getinfo(client->easy, CURLINFO_COOKIELIST, &cookies) != CURLE_OK) return NULL;
    bool ambiguous = false;
    for (struct curl_slist *item = cookies; item; item = item->next) {
        const char *name = item->data;
        for (int i = 0; name && i < 5; ++i) {
            const char *tab = strchr(name, '\t');
            name = tab ? tab + 1 : NULL;
        }
        if (name && strncmp(name, "Auth\t", 5) == 0 && name[5]) {
            if (snapshot) ambiguous = true;
            else snapshot = strdup(name + 5);
        }
    }
    for (struct curl_slist *item = cookies; item; item = item->next) {
        volatile char *p = item->data;
        size_t length = strlen(item->data);
        while (length--) *p++ = 0;
    }
    curl_slist_free_all(cookies);
    if (ambiguous) erase_free(&snapshot);
    return snapshot;
}

static bool begin_refresh(auth_client *client)
{
    erase_free(&client->refresh_cookie);
    client->refresh_cookie = cookie_snapshot(client);
    if (!client->refresh_cookie) {
        fail(client, "A sessão não possui um cookie válido. Entre novamente.");
        return false;
    }
    client->after_refresh = true;
    return request(client, AUTH_REFRESH, "/auth/refresh", NULL, true);
}

bool auth_refresh(auth_client *client)
{
    if (!auth_has_session(client) || client->queued || client->attached || client->state != AUTH_READY ||
        client->api_path || client->logout_requested) return false;
    ws_state state = ws_get_state(client->websocket);
    if (state == WS_QUEUED || state == WS_CONNECTING) ws_abort(client->websocket);
    return begin_refresh(client);
}

static bool recover_session(auth_client *client)
{
    client->session_rejected = true; /* Cleared only by a verified whoami. */
    client->authenticated = false;
    client->recovering = client->after_refresh = true;
    ws_abort(client->websocket);
    return begin_refresh(client);
}

bool auth_get_json(auth_client *client, const char *path)
{
    if (!auth_has_session(client) || client->state != AUTH_READY || client->queued || client->attached ||
        client->api_path || client->response_ready || !path || path[0] != '/' || path[1] == '/' || strlen(path) > 4096)
        return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        if (*p < 32 || *p == 127 || *p == '#') return false;
    client->api_path = strdup(path);
    if (!client->api_path) return false;
    client->api_retried = false;
    return request(client, AUTH_API_GET, client->api_path, NULL, false);
}

bool auth_response_ready(const auth_client *client) { return client->response_ready; }
bool auth_api_uncertain(const auth_client *client) { return client->api_uncertain; }

bool auth_mutate_json(auth_client *client, const char *method, const char *path, const json_t *body)
{
    if (!method || (strcmp(method, "POST") && strcmp(method, "PUT") && strcmp(method, "DELETE")) ||
        !auth_has_session(client) || client->state != AUTH_READY || client->queued || client->attached ||
        client->api_path || client->response_ready || !path || path[0] != '/' || path[1] == '/' ||
        strlen(path) > 4096 || (strcmp(method, "DELETE") && !json_is_object(body))) return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        if (*p < 32 || *p == 127 || *p == '#') return false;
    char *copy = strdup(path);
    json_t *payload = body ? json_deep_copy(body) : NULL;
    if (!copy || (body && !payload)) { free(copy); json_decref(payload); return false; }
    client->api_path = copy; client->api_body = payload;
    client->api_method = !strcmp(method, "POST") ? "POST" : !strcmp(method, "PUT") ? "PUT" : "DELETE";
    client->api_uncertain = false;
    return request(client, AUTH_API_MUTATION, copy, payload, payload != NULL);
}

void auth_set_session_gate(auth_client *client, auth_session_gate gate, void *context)
{
    client->session_gate = gate; client->gate_context = context;
}
void auth_session_free(char **snapshot) { erase_free(snapshot); }
bool auth_session_rejected(const auth_client *client) { return client->session_rejected; }

/* Accept only one host-only cookie, scoped to this instance's own jar. Ports
 * and URL prefixes are bound separately by the secret snapshot's exact URL. */
static bool session_cookie_valid(const char *row, const char *url)
{
    if (!row || strlen(row) > 8192 || !url) return false;
    char *copy = strdup(row);
    CURLU *parsed = curl_url(); char *host = NULL, *scheme = NULL;
    bool ok = false;
    if (!copy || !parsed || curl_url_set(parsed, CURLUPART_URL, url, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK) goto done;
    char *fields[7], *p = copy;
    for (int i = 0; i < 7; ++i) {
        fields[i] = p;
        char *tab = strchr(p, '\t');
        if (i < 6) { if (!tab) goto done; *tab = 0; p = tab + 1; }
        else if (tab) goto done;
    }
    const char *domain = fields[0];
    if (!strncmp(domain, "#HttpOnly_", 10)) domain += 10;
    if (strcasecmp(domain, host) || strcmp(fields[1], "FALSE") ||
        fields[2][0] != '/' || (strcmp(fields[3], "TRUE") && strcmp(fields[3], "FALSE")) ||
        (!strcmp(scheme, "https") && strcmp(fields[3], "TRUE")) ||
        strcmp(fields[5], "Auth") || !*fields[6]) goto done;
    for (const unsigned char *c = (const unsigned char *)fields[6]; *c; ++c)
        if (*c < 33 || *c > 126) goto done;
    for (const unsigned char *c = (const unsigned char *)fields[2]; *c; ++c)
        if (*c < 33 || *c > 126) goto done;
    char *end; errno = 0;
    unsigned long long expires = strtoull(fields[4], &end, 10);
    if (errno || !*fields[4] || *end || fields[4][0] == '-' || expires <= (unsigned long long)time(NULL)) goto done;
    ok = true;
done:
    erase_free(&copy); curl_free(host); curl_free(scheme); if (parsed) curl_url_cleanup(parsed);
    return ok;
}
char *auth_session_export(auth_client *client)
{
    if (!auth_has_session(client) || auth_busy(client)) return NULL;
    struct curl_slist *cookies = NULL;
    if (curl_easy_getinfo(client->easy, CURLINFO_COOKIELIST, &cookies) != CURLE_OK) return NULL;
    const char *row = NULL; bool ambiguous = false;
    for (struct curl_slist *item = cookies; item; item = item->next)
        if (session_cookie_valid(item->data, client->base)) { if (row) ambiguous = true; row = item->data; }
    char *result = NULL;
    if (row && !ambiguous) {
        json_t *obj = json_pack("{s:i,s:s,s:s,s:s}", "version", 1, "url", client->base,
                               "user_id", client->user_id, "cookie", row);
        if (obj) {
            result = json_dumps(obj, JSON_COMPACT | JSON_SORT_KEYS);
            volatile char *secret = (volatile char *)json_string_value(json_object_get(obj, "cookie"));
            if (secret) for (size_t i = 0; i < strlen(row); ++i) secret[i] = 0;
            json_decref(obj);
        }
    }
    for (struct curl_slist *item = cookies; item; item = item->next) {
        volatile char *secret = item->data;
        for (size_t i = 0, n = strlen(item->data); i < n; ++i) secret[i] = 0;
    }
    curl_slist_free_all(cookies); return result;
}
bool auth_session_restore(auth_client *client, const char *snapshot, const char *expected_url, bool allow_local_http)
{
    if (auth_busy(client) || auth_has_session(client) || !snapshot || strlen(snapshot) > 16384) return false;
    json_error_t error; json_t *obj = json_loads(snapshot, JSON_REJECT_DUPLICATES, &error);
    const char *url = json_text(obj, "url", 2049), *id = json_text(obj, "user_id", sizeof(client->user_id));
    const char *cookie = json_text(obj, "cookie", 8193);
    char message[192], *base = url ? auth_normalize_url(url, message, sizeof(message)) : NULL;
    bool safe_fields = base && expected_url && !strcmp(base, expected_url) && !strcmp(base, url) && id &&
              json_integer_value(json_object_get(obj, "version")) == 1 && session_cookie_valid(cookie, base);
    bool policy = base && (allow_local_http || strncmp(base, "http://", 7));
    bool ok = safe_fields && policy;
    if (ok) {
        detach(client); ws_abort(client->websocket); forget_inputs(client); clear_api(client); clear_identity(client); clear_cookies(client);
        curl_free(client->base); client->base = base; base = NULL;
        client->restoring = true; client->session_rejected = client->logout_requested = client->ws_requested = false;
        client->name[0] = 0;
        snprintf(client->user_id, sizeof(client->user_id), "%s", id);
        ok = curl_easy_setopt(client->easy, CURLOPT_COOKIELIST, cookie) == CURLE_OK &&
             request(client, AUTH_SERVER, "/server", NULL, false);
    }
    if (cookie) {
        volatile char *secret = (volatile char *)cookie;
        for (size_t i = 0, n = strlen(cookie); i < n; ++i) secret[i] = 0;
    }
    curl_free(base); json_decref(obj);
    if (!ok) {
        /* HTTP policy failure is reversible with --allow-local-http; malformed
         * or expired snapshots must never be retried from persistent storage. */
        client->session_rejected = !safe_fields;
        if (safe_fields && !policy)
            input_fail(client, AUTH_INPUT_URL, "HTTP local exige --allow-local-http. Use HTTPS para acesso remoto.");
        else fail(client, "Sessão salva indisponível. Entre novamente.");
    }
    return ok;
}

void auth_cancel_read(auth_client *client)
{
    if (client->operation == AUTH_API_MUTATION && (client->queued || client->attached)) return;
    if (!client->api_path && !client->response_ready) return;
    if (client->operation == AUTH_API_GET && (client->queued || client->attached)) {
        detach(client);
        free(client->response); client->response = NULL; client->response_length = 0;
        client->overflow = false;
        if (client->authenticated && !client->logout_requested) client->state = AUTH_READY;
    }
    clear_api(client);
}

bool auth_user_summaries(auth_client *client, const json_t *ids)
{
    if (!auth_has_session(client) || client->state != AUTH_READY || client->queued || client->attached ||
        client->api_path || client->response_ready || !json_is_array(ids) ||
        !json_array_size(ids) || json_array_size(ids) > 1000) return false;
    size_t index;
    json_t *id;
    json_array_foreach(ids, index, id) {
        const char *value = json_string_value(id);
        if (!value || json_string_length(id) != 36) return false;
        for (size_t i = 0; i < 36; ++i)
            if (i == 8 || i == 13 || i == 18 || i == 23 ? value[i] != '-' :
                !((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f') ||
                  (value[i] >= 'A' && value[i] <= 'F'))) return false;
    }
    json_t *copy = json_deep_copy(ids);
    if (!copy) return false;
    json_t *body = json_pack("{s:o}", "ids", copy);
    if (!body) return false;
    client->api_path = strdup("/users/user_summary_batch");
    if (!client->api_path) { json_decref(body); return false; }
    client->api_body = body;
    client->api_retried = false;
    return request(client, AUTH_API_GET, client->api_path, body, true);
}

json_t *auth_take_response(auth_client *client, long *status, uint64_t *generation)
{
    if (!client->response_ready) return NULL;
    json_t *body = client->api_response;
    client->api_response = NULL;
    client->response_ready = false;
    if (status) *status = client->api_status;
    if (generation) *generation = client->response_generation;
    return body;
}

static bool send_transmitted(auth_client *client)
{
    if (!client->attached) return false;
    long headers = 0;
    curl_off_t uploaded = 0;
    if (curl_easy_getinfo(client->easy, CURLINFO_REQUEST_SIZE, &headers) != CURLE_OK ||
        curl_easy_getinfo(client->easy, CURLINFO_SIZE_UPLOAD_T, &uploaded) != CURLE_OK) return true;
    return headers > 0 || uploaded > 0;
}

bool auth_send_text(auth_client *client, uint64_t operation_id, const char *channel_id, const char *content)
{
    if (!auth_has_session(client) || client->state != AUTH_READY || client->queued || client->attached ||
        client->api_path || client->response_ready || client->send_ready || !operation_id || !channel_id ||
        !*channel_id || strlen(channel_id) >= sizeof(client->send_channel) || !content || !*content || strlen(content) > 8192)
        return false;
    json_t *valid_utf8 = json_string(content);
    if (!valid_utf8) return false;
    json_decref(valid_utf8);
    clear_send(client);
    client->send_receipt = (auth_send_receipt){.outcome = AUTH_SEND_REJECTED,
        .operation_id = operation_id, .generation = client->generation};
    snprintf(client->send_channel, sizeof(client->send_channel), "%s", channel_id);
    if (!request(client, AUTH_MESSAGE_SEND, "/messages", NULL, true)) return client->send_ready;
    client->mime = curl_mime_init(client->easy);
    if (!client->mime) goto error;
    curl_mimepart *channel = curl_mime_addpart(client->mime);
    curl_mimepart *text = curl_mime_addpart(client->mime);
    if (!channel || !text || curl_mime_name(channel, "channel_id") != CURLE_OK ||
        curl_mime_data(channel, channel_id, CURL_ZERO_TERMINATED) != CURLE_OK ||
        curl_mime_name(text, "content") != CURLE_OK ||
        curl_mime_data(text, content, CURL_ZERO_TERMINATED) != CURLE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_MIMEPOST, client->mime) != CURLE_OK) goto error;
    return true;
error:
    detach(client);
    client->send_ready = true;
    client->state = AUTH_READY;
    return true; /* Accepted with a local failure; no request was transmitted. */
}

bool auth_send_result_ready(const auth_client *client) { return client->send_ready; }

json_t *auth_take_send_result(auth_client *client, auth_send_receipt *receipt)
{
    if (!client->send_ready) return NULL;
    if (receipt) *receipt = client->send_receipt;
    json_t *message = client->sent_message;
    client->sent_message = NULL;
    client->send_ready = false;
    return message;
}

bool auth_cancel_send(auth_client *client)
{
    if (client->operation != AUTH_MESSAGE_SEND || (!client->queued && !client->attached)) return false;
    bool transmitted = send_transmitted(client);
    detach(client);
    client->send_receipt.transmitted = transmitted;
    client->send_receipt.cancelled = true;
    client->send_receipt.outcome = transmitted ? AUTH_SEND_UNCERTAIN : AUTH_SEND_REJECTED;
    client->send_ready = true;
    client->state = AUTH_READY;
    return true;
}

static void complete_send(auth_client *client, CURLcode result, long status, bool transmitted)
{
    client->send_receipt.status = status;
    client->send_receipt.transmitted = transmitted;
    client->send_receipt.outcome = AUTH_SEND_REJECTED;
    json_error_t error;
    json_t *body = result == CURLE_OK && status == 201 ? json_loadb(client->response ? client->response : "",
                       client->response_length, JSON_REJECT_DUPLICATES, &error) : NULL;
    const char *id = json_text(body, "id", 64);
    const char *channel = json_text(body, "channel_id", 64);
    const char *author = json_text(body, "author_id", 64);
    if (result == CURLE_OK && status == 201 && id && channel && author &&
        strcmp(channel, client->send_channel) == 0 && strcmp(author, client->user_id) == 0) {
        client->send_receipt.outcome = AUTH_SEND_CONFIRMED;
        client->sent_message = body;
        body = NULL;
    } else if ((result != CURLE_OK && transmitted) || status >= 500 ||
               (status >= 200 && status < 400)) client->send_receipt.outcome = AUTH_SEND_UNCERTAIN;
    json_decref(body);
    client->send_ready = true;
    client->state = AUTH_READY;
    if (status == 429) net_rate_defer(client->retry_after);
    if (result == CURLE_OK && status == 401) recover_session(client); /* Never resubmit the POST. */
    else if (result == CURLE_OK && status == 403 && problem_is(client, "banned"))
        fail(client, "O servidor bloqueou esta conta. Entre novamente após resolver o acesso.");
}

static void complete_api(auth_client *client, CURLcode result, long status, bool transmitted)
{
    bool mutation = client->operation == AUTH_API_MUTATION;
    if (!mutation && result == CURLE_OK && status == 401) {
        if (!client->api_retried) { client->api_retried = true; recover_session(client); }
        else fail(client, "Não foi possível confirmar a sessão após renovar. Entre novamente.");
        return;
    }
    if (result == CURLE_OK && status == 403 && problem_is(client, "banned")) {
        fail(client, "O servidor bloqueou esta conta. Entre novamente após resolver o acesso.");
        return;
    }
    if (result == CURLE_OK && status == 429) net_rate_defer(client->retry_after);
    json_error_t error;
    json_t *body = result == CURLE_OK ? json_loadb(client->response ? client->response : "",
                              client->response_length, JSON_REJECT_DUPLICATES | JSON_DECODE_ANY, &error) : NULL;
    if (status == 200 && (json_is_true(json_object_get(body, "connection_violation")) ||
        (strcmp(client->api_path, "/auth/whoami") == 0 &&
         (!json_text(body, "id", sizeof(client->user_id)) ||
          strcmp(json_text(body, "id", sizeof(client->user_id)), client->user_id) != 0)))) {
        json_decref(body);
        fail(client, "A identidade da sessão não pôde ser confirmada. Entre novamente.");
        return;
    }
    client->api_uncertain = mutation && ((result != CURLE_OK && transmitted) || status >= 500 ||
                           (status >= 200 && status < 300 && status != 204 && !json_is_object(body)));
    if (result != CURLE_OK || (!body && status == 200)) status = 0;
    free(client->api_path);
    client->api_path = NULL;
    json_decref(client->api_body);
    client->api_body = NULL;
    client->api_retried = false;
    client->api_method = NULL;
    client->api_response = body;
    client->api_status = status;
    client->response_generation = client->generation;
    client->response_ready = true;
    client->state = AUTH_READY;
    snprintf(client->message, sizeof(client->message), "%s", status == 200 ? "Consulta concluída." :
             status == 429 ? "Limite de requisições atingido. Aguarde antes de tentar novamente." :
             "Não foi possível concluir a consulta autenticada.");
    if (mutation && result == CURLE_OK && status == 401) recover_session(client);
}

static void reconnect_later(auth_client *client, uint64_t now)
{
    client->ws_retry_at = now + net_retry_ms(client->ws_failures, (unsigned)rand());
    if (client->ws_failures < 5) ++client->ws_failures;
    client->ws_validate = true;
    client->ws_validated = false;
    client->ws_stable_at = 0;
}

static void complete(auth_client *client, CURLcode result, long status)
{
    auth_state previous = client->operation;
    bool transmitted = (previous == AUTH_MESSAGE_SEND || previous == AUTH_API_MUTATION) && send_transmitted(client);
    detach(client);
    if (previous == AUTH_MESSAGE_SEND) { complete_send(client, result, status, transmitted); return; }
    if (previous == AUTH_API_GET || previous == AUTH_API_MUTATION) { complete_api(client, result, status, transmitted); return; }
    /* A failed read proves unavailability, not revocation. Preserve an already
     * verified context; refresh is deliberately excluded because its POST may
     * have rotated a cookie even when its response was lost. */
    if (client->ws_managed && *client->user_id &&
        (client->authenticated || client->recovering || client->after_refresh) && previous == AUTH_WHOAMI &&
        !client->logout_requested &&
        (result == CURLE_COULDNT_CONNECT || result == CURLE_COULDNT_RESOLVE_HOST ||
         result == CURLE_OPERATION_TIMEDOUT || result == CURLE_RECV_ERROR || result == CURLE_GOT_NOTHING ||
         result == CURLE_SEND_ERROR || result == CURLE_PARTIAL_FILE || result == CURLE_COULDNT_RESOLVE_PROXY ||
         (result == CURLE_OK && (status == 429 || status >= 500)))) {
        uint64_t now;
        if (!net_now_ms(&now)) { fail(client, "Não foi possível acompanhar a sessão."); return; }
        if (status == 429) net_rate_defer(client->retry_after);
        ws_abort(client->websocket);
        reconnect_later(client, now);
        client->validation_wait = true;
        client->state = AUTH_READY;
        snprintf(client->message, sizeof(client->message), "Rede indisponível. Reconectando…");
        return;
    }
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
        if (previous == AUTH_REGISTER) message = "Cadastro não confirmado. Tente entrar antes de enviar novamente.";
        if (status == 401 || status == 403) client->session_rejected = true;
        fail(client, message);
        return;
    }
    long expected = previous == AUTH_REGISTER ? 201 : (previous == AUTH_SERVER_PASSWORD || previous == AUTH_LOGOUT) ? 204 : 200;
    if (status != expected) {
        if (status == 401 && previous == AUTH_WHOAMI && !client->after_refresh) {
            recover_session(client);
            return;
        }
        if (status == 401 && !client->server_public &&
            (previous == AUTH_REGISTER || (previous == AUTH_LOGIN && problem_is(client, "unauthorized")))) {
            input_fail(client, AUTH_INPUT_SERVER_PASSWORD, "Autorização privada expirou. Informe a senha do servidor e confirme novamente.");
            return;
        }
        const char *message = "O servidor não aceitou a solicitação. Tente novamente.";
        if (status == 401) {
            message = previous == AUTH_SERVER_PASSWORD ? "Senha do servidor incorreta." : "Usuário ou senha inválidos, ou sessão não autorizada.";
            if (previous == AUTH_REFRESH || previous == AUTH_WHOAMI)
                message = "Sua sessão expirou ou foi revogada. Entre novamente.";
        } else if (status == 429) {
            net_rate_defer(client->retry_after);
            message = "Limite de requisições atingido. Aguarde antes de tentar novamente.";
        } else if (status == 403) message = "O servidor recusou o acesso a esta conta.";
        else if (status == 409 && previous == AUTH_REGISTER) message = "Nome de usuário já está em uso. Escolha outro.";
        else if (status == 400 && previous == AUTH_REGISTER) message = "Cadastro recusado. Confira usuário e a política de senha desta instância.";
        else if (status == 404 && (previous == AUTH_SERVER || previous == AUTH_DISCOVERY_SERVER)) message = "Instância não inicializada ou URL incompatível com o Papo.";
        else if (status == 404 && previous == AUTH_HEALTH) message = "Backend incompatível: /health não está disponível nessa URL.";
        else if (status == 502 || status == 503 || status == 504) message = "Serviço indisponível. Tente novamente ou corrija a URL.";
        else if (status >= 500 && (previous == AUTH_HEALTH || previous == AUTH_DISCOVERY_SERVER))
            message = "Serviço indisponível. Tente novamente ou corrija a URL.";
        else if (status >= 300 && status < 400) message = "O servidor redirecionou a conexão. Informe a URL final.";
        if (status == 401 || status == 403) client->session_rejected = true;
        fail(client, message);
        if (status == 401 && previous == AUTH_SERVER_PASSWORD) {
            client->server_password_error = true;
            client->input_error = AUTH_INPUT_SERVER_PASSWORD;
        } else if (status == 409 && previous == AUTH_REGISTER) client->input_error = AUTH_INPUT_USERNAME;
        else if (status == 401 && previous == AUTH_LOGIN) client->input_error = AUTH_INPUT_PASSWORD;
        return;
    }
    if (previous == AUTH_REFRESH) {
        char *current = cookie_snapshot(client);
        bool changed = current && client->refresh_cookie && strcmp(current, client->refresh_cookie) != 0;
        erase_free(&current);
        erase_free(&client->refresh_cookie);
        if (!changed) { fail(client, "O servidor não confirmou a rotação do cookie. Entre novamente."); return; }
    }
    if (client->logout_requested && previous == AUTH_REFRESH) {
        request(client, AUTH_LOGOUT, "/auth/logout", NULL, true);
        return;
    }
    if (previous == AUTH_HEALTH) {
        request(client, AUTH_DISCOVERY_SERVER, "/server", NULL, false);
        return;
    }
    if (previous == AUTH_LOGOUT) {
        logout_done(client, true);
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
            } else if (client->restoring) request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false);
            else if (json_is_true(public_value)) user_login(client);
            else if (!*client->server_password) {
                fail(client, "Esta instância é privada. Preencha a senha do servidor.");
                client->server_password_error = true;
                client->input_error = AUTH_INPUT_SERVER_PASSWORD;
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
        else {
            const char *expected_id = json_text(json_object_get(body, "user"), "id", sizeof(client->user_id));
            if (expected_id) snprintf(client->user_id, sizeof(client->user_id), "%s", expected_id);
            request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false);
        }
    } else if (previous == AUTH_REGISTER) {
        if (!json_text(body, "id", sizeof(client->user_id)) || !json_text(body, "username", sizeof(client->user)))
            fail(client, "Cadastro não confirmado pelo servidor. Tente entrar antes de enviar novamente.");
        else {
            clear_cookies(client); /* Registration never authenticates a user. */
            clear_identity(client);
            client->state = AUTH_REGISTERED;
            snprintf(client->message, sizeof(client->message), "Conta criada. Faça login com sua nova conta.");
        }
    } else if (previous == AUTH_REFRESH) {
        json_t *connection = json_object_get(body, "connection");
        const char *id = json_text(connection, "id", sizeof(client->user_id));
        const char *expires = json_text(connection, "expires_at", 65);
        if (!id || !expires || !session_time_schedule(expires, &client->refresh_at) ||
            !session_time_expiration(expires, &client->expires_at))
            fail(client, "Não foi possível confirmar a renovação da sessão. Entre novamente.");
        else request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false);
    } else if (previous == AUTH_WHOAMI) {
        const char *username = json_text(body, "username", sizeof(client->user));
        const char *id = json_text(body, "id", sizeof(client->user_id));
        if (!username || !id ||
            (*client->user_id && strcmp(id, client->user_id) != 0) ||
            json_is_true(json_object_get(body, "connection_violation"))) {
            client->session_rejected = true;
            fail(client, "Não foi possível confirmar a sessão. Entre novamente.");
        } else {
            snprintf(client->user, sizeof(client->user), "%s", username);
            snprintf(client->user_id, sizeof(client->user_id), "%s", id);
            uint64_t now;
            if (!net_now_ms(&now)) fail(client, "Não foi possível acompanhar a sessão.");
            else {
                /* Start early enough for the existing 15s REST timeout. A
                 * rate-limited or busy queue must not keep WS trusted forever. */
                client->recheck_at = now + (client->ws_managed ? 15000 : 30000);
                client->validation_deadline = now + 30000;
                if (!client->refresh_at) client->refresh_at = now + 30000;
                client->authenticated = true;
                client->validation_wait = false;
                if (client->ws_validate) {
                    client->ws_validated = true;
                    client->ws_validate = false;
                }
                client->restoring = client->session_rejected = false;
                client->recovering = client->after_refresh = false;
                client->state = AUTH_READY;
                snprintf(client->message, sizeof(client->message), "Login realizado. Você está conectado.");
                if (client->api_path) request(client, AUTH_API_GET, client->api_path, client->api_body, client->api_body != NULL);
                if (client->ws_requested && ws_get_state(client->websocket) == WS_IDLE && !client->ws_managed)
                    auth_connect_ws(client);
            }
        }
    }
    json_decref(body);
}

void auth_poll(auth_client *client)
{
    uint64_t tick;
    if (!net_now_ms(&tick)) { fail(client, "Não foi possível acompanhar a sessão."); return; }
    if (client->ws_managed && *client->user_id && client->expires_at && tick >= client->expires_at &&
        !client->logout_requested) {
        fail(client, "Sua sessão expirou sem renovação. Entre novamente."); return;
    }
    if (client->ws_managed && client->validation_deadline && tick >= client->validation_deadline &&
        ws_get_state(client->websocket) == WS_OPEN && !client->logout_requested) {
        ws_abort(client->websocket);
        reconnect_later(client, tick);
        client->validation_wait = true;
    }
    if (client->ws_managed && client->ws_active && client->ws_requested && *client->user_id &&
        !client->logout_requested && !client->queued && !client->attached &&
        client->state == AUTH_READY && tick >= client->ws_retry_at) {
        ws_state state = ws_get_state(client->websocket);
        if (state == WS_ERROR || state == WS_CLOSED || (state == WS_IDLE && (client->ws_epoch || client->ws_validate))) {
            if (client->ws_validated && auth_has_session(client)) {
                client->ws_validated = false;
                ws_connect(client->websocket, client->base);
            } else {
                client->ws_validate = true;
                if (request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false))
                    client->state = AUTH_READY;
            }
        }
    }
    if (client->state == AUTH_READY && client->authenticated && !client->queued && !client->attached) {
        uint64_t now;
        if (!net_now_ms(&now)) { fail(client, "Não foi possível acompanhar a sessão."); return; }
        if (client->validation_wait || (client->ws_managed && client->ws_requested &&
            ws_get_state(client->websocket) != WS_OPEN)) { /* Recovery validates before refresh. */ }
        else if (now >= client->refresh_at) auth_refresh(client);
        else if (now >= client->recheck_at && request(client, AUTH_WHOAMI, "/auth/whoami", NULL, false))
            client->state = AUTH_READY; /* Background REST validation keeps logout available. */
    }
    ws_state ws_before = ws_get_state(client->websocket);
    if (client->operation != AUTH_REFRESH || (!client->queued && !client->attached) ||
        (ws_before != WS_QUEUED && ws_before != WS_CONNECTING)) ws_poll(client->websocket);
    ws_state ws_after = ws_get_state(client->websocket);
    if (ws_before != ws_after && ws_after == WS_OPEN) {
        ++client->ws_epoch;
        client->ws_stable_at = tick + 30000;
        client->ws_retry_at = 0;
    }
    if (ws_after == WS_OPEN && client->ws_stable_at && tick >= client->ws_stable_at) {
        client->ws_failures = 0; client->ws_stable_at = 0;
    }
    if (client->ws_managed && *client->user_id && ws_before != ws_after &&
        (ws_after == WS_ERROR || ws_after == WS_CLOSED)) reconnect_later(client, tick);
    if (client->authenticated && ws_before != ws_after && (ws_after == WS_ERROR || ws_after == WS_CLOSED))
        client->recheck_at = 0;
    if (client->queued) {
        if (!client->gate_passed && client->session_gate &&
            (client->operation == AUTH_REFRESH || client->operation == AUTH_LOGOUT)) {
            int permit = client->session_gate(client->gate_context, client);
            if (!permit) return;
            if (permit < 0) { fail(client, "Não foi possível proteger a sessão salva. Tente novamente."); return; }
            client->gate_passed = true;
        }
        int admitted = net_rate_try_acquire();
        if (admitted == 0) return;
        if (admitted < 0 || curl_multi_add_handle(client->multi, client->easy) != CURLM_OK) {
            if (client->operation == AUTH_MESSAGE_SEND || client->operation == AUTH_API_MUTATION) complete(client, CURLE_FAILED_INIT, 0);
            else fail(client, "Não foi possível iniciar a conexão.");
            return;
        }
        client->queued = false;
        client->attached = true;
    }
    if (!client->attached) return;
    int running;
    if (curl_multi_perform(client->multi, &running) != CURLM_OK) {
        if (client->operation == AUTH_MESSAGE_SEND || client->operation == AUTH_API_MUTATION) complete(client, CURLE_RECV_ERROR, 0);
        else fail(client, "A conexão foi interrompida. Tente novamente.");
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
    if ((!auth_has_session(client) && !*client->user_id) || client->logout_requested) return false;
    client->logout_requested = true;
    client->authenticated = false;
    ++client->generation;
    clear_api(client);
    forget_inputs(client);
    ws_close(client->websocket);
    if (client->operation == AUTH_REFRESH && (client->queued || client->attached)) {
        /* Wait for a rotation already in progress so logout uses its cookie. */
        client->state = AUTH_LOGOUT;
        return true;
    }
    return request(client, AUTH_LOGOUT, "/auth/logout", NULL, true);
}

void auth_cancel(auth_client *client)
{
    if (client->logout_requested) { logout_done(client, false); return; }
    bool unknown_registration = client->operation == AUTH_REGISTER && client->attached;
    client->server_password_error = false;
    ws_abort(client->websocket);
    detach(client);
    forget_inputs(client);
    clear_cookies(client);
    clear_api(client);
    clear_identity(client);
    client->state = AUTH_IDLE;
    client->user[0] = client->name[0] = '\0';
    client->user_id[0] = '\0';
    client->ws_requested = false;
    client->input_error = AUTH_INPUT_NONE;
    snprintf(client->message, sizeof(client->message), "%s", unknown_registration ?
             "Envio cancelado. Tente entrar antes de repetir o cadastro." : "Conexão cancelada.");
}

void auth_destroy(auth_client *client)
{
    if (!client) return;
    detach(client);
    ws_destroy(client->websocket);
    forget_inputs(client);
    clear_api(client);
    clear_send(client);
    erase_free(&client->refresh_cookie);
    curl_easy_cleanup(client->easy);
    curl_multi_cleanup(client->multi);
    curl_share_cleanup(client->cookies);
    curl_free(client->base);
    free(client->response);
    free(client);
}

auth_state auth_get_state(const auth_client *client) { return client->state; }
CURL *auth_media_easy(auth_client *client, auth_media_kind kind, const char *id)
{
    if (!auth_has_session(client) || !id || strlen(id) != 36) return NULL;
    for (size_t i = 0; i < 36; ++i) {
        bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? id[i] != '-' : !((id[i] >= '0' && id[i] <= '9') ||
            (id[i] >= 'a' && id[i] <= 'f') || (id[i] >= 'A' && id[i] <= 'F'))) return NULL;
    }
    const char *formats[] = {"/attachments/%s", "/attachments/%s/thumbnail",
                            "/embeds/%s", "/embeds/%s/video"};
    if (kind < AUTH_MEDIA_ATTACHMENT || kind > AUTH_MEDIA_VIDEO) return NULL;
    char path[96];
    snprintf(path, sizeof(path), formats[kind], id);
    size_t length = strlen(client->base) + strlen(path) + 1;
    char *url = malloc(length);
    CURL *easy = curl_easy_init();
    if (!url || !easy) { free(url); curl_easy_cleanup(easy); return NULL; }
    snprintf(url, length, "%s%s", client->base, path);
#define MEDIA_SET(option, value) do { if (curl_easy_setopt(easy, option, value) != CURLE_OK) goto error; } while (0)
    MEDIA_SET(CURLOPT_URL, url);
    MEDIA_SET(CURLOPT_SHARE, client->cookies);
    MEDIA_SET(CURLOPT_COOKIEFILE, "");
    MEDIA_SET(CURLOPT_FOLLOWLOCATION, 0L);
    MEDIA_SET(CURLOPT_FRESH_CONNECT, 1L);
    MEDIA_SET(CURLOPT_FORBID_REUSE, 1L);
    MEDIA_SET(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    MEDIA_SET(CURLOPT_PROTOCOLS_STR, "http,https");
    MEDIA_SET(CURLOPT_SSL_VERIFYPEER, 1L);
    MEDIA_SET(CURLOPT_SSL_VERIFYHOST, 2L);
    MEDIA_SET(CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    MEDIA_SET(CURLOPT_TIMEOUT_MS, 120000L);
    MEDIA_SET(CURLOPT_LOW_SPEED_LIMIT, 1024L);
    MEDIA_SET(CURLOPT_LOW_SPEED_TIME, 15L);
    MEDIA_SET(CURLOPT_NOSIGNAL, 1L);
    MEDIA_SET(CURLOPT_USERAGENT, "papo-tui/0.1.0-dev");
    const char *ca_bundle = getenv("CURL_CA_BUNDLE");
    if (ca_bundle && *ca_bundle) MEDIA_SET(CURLOPT_CAINFO, ca_bundle);
    free(url);
    return easy;
error:
    free(url); curl_easy_cleanup(easy); return NULL;
#undef MEDIA_SET
}
bool auth_busy(const auth_client *client)
{
    return (client->validation_wait && client->recovering) || client->queued || client->attached ||
           client->state == AUTH_SERVER || client->state == AUTH_SERVER_PASSWORD ||
           client->state == AUTH_LOGIN || client->state == AUTH_WHOAMI || client->state == AUTH_LOGOUT ||
           client->state == AUTH_HEALTH || client->state == AUTH_DISCOVERY_SERVER ||
           client->state == AUTH_REGISTER || client->state == AUTH_REFRESH || client->state == AUTH_API_GET ||
           client->state == AUTH_MESSAGE_SEND;
}
const char *auth_message(const auth_client *client)
{
    switch (client->state) {
        case AUTH_SERVER: return "Consultando a instância…";
        case AUTH_HEALTH: return "Consultando a disponibilidade da instância…";
        case AUTH_DISCOVERY_SERVER: return "Identificando a instância…";
        case AUTH_SERVER_PASSWORD: return "Autorizando acesso ao servidor…";
        case AUTH_LOGIN: return "Entrando na sua conta…";
        case AUTH_REGISTER: return "Criando sua conta…";
        case AUTH_REFRESH: return "Renovando sua sessão…";
        case AUTH_MESSAGE_SEND: return "Enviando sua mensagem…";
        case AUTH_API_GET: return "Consultando com a sessão autenticada…";
        case AUTH_WHOAMI: return "Confirmando sua sessão…";
        case AUTH_LOGOUT: return "Saindo da conta…";
        case AUTH_READY:
            if (client->ws_managed && client->ws_requested &&
                (client->ws_validate || client->ws_validated || client->validation_wait))
                return client->ws_active ? "Login realizado. Reconectando tempo real…" : "Tempo real aguardando esta aba ficar ativa.";
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
auth_input_error auth_error_input(const auth_client *client) { return client->input_error; }
bool auth_has_session(const auth_client *client) { return client->authenticated && !client->recovering && !client->logout_requested; }
const char *auth_user_id(const auth_client *client) { return auth_has_session(client) ? client->user_id : ""; }
const char *auth_instance_url(const auth_client *client) { return client->base ? client->base : ""; }
uint64_t auth_context_generation(const auth_client *client) { return client->generation; }
bool auth_connect_ws(auth_client *client)
{
    if (!auth_has_session(client) || client->state == AUTH_REFRESH || client->after_refresh) return false;
    client->ws_requested = true;
    ws_state state = ws_get_state(client->websocket);
    if (state == WS_OPEN || state == WS_QUEUED || state == WS_CONNECTING) return true;
    if (client->ws_managed && (client->ws_epoch || client->ws_validate || state == WS_ERROR || state == WS_CLOSED)) {
        client->ws_retry_at = 0; client->ws_validate = true; client->ws_validated = false;
        return true;
    }
    return ws_connect(client->websocket, client->base);
}
void auth_realtime_active(auth_client *client, bool active)
{
    if (!client) return;
    if (!client->ws_managed && client->validation_deadline >= 15000)
        client->recheck_at = client->validation_deadline - 15000;
    client->ws_managed = true;
    client->ws_active = active;
    ws_state state = ws_get_state(client->websocket);
    if (!active && (state == WS_QUEUED || state == WS_CONNECTING)) {
        ws_abort(client->websocket); client->ws_validate = true;
    }
}
uint64_t auth_ws_epoch(const auth_client *client) { return client->ws_epoch; }
bool auth_ws_reconnecting(const auth_client *client)
{
    return client->ws_managed && client->ws_requested &&
           (client->ws_validate || client->ws_validated || client->validation_wait ||
            ws_get_state(client->websocket) != WS_OPEN);
}
ws_state auth_ws_state(const auth_client *client) { return ws_get_state(client->websocket); }
const char *auth_ws_message(const auth_client *client) { return ws_message(client->websocket); }
long auth_ws_http_status(const auth_client *client) { return ws_http_status(client->websocket); }
json_t *auth_next_event(auth_client *client)
{
    return auth_has_session(client) ? ws_pop(client->websocket) : NULL;
}
bool auth_send_event(auth_client *client, const json_t *event)
{
    return auth_has_session(client) && ws_send(client->websocket, event);
}
