#ifndef PAPO_SERVER_ADMIN_H
#define PAPO_SERVER_ADMIN_H
#include "model/conversation/history.h"
#include "model/permissions.h"
/* Server administration for the signed-in account on this instance: the
 * server object, roles, channels, emojis, members and the audit log.
 * Event-loop only. Requests go through history's REST dispatcher one at a
 * time, share the global request budget and are never replayed. Roles and
 * channels are read from the permission snapshot that auth keeps; members
 * come from the user directory. */
typedef enum { ADMIN_IDLE, ADMIN_SAVING, ADMIN_SAVED, ADMIN_FAILED, ADMIN_UNCERTAIN } admin_state;
typedef enum {
    ADMIN_TAB_OVERVIEW,
    ADMIN_TAB_ROLES,
    ADMIN_TAB_CHANNELS,
    ADMIN_TAB_EMOJIS,
    ADMIN_TAB_WEBHOOKS,
    ADMIN_TAB_MEMBERS,
    ADMIN_TAB_AUDIT,
    ADMIN_TABS
} admin_tab;
/* PENDING: the permission it needs is not confirmed yet. */
typedef enum { ADMIN_HIDDEN, ADMIN_SHOWN, ADMIN_PENDING } admin_visibility;
typedef enum { ADMIN_LIST_EMOJIS, ADMIN_LIST_AUDIT, ADMIN_LIST_WEBHOOKS, ADMIN_LISTS } admin_list;
#define ADMIN_SERVER_NAME_MAX 32
#define ADMIN_TOPIC_MAX 512
#define ADMIN_WEBHOOK_NAME_MAX 80
#define ADMIN_WEBHOOK_NICKNAME_MAX 100

/* Pure policy, from the contract's permission checks; the owner holds all. */
admin_visibility server_admin_visibility(const permissions *p, admin_tab tab);
admin_visibility server_admin_tab(history *h, admin_tab tab);
bool server_admin_allowed(history *h, role_permission permission);
/* Validates without queueing: NULL when text is acceptable, else the reason.
 * max counts characters; 0 means only the field's own bound applies. */
const char *server_admin_check_text(const char *text, size_t max, bool required);
/* "" (no color) or #RRGGBB. */
bool server_admin_color_valid(const char *color);
/* PATCH /server body with only the changed fields, or NULL with *problem set.
 * visibility: -1 keeps it, 0 private, 1 public. Making a public server
 * private requires a password; a private one may change it. */
json_t *server_admin_patch_body(const json_t *server, const char *name, int visibility,
                                const char *password, const char **problem);

/* Borrowed permission snapshot: role catalog and channels keyed by ID, the
 * owner ("" when unknown) and the signed-in user. Changes with its revision. */
const json_t *server_admin_roles(history *h);
const json_t *server_admin_channels(history *h);
const char *server_admin_owner(history *h);
const char *server_admin_me(history *h);
uint64_t server_admin_permissions_revision(history *h);
const char *server_admin_permissions_notice(history *h);
/* Reads. Lists are borrowed until the next poll; NULL until loaded. */
void server_admin_load_server(history *h);
const json_t *server_admin_server(history *h);
void server_admin_load(history *h, admin_list list, bool more);
const json_t *server_admin_items(history *h, admin_list list);
bool server_admin_more(history *h, admin_list list);
bool server_admin_loading(history *h, admin_list list);
bool server_admin_failed(history *h, admin_list list);
/* Audit log filters. Empty strings and 0 mean no filter. entity is one of
 * the contract's entity types; action an action name such as role.create;
 * actor a user ID; until an ISO 8601 instant sent to the server. from (UTC
 * epoch ms) is applied here: the contract's since is also the page cursor,
 * so pages stop at the first older event instead. */
typedef struct {
    char entity[16], action[129], actor[37], until[40];
    uint64_t from;
} admin_audit_query;
/* False when a value is invalid; the current filters are kept then. */
bool server_admin_audit_query(history *h, const admin_audit_query *query);
const admin_audit_query *server_admin_audit_current(history *h);
/* Action names seen in loaded events since the panel opened, sorted. */
const json_t *server_admin_audit_actions(history *h);
/* Pure: whether the query's values are acceptable. */
bool server_admin_audit_valid(const admin_audit_query *query);
/* Keeps the member directory loaded while the Membros tab is shown. */
void server_admin_members_active(history *h, bool active);
/* Drops the lists and the reset link when the modal closes. */
void server_admin_close(history *h);

/* Mutations; false when one is already in flight or the input is invalid.
 * Each sets the notice; base64 strings are owned (g_free) on every path. */
bool server_admin_patch(history *h, json_t *body);
bool server_admin_set_icon(history *h, char *base64, const char *format);
bool server_admin_remove_icon(history *h);
bool server_admin_role_save(history *h, const char *id, const char *name, const char *color,
                            const bool flags[PERMISSION_ROLE_COUNT]);
bool server_admin_role_delete(history *h, const char *id);
bool server_admin_channel_create(history *h, const char *name, const char *type, const char *topic);
bool server_admin_channel_save(history *h, const char *id, const char *name, const char *topic);
bool server_admin_channel_delete(history *h, const char *id);
bool server_admin_channel_move(history *h, const char *id, int from, int to);
bool server_admin_channel_role(history *h, const char *channel, const char *role,
                               const bool flags[PERMISSION_CHANNEL_COUNT]);
bool server_admin_channel_role_remove(history *h, const char *channel, const char *role);
bool server_admin_emoji_create(history *h, const char *name, char *base64, const char *format);
bool server_admin_emoji_delete(history *h, const char *id);
bool server_admin_emoji_deletable(history *h, const json_t *emoji);
/* Webhooks (manage_webhooks). Creating one answers with its access token, shown
 * once: it is kept only in memory, for that webhook, until the modal closes or
 * the account looks at something else, and never goes to a notice or a log.
 * An empty nickname returns to the webhook's name. */
bool server_admin_webhook_create(history *h, const char *channel, const char *name);
bool server_admin_webhook_nickname(history *h, const char *id, const char *nickname);
bool server_admin_webhook_avatar(history *h, const char *id, char *base64, const char *format);
bool server_admin_webhook_avatar_remove(history *h, const char *id);
bool server_admin_webhook_revoke(history *h, const char *id);
/* Token of webhook id from its creation; "" when it is not held. */
const char *server_admin_webhook_token(history *h, const char *id);
/* Whether the creation's answer for webhook id came without a usable token. */
bool server_admin_webhook_token_lost(history *h, const char *id);
/* Whether the token (or its absence) held now belongs to webhook id. */
bool server_admin_webhook_held(history *h, const char *id);
/* ID of the webhook just created whose form was not shown yet; "" otherwise. */
const char *server_admin_webhook_created(history *h);
void server_admin_webhook_created_clear(history *h);
/* Base address of this instance, borrowed. */
const char *server_admin_instance_url(history *h);
/* Drops the held token. */
void server_admin_webhook_forget_token(history *h);
bool server_admin_member_role(history *h, const char *user, const char *role, bool assign);
bool server_admin_member_ban(history *h, const char *user, bool ban);
bool server_admin_member_reset(history *h, const char *user);
/* Whether the account may ban (or reset) that member: never itself or the owner. */
bool server_admin_member_bannable(history *h, const char *user);
/* Link from the last reset of user; "" otherwise. */
const char *server_admin_reset_url(history *h, const char *user);
const char *server_admin_reset_expires(history *h, const char *user);

admin_state server_admin_state(history *h);
bool server_admin_busy(history *h);
/* Last outcome in Portuguese, sanitized; "" when there is none. */
const char *server_admin_notice(history *h);
/* Changes whenever data, state or notice changes. */
uint64_t server_admin_revision(history *h);
#endif
