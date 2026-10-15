#ifndef PAPO_AUTH_H
#define PAPO_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "net/ws.h"

typedef enum {
    AUTH_IDLE,
    AUTH_SERVER,
    AUTH_SERVER_PASSWORD,
    AUTH_LOGIN,
    AUTH_WHOAMI,
    AUTH_READY,
    AUTH_LOGOUT,
    AUTH_ERROR,
    AUTH_HEALTH,
    AUTH_DISCOVERY_SERVER,
    AUTH_IDENTIFIED,
    AUTH_REGISTER,
    AUTH_REGISTERED,
    AUTH_REFRESH,
    AUTH_API_GET,
    AUTH_MESSAGE_SEND
} auth_state;

typedef enum { AUTH_INPUT_NONE, AUTH_INPUT_URL, AUTH_INPUT_USERNAME,
               AUTH_INPUT_PASSWORD, AUTH_INPUT_SERVER_PASSWORD } auth_input_error;

typedef struct auth_client auth_client;
typedef enum { AUTH_MEDIA_ATTACHMENT, AUTH_MEDIA_THUMBNAIL, AUTH_MEDIA_EMBED,
               AUTH_MEDIA_VIDEO } auth_media_kind;
/* Internal read-only media transport. Creates an owned easy handle configured
 * for this instance's cookies and TLS policy; never dispatches HTTP. The media
 * owner must destroy it before auth_destroy and acquire the global budget.
 * Only UUID-addressed, documented media endpoints are accepted. Main thread. */
CURL *auth_media_easy(auth_client *client, auth_media_kind kind, const char *id);
/* Gate called before admitting refresh/logout to HTTP: 1 permits, 0 waits,
 * -1 fails closed. Borrowed context; detach the gate before destroying it. */
typedef int (*auth_session_gate)(void *context, auth_client *client);
void auth_set_session_gate(auth_client *client, auth_session_gate gate, void *context);
/* Owned secret snapshot; wipe with auth_session_free. Never log or write it
 * outside a credential manager. Only verified, unexpired Auth cookies export. */
char *auth_session_export(auth_client *client);
void auth_session_free(char **snapshot);
bool auth_session_restore(auth_client *client, const char *snapshot, const char *expected_url,
                          bool allow_local_http);
bool auth_session_rejected(const auth_client *client);

/* Owns a per-instance cookie share, REST request buffers and WS transport.
 * All calls run on one thread. Only one REST transfer is active per client. */
auth_client *auth_create(void);
void auth_destroy(auth_client *client);
bool auth_start(auth_client *client, const char *url, const char *username,
                const char *password, const char *server_password);
bool auth_start_with_policy(auth_client *client, const char *url, const char *username,
                            const char *password, const char *server_password, bool allow_local_http);
/* Pure local validation: no transfer, cookie or session changes. */
bool auth_validate_registration(const char *username, const char *password,
                                auth_input_error *field, char *message, size_t capacity);
bool auth_register_with_policy(auth_client *client, const char *url, const char *username,
                               const char *password, const char *server_password, bool allow_local_http);
auth_input_error auth_error_input(const auth_client *client);
/* A verified identity remains visible during proactive renewal, but is hidden
 * immediately during logout or 401 recovery. Cookies are never exposed. */
bool auth_has_session(const auth_client *client);
const char *auth_user_id(const auth_client *client);
const char *auth_instance_url(const auth_client *client);
uint64_t auth_context_generation(const auth_client *client);
/* One renewal at a time, sharing the existing REST queue and request budget. */
bool auth_refresh(auth_client *client);
/* Copies a relative endpoint when READY and the REST queue is available.
 * Rejects concurrent submissions; a pending GET may recover one 401. Completed responses are bounded to one owned JSON value.
 * Message mutations use a separate response slot and never replay. */
bool auth_get_json(auth_client *client, const char *path);
/* Read-only POST /users/user_summary_batch. Copies 1..1000 UUID strings;
 * participates in the same queue/budget and may recover one 401, like GET.
 * This endpoint has no mutation semantics; no generic POST replay API exists. */
bool auth_user_summaries(auth_client *client, const json_t *ids);
/* Bounded asset reads through the same authenticated queue and global budget. */
bool auth_user_profiles(auth_client *client, const json_t *ids);
bool auth_emojis(auth_client *client, const char *path);
/* Discards an external read/result while preserving the verified session and
 * global budget. An already-started refresh/whoami recovery finishes safely;
 * its cancelled external read will not be replayed afterward. */
void auth_cancel_read(auth_client *client);
/* Readiness is also set for a response without JSON (status 0 on transport
 * failure). Taking clears readiness and transfers a possibly NULL reference;
 * the caller must json_decref(). Generation identifies the verified context.
 * Logout, cancellation and authentication failure discard pending responses. */
bool auth_response_ready(const auth_client *client);
json_t *auth_take_response(auth_client *client, long *status, uint64_t *generation);
typedef enum { AUTH_SEND_CONFIRMED, AUTH_SEND_REJECTED, AUTH_SEND_UNCERTAIN } auth_send_outcome;
typedef struct {
    auth_send_outcome outcome;
    long status;
    uint64_t operation_id, generation;
    bool transmitted, cancelled;
} auth_send_receipt;

/* Text-only POST /messages through libcurl MIME and the existing REST queue.
 * Copies inputs; rejects concurrent submissions. Never replays the mutation.
 * A 401 may renew identity once to permit a subsequent manual attempt. */
bool auth_send_text(auth_client *client, uint64_t operation_id, const char *channel_id, const char *content);
bool auth_send_result_ready(const auth_client *client);
/* Transfers one owned Message JSON reference only for confirmed sends.
 * Even NULL results must be consumed to release the response slot. */
json_t *auth_take_send_result(auth_client *client, auth_send_receipt *receipt);
/* Cancels only this mutation, preserving the verified session. Transmission
 * already started means the server outcome can be uncertain. */
bool auth_cancel_send(auth_client *client);

/* Public metadata only; owns/cancels the prior context before GET /health and
 * GET /server. Poll in the event loop, like login. No credentials are needed. */
bool auth_discover(auth_client *client, const char *url, bool allow_local_http);
/* Valid after AUTH_IDENTIFIED or AUTH_READY. */
bool auth_server_public(const auth_client *client);
/* Identifies a missing/rejected server password without parsing UI text. */
bool auth_server_password_error(const auth_client *client);
bool auth_logout(auth_client *client);
void auth_poll(auth_client *client);
void auth_cancel(auth_client *client);
auth_state auth_get_state(const auth_client *client);
bool auth_busy(const auth_client *client);
const char *auth_message(const auth_client *client);
const char *auth_username(const auth_client *client);
const char *auth_server_name(const auth_client *client);
/* WS is explicitly started after REST confirms the session. */
bool auth_connect_ws(auth_client *client);
/* Enables automatic recovery only for the foreground context. Existing open
 * sockets still receive maintenance when inactive. No REST or WS I/O here. */
void auth_realtime_active(auth_client *client, bool active);
/* Monotonic connection incarnation; changes only after a successful upgrade. */
uint64_t auth_ws_epoch(const auth_client *client);
bool auth_ws_reconnecting(const auth_client *client);
ws_state auth_ws_state(const auth_client *client);
const char *auth_ws_message(const auth_client *client);
long auth_ws_http_status(const auth_client *client);
/* Transfers an event reference to the caller, who must json_decref(). */
json_t *auth_next_event(auth_client *client);
bool auth_send_event(auth_client *client, const json_t *event);

/* Returns a libcurl-allocated canonical URL. Free with curl_free(). */
char *auth_normalize_url(const char *url, char *error, size_t error_size);

#endif
