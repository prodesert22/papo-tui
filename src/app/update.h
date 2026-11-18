#ifndef PAPO_APP_UPDATE_H
#define PAPO_APP_UPDATE_H

#include "platform/install.h"
#include <stdbool.h>
#include <stdint.h>

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

/* The package of the newer version for this installation. */
typedef enum {
    UPDATE_TRANSFER_NONE,
    /* The release's manifest and its signature are fetched and checked with
     * the project's key before any byte of the package is asked for. */
    UPDATE_TRANSFER_AUTHENTICATING,
    UPDATE_TRANSFER_RUNNING,
    UPDATE_TRANSFER_VERIFYING, /* Downloaded; its size and SHA-256 are being checked. */
    UPDATE_TRANSFER_UNPACKING, /* Checked; the portable archive is being unpacked. */
    UPDATE_TRANSFER_READY,     /* Checked and waiting to be taken. */
    UPDATE_TRANSFER_FAILED
} update_transfer;
typedef enum {
    UPDATE_TRANSFER_OK,
    UPDATE_TRANSFER_NETWORK,
    UPDATE_TRANSFER_TLS,
    UPDATE_TRANSFER_REFUSED,  /* An answer other than the file, or a redirect not allowed. */
    UPDATE_TRANSFER_MISMATCH, /* Not the published size or SHA-256: discarded. */
    UPDATE_TRANSFER_STORAGE,  /* The temporary file could not be written. */
    UPDATE_TRANSFER_UNUSABLE, /* The archive is not the tree of that version: discarded. */
    UPDATE_TRANSFER_UNSIGNED, /* The release publishes no signed manifest. */
    /* The manifest is not signed by the project's key, or does not list the
     * package with the sum GitHub reports. */
    UPDATE_TRANSFER_FORGED
} update_transfer_failure;

/* How this copy was installed, which decides the package offered. */
install_channel update_channel(void);
/* The package's file name and size; "" and zero when the newer release has
 * none for this installation, or this is not a recognized installation. */
const char *update_asset_name(void);
uint64_t update_asset_size(void);
/* Downloads the package into a private temporary folder and checks it; false
 * when there is none or a download is already under way. update_poll drives
 * it. Cancelling, or a failed check, removes the file. */
bool update_download_start(void);
void update_download_cancel(void);
update_transfer update_download_status(void);
update_transfer_failure update_download_error(void);
int update_download_percent(void);
/* READY only: hands the checked file to the caller (g_free), who moves or
 * removes it and then calls update_download_finish with the place it was
 * saved to, or NULL when it was not. */
char *update_download_take(void);
void update_download_finish(const char *saved);
/* Where the package of the newer version was saved in this run; "" when not. */
const char *update_saved(void);

/* Replacing this copy with the newer version, for an AppImage and for a folder
 * unpacked from the portable archive that this user can write. */
bool update_install_possible(void);
/* Downloads the package into a folder beside the installation, checks it and,
 * for the archive, unpacks and inspects it; false when this copy cannot be
 * replaced or a download is under way. update_download_status follows it, and
 * update_download_cancel stops it and removes what was prepared. */
bool update_install_start(void);
/* The newer version is prepared and waits to be put in place. */
bool update_install_staged(void);
/* Asks for the prepared version to be put in place when the program ends;
 * false when none is prepared. Leaving any other way discards it. */
bool update_install_on_exit(void);
typedef struct {
    bool done;
    char version[48];
    char place[1200]; /* What was replaced; or, when it was not, where the new version is. */
} update_applied;
/* After the screens are gone and update_shutdown ran: puts the version asked
 * for in place and reports it. false when none was asked for. */
bool update_apply(update_applied *result);

#endif
