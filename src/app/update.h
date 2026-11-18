#ifndef PAPO_APP_UPDATE_H
#define PAPO_APP_UPDATE_H

#include <stdbool.h>

typedef enum {
    UPDATE_UNKNOWN,  /* Never checked, or the last answer is older than a day. */
    UPDATE_CHECKING, /* A check is queued or running. */
    UPDATE_CURRENT,  /* The last check found nothing newer. */
    UPDATE_AVAILABLE,
    UPDATE_FAILED
} update_state;
typedef enum {
    UPDATE_FAILURE_NONE,
    UPDATE_FAILURE_NETWORK,
    UPDATE_FAILURE_TLS,
    UPDATE_FAILURE_LIMITED, /* GitHub refused for now (403 or 429). */
    UPDATE_FAILURE_RESPONSE
} update_failure;

/* Process-wide and owned by the event-loop thread, like the request budget:
 * the answer is about the program, not about a server or an account. Reads
 * the stored result of the last check. With automatic set, a check runs soon
 * after start when the last one is a day old, and then once a day; development
 * builds, and runs with PAPO_UPDATE_CHECK=0, only check on request. */
void update_init(bool automatic);
void update_shutdown(void);
void update_set_automatic(bool automatic);
/* Drives the schedule and the request; true when what a screen shows changed. */
bool update_poll(void);
/* Starts a check on request; false while one runs or within a minute of the
 * previous one. */
bool update_check_now(void);
update_state update_status(void);
update_failure update_error(void);
/* The newer version known from the last successful check, or "" when none. */
const char *update_latest(void);
/* This build is not a release: it never checks by itself. */
bool update_development(void);
/* PAPO_UPDATE_CHECK=0 keeps this run from checking by itself, whatever the
 * stored switch says; a check on request still runs. */
bool update_suppressed(void);
/* A newer version is known and no screen has shown its details in this run. */
bool update_notice(void);
void update_acknowledge(void);

#endif
