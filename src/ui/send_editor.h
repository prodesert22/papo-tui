#ifndef PAPO_SEND_EDITOR_H
#define PAPO_SEND_EDITOR_H

#include "app/outbox.h"
#include "ui/text.h"

typedef struct send_editor send_editor;

/* Auth must outlive the editor. One editor/outbox owns mutation receipts for
 * each session. Designed for the R5 conversation screen; no history fetching. */
send_editor *send_editor_create(auth_client *session);
void send_editor_destroy(send_editor *editor);
bool send_editor_select(send_editor *editor, const char *channel_id);
text_buffer *send_editor_input(send_editor *editor);
/* Call after every successful text edit or paste, before network polling.
 * Tracks revisions and cursor/viewport separately for each conversation. */
bool send_editor_changed(send_editor *editor);
outbox_submit_result send_editor_submit(send_editor *editor, bool accept_possible_duplicate);
outbox_submit_result send_editor_retry(send_editor *editor, uint64_t operation_id, bool accept_possible_duplicate);
/* Call after auth_poll(). Updates drafts after confirmed prefix removal. */
void send_editor_poll(send_editor *editor);
const char *send_editor_notice(const send_editor *editor);
/* NULL for ordinary editing instructions; otherwise a send-context notice. */
const char *send_editor_feedback(const send_editor *editor);
const char *send_editor_compact_notice(const send_editor *editor);
/* Borrowed model for status/cancellation and taking Message confirmations.
 * Taking transfers JSON ownership; merge into R5 history by definitive ID. */
outbox *send_editor_outbox(send_editor *editor);

#endif
