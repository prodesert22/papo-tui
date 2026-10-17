#ifndef PAPO_HISTORY_H
#define PAPO_HISTORY_H
#include "model/assets/catalog.h"
#include "model/notifications/inbox.h"
#include "model/text/mentions.h"
#include "net/auth/client.h"

#define HISTORY_CHANNEL_LIMIT 500
#define HISTORY_MESSAGE_LIMIT 1000
#define HISTORY_BYTE_LIMIT (4U * 1024U * 1024U)

typedef struct history history;
typedef enum {
    HISTORY_NOTICE_READ,
    HISTORY_NOTICE_EVENT,
    HISTORY_NOTICE_MUTATION,
    HISTORY_NOTICE_COUNT
} history_notice_kind;
/* Owns all JSON models; borrows the auth client. One active conversation, no
 * inactive history cache. Returned objects/strings are borrowed until poll,
 * event, confirmation, selection, reload or destroy. Callers retain anchors
 * by UUID, not pointer. */
history *history_create(auth_client *auth);
/* Valid live embed updates also invalidate media in inactive conversations. */
void history_set_media_updates(history *h, void (*update)(void *, const json_t *), void *context);
void history_destroy(history *h);
chat_assets *history_assets(history *h);
bool history_message_webhook(const json_t *message);
const char *history_message_author(const history *h, const json_t *message);
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
/* Instance-scoped message references and interaction requests. Borrowed JSON. */
json_t *history_find_message(history *h, const char *id);
void history_reference_request(history *h, const char *id);
const char *history_reference_notice(history *h);
uint64_t history_reference_revision(history *h);
bool history_jump(history *h, const char *id);
/* A known message timestamp allows a bounded cursor lookup in the active channel. */
bool history_jump_at(history *h, const char *id, const char *created_at);
void history_cancel_jump(history *h);
const char *history_jump_error(history *h);
bool history_take_jump(history *h, char id[37]);
bool history_reaction_valid(const json_t *emoji);
bool history_reaction_equal(const json_t *a, const json_t *b);
bool history_reaction_mine(const json_t *message, const json_t *emoji);
/* Queue one mutation through the shared asynchronous REST owner. DELETE is
 * authorized by the server; edits additionally require a loaded own message.
 * content is copied. Results are consumed once and scoped to the active channel/session. */
bool history_edit_message(history *h, const char *message, const char *content);
bool history_delete_message(history *h, const char *message);
bool history_take_edit_result(history *h, char message[37], bool *success);
bool history_react(history *h, const char *message, const json_t *emoji, bool remove);
void history_reactors_open(history *h, const char *message);
void history_reactors_close(history *h);
void history_reactors_more(history *h);
json_t *history_reactors(history *h);
const char *history_reactors_notice(history *h);
bool history_interactions_retry(history *h);
bool history_recovering(const history *h);
uint64_t history_insertions(const history *h);
void history_reload_channels(history *h);
bool history_select(history *h, size_t channel);
void history_recent(history *h);
/* Queue pages through the existing REST dispatcher, even while an auxiliary
 * read is pending. Failures require history_retry(), with no automatic replay. */
void history_older(history *h);
void history_newer(history *h);
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
uint64_t history_message_revision(history *h, size_t index);
uint64_t history_messages_revision(const history *h);
/* Borrowed instance-scoped summaries. Directory reads are dispatched by the
 * history owner through the same REST queue, never by the popup. */
/* Owned GLib string; caller g_free(). Comparison only, never wire identity. */
char *history_search_normalize(const char *text);
bool history_maintain(history *h, uint64_t now);
void history_users_active(history *h, bool active);
void history_users_retry(history *h);
void history_users_refresh(history *h);
size_t history_user_count(const history *h);
uint64_t history_users_revision(const history *h);
json_t *history_user(const history *h, size_t index);
const char *history_users_notice(const history *h);
const char *history_mention_name(void *context, const char *id);
const char *history_author(const history *h, const char *id);
const char *history_author_username(const history *h, const char *id);
const char *history_status(const history *h);
/* Independent latest read, event and mutation notices. Empty text clears that context. */
const char *history_notice(const history *h, history_notice_kind kind, uint64_t *revision);
const char *history_event_compact(const history *h);
/* Copies remote labels into a bounded buffer; render with text_label(). */
void history_typing_text(const history *h, char *text, size_t capacity);
/* Actual editor mutation only. Nonblocking WS, throttled per active channel. */
void history_typing_activity(history *h);
bool history_loading(const history *h);
/* -1: older page, +1: newer page, 0: no queued/in-flight pagination. */
int history_paging_direction(const history *h);
bool history_has_more(const history *h);
bool history_has_newer(const history *h);
bool history_evicted(const history *h);
/* UUID validation protects endpoint interpolation. */
bool history_uuid(const char *value);
#endif
