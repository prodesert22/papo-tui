#ifndef PAPO_ASSETS_H
#define PAPO_ASSETS_H
#include "net/auth/client.h"
#include <jansson.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct chat_assets chat_assets;
typedef struct {
    char id[37], name[129], format[8], created[65];
    char *path;
    size_t bytes;
    uint64_t revision, seen;
} server_emoji;
typedef struct {
    char id[37];
    char checksum[65];
    char *path;
    size_t bytes;
    uint64_t revision, used, version;
    const char *url; /* Borrowed descriptor; the shared image cache owns transfers. */
    bool requested, dirty, failed;
} author_avatar;
chat_assets *chat_assets_create(void);
void chat_assets_destroy(chat_assets *a);
void chat_assets_emojis_request(chat_assets *a, bool refresh);
bool chat_assets_emojis_wanted(const chat_assets *a);
const char *chat_assets_emojis_path(chat_assets *a);
bool chat_assets_emojis_response(chat_assets *a, json_t *response, long status);
size_t chat_assets_emoji_count(const chat_assets *a);
const server_emoji *chat_assets_emoji(const chat_assets *a, size_t index);
const server_emoji *chat_assets_emoji_id(const chat_assets *a, const char *id);
const char *chat_assets_notice(const chat_assets *a);
uint64_t chat_assets_revision(const chat_assets *a);
/* Avatar downloads do not alter the text/emoji layout of cached messages. */
uint64_t chat_assets_emoji_revision(const chat_assets *a);
/* Session-owned entries keyed by user UUID (up to 10,000). A visible uncached
 * author queues a bounded profile read. Repaints/scrolling reuse the entry;
 * avatar_update schedules replacement without discarding the current image. */
const author_avatar *chat_assets_avatar(chat_assets *a, const char *id);
/* Public webhook attribution; visible URLs share a bounded per-session cache.
 * No I/O in this accessor; the shared image cache owns asynchronous transfers. */
const author_avatar *chat_assets_webhook_avatar(chat_assets *a, const json_t *message);
void chat_assets_avatar_invalidate(chat_assets *a, const char *id);
json_t *chat_assets_profiles_request(chat_assets *a);
bool chat_assets_profiles_wanted(const chat_assets *a);
bool chat_assets_profiles_response(chat_assets *a, json_t *response, long status);
void chat_assets_cancel(chat_assets *a);
void chat_assets_visible_begin(chat_assets *a);
void chat_assets_profiles_retry(chat_assets *a);
#endif
