#ifndef PAPO_INSTANCES_H
#define PAPO_INSTANCES_H
#include "net/auth/client.h"
#define INSTANCE_LIMIT 64

typedef struct send_editor send_editor;
typedef struct session_store session_store;
typedef struct history history;
typedef struct desktop_notifier desktop_notifier;
typedef struct profile_config profile_config;
typedef struct {
    auth_client *auth;
    void *media; /* Owned presentation cache; callbacks avoid a UI dependency. */
    void (*media_destroy)(void *);
    void (*media_idle)(void *);
    void (*media_update)(void *, const json_t *);
    send_editor *editor;
    history *model;
    json_t *alerts;
    uint64_t alert_due, serial, desktop_generation;
    /* Next automatic read of the saved session while listed offline; 0 when
     * none is scheduled. Failures grow the wait until the server answers. */
    uint64_t retry_at;
    unsigned retry_failures;
    bool retrying;      /* An automatic read is validating; the tab still shows offline. */
    bool offline_shown; /* Last offline state reported, to redraw when it changes. */
    char name[129], url[2049];
    /* Set when the user clicks one of this server's desktop notifications. */
    char open_channel[37], open_message[37], open_time[65];
} instance_connection;
typedef struct {
    instance_connection items[INSTANCE_LIMIT];
    size_t count;
    session_store *sessions; /* Owned; initialized by login after profiles. */
    desktop_notifier *desktop;
    profile_config *config; /* Borrowed; outlives connections. */
    auth_client *visible_auth;
    char visible_channel[37];
    bool allow_local_http; /* Policy for restoring saved sessions of offline tabs. */
    uint64_t next_serial;
    size_t poll_cursor;
} instance_connections;
/* Owns attached clients, each with its own cookies/identity. Public labels and
 * sessions live only in memory. A pending login client is owned by its caller. */
instance_connections *instances_create(void);
void instances_destroy(instance_connections *connections);
bool instances_contains(const instance_connections *connections, const auth_client *auth);
/* Takes ownership on success, replacing an older connection to the same URL. */
bool instances_attach(instance_connections *connections, auth_client *auth);
/* Keeps a saved server that could not be reached as a tab without a session,
 * so it can be retried or removed later. Takes ownership on success. */
bool instances_attach_offline(instance_connections *connections, auth_client *auth);
/* Stable reorder of the tabs by ascending rank; call only while no screen
 * holds a tab index. */
void instances_order(instance_connections *connections,
                     unsigned (*rank)(const auth_client *auth, void *context), void *context);
/* The tab has no session and is not connecting, or only an automatic retry
 * of its saved session is, or its kept session cannot reach the server. */
bool instances_offline(const instance_connections *connections, size_t index);
/* Transfers ownership back to caller, removing this tab without destroying it. */
void instances_detach(instance_connections *connections, auth_client *auth);
size_t instances_index(const instance_connections *connections, const auth_client *auth);
/* Borrowed editor, owned by its connection and destroyed before auth. */
send_editor *instances_editor(const instance_connections *connections, const auth_client *auth);
void instances_media_event(instance_connections *connections, auth_client *auth,
                           const json_t *event);
history *instances_history(const instance_connections *connections, const auth_client *auth);
void instances_visible(instance_connections *connections, auth_client *auth, const char *channel);
/* Another tab with a session whose desktop notification was clicked, or
 * SIZE_MAX. Its destination stays pending until that tab takes it. */
size_t instances_open_elsewhere(const instance_connections *connections, const auth_client *active);
/* Takes the destination of a clicked desktop notification of this server, once.
 * The time is empty when the message's own creation time is unknown. */
bool instances_take_open(instance_connections *connections, const auth_client *auth,
                         char channel[37], char message[37], char time[65]);
/* Polls inactive sessions/WS and metadata; never requests inactive history.
 * A saved server listed offline reads its saved session again on its own,
 * waiting longer after each failure; a rejected session is never retried. */
bool instances_poll(instance_connections *connections, auth_client *active);
/* Restores saved sessions read for offline tabs, for screens other than the
 * sign-in screen, which restores them itself. */
bool instances_resume(instance_connections *connections);
#endif
