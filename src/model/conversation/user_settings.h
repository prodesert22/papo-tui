#ifndef PAPO_USER_SETTINGS_H
#define PAPO_USER_SETTINGS_H
#include "model/conversation/history.h"
#include "model/user_config.h"
/* The signed-in account's settings on this server (UserConfig). They are read
 * from whoami and saved with PUT /users/settings through history's REST
 * dispatcher: one at a time, inside the global request budget, never replayed.
 * The server replaces the whole config, so a save always sends all of it. */
typedef enum {
    USER_SETTINGS_IDLE,
    USER_SETTINGS_SAVING,
    USER_SETTINGS_SAVED,
    USER_SETTINGS_FAILED,
    USER_SETTINGS_UNCERTAIN
} user_settings_state;

/* The account's settings, or the contract defaults before whoami brings them.
 * Borrowed until the next poll; never NULL. */
const user_config *history_user_config(history *h);
/* Queues a save of config for the next free REST turn. False while another
 * save waits or runs, without a session, or when nothing would change. */
bool history_user_settings_save(history *h, const user_config *config);
bool history_user_settings_busy(history *h);
user_settings_state history_user_settings_state(history *h);
/* Last outcome in Portuguese, without terminal controls; "" when none. */
const char *history_user_settings_notice(history *h);
/* Changes whenever the settings, state or notice change, across accounts. */
uint64_t history_user_settings_revision(history *h);
#endif
