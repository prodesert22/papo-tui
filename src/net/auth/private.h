#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef PAPO_AUTH_PRIVATE_H
#define PAPO_AUTH_PRIVATE_H
#include "net/auth/client.h"
#include "net/auth/session_time.h"
#include "net/clock.h"
#include "net/rate_limit.h"
#include "net/retry.h"
#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define RESPONSE_LIMIT (1024U * 1024U)

struct auth_client {
    permissions *permissions;
    json_t *permission_message;
    channel_permission permission_guard;
    bool permission_edit, permission_delete;
    unsigned permission_wanted;
    unsigned permission_failed, permission_retried;
    uint64_t permission_due, permission_serial;
    CURLSH *cookies;
    ws_client *websocket;
    CURL *easy;
    CURLM *multi;
    struct curl_slist *headers;
    curl_mime *mime;
    bool send_ready;
    bool send_has_attachments;
    auth_send_receipt send_receipt;
    json_t *sent_message;
    char send_channel[64];
    bool attached;
    bool queued;
    unsigned retry_after;
    auth_state state;
    auth_state operation;
    bool ws_requested;
    bool ws_managed, ws_active, ws_validate, ws_validated, validation_wait;
    uint64_t ws_retry_at, ws_stable_at, ws_epoch, expires_at;
    unsigned ws_failures;
    bool restoring, session_rejected, gate_passed;
    auth_session_gate session_gate;
    void *gate_context;
    uint64_t recheck_at;
    uint64_t validation_deadline;
    uint64_t refresh_at, generation;
    bool registering, authenticated, recovering, after_refresh, logout_requested;
    bool api_retried, response_ready;
    bool api_uncertain;
    const char *api_method; /* Static method literal; no borrowed input. */
    auth_input_error input_error;
    char *api_path;
    char *refresh_cookie;
    json_t *api_response;
    json_t *api_body;
    long api_status;
    uint64_t response_generation;
    char *base;
    char *username;
    char *password;
    char *server_password;
    char *payload;
    char *response;
    size_t response_length, response_limit;
    bool overflow;
    bool server_public;
    bool server_password_error;
    char message[192];
    char name[128];
    char user[1025];
    char user_id[64];
};

void auth_internal_erase_free(char **value);
void auth_internal_forget_inputs(auth_client *client);
void auth_internal_detach(auth_client *client);
void auth_internal_clear_cookies(auth_client *client);
void auth_internal_clear_api(auth_client *client);
void auth_internal_clear_send(auth_client *client);
void auth_internal_clear_identity(auth_client *client);
void auth_internal_logout_done(auth_client *client, bool revoked);
void auth_internal_fail(auth_client *client, const char *message);
bool auth_internal_input_fail(auth_client *client, auth_input_error field, const char *message);
const char *auth_internal_json_text(json_t *object, const char *key, size_t capacity);
bool auth_internal_problem_is(auth_client *client, const char *kind);
bool auth_internal_registration_valid(auth_client *client, const char *username,
                                      const char *password);
size_t auth_internal_receive(char *data, size_t size, size_t count, void *context);
size_t auth_internal_receive_header(char *data, size_t size, size_t count, void *context);
bool auth_internal_request(auth_client *client, auth_state state, const char *path, json_t *body,
                           bool post);
bool auth_internal_user_login(auth_client *client);
void auth_internal_copy_server_name(auth_client *client, const char *name);
char *auth_internal_cookie_snapshot(auth_client *client);
bool auth_internal_recover_session(auth_client *client);
bool auth_internal_session_cookie_valid(const char *row, const char *url);
bool auth_internal_send_transmitted(auth_client *client);
void auth_internal_reconnect_later(auth_client *client, uint64_t now);
void auth_internal_complete(auth_client *client, CURLcode result, long status);
void auth_internal_permissions_observe(void *context, const json_t *event);
void auth_internal_permissions_poll(auth_client *client, uint64_t now);
void auth_internal_permissions_response(auth_client *client, CURLcode result, long status);
bool auth_internal_permission_admitted(auth_client *client);

#endif
