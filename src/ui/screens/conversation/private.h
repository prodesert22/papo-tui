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
#include "ui/components/help.h"
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
/* Server tabs take the top rows, like on the sign-in screen: one row on wide
 * screens, plus the [Canais]/[Conversa] switch on narrow ones. The frame starts
 * below them. The focus label lives in the footer. */
#define TABS_ROWS (COLS >= 80 ? 1 : 2)
#define CONTENT_Y (TABS_ROWS + 2)

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
    PINS_BUTTON,
    VOICE_OUTPUT_BUTTON,
    VOICE_MICROPHONE_BUTTON,
    VOICE_LEAVE_BUTTON,
    VOICE_PTT_BUTTON
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
/* A cell of the history in message coordinates: the line within the message,
 * counted from its first row, and the body column. Survives scrolling and
 * pages loaded before it. */
typedef struct {
    char id[37];
    int line, column;
} history_point;
/* What the last paint showed on one history row. */
typedef struct {
    char id[37];
    int line;
} history_row;
typedef struct {
    char id[37];
    int kind, y, x, height, width;
    size_t index;
} message_hit;
typedef struct {
    struct member_drawer *members;
    bool members_open;
    bool delete_confirm;
    char edit_message[37];
    text_buffer edit_input;
    /* The last submitted edit: the editor closes at once and the history shows
     * the text until the server answers. A failed edit keeps its text for the
     * next edit of that message. */
    char edit_sent[37], edit_sent_text[TEXT_UTF8_BYTES];
    bool edit_sent_failed;
    /* Owned copy of the history message with the pending edit's text, keyed by
     * the model's message revision and the text's signature. */
    json_t *edit_view;
    uint64_t edit_view_revision, edit_view_signature;
    bool navigating, context_open, context_keyboard, reactors_open, reaction_picker,
        reactor_users_focus;
    char selected_message[37], action_channel[37], reaction_target[37];
    char dm_context_id[37];
    char channel_context_id[37];
    /* The context menu belongs to server tab server_context_index. */
    bool server_context;
    size_t server_context_index;
    int channel_menu; /* 0 context actions, 1 mute duration, 2 notifications. */
    char centered_message[37];
    char notification_channel[37], notification_message[37];
    char notification_time[65], notification_hint[65];
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
    bool settings_presented;
    help_modal help;
    bool list_dms, initial_channel_pending;
    /* Conversation to open first: the one shown last on this server. The
     * initial channel waits briefly for it before falling back to the first. */
    char restore_conversation[37];
    uint64_t restore_deadline;
    char saved_conversation[37]; /* Last conversation recorded in view_state. */
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
    click_range voice_output_hit, voice_microphone_hit, voice_leave_hit, voice_ptt_hit;
    click_range tab_hits[INSTANCE_LIMIT], add_hit, back_hit, next_hit, channels_hit,
        conversation_hit, search_hit, pins_hit, attach_button_hit, emoji_button_hit;
    char anchor[37];
    char editor_channel[64];
    /* Optimistic history entries for unconfirmed sends, owned; rebuilt when
     * an operation of the open conversation changes state. */
    json_t *pending;
    uint64_t pending_signature;
    const char *feedback;
    status_bar status;
    uint64_t notice_revisions[HISTORY_NOTICE_COUNT];
    uint64_t voice_revision;
    uint64_t copy_revision;
    uint64_t retry_id;
    /* The confirmation asks to remove the server tab remove_index instead of
     * resending a message. */
    bool remove_server;
    size_t remove_index;
    uint64_t unseen;
    int waiting_page;
    bool paging_notice;
    uint64_t menu_permissions;
    int menu_cached[8], menu_cached_count;
    size_t section, offset;
    bool at_end, paste, show_channels, layout_failed, duplicate_modal, reenviar;
    bool dragging, editor_scrolling;
    /* Mouse text selection in the history: dragging while the button is held,
     * shown until another click. Rows are owned and sized to the history. */
    history_point select_anchor, select_head;
    /* Anchor and head in history order, resolved before each paint. */
    history_point select_from, select_to;
    size_t select_from_index, select_to_index;
    bool select_dragging, select_shown, select_mapping;
    history_row *select_rows;
    int select_row_count;
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
help_context conversation_help_context(view *ui, const char **area);
const char *conversation_str(const json_t *obj, const char *key);
size_t conversation_list_index(view *ui, size_t row);
size_t conversation_list_count(view *ui);
size_t conversation_list_visual_row(view *ui, size_t channel);
size_t conversation_list_visual_hit(view *ui, size_t row, const char **participant);
int conversation_list_rows(view *ui);
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
void conversation_draw_tabs(view *ui);
void conversation_release_layout(view *ui, cached_layout *c);
cached_layout *conversation_find_layout(view *ui, size_t index, int width);
/* History messages followed by optimistic entries on the latest page, with a
 * pending edit's text over its message. Only the walk and scrolling see these
 * entries; selection and actions use the model. */
size_t conversation_entry_count(view *ui);
json_t *conversation_entry(view *ui, size_t index);
bool conversation_entry_pending(view *ui, size_t index);
void conversation_account_layout(view *ui, cached_layout *c);
int conversation_walk(view *ui, int first, bool paint, bool save, int *anchor);
/* Builds released blocks again; NULL when the message cannot be laid out. */
message_blocks *conversation_entry_blocks(view *ui, size_t index, cached_layout *cached, int width);
/* History text selection with the mouse; copied when the button is released. */
void conversation_selection_rows(view *ui);
void conversation_selection_row(view *ui, int row, const char *id, int line);
void conversation_selection_paint(view *ui, size_t index, const message_row *r, int y);
bool conversation_selection_begin(view *ui, int y, int x);
void conversation_selection_drag(view *ui, int y, int x, bool released);
void conversation_selection_clear(view *ui);
void conversation_selection_destroy(view *ui);
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
/* Saving an edit without text asks whether to delete the message instead.
 * False when deletion is not allowed; Cancel returns to the open edit. */
bool conversation_confirm_delete_edit(view *ui);
void conversation_submit(view *ui, uint64_t retry_id, bool duplicate);
void conversation_close_confirmation(view *ui);
void conversation_confirm_remove(view *ui, size_t index);
/* Opens the context menu of server tab index, below the tab. */
void conversation_open_server_menu(view *ui, size_t index, bool keyboard);
bool conversation_poll_editor(view *ui, auth_client *auth);
bool conversation_poll_model(view *ui, auth_client *auth);
bool conversation_edit(view *ui, int type, uint32_t key);
void conversation_dispose_media(void *v);
void conversation_update_media(void *v, const json_t *event);
void conversation_idle_media(void *v);
/* Returns true when the user confirmed removing ui->remove_index. */
bool conversation_handle_confirmation(view *ui, int type, uint32_t key, bool enter);
const GPtrArray *conversation_attachments(view *ui);
bool conversation_attachments_poll(view *ui, auth_client *auth);
void conversation_attachments_open(view *ui, bool clipboard);
void conversation_attachments_close(view *ui);
bool conversation_attachments_key(view *ui, int type, uint32_t key);
bool conversation_attachments_mouse(view *ui, const MEVENT *event);
void conversation_members_toggle(view *ui);
/* Opens the profile card of user id, without the drawer when it is closed. */
void conversation_members_profile(view *ui, const char *id);
void conversation_members_destroy(view *ui);
bool conversation_members_poll(view *ui);
bool conversation_members_key(view *ui, int type, uint32_t key);
bool conversation_members_mouse(view *ui, const MEVENT *event);
void conversation_members_draw(view *ui);
bool conversation_members_modal(view *ui);
help_context conversation_members_help(view *ui, const char **area);
text_buffer *conversation_members_input(view *ui);
bool conversation_members_cursor(view *ui);
void conversation_attachments_draw_row(view *ui);
WINDOW *conversation_attachments_draw_dialog(view *ui);

#endif
