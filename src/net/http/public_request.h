#ifndef PAPO_PUBLIC_REQUEST_H
#define PAPO_PUBLIC_REQUEST_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PUBLIC_IDLE,
    PUBLIC_WAITING, /* Queued for the global request budget. */
    PUBLIC_RUNNING,
    PUBLIC_READY, /* A complete response arrived; its status may be an error. */
    PUBLIC_FAILED /* No complete response: transport, TLS, size or redirect failure. */
} public_state;
typedef enum {
    PUBLIC_ERROR_NONE,
    PUBLIC_ERROR_TRANSPORT,
    PUBLIC_ERROR_TLS,
    PUBLIC_ERROR_TOO_LARGE,
    PUBLIC_ERROR_REDIRECT, /* A redirect that may not be followed, or too many. */
    PUBLIC_ERROR_STORAGE   /* The destination file could not be written. */
} public_error;
typedef struct public_request public_request;
/* Says whether a download may continue at the address a redirect names. */
typedef bool (*public_redirect)(const char *url, void *context);

#define PUBLIC_REDIRECT_LIMIT 3U

/* One HTTPS GET at a time to a host that is not a chat server. It belongs to
 * no instance: no session, no cookies and no shared connections. Poll on the
 * main thread; each request sent takes one token of the global request budget
 * and waits for it without blocking. A 403 or 429 answer is only reported: it
 * never defers the budget the chat servers use. */
public_request *public_request_create(void);
void public_request_destroy(public_request *request);
/* Starts a GET of an https URL, replacing any transfer in progress. headers are
 * borrowed during the call; the body is kept in memory up to limit bytes and
 * the whole request may take half a minute. Redirects are not followed. */
bool public_request_get(public_request *request, const char *url, const char *const *headers,
                        size_t header_count, size_t limit);
/* Starts a GET whose body goes to the file at path, which the caller created,
 * owns and removes; it is rewritten from the start. The transfer has no total
 * time limit, only a minimum speed. libcurl never follows a redirect by
 * itself: each one is checked with allowed, must be https, and is then sent as
 * a new request with its own budget token, at most PUBLIC_REDIRECT_LIMIT
 * times. */
bool public_request_download(public_request *request, const char *url, const char *path,
                             size_t limit, public_redirect allowed, void *context);
/* True when the state changed. */
bool public_request_poll(public_request *request);
void public_request_cancel(public_request *request);
public_state public_request_state(const public_request *request);
public_error public_request_error(const public_request *request);
/* Body bytes received so far by the request in progress or just finished. */
size_t public_request_bytes(const public_request *request);
/* READY only: the HTTP status, the seconds of a Retry-After header (zero when
 * absent) and, for a request kept in memory, the body, borrowed until the next
 * request or cancel. */
long public_request_status(const public_request *request);
unsigned public_request_retry_after(const public_request *request);
const char *public_request_body(const public_request *request, size_t *length);

#endif
