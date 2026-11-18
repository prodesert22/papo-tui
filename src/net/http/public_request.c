#include "net/http/public_request.h"
#include "net/http/easy.h"
#include "net/rate_limit.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PUBLIC_URL_LIMIT 2048U
#define PUBLIC_TIMEOUT_MS 30000L

struct public_request {
    CURLM *multi;
    CURL *easy;
    struct curl_slist *headers;
    char *body;
    size_t bytes, capacity, limit;
    public_state state;
    public_error error;
    long status;
    unsigned retry_after;
    bool attached, overflow;
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
    free(request->body);
    request->body = NULL;
    request->bytes = request->capacity = 0;
    request->state = PUBLIC_IDLE;
    request->error = PUBLIC_ERROR_NONE;
    request->status = 0;
    request->retry_after = 0;
    request->overflow = false;
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
    if (bytes >= 5 && !strncasecmp(data, "HTTP/", 5))
        request->retry_after = 0;
    if (bytes > 12 && !strncasecmp(data, "Retry-After:", 12))
        request->retry_after = net_rate_retry_after(data + 12, bytes - 12);
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
    request->easy = net_easy_base(url);
    if (!request->easy ||
        curl_easy_setopt(request->easy, CURLOPT_PROTOCOLS_STR, "https") != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_TIMEOUT_MS, PUBLIC_TIMEOUT_MS) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_HTTPHEADER, request->headers) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_WRITEFUNCTION, receive) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_WRITEDATA, request) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_HEADERFUNCTION, header) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_HEADERDATA, request) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)limit) != CURLE_OK)
        goto error;
    request->state = PUBLIC_WAITING;
    return true;
error:
    public_request_cancel(request);
    return false;
}
static void finish(public_request *request, CURLcode result)
{
    curl_easy_getinfo(request->easy, CURLINFO_RESPONSE_CODE, &request->status);
    release_transfer(request);
    if (result == CURLE_OK && request->status) {
        request->state = PUBLIC_READY;
        return;
    }
    request->error =
        request->overflow || result == CURLE_FILESIZE_EXCEEDED ? PUBLIC_ERROR_TOO_LARGE
        : result == CURLE_PEER_FAILED_VERIFICATION || result == CURLE_SSL_CACERT_BADFILE
            ? PUBLIC_ERROR_TLS
            : PUBLIC_ERROR_TRANSPORT;
    free(request->body);
    request->body = NULL;
    request->bytes = request->capacity = 0;
    request->status = 0;
    request->state = PUBLIC_FAILED;
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
            finish(request, CURLE_FAILED_INIT);
            return true;
        }
        request->attached = true;
        request->state = PUBLIC_RUNNING;
    }
    int running, remaining;
    if (curl_multi_perform(request->multi, &running) != CURLM_OK) {
        finish(request, CURLE_RECV_ERROR);
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
