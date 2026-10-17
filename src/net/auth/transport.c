#include "net/auth/private.h"
#include "net/tls.h"

void auth_internal_detach(auth_client *client)
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
    auth_internal_erase_free(&client->payload);
    curl_slist_free_all(client->headers);
    client->headers = NULL;
    curl_easy_setopt(client->easy, CURLOPT_HTTPHEADER, NULL);
}

size_t auth_internal_receive(char *data, size_t size, size_t count, void *context)
{
    auth_client *client = context;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t bytes = size * count;
    if (bytes > client->response_limit - client->response_length) {
        client->overflow = true;
        return 0;
    }
    char *next = realloc(client->response, client->response_length + bytes + 1);
    if (!next)
        return 0;
    client->response = next;
    memcpy(next + client->response_length, data, bytes);
    client->response_length += bytes;
    next[client->response_length] = '\0';
    return bytes;
}

size_t auth_internal_receive_header(char *data, size_t size, size_t count, void *context)
{
    auth_client *client = context;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t bytes = size * count;
    if (bytes >= 5 && strncasecmp(data, "HTTP/", 5) == 0)
        client->retry_after = PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    if (bytes > 12 && strncasecmp(data, "Retry-After:", 12) == 0)
        client->retry_after = net_rate_retry_after(data + 12, bytes - 12);
    return bytes;
}

bool auth_internal_request(auth_client *client, auth_state state, const char *path, json_t *body,
                           bool post)
{
    auth_internal_detach(client);
    /* Restore ordinary REST deadlines after a potentially long file upload. */
    if (curl_easy_setopt(client->easy, CURLOPT_TIMEOUT_MS, 15000L) != CURLE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_LOW_SPEED_LIMIT, 0L) != CURLE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_LOW_SPEED_TIME, 0L) != CURLE_OK)
        goto error;
    free(client->response);
    client->response = NULL;
    client->response_length = 0;
    client->response_limit =
        /* A page can contain 25 base64-encoded emojis of up to 2 MiB each. */
        state == AUTH_API_GET && !strncmp(path, "/emojis?", 8)           ? 72U * 1024U * 1024U
        : state == AUTH_API_GET && !strcmp(path, "/users/profile_batch") ? 16U * 1024U * 1024U
                                                                         : RESPONSE_LIMIT;
    client->overflow = false;
    client->retry_after = PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    /* A fresh HTTP/1.1 connection avoids libcurl's stale-connection/stream
     * retries for this non-idempotent mutation. Other REST calls keep defaults. */
    bool mutation = state == AUTH_MESSAGE_SEND || state == AUTH_API_MUTATION;
    if (curl_easy_setopt(client->easy, CURLOPT_FRESH_CONNECT, mutation ? 1L : 0L) != CURLE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_FORBID_REUSE, mutation ? 1L : 0L) != CURLE_OK ||
        curl_easy_setopt(client->easy, CURLOPT_HTTP_VERSION,
                         (long)(mutation ? CURL_HTTP_VERSION_1_1 : CURL_HTTP_VERSION_NONE)) !=
            CURLE_OK)
        goto error;
    size_t length = strlen(client->base) + strlen(path) + 1;
    char *url = malloc(length);
    if (!url)
        goto error;
    snprintf(url, length, "%s%s", client->base, path);
    CURLcode result = curl_easy_setopt(client->easy, CURLOPT_URL, url);
    free(url);
    if (result != CURLE_OK)
        goto error;
    client->headers = curl_slist_append(NULL, "Accept: application/json");
    if (!client->headers)
        goto error;
    if (body) {
        client->payload = json_dumps(body, JSON_COMPACT);
        if (!client->payload)
            goto error;
        struct curl_slist *headers =
            curl_slist_append(client->headers, "Content-Type: application/json");
        if (!headers)
            goto error;
        client->headers = headers;
    }
    if (curl_easy_setopt(client->easy, CURLOPT_HTTPHEADER, client->headers) != CURLE_OK)
        goto error;
    if (post) {
        if (curl_easy_setopt(client->easy, CURLOPT_POSTFIELDS,
                             client->payload ? client->payload : "") != CURLE_OK ||
            curl_easy_setopt(client->easy, CURLOPT_POSTFIELDSIZE,
                             (long)(client->payload ? strlen(client->payload) : 0)) != CURLE_OK)
            goto error;
    } else {
        if (curl_easy_setopt(client->easy, CURLOPT_HTTPGET, 1L) != CURLE_OK)
            goto error;
    }
    if (state == AUTH_API_MUTATION &&
        curl_easy_setopt(client->easy, CURLOPT_CUSTOMREQUEST, client->api_method) != CURLE_OK)
        goto error;
    client->permission_serial = permissions_event_serial(client->permissions);
    client->queued = true;
    client->state = state;
    client->operation = state;
    client->gate_passed = false;
    return true;
error:
    if (state == AUTH_API_MUTATION) {
        auth_internal_detach(client);
        auth_internal_clear_api(client);
        client->api_status = 0;
        client->response_generation = client->generation;
        client->response_ready = true;
        client->state = AUTH_READY;
        snprintf(client->message, sizeof(client->message), "Não foi possível preparar a operação.");
        return true; /* Accepted with an unsent local rejection. */
    }
    if (state == AUTH_MESSAGE_SEND) {
        auth_internal_detach(client);
        client->send_receipt.outcome = AUTH_SEND_REJECTED;
        client->send_ready = true;
        client->state = AUTH_READY;
        return false;
    }
    auth_internal_fail(client, "Não foi possível preparar a conexão.");
    return false;
}

static CURL *media_easy(auth_client *client, const char *url, bool cookies)
{
    CURL *easy = curl_easy_init();
    if (!easy)
        return NULL;
#define MEDIA_SET(option, value)                                                                   \
    do {                                                                                           \
        if (curl_easy_setopt(easy, option, value) != CURLE_OK)                                     \
            goto error;                                                                            \
    } while (0)
    MEDIA_SET(CURLOPT_URL, url);
    if (cookies) {
        MEDIA_SET(CURLOPT_SHARE, client->cookies);
        MEDIA_SET(CURLOPT_COOKIEFILE, "");
    }
    MEDIA_SET(CURLOPT_FOLLOWLOCATION, 0L);
    MEDIA_SET(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    MEDIA_SET(CURLOPT_PROTOCOLS_STR, "http,https");
    MEDIA_SET(CURLOPT_SSL_VERIFYPEER, 1L);
    MEDIA_SET(CURLOPT_SSL_VERIFYHOST, 2L);
    MEDIA_SET(CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    MEDIA_SET(CURLOPT_TIMEOUT_MS, 120000L);
    MEDIA_SET(CURLOPT_LOW_SPEED_LIMIT, 1024L);
    MEDIA_SET(CURLOPT_LOW_SPEED_TIME, 15L);
    MEDIA_SET(CURLOPT_NOSIGNAL, 1L);
    MEDIA_SET(CURLOPT_USERAGENT, "papo-tui/" PAPO_VERSION);
    const char *ca_bundle = net_ca_bundle();
    if (ca_bundle && *ca_bundle)
        MEDIA_SET(CURLOPT_CAINFO, ca_bundle);
    return easy;
error:
    curl_easy_cleanup(easy);
    return NULL;
#undef MEDIA_SET
}

CURL *auth_media_easy(auth_client *client, auth_media_kind kind, const char *id)
{
    if (!auth_has_session(client) || !id || strlen(id) != 36)
        return NULL;
    for (size_t i = 0; i < 36; ++i) {
        bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? id[i] != '-'
                 : !((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f') ||
                     (id[i] >= 'A' && id[i] <= 'F')))
            return NULL;
    }
    const char *formats[] = {"/attachments/%s", "/attachments/%s/thumbnail", "/embeds/%s",
                             "/embeds/%s/video"};
    if (kind < AUTH_MEDIA_ATTACHMENT || kind > AUTH_MEDIA_VIDEO)
        return NULL;
    char path[96];
    snprintf(path, sizeof(path), formats[kind], id);
    size_t length = strlen(client->base) + strlen(path) + 1;
    char *url = malloc(length);
    if (!url)
        return NULL;
    snprintf(url, length, "%s%s", client->base, path);
    CURL *easy = media_easy(client, url, true);
    free(url);
    return easy;
}

CURL *auth_avatar_easy(auth_client *client, const char *reference)
{
    if (!auth_has_session(client) || !reference || !*reference || strlen(reference) > 2048)
        return NULL;
    for (const unsigned char *p = (const unsigned char *)reference; *p; ++p)
        if (*p <= 0x20 || *p == 0x7f)
            return NULL;
    char *relative = NULL;
    if (*reference == '/') {
        if (strlen(reference) != 53 || strncmp(reference, "/webhooks/", 10) ||
            strcmp(reference + 46, "/avatar"))
            return NULL;
        for (size_t i = 0; i < 36; ++i) {
            char c = reference[10 + i];
            bool dash = i == 8 || i == 13 || i == 18 || i == 23;
            if (dash
                    ? c != '-'
                    : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return NULL;
        }
        size_t length = strlen(client->base) + strlen(reference) + 1;
        relative = malloc(length);
        if (!relative)
            return NULL;
        snprintf(relative, length, "%s%s", client->base, reference);
        reference = relative;
    }
    CURLU *parsed = curl_url();
    char *scheme = NULL, *part = NULL;
    bool valid = parsed && curl_url_set(parsed, CURLUPART_URL, reference, 0) == CURLUE_OK &&
                 curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK;
    if (valid) {
        size_t base_length = strlen(client->base);
        bool local_http = !strcmp(scheme, "http") &&
                          !strncmp(reference, client->base, base_length) &&
                          reference[base_length] == '/';
        valid = !strcmp(scheme, "https") || local_http;
        const CURLUPart forbidden[] = {CURLUPART_USER, CURLUPART_PASSWORD, CURLUPART_FRAGMENT};
        for (size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
            if (curl_url_get(parsed, forbidden[i], &part, 0) == CURLUE_OK)
                valid = false;
            curl_free(part);
            part = NULL;
        }
    }
    CURL *easy = valid ? media_easy(client, reference, false) : NULL;
    curl_free(scheme);
    curl_url_cleanup(parsed);
    free(relative);
    return easy;
}
