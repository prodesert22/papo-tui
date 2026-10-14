#ifndef PAPO_SESSION_STORE_H
#define PAPO_SESSION_STORE_H
#include "net/auth.h"

typedef struct session_store session_store;
/* Owns private public-state descriptors and an optional asynchronous desktop
 * credential provider. config_directory is borrowed and defines the scope. */
session_store *session_store_create(int config_directory, bool restore);
void session_store_destroy(session_store *store);
/* Runs completions and watches the active borrowed client. Call before
 * auth_poll in login/history; cancellation/destruction must unwatch clients. */
bool session_store_poll(session_store *store, auth_client *active);
void session_store_unwatch(session_store *store, auth_client *client);
const char *session_store_last_url(const session_store *store);
const char *session_store_notice(const session_store *store);
bool session_store_restoring(const session_store *store);
/* Transfers an owned secret snapshot, wipe with auth_session_free. */
char *session_store_take_resume(session_store *store);
void session_store_cancel_resume(session_store *store);
#endif
