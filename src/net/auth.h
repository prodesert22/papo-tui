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
    AUTH_API_GET
} auth_state;

typedef enum { AUTH_INPUT_NONE, AUTH_INPUT_URL, AUTH_INPUT_USERNAME,
               AUTH_INPUT_PASSWORD, AUTH_INPUT_SERVER_PASSWORD } auth_input_error;

typedef struct auth_client auth_client;

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
 * No mutation API or automatic mutation replay is provided. */
bool auth_get_json(auth_client *client, const char *path);
/* Read-only POST /users/user_summary_batch. Copies 1..1000 UUID strings;
 * participates in the same queue/budget and may recover one 401, like GET.
 * This endpoint has no mutation semantics; no generic POST replay API exists. */
bool auth_user_summaries(auth_client *client, const json_t *ids);
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
ws_state auth_ws_state(const auth_client *client);
const char *auth_ws_message(const auth_client *client);
long auth_ws_http_status(const auth_client *client);
/* Transfers an event reference to the caller, who must json_decref(). */
json_t *auth_next_event(auth_client *client);
bool auth_send_event(auth_client *client, const json_t *event);

/* Returns a libcurl-allocated canonical URL. Free with curl_free(). */
char *auth_normalize_url(const char *url, char *error, size_t error_size);

#endif
