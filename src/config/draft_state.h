#ifndef PAPO_DRAFT_STATE_H
#define PAPO_DRAFT_STATE_H
#include <jansson.h>
#include <stdbool.h>

#define DRAFT_STATE_LIMIT 64U
#define DRAFT_STATE_TEXT_BYTES 8192U

/* Unsent message text and reply targets, kept for the next run. Scope is the
 * normalized instance URL plus the verified user UUID. The text is your own
 * writing: it is stored only in this private file (mode 0600 beside the read
 * cursors, atomically replaced) and never logged. Attachments are not kept.
 *
 * Maps are {channel_id: {"text": string, "reply_to": UUID or null}}. Load
 * returns an owned map, empty when unavailable, keeping only valid entries
 * with control characters other than newline and tab removed. Save replaces
 * the scope's map, dropping conversations whose draft became empty; forget
 * removes the scope. Both take the storage lock without waiting. */
json_t *draft_state_load(const char *url, const char *user);
bool draft_state_save(const char *url, const char *user, const json_t *drafts);
bool draft_state_forget(const char *url, const char *user);
bool draft_state_validate(json_t *root);
#endif
