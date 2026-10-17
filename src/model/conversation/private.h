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

enum request_kind { NONE, CHANNELS, MESSAGES, AUTHORS, USERS, EMOJIS, PROFILES, INBOX, ACTIONS };
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
typedef struct {
    json_t *ids;
    bool ok;
} protected_users;
struct history {
    struct history_actions *actions;
    GHashTable *costs;
    uint64_t cost_sequence, cost_sweep, maintenance, directory_used;
    void (*media_update)(void *, const json_t *);
    void *media_context;
    auth_client *auth;
    chat_assets *assets;
    inbox *inbox;
    bool r8, active;
    uint64_t activity_sent;
    char read_attempt[37], read_visible[37];
    uint64_t read_after;
    uint64_t read_save_after;
    char read_url[2049], read_user[37];
    bool read_loaded, read_dirty, read_save_failed;
    json_t *users, *directory, *queried, *summary_ids;
    size_t user_bytes;
    uint64_t users_revision, messages_revision;
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
    uint64_t context, selection, request_context, request_selection;
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
bool history_internal_cache_user(history *h, json_t *u, bool presence);
void history_internal_typing_remove(history *h, const char *id);
bool history_internal_typing_event(history *h, const json_t *event);
bool history_internal_presence_event(history *h, const json_t *event);
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
bool history_internal_request_authors(history *h);

bool history_internal_actions_mutating(history *h);
void history_internal_actions_reset(history *h);
void history_internal_actions_destroy(history *h);
bool history_internal_actions_dispatch(history *h, bool foreground);
void history_internal_actions_response(history *h, json_t *body, long code, bool uncertain);
void history_internal_actions_users(history *h, json_t *ids, json_t *seen);
void history_internal_reference_event(history *h, const json_t *event);
bool history_internal_react_event(history *h, const json_t *event);
#endif
