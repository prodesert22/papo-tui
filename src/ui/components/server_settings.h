#ifndef PAPO_SERVER_SETTINGS_H
#define PAPO_SERVER_SETTINGS_H
#include "model/conversation/server_admin.h"
#include "ui/components/tabs.h"
#include "ui/input/text.h"
#include "ui/media/icons.h"
#include <ncurses.h>
/* Configurações do servidor: one tab per category, shown only when the
 * signed-in account may act there. The modal borrows the active server's
 * model while open and owns its window, drafts and hit boxes. */
typedef enum {
    SERVER_PICK_NONE,
    SERVER_PICK_ICON,
    SERVER_PICK_EMOJI,
    SERVER_PICK_WEBHOOK
} server_settings_pick;
#define SERVER_SETTINGS_FIELDS 512
#define SERVER_SETTINGS_ROWS 128
typedef struct {
    bool open;
    history *model;
    icon_view *icons; /* Borrowed from the screen; paints emoji previews. */
    char server[129];
    admin_tab tabs[ADMIN_TABS], tab;
    int tab_count;
    bool pending; /* Some tab waits for its permission. */
    int focus;    /* 0 tabs, 1 list, 2 form, 3 close button. */
    tab_strip strip;
    WINDOW *window;
    char error[192], feedback[192];
    uint64_t seen_admin, seen_permissions, seen_users, filled;
    /* List pane: the selected entity; "" is the "new" row. */
    int list_selected, list_first;
    char selected[40];
    /* Form pane. */
    int field, field_first, confirm;
    char confirm_key[40];
    text_buffer name, aux, secret, extra;
    int name_first, aux_first, secret_first, extra_first;
    char draft_action[129], draft_actor[37];
    bool flags[8];
    int choice, choice2;
    bool dirty, awaiting; /* awaiting: a draft was sent; keep it until confirmed. */
    char draft_for[40];
    admin_tab draft_tab;
    char query[129];
    server_settings_pick pick; /* Taken by the screen, which opens the file picker. */
    char emoji_name[129];      /* Name of the emoji whose image is being picked. */
    char webhook_pick[37];     /* ID of the webhook whose avatar is being picked. */
    /* Geometry and hit boxes from the last draw. */
    int top, bottom, list_x, list_w, form_x, form_w;
    int row_y[SERVER_SETTINGS_ROWS], row_index[SERVER_SETTINGS_ROWS], row_count;
    int field_y[SERVER_SETTINGS_FIELDS], field_index[SERVER_SETTINGS_FIELDS], field_rows;
    int cursor_y, cursor_x;
    bool cursor;
} server_settings;
/* Opens on the first tab the account may use. */
void server_settings_open(server_settings *s, history *model, icon_view *icons, const char *server);
/* Moves the open modal to tab with the item whose ID is selected chosen and
 * its form focused; false when the account may not use that tab. */
bool server_settings_show(server_settings *s, admin_tab tab, const char *selected);
void server_settings_close(server_settings *s);
void server_settings_draw(server_settings *s);
void server_settings_key(server_settings *s, int type, uint32_t key);
/* Follows permission and data changes; true when a redraw is due. */
bool server_settings_poll(server_settings *s);
bool server_settings_cursor(const server_settings *s);
bool server_settings_paste(server_settings *s, text_paste *paste);
#endif
