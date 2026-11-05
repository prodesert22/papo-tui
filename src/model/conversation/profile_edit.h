#ifndef PAPO_PROFILE_EDIT_H
#define PAPO_PROFILE_EDIT_H
#include "model/conversation/history.h"
/* The signed-in user's own profile on this server: nickname, status phrase,
 * description, typing phrase, presence status, avatar and banner. Event-loop
 * only; requests go through history's REST dispatcher one at a time, share the
 * global request budget and are never replayed. */
typedef enum {
    PROFILE_IDLE,
    PROFILE_LOADING,
    PROFILE_SAVING,
    PROFILE_SAVED,
    PROFILE_FAILED,
    PROFILE_UNCERTAIN
} profile_state;
typedef enum { PROFILE_AVATAR, PROFILE_BANNER } profile_image;
/* Server limits (characters; JSON Schema maxLength). */
#define PROFILE_NICKNAME_MAX 32
#define PROFILE_STATUS_MAX 64
#define PROFILE_DESCRIPTION_MAX 512
#define PROFILE_TYPING_MAX 64

/* Loads the profile unless it is loaded or loading; force reloads it. */
void history_profile_load(history *h, bool force);
/* Borrowed until the next poll; NULL until the first load succeeds. */
const json_t *history_profile(history *h);
/* Validates without queueing: NULL when text fits field, else the reason. */
const char *history_profile_check(const char *field, const char *text);
/* Each NULL keeps the current value; "" clears it. The four text fields are
 * always sent together, completed from the loaded profile. */
bool history_profile_save(history *h, const char *nickname, const char *status_phrase,
                          const char *description, const char *typing);
/* "away", "busy", or NULL to be online again. */
bool history_profile_set_status(history *h, const char *status);
/* Takes ownership of base64 (g_free). format: GIF, JPEG, PNG or WEBP. */
bool history_profile_set_image(history *h, profile_image kind, char *base64, const char *format);
bool history_profile_remove_banner(history *h);
profile_state history_profile_state(history *h);
/* Last outcome in Portuguese, sanitized; "" when there is none. */
const char *history_profile_notice(history *h);
/* Changes whenever the profile, state or notice changes. */
uint64_t history_profile_revision(history *h);
/* Counts completed loads, so a form refills only from a new server copy. */
uint64_t history_profile_loads(history *h);
#endif
