#ifndef PAPO_HISTORY_H
#define PAPO_HISTORY_H
#include "net/auth.h"
#include "model/mentions.h"
#include "model/inbox.h"

#define HISTORY_CHANNEL_LIMIT 500
#define HISTORY_MESSAGE_LIMIT 1000
#define HISTORY_BYTE_LIMIT (4U * 1024U * 1024U)

typedef struct history history;
typedef enum { HISTORY_NOTICE_READ, HISTORY_NOTICE_EVENT, HISTORY_NOTICE_COUNT } history_notice_kind;
/* Owns all JSON models; borrows the auth client. One active conversation, no
 * inactive history cache. Returned objects/strings are borrowed until poll,
 * event, confirmation, selection, reload or destroy. Callers retain anchors
 * by UUID, not pointer. */
history *history_create(auth_client *auth);
void history_destroy(history *h);
/* A connection owns the model; views borrow it. Inactive contexts keep only
 * metadata and never request message history. R8 is opt-in for old probes. */
void history_enable_r8(history *h);
inbox *history_inbox(history *h);
void history_active(history *h, bool active);
void history_activity(history *h);
bool history_poll(history *h);
/* Copies a confirmed Message. Merges only the active channel/current
 * generation, with definitive ID deduplication and bounded memory. No HTTP. */
bool history_confirm(history *h, json_t *message, uint64_t generation);
/* Borrows event; stores owned copies only where needed for concurrent reads.
 * Never reads an inactive channel's history. */
bool history_event(history *h, const json_t *event, uint64_t generation);
void history_recover(history *h);
bool history_recovering(const history *h);
uint64_t history_insertions(const history *h);
void history_reload_channels(history *h);
bool history_select(history *h, size_t channel);
void history_recent(history *h);
void history_older(history *h);
/* Returns true when retrying in place; false when reloading the recent page
 * or channel list, so the view can reset its anchor deliberately. */
bool history_retry(history *h);
size_t history_channel_count(const history *h);
json_t *history_channel(const history *h, size_t index);
/* Reconcile backend cursors with locally saved successful reads, scoped to
 * the normalized instance URL and verified account UUID. NULL h checks only
 * the backend snapshot. Notifications/counts do not override reading. */
bool history_channel_unread(const history *h, const json_t *channel);
/* Schedule a coalesced read of the active, actually visible latest message.
 * NULL cancels a queued read when the view loses visibility/focus. */
void history_read_visible(history *h, const char *message_id);
size_t history_selected(const history *h);
size_t history_message_count(const history *h);
json_t *history_message(const history *h, size_t index);
/* Changes when the loaded active conversation is cleared or updated. */
uint64_t history_messages_revision(const history *h);
/* Borrowed instance-scoped summaries. Directory reads are dispatched by the
 * history owner through the same REST queue, never by the popup. */
void history_users_active(history *h, bool active);
void history_users_retry(history *h);
void history_users_refresh(history *h);
size_t history_user_count(const history *h);
uint64_t history_users_revision(const history *h);
json_t *history_user(const history *h, size_t index);
const char *history_users_notice(const history *h);
const char *history_mention_name(void *context, const char *id);
const char *history_author(const history *h, const char *id);
const char *history_status(const history *h);
/* Independent latest read/event notices. Empty text clears that context. */
const char *history_notice(const history *h, history_notice_kind kind, uint64_t *revision);
const char *history_event_compact(const history *h);
/* Copies remote labels into a bounded buffer; render with text_label(). */
void history_typing_text(const history *h, char *text, size_t capacity);
/* Actual editor mutation only. Nonblocking WS, throttled per active channel. */
void history_typing_activity(history *h);
bool history_loading(const history *h);
bool history_has_more(const history *h);
bool history_evicted(const history *h);
/* UUID validation protects endpoint interpolation. */
bool history_uuid(const char *value);
#endif
