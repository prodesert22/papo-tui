#ifndef PAPO_PUBLIC_REQUEST_H
#define PAPO_PUBLIC_REQUEST_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PUBLIC_IDLE,
    PUBLIC_WAITING, /* Queued for the global request budget. */
    PUBLIC_RUNNING,
    PUBLIC_READY, /* A complete response arrived; its status may be an error. */
    PUBLIC_FAILED /* No complete response: transport, TLS or size failure. */
} public_state;
typedef enum {
    PUBLIC_ERROR_NONE,
    PUBLIC_ERROR_TRANSPORT,
    PUBLIC_ERROR_TLS,
    PUBLIC_ERROR_TOO_LARGE
} public_error;
typedef struct public_request public_request;

/* One HTTPS GET at a time to a host that is not a chat server. It belongs to
 * no instance: no session, no cookies and no shared connections, and redirects
 * are not followed. Poll on the main thread; each request takes one token of
 * the global request budget and waits for it without blocking. A 403 or 429
 * answer is only reported: it never defers the budget the chat servers use. */
public_request *public_request_create(void);
void public_request_destroy(public_request *request);
/* Starts a GET of an https URL, replacing any transfer in progress. headers are
 * borrowed during the call; the body is kept in memory up to limit bytes. */
bool public_request_get(public_request *request, const char *url, const char *const *headers,
                        size_t header_count, size_t limit);
/* True when the state changed. */
bool public_request_poll(public_request *request);
void public_request_cancel(public_request *request);
public_state public_request_state(const public_request *request);
public_error public_request_error(const public_request *request);
/* READY only: the HTTP status, the seconds of a Retry-After header (zero when
 * absent) and the body, borrowed until the next request or cancel. */
long public_request_status(const public_request *request);
unsigned public_request_retry_after(const public_request *request);
const char *public_request_body(const public_request *request, size_t *length);

#endif
