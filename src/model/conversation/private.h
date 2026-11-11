#ifndef PAPO_HISTORY_PRIVATE_H
#define PAPO_HISTORY_PRIVATE_H
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "config/read_state.h"
#include "model/conversation/history.h"
#include "model/text/mentions.h"
#include "net/auth/session_time.h"
#include "net/clock.h"
#include "net/retry.h"
#include <curl/curl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum request_kind {
    NONE,
    CHANNELS,
    MESSAGES,
    AUTHORS,
    USERS,
    EMOJIS,
    PROFILES,
    INBOX,
    ACTIONS,
    DISCOVERY,
    MEMBERS,
    CHANNEL_SETTINGS,
    CHANNEL_READ,
    VOICE_ICE_SERVERS,
    PROFILE_EDIT,
    SERVER_ADMIN,
    USER_SETTINGS
};
#define TYPING_LIMIT 128U
#define TYPING_TIMEOUT_MS 6000U
#define TYPING_SEND_MS 2000U
typedef struct {
    json_t *message;
    size_t bytes;
    uint64_t revision, seen;
} message_cost;
typedef struct {
    history *model;
    json_t *ids, *seen;
    bool ok;
} mention_batch;
/* A conversation window kept after another one was selected. dirty: a new
 * message arrived for it, so the latest messages must be fetched on return. */
#define HISTORY_STASH_LIMIT 8U
#define HISTORY_STASH_BYTES (2U * HISTORY_BYTE_LIMIT)
typedef struct {
    char channel[37];
    json_t *messages; /* Owned. */
    size_t bytes;
    bool more, newer_more, evicted, dirty;
    char cursor_time[65], cursor_id[37], newer_time[65], newer_id[37];
    history_reading reading;
    uint64_t used;
} channel_stash;
typedef struct {
    json_t *ids;
    bool ok;
} protected_users;
/* Channel list refresh period for notification preferences changed elsewhere. */
#define SETTINGS_REFRESH_MS 300000U
struct history {
    voice_client *voice;
    struct history_members *members;
    struct history_profile_edit *profile_edit;
    struct history_server_admin *server_admin;
    struct history_user_settings *user_settings;
    bool members_active, admin_users;
    struct history_actions *actions;
    struct history_discovery *discovery;
    GHashTable *costs;
    uint64_t cost_sequence, cost_sweep, maintenance, directory_used;
    void (*media_update)(void *, const json_t *);
    void *media_context;
    auth_client *auth;
    chat_assets *assets;
    inbox *inbox;
    char notification_channel[37], notification_mode[14];
    bool notification_queued;
    bool r8, active;
    uint64_t activity_sent;
    char read_attempt[37], read_visible[37];
    /* PUT /channels/{id}/read in flight; sent_reads maps channel -> last
     * message sent, so a read is never repeated automatically. */
    char read_channel[37], read_message[37], read_time[65];
    json_t *sent_reads;
    uint64_t read_after;
    uint64_t read_save_after;
    char read_url[2049], read_user[37];
    bool read_loaded, read_dirty, read_save_failed;
    json_t *users, *directory, *queried, *summary_ids;
    /* Sets of UUIDs: listed holds every directory entry; unlisted, those a
     * complete pass no longer returned, kept in place so indices borrowed by
     * open panels stay valid until maintenance compacts them; refresh_seen,
     * the entries the running revalidation pass has returned. */
    json_t *listed, *unlisted, *refresh_seen;
    bool users_refreshing;
    uint64_t users_complete_at;
    /* Owned: the filters of recent searches on this account, newest first,
     * without the search text. Kept across conversations, in memory only. */
    json_t *search_recent;
    size_t user_bytes;
    uint64_t users_revision, messages_revision;
    uint64_t role_events, request_role_events;
    uint64_t permission_revision, permission_channels;
    bool users_active, users_complete, users_failed, users_partial;
    char users_time[65], users_id[37], users_notice[192];
    json_t *channels, *messages, *read_messages;
    json_t *journal, *patches, *fresh, *boundary;
    size_t journal_bytes;
    bool replaying, recovering, ascending;
    uint64_t epoch, insertions, retry_at;
    unsigned failures;
    char recovery_time[65], recovery_id[37];
    size_t selected, bytes;
    channel_stash stash[HISTORY_STASH_LIMIT];
    size_t stash_count;
    uint64_t stash_tick;
    history_reading reading, restored;
    bool reading_set, restored_set;
    uint64_t context, selection, request_context, request_selection;
    /* Where reading stopped in the selected conversation when it was selected,
     * and its latest message then, before this visit could read anything. */
    char opened_channel[37], opened_read_id[37], opened_read_time[65];
    char opened_last_id[37], opened_last_time[65];
    /* Periodic channel list refresh for preferences changed on other devices. */
    uint64_t settings_refresh_at;
    bool background_channels, request_background;
    enum request_kind pending;
    bool retry_requested;
    bool want_channels, want_messages, want_authors, older, newer, more, newer_more, evicted;
    enum request_kind failed;
    char cursor_time[65], cursor_id[37], status[192];
    char newer_time[65], newer_id[37], page_time[65], page_id[37];
    char notices[HISTORY_NOTICE_COUNT][192];
    uint64_t notice_sequence, notice_revisions[HISTORY_NOTICE_COUNT];
    const char *event_compact; /* Static diagnostic label, never remote text. */
    struct {
        char id[37];
        uint64_t expires;
    } typing[TYPING_LIMIT];
    size_t typing_count;
    uint64_t typing_sent;
    bool typing_connected;
};

const char *history_internal_str(const json_t *obj, const char *key);
bool history_internal_string_field(json_t *obj, const char *key, size_t limit, bool optional);
message_cost *history_internal_measure_message(history *h, json_t *m, bool changed);
void history_internal_notice(history *h, history_notice_kind kind, const char *value);
void history_internal_status(history *h, const char *value);
void history_internal_event_status(history *h, const char *value, const char *compact);
void history_internal_restore_selection(history *h, const char *id);
/* Records the opening read position once the selection names another channel. */
void history_internal_capture_opening(history *h);
bool history_internal_cache_user(history *h, json_t *u, bool presence);
void history_internal_typing_remove(history *h, const char *id);
bool history_internal_typing_event(history *h, const json_t *event);
bool history_internal_presence_event(history *h, const json_t *event);
bool history_internal_role_event(history *h, const json_t *event);
bool history_internal_users_response(history *h, json_t *body);
bool history_internal_request_users(history *h);
bool history_internal_channels_response(history *h, json_t *body);
bool history_internal_message_valid(json_t *m, const char *channel);
size_t history_internal_encoded_size(json_t *m);
bool history_internal_contains(json_t *array, const char *id);
int history_internal_time_compare(const char *at, const char *bt);
int history_internal_message_compare(json_t *a, json_t *b);
size_t history_internal_find(json_t *array, const char *id);
bool history_internal_trim(history *h, json_t *array, bool oldest);
bool history_internal_upsert(history *h, json_t **array, json_t *value, bool oldest,
                             bool protect_edit);
bool history_internal_apply_patch(history *h, json_t *message, json_t *patch);
bool history_internal_patched(history *h, json_t *message);
bool history_internal_replay(history *h);
void history_internal_clear_journal(history *h);
bool history_internal_messages_response(history *h, json_t *body);
bool history_internal_remember(history *h, const json_t *event);
bool history_internal_recovery_response(history *h, json_t *body);
bool history_internal_authors_response(history *h, json_t *body);
bool history_internal_request_messages(history *h);
/* Cached conversation windows: a new message marks one stale, other changes
 * to its messages drop it; reconnects and reloads drop them all. */
void history_internal_stash_touch(history *h, const char *channel);
void history_internal_stash_forget(history *h, const char *channel);
/* Reaction and pin events name only the message. */
void history_internal_stash_forget_message(history *h, const char *message);
void history_internal_stash_clear(history *h);
bool history_internal_request_authors(history *h);

void history_internal_members_destroy(history *h);
bool history_internal_members_mutating(history *h);
bool history_internal_members_dispatch(history *h);
void history_internal_members_response(history *h, json_t *body, long code, bool uncertain);
void history_internal_members_recover(history *h);
void history_internal_profile_destroy(history *h);
bool history_internal_profile_mutating(history *h);
bool history_internal_profile_dispatch(history *h);
bool history_internal_password_dispatch(history *h);
void history_internal_profile_response(history *h, json_t *body, long code, bool uncertain);
void history_internal_user_settings_destroy(history *h);
bool history_internal_user_settings_dispatch(history *h);
void history_internal_user_settings_response(history *h, json_t *body, long code, bool uncertain);
void history_internal_server_admin_destroy(history *h);
bool history_internal_server_admin_mutating(history *h);
bool history_internal_server_admin_dispatch(history *h);
void history_internal_server_admin_response(history *h, json_t *body, long code, bool uncertain);
void history_internal_discovery_destroy(history *h);
void history_internal_discovery_recover(history *h);
bool history_internal_discovery_dispatch(history *h);
void history_internal_discovery_response(history *h, json_t *body, long code);
bool history_internal_discovery_event(history *h, const json_t *event);
void history_internal_pins_invalidate(history *h);
bool history_internal_actions_mutating(history *h);
void history_internal_actions_reset(history *h);
void history_internal_actions_destroy(history *h);
bool history_internal_actions_dispatch(history *h, bool foreground);
void history_internal_actions_response(history *h, json_t *body, long code, bool uncertain);
void history_internal_actions_users(history *h, json_t *ids, json_t *seen);
/* Adds the authors of referenced messages and the listed reactors to ids. */
bool history_internal_actions_protect(const history *h, json_t *ids);
/* The user's name is known; a placeholder from a role event is not. */
bool history_internal_user_resolved(const history *h, const char *id);
bool history_internal_channel_settings_dispatch(history *h);
void history_internal_channel_settings_response(history *h, json_t *body, long code,
                                                bool uncertain);
void history_internal_notification_reads(history *h);
bool history_internal_remember_read(history *h, json_t *message);
/* The server read position is behind the channel's latest message, or unread
 * notification records remain that a read would clear on the server. */
bool history_internal_read_needed(const history *h, const json_t *channel);
bool history_internal_read_send(history *h, const char *channel, const char *id, const char *time);
bool history_internal_read_dispatch(history *h);
void history_internal_read_response(history *h, long code);
/* Server read position of a channel as (time, id); borrowed strings. */
bool history_internal_server_read(const history *h, const json_t *channel, const char **id,
                                  const char **time);
void history_internal_reference_event(history *h, const json_t *event);
bool history_internal_react_event(history *h, const json_t *event);
#endif
