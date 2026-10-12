#ifndef PAPO_WS_H
#define PAPO_WS_H

#include <curl/curl.h>
#include <jansson.h>
#include <stdbool.h>
#include <stddef.h>

#define WS_MESSAGE_LIMIT (128U * 1024U)
#define WS_QUEUE_LIMIT 32U
#define WS_QUEUE_BYTES (256U * 1024U)

typedef enum { WS_IDLE, WS_QUEUED, WS_CONNECTING, WS_OPEN, WS_CLOSING,
               WS_CLOSED, WS_ERROR } ws_state;
typedef struct ws_client ws_client;

bool ws_supported(void);
/* Owns handles, buffers and event references. Borrows a per-context cookie
 * share, which must outlive the client. All calls run on one thread. */
ws_client *ws_create(CURLSH *cookies);
void ws_destroy(ws_client *client);
/* Base must be validated by auth_normalize_url() and belong to this jar. */
bool ws_connect(ws_client *client, const char *base);
void ws_poll(ws_client *client);
/* Borrows event and copies its serialized JSON into a bounded output queue. */
bool ws_send(ws_client *client, const json_t *event);
/* Graceful close is polled for at most 250 ms; abort releases I/O immediately. */
void ws_close(ws_client *client);
void ws_abort(ws_client *client);
/* Transfers an owned reference to the caller; caller must json_decref(). */
json_t *ws_pop(ws_client *client);
ws_state ws_get_state(const ws_client *client);
const char *ws_message(const ws_client *client);
long ws_http_status(const ws_client *client);
size_t ws_queued_events(const ws_client *client);

#endif
