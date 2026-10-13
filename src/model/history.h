#ifndef PAPO_HISTORY_H
#define PAPO_HISTORY_H
#include "net/auth.h"

#define HISTORY_CHANNEL_LIMIT 500
#define HISTORY_MESSAGE_LIMIT 1000
#define HISTORY_BYTE_LIMIT (4U * 1024U * 1024U)

typedef struct history history;
/* Owns all JSON models; borrows the auth client. One active conversation, no
 * inactive history cache. Returned objects/strings are borrowed until poll,
 * selection, reload or destroy. Callers retain anchors by UUID, not pointer. */
history *history_create(auth_client *auth);
void history_destroy(history *h);
bool history_poll(history *h);
/* Copies a confirmed Message. Merges only the active channel/current
 * generation, with definitive ID deduplication and bounded memory. No HTTP. */
bool history_confirm(history *h, json_t *message, uint64_t generation);
void history_reload_channels(history *h);
bool history_select(history *h, size_t channel);
void history_recent(history *h);
void history_older(history *h);
/* Returns true when retrying in place; false when reloading the recent page
 * or channel list, so the view can reset its anchor deliberately. */
bool history_retry(history *h);
size_t history_channel_count(const history *h);
json_t *history_channel(const history *h, size_t index);
size_t history_selected(const history *h);
size_t history_message_count(const history *h);
json_t *history_message(const history *h, size_t index);
const char *history_author(const history *h, const char *id);
const char *history_status(const history *h);
bool history_loading(const history *h);
bool history_has_more(const history *h);
bool history_evicted(const history *h);
/* UUID validation protects endpoint interpolation. */
bool history_uuid(const char *value);
#endif
