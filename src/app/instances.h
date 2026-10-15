#ifndef PAPO_INSTANCES_H
#define PAPO_INSTANCES_H
#include "net/auth.h"
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
    char name[129], url[2049];
} instance_connection;
typedef struct {
    instance_connection items[INSTANCE_LIMIT];
    size_t count;
    session_store *sessions; /* Owned; initialized by login after profiles. */
    desktop_notifier *desktop;
    profile_config *config; /* Borrowed; outlives connections. */
    auth_client *visible_auth;
    char visible_channel[37];
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
/* Transfers ownership back to caller, removing this tab without destroying it. */
void instances_detach(instance_connections *connections, auth_client *auth);
size_t instances_index(const instance_connections *connections, const auth_client *auth);
/* Borrowed editor, owned by its connection and destroyed before auth. */
send_editor *instances_editor(const instance_connections *connections, const auth_client *auth);
void instances_media_event(instance_connections *connections, auth_client *auth, const json_t *event);
history *instances_history(const instance_connections *connections, const auth_client *auth);
void instances_visible(instance_connections *connections, auth_client *auth, const char *channel);
/* Polls inactive sessions/WS and metadata; never requests inactive history. */
bool instances_poll(instance_connections *connections, auth_client *active);
#endif
