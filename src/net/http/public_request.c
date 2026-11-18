#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "net/http/public_request.h"
#include "net/http/easy.h"
#include "net/rate_limit.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PUBLIC_URL_LIMIT 2048U
#define PUBLIC_TIMEOUT_MS 30000L
/* A download may take long, but not stall. */
#define PUBLIC_SLOW_BYTES 1024L
#define PUBLIC_SLOW_SECONDS 30L

struct public_request {
    CURLM *multi;
    CURL *easy;
    struct curl_slist *headers;
    char *body;
    size_t bytes, capacity, limit;
    public_state state;
    public_error error;
    long status;
    unsigned retry_after, hops;
    bool attached, overflow, unwritten;
    /* Download: the destination, the redirect being considered and its policy. */
    FILE *file;
    char *path, *location;
    public_redirect allowed;
    void *context;
};

static void release_transfer(public_request *request)
{
    if (request->attached)
        curl_multi_remove_handle(request->multi, request->easy);
    request->attached = false;
    curl_easy_cleanup(request->easy);
    request->easy = NULL;
    curl_slist_free_all(request->headers);
    request->headers = NULL;
}
void public_request_cancel(public_request *request)
{
    if (!request)
        return;
    release_transfer(request);
    if (request->file)
        fclose(request->file);
    request->file = NULL;
    free(request->body);
    free(request->path);
    free(request->location);
    request->body = request->path = request->location = NULL;
    request->bytes = request->capacity = 0;
    request->state = PUBLIC_IDLE;
    request->error = PUBLIC_ERROR_NONE;
    request->status = 0;
    request->retry_after = request->hops = 0;
    request->overflow = request->unwritten = false;
    request->allowed = NULL;
    request->context = NULL;
}
public_request *public_request_create(void)
{
    public_request *request = calloc(1, sizeof(*request));
    if (!request)
        return NULL;
    request->multi = curl_multi_init();
    if (!request->multi) {
        free(request);
        return NULL;
    }
    return request;
}
void public_request_destroy(public_request *request)
{
    if (!request)
        return;
    public_request_cancel(request);
    curl_multi_cleanup(request->multi);
    free(request);
}
static size_t receive(char *data, size_t size, size_t count, void *context)
{
    public_request *request = context;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t bytes = size * count;
    if (bytes > request->limit - request->bytes) {
        request->overflow = true;
        return 0;
    }
    if (request->file) {
        if (fwrite(data, 1, bytes, request->file) != bytes) {
            request->unwritten = true;
            return 0;
        }
        request->bytes += bytes;
        return bytes;
    }
    if (request->bytes + bytes + 1 > request->capacity) {
        size_t capacity = request->capacity ? request->capacity : 16384;
        while (capacity < request->bytes + bytes + 1)
            capacity *= 2;
        if (capacity > request->limit + 1)
            capacity = request->limit + 1;
        char *body = realloc(request->body, capacity);
        if (!body)
            return 0;
        request->body = body;
        request->capacity = capacity;
    }
    memcpy(request->body + request->bytes, data, bytes);
    request->bytes += bytes;
    request->body[request->bytes] = 0;
    return bytes;
}
static size_t header(char *data, size_t size, size_t count, void *context)
{
    public_request *request = context;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t bytes = size * count;
    /* Only the final response counts: each status line starts over. */
    if (bytes >= 5 && !strncasecmp(data, "HTTP/", 5)) {
        request->retry_after = 0;
        free(request->location);
        request->location = NULL;
    }
    if (bytes > 12 && !strncasecmp(data, "Retry-After:", 12))
        request->retry_after = net_rate_retry_after(data + 12, bytes - 12);
    if (request->file && bytes > 9 && !strncasecmp(data, "Location:", 9)) {
        const char *value = data + 9, *end = data + bytes;
        while (value < end && (*value == ' ' || *value == '\t'))
            ++value;
        while (end > value && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' '))
            --end;
        free(request->location);
        request->location = NULL;
        size_t length = (size_t)(end - value);
        if (length && length <= PUBLIC_URL_LIMIT && (request->location = malloc(length + 1))) {
            memcpy(request->location, value, length);
            request->location[length] = 0;
        }
    }
    return bytes;
}
static bool url_allowed(const char *url)
{
    if (!url || strncmp(url, "https://", 8) || !url[8])
        return false;
    size_t length = 0;
    for (const unsigned char *c = (const unsigned char *)url; *c; ++c, ++length)
        if (*c <= ' ' || *c == 0x7f || length >= PUBLIC_URL_LIMIT)
            return false;
    return true;
}
/* A new handle for url, with the options every request of this kind shares. */
static bool prepare(public_request *request, const char *url)
{
    request->easy = net_easy_base(url);
    return request->easy &&
           curl_easy_setopt(request->easy, CURLOPT_PROTOCOLS_STR, "https") == CURLE_OK &&
           curl_easy_setopt(request->easy, CURLOPT_HTTPHEADER, request->headers) == CURLE_OK &&
           curl_easy_setopt(request->easy, CURLOPT_WRITEFUNCTION, receive) == CURLE_OK &&
           curl_easy_setopt(request->easy, CURLOPT_WRITEDATA, request) == CURLE_OK &&
           curl_easy_setopt(request->easy, CURLOPT_HEADERFUNCTION, header) == CURLE_OK &&
           curl_easy_setopt(request->easy, CURLOPT_HEADERDATA, request) == CURLE_OK &&
           curl_easy_setopt(request->easy, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)request->limit) ==
               CURLE_OK &&
           (request->file ? curl_easy_setopt(request->easy, CURLOPT_LOW_SPEED_LIMIT,
                                             PUBLIC_SLOW_BYTES) == CURLE_OK &&
                                curl_easy_setopt(request->easy, CURLOPT_LOW_SPEED_TIME,
                                                 PUBLIC_SLOW_SECONDS) == CURLE_OK
                          : curl_easy_setopt(request->easy, CURLOPT_TIMEOUT_MS,
                                             PUBLIC_TIMEOUT_MS) == CURLE_OK);
}
bool public_request_get(public_request *request, const char *url, const char *const *headers,
                        size_t header_count, size_t limit)
{
    if (!request)
        return false;
    public_request_cancel(request);
    if (!url_allowed(url) || !limit || limit >= SIZE_MAX / 2)
        return false;
    request->limit = limit;
    for (size_t i = 0; i < header_count; ++i) {
        struct curl_slist *next = curl_slist_append(request->headers, headers[i]);
        if (!next)
            goto error;
        request->headers = next;
    }
    if (!prepare(request, url))
        goto error;
    request->state = PUBLIC_WAITING;
    return true;
error:
    public_request_cancel(request);
    return false;
}
bool public_request_download(public_request *request, const char *url, const char *path,
                             size_t limit, public_redirect allowed, void *context)
{
    if (!request)
        return false;
    public_request_cancel(request);
    if (!url_allowed(url) || !path || !*path || !limit || limit >= SIZE_MAX / 2)
        return false;
    request->limit = limit;
    request->allowed = allowed;
    request->context = context;
    request->path = strdup(path);
    request->file = request->path ? fopen(request->path, "wb") : NULL;
    if (!request->file || !prepare(request, url)) {
        public_request_cancel(request);
        return false;
    }
    request->state = PUBLIC_WAITING;
    return true;
}
static void fail(public_request *request, public_error error)
{
    release_transfer(request);
    if (request->file)
        fclose(request->file);
    request->file = NULL;
    free(request->body);
    request->body = NULL;
    request->bytes = request->capacity = 0;
    request->status = 0;
    request->error = error;
    request->state = PUBLIC_FAILED;
}
/* Sends the request again at the address the server named, as a request of
 * its own: the file starts over and the budget is asked again. */
static void redirect(public_request *request)
{
    char *location = request->location;
    request->location = NULL;
    bool follow = location && request->hops < PUBLIC_REDIRECT_LIMIT && url_allowed(location) &&
                  request->allowed && request->allowed(location, request->context);
    release_transfer(request);
    if (follow) {
        ++request->hops;
        fclose(request->file);
        request->file = fopen(request->path, "wb");
        request->bytes = 0;
        request->status = 0;
        request->overflow = request->unwritten = false;
        follow = request->file && prepare(request, location);
    }
    free(location);
    if (!follow) {
        fail(request, request->file ? PUBLIC_ERROR_REDIRECT : PUBLIC_ERROR_STORAGE);
        return;
    }
    request->state = PUBLIC_WAITING;
}
static void finish(public_request *request, CURLcode result)
{
    curl_easy_getinfo(request->easy, CURLINFO_RESPONSE_CODE, &request->status);
    bool moved = request->status == 301 || request->status == 302 || request->status == 303 ||
                 request->status == 307 || request->status == 308;
    if (result == CURLE_OK && request->file && moved) {
        redirect(request);
        return;
    }
    if (result == CURLE_OK && request->status) {
        bool written = !request->file || (fflush(request->file) == 0 && !ferror(request->file));
        if (!written) {
            fail(request, PUBLIC_ERROR_STORAGE);
            return;
        }
        release_transfer(request);
        if (request->file)
            fclose(request->file);
        request->file = NULL;
        request->state = PUBLIC_READY;
        return;
    }
    fail(request, request->overflow || result == CURLE_FILESIZE_EXCEEDED ? PUBLIC_ERROR_TOO_LARGE
                  : request->unwritten                                   ? PUBLIC_ERROR_STORAGE
                  : result == CURLE_PEER_FAILED_VERIFICATION || result == CURLE_SSL_CACERT_BADFILE
                      ? PUBLIC_ERROR_TLS
                      : PUBLIC_ERROR_TRANSPORT);
}
bool public_request_poll(public_request *request)
{
    if (!request || (request->state != PUBLIC_WAITING && request->state != PUBLIC_RUNNING))
        return false;
    if (request->state == PUBLIC_WAITING) {
        int admitted = net_rate_try_acquire();
        if (!admitted)
            return false;
        if (admitted < 0 || curl_multi_add_handle(request->multi, request->easy) != CURLM_OK) {
            fail(request, PUBLIC_ERROR_TRANSPORT);
            return true;
        }
        request->attached = true;
        request->state = PUBLIC_RUNNING;
    }
    int running, remaining;
    if (curl_multi_perform(request->multi, &running) != CURLM_OK) {
        fail(request, PUBLIC_ERROR_TRANSPORT);
        return true;
    }
    CURLMsg *message;
    while ((message = curl_multi_info_read(request->multi, &remaining)))
        if (message->msg == CURLMSG_DONE) {
            finish(request, message->data.result);
            return true;
        }
    return false;
}
public_state public_request_state(const public_request *request)
{
    return request ? request->state : PUBLIC_FAILED;
}
public_error public_request_error(const public_request *request)
{
    return request ? request->error : PUBLIC_ERROR_TRANSPORT;
}
size_t public_request_bytes(const public_request *request)
{
    return request ? request->bytes : 0;
}
long public_request_status(const public_request *request)
{
    return request && request->state == PUBLIC_READY ? request->status : 0;
}
unsigned public_request_retry_after(const public_request *request)
{
    return request && request->state == PUBLIC_READY ? request->retry_after : 0;
}
const char *public_request_body(const public_request *request, size_t *length)
{
    bool ready = request && request->state == PUBLIC_READY && request->body;
    if (length)
        *length = ready ? request->bytes : 0;
    return ready ? request->body : "";
}
