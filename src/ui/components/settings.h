#ifndef PAPO_SETTINGS_H
#define PAPO_SETTINGS_H
#include "config/keymap.h"
#include "config/profiles.h"
#include "model/conversation/profile_edit.h"
#include "model/conversation/user_settings.h"
#include "net/voice/client.h"
#include "ui/components/about.h"
#include "ui/components/tabs.h"
#include "ui/input/text.h"
#include "ui/media/icons.h"
#include <ncurses.h>
/* Settings open on the first tab, Perfil. Aparência holds the app modes (this
 * device) and the theme and display of the account on the active server;
 * Notificações holds that account's notification settings; Atalhos lists every
 * key and changes the keys of commands on this device; Conta changes the
 * account's password on the active server; Sobre shows the program's version,
 * license and source link. */
enum {
    SETTINGS_PROFILE,
    SETTINGS_APPEARANCE,
    SETTINGS_NOTIFICATIONS,
    SETTINGS_AUDIO,
    SETTINGS_ACCESSIBILITY,
    SETTINGS_SHORTCUTS,
    SETTINGS_ACCOUNT,
    SETTINGS_ABOUT,
    SETTINGS_SECTIONS
};
/* Perfil fields, in display order. */
enum {
    PROFILE_FIELD_NICKNAME,
    PROFILE_FIELD_STATUS,
    PROFILE_FIELD_DESCRIPTION,
    PROFILE_FIELD_TYPING
};
#define PROFILE_FIELDS 4
typedef enum { SETTINGS_PICK_NONE, SETTINGS_PICK_AVATAR, SETTINGS_PICK_BANNER } settings_pick;
typedef struct {
    bool open, applied, ptt_capture;
    int section, selection, focus;
    app_mode draft;
    language_choice language; /* Draft of this device's interface language. */
    audio_config audio, original_audio;
    voice_client *voice; /* Borrowed; the instance model outlives the modal. */
    int audio_selection, audio_first;
    /* Hit boxes from the last draw. */
    tab_strip tabs;
    WINDOW *window;
    char error[192];
    /* Perfil: the active server's own profile, borrowed while open. */
    history *profile;
    icon_view *icons; /* Borrowed from the screen; paints the avatar and banner preview. */
    char server[129];
    text_buffer fields[PROFILE_FIELDS];
    int field_first[PROFILE_FIELDS], description_first;
    uint64_t filled_loads, seen_revision;
    bool dirty, requested;
    int profile_item, profile_first, status_choice, loaded_status, banner_button;
    int item_y[7], item_rows[7]; /* Rows from the last draw; -1 when hidden. */
    settings_pick pick;          /* Taken by the view, which opens the file picker. */
    int cursor_y, cursor_x;
    bool cursor;
    /* Conta: the new password and its confirmation, masked and erased on close. */
    auth_client *auth; /* Borrowed, like profile. */
    text_buffer account[2];
    int account_first[2], account_item, account_y[2];
    int profile_bad; /* 1 + the profile field refused, shown in red until edited; 0 none. */
    int account_bad; /* 1 + the password field refused, shown in red until edited; 0 none. */
    bool account_confirm, account_sent;
    unsigned account_seen; /* Outcome shown in the last draw. */
    /* Account settings: the draft, the server copy it was filled from, and
     * whether a save of it is on its way. */
    user_config user, user_base;
    uint64_t user_revision;
    bool user_sent;
    int appearance_first, notify_selection;
    /* Atalhos: the draft of every command's keys, its search and list, the
     * menu of the selected command, a key being captured for it, a key of
     * another command waiting to be moved and the reset confirmation. */
    keymap_table keys;
    text_buffer keys_search;
    int keys_search_first, keys_selection, keys_first, keys_menu_item, keys_menu_y, keys_menu_x;
    bool keys_list, keys_menu, keys_capture, keys_add, keys_reset_confirm;
    keymap_action keys_action, keys_conflict;
    key_binding keys_pending;
    /* Acessibilidade: the draft of this device's choices and the item selected. */
    accessibility_config access;
    int access_selection, access_first;
    /* Sobre: the facts drawn and whether the link copy was just requested. */
    about_view about;
    bool about_copied;
} app_settings;
void settings_open(app_settings *settings, const profile_config *config, voice_client *voice,
                   history *profile, auth_client *auth, icon_view *icons, const char *server);
void settings_close(app_settings *settings);
void settings_draw(app_settings *settings);
void settings_key(app_settings *settings, profile_config *config, int type, uint32_t key);
/* Refreshes the Perfil fields from the server copy; true when a redraw is due. */
bool settings_poll(app_settings *settings);
/* Places the terminal cursor in the focused Perfil or Conta field. */
bool settings_cursor(const app_settings *settings);
/* Inserts pasted text into the focused Perfil or Conta field; false when none is focused. */
bool settings_paste(app_settings *settings, text_paste *paste);
#endif
