#include "model/text/translation.h"
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

static bool password_error(char *message, size_t capacity, const char *reason)
{
    if (message && capacity)
        snprintf(message, capacity, "%s", reason);
    return false;
}

bool auth_validate_password(const char *password, char *message, size_t capacity)
{
    if (message && capacity)
        *message = '\0';
    if (!password || !*password)
        return password_error(message, capacity, _("Preencha a senha."));
    size_t characters = 0;
    bool upper = false, special = false;
    while (*password) {
        uint32_t character;
        size_t bytes = papo_utf8_next(password, &character);
        if (!bytes || bytes == (size_t)-1 || bytes == (size_t)-2 || !g_unichar_isprint(character))
            return password_error(message, capacity,
                                  _("Informe uma senha válida, sem controles de terminal."));
        upper |= g_unichar_isupper(character) != 0;
        special |= g_unichar_isalnum(character) == 0;
        ++characters;
        password += bytes;
    }
    if (characters < 8)
        return password_error(message, capacity, _("A senha precisa ter pelo menos 8 caracteres."));
    if (!upper)
        return password_error(message, capacity, _("A senha precisa de uma letra maiúscula."));
    if (!special)
        return password_error(message, capacity, _("A senha precisa de um caractere especial."));
    return true;
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
                                  _("Preencha o usuário."));
    if (!password || !*password)
        return registration_error(field, message, capacity, AUTH_INPUT_PASSWORD,
                                  _("Preencha a senha."));

    size_t characters = 0;
    while (*username) {
        uint32_t character;
        size_t bytes = papo_utf8_next(username, &character);
        if (!bytes || bytes == (size_t)-1 || bytes == (size_t)-2 || !g_unichar_isprint(character))
            return registration_error(field, message, capacity, AUTH_INPUT_USERNAME,
                                      _("Informe um usuário válido, sem controles de terminal."));
        ++characters;
        username += bytes;
    }
    if (characters < 3)
        return registration_error(field, message, capacity, AUTH_INPUT_USERNAME,
                                  _("O usuário precisa ter pelo menos 3 caracteres."));
    if (!auth_validate_password(password, message, capacity)) {
        if (field)
            *field = AUTH_INPUT_PASSWORD;
        return false;
    }
    return true;
}

/* Base64url, as the server encodes the 32 random bytes of the token. */
static bool token_valid(const char *token, size_t length)
{
    if (!length || length > 256)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!g_ascii_isalnum(token[i]) && token[i] != '-' && token[i] != '_')
            return false;
    return true;
}

bool auth_reset_token(const char *input, char *token, size_t token_size, char *host,
                      size_t host_size)
{
    if (token && token_size)
        *token = '\0';
    if (host && host_size)
        *host = '\0';
    if (!input || !token || token_size < 2 || !host || !host_size)
        return false;
    while (*input == ' ' || *input == '\t')
        ++input;
    size_t length = strlen(input);
    while (length && (input[length - 1] == ' ' || input[length - 1] == '\t'))
        --length;
    if (!length || length > 2048)
        return false;
    static const char marker[] = "/passwordchange/";
    char *copy = g_strndup(input, length);
    const char *value = copy;
    size_t value_length = length;
    CURLU *parsed = NULL;
    char *path = NULL, *name = NULL, *part = NULL;
    bool ok = false;
    if (strstr(copy, "://") || strstr(copy, marker)) {
        /* A link copied without its scheme still names its host. */
        char *link = strstr(copy, "://") ? g_strdup(copy) : g_strconcat("https://", copy, NULL);
        parsed = curl_url();
        char *scheme = NULL;
        bool parsed_ok = parsed && curl_url_set(parsed, CURLUPART_URL, link, 0) == CURLUE_OK &&
                         curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK &&
                         (!strcmp(scheme, "https") || !strcmp(scheme, "http")) &&
                         curl_url_get(parsed, CURLUPART_HOST, &name, 0) == CURLUE_OK &&
                         curl_url_get(parsed, CURLUPART_PATH, &path, 0) == CURLUE_OK;
        const CURLUPart forbidden[] = {CURLUPART_USER, CURLUPART_PASSWORD, CURLUPART_QUERY,
                                       CURLUPART_FRAGMENT};
        for (size_t i = 0; parsed_ok && i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
            if (curl_url_get(parsed, forbidden[i], &part, CURLU_GET_EMPTY) == CURLUE_OK)
                parsed_ok = false;
            curl_free(part);
            part = NULL;
        }
        curl_free(scheme);
        g_free(link);
        const char *found = parsed_ok ? strstr(path, marker) : NULL;
        if (!found || strlen(name) >= host_size)
            goto done;
        value = found + strlen(marker);
        value_length = strlen(value);
        while (value_length && value[value_length - 1] == '/')
            --value_length;
        snprintf(host, host_size, "%s", name);
    }
    if (!token_valid(value, value_length) || value_length >= token_size)
        goto done;
    memcpy(token, value, value_length);
    token[value_length] = '\0';
    ok = true;
done:
    if (!ok)
        *host = '\0';
    curl_free(path);
    curl_free(name);
    if (parsed)
        curl_url_cleanup(parsed);
    volatile char *secret = copy;
    for (size_t i = 0; i < length; ++i)
        secret[i] = 0;
    g_free(copy);
    return ok;
}

void auth_internal_problem_detail(auth_client *client, char *out, size_t capacity)
{
    if (!out || !capacity)
        return;
    *out = '\0';
    json_error_t error;
    json_t *body = json_loadb(client->response ? client->response : "", client->response_length,
                              JSON_REJECT_DUPLICATES, &error);
    const char *detail = auth_internal_json_text(body, "detail", 1024);
    if (!detail)
        detail = auth_internal_json_text(body, "title", 1024);
    size_t length = 0;
    /* Remote text never brings terminal controls into a notice. */
    for (const char *at = detail; at && *at && g_utf8_validate(at, -1, NULL);
         at = g_utf8_next_char(at)) {
        gunichar c = g_utf8_get_char(at);
        char bytes[6];
        int size = g_unichar_iscntrl(c) || g_unichar_type(c) == G_UNICODE_FORMAT
                       ? (bytes[0] = ' ', 1)
                       : g_unichar_to_utf8(c, bytes);
        if (length + (size_t)size >= capacity)
            break;
        memcpy(out + length, bytes, (size_t)size);
        length += (size_t)size;
    }
    out[length] = '\0';
    json_decref(body);
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

/* The address as typed or pasted, without surrounding blanks. A scheme
 * written loosely ("https//", "https:/", "HTTP:") is the one the user meant,
 * so it is repaired rather than prefixed; hosts such as httpbin.org stay
 * untouched. Owned by the caller. */
static char *typed_url(const char *url)
{
    while (*url == ' ' || *url == '\t' || *url == '\r' || *url == '\n')
        ++url;
    size_t length = strlen(url);
    while (length && strchr(" \t\r\n", url[length - 1]))
        --length;
    static const char *const schemes[] = {"https", "http"};
    for (size_t i = 0; i < sizeof(schemes) / sizeof(schemes[0]); ++i) {
        size_t name = strlen(schemes[i]), rest = name;
        if (length <= name || g_ascii_strncasecmp(url, schemes[i], name) ||
            (url[name] != ':' && url[name] != '/'))
            continue;
        if (url[rest] == ':')
            ++rest;
        while (rest < length && url[rest] == '/')
            ++rest;
        return g_strdup_printf("%s://%.*s", schemes[i], (int)(length - rest), url + rest);
    }
    return g_strndup(url, length);
}

char *auth_normalize_url(const char *url, char *error, size_t error_size)
{
    CURLU *parsed = curl_url();
    char *scheme = NULL, *host = NULL, *path = NULL, *part = NULL, *result = NULL;
    char *typed = url ? typed_url(url) : NULL;
    const char *reason = _("Informe uma URL válida, como chat.exemplo.com.");
    /* Only an address typed without any scheme gets HTTPS. */
    if (!parsed || !typed || !*typed || strlen(typed) > 2048 ||
        curl_url_set(parsed, CURLUPART_URL, typed, CURLU_DEFAULT_SCHEME) != CURLUE_OK)
        goto done;
    if (curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK)
        goto done;
    if (strcmp(scheme, "https") != 0 && strcmp(scheme, "http") != 0)
        goto done;
    reason = _("HTTP é permitido apenas em localhost. Use HTTPS para outros servidores.");
    if (strcmp(scheme, "http") == 0 && strcmp(host, "localhost") != 0 &&
        strcmp(host, "127.0.0.1") != 0 && strcmp(host, "[::1]") != 0)
        goto done;
    reason = _("Use uma URL sem credenciais, query ou fragmento.");
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
    g_free(typed);
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
        name = _("Nome não informado");
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
