#ifndef PAPO_SESSION_SLOT_H
#define PAPO_SESSION_SLOT_H
#include "config/profiles.h"
#include "net/auth/client.h"

/* Private interface between session_store.c and the platform backends. A slot
 * persists one instance session: a public marker file plus one desktop
 * credential. Slot 0 keeps the names of the former single-session store, so an
 * existing saved session restores after an update. */
#define SESSION_SLOT_LIMIT 64
typedef struct session_slot session_slot;
typedef struct session_scope session_scope;

/* Holds the exclusive lock of this configuration for the store lifetime.
 * Returns NULL, with an explanation in notice, when another window holds it or
 * private state is unavailable; sessions then stay in memory only. */
session_scope *session_scope_open(const profile_config *config, char *notice, size_t size);
/* Writes up to limit sequence numbers of existing slot markers, ascending. */
size_t session_scope_list(const session_scope *scope, unsigned *sequences, size_t limit);
void session_scope_close(session_scope *scope);

/* Never returns a partially usable slot: on failure it is memory-only and
 * reports why in its notice. config is borrowed and must outlive the slot. */
session_slot *session_slot_create(const profile_config *config, unsigned sequence, bool restore);
/* Waits briefly for durable writes; call session_slot_busy first to drain
 * several slots together. */
void session_slot_destroy(session_slot *slot);
/* Watches the borrowed client, which must belong to this slot's URL once it has
 * a session. NULL keeps any previous client and only advances operations. */
bool session_slot_poll(session_slot *slot, auth_client *client);
void session_slot_unwatch(session_slot *slot, auth_client *client);
bool session_slot_busy(const session_slot *slot);
const char *session_slot_url(const session_slot *slot);
const char *session_slot_notice(const session_slot *slot);
/* The committed marker allows restoring this slot's credential. */
bool session_slot_saved(const session_slot *slot);
bool session_slot_restoring(const session_slot *slot);
/* Transfers an owned secret snapshot, wipe with auth_session_free. */
char *session_slot_take_resume(session_slot *slot);
void session_slot_cancel_resume(session_slot *slot);
#endif
