#ifndef PAPO_AUTH_H
#define PAPO_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include "net/ws.h"

typedef enum {
    AUTH_IDLE,
    AUTH_SERVER,
    AUTH_SERVER_PASSWORD,
    AUTH_LOGIN,
    AUTH_WHOAMI,
    AUTH_READY,
    AUTH_LOGOUT,
    AUTH_ERROR
} auth_state;

typedef struct auth_client auth_client;

/* Owns a per-instance cookie share, REST request buffers and WS transport.
 * All calls run on one thread. Only one REST transfer is active per client. */
auth_client *auth_create(void);
void auth_destroy(auth_client *client);
bool auth_start(auth_client *client, const char *url, const char *username,
                const char *password, const char *server_password);
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
