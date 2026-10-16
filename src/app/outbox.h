#ifndef PAPO_OUTBOX_H
#define PAPO_OUTBOX_H

#include "net/auth/client.h"

#define OUTBOX_DRAFT_LIMIT 64
#define OUTBOX_OPERATION_LIMIT 32
#define OUTBOX_TEXT_BYTES 8193
#define OUTBOX_TEXT_LIMIT 2048

typedef enum {
    OUTBOX_QUEUED,
    OUTBOX_SENDING,
    OUTBOX_CONFIRMED,
    OUTBOX_FAILED,
    OUTBOX_UNCERTAIN
} outbox_state;
typedef enum {
    OUTBOX_ACCEPTED,
    OUTBOX_EMPTY,
    OUTBOX_BUSY,
    OUTBOX_CONFIRM_DUPLICATION,
    OUTBOX_NO_SESSION,
    OUTBOX_FULL,
    OUTBOX_INVALID
} outbox_submit_result;
typedef struct outbox outbox;
typedef struct {
    uint64_t id, generation, draft_revision;
    outbox_state state;
    long status;
    const char *channel_id, *content, *reply_to; /* Borrowed until update/removal/destruction. */
    bool transmitted, cancelled;
} outbox_operation;

/* Borrows auth_client, which must outlive the outbox. All calls use the event
 * loop thread. This is the sole consumer of send receipts for the session.
 * No persistence, networking threads or automatic mutation replay. */
outbox *outbox_create(auth_client *session);
void outbox_destroy(outbox *box);
/* Explicit logout/context destruction must reset or destroy the outbox. */
void outbox_reset(outbox *box);
/* Authenticated identities gate access. Failed revalidation freezes drafts;
 * only the same verified instance/account may restore them in memory. */
const char *outbox_draft(outbox *box, const char *channel_id, uint64_t *revision);
bool outbox_set_draft(outbox *box, const char *channel_id, const char *content);
/* Reply metadata is owned by the per-conversation draft, independently of text. */
bool outbox_set_reply(outbox *box, const char *channel_id, const char *reply_to);
const char *outbox_reply(outbox *box, const char *channel_id);
outbox_submit_result outbox_submit(outbox *box, const char *channel_id,
                                   bool accept_possible_duplicate, uint64_t *operation_id);
/* Retries the captured text, preserving the current editor. An uncertain
 * result requires explicit acknowledgement of possible duplication. */
outbox_submit_result outbox_retry(outbox *box, uint64_t operation_id,
                                  bool accept_possible_duplicate, uint64_t *new_operation_id);
/* Call after auth_poll(). Dispatches a bounded queue one request at a time. */
void outbox_poll(outbox *box);
bool outbox_cancel(outbox *box, uint64_t operation_id);
bool outbox_get_operation(outbox *box, uint64_t operation_id, outbox_operation *operation);
bool outbox_latest(outbox *box, const char *channel_id, outbox_operation *operation);
/* Transfers one confirmed Message reference to the caller for merge by ID.
 * Channel and generation are returned in operation. Caller json_decref(). */
json_t *outbox_take_confirmed(outbox *box, outbox_operation *operation);
/* Release the confirmed text after the caller merges/discards its Message. */
void outbox_reconciled(outbox *box, uint64_t operation_id);
bool outbox_forget(outbox *box, uint64_t operation_id);
const char *outbox_operation_notice(const outbox_operation *operation);

#endif
