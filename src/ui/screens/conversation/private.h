#ifndef PAPO_CONVERSATION_PRIVATE_H
#define PAPO_CONVERSATION_PRIVATE_H
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "app/desktop.h"
#include "app/session_store.h"
#include "config/profiles.h"
#include "model/conversation/discovery.h"
#include "model/conversation/history.h"
#include "model/conversation/members.h"
#include "net/clock.h"
#include "platform/clipboard.h"
#include "platform/file_transfer.h"
#include "platform/runtime.h"
#include "platform/unicode.h"
#include "ui/components/button.h"
#include "ui/components/emoji_picker.h"
#include "ui/components/file_picker.h"
#include "ui/components/mention_popup.h"
#include "ui/components/settings.h"
#include "ui/components/status_bar.h"
#include "ui/components/terminal_title.h"
#include "ui/components/theme.h"
#include "ui/input/editor.h"
#include "ui/input/mouse.h"
#include "ui/input/text.h"
#include "ui/media/graphics.h"
#include "ui/media/icons.h"
#include "ui/media/media.h"
#include "ui/media/media_service.h"
#include "ui/screens/conversation.h"
#include "ui/screens/inbox.h"
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wctype.h>

#define PASTE_START (KEY_MAX + 1)
#define PASTE_END (KEY_MAX + 2)
#define CONTENT_Y 5

enum {
    SERVER_TABS,
    CHANNEL_LIST,
    HISTORY_PANEL,
    MESSAGE_INPUT,
    EMOJI_BUTTON,
    LIST_BUTTONS,
    SETTINGS_BUTTON,
    ATTACHMENT_ROW,
    ATTACH_BUTTON,
    MEMBERS_BUTTON,
    MEMBERS_PANEL,
    SEARCH_BUTTON,
    PINS_BUTTON
};
typedef enum {
    DRAW_ALL,
    DRAW_CONVERSATION,
    DRAW_NAVIGATION,
    DRAW_EDITOR,
    DRAW_MEDIA,
    DRAW_FOOTER
} draw_scope;
#define LAYOUT_BYTES (8U * 1024U * 1024U)
typedef struct {
    int x, width;
} click_range;
typedef struct {
    char id[37];
    uint64_t revision, names, dimensions, used;
    int width, height;
    bool graphics, tail_gap, continuation;
    int cell_width, cell_height, calendar_day;
    uint64_t assets;
    uint64_t reply_revision;
    size_t bytes;
    message_blocks blocks;
} cached_layout;
enum { MESSAGE_HIT, REPLY_HIT, REACTION_HIT };
typedef struct {
    char id[37];
    int kind, y, x, height, width;
    size_t index;
} message_hit;
typedef struct {
    struct member_drawer *members;
    bool members_open;
    bool delete_confirm, edit_pending;
    char edit_message[37], edit_submitted[TEXT_UTF8_BYTES];
    text_buffer edit_input;
    bool navigating, context_open, context_keyboard, reactors_open, reaction_picker,
        reactor_users_focus;
    char selected_message[37], action_channel[37], reaction_target[37];
    char dm_context_id[37];
    char channel_context_id[37];
    int channel_menu; /* 0 context actions, 1 mute duration, 2 notifications. */
    char centered_message[37];
    char notification_channel[37], notification_message[37];
    char notification_time[65];
    char reactor_emoji[37], reactor_unicode[65];
    uint64_t action_generation;
    size_t selected_media;
    int return_focus, menu_selected, menu_x, menu_y, selection_pending;
    int reactor_first[2], reactor_cursor, reactor_user;
    WINDOW *context_window;
    message_hit hits[2048];
    size_t hit_count;
    int selected_start, selected_end;
    GHashTable *layout_index;
    cached_layout layouts[HISTORY_MESSAGE_LIMIT];
    /* Reused on the UI thread; the view owns this large measurement buffer. */
    text_layout measured;
    size_t layout_bytes;
    uint64_t layout_sequence;
    history *model;
    r8_modal *r8;
    app_settings settings;
    bool list_dms, initial_channel_pending;
    int list_button;
    size_t highlights[2];
    uint64_t inbox_revision;
    uint64_t view_requested_until;
    mention_popup *mentions;
    icon_view *icons;
    emoji_picker *emojis;
    int emoji_return_focus, calendar_day;
    media_view *media;
    char block[128];
    send_editor *editor; /* Borrowed from the instance connection. */
    instance_connections *connections;
    WINDOW *frame, *tabs, *footer, *list, *body, *confirm, *attachment_bar;
    int members_reserved;
    int width, height, side, visible, first_channel, focus, first, editor_border, history_top,
        editor_rows, editor_padding, attachment_top, attachment_rows;
    size_t highlighted, active_tab, tab_cursor, first_tab;
    click_range members_hit;
    click_range tab_hits[INSTANCE_LIMIT], add_hit, back_hit, next_hit, channels_hit,
        conversation_hit, search_hit, pins_hit, attach_button_hit, emoji_button_hit;
    char anchor[37];
    char editor_channel[64];
    const char *feedback;
    status_bar status;
    uint64_t notice_revisions[HISTORY_NOTICE_COUNT];
    uint64_t copy_revision;
    uint64_t retry_id;
    uint64_t unseen;
    int waiting_page;
    bool paging_notice;
    uint64_t menu_permissions;
    int menu_cached[7], menu_cached_count;
    size_t section, offset;
    bool at_end, paste, show_channels, layout_failed, duplicate_modal, reenviar;
    bool dragging, editor_scrolling;
    file_picker *files;
    GTask *attachment_task;
    GCancellable *attachment_cancel;
    char attachment_channel[64], attachment_notice[256];
    uint64_t attachment_generation, attachment_started, remove_id;
    bool attachment_clipboard, remove_confirm, remove_accept;
    WINDOW *attachment_dialog;
    int attachment_cursor, attachment_first, attachment_count;
    click_range attachment_hits[ATTACHMENT_LIMIT], attachment_remove_hits[ATTACHMENT_LIMIT];
    text_paste paste_data;
} view;
const char *conversation_str(const json_t *obj, const char *key);
size_t conversation_list_index(view *ui, size_t row);
size_t conversation_list_count(view *ui);
json_t *conversation_list_channel(view *ui, size_t row);
bool conversation_list_selectable(view *ui, size_t row);
size_t conversation_list_find(view *ui, size_t row, int direction);
void conversation_list_normalize(view *ui);
bool conversation_list_notified(view *ui, bool dms);
void conversation_notification_dot(view *ui, WINDOW *window, int row, int column, int pair);
void conversation_list_button(view *ui, int button, int column, int width, const char *label);
void conversation_switch_list(view *ui, bool dms);
void conversation_destroy_layout(view *ui);
bool conversation_layout(view *ui);
bool conversation_hit(click_range range, int x);
int conversation_tab_width(view *ui, size_t index, int available);
void conversation_interactions_sync(view *ui);
bool conversation_interactions_key(view *ui, int type, uint32_t key);
bool conversation_interactions_mouse(view *ui, const MEVENT *event);
void conversation_interactions_draw(view *ui);
void conversation_reactors_open(view *ui);
void conversation_reactors_key(view *ui, int type, uint32_t key);
void conversation_reactors_mouse(view *ui, const MEVENT *event);
void conversation_reactors_draw(view *ui, WINDOW *window);
void conversation_interactions_close(view *ui);
void conversation_close_dm(view *ui, const char *id);
void conversation_reply_label(view *ui, const char *id, int width, char *label, size_t capacity);
void conversation_message_hit(view *ui, const char *id, int kind, int y, int x, int height,
                              int width, size_t index);
bool conversation_accept_reaction(view *ui);
const char *conversation_reply_target(view *ui);
void conversation_draw_tabs(view *ui, auth_client *auth);
void conversation_release_layout(view *ui, cached_layout *c);
cached_layout *conversation_find_layout(view *ui, size_t index, int width);
void conversation_account_layout(view *ui, cached_layout *c);
int conversation_walk(view *ui, int first, bool paint, bool save, int *anchor);
int conversation_position(view *ui);
void conversation_scroll_history(view *ui, int delta, bool paginate);
text_buffer *conversation_input(view *ui);
int conversation_input_width(view *ui);
int conversation_input_left(view *ui);
int conversation_input_height(view *ui);
int conversation_input_top(view *ui);
bool conversation_input_row(view *ui, int row);
bool conversation_editor_geometry(view *ui);
bool conversation_collect_notices(view *ui, auth_client *auth);
void conversation_draw_footer(view *ui);
void conversation_draw_confirmation(view *ui);
void conversation_draw(view *ui, auth_client *auth, draw_scope scope);
void conversation_set_focus(view *ui, int focus);
void conversation_open_emojis(view *ui);
void conversation_open_channel(view *ui, bool voice_history);
int conversation_activate_tab(view *ui, size_t index, size_t *selected);
void conversation_cancel_edit(view *ui);
void conversation_submit(view *ui, uint64_t retry_id, bool duplicate);
void conversation_close_confirmation(view *ui);
bool conversation_poll_editor(view *ui, auth_client *auth);
bool conversation_poll_model(view *ui, auth_client *auth);
bool conversation_edit(view *ui, int type, uint32_t key);
void conversation_dispose_media(void *v);
void conversation_update_media(void *v, const json_t *event);
void conversation_idle_media(void *v);
void conversation_handle_confirmation(view *ui, int type, uint32_t key, bool enter);
const GPtrArray *conversation_attachments(view *ui);
bool conversation_attachments_poll(view *ui, auth_client *auth);
void conversation_attachments_open(view *ui, bool clipboard);
void conversation_attachments_close(view *ui);
bool conversation_attachments_key(view *ui, int type, uint32_t key);
bool conversation_attachments_mouse(view *ui, const MEVENT *event);
void conversation_members_toggle(view *ui);
void conversation_members_destroy(view *ui);
bool conversation_members_poll(view *ui);
bool conversation_members_key(view *ui, int type, uint32_t key);
bool conversation_members_mouse(view *ui, const MEVENT *event);
void conversation_members_draw(view *ui);
bool conversation_members_modal(view *ui);
bool conversation_members_cursor(view *ui);
void conversation_attachments_draw_row(view *ui);
WINDOW *conversation_attachments_draw_dialog(view *ui);

#endif
