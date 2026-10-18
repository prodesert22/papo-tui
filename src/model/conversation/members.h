#ifndef PAPO_MEMBERS_H
#define PAPO_MEMBERS_H
#include "model/conversation/history.h"
typedef enum { MEMBER_BLOCK, MEMBER_UNBLOCK, MEMBER_BAN, MEMBER_UNBAN, MEMBER_RESET } member_action;
/* Event-loop only. Accessors borrow until the next poll/reset. This owner uses
 * history's REST dispatcher; no UI code takes authentication responses. */
bool history_member_menu(history *h, const char *id);
void history_member_prepare(history *h);
void history_member_refresh(history *h);
bool history_member_profile_open(history *h, const char *id);
void history_member_close(history *h);
void history_member_view_save(history *h, const char *query, const char *selected);
const char *history_member_view_query(history *h);
const char *history_member_view_selected(history *h);
json_t *history_member_profile(history *h);
const char *history_member_notice(history *h);
const char *history_member_target(history *h);
bool history_member_ready(history *h);
bool history_member_busy(history *h);
bool history_member_blocked(history *h);
bool history_member_banned(history *h);
bool history_member_admin(history *h);
bool history_member_can_ban(history *h);
bool history_member_change(history *h, member_action action);
const char *history_member_reset_url(history *h);
const char *history_member_reset_expires(history *h);
#endif
