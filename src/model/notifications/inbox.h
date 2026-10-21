#ifndef PAPO_INBOX_H
#define PAPO_INBOX_H
#include "net/auth/client.h"
typedef struct inbox inbox;
/* Owns bounded JSON copies. Borrows auth; all methods run on the event loop.
 * The history owner alone dispatches/takes REST responses and WS events. */
inbox *inbox_create(auth_client *auth);
void inbox_destroy(inbox *box);
void inbox_reset(inbox *box);
bool inbox_event(inbox *box, const json_t *event);
void inbox_recover(inbox *box);
bool inbox_dispatch(inbox *box);
bool inbox_response(inbox *box, json_t *body, long code, bool uncertain);
bool inbox_pending(const inbox *box);
void inbox_cancel_read(inbox *box);
size_t inbox_dm_count(const inbox *box);
json_t *inbox_dm(const inbox *box, size_t index);
bool inbox_open_dm(inbox *box, const char *user);
bool inbox_close_dm(inbox *box, const char *id);
const char *inbox_opened(const inbox *box);
bool inbox_opening(const inbox *box);
void inbox_opened_clear(inbox *box);
void inbox_resolve_dm(inbox *box, const char *id);
/* Cancel queued opening and ignore late navigation receipts; sent mutations may finish. */
void inbox_cancel_open(inbox *box);
void inbox_refresh(inbox *box);
void inbox_older(inbox *box);
void inbox_retry(inbox *box);
size_t inbox_notification_count(const inbox *box);
json_t *inbox_notification(const inbox *box, size_t index);
size_t inbox_event_count(const inbox *box);
json_t *inbox_notification_event(const inbox *box, size_t index);
bool inbox_has_more(const inbox *box);
size_t inbox_unread(const inbox *box);
/* Activity badges exclude notification records covered by successful reads.
 * Reading a conversation does not mark its notification-center records read. */
bool inbox_has_activity(const inbox *box);
void inbox_read_through(inbox *box, const char *channel, const json_t *cursor);
bool inbox_preferences_poll(inbox *box);
bool inbox_conversation_muted(const inbox *box, const char *channel);
bool inbox_mute_conversation(inbox *box, const char *channel, int seconds);
/* Known explicit mentions, deduplicated by notification/message promotion.
 * NULL channel counts the whole instance; previews may be truncated. */
size_t inbox_unread_mentions(const inbox *box, const char *channel);
/* Cache bounded display metadata from an already validated active-history/WS
 * message. Borrows input; does not change notification or conversation reads. */
void inbox_observe_message(inbox *box, const json_t *message);
/* Bounded author/time metadata observed in this instance; borrowed JSON. */
json_t *inbox_message_preview(const inbox *box, const char *message_id);
void inbox_view(inbox *box, const char *notification_id, bool event);
void inbox_view_message(inbox *box, const char *message_id);
void inbox_read_conversation(inbox *box, const char *channel_id);
bool inbox_conversation_unseen(const inbox *box, const char *channel_id);
bool inbox_message_viewed(const inbox *box, const char *message_id);
size_t inbox_unseen_conversations(const inbox *box);
const char *inbox_notice(const inbox *box);
const char *inbox_presence(const inbox *box, const char *user);
const json_t *inbox_presence_info(const inbox *box, const char *user);
const char *inbox_message_channel(const inbox *box, const char *message_id);
/* Transfers a new WS notification for desktop eligibility. Never snapshots. */
json_t *inbox_take_alert(inbox *box);
uint64_t inbox_revision(const inbox *box);
#endif
