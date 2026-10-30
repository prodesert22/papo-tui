#ifndef PAPO_SESSION_STORE_H
#define PAPO_SESSION_STORE_H
#include "config/profiles.h"
#include "net/auth/client.h"

typedef struct session_store session_store;
/* Owns private public-state descriptors and an optional asynchronous desktop
 * credential provider. config_directory is borrowed and defines the scope. */
session_store *session_store_create(const profile_config *config, bool restore);
void session_store_destroy(session_store *store);
/* Keeps one saved session per instance URL. Runs completions and watches every
 * borrowed client bound to a saved instance; the active client is bound once it
 * has a session. Call before auth_poll in login/history; cancellation and
 * destruction must unwatch clients. */
bool session_store_poll(session_store *store, auth_client *active);
/* Binds a client restored from session_store_take_resume to its saved slot. */
void session_store_watch(session_store *store, auth_client *client);
void session_store_unwatch(session_store *store, auth_client *client);
/* The newest saved instance, or the newest remembered URL. */
const char *session_store_last_url(const session_store *store);
const char *session_store_notice(const session_store *store);
bool session_store_restoring(const session_store *store);
/* Transfers an owned secret snapshot, wipe with auth_session_free. url borrows
 * the instance of that snapshot until the store is destroyed. */
char *session_store_take_resume(session_store *store, const char **url);
void session_store_cancel_resume(session_store *store);
/* Ascending creation order of a bound client's slot, for restoring tab order. */
unsigned session_store_order(const session_store *store, const auth_client *client);
#endif
