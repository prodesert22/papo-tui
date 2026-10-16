#include "net/auth/private.h"
#include "platform/unicode.h"

const char *auth_internal_json_text(json_t *object, const char *key, size_t capacity)
{
    json_t *value = json_object_get(object, key);
    const char *text = json_string_value(value);
    return text && *text && json_string_length(value) == strlen(text) && strlen(text) < capacity
               ? text
               : NULL;
}

bool auth_internal_problem_is(auth_client *client, const char *kind)
{
    json_error_t error;
    json_t *body = json_loadb(client->response ? client->response : "", client->response_length,
                              JSON_REJECT_DUPLICATES, &error);
    const char *type = auth_internal_json_text(body, "type", 2049);
    const char *last = type ? strrchr(type, '/') : NULL;
    bool matches = last && strcmp(last + 1, kind) == 0;
    json_decref(body);
    return matches;
}

static bool registration_error(auth_input_error *field, char *message, size_t capacity,
                               auth_input_error error, const char *reason)
{
    if (field)
        *field = error;
    if (message && capacity)
        snprintf(message, capacity, "%s", reason);
    return false;
}

bool auth_validate_registration(const char *username, const char *password, auth_input_error *field,
                                char *message, size_t capacity)
{
    if (field)
        *field = AUTH_INPUT_NONE;
    if (message && capacity)
        *message = '\0';
    if (!username || !*username)
        return registration_error(field, message, capacity, AUTH_INPUT_USERNAME,
                                  "Preencha o usuário.");
    if (!password || !*password)
        return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD,
                                  "Preencha a senha.");

    size_t characters = 0;
    while (*username) {
        uint32_t character;
        size_t bytes = papo_utf8_next(username, &character);
        if (!bytes || bytes == (size_t)-1 || bytes == (size_t)-2 || !g_unichar_isprint(character))
            return registration_error(field, message, capacity, AUTH_INPUT_USERNAME,
                                      "Informe um usuário válido, sem controles de terminal.");
        ++characters;
        username += bytes;
    }
    if (characters < 3)
        return registration_error(field, message, capacity, AUTH_INPUT_USERNAME,
                                  "O usuário precisa ter pelo menos 3 caracteres.");
    characters = 0;
    bool upper = false, special = false;
    while (*password) {
        uint32_t character;
        size_t bytes = papo_utf8_next(password, &character);
        if (!bytes || bytes == (size_t)-1 || bytes == (size_t)-2 || !g_unichar_isprint(character))
            return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD,
                                      "Informe uma senha válida, sem controles de terminal.");
        upper |= g_unichar_isupper(character) != 0;
        special |= g_unichar_isalnum(character) == 0;
        ++characters;
        password += bytes;
    }
    if (characters < 8)
        return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD,
                                  "A senha precisa ter pelo menos 8 caracteres.");
    if (!upper)
        return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD,
                                  "A senha precisa de uma letra maiúscula.");
    if (!special)
        return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD,
                                  "A senha precisa de um caractere especial.");
    return true;
}

bool auth_internal_registration_valid(auth_client *client, const char *username,
                                      const char *password)
{
    char message[192];
    auth_input_error field;
    if (auth_validate_registration(username, password, &field, message, sizeof(message)))
        return true;
    return auth_internal_input_fail(client, field, message);
}

char *auth_normalize_url(const char *url, char *error, size_t error_size)
{
    CURLU *parsed = curl_url();
    char *scheme = NULL, *host = NULL, *path = NULL, *part = NULL, *result = NULL;
    const char *reason = "Informe uma URL válida com https:// ou http://.";
    if (!parsed || !url || !*url || strlen(url) > 2048 ||
        curl_url_set(parsed, CURLUPART_URL, url, 0) != CURLUE_OK)
        goto done;
    if (curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK)
        goto done;
    if (strcmp(scheme, "https") != 0 && strcmp(scheme, "http") != 0)
        goto done;
    reason = "HTTP é permitido apenas em localhost. Use HTTPS para outros servidores.";
    if (strcmp(scheme, "http") == 0 && strcmp(host, "localhost") != 0 &&
        strcmp(host, "127.0.0.1") != 0 && strcmp(host, "[::1]") != 0)
        goto done;
    reason = "Use uma URL sem credenciais, query ou fragmento.";
    const CURLUPart forbidden[] = {CURLUPART_USER, CURLUPART_PASSWORD, CURLUPART_QUERY,
                                   CURLUPART_FRAGMENT};
    for (size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
        if (curl_url_get(parsed, forbidden[i], &part, CURLU_GET_EMPTY) == CURLUE_OK) {
            curl_free(part);
            part = NULL;
            goto done;
        }
    }
    if (curl_url_get(parsed, CURLUPART_PATH, &path, 0) != CURLUE_OK)
        goto done;
    /* Remove trailing slashes, preserving any reverse-proxy path prefix. */
    size_t length = strlen(path);
    while (length && path[length - 1] == '/')
        path[--length] = '\0';
    if (curl_url_set(parsed, CURLUPART_PATH, path, 0) != CURLUE_OK)
        goto done;
    if (curl_url_get(parsed, CURLUPART_URL, &result, 0) != CURLUE_OK)
        goto done;
    length = strlen(result);
    while (length && result[length - 1] == '/')
        result[--length] = '\0';
done:
    if (!result && error && error_size)
        snprintf(error, error_size, "%s", reason);
    curl_free(scheme);
    curl_free(host);
    curl_free(path);
    curl_free(part);
    curl_url_cleanup(parsed);
    return result;
}

void auth_internal_copy_server_name(auth_client *client, const char *name)
{
    if (!name || !*name)
        name = "Nome não informado";
    size_t length = strlen(name);
    if (length >= sizeof(client->name))
        length = sizeof(client->name) - 1;
    while (length && ((unsigned char)name[length] & 0xc0) == 0x80)
        --length;
    memcpy(client->name, name, length);
    client->name[length] = '\0';
}

bool auth_internal_session_cookie_valid(const char *row, const char *url)
{
    if (!row || strlen(row) > 8192 || !url)
        return false;
    char *copy = strdup(row);
    CURLU *parsed = curl_url();
    char *host = NULL, *scheme = NULL;
    bool ok = false;
    if (!copy || !parsed || curl_url_set(parsed, CURLUPART_URL, url, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK)
        goto done;
    char *fields[7], *p = copy;
    for (int i = 0; i < 7; ++i) {
        fields[i] = p;
        char *tab = strchr(p, '\t');
        if (i < 6) {
            if (!tab)
                goto done;
            *tab = 0;
            p = tab + 1;
        } else if (tab)
            goto done;
    }
    const char *domain = fields[0];
    if (!strncmp(domain, "#HttpOnly_", 10))
        domain += 10;
    if (strcasecmp(domain, host) || strcmp(fields[1], "FALSE") || fields[2][0] != '/' ||
        (strcmp(fields[3], "TRUE") && strcmp(fields[3], "FALSE")) ||
        (!strcmp(scheme, "https") && strcmp(fields[3], "TRUE")) || strcmp(fields[5], "Auth") ||
        !*fields[6])
        goto done;
    for (const unsigned char *c = (const unsigned char *)fields[6]; *c; ++c)
        if (*c < 33 || *c > 126)
            goto done;
    for (const unsigned char *c = (const unsigned char *)fields[2]; *c; ++c)
        if (*c < 33 || *c > 126)
            goto done;
    char *end;
    errno = 0;
    unsigned long long expires = strtoull(fields[4], &end, 10);
    if (errno || !*fields[4] || *end || fields[4][0] == '-' ||
        expires <= (unsigned long long)time(NULL))
        goto done;
    ok = true;
done:
    auth_internal_erase_free(&copy);
    curl_free(host);
    curl_free(scheme);
    if (parsed)
        curl_url_cleanup(parsed);
    return ok;
}
